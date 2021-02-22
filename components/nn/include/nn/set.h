#ifndef _NN_SET_H_
#define _NN_SET_H_

#include <nn/vector.h>

#include <string.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Set of simple data types.
 */

// compare two items;
typedef bool (*nn_set_less)(const void* lhs, const void* rhs);

struct nn_set {
    nn_set_less less;
    struct nn_vector items;
};

struct nn_set_iterator {
    struct nn_vector_iterator it;
};

/**
 * Initialize a set.
 * 
 * @param set the set.
 * @param itemSize the size of each item, e.g. sizeof(int).
 * @param less Comparator function comparing two items by their pointer.
 */
void nn_set_init(struct nn_set* set, size_t itemSize, nn_set_less less);

/**
 * Deinitialize a set.
 */
void nn_set_deinit(struct nn_set* set);

/**
 * Insert an item into a set.
 * 
 * @return false iff the allocation failed
 */
bool nn_set_insert(struct nn_set* set, void* item);

/**
 * Test if the set contains the given item.
 * 
 * @return true if the item exists
 */
bool nn_set_contains(const struct nn_set* set, const void* item);

/**
 * Erase an item with the given value if it exists
 */
void nn_set_erase(struct nn_set* set, const void* item);

/**
 * Erase all items in the set
 */
void nn_set_clear(struct nn_set* set);

/**
 * @return true iff the set is empty
 */
bool nn_set_empty(struct nn_set* set);

/**
 * @return the size of the set
 */
size_t nn_set_size(struct nn_set* set);

/**
 * Initialize the iterator to the first element in the set.
 */
struct nn_set_iterator nn_set_begin(const struct nn_set* set);

/**
 * @return true iff the iterator is at the end
 */
bool nn_set_is_end(const struct nn_set_iterator* it);

/**
 * Increment the iterator.
 */
void nn_set_next(struct nn_set_iterator* it);

/**
 * Get a the item at the iterators position.
 */
void nn_set_get_element(const struct nn_set_iterator* it, void* item);

#define NN_SET_FOREACH(item, set) for(struct nn_set_iterator it = nn_set_begin(set); nn_set_get_element(&it, item), !nn_set_is_end(&it); nn_set_next(&it))



#ifdef __cplusplus
} //extern "C"
#endif

#endif
