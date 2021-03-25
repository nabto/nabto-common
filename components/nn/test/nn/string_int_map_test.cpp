#include <boost/test/unit_test.hpp>

#include <nn/string_int_map.h>


BOOST_AUTO_TEST_SUITE(string_int_map)

BOOST_AUTO_TEST_CASE(basic)
{
    struct nn_string_int_map map;
    nn_string_int_map_init(&map);

    struct nn_string_int_map_iterator it = nn_string_int_map_insert(&map, "key", 42);

    BOOST_TEST(!nn_string_int_map_is_end(&it));
    it = nn_string_int_map_get(&map, "key");
    BOOST_TEST(!nn_string_int_map_is_end(&it));

    BOOST_TEST(nn_string_int_map_value(&it) == 42);

    it = nn_string_int_map_get(&map, "nonexisting");
    BOOST_TEST(nn_string_int_map_is_end(&it));

    nn_string_int_map_deinit(&map);
}

BOOST_AUTO_TEST_SUITE_END()
