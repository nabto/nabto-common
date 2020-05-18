#ifndef _NN_VECTOR_H_
#define _NN_VECTOR_H_

#include <stdlib.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif


struct nn_vector {
    void* elements;
    size_t capacity;
    size_t used;
    size_t itemSize;
};

struct nn_vector_iterator {
    const struct nn_vector* vector;
    size_t current;
};


void nn_vector_init(struct nn_vector* vector, size_t itemSize);
void nn_vector_deinit(struct nn_vector* vector);

/**
 * Add an element to the end of the vector.
 *
 * @return false iff reallocation fails.
 */
bool nn_vector_push_back(struct nn_vector* vector, void* element);

/**
 * return the number of elements in the vector
 */
size_t nn_vector_size(const struct nn_vector* vector);

/**
 * get the element at the specific index.
 */
void nn_vector_get(const struct nn_vector* vector, size_t index, void* element);

/**
 * return true iff the vector is empty
 */
bool nn_vector_empty(const struct nn_vector* vector);

/**
 * erase element at position and move all the other elements
 */
void nn_vector_erase(struct nn_vector* vector, size_t index);

/**
 * clear vector
 */
void nn_vector_clear(struct nn_vector* vector);

/**
 * initialize the iterator to the beginning of the vector.
 */
struct nn_vector_iterator nn_vector_begin(const struct nn_vector* vector);

/**
 * Get the element the iterator is at.
 */
void nn_vector_get_element(const struct nn_vector_iterator* it, void* element);

/**
 * increment the iterator.
 */
void nn_vector_next(struct nn_vector_iterator* it);

/**
 * test if the iterator has reached the end.
 */
bool nn_vector_is_end(const struct nn_vector_iterator* it);

/**
 * Helper macro to iterate over all elements in the vector
 */
#define NN_VECTOR_FOREACH(element, vector) for (struct nn_vector_iterator it = nn_vector_begin(vector); nn_vector_get_element(&it, element), !nn_vector_is_end(&it); nn_vector_next(&it))

#ifdef __cplusplus
} //extern "C"
#endif


#endif
