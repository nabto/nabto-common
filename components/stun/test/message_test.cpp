#include <boost/test/unit_test.hpp>
#include <nabto_stun/nabto_stun_message.h>

#include <cstring>
#include <vector>

#ifdef __cplusplus
extern "C" {
#endif

const uint8_t* nabto_stun_read_uint8(const uint8_t* ptr, const uint8_t* end, uint8_t* val);
const uint8_t* nabto_stun_read_uint16(const uint8_t* ptr, const uint8_t* end, uint16_t* val);
const uint8_t* nabto_stun_read_uint32(const uint8_t* ptr, const uint8_t* end, uint32_t* val);
const uint8_t* nabto_stun_read_buf(const uint8_t* ptr, const uint8_t* end, uint8_t* val, uint16_t size);
uint8_t* nabto_stun_uint8_write_forward(uint8_t* buf, uint8_t* end, uint8_t val);
uint8_t* nabto_stun_uint16_write_forward(uint8_t* buf, uint8_t* end, uint16_t val);
uint8_t* nabto_stun_uint32_write_forward(uint8_t* buf, uint8_t* end, uint32_t val);
uint8_t* nabto_stun_buf_write_forward(uint8_t* buf, uint8_t* end, const uint8_t* val, uint16_t size);

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
    ptr = uint32_write_forward(ptr, STUN_MAGIC_COOKIE);
    ptr += 12; // transaction id
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
    ptr = uint32_write_forward(ptr, STUN_MAGIC_COOKIE);
    ptr += 12; // transaction id
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
    ptr = uint32_write_forward(ptr, STUN_MAGIC_COOKIE);
    ptr += 12; // transaction id
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
    ptr = uint32_write_forward(ptr, STUN_MAGIC_COOKIE);
    ptr += 12; // transaction id
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
    ptr = uint32_write_forward(ptr, STUN_MAGIC_COOKIE);
    ptr += 12; // transaction id
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
    ptr = uint32_write_forward(ptr, STUN_MAGIC_COOKIE);
    ptr += 12; // transaction id
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
    ptr = uint32_write_forward(ptr, STUN_MAGIC_COOKIE);
    ptr += 12; // transaction id
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
    ptr = uint32_write_forward(ptr, STUN_MAGIC_COOKIE);
    ptr += 12; // transaction id
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
    ptr = uint32_write_forward(ptr, STUN_MAGIC_COOKIE);
    ptr += 12; // transaction id
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

BOOST_AUTO_TEST_CASE(decode_short_address_attribute_at_end_of_packet)
{
    // An address attribute needs a reserved byte, a family byte and a port
    // before it can be rejected on its length. Put a shorter one last in the
    // packet, in a heap buffer that ends where the attribute does, so any
    // read past the end shows up under the sanitizer.
    for (uint16_t attLen = 0; attLen < 4; attLen++) {
        std::vector<uint8_t> buf(24 + attLen, 0);
        uint8_t* ptr = buf.data();
        ptr = uint16_write_forward(ptr, STUN_MESSAGE_BINDING_RESPONSE_SUCCESS);
        ptr = uint16_write_forward(ptr, 4 + attLen);
        ptr = uint32_write_forward(ptr, STUN_MAGIC_COOKIE);
        ptr += 12; // transaction id
        ptr = uint16_write_forward(ptr, STUN_ATTRIBUTE_XOR_MAPPED_ADDRESS_ALT);
        ptr = uint16_write_forward(ptr, attLen);

        struct nabto_stun_message msg;
        bool res = nabto_stun_decode_message(&msg, buf.data(), (uint16_t)buf.size());
        BOOST_TEST(!res, "attLen " << attLen);
    }
}

BOOST_AUTO_TEST_CASE(decode_unknown_attribute_ending_inside_padding)
{
    // Attribute values are padded to 4 bytes. A packet that stops inside the
    // padding of its last attribute is still accepted and nothing after the
    // buffer is touched.
    for (uint16_t attLen = 0; attLen < 8; attLen++) {
        std::vector<uint8_t> buf(24 + attLen, 0);
        uint8_t* ptr = buf.data();
        ptr = uint16_write_forward(ptr, STUN_MESSAGE_BINDING_RESPONSE_SUCCESS);
        ptr = uint16_write_forward(ptr, 4 + attLen);
        ptr = uint32_write_forward(ptr, STUN_MAGIC_COOKIE);
        ptr += 12; // transaction id
        ptr = uint16_write_forward(ptr, 0x0022); // SOFTWARE, ignored by the decoder
        ptr = uint16_write_forward(ptr, attLen);

        struct nabto_stun_message msg;
        bool res = nabto_stun_decode_message(&msg, buf.data(), (uint16_t)buf.size());
        BOOST_TEST(res, "attLen " << attLen);
    }
}

BOOST_AUTO_TEST_CASE(decode_rejects_wrong_magic_cookie)
{
    // The same response with and without the rfc 5389 magic cookie. Only the
    // one carrying the cookie is a stun response.
    for (int good = 0; good < 2; good++) {
        std::vector<uint8_t> buf(32, 0);
        uint8_t* ptr = buf.data();
        ptr = uint16_write_forward(ptr, STUN_MESSAGE_BINDING_RESPONSE_SUCCESS);
        ptr = uint16_write_forward(ptr, 12);
        ptr = uint32_write_forward(ptr, good ? STUN_MAGIC_COOKIE : STUN_MAGIC_COOKIE + 1);
        ptr += 12; // transaction id
        ptr = uint16_write_forward(ptr, STUN_ATTRIBUTE_XOR_MAPPED_ADDRESS_ALT);
        ptr = uint16_write_forward(ptr, 8);
        ptr = uint16_write_forward(ptr, STUN_ADDRESS_FAMILY_V4);
        ptr = uint16_write_forward(ptr, 4242);
        ptr = uint32_write_forward(ptr, 0x7f000001);

        struct nabto_stun_message msg;
        memset(&msg, 0, sizeof(msg));
        bool res = nabto_stun_decode_message(&msg, buf.data(), (uint16_t)buf.size());
        BOOST_TEST(res == (good == 1), "good " << good);
        BOOST_TEST((msg.mappedEp.port != 0) == (good == 1), "good " << good);
    }
}

BOOST_AUTO_TEST_CASE(write_message_rejects_too_small_buffer)
{
    struct nabto_stun_message msg;
    memset(&msg, 0, sizeof(msg));
    for (uint16_t size = 0; size < STUN_BINDING_REQUEST_SIZE; size++) {
        std::vector<uint8_t> buf(size);
        BOOST_TEST(nabto_stun_write_message(buf.data(), size, &msg) == 0, "size " << size);
    }
    std::vector<uint8_t> buf(STUN_BINDING_REQUEST_SIZE);
    BOOST_TEST(nabto_stun_write_message(buf.data(), (uint16_t)buf.size(), &msg) == STUN_BINDING_REQUEST_SIZE);
    uint16_t length;
    BOOST_TEST((nabto_stun_read_uint16(buf.data() + 2, buf.data() + buf.size(), &length) != NULL));
    BOOST_TEST(length == STUN_BINDING_REQUEST_SIZE - 20);
}

BOOST_AUTO_TEST_CASE(write_forward_helpers_stop_at_end)
{
    // Every writer checks the space itself: exactly enough succeeds and
    // returns end, one byte short returns NULL, NULL in gives NULL out.
    // Heap buffers sized exactly so an overrun is visible to the sanitizer.
    const uint8_t data[6] = {1, 2, 3, 4, 5, 6};
    {
        std::vector<uint8_t> buf(1);
        BOOST_TEST(nabto_stun_uint8_write_forward(buf.data(), buf.data() + 1, 0x42) == buf.data() + 1);
        BOOST_TEST(buf[0] == 0x42);
        BOOST_TEST(nabto_stun_uint8_write_forward(buf.data(), buf.data(), 0x42) == (uint8_t*)NULL);
        BOOST_TEST(nabto_stun_uint8_write_forward(NULL, buf.data() + 1, 0x42) == (uint8_t*)NULL);
    }
    {
        std::vector<uint8_t> buf(2);
        BOOST_TEST(nabto_stun_uint16_write_forward(buf.data(), buf.data() + 2, 0x1234) == buf.data() + 2);
        BOOST_TEST(buf[0] == 0x12);
        BOOST_TEST(buf[1] == 0x34);
        BOOST_TEST(nabto_stun_uint16_write_forward(buf.data(), buf.data() + 1, 0x1234) == (uint8_t*)NULL);
        BOOST_TEST(nabto_stun_uint16_write_forward(NULL, buf.data() + 2, 0x1234) == (uint8_t*)NULL);
    }
    {
        std::vector<uint8_t> buf(4);
        BOOST_TEST(nabto_stun_uint32_write_forward(buf.data(), buf.data() + 4, 0x12345678) == buf.data() + 4);
        BOOST_TEST(buf[0] == 0x12);
        BOOST_TEST(buf[3] == 0x78);
        BOOST_TEST(nabto_stun_uint32_write_forward(buf.data(), buf.data() + 3, 0x12345678) == (uint8_t*)NULL);
        BOOST_TEST(nabto_stun_uint32_write_forward(NULL, buf.data() + 4, 0x12345678) == (uint8_t*)NULL);
    }
    {
        std::vector<uint8_t> buf(6);
        BOOST_TEST(nabto_stun_buf_write_forward(buf.data(), buf.data() + 6, data, 6) == buf.data() + 6);
        BOOST_TEST(memcmp(buf.data(), data, 6) == 0);
        BOOST_TEST(nabto_stun_buf_write_forward(buf.data(), buf.data() + 5, data, 6) == (uint8_t*)NULL);
        BOOST_TEST(nabto_stun_buf_write_forward(NULL, buf.data() + 6, data, 6) == (uint8_t*)NULL);
        // a zero length write never touches the buffer, so it succeeds even at end
        BOOST_TEST(nabto_stun_buf_write_forward(buf.data() + 6, buf.data() + 6, data, 0) == buf.data() + 6);
    }
}

BOOST_AUTO_TEST_CASE(read_helpers_stop_at_end)
{
    const std::vector<uint8_t> buf = {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc};
    const uint8_t* start = buf.data();
    const uint8_t* end = start + buf.size();
    uint8_t u8;
    uint16_t u16;
    uint32_t u32;
    uint8_t out[6];

    BOOST_TEST(nabto_stun_read_uint8(end - 1, end, &u8) == end);
    BOOST_TEST(u8 == 0xbc);
    BOOST_TEST(nabto_stun_read_uint8(end, end, &u8) == (const uint8_t*)NULL);
    BOOST_TEST(nabto_stun_read_uint8(NULL, end, &u8) == (const uint8_t*)NULL);

    BOOST_TEST(nabto_stun_read_uint16(end - 2, end, &u16) == end);
    BOOST_TEST(u16 == 0x9abc);
    BOOST_TEST(nabto_stun_read_uint16(end - 1, end, &u16) == (const uint8_t*)NULL);
    BOOST_TEST(nabto_stun_read_uint16(NULL, end, &u16) == (const uint8_t*)NULL);

    BOOST_TEST(nabto_stun_read_uint32(end - 4, end, &u32) == end);
    BOOST_TEST(u32 == 0x56789abc);
    BOOST_TEST(nabto_stun_read_uint32(end - 3, end, &u32) == (const uint8_t*)NULL);
    BOOST_TEST(nabto_stun_read_uint32(NULL, end, &u32) == (const uint8_t*)NULL);

    BOOST_TEST(nabto_stun_read_buf(start, end, out, 6) == end);
    BOOST_TEST(memcmp(out, start, 6) == 0);
    BOOST_TEST(nabto_stun_read_buf(start + 1, end, out, 6) == (const uint8_t*)NULL);
    BOOST_TEST(nabto_stun_read_buf(NULL, end, out, 6) == (const uint8_t*)NULL);
    BOOST_TEST(nabto_stun_read_buf(end, end, out, 0) == end);
}

BOOST_AUTO_TEST_SUITE_END()
