#ifndef _STRING_SET_H_
#define _STRING_SET_H_

#include <nn/vector.h>

#ifdef __cplusplus
extern "C" {
#endif

struct nn_string_set {
    struct nn_vector strings;
};

struct nn_string_set_iterator {
    struct nn_vector_iterator it;
};

/**
 * init the string set
 */
void nn_string_set_init(struct nn_string_set* set);

/**
 * deinit the string set
 */
void nn_string_set_deinit(struct nn_string_set* set);

/**
 * add a string to a set, return false if allocation fails
 */
bool nn_string_set_insert(struct nn_string_set* set, const char* item);

/**
 * Test if the set contains the given string.
 */
bool nn_string_set_contains(const struct nn_string_set* set, const char* item);

/**
 * Erase an item with the given value if it exists
 */
void nn_string_set_erase(struct nn_string_set* set, const char* item);

/**
 * @return true iff the set is empty
 */
bool nn_string_set_empty(struct nn_string_set* set);

/**
 * @return the size of the set
 */
size_t nn_string_set_size(struct nn_string_set* set);

/**
 * Initialize the iterator to the first element in the set.
 */
struct nn_string_set_iterator nn_string_set_begin(const struct nn_string_set* set);

/**
 * @return true iff the iterator is at the end
 */
bool nn_string_set_is_end(const struct nn_string_set_iterator* it);

/**
 * Increment the iterator.
 */
void nn_string_set_next(struct nn_string_set_iterator* it);

/**
 * Get a reference to the string at the iterators position.
 */
const char* nn_string_set_get_element(const struct nn_string_set_iterator* it);

#define NN_STRING_SET_FOREACH(element, set) for(struct nn_string_set_iterator it = nn_string_set_begin(set); element = nn_string_set_get_element(&it), !nn_string_set_is_end(&it); nn_string_set_next(&it))

#ifdef __cplusplus
} //extern "C"
#endif


#endif
