#include <boost/test/unit_test.hpp>

#include <nn/string_map.h>

static struct nn_allocator defaultAllocator = {
    .calloc = &calloc,
    .free = &free
};

BOOST_AUTO_TEST_SUITE(string_map)

BOOST_AUTO_TEST_CASE(basic)
{
    struct nn_string_map map;
    nn_string_map_init(&map, &defaultAllocator);

    struct nn_string_map_iterator it = nn_string_map_insert(&map, "key", "value");

    BOOST_TEST(!nn_string_map_is_end(&it));
    it = nn_string_map_get(&map, "key");
    BOOST_TEST(!nn_string_map_is_end(&it));

    BOOST_TEST(strcmp(nn_string_map_value(&it), "value") == 0);

    it = nn_string_map_get(&map, "nonexisting");
    BOOST_TEST(nn_string_map_is_end(&it));

    nn_string_map_deinit(&map);
}

BOOST_AUTO_TEST_CASE(erase)
{
    struct nn_string_map map;
    nn_string_map_init(&map, &defaultAllocator);

    struct nn_string_map_iterator it1 = nn_string_map_insert(&map, "key", "value");
    struct nn_string_map_iterator it2 = nn_string_map_insert(&map, "key2", "value2");

    nn_string_map_erase(&map, "key");
    nn_string_map_erase_iterator(&map, &it2);

    BOOST_TEST(nn_string_map_size(&map) == (size_t)0);

    nn_string_map_deinit(&map);
}

BOOST_AUTO_TEST_CASE(getn_exact_length)
{
    struct nn_string_map map;
    nn_string_map_init(&map, &defaultAllocator);

    // Insert the longer key first so a prefix match on the shorter lookup
    // key finds the wrong item.
    nn_string_map_insert(&map, "Connection:UserId", "user");
    nn_string_map_insert(&map, "Connection:User", "short");

    struct nn_string_map_iterator it = nn_string_map_getn(&map, "Connection:UserId", 17);
    BOOST_TEST(!nn_string_map_is_end(&it));
    BOOST_TEST(strcmp(nn_string_map_value(&it), "user") == 0);

    it = nn_string_map_getn(&map, "Connection:User", 15);
    BOOST_TEST(!nn_string_map_is_end(&it));
    BOOST_TEST(strcmp(nn_string_map_value(&it), "short") == 0);

    // a strict prefix of a stored key is not a match
    it = nn_string_map_getn(&map, "Connection:Use", 14);
    BOOST_TEST(nn_string_map_is_end(&it));

    // neither is a longer key
    it = nn_string_map_getn(&map, "Connection:UserIdX", 18);
    BOOST_TEST(nn_string_map_is_end(&it));

    it = nn_string_map_getn(&map, "", 0);
    BOOST_TEST(nn_string_map_is_end(&it));

    nn_string_map_deinit(&map);
}

BOOST_AUTO_TEST_CASE(getn_key_not_terminated)
{
    struct nn_string_map map;
    nn_string_map_init(&map, &defaultAllocator);

    nn_string_map_insert(&map, "Connection:UserId", "user");

    // lookup key is a slice of a longer string, as when resolving
    // ${Connection:UserId} in an IAM policy
    const char* variable = "${Connection:UserId}";
    struct nn_string_map_iterator it = nn_string_map_getn(&map, variable + 2, strlen(variable) - 3);
    BOOST_TEST(!nn_string_map_is_end(&it));
    BOOST_TEST(strcmp(nn_string_map_value(&it), "user") == 0);

    nn_string_map_deinit(&map);
}

BOOST_AUTO_TEST_SUITE_END()
