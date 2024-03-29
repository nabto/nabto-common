#include <nn/vector.h>

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

void nn_vector_init(struct nn_vector* vector, size_t itemSize, struct nn_allocator* allocator)
{
    memset(vector, 0, sizeof(struct nn_vector));
    vector->itemSize = itemSize;
    vector->allocator = *allocator;
}

void nn_vector_deinit(struct nn_vector* vector)
{
    nn_allocator_free(&vector->allocator, vector->elements);
    vector->elements = NULL;
    vector->capacity = 0;
    vector->used = 0;
    vector->itemSize = 0;
}

bool nn_vector_push_back(struct nn_vector* vector, void* element)
{
    if (vector->used == vector->capacity) {
        size_t newCapacity = vector->capacity*2;
        if (newCapacity == 0) {
            newCapacity = 1;
        }
        void** newElements = vector->allocator.calloc(newCapacity,vector->itemSize);
        if (newElements == NULL) {
            return false;
        }
        memcpy(newElements, vector->elements, (vector->capacity * vector->itemSize));
        vector->allocator.free(vector->elements);
        vector->elements = newElements;
        vector->capacity = newCapacity;
    }
    memcpy((uint8_t*)vector->elements + (vector->used*vector->itemSize), element, vector->itemSize);
    vector->used += 1;
    return true;
}

void nn_vector_erase(struct nn_vector* vector, size_t index)
{
    if (index < vector->used) {
        // eg. used = 2, remove index 1 aka last element,
        // used -= 1; used(1) - index(1) = 0
        vector->used -= 1;
        size_t after = vector->used - index;
        memmove((uint8_t*)vector->elements + (index * vector->itemSize), (uint8_t*)vector->elements + ((index+1) * vector->itemSize), vector->itemSize * after);
    }
}

bool nn_vector_empty(const struct nn_vector* vector)
{
    return vector->used == 0;
}

size_t nn_vector_size(const struct nn_vector* vector)
{
    return vector->used;
}

void nn_vector_get(const struct nn_vector* vector, size_t index, void* element)
{
    if (index < vector->used) {
        memcpy(element, (uint8_t*)vector->elements + (index * vector->itemSize), vector->itemSize);
    }
}

void* nn_vector_reference(const struct nn_vector* vector, size_t index)
{
    return (void*)((uint8_t*)vector->elements + (index * vector->itemSize));
}

void nn_vector_clear(struct nn_vector* vector)
{
    vector->used = 0;
}

struct nn_vector_iterator nn_vector_begin(const struct nn_vector* vector)
{
    struct nn_vector_iterator iterator;
    iterator.vector = vector;
    iterator.current = 0;
    return iterator;
}

void nn_vector_next(struct nn_vector_iterator* iterator)
{
    iterator->current += 1;
}

bool nn_vector_is_end(const struct nn_vector_iterator* iterator)
{
    return iterator->current >= iterator->vector->used;
}

void nn_vector_get_element(const struct nn_vector_iterator* iterator, void* element)
{
    nn_vector_get(iterator->vector, iterator->current, element);
}

void* nn_vector_get_reference(const struct nn_vector_iterator* it)
{
    const struct nn_vector* vector = it->vector;
    return nn_vector_reference(vector, it->current);
}
