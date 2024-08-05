#include <boost/test/unit_test.hpp>
#include <nabto_stun/nabto_stun_message.h>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t* write_forward(uint8_t* buf, uint8_t* val, uint16_t size)
{
    int i;
    for (i = 0; i < size; i++) {
        *(buf+i) = *(val+i);
    }
    return buf+size;
}

uint8_t* uint16_write_forward(uint8_t* buf, uint16_t val)
{
    uint8_t d0 = (uint8_t)(((val) >> 8) & 0xff);
    uint8_t d1 = (uint8_t)( (val)       & 0xff);
    *buf = d0;
    buf++;
    *buf = d1;
    buf++;
    return buf;
}

uint8_t* uint32_write_forward(uint8_t* buf, uint32_t val)
{
    uint8_t d0 = (uint8_t)(((val) >> 24) & 0xff);
    uint8_t d1 = (uint8_t)(((val) >> 16) & 0xff);
    uint8_t d2 = (uint8_t)(((val) >> 8)  & 0xff);
    uint8_t d3 = (uint8_t)( (val)        & 0xff);
    *buf = d0;
    buf++;
    *buf = d1;
    buf++;
    *buf = d2;
    buf++;
    *buf = d3;
    buf++;
    return buf;
}

#ifdef __cplusplus
} //extern "C"
#endif

BOOST_AUTO_TEST_SUITE(stun)

BOOST_AUTO_TEST_CASE(only_decode_bining_response)
{
    uint8_t buf[128];
    uint8_t* ptr = buf;
    ptr = uint16_write_forward(ptr, STUN_MESSAGE_BINDING_REQUEST); // failure origin
    ptr = uint16_write_forward(ptr, 0);
    struct nabto_stun_message msg;
    bool res = nabto_stun_decode_message(&msg, buf, 20);
    BOOST_TEST(!res);
}

BOOST_AUTO_TEST_CASE(decode_invalid_header)
{
    uint8_t buf[128];
    uint8_t* ptr = buf;
    ptr = uint16_write_forward(ptr, STUN_MESSAGE_BINDING_RESPONSE_SUCCESS);
    ptr = uint16_write_forward(ptr, 20); // failure origin
    struct nabto_stun_message msg;
    bool res = nabto_stun_decode_message(&msg, buf, 20);
    BOOST_TEST(!res);
}

BOOST_AUTO_TEST_CASE(decode_invalid_att_header)
{
    uint8_t buf[128];
    uint8_t* ptr = buf;
    ptr = uint16_write_forward(ptr, STUN_MESSAGE_BINDING_RESPONSE_SUCCESS);
    ptr = uint16_write_forward(ptr, 4);
    ptr += 16;
    ptr = uint16_write_forward(ptr, STUN_ATTRIBUTE_XOR_MAPPED_ADDRESS_ALT);
    ptr = uint16_write_forward(ptr, 20); // no room in packet for 20B attribute
    struct nabto_stun_message msg;
    bool res = nabto_stun_decode_message(&msg, buf, 24);
    BOOST_TEST(!res);
}

BOOST_AUTO_TEST_CASE(decode_invalid_att_header_2)
{
    uint8_t buf[128];
    uint8_t* ptr = buf;
    ptr = uint16_write_forward(ptr, STUN_MESSAGE_BINDING_RESPONSE_SUCCESS);
    ptr = uint16_write_forward(ptr, 14);
    ptr += 16;
    ptr = uint16_write_forward(ptr, STUN_ATTRIBUTE_XOR_MAPPED_ADDRESS_ALT);
    ptr = uint16_write_forward(ptr, 6); // not long enough for addr
    struct nabto_stun_message msg;
    bool res = nabto_stun_decode_message(&msg, buf, 34);
    BOOST_TEST(!res);
}

BOOST_AUTO_TEST_CASE(decode_xor_addr)
{
    uint8_t buf[128];
    uint8_t* ptr = buf;
    ptr = uint16_write_forward(ptr, STUN_MESSAGE_BINDING_RESPONSE_SUCCESS);
    ptr = uint16_write_forward(ptr, 12);
    ptr += 16;
    ptr = uint16_write_forward(ptr, STUN_ATTRIBUTE_XOR_MAPPED_ADDRESS_ALT);
    ptr = uint16_write_forward(ptr, 8);
    ptr = uint16_write_forward(ptr, STUN_ADDRESS_FAMILY_V4);
    ptr = uint16_write_forward(ptr, 4242);

    struct nabto_stun_message msg;
    bool res = nabto_stun_decode_message(&msg, buf, 32);
    BOOST_TEST(res);
    BOOST_TEST(msg.mappedEp.ip.type == NN_IPV4);
    BOOST_TEST(msg.mappedEp.port == (4242^(STUN_MAGIC_COOKIE >> 16)));
}

BOOST_AUTO_TEST_CASE(decode_response_origin)
{
    uint8_t buf[128];
    uint8_t* ptr = buf;
    ptr = uint16_write_forward(ptr, STUN_MESSAGE_BINDING_RESPONSE_SUCCESS);
    ptr = uint16_write_forward(ptr, 12);
    ptr += 16;
    ptr = uint16_write_forward(ptr, STUN_ATTRIBUTE_RESPONSE_ORIGIN);
    ptr = uint16_write_forward(ptr, 8);
    ptr = uint16_write_forward(ptr, STUN_ADDRESS_FAMILY_V4);
    ptr = uint16_write_forward(ptr, 4242);

    struct nabto_stun_message msg;
    bool res = nabto_stun_decode_message(&msg, buf, 32);
    BOOST_TEST(res);
    BOOST_TEST(msg.serverEp.ip.type == NN_IPV4);
    BOOST_TEST(msg.serverEp.port == 4242);
}

BOOST_AUTO_TEST_CASE(decode_other_addr)
{
    uint8_t buf[128];
    uint8_t* ptr = buf;
    ptr = uint16_write_forward(ptr, STUN_MESSAGE_BINDING_RESPONSE_SUCCESS);
    ptr = uint16_write_forward(ptr, 12);
    ptr += 16;
    ptr = uint16_write_forward(ptr, STUN_ATTRIBUTE_OTHER_ADDRESS);
    ptr = uint16_write_forward(ptr, 8);
    ptr = uint16_write_forward(ptr, STUN_ADDRESS_FAMILY_V4);
    ptr = uint16_write_forward(ptr, 4242);

    struct nabto_stun_message msg;
    bool res = nabto_stun_decode_message(&msg, buf, 32);
    BOOST_TEST(res);
    BOOST_TEST(msg.altServerEp.ip.type == NN_IPV4);
    BOOST_TEST(msg.altServerEp.port == 4242);
}

BOOST_AUTO_TEST_CASE(decode_xor_addr_ipv6)
{
    uint8_t buf[128];
    uint8_t* ptr = buf;
    ptr = uint16_write_forward(ptr, STUN_MESSAGE_BINDING_RESPONSE_SUCCESS);
    ptr = uint16_write_forward(ptr, 24);
    ptr += 16;
    ptr = uint16_write_forward(ptr, STUN_ATTRIBUTE_XOR_MAPPED_ADDRESS_ALT);
    ptr = uint16_write_forward(ptr, 20);
    ptr = uint16_write_forward(ptr, STUN_ADDRESS_FAMILY_V6);
    ptr = uint16_write_forward(ptr, 4242);

    struct nabto_stun_message msg;
    msg.mappedEp.port = 0;
    bool res = nabto_stun_decode_message(&msg, buf, 45);
    BOOST_TEST(res);
    BOOST_TEST(msg.mappedEp.port == 0);  // IPv6 should leave ep untouched
}

BOOST_AUTO_TEST_CASE(decode_response_origin_ipv6)
{
    uint8_t buf[128];
    uint8_t* ptr = buf;
    ptr = uint16_write_forward(ptr, STUN_MESSAGE_BINDING_RESPONSE_SUCCESS);
    ptr = uint16_write_forward(ptr, 24);
    ptr += 16;
    ptr = uint16_write_forward(ptr, STUN_ATTRIBUTE_RESPONSE_ORIGIN);
    ptr = uint16_write_forward(ptr, 20);
    ptr = uint16_write_forward(ptr, STUN_ADDRESS_FAMILY_V6);
    ptr = uint16_write_forward(ptr, 4242);

    struct nabto_stun_message msg;
    msg.serverEp.port = 0;
    bool res = nabto_stun_decode_message(&msg, buf, 45);
    BOOST_TEST(res);
    BOOST_TEST(msg.serverEp.port == 0);  // IPv6 should leave ep untouched
}

BOOST_AUTO_TEST_CASE(decode_other_addr_ipv6)
{
    uint8_t buf[128];
    uint8_t* ptr = buf;
    ptr = uint16_write_forward(ptr, STUN_MESSAGE_BINDING_RESPONSE_SUCCESS);
    ptr = uint16_write_forward(ptr, 24);
    ptr += 16;
    ptr = uint16_write_forward(ptr, STUN_ATTRIBUTE_OTHER_ADDRESS);
    ptr = uint16_write_forward(ptr, 20);
    ptr = uint16_write_forward(ptr, STUN_ADDRESS_FAMILY_V6);
    ptr = uint16_write_forward(ptr, 4242);

    struct nabto_stun_message msg;
    msg.altServerEp.port = 0;
    bool res = nabto_stun_decode_message(&msg, buf, 45);
    BOOST_TEST(res);
    BOOST_TEST(msg.altServerEp.port == 0);  // IPv6 should leave ep untouched
}

BOOST_AUTO_TEST_CASE(decode_full_packet)
{
    uint8_t buf[128];
    uint8_t* ptr = buf;
    ptr = uint16_write_forward(ptr, STUN_MESSAGE_BINDING_RESPONSE_SUCCESS);
    ptr = uint16_write_forward(ptr, 36);
    ptr += 16;
    // XOR MAPPED ADDR
    ptr = uint16_write_forward(ptr, STUN_ATTRIBUTE_XOR_MAPPED_ADDRESS_ALT);
    ptr = uint16_write_forward(ptr, 8);
    ptr = uint16_write_forward(ptr, STUN_ADDRESS_FAMILY_V4);
    ptr = uint16_write_forward(ptr, 4242);
    ptr += 4; // IP addr
    // RESPONSE ORIGIN
    ptr = uint16_write_forward(ptr, STUN_ATTRIBUTE_RESPONSE_ORIGIN);
    ptr = uint16_write_forward(ptr, 8);
    ptr = uint16_write_forward(ptr, STUN_ADDRESS_FAMILY_V4);
    ptr = uint16_write_forward(ptr, 4242);
    ptr += 4; // IP addr
    // OTHER ADDR
    ptr = uint16_write_forward(ptr, STUN_ATTRIBUTE_OTHER_ADDRESS);
    ptr = uint16_write_forward(ptr, 8);
    ptr = uint16_write_forward(ptr, STUN_ADDRESS_FAMILY_V4);
    ptr = uint16_write_forward(ptr, 4242);

    struct nabto_stun_message msg;
    bool res = nabto_stun_decode_message(&msg, buf, 56);
    BOOST_TEST(res);
    BOOST_TEST(msg.mappedEp.ip.type == NN_IPV4);
    BOOST_TEST(msg.mappedEp.port == (4242^(STUN_MAGIC_COOKIE >> 16)));
    BOOST_TEST(msg.serverEp.ip.type == NN_IPV4);
    BOOST_TEST(msg.serverEp.port == 4242);
    BOOST_TEST(msg.altServerEp.ip.type == NN_IPV4);
    BOOST_TEST(msg.altServerEp.port == 4242);
}


BOOST_AUTO_TEST_SUITE_END()
