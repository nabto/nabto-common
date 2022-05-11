#include <boost/test/unit_test.hpp>

#include <nn/endian.h>
#include <arpa/inet.h>

BOOST_AUTO_TEST_SUITE(endian)

// The endian conversion macros are universal. This test should be sufficient.

BOOST_AUTO_TEST_CASE(htons_test)
{
    BOOST_TEST(htons(4242) == nn_htons(4242));
    BOOST_TEST(ntohs(4242) == nn_ntohs(4242));
}

BOOST_AUTO_TEST_CASE(htonl_test)
{
    BOOST_TEST(htonl(42424242) == nn_htonl(42424242));
    BOOST_TEST(ntohl(42424242) == nn_ntohl(42424242));
}

BOOST_AUTO_TEST_CASE(htonll_test)
{
    BOOST_TEST(nn_ntohll(nn_htonll(4242424242424242)) == 4242424242424242);
}

BOOST_AUTO_TEST_SUITE_END()
