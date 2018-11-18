#include "tag.h"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

struct private {
    char *name;
    union {
        long value_as_int;
        bool value_as_bool;
        double value_as_float;
        char *value_as_string;
    };
};

static const char *
tag_name(const struct tag *tag)
{
    const struct private *priv = tag->private;
    return priv->name;
}

static void
destroy_int_and_float(struct tag *tag)
{
    struct private *priv = tag->private;
    free(priv->name);
    free(priv);
    free(tag);
}

static void
destroy_string(struct tag *tag)
{
    struct private *priv = tag->private;
    free(priv->value_as_string);
    destroy_int_and_float(tag);
}

static const char *
int_as_string(const struct tag *tag)
{
    static char as_string[128];
    const struct private *priv = tag->private;

    snprintf(as_string, sizeof(as_string), "%ld", priv->value_as_int);
    return as_string;
}

static long
int_as_int(const struct tag *tag)
{
    const struct private *priv = tag->private;
    return priv->value_as_int;
}

static bool
int_as_bool(const struct tag *tag)
{
    const struct private *priv = tag->private;
    return priv->value_as_int;
}

static double
int_as_float(const struct tag *tag)
{
    const struct private *priv = tag->private;
    return priv->value_as_int;
}

static const char *
bool_as_string(const struct tag *tag)
{
    const struct private *priv = tag->private;
    return priv->value_as_bool ? "true" : "false";
}

static long
bool_as_int(const struct tag *tag)
{
    const struct private *priv = tag->private;
    return priv->value_as_bool;
}

static bool
bool_as_bool(const struct tag *tag)
{
    const struct private *priv = tag->private;
    return priv->value_as_bool;
}

static double
bool_as_float(const struct tag *tag)
{
    const struct private *priv = tag->private;
    return priv->value_as_bool;
}

static const char *
float_as_string(const struct tag *tag)
{
    static char as_string[128];
    const struct private *priv = tag->private;

    snprintf(as_string, sizeof(as_string), "%.2f", priv->value_as_float);
    return as_string;
}

static long
float_as_int(const struct tag *tag)
{
    const struct private *priv = tag->private;
    return priv->value_as_float;
}

static bool
float_as_bool(const struct tag *tag)
{
    const struct private *priv = tag->private;
    return priv->value_as_float;
}

static double
float_as_float(const struct tag *tag)
{
    const struct private *priv = tag->private;
    return priv->value_as_float;
}

static const char *
string_as_string(const struct tag *tag)
{
    const struct private *priv = tag->private;
    return priv->value_as_string;
}

static long
string_as_int(const struct tag *tag)
{
    const struct private *priv = tag->private;

    long value;
    int matches = sscanf(priv->value_as_string, "%ld", &value);
    return matches == 1 ? value : 0;
}

static bool
string_as_bool(const struct tag *tag)
{
    const struct private *priv = tag->private;

    uint8_t value;
    int matches = sscanf(priv->value_as_string, "%hhu", &value);
    return matches == 1 ? value : 0;
}

static double
string_as_float(const struct tag *tag)
{
    const struct private *priv = tag->private;

    double value;
    int matches = sscanf(priv->value_as_string, "%lf", &value);
    return matches == 1 ? value : 0;
}

struct tag *
tag_new_int(const char *name, long value)
{
    struct private *priv = malloc(sizeof(*priv));
    priv->name = strdup(name);
    priv->value_as_int = value;

    struct tag *tag = malloc(sizeof(*tag));
    tag->private = priv;
    tag->destroy = &destroy_int_and_float;
    tag->name = &tag_name;
    tag->as_string = &int_as_string;
    tag->as_int = &int_as_int;
    tag->as_bool = &int_as_bool;
    tag->as_float = &int_as_float;
    return tag;
}

struct tag *
tag_new_bool(const char *name, bool value)
{
    struct private *priv = malloc(sizeof(*priv));
    priv->name = strdup(name);
    priv->value_as_int = value;

    struct tag *tag = malloc(sizeof(*tag));
    tag->private = priv;
    tag->destroy = &destroy_int_and_float;
    tag->name = &tag_name;
    tag->as_string = &bool_as_string;
    tag->as_int = &bool_as_int;
    tag->as_bool = &bool_as_bool;
    tag->as_float = &bool_as_float;
    return tag;
}

struct tag *
tag_new_float(const char *name, double value)
{
    struct private *priv = malloc(sizeof(*priv));
    priv->name = strdup(name);
    priv->value_as_float = value;

    struct tag *tag = malloc(sizeof(*tag));
    tag->private = priv;
    tag->destroy = &destroy_int_and_float;
    tag->name = &tag_name;
    tag->as_string = &float_as_string;
    tag->as_int = &float_as_int;
    tag->as_bool = &float_as_bool;
    tag->as_float = &float_as_float;
    return tag;
}

struct tag *
tag_new_string(const char *name, const char *value)
{
    struct private *priv = malloc(sizeof(*priv));
    priv->name = strdup(name);
    priv->value_as_string = strdup(value);

    struct tag *tag = malloc(sizeof(*tag));
    tag->private = priv;
    tag->destroy = &destroy_string;
    tag->name = &tag_name;
    tag->as_string = &string_as_string;
    tag->as_int = &string_as_int;
    tag->as_bool = &string_as_bool;
    tag->as_float = &string_as_float;
    return tag;
}

const struct tag *
tag_for_name(const struct tag_set *set, const char *name)
{
    if (set == NULL)
        return NULL;

    for (size_t i = 0; i < set->count; i++) {
        const struct tag *tag = set->tags[i];
        if (strcmp(tag->name(tag), name) == 0)
            return tag;
    }

    return NULL;
}

void
tag_set_destroy(struct tag_set *set)
{
    for (size_t i = 0; i < set->count; i++)
        set->tags[i]->destroy(set->tags[i]);

    set->tags = NULL;
    set->count = 0;
}
