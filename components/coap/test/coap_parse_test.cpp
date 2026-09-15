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

bool parses(const std::vector<uint8_t>& packet)
{
    struct nabto_coap_incoming_message msg;
    return nabto_coap_parse_message(packet.data(), packet.size(), &msg);
}

// Expected wire form of an option delta or length field, RFC 7252 section 3.1.
struct OptionField {
    uint8_t nibble;
    std::vector<uint8_t> extension;
};

OptionField optionField(uint32_t value)
{
    if (value < 13) {
        return { (uint8_t)value, {} };
    } else if (value < 269) {
        return { 13, { (uint8_t)(value - 13) } };
    } else {
        value -= 269;
        return { 14, { (uint8_t)(value >> 8), (uint8_t)value } };
    }
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

/**
 * Option numbers are 16 bit (RFC 7252 section 12.2). A message whose
 * option numbers run past that used to be accepted with the options from
 * the overflow onwards invisible; it is rejected instead.
 */
BOOST_AUTO_TEST_CASE(option_number_overflow_is_rejected)
{
    // a single option with delta 0xFFFF + 269
    BOOST_TEST(!parses(packetWithOptions({ 0xE0, 0xFF, 0xFF })));
    // option 65535 followed by delta 1
    BOOST_TEST(!parses(packetWithOptions({ 0xE0, 0xFE, 0xF2, 0x10 })));
    // option 65535 on its own is fine
    BOOST_TEST(parses(packetWithOptions({ 0xE0, 0xFE, 0xF2 })));
}

BOOST_AUTO_TEST_CASE(reserved_option_nibbles_are_rejected)
{
    // delta 15 which is not the payload marker
    BOOST_TEST(!parses(packetWithOptions({ 0xF3 })));
    // length 15
    BOOST_TEST(!parses(packetWithOptions({ 0x0F })));
    // a valid option followed by a reserved byte
    BOOST_TEST(!parses(packetWithOptions({ 0x10, 0xF3 })));
}

BOOST_AUTO_TEST_CASE(payload_marker_needs_a_payload)
{
    BOOST_TEST(!parses(packetWithOptions({ 0xFF })));
    BOOST_TEST(!parses(packetWithOptions({ 0x10, 0xFF })));

    std::vector<uint8_t> packet = packetWithOptions({ 0xFF, 0x01 });
    struct nabto_coap_incoming_message msg;
    BOOST_REQUIRE(nabto_coap_parse_message(packet.data(), packet.size(), &msg));
    BOOST_TEST(msg.options != nullptr);
    BOOST_TEST(msg.optionsLength == 0u);
    BOOST_TEST(msg.payloadLength == 1u);
    BOOST_TEST(msg.payload[0] == 0x01);
}

BOOST_AUTO_TEST_CASE(message_without_options_has_no_option_view)
{
    struct nabto_coap_incoming_message msg;
    BOOST_REQUIRE(nabto_coap_parse_message(header.data(), header.size(), &msg));
    BOOST_TEST(msg.options == nullptr);
    BOOST_TEST(msg.optionsLength == 0u);
    BOOST_TEST(msg.payload == nullptr);
    BOOST_TEST(msg.payloadLength == 0u);

    // two byte token, nothing after it
    std::vector<uint8_t> withToken = { 0x42, 0x01, 0x00, 0x01, 0xAA, 0xBB };
    BOOST_REQUIRE(nabto_coap_parse_message(withToken.data(), withToken.size(), &msg));
    BOOST_TEST(msg.token.tokenLength == 2u);
    BOOST_TEST(msg.options == nullptr);
    BOOST_TEST(msg.optionsLength == 0u);
    BOOST_TEST(msg.payload == nullptr);
}

/**
 * nabto_coap_parse_message relies on the iterator leaving buffer at the
 * option it could not decode, so that it can tell the payload marker
 * from a malformed option.
 *
 * Pointers into the buffers are compared as offsets throughout this
 * file: on failure Boost.Test prints a uint8_t* as a C string.
 */
BOOST_AUTO_TEST_CASE(iterator_stops_at_malformed_option)
{
    std::vector<uint8_t> options = { 0x10, 0xD0 };
    struct nabto_coap_option_iterator it;
    nabto_coap_option_iterator_init(&it, options.data(), options.data() + options.size());
    BOOST_REQUIRE(nabto_coap_get_next_option(&it) != nullptr);
    BOOST_TEST(it.option == 1u);
    BOOST_TEST(it.buffer - options.data() == 1);
    BOOST_TEST(nabto_coap_get_next_option(&it) == nullptr);
    BOOST_TEST(it.buffer - options.data() == 1);
    BOOST_TEST(it.option == 1u);

    std::vector<uint8_t> truncated = { 0xD0 };
    nabto_coap_option_iterator_init(&it, truncated.data(), truncated.data() + truncated.size());
    BOOST_TEST(nabto_coap_get_next_option(&it) == nullptr);
    BOOST_TEST(it.buffer - truncated.data() == 0);
}

/**
 * Encode and decode an option at each boundary of the delta and length
 * extension rules, checking the exact bytes on the wire. The option
 * follows a leading option 1 so that its number (1 + delta) never hits
 * one of the options nabto_coap_parse_message decodes as an integer.
 */
BOOST_AUTO_TEST_CASE(option_field_boundaries_round_trip)
{
    const uint32_t boundaries[] = { 0, 12, 13, 268, 269 };
    for (uint32_t delta : boundaries) {
        for (uint32_t length : boundaries) {
            std::vector<uint8_t> value(length, 0x5A);
            std::vector<uint8_t> buffer(header.size() + 1 + 5 + length);
            memcpy(buffer.data(), header.data(), header.size());
            uint8_t* end = buffer.data() + buffer.size();
            uint8_t* begin = nabto_coap_encode_option(1, NULL, 0, buffer.data() + header.size(), end);
            BOOST_REQUIRE(begin != nullptr);
            uint8_t* ptr = nabto_coap_encode_option((uint16_t)delta, value.data(), value.size(), begin, end);
            BOOST_TEST_CONTEXT("delta " << delta << " length " << length) {
                BOOST_REQUIRE(ptr != nullptr);

                OptionField d = optionField(delta);
                OptionField l = optionField(length);
                std::vector<uint8_t> expected = { (uint8_t)((d.nibble << 4) | l.nibble) };
                expected.insert(expected.end(), d.extension.begin(), d.extension.end());
                expected.insert(expected.end(), l.extension.begin(), l.extension.end());
                expected.insert(expected.end(), value.begin(), value.end());
                std::vector<uint8_t> actual(begin, ptr);
                BOOST_TEST(actual == expected, boost::test_tools::per_element());

                struct nabto_coap_incoming_message msg;
                BOOST_REQUIRE(nabto_coap_parse_message(buffer.data(), ptr - buffer.data(), &msg));
                BOOST_TEST(msg.optionsLength == 1 + expected.size());
                struct nabto_coap_option_iterator it;
                nabto_coap_option_iterator_init(&it, msg.options, msg.options + msg.optionsLength);
                BOOST_REQUIRE(nabto_coap_get_next_option(&it) != nullptr);
                BOOST_TEST(it.option == 1u);
                BOOST_REQUIRE(nabto_coap_get_next_option(&it) != nullptr);
                BOOST_TEST(it.option == 1 + delta);
                BOOST_TEST(it.optionDataEnd - it.optionDataBegin == (ptrdiff_t)length);
                BOOST_TEST(nabto_coap_get_next_option(&it) == nullptr);
            }
        }
    }
}

/**
 * The encoder only needs room for the bytes it actually writes, and
 * writes nothing past bufferEnd even when the extension bytes are what
 * does not fit.
 */
BOOST_AUTO_TEST_CASE(encode_option_stays_inside_buffer)
{
    const uint8_t guard = 0xEE;

    // delta 269 and length 269 take 1 + 2 + 2 + 269 bytes
    std::vector<uint8_t> value(269, 0x5A);
    const size_t needed = 5 + value.size();

    std::vector<uint8_t> exact(needed + 1, guard);
    uint8_t* ptr = nabto_coap_encode_option(269, value.data(), value.size(), exact.data(), exact.data() + needed);
    BOOST_REQUIRE(ptr != nullptr);
    BOOST_TEST(ptr - exact.data() == (ptrdiff_t)needed);
    BOOST_TEST(exact[needed] == guard);

    // one byte short at each point where the encoder writes: the value,
    // the length extension bytes, the delta extension bytes, the first byte
    for (size_t room : { needed - 1, size_t(4), size_t(3), size_t(2), size_t(1), size_t(0) }) {
        std::vector<uint8_t> tight(room + 1, guard);
        BOOST_TEST_CONTEXT("room " << room) {
            BOOST_TEST(nabto_coap_encode_option(269, value.data(), value.size(), tight.data(), tight.data() + room) == nullptr);
            BOOST_TEST(tight[room] == guard);
        }
    }

    // delta 1, length 3 takes 4 bytes; no worst case margin is required
    std::vector<uint8_t> small(4 + 1, guard);
    ptr = nabto_coap_encode_option(1, value.data(), 3, small.data(), small.data() + 4);
    BOOST_REQUIRE(ptr != nullptr);
    BOOST_TEST(ptr - small.data() == 4);
    BOOST_TEST(small[0] == 0x13);
    BOOST_TEST(small[4] == guard);
}

BOOST_AUTO_TEST_SUITE_END()
