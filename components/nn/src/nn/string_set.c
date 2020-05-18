#include <nn/string_set.h>


#include <stdlib.h>
#include <string.h>

void nn_string_set_init(struct nn_string_set* set)
{
    nn_vector_init(&set->strings, sizeof(char*));
}

void nn_string_set_deinit(struct nn_string_set* set)
{
    char* str;
    NN_VECTOR_FOREACH(&str, &set->strings)
    {
        free(str);
    }
    nn_vector_clear(&set->strings);
    nn_vector_deinit(&set->strings);
}

bool nn_string_set_insert(struct nn_string_set* set, const char* item)
{
    if (nn_string_set_contains(set, item)) {
        return true;
    }
    char* dup = strdup(item);
    if (dup == NULL) {
        return false;
    }
    if (!nn_vector_push_back(&set->strings, &dup)) {
        free(dup);
        return false;
    }
    return true;
}

bool nn_string_set_contains(const struct nn_string_set* set, const char* item)
{
    char* e;
    NN_VECTOR_FOREACH(&e, &set->strings)
    {
        if (strcmp(e,item) == 0) {
            return true;
        }
    }
    return false;
}

void nn_string_set_erase(struct nn_string_set* set, const char* item)
{
    size_t s = nn_vector_size(&set->strings);
    for (size_t i = 0; i < s; i++) {
        char* e;
        nn_vector_get(&set->strings, i, &e);
        if (strcmp(item, e) == 0) {
            nn_vector_erase(&set->strings, i);
            free(e);
            return;
        }
    }
}

bool nn_string_set_empty(struct nn_string_set* set)
{
    return nn_vector_empty(&set->strings);
}

size_t nn_string_set_size(struct nn_string_set* set)
{
    return nn_vector_size(&set->strings);
}


struct nn_string_set_iterator nn_string_set_begin(const struct nn_string_set* set)
{
    struct nn_string_set_iterator it;
    it.it = nn_vector_begin(&set->strings);
    return it;
}

bool nn_string_set_is_end(const struct nn_string_set_iterator* it)
{
    return nn_vector_is_end(&it->it);
}

void nn_string_set_next(struct nn_string_set_iterator* it)
{
    nn_vector_next(&it->it);
}

const char* nn_string_set_get_element(const struct nn_string_set_iterator* it)
{
    const char* s;
    nn_vector_get_element(&it->it, &s);
    return s;
}
