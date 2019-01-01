#include "yml.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

#include <yaml.h>

#include "tllist.h"

enum node_type {
    ROOT,
    SCALAR,
    DICT,
    LIST,
};

struct yml_node;

struct dict_pair {
    struct yml_node *key;
    struct yml_node *value;
};

struct anchor_map {
    char *anchor;
    const struct yml_node *node;
};

struct yml_node {
    enum node_type type;
    union {
        struct {
            struct yml_node *root;
            struct anchor_map anchors[100]; /* TODO: dynamic resize */
            size_t anchor_count;
        } root;
        struct {
            char *value;
        } scalar;
        struct {
            tll(struct dict_pair) pairs;
            bool next_is_value;
        } dict;
        struct {
            tll(struct yml_node *) values;
        } list;
    };

    struct yml_node *parent;
};

static struct yml_node *
clone_node(struct yml_node *parent, const struct yml_node *node)
{
    struct yml_node *clone = calloc(1, sizeof(*clone));
    clone->type = node->type;
    clone->parent = parent;

    switch (node->type) {
    case SCALAR:
        clone->scalar.value = strdup(node->scalar.value);
        break;

    case DICT:
        tll_foreach(node->dict.pairs, it) {
            struct dict_pair p = {
                .key = clone_node(clone, it->item.key),
                .value = clone_node(clone, it->item.value),
            };
            tll_push_back(clone->dict.pairs, p);
        }
        break;

    case LIST:
        tll_foreach(node->list.values, it)
            tll_push_back(clone->list.values, clone_node(clone, it->item));
        break;

    case ROOT:
        assert(false);
        break;
    }

    return clone;
}

static void
add_node(struct yml_node *parent, struct yml_node *new_node)
{
    switch (parent->type) {
    case ROOT:
        assert(parent->root.root == NULL);
        parent->root.root = new_node;
        new_node->parent = parent;
        break;

    case DICT:
        if (!parent->dict.next_is_value) {
            tll_push_back(parent->dict.pairs, (struct dict_pair){.key = new_node});
            parent->dict.next_is_value = true;
        } else {
            tll_back(parent->dict.pairs).value = new_node;
            parent->dict.next_is_value = false;
        }
        new_node->parent = parent;
        break;

    case LIST:
        tll_push_back(parent->list.values, new_node);
        new_node->parent = parent;
        break;

    case SCALAR:
        assert(false);
        break;
    }
}

static void
add_anchor(struct yml_node *root, const char *anchor,
           const struct yml_node *node)
{
    assert(root->type == ROOT);

    struct anchor_map *map = &root->root.anchors[root->root.anchor_count];
    map->anchor = strdup(anchor);
    map->node = node;
    root->root.anchor_count++;
}

static void
post_process(struct yml_node *node)
{
    switch (node->type) {
    case ROOT:
        post_process(node->root.root);
        break;

    case SCALAR:
        //assert(strcmp(node->scalar.value, "<<") != 0);
        break;

    case LIST:
        tll_foreach(node->list.values, it)
            post_process(it->item);
        break;

    case DICT:
        tll_foreach(node->dict.pairs, it) {
            post_process(it->item.key);
            post_process(it->item.value);
        }

        tll_foreach(node->dict.pairs, it) {
            if (it->item.key->type != SCALAR)
                continue;

            if (strcmp(it->item.key->scalar.value, "<<") != 0)
                continue;

            if (it->item.value->type == LIST) {
                /*
                 * Merge value is a list (of dictionaries)
                 * e.g. <<: [*foo, *bar]
                 */
                tll_foreach(it->item.value->list.values, v_it) {
                    assert(v_it->item->type == DICT);
                    tll_foreach(v_it->item->dict.pairs, vv_it) {
                        struct dict_pair p = {
                            .key = vv_it->item.key,
                            .value = vv_it->item.value,
                        };
                        tll_push_back(node->dict.pairs, p);
                    }

                    /* Destroy lits, but don't free (since its nodes
                     * have been moved to this node), *before*
                     * destroying the key/value nodes. This ensures
                     * the dict nodes aren't free:d in the
                     * yml_destroy() below). */
                    tll_free(v_it->item->dict.pairs);
                }
            } else {
                /*
                 * Merge value is a dictionary only
                 * e.g. <<: *foo
                 */
                assert(it->item.value->type == DICT);
                tll_foreach(it->item.value->dict.pairs, v_it) {
                    struct dict_pair p = {
                        .key = v_it->item.key,
                        .value = v_it->item.value,
                    };
                    tll_push_back(node->dict.pairs, p);
                }

                /* Destroy list here, *without* freeing nodes (since
                 * nodes have been moved to this node), *before*
                 * destroying the key/value nodes. This ensures the
                 * dict nodes aren't free:d in the yml_destroy()
                 * below */
                tll_free(it->item.value->dict.pairs);
            }

            yml_destroy(it->item.key);
            yml_destroy(it->item.value);

            tll_remove(node->dict.pairs, it);
        }
        break;
    }
}

struct yml_node *
yml_load(FILE *yml)
{
    yaml_parser_t yaml;
    yaml_parser_initialize(&yaml);

    //FILE *yml = fopen("yml.yml", "r");
    //assert(yml != NULL);

    yaml_parser_set_input_file(&yaml, yml);

    bool done = false;
    int indent = 0;

    struct yml_node *root = malloc(sizeof(*root));
    root->type = ROOT;
    root->root.root = NULL;
    root->root.anchor_count = 0;

    struct yml_node *n = root;

    while (!done) {
        yaml_event_t event;
        if (!yaml_parser_parse(&yaml, &event)) {
            //printf("yaml parser error\n");
            /* TODO: free node tree */
            root = NULL;
            assert(false);
            break;
        }

        switch (event.type) {
        case YAML_NO_EVENT:
            //printf("%*sNO EVENT\n", indent, "");
            break;

        case YAML_STREAM_START_EVENT:
            //printf("%*sSTREAM START\n", indent, "");
            indent += 2;
            break;

        case YAML_STREAM_END_EVENT:
            indent -= 2;
            //printf("%*sSTREAM END\n", indent, "");
            done = true;
            break;

        case YAML_DOCUMENT_START_EVENT:
            //printf("%*sDOC START\n", indent, "");
            indent += 2;
            break;

        case YAML_DOCUMENT_END_EVENT:
            indent -= 2;
            //printf("%*sDOC END\n", indent, "");
            break;

        case YAML_ALIAS_EVENT:
            //printf("%*sALIAS %s\n", indent, "", event.data.alias.anchor);

            for (size_t i = 0; i < root->root.anchor_count; i++) {
                const struct anchor_map *map = &root->root.anchors[i];

                if (strcmp(map->anchor, (const char *)event.data.alias.anchor) != 0)
                    continue;

                struct yml_node *clone = clone_node(NULL, map->node);
                assert(clone != NULL);
                add_node(n, clone);
                break;
            }
            break;

        case YAML_SCALAR_EVENT: {
            //printf("%*sSCALAR: %.*s\n", indent, "",
            //(int)event.data.scalar.length, event.data.scalar.value);
            struct yml_node *new_scalar = calloc(1, sizeof(*new_scalar));
            new_scalar->type = SCALAR;
            new_scalar->scalar.value = strndup(
                (const char*)event.data.scalar.value, event.data.scalar.length);
            add_node(n, new_scalar);

            if (event.data.scalar.anchor != NULL) {
                const char *anchor = (const char *)event.data.scalar.anchor;
                add_anchor(root, anchor, new_scalar);
            }

            break;
        }

        case YAML_SEQUENCE_START_EVENT: {
            //printf("%*sSEQ START\n", indent, "");
            indent += 2;
            struct yml_node *new_list = calloc(1, sizeof(*new_list));
            new_list->type = LIST;
            add_node(n, new_list);
            n = new_list;

            if (event.data.sequence_start.anchor != NULL) {
                const char *anchor = (const char *)event.data.sequence_start.anchor;
                add_anchor(root, anchor, new_list);
            }
            break;
        }

        case YAML_SEQUENCE_END_EVENT:
            indent -= 2;
            //printf("%*sSEQ END\n", indent, "");

            assert(n->parent != NULL);
            n = n->parent;
            break;

        case YAML_MAPPING_START_EVENT: {
            //printf("%*sMAP START\n", indent, "");
            indent += 2;

            struct yml_node *new_dict = calloc(1, sizeof(*new_dict));
            new_dict->type = DICT;
            add_node(n, new_dict);
            n = new_dict;

            if (event.data.mapping_start.anchor != NULL) {
                const char *anchor = (const char *)event.data.mapping_start.anchor;
                add_anchor(root, anchor, new_dict);
            }
            break;
        }

        case YAML_MAPPING_END_EVENT:
            indent -= 2;
            //printf("%*sMAP END\n", indent, "");
            assert(n->parent != NULL);
            n = n->parent;
            break;
        }

        yaml_event_delete(&event);
    }

    yaml_parser_delete(&yaml);

    //print_node(root);
    post_process(root);
    //print_node(root);
    return root;
}

void
yml_destroy(struct yml_node *node)
{
    switch (node->type) {
    case ROOT:
        yml_destroy(node->root.root);
        for (size_t i = 0; i < node->root.anchor_count; i++)
            free(node->root.anchors[i].anchor);
        break;

    case SCALAR:
        free(node->scalar.value);
        break;

    case LIST:
        tll_free_and_free(node->list.values, yml_destroy);
        break;

    case DICT:
        tll_foreach(node->dict.pairs, it) {
            yml_destroy(it->item.key);
            yml_destroy(it->item.value);
        }
        tll_free(node->dict.pairs);
        break;
    }

    free(node);
}

bool
yml_is_scalar(const struct yml_node *node)
{
    return node->type == SCALAR;
}

bool
yml_is_dict(const struct yml_node *node)
{
    return node->type == DICT;
}

bool
yml_is_list(const struct yml_node *node)
{
    return node->type == LIST;
}

 const struct yml_node *
yml_get_value(const struct yml_node *node, const char *_path)
{
    char *path = strdup(_path);

    if (node->type == ROOT)
        node = node->root.root;

    for (const char *part = strtok(path, "."), *next_part = strtok(NULL, ".");
         part != NULL;
         part = next_part, next_part = strtok(NULL, "."))
    {
        assert(yml_is_dict(node));

        tll_foreach(node->dict.pairs, it) {
            assert(yml_is_scalar(it->item.key));
            if (strcmp(it->item.key->scalar.value, part) == 0) {
                if (next_part == NULL) {
                    free(path);
                    return it->item.value;
                }

                node = it->item.value;
                break;
            }
        }
    }

    free(path);
    return NULL;
}

struct yml_list_iter
yml_list_iter(const struct yml_node *list)
{
    assert(yml_is_list(list));
    tll_foreach(list->list.values, it) {
        return (struct yml_list_iter){
            .node = it->item,
            .private = it,
        };
    }

    return (struct yml_list_iter){
        .node = NULL,
        .private = NULL,
    };
}

void
yml_list_next(struct yml_list_iter *iter)
{
    if (iter->private == NULL)
        return;

    const struct yml_node *d = (const void *)(uintptr_t)0xdeadbeef;
    __typeof__(d->list.values.head) it = (__typeof__(d->list.values.head))iter->private;
    __typeof__(d->list.values.head) next = it->next;

    iter->node = next != NULL ? next->item : NULL;
    iter->private = next;
}

size_t
yml_list_length(const struct yml_node *list)
{
    assert(yml_is_list(list));

    size_t length = 0;
    for (struct yml_list_iter it = yml_list_iter(list);
         it.node != NULL;
         yml_list_next(&it), length++)
        ;

    return length;
}

struct yml_dict_iter
yml_dict_iter(const struct yml_node *dict)
{
    assert(yml_is_dict(dict));

    tll_foreach(dict->dict.pairs, it) {
        return (struct yml_dict_iter){
            .key = it->item.key,
            .value = it->item.value,
            .private1 = it,
        };
    }

    return (struct yml_dict_iter) {
        .key = NULL,
        .value = NULL,
        .private1 = NULL,
    };
}

void
yml_dict_next(struct yml_dict_iter *iter)
{
    const struct yml_node *d = (const void *)(uintptr_t)0xdeadbeef;
    __typeof__(d->dict.pairs.head) it = (__typeof__(d->dict.pairs.head))iter->private1;

    if (it == NULL)
        return;

    __typeof__(d->dict.pairs.head) next = it->next;
    iter->key = next != NULL ? next->item.key : NULL;
    iter->value = next != NULL ? next->item.value : NULL;
    iter->private1 = next;
}

size_t
yml_dict_length(const struct yml_node *dict)
{
    assert(yml_is_dict(dict));
    return tll_length(dict->dict.pairs);
}

const char *
yml_value_as_string(const struct yml_node *value)
{
    assert(yml_is_scalar(value));
    return value->scalar.value;
}

long
yml_value_as_int(const struct yml_node *value)
{
    assert(yml_is_scalar(value));

    long ival;
    int res = sscanf(yml_value_as_string(value), "%ld", &ival);
    return res != 1 ? -1 : ival;
}

bool
yml_value_as_bool(const struct yml_node *value)
{
    const char *v = yml_value_as_string(value);
    if (strcasecmp(v, "y") == 0 ||
        strcasecmp(v, "yes") == 0 ||
        strcasecmp(v, "true") == 0 ||
        strcasecmp(v, "on") == 0)
    {
        return true;
    } else if (strcasecmp(v, "n") == 0 ||
               strcasecmp(v, "no") == 0 ||
               strcasecmp(v, "false") == 0 ||
               strcasecmp(v, "off") == 0)
    {
        return false;
    } else
        assert(false);

    return false;
}

static void
_print_node(const struct yml_node *n, int indent)
{
    switch (n->type) {
    case ROOT:
        _print_node(n->root.root, indent);
        break;

    case DICT:
        tll_foreach(n->dict.pairs, it) {
            _print_node(it->item.key, indent);
            printf(": ");

            if (it->item.value->type != SCALAR) {
                printf("\n");
                _print_node(it->item.value, indent + 2);
            } else {
                _print_node(it->item.value, 0);
                printf("\n");
            }
        }
        break;

    case LIST:
        tll_foreach(n->list.values, it) {
            printf("%*s- ", indent, "");
            if (it->item->type != SCALAR) {
                printf("\n");
                _print_node(it->item, indent + 2);
            } else {
                _print_node(it->item, 0);
            }
        }
        break;

    case SCALAR:
        printf("%*s%s", indent, "", n->scalar.value);
        break;
    }
}

void
print_node(const struct yml_node *n)
{
    _print_node(n, 0);
}
