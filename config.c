#include "config.h"

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include "color.h"

#include "particle.h"
#include "particles/string.h"
#include "particles/list.h"

#include "module.h"
#include "modules/label.h"
#include "modules/clock.h"
#include "modules/xwindow.h"

static uint8_t
hex_nibble(char hex)
{
    assert((hex >= '0' && hex <= '9') ||
           (hex >= 'a' && hex <= 'f') ||
           (hex >= 'A' && hex <= 'F'));

    if (hex >= '0' && hex <= '9')
        return hex - '0';
    else if (hex >= 'a' && hex <= 'f')
        return hex - 'a' + 10;
    else
        return hex - 'A' + 10;
}

static uint8_t
hex_byte(const char hex[2])
{
    uint8_t upper = hex_nibble(hex[0]);
    uint8_t lower = hex_nibble(hex[1]);
    return upper << 4 | lower;
}

static struct rgba
color_from_hexstr(const char *hex)
{
    assert(strlen(hex) == 8);
    uint8_t red = hex_byte(&hex[0]);
    uint8_t green = hex_byte(&hex[2]);
    uint8_t blue = hex_byte(&hex[4]);
    uint8_t alpha = hex_byte(&hex[6]);

    struct rgba rgba = {
        (double)red / 255.0,
        (double)green / 255.0,
        (double)blue / 255.0,
        (double)alpha / 255.0
    };

    assert(rgba.red >= 0.0 && rgba.red <= 1.0);
    assert(rgba.green >= 0.0 && rgba.green <= 1.0);
    assert(rgba.blue >= 0.0 && rgba.blue <= 1.0);
    assert(rgba.alpha >= 0.0 && rgba.alpha <= 1.0);

    return rgba;
}

static struct font *
font_from_config(const struct yml_node *node)
{
    const struct yml_node *family = yml_get_value(node, "family");
    const struct yml_node *size = yml_get_value(node, "size");
    const struct yml_node *italic = yml_get_value(node, "italic");
    const struct yml_node *bold = yml_get_value(node, "bold");
    const struct yml_node *y_offset = yml_get_value(node, "y_offset");

    return font_new(
        family != NULL ? yml_value_as_string(family) : "monospace",
        size != NULL ? yml_value_as_int(size) : 12,
        italic != NULL ? yml_value_as_bool(italic) : false,
        bold != NULL ? yml_value_as_bool(bold) : false,
        y_offset != NULL ? yml_value_as_int(y_offset) : 0);
}

static struct particle *
particle_string_from_config(const struct yml_node *node, const struct font *parent_font)
{
    assert(yml_is_dict(node));

    const struct yml_node *text_node = yml_get_value(node, "text");
    const struct yml_node *font_node = yml_get_value(node, "font");
    const struct yml_node *foreground_node = yml_get_value(node, "foreground");
    const struct yml_node *margin_node = yml_get_value(node, "margin");
    const struct yml_node *left_margin_node = yml_get_value(node, "left_margin");
    const struct yml_node *right_margin_node = yml_get_value(node, "right_margin");

    /* TODO: inherit values? At least color... */
    struct rgba foreground = {1.0, 1.0, 1.0, 1.0};
    int left_margin = 0;
    int right_margin = 0;

    struct font *font = NULL;
    if (font_node != NULL)
        font = font_from_config(font_node);
    else
        font = font_clone(parent_font);

    if (foreground_node != NULL)
        foreground = color_from_hexstr(yml_value_as_string(foreground_node));

    if (margin_node != NULL)
        left_margin = right_margin = yml_value_as_int(margin_node);
    if (left_margin_node != NULL)
        left_margin = yml_value_as_int(left_margin_node);
    if (right_margin_node != NULL)
        right_margin = yml_value_as_int(right_margin_node);

    assert(text_node != NULL);
    return particle_string_new(
        yml_value_as_string(text_node), font, foreground, left_margin, right_margin);
}

static struct particle * particle_from_config(
    const struct yml_node *node, const struct font *parent_font);

static struct particle *
particle_list_from_config(const struct yml_node *node,
                          const struct font *parent_font)
{
    const struct yml_node *items_node = yml_get_value(node, "items");

    const struct yml_node *margin_node = yml_get_value(node, "margin");
    const struct yml_node *left_margin_node = yml_get_value(node, "left_margin");
    const struct yml_node *right_margin_node = yml_get_value(node, "right_margin");

    const struct yml_node *spacing_node = yml_get_value(node, "spacing");
    const struct yml_node *left_spacing_node = yml_get_value(node, "left_spacing");
    const struct yml_node *right_spacing_node = yml_get_value(node, "right_spacing");

    int left_margin = 0;
    int right_margin = 0;
    int left_spacing = 0;
    int right_spacing = 0;

    if (margin_node != NULL)
        left_margin = right_margin = yml_value_as_int(margin_node);
    if (left_margin_node != NULL)
        left_margin = yml_value_as_int(left_margin_node);
    if (right_margin_node != NULL)
        right_margin = yml_value_as_int(right_margin_node);

    if (spacing_node != NULL)
        left_spacing = right_spacing = yml_value_as_int(spacing_node);
    if (left_spacing_node != NULL)
        left_spacing = yml_value_as_int(left_spacing_node);
    if (right_spacing_node != NULL)
        right_spacing = yml_value_as_int(right_spacing_node);

    size_t count = yml_list_length(items_node);
    struct particle **parts = calloc(count, sizeof(*parts));

    size_t idx = 0;
    for (struct yml_list_iter it = yml_list_iter(items_node);
         it.node != NULL;
         yml_list_next(&it), idx++)
    {
        parts[idx] = particle_from_config(it.node, parent_font);
    }

    struct particle *list = particle_list_new(
        parts, count, left_spacing, right_spacing, left_margin, right_margin);

    free(parts);
    return list;
}

static struct particle *
particle_from_config(const struct yml_node *node, const struct font *parent_font)
{
    assert(yml_is_dict(node));
    assert(yml_dict_length(node) == 1);

    struct yml_dict_iter pair = yml_dict_iter(node);
    const char *type = yml_value_as_string(pair.key);

    if (strcmp(type, "string") == 0)
        return particle_string_from_config(pair.value, parent_font);
    else if (strcmp(type, "list") == 0)
        return particle_list_from_config(pair.value, parent_font);
    else
        assert(false);
}

struct bar *
conf_to_bar(const struct yml_node *bar)
{
    struct bar_config conf = {0};

    /* Create a default font */
    struct font *font = font_new("sans", 12, false, false, 0);

    const struct yml_node *height = yml_get_value(bar, "height");
    const struct yml_node *location = yml_get_value(bar, "location");
    const struct yml_node *background = yml_get_value(bar, "background");
    const struct yml_node *spacing = yml_get_value(bar, "spacing");
    const struct yml_node *left_spacing = yml_get_value(bar, "left_spacing");
    const struct yml_node *right_spacing = yml_get_value(bar, "right_spacing");
    const struct yml_node *margin = yml_get_value(bar, "margin");
    const struct yml_node *left_margin = yml_get_value(bar, "left_margin");
    const struct yml_node *right_margin = yml_get_value(bar, "right_margin");
    const struct yml_node *border = yml_get_value(bar, "border");
    const struct yml_node *font_node = yml_get_value(bar, "font");
    const struct yml_node *left = yml_get_value(bar, "left");
    const struct yml_node *center = yml_get_value(bar, "center");
    const struct yml_node *right = yml_get_value(bar, "right");

    if (height != NULL)
        conf.height = yml_value_as_int(height);

    if (location != NULL) {
        const char *loc = yml_value_as_string(location);
        assert(strcasecmp(loc, "top") == 0 || strcasecmp(loc, "bottom") == 0);

        if (strcasecmp(loc, "top") == 0)
            conf.location = BAR_TOP;
        else if (strcasecmp(loc, "bottom") == 0)
            conf.location = BAR_BOTTOM;
        else
            assert(false);
    }

    if (background != NULL)
        conf.background = color_from_hexstr(yml_value_as_string(background));

    if (spacing != NULL)
        conf.left_spacing = conf.right_spacing = yml_value_as_int(spacing);

    if (left_spacing != NULL)
        conf.left_spacing = yml_value_as_int(left_spacing);

    if (right_spacing != NULL)
        conf.right_spacing = yml_value_as_int(right_spacing);

    if (margin != NULL)
        conf.left_margin = conf.right_margin = yml_value_as_int(margin);

    if (left_margin != NULL)
        conf.left_margin = yml_value_as_int(left_margin);

    if (right_margin != NULL)
        conf.right_margin = yml_value_as_int(right_margin);

    if (border != NULL) {
        assert(yml_is_dict(border));

        const struct yml_node *width = yml_get_value(border, "width");
        const struct yml_node *color = yml_get_value(border, "color");

        if (width != NULL)
            conf.border.width = yml_value_as_int(width);

        if (color != NULL)
            conf.border.color = color_from_hexstr(yml_value_as_string(color));
    }

    if (font_node != NULL) {
        font_destroy(font);
        font = font_from_config(font_node);
    }

    for (size_t i = 0; i < 3; i++) {
        const struct yml_node *node = i == 0 ? left : i == 1 ? center : right;

        if (node != NULL) {
            const size_t count = yml_list_length(node);
            struct module **mods = calloc(count, sizeof(*mods));

            size_t idx = 0;
            for (struct yml_list_iter it = yml_list_iter(node);
                 it.node != NULL;
                 yml_list_next(&it), idx++)
            {
                const struct yml_node *n = yml_get_value(it.node, "module");
                assert(n != NULL);

                const struct yml_node *c = yml_get_value(it.node, "content");
                assert(c != NULL);

                const char *mod_name = yml_value_as_string(n);
                struct particle *content = particle_from_config(c, font);

                if (strcmp(mod_name, "label") == 0)
                    mods[idx] = module_label(content);
                else if (strcmp(mod_name, "clock") == 0)
                    mods[idx] = module_clock(content);
                else if (strcmp(mod_name, "xwindow") == 0)
                    mods[idx] = module_xwindow(content);
                else
                    assert(false);
            }

            if (i == 0) {
                conf.left.mods = mods;
                conf.left.count = count;
            } else if (i == 1) {
                conf.center.mods = mods;
                conf.center.count = count;
            } else {
                conf.right.mods = mods;
                conf.right.count = count;
            }
        }
    }

    struct bar *ret = bar_new(&conf);

    free(conf.left.mods);
    free(conf.center.mods);
    free(conf.right.mods);
    font_destroy(font);

    return ret;
}
