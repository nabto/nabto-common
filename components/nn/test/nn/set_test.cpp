#include <boost/test/unit_test.hpp>


#include <nn/set.h>

#include <set>

static struct nn_allocator defaultAllocator = {
    .calloc = &calloc,
    .free = &free
};


bool int_less(const void* lhs, const void* rhs)
{
    const int* l = (const int*)lhs;
    const int* r = (const int*)rhs;
    return (*l) < (*r);
}

BOOST_AUTO_TEST_SUITE(set)

BOOST_AUTO_TEST_CASE(init_deinit)
{
    struct nn_set set;
    nn_set_init(&set, sizeof(int), int_less, &defaultAllocator);
    nn_set_deinit(&set);
}

BOOST_AUTO_TEST_CASE(add_contains)
{
    struct nn_set set;
    nn_set_init(&set, sizeof(int), int_less, &defaultAllocator);

    int foo = 2;
    int bar = 3;
    int foobar = 4;

    nn_set_insert(&set, &foo);
    nn_set_insert(&set, &bar);

    BOOST_TEST(nn_set_contains(&set, &foo));
    BOOST_TEST(!nn_set_contains(&set, &foobar));

    nn_set_deinit(&set);
}

BOOST_AUTO_TEST_CASE(erase_item)
{
    struct nn_set set;
    nn_set_init(&set, sizeof(int), int_less, &defaultAllocator);

    int foo = 2;
    int bar = 3;


    nn_set_insert(&set, &foo);
    nn_set_insert(&set, &bar);

    nn_set_erase(&set, &foo);

    BOOST_TEST(!nn_set_contains(&set, &foo));
    BOOST_TEST(nn_set_contains(&set, &bar));

    nn_set_erase(&set, &bar);

    BOOST_TEST(!nn_set_contains(&set, &bar));

    nn_set_deinit(&set);

}

BOOST_AUTO_TEST_CASE(iterator)
{
    struct nn_set set;
    nn_set_init(&set, sizeof(int), int_less, &defaultAllocator);

    int foo = 2;
    int bar = 3;

    nn_set_insert(&set, &foo);
    nn_set_insert(&set, &bar);

    std::set<int> items;
    int item;
    NN_SET_FOREACH(&item, &set)
    {
        items.insert(item);
    }

    BOOST_TEST(items.count(2) == (size_t)1);
    BOOST_TEST(items.count(3) == (size_t)1);
    BOOST_TEST(items.size() == (size_t)2);

    nn_set_deinit(&set);
}


BOOST_AUTO_TEST_SUITE_END();
