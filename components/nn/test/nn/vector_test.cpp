#include <boost/test/unit_test.hpp>

#include <nn/vector.h>
#include <nn/allocator.h>

#include <stdint.h>

static struct nn_allocator defaultAllocator = {
    .calloc = &calloc,
    .free = &free
};

static void* failing_calloc(size_t n, size_t size)
{
    (void)n; (void)size;
    return NULL;
}

static struct nn_allocator failingAllocator = {
    .calloc = &failing_calloc,
    .free = &free
};

BOOST_AUTO_TEST_SUITE(vector)

BOOST_AUTO_TEST_CASE(init)
{
    struct nn_vector vector;
    nn_vector_init(&vector, sizeof(void*), &defaultAllocator);

    char* foo = strdup("foo");

    BOOST_TEST(nn_vector_size(&vector) == (size_t)0);
    BOOST_TEST(nn_vector_empty(&vector));
    BOOST_TEST(nn_vector_push_back(&vector, &foo));
    BOOST_TEST(nn_vector_size(&vector) == (size_t)1);
    BOOST_TEST(!nn_vector_empty(&vector));

    char* fooRet;
    nn_vector_get(&vector, 0, &fooRet);
    BOOST_TEST((void*)foo == (void*)fooRet);
    free(fooRet);

    nn_vector_deinit(&vector);
    // check with valgrind that no memory is leaked.
}

BOOST_AUTO_TEST_CASE(erase)
{
    struct nn_vector vector;
    nn_vector_init(&vector, sizeof(int), &defaultAllocator);

    int foo = 42;
    int bar = 43;
    int baz = 44;

    BOOST_TEST(nn_vector_push_back(&vector, &foo));
    BOOST_TEST(nn_vector_push_back(&vector, &bar));
    BOOST_TEST(nn_vector_push_back(&vector, &baz));

    BOOST_TEST(nn_vector_size(&vector) == (size_t)3);

    nn_vector_erase(&vector, 2);

    BOOST_TEST(nn_vector_size(&vector) == (size_t)2);

    nn_vector_erase(&vector, 0);
    nn_vector_erase(&vector, 0);

    BOOST_TEST(nn_vector_size(&vector) == (size_t)0);

    nn_vector_deinit(&vector);
    // check with valgrind that no memory is leaked.
}

BOOST_AUTO_TEST_CASE(iterator)
{
    struct nn_vector vector;
    nn_vector_init(&vector, sizeof(int), &defaultAllocator);

    int foo = 42;

    nn_vector_push_back(&vector, &foo);
    nn_vector_push_back(&vector, &foo);
    nn_vector_push_back(&vector, &foo);


    int element;
    NN_VECTOR_FOREACH(&element, &vector)
    {
        BOOST_TEST(element == foo);
    }

    nn_vector_deinit(&vector);
}

BOOST_AUTO_TEST_CASE(push_back_on_empty_vector)
{
    // The first push_back grows the capacity from 0 and used to copy from
    // the NULL elements pointer; UBSan reports that even for 0 bytes.
    struct nn_vector vector;
    nn_vector_init(&vector, sizeof(int), &defaultAllocator);

    int foo = 42;
    int bar = 43;
    BOOST_TEST(nn_vector_push_back(&vector, &foo));
    BOOST_TEST(nn_vector_size(&vector) == (size_t)1);
    BOOST_TEST(nn_vector_push_back(&vector, &bar));
    BOOST_TEST(nn_vector_size(&vector) == (size_t)2);

    int element;
    nn_vector_get(&vector, 0, &element);
    BOOST_TEST(element == foo);
    nn_vector_get(&vector, 1, &element);
    BOOST_TEST(element == bar);

    nn_vector_deinit(&vector);
}

#define BLOCK_SIZE 42
struct large_element {
    uint8_t block[BLOCK_SIZE];
};

BOOST_AUTO_TEST_CASE(larger_elements)
{

    struct nn_vector vector;
    nn_vector_init(&vector, sizeof(struct large_element), &defaultAllocator);

    struct large_element foo;
    memset(foo.block, 42, BLOCK_SIZE);

    nn_vector_push_back(&vector, &foo);
    nn_vector_push_back(&vector, &foo);
    nn_vector_push_back(&vector, &foo);

    struct large_element element;
    NN_VECTOR_FOREACH(&element, &vector)
    {
        BOOST_TEST(memcmp(element.block, foo.block, BLOCK_SIZE) == 0);
    }

    nn_vector_deinit(&vector);
}


BOOST_AUTO_TEST_CASE(reference_out_of_range_is_null)
{
    struct nn_vector vector;
    nn_vector_init(&vector, sizeof(int), &defaultAllocator);

    // An empty vector has no elements buffer at all; forming
    // elements + index on it is undefined behaviour, so the bounds test
    // has to come first.
    BOOST_TEST(nn_vector_reference(&vector, 0) == nullptr);

    int foo = 42;
    nn_vector_push_back(&vector, &foo);
    BOOST_TEST(nn_vector_reference(&vector, 0) != nullptr);
    BOOST_TEST(nn_vector_reference(&vector, 1) == nullptr);
    BOOST_TEST(nn_vector_reference(&vector, 4242) == nullptr);

    // The capacity doubles ahead of used, so index 1 is inside the
    // allocation here and would have been handed out unchecked.
    nn_vector_clear(&vector);
    BOOST_TEST(nn_vector_reference(&vector, 0) == nullptr);

    nn_vector_deinit(&vector);
}

BOOST_AUTO_TEST_CASE(foreach_reference_over_an_empty_vector)
{
    // NN_VECTOR_FOREACH_REFERENCE assigns the reference before it tests
    // for the end, so on an empty vector it used to form NULL + 0. That
    // is what clang's -fsanitize=undefined reports as "applying zero
    // offset to null pointer"; it reaches nn_set_insert through
    // nn_set_contains on every first insert into a set.
    struct nn_vector vector;
    nn_vector_init(&vector, sizeof(int), &defaultAllocator);

    size_t visited = 0;
    void* reference;
    NN_VECTOR_FOREACH_REFERENCE(reference, &vector)
    {
        visited++;
    }
    BOOST_TEST(visited == (size_t)0);
    BOOST_TEST(reference == nullptr);

    nn_vector_deinit(&vector);
}

BOOST_AUTO_TEST_CASE(push_back_refuses_to_overflow_the_capacity)
{
    // The growth is capacity*2 and the buffer is newCapacity*itemSize;
    // neither product was checked, and the memcpy which follows the
    // allocation uses the second one. Reaching the bound by pushing is not
    // possible, so set the capacity up directly - struct nn_vector is
    // public. push_back has to refuse before it allocates or copies
    // anything, so no element is ever touched here.
    struct nn_vector vector;
    int item = 42;

    // capacity*2 does not fit in a size_t.
    nn_vector_init(&vector, 1, &defaultAllocator);
    vector.used = vector.capacity = (SIZE_MAX / 2) + 1;
    BOOST_TEST(!nn_vector_push_back(&vector, &item));

    // capacity*2 fits, but the buffer it needs does not.
    nn_vector_init(&vector, 8, &defaultAllocator);
    vector.used = vector.capacity = SIZE_MAX / 4;
    BOOST_TEST(!nn_vector_push_back(&vector, &item));

    // One below the bound the guard lets it through, and the allocation
    // fails instead of wrapping. A real allocator is asked for the huge
    // buffer here, so use one which reports failure the way an embedded
    // allocator does rather than one which aborts the process.
    nn_vector_init(&vector, 2, &failingAllocator);
    vector.used = vector.capacity = SIZE_MAX / 4;
    BOOST_TEST(!nn_vector_push_back(&vector, &item));
}

BOOST_AUTO_TEST_SUITE_END()
