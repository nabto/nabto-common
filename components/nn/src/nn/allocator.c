#include <nn/allocator.h>

void* nn_allocator_calloc(struct nn_allocator* allocator, size_t n, size_t size)
{
    if (allocator->calloc == NULL) {
        return NULL;
    }
    return allocator->calloc(n, size);
}

void nn_allocator_free(struct nn_allocator* allocator, void* ptr)
{
    if (allocator->free == NULL) {
        return;
    }
    allocator->free(ptr);
}
