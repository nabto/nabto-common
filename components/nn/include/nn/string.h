#ifndef _NN_STRING_H_
#define _NN_STRING_H_

#include <nn/allocator.h>

/**
 * like strdup, duplicate a null terminated c string.
 */
char* nn_strdup(const char* str, struct nn_allocator* allocator);

#endif