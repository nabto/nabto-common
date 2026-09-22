#include <boost/test/unit_test.hpp>

#include <nn/string_int_map.h>

#include <string>


static struct nn_allocator defaultAllocator = {
    .calloc = &calloc,
    .free = &free
};

BOOST_AUTO_TEST_SUITE(string_int_map)

BOOST_AUTO_TEST_CASE(basic)
{
    struct nn_string_int_map map;
    nn_string_int_map_init(&map, &defaultAllocator);

    struct nn_string_int_map_iterator it = nn_string_int_map_insert(&map, "key", 42);

    BOOST_TEST(!nn_string_int_map_is_end(&it));
    it = nn_string_int_map_get(&map, "key");
    BOOST_TEST(!nn_string_int_map_is_end(&it));

    BOOST_TEST(nn_string_int_map_value(&it) == 42);

    it = nn_string_int_map_get(&map, "nonexisting");
    BOOST_TEST(nn_string_int_map_is_end(&it));

    nn_string_int_map_deinit(&map);
}

BOOST_AUTO_TEST_CASE(key_and_value)
{
    struct nn_string_int_map map;
    nn_string_int_map_init(&map, &defaultAllocator);

    nn_string_int_map_insert(&map, "port", 4242);

    struct nn_string_int_map_iterator it = nn_string_int_map_get(&map, "port");
    BOOST_TEST_REQUIRE(!nn_string_int_map_is_end(&it));
    BOOST_TEST(std::string(nn_string_int_map_key(&it)) == "port");
    BOOST_TEST(nn_string_int_map_value(&it) == 4242);

    nn_string_int_map_deinit(&map);
}

BOOST_AUTO_TEST_CASE(insert_keeps_the_existing_value)
{
    // Documented behaviour: inserting a key which is already there returns
    // the existing item rather than replacing its value.
    struct nn_string_int_map map;
    nn_string_int_map_init(&map, &defaultAllocator);

    nn_string_int_map_insert(&map, "key", 1);
    struct nn_string_int_map_iterator it = nn_string_int_map_insert(&map, "key", 2);
    BOOST_TEST_REQUIRE(!nn_string_int_map_is_end(&it));
    BOOST_TEST(nn_string_int_map_value(&it) == 1);

    nn_string_int_map_deinit(&map);
}

BOOST_AUTO_TEST_CASE(erase)
{
    struct nn_string_int_map map;
    nn_string_int_map_init(&map, &defaultAllocator);

    nn_string_int_map_insert(&map, "first", 1);
    nn_string_int_map_insert(&map, "second", 2);

    nn_string_int_map_erase(&map, "first");

    struct nn_string_int_map_iterator it = nn_string_int_map_get(&map, "first");
    BOOST_TEST(nn_string_int_map_is_end(&it));
    it = nn_string_int_map_get(&map, "second");
    BOOST_TEST_REQUIRE(!nn_string_int_map_is_end(&it));
    BOOST_TEST(nn_string_int_map_value(&it) == 2);

    // Erasing a key which is not there is a no-op.
    nn_string_int_map_erase(&map, "first");
    nn_string_int_map_erase(&map, "nonexisting");

    nn_string_int_map_deinit(&map);
}

BOOST_AUTO_TEST_CASE(erase_iterator)
{
    struct nn_string_int_map map;
    nn_string_int_map_init(&map, &defaultAllocator);

    nn_string_int_map_insert(&map, "key", 42);

    struct nn_string_int_map_iterator it = nn_string_int_map_get(&map, "key");
    BOOST_TEST_REQUIRE(!nn_string_int_map_is_end(&it));
    // it is dangling after this call, see string_int_map.h.
    nn_string_int_map_erase_iterator(&map, &it);

    it = nn_string_int_map_get(&map, "key");
    BOOST_TEST(nn_string_int_map_is_end(&it));

    nn_string_int_map_deinit(&map);
}

BOOST_AUTO_TEST_SUITE_END()
