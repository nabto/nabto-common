#ifndef _NN_ALLOCATOR_H_
#define _NN_ALLOCATOR_H_

#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif


typedef void* (*nn_calloc)(size_t n, size_t size);
typedef void (*nn_free)(void* ptr);

struct nn_allocator {
    nn_calloc calloc;
    nn_free free;
};

void* nn_allocator_calloc(struct nn_allocator* allocator, size_t n, size_t size);
void nn_allocator_free(struct nn_allocator* allocator, void* ptr);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
