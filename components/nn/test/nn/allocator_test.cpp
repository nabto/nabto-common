#include <boost/test/unit_test.hpp>

#include <nn/allocator.h>
#include <nn/vector.h>
#include <nn/string.h>
#include <nn/string_map.h>
#include <nn/string_int_map.h>
#include <nn/string_set.h>

#include <stdlib.h>

// nn_allocator_calloc and nn_allocator_free exist to make a partially
// populated struct nn_allocator safe, so every container has to reach the
// allocator through them rather than through the function pointers.
static struct nn_allocator emptyAllocator = {
    .calloc = NULL,
    .free = NULL
};

static struct nn_allocator noCallocAllocator = {
    .calloc = NULL,
    .free = &free
};

BOOST_AUTO_TEST_SUITE(allocator)

BOOST_AUTO_TEST_CASE(wrappers_tolerate_missing_functions)
{
    BOOST_TEST(nn_allocator_calloc(&emptyAllocator, 1, 16) == nullptr);
    nn_allocator_free(&emptyAllocator, NULL);
}

BOOST_AUTO_TEST_CASE(strdup_without_a_calloc)
{
    BOOST_TEST(nn_strdup("hello", &emptyAllocator) == nullptr);
}

BOOST_AUTO_TEST_CASE(vector_without_a_calloc)
{
    struct nn_vector vector;
    nn_vector_init(&vector, sizeof(int), &emptyAllocator);

    int item = 42;
    BOOST_TEST(!nn_vector_push_back(&vector, &item));
    BOOST_TEST(nn_vector_size(&vector) == (size_t)0);

    nn_vector_deinit(&vector);
}

BOOST_AUTO_TEST_CASE(string_set_without_a_calloc)
{
    struct nn_string_set set;
    nn_string_set_init(&set, &noCallocAllocator);

    BOOST_TEST(!nn_string_set_insert(&set, "foo"));
    BOOST_TEST(nn_string_set_empty(&set));

    nn_string_set_deinit(&set);
}

BOOST_AUTO_TEST_CASE(string_map_without_a_calloc)
{
    struct nn_string_map map;
    nn_string_map_init(&map, &noCallocAllocator);

    struct nn_string_map_iterator it = nn_string_map_insert(&map, "key", "value");
    BOOST_TEST(nn_string_map_is_end(&it));
    BOOST_TEST(nn_string_map_empty(&map));

    nn_string_map_deinit(&map);
}

BOOST_AUTO_TEST_CASE(string_int_map_without_a_calloc)
{
    struct nn_string_int_map map;
    nn_string_int_map_init(&map, &noCallocAllocator);

    struct nn_string_int_map_iterator it = nn_string_int_map_insert(&map, "key", 42);
    BOOST_TEST(nn_string_int_map_is_end(&it));

    nn_string_int_map_deinit(&map);
}

BOOST_AUTO_TEST_SUITE_END()
