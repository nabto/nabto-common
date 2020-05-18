#include <boost/test/unit_test.hpp>

#include <nn/llist.h>

BOOST_AUTO_TEST_SUITE(llist)

BOOST_AUTO_TEST_CASE(init_deinit)
{
    struct nn_llist list;
    nn_llist_init(&list);
    nn_llist_deinit(&list);
}

BOOST_AUTO_TEST_CASE(create_insert_remove)
{
    struct nn_llist list;
    nn_llist_init(&list);

    struct nn_llist_node item1;
    struct nn_llist_node item2;

    const char* foo = "foo";
    const char* bar = "bar";

    BOOST_TEST(nn_llist_empty(&list));
    nn_llist_append(&list, &item1, (void*)foo);
    nn_llist_append(&list, &item2, (void*)bar);

    BOOST_TEST(nn_llist_size(&list) == (size_t)2);

    struct nn_llist_iterator it = nn_llist_begin(&list);

    BOOST_TEST(!nn_llist_is_end(&it));
    BOOST_TEST(nn_llist_get_item(&it) == (void*)foo);
    nn_llist_next(&it);
    BOOST_TEST(nn_llist_get_item(&it) == (void*)bar);
    nn_llist_next(&it);
    BOOST_TEST(nn_llist_is_end(&it));

    it = nn_llist_begin(&list);
    nn_llist_erase(&it);
    it = nn_llist_begin(&list);
    nn_llist_erase(&it);
    BOOST_TEST(nn_llist_empty(&list));

    nn_llist_deinit(&list);
}

BOOST_AUTO_TEST_CASE(list_end)
{
    struct nn_llist list;
    nn_llist_init(&list);

    struct nn_llist_iterator it = nn_llist_end(&list);
    BOOST_TEST(nn_llist_is_end(&it));

    nn_llist_deinit(&list);
}

BOOST_AUTO_TEST_CASE(insert_before_end)
{
    struct nn_llist list;
    nn_llist_init(&list);
    struct nn_llist_node item1;
    struct nn_llist_node item2;
    nn_llist_append(&list, &item1, (void*)1);
    struct nn_llist_iterator it = nn_llist_end(&list);

    nn_llist_insert_before(&it, &item2, (void*)2);

    it = nn_llist_begin(&list);

    BOOST_TEST(nn_llist_get_item(&it) == (void*)1);
    nn_llist_next(&it);
    BOOST_TEST(nn_llist_get_item(&it) == (void*)2);

}

BOOST_AUTO_TEST_CASE(insert_before_front)
{
    struct nn_llist list;
    nn_llist_init(&list);
    struct nn_llist_node item1;
    struct nn_llist_node item2;
    nn_llist_append(&list, &item1, (void*)1);
    struct nn_llist_iterator it = nn_llist_begin(&list);

    nn_llist_insert_before(&it, &item2, (void*)2);

    it = nn_llist_begin(&list);

    BOOST_TEST(nn_llist_get_item(&it) == (void*)2);
    nn_llist_next(&it);
    BOOST_TEST(nn_llist_get_item(&it) == (void*)1);

}

BOOST_AUTO_TEST_SUITE_END()
