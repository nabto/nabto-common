#include <nn/set.h>
#include <nn/vector.h>

#include <stdlib.h>

void nn_set_init(struct nn_set* set, size_t itemSize, nn_set_less less)
{
    set->less = less;
    nn_vector_init(&set->items, itemSize);
}

void nn_set_deinit(struct nn_set* set)
{
    nn_vector_clear(&set->items);
    nn_vector_deinit(&set->items);
}

bool nn_set_insert(struct nn_set* set, void* item)
{
    if (nn_set_contains(set, item)) {
        return true;
    }
    if (!nn_vector_push_back(&set->items, item)) {
        return false;
    }
    return true;
}

bool nn_set_equal(const struct nn_set* set, const void* lhs, const void* rhs)
{
    // if !(a < b) && !(b < a) => a == b
    if (!(set->less(lhs, rhs)) && !(set->less(rhs, lhs))) {
        return true;
    }
    return false;
}

bool nn_set_contains(const struct nn_set* set, const void* item)
{
    const void* e;
    NN_VECTOR_FOREACH_REFERENCE(e, &set->items)
    {
        // 
        if (nn_set_equal(set, e, item)) {
            return true;
        }
    }
    return false;
}

void nn_set_erase(struct nn_set* set, const void* item)
{
    size_t s = nn_vector_size(&set->items);
    for (size_t i = 0; i < s; i++) {
        const void* e = nn_vector_reference(&set->items, i);
        if (nn_set_equal(set, item, e)) {
            nn_vector_erase(&set->items, i);
            return;
        }
    }
}

void nn_set_clear(struct nn_set* set)
{
    nn_vector_clear(&set->items);
}

bool nn_set_empty(struct nn_set* set)
{
    return nn_vector_empty(&set->items);
}

size_t nn_set_size(struct nn_set* set)
{
    return nn_vector_size(&set->items);
}


struct nn_set_iterator nn_set_begin(const struct nn_set* set)
{
    struct nn_set_iterator it;
    it.it = nn_vector_begin(&set->items);
    return it;
}

bool nn_set_is_end(const struct nn_set_iterator* it)
{
    return nn_vector_is_end(&it->it);
}

void nn_set_next(struct nn_set_iterator* it)
{
    nn_vector_next(&it->it);
}

const void* nn_set_get_element(const struct nn_set_iterator* it)
{
    const void* s;
    s = nn_vector_get_reference(&it->it);
    return s;
}
