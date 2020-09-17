#include <boost/test/unit_test.hpp>

#include <nn/string_map.h>

BOOST_AUTO_TEST_SUITE(string_map)

BOOST_AUTO_TEST_CASE(basic)
{
    struct nn_string_map map;
    nn_string_map_init(&map);

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
    nn_string_map_init(&map);

    struct nn_string_map_iterator it1 = nn_string_map_insert(&map, "key", "value");
    struct nn_string_map_iterator it2 = nn_string_map_insert(&map, "key2", "value2");

    nn_string_map_erase(&map, "key");
    nn_string_map_erase_iterator(&map, &it2);

    BOOST_TEST(nn_string_map_size(&map) == (size_t)0);

    nn_string_map_deinit(&map);
}

BOOST_AUTO_TEST_SUITE_END()
