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