#ifndef _NN_STRING_H_
#define _NN_STRING_H_

#include <nn/allocator.h>
#include <stdbool.h>

/**
 * like strdup, duplicate a null terminated c string.
 */
char* nn_strdup(const char* str, struct nn_allocator* allocator);

/**
 * Append the src string to the destination string if enough space is available.
 * The src string is only copied if the remaining space in dst has room for the string and a trailing NULL.
 *
 * dstLen must be the total allocated size of dst. This means this function will only succeed if strlen(dst)+strlen(src) < dstLen
 *
 * @param dst The string to append to
 * @param dstLen The total allocated size of dst
 * @param src The string to copy
 * @return true on success
 */
bool nn_strcat(char* dst, size_t dstLen, const char* src);



#endif
