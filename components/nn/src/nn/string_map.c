#include <nn/string_map.h>

#include <stdlib.h>
#include <string.h>

static void nn_string_map_destroy_item(struct nn_string_map_item* item);

void nn_string_map_init(struct nn_string_map* map)
{
    nn_llist_init(&map->items);
}

void nn_string_map_deinit(struct nn_string_map* map)
{
    struct nn_llist_iterator it = nn_llist_begin(&map->items);
    while(!nn_llist_is_end(&it))
    {
        struct nn_string_map_item* item = nn_llist_get_item(&it);
        nn_string_map_destroy_item(item);
        it = nn_llist_begin(&map->items);
    }
}

void nn_string_map_destroy_item(struct nn_string_map_item* item)
{
    nn_llist_erase_node(&item->node);
    free(item->key);
    free(item->value);
    free(item);
}

struct nn_string_map_iterator nn_string_map_insert(struct nn_string_map* map, const char* key, const char* value)
{
    {
        struct nn_string_map_iterator it = nn_string_map_get(map, key);

        if (!nn_string_map_is_end(&it)) {
            return it;
        }
    }

    struct nn_string_map_item* item = calloc(1, sizeof(struct nn_string_map_item));
    char* keyDup = strdup(key);
    char* valueDup = strdup(value);
    if (item == NULL || keyDup == NULL || valueDup == NULL) {
        free(item); free(keyDup); free(valueDup);
        return nn_string_map_end(map);
    }

    item->key = keyDup;
    item->value = valueDup;

    {
        struct nn_string_map_iterator it;
        it.it = nn_llist_append(&map->items, &item->node, item);
        return it;
    }
}

struct nn_string_map_iterator nn_string_map_get(const struct nn_string_map* map, const char* key)
{
    struct nn_llist_iterator it;
    for (it = nn_llist_begin(&map->items);
         !nn_llist_is_end(&it);
         nn_llist_next(&it))
    {
        struct nn_string_map_item* item = nn_llist_get_item(&it);
        if (strcmp(item->key, key) == 0) {
            struct nn_string_map_iterator ret;
            ret.it = it;
            return ret;
        }
    }
    return nn_string_map_end(map);
}

struct nn_string_map_iterator nn_string_map_getn(const struct nn_string_map* map, const char* key, size_t keyLength)
{
    struct nn_llist_iterator it;
    for (it = nn_llist_begin(&map->items);
         !nn_llist_is_end(&it);
         nn_llist_next(&it))
    {
        struct nn_string_map_item* item = nn_llist_get_item(&it);
        if (strncmp(item->key, key, keyLength) == 0) {
            struct nn_string_map_iterator ret;
            ret.it = it;
            return ret;
        }
    }
    return nn_string_map_end(map);
}


bool nn_string_map_empty(const struct nn_string_map* map)
{
    return nn_llist_empty(&map->items);
}

size_t nn_string_map_size(const struct nn_string_map* map)
{
    return nn_llist_size(&map->items);
}

struct nn_string_map_iterator nn_string_map_begin(const struct nn_string_map* map)
{
    struct nn_string_map_iterator it;
    it.it = nn_llist_begin(&map->items);
    return it;
}

struct nn_string_map_iterator nn_string_map_end(const struct nn_string_map* map)
{
    struct nn_string_map_iterator it;
    it.it = nn_llist_end(&map->items);
    return it;
}

bool nn_string_map_is_end(const struct nn_string_map_iterator* it)
{
    return nn_llist_is_end(&it->it);
}

void nn_string_map_next(struct nn_string_map_iterator* it)
{
    nn_llist_next(&it->it);
}

const char* nn_string_map_key(struct nn_string_map_iterator* it)
{
    struct nn_string_map_item* item = nn_llist_get_item(&it->it);
    return item->key;
}

const char* nn_string_map_value(struct nn_string_map_iterator* it)
{
    struct nn_string_map_item* item = nn_llist_get_item(&it->it);
    return item->value;
}
