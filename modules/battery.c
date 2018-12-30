#include "battery.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

#include <poll.h>

#include <sys/stat.h>
#include <fcntl.h>

#include <libudev.h>

#define LOG_MODULE "battery"
#include "../log.h"
#include "../bar.h"

enum state { STATE_FULL, STATE_CHARGING, STATE_DISCHARGING };

struct private {
    struct particle *label;

    int poll_interval;
    char *battery;
    char *manufacturer;
    char *model;
    long energy_full_design;
    long energy_full;

    enum state state;
    long capacity;
    long energy;
    long power;
};

static void
destroy(struct module *mod)
{
    struct private *m = mod->private;
    free(m->battery);
    free(m->manufacturer);
    free(m->model);

    m->label->destroy(m->label);

    free(m);
    module_default_destroy(mod);
}

static struct exposable *
content(struct module *mod)
{
    const struct private *m = mod->private;

    mtx_lock(&mod->lock);

    assert(m->state == STATE_FULL ||
           m->state == STATE_CHARGING ||
           m->state == STATE_DISCHARGING);

    unsigned long energy = m->state == STATE_CHARGING
        ? m->energy_full - m->energy : m->energy;

    double hours_as_float;
    if (m->state == STATE_FULL)
        hours_as_float = 0.0;
    else if (m->power > 0)
        hours_as_float = (double)energy / m->power;
    else
        hours_as_float = 99.0;

    unsigned long hours = hours_as_float;
    unsigned long minutes = (hours_as_float - (double)hours) * 60;

    char estimate[64];
    snprintf(estimate, sizeof(estimate), "%02lu:%02lu", hours, minutes);

    struct tag_set tags = {
        .tags = (struct tag *[]){
            tag_new_string(mod, "name", m->battery),
            tag_new_string(mod, "manufacturer", m->manufacturer),
            tag_new_string(mod, "model", m->model),
            tag_new_string(mod, "state",
                           m->state == STATE_FULL ? "full" :
                           m->state == STATE_CHARGING ? "charging" :
                           m->state == STATE_DISCHARGING ? "discharging" :
                           "unknown"),
            tag_new_int_range(mod, "capacity", m->capacity, 0, 100),
            tag_new_string(mod, "estimate", estimate),
        },
        .count = 6,
    };

    mtx_unlock(&mod->lock);

    struct exposable *exposable = m->label->instantiate(m->label, &tags);

    tag_set_destroy(&tags);
    return exposable;
}

static const char *
readline_from_fd(int fd)
{
    static char buf[4096];

    ssize_t sz = read(fd, buf, sizeof(buf) - 1);
    lseek(fd, 0, SEEK_SET);

    if (sz < 0) {
        LOG_WARN("failed to read from FD=%d", fd);
        return NULL;
    }

    buf[sz] = '\0';
    for (ssize_t i = sz - 1; i >= 0 && buf[i] == '\n'; sz--)
        buf[i] = '\0';

    return buf;
}

static long
readint_from_fd(int fd)
{
    const char *s = readline_from_fd(fd);
    if (s == NULL)
        return 0;

    long ret;
    int r = sscanf(s, "%ld", &ret);
    if (r != 1) {
        LOG_WARN("failed to convert \"%s\" to an integer", s);
        return 0;
    }

    return ret;
}

static int
initialize(struct private *m)
{
    int pw_fd = open("/sys/class/power_supply", O_RDONLY);
    if (pw_fd == -1) {
        LOG_ERRNO("/sys/class/power_supply");
        return -1;
    }

    int base_dir_fd = openat(pw_fd, m->battery, O_RDONLY);
    close(pw_fd);

    if (base_dir_fd == -1) {
        LOG_ERRNO("%s", m->battery);
        return -1;
    }

    {
        int fd = openat(base_dir_fd, "manufacturer", O_RDONLY);
        if (fd == -1) {
            LOG_ERRNO("/sys/class/power_supply/%s/manufacturer", m->battery);
            goto err;
        }

        m->manufacturer = strdup(readline_from_fd(fd));
        close(fd);
    }

    {
        int fd = openat(base_dir_fd, "model_name", O_RDONLY);
        if (fd == -1) {
            LOG_ERRNO("/sys/class/power_supply/%s/model_name", m->battery);
            goto err;
        }

        m->model = strdup(readline_from_fd(fd));
        close(fd);
    }

    {
        int fd = openat(base_dir_fd, "energy_full_design", O_RDONLY);
        if (fd == -1) {
            LOG_ERRNO("/sys/class/power_supply/%s/energy_full_design", m->battery);
            goto err;
        }

        m->energy_full_design = readint_from_fd(fd);
        close(fd);
    }

    {
        int fd = openat(base_dir_fd, "energy_full", O_RDONLY);
        if (fd == -1) {
            LOG_ERRNO("/sys/class/power_supply/%s/energy_full", m->battery);
            goto err;
        }

        m->energy_full = readint_from_fd(fd);
        close(fd);
    }

    return base_dir_fd;

err:
    close(base_dir_fd);
    return -1;
}

static void
update_status(struct module *mod, int capacity_fd, int energy_fd, int power_fd,
              int status_fd)
{
    struct private *m = mod->private;

    long capacity = readint_from_fd(capacity_fd);
    long energy = readint_from_fd(energy_fd);
    long power = readint_from_fd(power_fd);

    const char *status = readline_from_fd(status_fd);
    enum state state;

    if (strcmp(status, "Full") == 0)
        state = STATE_FULL;
    else if (strcmp(status, "Charging") == 0)
        state = STATE_CHARGING;
    else if (strcmp(status, "Discharging") == 0)
        state = STATE_DISCHARGING;
    else if (strcmp(status, "Unknown") == 0)
        state = STATE_DISCHARGING;
    else {
        LOG_ERR("unrecognized battery state: %s", status);
        state = STATE_DISCHARGING;
    }

    LOG_DBG("capacity: %ld, energy: %ld, power: %ld", capacity, energy, power);

    mtx_lock(&mod->lock);
    m->state = state;
    m->capacity = capacity;
    m->energy = energy;
    m->power = power;
    mtx_unlock(&mod->lock);
}

static int
run(struct module_run_context *ctx)
{
    const struct bar *bar = ctx->module->bar;
    struct private *m = ctx->module->private;

    int base_dir_fd = initialize(m);
    if (base_dir_fd == -1) {
        module_signal_ready(ctx);
        return -1;
    }

    LOG_INFO("%s: %s %s (at %.1f%% of original capacity)",
             m->battery, m->manufacturer, m->model,
             100.0 * m->energy_full / m->energy_full_design);

    int ret = 1;
    int status_fd = openat(base_dir_fd, "status", O_RDONLY);
    int capacity_fd = openat(base_dir_fd, "capacity", O_RDONLY);
    int energy_fd = openat(base_dir_fd, "energy_now", O_RDONLY);
    int power_fd = openat(base_dir_fd, "power_now", O_RDONLY);

    struct udev *udev = udev_new();
    struct udev_monitor *mon = udev_monitor_new_from_netlink(udev, "udev");

    if (status_fd == -1 || capacity_fd == -1 || energy_fd == -1 ||
        power_fd == -1 || udev == NULL || mon == NULL)
    {
        module_signal_ready(ctx);
        goto out;
    }

    udev_monitor_filter_add_match_subsystem_devtype(mon, "power_supply", NULL);
    udev_monitor_enable_receiving(mon);

    update_status(ctx->module, capacity_fd, energy_fd, power_fd, status_fd);
    module_signal_ready(ctx);

    while (true) {
        struct pollfd fds[] = {
            {.fd = ctx->abort_fd, .events = POLLIN},
            {.fd = udev_monitor_get_fd(mon), .events = POLLIN},
        };
        poll(fds, 2, m->poll_interval * 1000);

        if (fds[0].revents & POLLIN) {
            ret = 0;
            break;
        }

        if (fds[1].revents & POLLIN) {
            struct udev_device *dev = udev_monitor_receive_device(mon);
            const char *sysname = udev_device_get_sysname(dev);

            bool is_us = strcmp(sysname, m->battery) == 0;
            udev_device_unref(dev);

            if (!is_us)
                continue;
        }

        update_status(ctx->module, capacity_fd, energy_fd, power_fd, status_fd);
        bar->refresh(bar);
    }

out:
    if (mon != NULL)
        udev_monitor_unref(mon);
    if (udev != NULL)
        udev_unref(udev);

    if (power_fd != -1)
        close(power_fd);
    if (energy_fd != -1)
        close(energy_fd);
    if (capacity_fd != -1)
        close(capacity_fd);
    if (status_fd != -1)
        close(status_fd);

    close(base_dir_fd);
    return ret;
}

struct module *
module_battery(const char *battery, struct particle *label,
               int poll_interval_secs)
{
    struct private *m = malloc(sizeof(*m));
    m->label = label;
    m->poll_interval = poll_interval_secs;
    m->battery = strdup(battery);
    m->manufacturer = NULL;
    m->model = NULL;

    struct module *mod = module_common_new();
    mod->private = m;
    mod->run = &run;
    mod->destroy = &destroy;
    mod->content = &content;
    return mod;
}