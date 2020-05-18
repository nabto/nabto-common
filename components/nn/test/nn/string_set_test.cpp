#include <boost/test/unit_test.hpp>


#include <nn/string_set.h>

#include <set>

BOOST_AUTO_TEST_SUITE(string_set)

BOOST_AUTO_TEST_CASE(init_deinit)
{
    struct nn_string_set set;
    nn_string_set_init(&set);
    nn_string_set_deinit(&set);
}

BOOST_AUTO_TEST_CASE(add_contains)
{
    struct nn_string_set set;
    nn_string_set_init(&set);

    nn_string_set_insert(&set, "foo");
    nn_string_set_insert(&set, "bar");

    BOOST_TEST(nn_string_set_contains(&set, "foo"));
    BOOST_TEST(!nn_string_set_contains(&set, "foobar"));

    nn_string_set_deinit(&set);
}

BOOST_AUTO_TEST_CASE(erase_item)
{
    struct nn_string_set set;
    nn_string_set_init(&set);

    nn_string_set_insert(&set, "foo");
    nn_string_set_insert(&set, "bar");

    nn_string_set_erase(&set, "foo");

    BOOST_TEST(!nn_string_set_contains(&set, "foo"));
    BOOST_TEST(nn_string_set_contains(&set, "bar"));

    nn_string_set_erase(&set, "bar");

    BOOST_TEST(!nn_string_set_contains(&set, "bar"));

    nn_string_set_deinit(&set);

}

BOOST_AUTO_TEST_CASE(iterator)
{
    struct nn_string_set set;
    nn_string_set_init(&set);

    nn_string_set_insert(&set, "foo");
    nn_string_set_insert(&set, "bar");

    std::set<std::string> items;
    const char* str;
    NN_STRING_SET_FOREACH(str, &set)
    {
        items.insert(std::string(str));
    }

    BOOST_TEST(items.count("foo") == (size_t)1);
    BOOST_TEST(items.count("bar") == (size_t)1);
    BOOST_TEST(items.size() == (size_t)2);

    nn_string_set_deinit(&set);
}


BOOST_AUTO_TEST_SUITE_END();
