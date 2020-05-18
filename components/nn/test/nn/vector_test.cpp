#include <boost/test/unit_test.hpp>

#include <nn/vector.h>

BOOST_AUTO_TEST_SUITE(vector)

BOOST_AUTO_TEST_CASE(init)
{
    struct nn_vector vector;
    nn_vector_init(&vector, sizeof(void*));

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
    nn_vector_init(&vector, sizeof(int));

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
    nn_vector_init(&vector, sizeof(int));

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


BOOST_AUTO_TEST_SUITE_END()
