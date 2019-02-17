#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <threads.h>

#include <sys/types.h>
#include <fcntl.h>

#include <i3/ipc.h>

#define LOG_MODULE "i3"
#define LOG_ENABLE_DBG 0
#include "../log.h"
#include "../bar/bar.h"
#include "../config.h"
#include "../config-verify.h"
#include "../particles/dynlist.h"
#include "../plugin.h"

#include "i3-common.h"

struct ws_content {
    char *name;
    struct particle *content;
};

struct workspace {
    char *name;
    char *output;
    bool visible;
    bool focused;
    bool urgent;

    struct {
        unsigned id;
        char *title;
        char *application;
        pid_t pid;
    } window;
};

struct private {
    int left_spacing;
    int right_spacing;

    bool dirty;

    struct {
        struct ws_content *v;
        size_t count;
    } ws_content;

    struct {
        struct workspace *v;
        size_t count;
    } workspaces;
};

static bool
workspace_from_json(const struct json_object *json, struct workspace *ws)
{
    assert(json_object_is_type(json, json_type_object));
    if (!json_object_is_type(json, json_type_object)) {
        LOG_ERR("'workspace' object is not of type 'object'");
        return false;
    }

    struct json_object *name = json_object_object_get(json, "name");
    struct json_object *output = json_object_object_get(json, "output");
    const struct json_object *visible = json_object_object_get(json, "visible");
    const struct json_object *focused = json_object_object_get(json, "focused");
    const struct json_object *urgent = json_object_object_get(json, "urgent");

    if (name == NULL || !json_object_is_type(name, json_type_string)) {
        LOG_ERR("'workspace' object has no 'name' string value");
        return false;
    }

    if (output == NULL || !json_object_is_type(output, json_type_string)) {
        LOG_ERR("'workspace' object has no 'output' string value");
        return false;
    }

    if (visible != NULL && !json_object_is_type(visible, json_type_boolean)) {
        LOG_ERR("'workspace' object's 'visible' value is not a boolean");
        return false;
    }

    if (focused != NULL && !json_object_is_type(focused, json_type_boolean)) {
        LOG_ERR("'workspace' object's 'focused' value is not a boolean");
        return false;
    }

    if (urgent != NULL && !json_object_is_type(urgent, json_type_boolean)) {
        LOG_ERR("'workspace' object's 'urgent' value is not a boolean");
        return false;
    }

    *ws = (struct workspace) {
        .name = strdup(json_object_get_string(name)),
        .output = strdup(json_object_get_string(output)),
        .visible = json_object_get_boolean(visible),
        .focused = json_object_get_boolean(focused),
        .urgent = json_object_get_boolean(urgent),
        .window = {.title = NULL, .pid = -1},
    };

    return true;
}

static void
workspace_free(struct workspace ws)
{
    free(ws.name);
    free(ws.output);
    free(ws.window.title);
    free(ws.window.application);
}

static void
workspaces_free(struct private *m)
{
    for (size_t i = 0; i < m->workspaces.count; i++)
        workspace_free(m->workspaces.v[i]);

    free(m->workspaces.v);
    m->workspaces.v = NULL;
    m->workspaces.count = 0;
}

static void
workspace_add(struct private *m, struct workspace ws)
{
    size_t new_count = m->workspaces.count + 1;
    struct workspace *new_v = realloc(m->workspaces.v, new_count * sizeof(new_v[0]));

    m->workspaces.count = new_count;
    m->workspaces.v = new_v;
    m->workspaces.v[new_count - 1] = ws;
}

static void
workspace_del(struct private *m, const char *name)
{
    struct workspace *workspaces = m->workspaces.v;

    for (size_t i = 0; i < m->workspaces.count; i++) {
        const struct workspace *ws = &workspaces[i];

        if (strcmp(ws->name, name) != 0)
            continue;

        workspace_free(*ws);

        memmove(
            &workspaces[i],
            &workspaces[i + 1],
            (m->workspaces.count - i - 1) * sizeof(workspaces[0]));
        m->workspaces.count--;
        break;
    }
}

static struct workspace *
workspace_lookup(struct private *m, const char *name)
{
    for (size_t i = 0; i < m->workspaces.count; i++)
        if (strcmp(m->workspaces.v[i].name, name) == 0)
            return &m->workspaces.v[i];
    return NULL;
}


static bool
handle_get_version_reply(int type, const struct json_object *json, void *_m)
{
    if (!json_object_is_type(json, json_type_object)) {
        LOG_ERR("'version' reply is not of type 'object'");
        return false;
    }

    struct json_object *version = json_object_object_get(json, "human_readable");

    if (version == NULL || !json_object_is_type(version, json_type_string)) {
        LOG_ERR("'version' reply did not contain a 'human_readable' string value");
        return false;
    }

    LOG_INFO("i3: %s", json_object_get_string(version));
    return true;
}

static bool
handle_subscribe_reply(int type, const struct json_object *json, void *_m)
{
    if (!json_object_is_type(json, json_type_object)) {
        LOG_ERR("'subscribe' reply is not of type 'object'");
        return false;
    }

    const struct json_object *success = json_object_object_get(json, "success");

    if (success == NULL || !json_object_is_type(success, json_type_boolean)) {
        LOG_ERR("'subscribe' reply did not contain a 'success' boolean value");
        return false;
    }

    if (!json_object_get_boolean(success)) {
        LOG_ERR("failed to subscribe");
        return false;
    }

    return true;
}

static bool
handle_get_workspaces_reply(int type, const struct json_object *json, void *_mod)
{
    struct module *mod = _mod;
    struct private *m = mod->private;

    if (!json_object_is_type(json, json_type_array)) {
        LOG_ERR("'workspaces' reply is not of type 'array'");
        return false;
    }

    mtx_lock(&mod->lock);

    workspaces_free(m);
    m->dirty = true;

    size_t count = json_object_array_length(json);
    m->workspaces.count = count;
    m->workspaces.v = calloc(count, sizeof(m->workspaces.v[0]));

    for (size_t i = 0; i < count; i++) {
        if (!workspace_from_json(
                json_object_array_get_idx(json, i), &m->workspaces.v[i])) {
            workspaces_free(m);
            mtx_unlock(&mod->lock);
            return false;
        }

        LOG_DBG("#%zu: %s", i, m->workspaces.v[i].name);
    }

    mtx_unlock(&mod->lock);
    return true;
}

static bool
handle_workspace_event(int type, const struct json_object *json, void *_mod)
{
    struct module *mod = _mod;
    struct private *m = mod->private;

    if (!json_object_is_type(json, json_type_object)) {
        LOG_ERR("'workspace' event is not of type 'object'");
        return false;
    }

    struct json_object *change = json_object_object_get(json, "change");
    const struct json_object *current = json_object_object_get(json, "current");
    const struct json_object *old = json_object_object_get(json, "old");
    struct json_object *current_name = NULL;

    if (change == NULL || !json_object_is_type(change, json_type_string)) {
        LOG_ERR("'workspace' event did not contain a 'change' string value");
        return false;
    }

    const char *change_str = json_object_get_string(change);
    bool is_urgent = strcmp(change_str, "reload") == 0;

    if (!is_urgent) {
        if (current == NULL || !json_object_is_type(current, json_type_object)) {
            LOG_ERR("'workspace' event did not contain a 'current' object value");
            return false;
        }

        current_name = json_object_object_get(current, "name");
        if (current_name == NULL ||
            !json_object_is_type(current_name, json_type_string))
        {
            LOG_ERR("'workspace' event's 'current' object did not "
                    "contain a 'name' string value");
            return false;
        }
    }

    mtx_lock(&mod->lock);

    if (strcmp(change_str, "init") == 0) {
        assert(workspace_lookup(m, json_object_get_string(current_name)) == NULL);

        struct workspace ws;
        if (!workspace_from_json(current, &ws))
            goto err;

        workspace_add(m, ws);
    }

    else if (strcmp(change_str, "empty") == 0) {
        assert(workspace_lookup(m, json_object_get_string(current_name)) != NULL);
        workspace_del(m, json_object_get_string(current_name));
    }

    else if (strcmp(change_str, "focus") == 0) {
        if (old == NULL || !json_object_is_type(old, json_type_object)) {
            LOG_ERR("'workspace' event did not contain a 'old' object value");
            goto err;
        }

        struct json_object *old_name = json_object_object_get(old, "name");
        if (old_name == NULL || !json_object_is_type(old_name, json_type_string)) {
            LOG_ERR("'workspace' event's 'old' object did not "
                    "contain a 'name' string value");
            goto err;
        }

        struct workspace *w = workspace_lookup(
            m, json_object_get_string(current_name));
        LOG_DBG("w: %s", w->name);
        assert(w != NULL);

        /* Mark all workspaces on current's output invisible */
        for (size_t i = 0; i < m->workspaces.count; i++) {
            struct workspace *ws = &m->workspaces.v[i];
            if (strcmp(ws->output, w->output) == 0)
                ws->visible = false;
        }

        const struct json_object *urgent = json_object_object_get(current, "urgent");
        if (urgent == NULL || !json_object_is_type(urgent, json_type_boolean)) {
            LOG_ERR("'workspace' event's 'current' object did not "
                    "contain a 'urgent' boolean value");
            goto err;
        }

        w->urgent = json_object_get_boolean(urgent);
        w->focused = true;
        w->visible = true;

        /* Old workspace is no longer focused */
        struct workspace *old_w = workspace_lookup(
            m, json_object_get_string(old_name));
        if (old_w != NULL)
            old_w->focused = false;
    }

    else if (strcmp(change_str, "urgent") == 0){
        const struct json_object *urgent = json_object_object_get(current, "urgent");
        if (urgent == NULL || !json_object_is_type(urgent, json_type_boolean)) {
            LOG_ERR("'workspace' event's 'current' object did not "
                    "contain a 'urgent' boolean value");
            goto err;
        }

        struct workspace *w = workspace_lookup(
            m, json_object_get_string(current_name));
        w->urgent = json_object_get_boolean(urgent);
    }

    else if (strcmp(change_str, "reload") == 0)
        LOG_WARN("unimplemented: 'reload' event");

    m->dirty = true;
    mtx_unlock(&mod->lock);
    return true;

err:
    mtx_unlock(&mod->lock);
    return false;
}

static bool
handle_window_event(int type, const struct json_object *json, void *_mod)
{
    struct module *mod = _mod;
    struct private *m = mod->private;

    mtx_lock(&mod->lock);

    struct workspace *ws = NULL;
    size_t focused = 0;
    for (size_t i = 0; i < m->workspaces.count; i++) {
        if (m->workspaces.v[i].focused) {
            ws = &m->workspaces.v[i];
            focused++;
        }
    }

    assert(focused == 1);
    assert(ws != NULL);

    if (!json_object_is_type(json, json_type_object)) {
        LOG_ERR("'window' event is not of type 'object'");
        goto err;
    }

    struct json_object *change = json_object_object_get(json, "change");
    if (change == NULL || !json_object_is_type(change, json_type_string)) {
        LOG_ERR("'window' event did not contain a 'change' string value");
        goto err;
    }

    const char *change_str = json_object_get_string(change);

    bool is_focus = strcmp(change_str, "focus") == 0;
    bool is_close = !is_focus && strcmp(change_str, "close") == 0;
    bool is_title = !is_focus && !is_close && strcmp(change_str, "title") == 0;

    if (!is_focus && !is_close && !is_title) {
        mtx_unlock(&mod->lock);
        return true;
    }

    if (is_close) {
        free(ws->window.title);
        free(ws->window.application);

        ws->window.id = -1;
        ws->window.title = ws->window.application = NULL;
        ws->window.pid = -1;

        m->dirty = true;
        mtx_unlock(&mod->lock);
        return true;

    }

    const struct json_object *container = json_object_object_get(json, "container");
    if (container == NULL || !json_object_is_type(container, json_type_object)) {
        LOG_ERR("'window' event (%s) did not contain a 'container' object", change_str);
        goto err;
    }

    struct json_object *id = json_object_object_get(container, "id");
    if (id == NULL || !json_object_is_type(id, json_type_int)) {
        LOG_ERR("'window' event (%s) did not contain a 'container.id' integer value",
                change_str);
        goto err;
    }

    struct json_object *name = json_object_object_get(container, "name");
    if (name == NULL || !json_object_is_type(name, json_type_string)) {
        LOG_ERR(
            "'window' event (%s) did not contain a 'container.name' string value",
            change_str);
        goto err;
    }

    const struct json_object *pid_node = json_object_object_get(container, "pid");
    if (pid_node == NULL || !json_object_is_type(pid_node, json_type_int)) {
        LOG_ERR(
            "'window' event (%s) did not contain a 'container.pid' integer value",
            change_str);
        goto err;
    }

    if (is_title && ws->window.id != json_object_get_int(id)) {
        /* Ignore title changed event if it's not current window */
        mtx_unlock(&mod->lock);
        return true;
    }

    free(ws->window.title);
    ws->window.title = strdup(json_object_get_string(name));
    ws->window.id = json_object_get_int(id);

    /* If PID has changed, update application name from /proc/<pid>/comm */
    pid_t pid = json_object_get_int(pid_node);
    if (ws->window.pid != pid) {
        ws->window.pid = pid;

        char path[64];
        snprintf(path, sizeof(path), "/proc/%u/comm", ws->window.pid);

        int fd = open(path, O_RDONLY);
        if (fd == -1) {
            /* Application may simply have terminated */
            free(ws->window.application); ws->window.application = NULL;
            ws->window.pid = -1;

            mtx_unlock(&mod->lock);
            return true;
        }

        char application[128];
        ssize_t bytes = read(fd, application, sizeof(application));
        assert(bytes >= 0);

        application[bytes - 1] = '\0';
        ws->window.application = strdup(application);
        close(fd);
    }

    m->dirty = true;
    mtx_unlock(&mod->lock);
    return true;

err:
    m->dirty = false;
    mtx_unlock(&mod->lock);
    return false;
}

static void
burst_done(void *_mod)
{
    struct module *mod = _mod;
    struct private *m = mod->private;

    if (m->dirty) {
        m->dirty = false;
        mod->bar->refresh(mod->bar);
    }
}

static int
run(struct module *mod)
{
    struct sockaddr_un addr;
    if (!i3_get_socket_address(&addr))
        return 1;

    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock == -1) {
        LOG_ERRNO("failed to create UNIX socket");
        return 1;
    }

    int r = connect(sock, (const struct sockaddr *)&addr, sizeof(addr));
    if (r == -1) {
        LOG_ERRNO("failed to connect to i3 socket");
        close(sock);
        return 1;
    }

    i3_send_pkg(sock, I3_IPC_MESSAGE_TYPE_GET_VERSION, NULL);
    i3_send_pkg(sock, I3_IPC_MESSAGE_TYPE_SUBSCRIBE, "[\"workspace\", \"window\"]");
    i3_send_pkg(sock, I3_IPC_MESSAGE_TYPE_GET_WORKSPACES, NULL);

    static const struct i3_ipc_callbacks callbacks = {
        .burst_done = &burst_done,
        .reply_version = &handle_get_version_reply,
        .reply_subscribe = &handle_subscribe_reply,
        .reply_workspaces = &handle_get_workspaces_reply,
        .event_workspace = &handle_workspace_event,
        .event_window = &handle_window_event,
    };

    bool ret = i3_receive_loop(mod->abort_fd, sock, &callbacks, mod);
    close(sock);
    return ret ? 0 : 1;
}

static void
destroy(struct module *mod)
{
    struct private *m = mod->private;

    for (size_t i = 0; i < m->ws_content.count; i++) {
        struct particle *p = m->ws_content.v[i].content;
        p->destroy(p);
        free(m->ws_content.v[i].name);
    }

    free(m->ws_content.v);
    workspaces_free(m);

    free(m);
    module_default_destroy(mod);
}

static struct ws_content *
ws_content_for_name(struct private *m, const char *name)
{
    for (size_t i = 0; i < m->ws_content.count; i++) {
        struct ws_content *content = &m->ws_content.v[i];
        if (strcmp(content->name, name) == 0)
            return content;
    }

    return NULL;
}

static struct exposable *
content(struct module *mod)
{
    struct private *m = mod->private;

    mtx_lock(&mod->lock);

    size_t particle_count = 0;
    struct exposable *particles[m->workspaces.count + 1];
    struct exposable *current = NULL;

    for (size_t i = 0; i < m->workspaces.count; i++) {
        const struct workspace *ws = &m->workspaces.v[i];
        const struct ws_content *template = NULL;

        /* Lookup content template for workspace. Fall back to default
         * template if this workspace doesn't have a specific
         * template */
        template = ws_content_for_name(m, ws->name);
        if (template == NULL) {
            LOG_DBG("no ws template for %s, using default template", ws->name);
            template = ws_content_for_name(m, "");
        }

        const char *state =
            ws->urgent ? "urgent" :
            ws->visible ? ws->focused ? "focused" : "unfocused" :
            "invisible";

        struct tag_set tags = {
            .tags = (struct tag *[]){
                tag_new_string(mod, "name", ws->name),
                tag_new_bool(mod, "visible", ws->visible),
                tag_new_bool(mod, "focused", ws->focused),
                tag_new_bool(mod, "urgent", ws->urgent),
                tag_new_string(mod, "state", state),

                tag_new_string(mod, "application", ws->window.application),
                tag_new_string(mod, "title", ws->window.title),
            },
            .count = 7,
        };

        if (ws->focused) {
            const struct ws_content *cur = ws_content_for_name(m, "current");
            if (cur != NULL)
                current = cur->content->instantiate(cur->content, &tags);
        }

        if (template == NULL) {
            LOG_WARN(
                "no ws template for %s, and no default template available",
                ws->name);
        } else {
            particles[particle_count++] = template->content->instantiate(
                template->content, &tags);
        }

        tag_set_destroy(&tags);
    }

    if (current != NULL)
        particles[particle_count++] = current;

    mtx_unlock(&mod->lock);
    return dynlist_exposable_new(
        particles, particle_count, m->left_spacing, m->right_spacing);
}

/* Maps workspace name to a content particle. */
struct i3_workspaces {
    const char *name;
    struct particle *content;
};

static struct module *
i3_new(struct i3_workspaces workspaces[], size_t workspace_count,
       int left_spacing, int right_spacing)
{
    struct private *m = calloc(1, sizeof(*m));

    m->left_spacing = left_spacing;
    m->right_spacing = right_spacing;

    m->ws_content.count = workspace_count;
    m->ws_content.v = malloc(workspace_count * sizeof(m->ws_content.v[0]));

    for (size_t i = 0; i < workspace_count; i++) {
        m->ws_content.v[i].name = strdup(workspaces[i].name);
        m->ws_content.v[i].content = workspaces[i].content;
    }

    struct module *mod = module_common_new();
    mod->private = m;
    mod->run = &run;
    mod->destroy = &destroy;
    mod->content = &content;
    return mod;
}

static struct module *
from_conf(const struct yml_node *node, struct conf_inherit inherited)
{
    const struct yml_node *c = yml_get_value(node, "content");
    const struct yml_node *spacing = yml_get_value(node, "spacing");
    const struct yml_node *left_spacing = yml_get_value(node, "left-spacing");
    const struct yml_node *right_spacing = yml_get_value(node, "right-spacing");

    int left = spacing != NULL ? yml_value_as_int(spacing) :
        left_spacing != NULL ? yml_value_as_int(left_spacing) : 0;
    int right = spacing != NULL ? yml_value_as_int(spacing) :
        right_spacing != NULL ? yml_value_as_int(right_spacing) : 0;

    struct i3_workspaces workspaces[yml_dict_length(c)];

    size_t idx = 0;
    for (struct yml_dict_iter it = yml_dict_iter(c);
         it.key != NULL;
         yml_dict_next(&it), idx++)
    {
        workspaces[idx].name = yml_value_as_string(it.key);
        workspaces[idx].content = conf_to_particle(it.value, inherited);
    }

    return i3_new(workspaces, yml_dict_length(c), left, right);
}

static bool
verify_content(keychain_t *chain, const struct yml_node *node)
{
    if (!yml_is_dict(node)) {
        LOG_ERR(
            "%s: must be a dictionary of workspace-name: particle mappings",
            conf_err_prefix(chain, node));
        return false;
    }

    for (struct yml_dict_iter it = yml_dict_iter(node);
         it.key != NULL;
         yml_dict_next(&it))
    {
        const char *key = yml_value_as_string(it.key);
        if (key == NULL) {
            LOG_ERR("%s: key must be a string (a i3 workspace name)",
                    conf_err_prefix(chain, it.key));
            return false;
        }

        if (!conf_verify_particle(chain_push(chain, key), it.value))
            return false;

        chain_pop(chain);
    }

    return true;
}

static bool
verify_conf(keychain_t *chain, const struct yml_node *node)
{
    static const struct attr_info attrs[] = {
        {"spacing", false, &conf_verify_int},
        {"left-spacing", false, &conf_verify_int},
        {"right-spacing", false, &conf_verify_int},
        {"content", true, &verify_content},
        {"anchors", false, NULL},
        MODULE_COMMON_ATTRS,
    };

    return conf_verify_dict(chain, node, attrs);
}

const struct module_iface module_i3_iface = {
    .verify_conf = &verify_conf,
    .from_conf = &from_conf,
};

#if defined(CORE_PLUGINS_AS_SHARED_LIBRARIES)
extern const struct module_iface iface __attribute__((weak, alias("module_i3_iface"))) ;
#endif
