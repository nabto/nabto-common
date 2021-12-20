#include <nn/string_int_map.h>
#include <nn/string.h>

#include <stdlib.h>
#include <string.h>

static void nn_string_int_map_destroy_item(struct nn_string_int_map* map, struct nn_string_int_map_item* item);

void nn_string_int_map_init(struct nn_string_int_map* map, struct nn_allocator* allocator)
{
    nn_llist_init(&map->items);
    map->allocator = *allocator;
}

void nn_string_int_map_deinit(struct nn_string_int_map* map)
{
    struct nn_llist_iterator it = nn_llist_begin(&map->items);
    while(!nn_llist_is_end(&it))
    {
        struct nn_string_int_map_item* item = nn_llist_get_item(&it);
        nn_string_int_map_destroy_item(map, item);
        it = nn_llist_begin(&map->items);
    }
}

void nn_string_int_map_destroy_item(struct nn_string_int_map* map, struct nn_string_int_map_item* item)
{
    nn_llist_erase_node(&item->node);
    map->allocator.free(item->key);
    map->allocator.free(item);
}

struct nn_string_int_map_iterator nn_string_int_map_insert(struct nn_string_int_map* map, const char* key, int value)
{
    {
        struct nn_string_int_map_iterator it = nn_string_int_map_get(map, key);

        if (!nn_string_int_map_is_end(&it)) {
            return it;
        }
    }

    struct nn_string_int_map_item* item = map->allocator.calloc(1, sizeof(struct nn_string_int_map_item));
    char* keyDup = nn_strdup(key, &map->allocator);
    if (item == NULL || keyDup == NULL) {
        map->allocator.free(item); map->allocator.free(keyDup);
        return nn_string_int_map_end(map);
    }

    item->key = keyDup;
    item->value = value;

    {
        struct nn_string_int_map_iterator it;
        it.it = nn_llist_append(&map->items, &item->node, item);
        return it;
    }
}

void nn_string_int_map_erase(struct nn_string_int_map* map, const char* key)
{
    struct nn_string_int_map_iterator it = nn_string_int_map_get(map, key);
    if (!nn_string_int_map_is_end(&it)) {
        nn_string_int_map_erase_iterator(map, &it);
    }
}

void nn_string_int_map_erase_iterator(struct nn_string_int_map* map, struct nn_string_int_map_iterator* it)
{
    (void)map;
    void* tmp = nn_llist_get_item(&it->it);
    struct nn_string_int_map_item* item = tmp;
    nn_string_int_map_destroy_item(map, item);
}

struct nn_string_int_map_iterator nn_string_int_map_get(const struct nn_string_int_map* map, const char* key)
{
    struct nn_llist_iterator it;
    for (it = nn_llist_begin(&map->items);
         !nn_llist_is_end(&it);
         nn_llist_next(&it))
    {
        struct nn_string_int_map_item* item = nn_llist_get_item(&it);
        if (strcmp(item->key, key) == 0) {
            struct nn_string_int_map_iterator ret;
            ret.it = it;
            return ret;
        }
    }
    return nn_string_int_map_end(map);
}

struct nn_string_int_map_iterator nn_string_int_map_begin(const struct nn_string_int_map* map)
{
    struct nn_string_int_map_iterator it;
    it.it = nn_llist_begin(&map->items);
    return it;
}

struct nn_string_int_map_iterator nn_string_int_map_end(const struct nn_string_int_map* map)
{
    struct nn_string_int_map_iterator it;
    it.it = nn_llist_end(&map->items);
    return it;
}

bool nn_string_int_map_is_end(const struct nn_string_int_map_iterator* it)
{
    return nn_llist_is_end(&it->it);
}

const char* nn_string_int_map_key(struct nn_string_int_map_iterator* it)
{
    struct nn_string_int_map_item* item = nn_llist_get_item(&it->it);
    return item->key;
}

int nn_string_int_map_value(struct nn_string_int_map_iterator* it)
{
    struct nn_string_int_map_item* item = nn_llist_get_item(&it->it);
    return item->value;
}
