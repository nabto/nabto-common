#include <boost/test/unit_test.hpp>
#include <nabto_coap/nabto_coap.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

// CON GET, message id 1, empty token.
const std::vector<uint8_t> header = { 0x40, 0x01, 0x00, 0x01 };

std::vector<uint8_t> packetWithOptions(const std::vector<uint8_t>& options)
{
    std::vector<uint8_t> packet = header;
    packet.insert(packet.end(), options.begin(), options.end());
    return packet;
}

} // namespace

BOOST_AUTO_TEST_SUITE(coap_parse)

/**
 * Audit L6: the option pre-scan stepped over the delta extension bytes
 * before checking they were there. A packet ending in the first option
 * byte must be rejected, and ptr must never be formed past end.
 */
BOOST_AUTO_TEST_CASE(truncated_option_delta_extension_is_rejected)
{
    struct nabto_coap_incoming_message msg;

    // delta 13 announces one extension byte, none present.
    std::vector<uint8_t> oneByte = packetWithOptions({ 0xD0 });
    BOOST_TEST(!nabto_coap_parse_message(oneByte.data(), oneByte.size(), &msg));

    // delta 14 announces two extension bytes, none present.
    std::vector<uint8_t> twoBytes = packetWithOptions({ 0xE0 });
    BOOST_TEST(!nabto_coap_parse_message(twoBytes.data(), twoBytes.size(), &msg));

    // delta 14, only one of the two extension bytes present.
    std::vector<uint8_t> twoBytesOneMissing = packetWithOptions({ 0xE0, 0x00 });
    BOOST_TEST(!nabto_coap_parse_message(twoBytesOneMissing.data(), twoBytesOneMissing.size(), &msg));

    // delta 13 with its extension byte, then length 13 without its extension byte.
    std::vector<uint8_t> lengthMissing = packetWithOptions({ 0xDD, 0x00 });
    BOOST_TEST(!nabto_coap_parse_message(lengthMissing.data(), lengthMissing.size(), &msg));
}

BOOST_AUTO_TEST_CASE(option_delta_extensions_are_parsed)
{
    struct nabto_coap_incoming_message msg;

    // delta 13 + 0 = option 13 with no value, then delta 269 + 0 =
    // option 282 with the two byte value 0x01 0x02. Both extension forms
    // are exercised on a packet that ends exactly at the last option byte.
    std::vector<uint8_t> packet = packetWithOptions({ 0xD0, 0x00, 0xE2, 0x00, 0x00, 0x01, 0x02 });
    BOOST_REQUIRE(nabto_coap_parse_message(packet.data(), packet.size(), &msg));
    BOOST_TEST(msg.optionsLength == 7u);
    BOOST_TEST(msg.payloadLength == 0u);

    struct nabto_coap_option_iterator it;
    nabto_coap_option_iterator_init(&it, msg.options, msg.options + msg.optionsLength);
    BOOST_REQUIRE(nabto_coap_get_next_option(&it) != nullptr);
    BOOST_TEST(it.option == 13u);
    BOOST_TEST(it.optionDataEnd - it.optionDataBegin == 0);
    BOOST_REQUIRE(nabto_coap_get_next_option(&it) != nullptr);
    BOOST_TEST(it.option == 13u + 269u);
    BOOST_TEST(it.optionDataEnd - it.optionDataBegin == 2);
    BOOST_TEST(it.optionDataBegin[0] == 0x01);
    BOOST_TEST(it.optionDataBegin[1] == 0x02);
    BOOST_TEST(nabto_coap_get_next_option(&it) == nullptr);
}

/**
 * Audit L6: nabto_coap_encode_option truncated the length extension for
 * options longer than 65804 bytes and produced a mis-framed message.
 */
BOOST_AUTO_TEST_CASE(encode_option_rejects_option_longer_than_max)
{
    std::vector<uint8_t> value(NABTO_COAP_MAX_OPTION_LENGTH + 1, 0xAB);
    std::vector<uint8_t> buffer(value.size() + 16);

    BOOST_TEST(nabto_coap_encode_option(11, value.data(), value.size(), buffer.data(), buffer.data() + buffer.size()) == nullptr);
}

BOOST_AUTO_TEST_CASE(encode_option_max_length_round_trips)
{
    std::vector<uint8_t> value(NABTO_COAP_MAX_OPTION_LENGTH, 0xAB);
    std::vector<uint8_t> buffer(header.size() + value.size() + 16);

    memcpy(buffer.data(), header.data(), header.size());
    uint8_t* ptr = buffer.data() + header.size();
    uint8_t* end = buffer.data() + buffer.size();
    ptr = nabto_coap_encode_option(11, value.data(), value.size(), ptr, end);
    BOOST_REQUIRE(ptr != nullptr);
    // 1 header byte, 2 length extension bytes 0xFFFF, then the value.
    BOOST_TEST(ptr - buffer.data() == (ptrdiff_t)(header.size() + 3 + value.size()));
    BOOST_TEST(buffer[header.size()] == 0xBE);
    BOOST_TEST(buffer[header.size() + 1] == 0xFF);
    BOOST_TEST(buffer[header.size() + 2] == 0xFF);

    struct nabto_coap_incoming_message msg;
    BOOST_REQUIRE(nabto_coap_parse_message(buffer.data(), ptr - buffer.data(), &msg));
    BOOST_TEST(msg.payloadLength == 0u);

    struct nabto_coap_option_iterator it;
    nabto_coap_option_iterator_init(&it, msg.options, msg.options + msg.optionsLength);
    BOOST_REQUIRE(nabto_coap_get_option(NABTO_COAP_OPTION_URI_PATH, &it) != nullptr);
    BOOST_TEST(it.optionDataEnd - it.optionDataBegin == (ptrdiff_t)value.size());
    BOOST_TEST(memcmp(it.optionDataBegin, value.data(), value.size()) == 0);
}

BOOST_AUTO_TEST_SUITE_END()
