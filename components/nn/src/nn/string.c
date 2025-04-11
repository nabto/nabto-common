#include <nn/string.h>

char* nn_strdup(const char* str, struct nn_allocator* allocator)
{
    size_t len = strlen(str);
    char* dupBuf = allocator->calloc(1, len + 1);
    if (dupBuf == NULL) {
        return NULL;
    }
    memcpy(dupBuf, str, len+1);
    return dupBuf;
}

bool nn_strcat(char* dst, size_t dstLen, const char* src)
{
    size_t srcLen = strlen(src);
    size_t orgStrLen = strlen(dst);
    if (dstLen > srcLen+orgStrLen) {
        // We have space for the string in dst and the src string and a trailing NULL
        memcpy(dst+orgStrLen, src, srcLen+1);
        return true;
    } else {
        return false;
    }
}
