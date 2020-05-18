#ifndef _NN_STRING_MAP_
#define _NN_STRING_MAP_

/**
 * Dynamic allocated map<string,string>
 */

#include <nn/llist.h>

#ifdef __cplusplus
extern "C" {
#endif

struct nn_string_map_item {
    struct nn_llist_node node;
    char* key;
    char* value;
};

struct nn_string_map {
    struct nn_llist items;
};

struct nn_string_map_iterator {
    struct nn_llist_iterator it;
};

/**
 * Initialize a string map
 */
void nn_string_map_init(struct nn_string_map* map);

/**
 * Deinitialize a string map
 */
void nn_string_map_deinit(struct nn_string_map* map);


/**
 * If an item with the key exists return it, else return NULL;
 */
struct nn_string_map_iterator nn_string_map_get(const struct nn_string_map* map, const char* key);
struct nn_string_map_iterator nn_string_map_getn(const struct nn_string_map* map, const char* key, size_t keyLength);

/**
 * Insert an item into a string map.
 *
 * If an item with the given key already exists a pointer to that item
 * is returned.  If the item could not be inserted, an iterator
 * pointing at the end is returned such that !nn_string_map_end(&it) == true;
 */
struct nn_string_map_iterator nn_string_map_insert(struct nn_string_map* map, const char* key, const char* value);

/**
 * Erase a key from the map
 */
void nn_string_map_erase(struct nn_string_map* map, const char* key);

/**
 * return true iff the map is empty
 */
bool nn_string_map_empty(const struct nn_string_map* map);

/**
 * return the number of key value pairs in the map
 */
size_t nn_string_map_size(const struct nn_string_map* map);

/**
 * Return the key for an item.
 */
const char* nn_string_map_key(struct nn_string_map_iterator* it);

/**
 * Return the value for an item.
 */
const char* nn_string_map_value(struct nn_string_map_iterator* it);

/**
 * return a iterator to the start of the key value map
 */
struct nn_string_map_iterator nn_string_map_begin(const struct nn_string_map* map);

/**
 * return iterator to the end of the map.
 */
struct nn_string_map_iterator nn_string_map_end(const struct nn_string_map* map);

/**
 * return true if the iterator is at the end.
 */
bool nn_string_map_is_end(const struct nn_string_map_iterator* it);

/**
 * increment the iterator to the next element
 */
void nn_string_map_next(struct nn_string_map_iterator* it);

#define NN_STRING_MAP_FOREACH(item, map) for(struct nn_string_map_iterator it = nn_string_map_front2(map); item = nn_string_map_get_element(&it), !nn_string_map_end(&it); nn_string_map_next(&it))

#ifdef __cplusplus
} //extern "C"
#endif

#endif
