#include <boost/test/unit_test.hpp>

#include <nn/ip_address.h>

BOOST_AUTO_TEST_SUITE(ip_address)

BOOST_AUTO_TEST_CASE(assign_ipv4)
{
    struct nn_ip_address ip;
    nn_ip_address_assign_v4(&ip, 2130706433 /*127.0.0.1*/);

    const char* str = nn_ip_address_to_string(&ip);
    BOOST_TEST(std::string(str) == "127.0.0.1");
}

BOOST_AUTO_TEST_CASE(is_v4_mapped)
{
    struct nn_ip_address ip;
    nn_ip_address_assign_v4(&ip, 2130706433 /*127.0.0.1*/);

    struct nn_ip_address v6;
    nn_ip_convert_v4_to_v4_mapped(&ip, &v6);

    BOOST_TEST(nn_ip_is_v4_mapped(&v6));

    const char* str = nn_ip_address_to_string(&v6);
    BOOST_TEST(std::string(str) == "0000:0000:0000:0000:0000:ffff:7f00:0001");
}

BOOST_AUTO_TEST_CASE(is_v4)
{
    struct nn_ip_address ip;
    ip.type = NN_IPV4;
    BOOST_TEST(nn_ip_is_v4(&ip));
}

BOOST_AUTO_TEST_CASE(is_v6)
{
    struct nn_ip_address ip;
    ip.type = NN_IPV6;
    BOOST_TEST(nn_ip_is_v6(&ip));
}

BOOST_AUTO_TEST_CASE(read_v4)
{
    std::vector<std::string> ips = { "1.2.3.4", "123.255.0.0", "0.0.0.0" };

    for (auto ip : ips) {
        struct nn_ip_address address;
        BOOST_TEST(nn_ip_address_read_v4(ip.c_str(), &address));
        BOOST_TEST(std::string(nn_ip_address_to_string(&address)) == ip);
    }
}

BOOST_AUTO_TEST_SUITE_END()
