#include <boost/test/unit_test.hpp>
#include <nabto_stream/nabto_stream_packet.h>
#include <nabto_stream/nabto_stream_protocol.h>
#include <nabto_stream/nabto_stream_window.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

uint32_t getStamp(void* userData)
{
    uint32_t* stamp = (uint32_t*)userData;
    return (*stamp)++;
}

struct nabto_stream_send_segment* allocSendSegment(size_t bufferSize, void* userData)
{
    (void)userData;
    struct nabto_stream_send_segment* segment = (struct nabto_stream_send_segment*)calloc(1, sizeof(struct nabto_stream_send_segment));
    segment->buf = (uint8_t*)calloc(1, bufferSize);
    segment->capacity = (uint16_t)bufferSize;
    return segment;
}

void freeSendSegment(struct nabto_stream_send_segment* segment, void* userData)
{
    (void)userData;
    free(segment->buf);
    free(segment);
}

struct nabto_stream_recv_segment* allocRecvSegment(size_t bufferSize, void* userData)
{
    (void)userData;
    struct nabto_stream_recv_segment* segment = (struct nabto_stream_recv_segment*)calloc(1, sizeof(struct nabto_stream_recv_segment));
    segment->buf = (uint8_t*)calloc(1, bufferSize);
    segment->capacity = (uint16_t)bufferSize;
    return segment;
}

void freeRecvSegment(struct nabto_stream_recv_segment* segment, void* userData)
{
    (void)userData;
    free(segment->buf);
    free(segment);
}

void notifyEvent(enum nabto_stream_module_event event, void* userData)
{
    (void)event;
    (void)userData;
}

const uint8_t NONCE[NABTO_STREAM_NONCE_SIZE] = { 1, 2, 3, 4, 5, 6, 7, 8 };

/**
 * A stream backed by calloc/free segment allocators and no logger.
 */
struct StreamFixture {
    StreamFixture()
    {
        memset(&module, 0, sizeof(module));
        module.get_stamp = &getStamp;
        module.logger = NULL;
        module.alloc_send_segment = &allocSendSegment;
        module.free_send_segment = &freeSendSegment;
        module.alloc_recv_segment = &allocRecvSegment;
        module.free_recv_segment = &freeRecvSegment;
        module.notify_event = &notifyEvent;
        nabto_stream_init(&stream, &module, &stamp);
    }

    ~StreamFixture()
    {
        nabto_stream_destroy(&stream);
    }

    void responder()
    {
        uint8_t nonce[NABTO_STREAM_NONCE_SIZE];
        memcpy(nonce, NONCE, sizeof(nonce));
        nabto_stream_init_responder(&stream, nonce);
    }

    void initiator()
    {
        nabto_stream_init_initiator(&stream);
    }

    struct nabto_stream_module module;
    struct nabto_stream stream;
    uint32_t stamp = 0;
};

/**
 * Builds a packet: header followed by (type, length, payload) extensions.
 * build() returns a vector sized exactly so the sanitizer sees over-reads.
 */
class PacketBuilder {
 public:
    PacketBuilder(uint8_t flags, uint32_t timestamp)
    {
        uint8_t hdr[5];
        BOOST_REQUIRE(nabto_stream_write_header(hdr, hdr + sizeof(hdr), flags, timestamp) == hdr + sizeof(hdr));
        bytes_.assign(hdr, hdr + sizeof(hdr));
    }

    PacketBuilder& ext(uint16_t type, const std::vector<uint8_t>& payload)
    {
        u16(type);
        u16((uint16_t)payload.size());
        bytes_.insert(bytes_.end(), payload.begin(), payload.end());
        return *this;
    }

    PacketBuilder& ext(uint16_t type, uint32_t value)
    {
        return ext(type, be32(value));
    }

    /** Append bytes as is, for deliberately broken extension framing. */
    PacketBuilder& raw(const std::vector<uint8_t>& bytes)
    {
        bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
        return *this;
    }

    /** An extension header declaring `declared` payload bytes, followed by `actual` of them. */
    PacketBuilder& truncatedExt(uint16_t type, uint16_t declared, size_t actual)
    {
        u16(type);
        u16(declared);
        bytes_.insert(bytes_.end(), actual, 0);
        return *this;
    }

    std::vector<uint8_t> build() const { return bytes_; }

    static std::vector<uint8_t> be32(uint32_t v)
    {
        std::vector<uint8_t> out;
        out.push_back((uint8_t)(v >> 24));
        out.push_back((uint8_t)(v >> 16));
        out.push_back((uint8_t)(v >> 8));
        out.push_back((uint8_t)v);
        return out;
    }

    static std::vector<uint8_t> be16(uint16_t v)
    {
        std::vector<uint8_t> out;
        out.push_back((uint8_t)(v >> 8));
        out.push_back((uint8_t)v);
        return out;
    }

 private:
    void u16(uint16_t v)
    {
        std::vector<uint8_t> b = be16(v);
        bytes_.insert(bytes_.end(), b.begin(), b.end());
    }

    std::vector<uint8_t> bytes_;
};

std::vector<uint8_t> segmentSizes(uint16_t send, uint16_t recv)
{
    std::vector<uint8_t> out = PacketBuilder::be16(send);
    std::vector<uint8_t> r = PacketBuilder::be16(recv);
    out.insert(out.end(), r.begin(), r.end());
    return out;
}

std::vector<uint8_t> prefix(const std::vector<uint8_t>& v, size_t n)
{
    return std::vector<uint8_t>(v.begin(), v.begin() + n);
}

/** Ack extension payload: maxAcked, windowSize, tsEcr, delay, then gap blocks. */
std::vector<uint8_t> ackPayload(uint32_t maxAcked, uint32_t windowSize, size_t extraBytes)
{
    std::vector<uint8_t> out = PacketBuilder::be32(maxAcked);
    std::vector<uint8_t> w = PacketBuilder::be32(windowSize);
    out.insert(out.end(), w.begin(), w.end());
    out.resize(16, 0);
    out.resize(16 + extraBytes, 0);
    return out;
}

uint16_t readU16(const uint8_t* p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

void handle(struct nabto_stream* stream, const std::vector<uint8_t>& packet)
{
    nabto_stream_handle_packet(stream, packet.data(), packet.size());
}

} // namespace

BOOST_AUTO_TEST_SUITE(streaming)

// L7: parse_syn

BOOST_FIXTURE_TEST_CASE(syn_with_valid_syn_extension_is_accepted, StreamFixture)
{
    responder();
    std::vector<uint8_t> packet = PacketBuilder(NABTO_STREAM_FLAG_SYN, 42)
        .ext(NABTO_STREAM_EXTENSION_SYN, 0x11223344u)
        .ext(NABTO_STREAM_EXTENSION_CONTENT_TYPE, 7u)
        .build();
    handle(&stream, packet);

    BOOST_TEST(stream.state == ST_ACCEPT);
    BOOST_TEST(stream.recvMax == 0x11223344u);
    BOOST_TEST(stream.recvTop == 0x11223344u);
    BOOST_TEST(stream.recvMaxAllocated == 0x11223344u);
    BOOST_TEST(stream.contentType == 7u);
}

BOOST_AUTO_TEST_CASE(syn_with_truncated_syn_extension_is_rejected)
{
    // The syn extension is the last thing in the packet and holds 0..3 of
    // the 4 sequence number bytes. The old parser accepted the packet with
    // whatever was in req.seq.
    std::vector<uint8_t> seq = PacketBuilder::be32(0x11223344u);
    for (size_t len = 0; len < 4; len++) {
        StreamFixture f;
        f.responder();
        std::vector<uint8_t> packet = PacketBuilder(NABTO_STREAM_FLAG_SYN, 42)
            .ext(NABTO_STREAM_EXTENSION_CONTENT_TYPE, 7u)
            .ext(NABTO_STREAM_EXTENSION_SYN, prefix(seq, len))
            .build();
        handle(&f.stream, packet);

        BOOST_TEST(f.stream.state == ST_IDLE, "len " << len);
        BOOST_TEST(f.stream.recvMax == 0u, "len " << len);
        BOOST_TEST(f.stream.recvTop == 0u, "len " << len);
    }
}

BOOST_FIXTURE_TEST_CASE(syn_extension_read_does_not_cross_into_next_extension, StreamFixture)
{
    // A 2 byte syn extension followed by another extension. The old parser
    // bounded the read by the packet and took the sequence number from the
    // following extension header.
    responder();
    std::vector<uint8_t> packet = PacketBuilder(NABTO_STREAM_FLAG_SYN, 42)
        .ext(NABTO_STREAM_EXTENSION_SYN, prefix(PacketBuilder::be32(0x11223344u), 2))
        .ext(NABTO_STREAM_EXTENSION_CONTENT_TYPE, 7u)
        .build();
    handle(&stream, packet);

    BOOST_TEST(stream.state == ST_IDLE);
    BOOST_TEST(stream.recvMax == 0u);
}

BOOST_FIXTURE_TEST_CASE(syn_with_short_optional_extensions_uses_defaults, StreamFixture)
{
    // Short segment sizes / content type extensions are ignored, not read
    // out of the packet past their own length.
    responder();
    std::vector<uint8_t> packet = PacketBuilder(NABTO_STREAM_FLAG_SYN, 42)
        .ext(NABTO_STREAM_EXTENSION_SEGMENT_SIZES, prefix(segmentSizes(100, 100), 3))
        .ext(NABTO_STREAM_EXTENSION_CONTENT_TYPE, prefix(PacketBuilder::be32(7u), 3))
        .ext(NABTO_STREAM_EXTENSION_SYN, 0x11223344u)
        .build();
    handle(&stream, packet);

    BOOST_TEST(stream.state == ST_ACCEPT);
    BOOST_TEST(stream.contentType == 0u);
    BOOST_TEST(stream.maxSendSegmentSize == NABTO_STREAM_DEFAULT_MAX_SEND_SEGMENT_SIZE);
}

BOOST_AUTO_TEST_CASE(syn_with_truncated_extension_list_is_rejected)
{
    // A valid syn extension followed by broken framing: a header declaring
    // more payload than is left, or a partial header. The old loops treated
    // this as the end of the list and accepted the syn; with a truncated
    // NONCE_CAPABILITY that also turned replay protection off.
    struct Case { const char* name; uint16_t type; uint16_t declared; size_t actual; };
    const Case cases[] = {
        { "nonce capability declaring 1 byte, none present", NABTO_STREAM_EXTENSION_NONCE_CAPABILITY, 1, 0 },
        { "content type declaring 4 bytes, 3 present", NABTO_STREAM_EXTENSION_CONTENT_TYPE, 4, 3 },
        { "unknown type declaring 100 bytes, 1 present", 0x1fff, 100, 1 },
    };
    for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
        StreamFixture f;
        f.responder();
        std::vector<uint8_t> packet = PacketBuilder(NABTO_STREAM_FLAG_SYN, 42)
            .ext(NABTO_STREAM_EXTENSION_SYN, 0x11223344u)
            .truncatedExt(cases[i].type, cases[i].declared, cases[i].actual)
            .build();
        handle(&f.stream, packet);

        BOOST_TEST(f.stream.state == ST_IDLE, cases[i].name);
        BOOST_TEST(!f.stream.disableReplayProtection, cases[i].name);
        BOOST_TEST(f.stream.receivedPackets == 0u, cases[i].name);
    }

    // 1..3 stray bytes after the last extension: not enough for a header.
    for (size_t stray = 1; stray < 4; stray++) {
        StreamFixture f;
        f.responder();
        std::vector<uint8_t> packet = PacketBuilder(NABTO_STREAM_FLAG_SYN, 42)
            .ext(NABTO_STREAM_EXTENSION_SYN, 0x11223344u)
            .raw(std::vector<uint8_t>(stray, 0))
            .build();
        handle(&f.stream, packet);

        BOOST_TEST(f.stream.state == ST_IDLE, "stray " << stray);
        BOOST_TEST(f.stream.receivedPackets == 0u, "stray " << stray);
    }

    // control: a complete, empty nonce capability keeps replay protection on.
    StreamFixture f;
    f.responder();
    std::vector<uint8_t> packet = PacketBuilder(NABTO_STREAM_FLAG_SYN, 42)
        .ext(NABTO_STREAM_EXTENSION_SYN, 0x11223344u)
        .ext(NABTO_STREAM_EXTENSION_NONCE_CAPABILITY, std::vector<uint8_t>())
        .build();
    handle(&f.stream, packet);
    BOOST_TEST(f.stream.state == ST_ACCEPT);
    BOOST_TEST(!f.stream.disableReplayProtection);
    BOOST_TEST(f.stream.receivedPackets == 1u);
}

BOOST_FIXTURE_TEST_CASE(parse_syn_rejects_truncated_extension_list_itself, StreamFixture)
{
    // The parser checks the framing as it reads; it does not rely on the
    // sanity check in nabto_stream_handle_packet.
    responder();
    std::vector<uint8_t> packet = PacketBuilder(NABTO_STREAM_FLAG_SYN, 42)
        .ext(NABTO_STREAM_EXTENSION_SYN, 0x11223344u)
        .truncatedExt(NABTO_STREAM_EXTENSION_NONCE_CAPABILITY, 1, 0)
        .build();
    struct nabto_stream_header hdr;
    hdr.flags = NABTO_STREAM_FLAG_SYN;
    hdr.timestampValue = 42;
    nabto_stream_parse_syn(&stream, packet.data() + 5, packet.data() + packet.size(), &hdr);

    BOOST_TEST(stream.state == ST_IDLE);
    BOOST_TEST(!stream.disableReplayProtection);
}

// L7: parse_syn_ack

BOOST_AUTO_TEST_CASE(syn_ack_with_truncated_nonce_is_rejected)
{
    std::vector<uint8_t> nonce(NONCE, NONCE + NABTO_STREAM_NONCE_SIZE);
    for (size_t len = 0; len <= NABTO_STREAM_NONCE_SIZE; len++) {
        StreamFixture f;
        f.initiator();
        f.stream.state = ST_SYN_SENT;
        std::vector<uint8_t> packet = PacketBuilder(NABTO_STREAM_FLAG_SYN | NABTO_STREAM_FLAG_ACK, 42)
            .ext(NABTO_STREAM_EXTENSION_SEGMENT_SIZES, segmentSizes(200, 200))
            .ext(NABTO_STREAM_EXTENSION_SYN, 0x11223344u)
            .ext(NABTO_STREAM_EXTENSION_NONCE, prefix(nonce, len))
            .build();
        handle(&f.stream, packet);

        if (len < NABTO_STREAM_NONCE_SIZE) {
            // malformed nonce: the packet is dropped, replay protection stays on.
            BOOST_TEST(f.stream.state == ST_SYN_SENT, "len " << len);
            BOOST_TEST(!f.stream.disableReplayProtection, "len " << len);
            BOOST_TEST(!f.stream.sendNonce, "len " << len);
        } else {
            BOOST_TEST(f.stream.state == ST_ESTABLISHED);
            BOOST_TEST(f.stream.sendNonce);
            BOOST_TEST(memcmp(f.stream.nonce, NONCE, NABTO_STREAM_NONCE_SIZE) == 0);
            BOOST_TEST(f.stream.recvMax == 0x11223344u);
            BOOST_TEST(f.stream.maxSendSegmentSize == 200);
        }
    }
}

BOOST_AUTO_TEST_CASE(syn_ack_with_truncated_syn_extension_is_rejected)
{
    std::vector<uint8_t> seq = PacketBuilder::be32(0x11223344u);
    for (size_t len = 0; len < 4; len++) {
        StreamFixture f;
        f.initiator();
        f.stream.state = ST_SYN_SENT;
        std::vector<uint8_t> packet = PacketBuilder(NABTO_STREAM_FLAG_SYN | NABTO_STREAM_FLAG_ACK, 42)
            .ext(NABTO_STREAM_EXTENSION_SEGMENT_SIZES, segmentSizes(200, 200))
            .ext(NABTO_STREAM_EXTENSION_SYN, prefix(seq, len))
            .build();
        handle(&f.stream, packet);

        BOOST_TEST(f.stream.state == ST_SYN_SENT, "len " << len);
        BOOST_TEST(f.stream.recvMax == 0u, "len " << len);
    }
}

BOOST_AUTO_TEST_CASE(syn_ack_with_short_segment_sizes_is_rejected)
{
    // segment sizes are mandatory in a syn|ack; a short extension counts as
    // absent and nothing of it is copied into the request.
    std::vector<uint8_t> sizes = segmentSizes(200, 200);
    for (size_t len = 0; len < 4; len++) {
        StreamFixture f;
        f.initiator();
        f.stream.state = ST_SYN_SENT;
        std::vector<uint8_t> packet = PacketBuilder(NABTO_STREAM_FLAG_SYN | NABTO_STREAM_FLAG_ACK, 42)
            .ext(NABTO_STREAM_EXTENSION_SEGMENT_SIZES, prefix(sizes, len))
            .ext(NABTO_STREAM_EXTENSION_SYN, 0x11223344u)
            .build();
        handle(&f.stream, packet);

        BOOST_TEST(f.stream.state == ST_SYN_SENT, "len " << len);
        BOOST_TEST(f.stream.maxSendSegmentSize == NABTO_STREAM_DEFAULT_MAX_SEND_SEGMENT_SIZE, "len " << len);
    }
}

BOOST_AUTO_TEST_CASE(syn_ack_ack_extension_is_applied_only_when_packet_is_valid)
{
    // the ack extension of a syn|ack must not touch the stream when the
    // packet is rejected for another reason.
    std::vector<uint8_t> nonce(NONCE, NONCE + NABTO_STREAM_NONCE_SIZE);
    for (size_t len = NABTO_STREAM_NONCE_SIZE - 1; len <= NABTO_STREAM_NONCE_SIZE; len++) {
        StreamFixture f;
        f.initiator();
        f.stream.state = ST_SYN_SENT;
        std::vector<uint8_t> packet = PacketBuilder(NABTO_STREAM_FLAG_SYN | NABTO_STREAM_FLAG_ACK, 42)
            .ext(NABTO_STREAM_EXTENSION_ACK, ackPayload(0, 1000, 0))
            .ext(NABTO_STREAM_EXTENSION_SEGMENT_SIZES, segmentSizes(200, 200))
            .ext(NABTO_STREAM_EXTENSION_SYN, 0x11223344u)
            .ext(NABTO_STREAM_EXTENSION_NONCE, prefix(nonce, len))
            .build();
        handle(&f.stream, packet);

        if (len < NABTO_STREAM_NONCE_SIZE) {
            BOOST_TEST(f.stream.state == ST_SYN_SENT);
            BOOST_TEST(f.stream.maxAdvertisedWindow == 0u);
        } else {
            BOOST_TEST(f.stream.state == ST_ESTABLISHED);
            BOOST_TEST(f.stream.maxAdvertisedWindow == 1000u);
        }
    }
}

// L7: parse_ack_extension

BOOST_AUTO_TEST_CASE(ack_extension_with_bad_length_is_ignored)
{
    // 16 fixed bytes plus whole 8 byte gap blocks is the only valid shape.
    // A responder in ST_SYN_RCVD moves to ST_ESTABLISHED on a valid ack, so
    // the state shows whether the extension was acted on.
    const size_t badLengths[] = { 0, 4, 8, 12, 15, 17, 20, 23 };
    for (size_t i = 0; i < sizeof(badLengths)/sizeof(badLengths[0]); i++) {
        size_t length = badLengths[i];
        StreamFixture f;
        f.responder();
        f.stream.state = ST_SYN_RCVD;
        f.stream.nonceValidated = true;
        std::vector<uint8_t> payload = prefix(ackPayload(0, 1000, 8), length);
        std::vector<uint8_t> packet = PacketBuilder(NABTO_STREAM_FLAG_ACK, 42)
            .ext(NABTO_STREAM_EXTENSION_ACK, payload)
            .build();
        handle(&f.stream, packet);

        BOOST_TEST(f.stream.state == ST_SYN_RCVD, "length " << length);
        BOOST_TEST(f.stream.maxAdvertisedWindow == 0u, "length " << length);
    }

    const size_t goodLengths[] = { 16, 24, 32 };
    for (size_t i = 0; i < sizeof(goodLengths)/sizeof(goodLengths[0]); i++) {
        size_t length = goodLengths[i];
        StreamFixture f;
        f.responder();
        f.stream.state = ST_SYN_RCVD;
        f.stream.nonceValidated = true;
        std::vector<uint8_t> packet = PacketBuilder(NABTO_STREAM_FLAG_ACK, 42)
            .ext(NABTO_STREAM_EXTENSION_ACK, ackPayload(0, 1000, length - 16))
            .build();
        handle(&f.stream, packet);

        BOOST_TEST(f.stream.state == ST_ESTABLISHED, "length " << length);
        BOOST_TEST(f.stream.maxAdvertisedWindow == 1000u, "length " << length);
    }
}

BOOST_FIXTURE_TEST_CASE(ack_with_truncated_extension_list_has_no_effect, StreamFixture)
{
    // The ack extension is applied while walking the list, so only the
    // framing check in nabto_stream_handle_packet keeps a valid ack in front
    // of a truncated data extension from taking effect.
    responder();
    stream.state = ST_SYN_RCVD;
    stream.nonceValidated = true;
    std::vector<uint8_t> packet = PacketBuilder(NABTO_STREAM_FLAG_ACK, 42)
        .ext(NABTO_STREAM_EXTENSION_ACK, ackPayload(0, 1000, 0))
        .truncatedExt(NABTO_STREAM_EXTENSION_DATA, 10, 2)
        .build();
    handle(&stream, packet);

    BOOST_TEST(stream.state == ST_SYN_RCVD);
    BOOST_TEST(stream.maxAdvertisedWindow == 0u);
    BOOST_TEST(stream.receivedPackets == 0u);
}

// L8: add_ack_extension

BOOST_FIXTURE_TEST_CASE(add_ack_extension_with_tiny_buffer_returns_null, StreamFixture)
{
    // The extension needs 4 bytes of header, 16 fixed bytes and room for two
    // gap blocks: 36 bytes. Heap buffers of exactly N bytes so the sanitizer
    // sees any write past them.
    responder();
    stream.state = ST_ESTABLISHED;
    const size_t tooSmall[] = { 0, 1, 2, 3, 4, 5, 19, 20, 27, 28, 35 };
    for (size_t i = 0; i < sizeof(tooSmall)/sizeof(tooSmall[0]); i++) {
        size_t n = tooSmall[i];
        std::vector<uint8_t> buf(n, 0xff);
        BOOST_TEST(nabto_stream_add_ack_extension(&stream, buf.data(), buf.data() + n) == (uint8_t*)NULL, "n " << n);
    }

    std::vector<uint8_t> buf(36, 0xff);
    uint8_t* ptr = nabto_stream_add_ack_extension(&stream, buf.data(), buf.data() + 36);
    BOOST_TEST(ptr == buf.data() + 20);
    BOOST_TEST(readU16(buf.data()) == NABTO_STREAM_EXTENSION_ACK);
    BOOST_TEST(readU16(buf.data() + 2) == 16);
}

BOOST_FIXTURE_TEST_CASE(add_ack_extension_with_holes_and_no_room_for_gap_blocks_returns_null, StreamFixture)
{
    // Receive segment 3 with 1 and 2 missing so the window has a hole. With
    // room for zero gap blocks the old consolidation step underflowed the
    // block count and wrote gapBlocks[65535]; with room for one block it
    // overwrote the newest gap.
    responder();
    stream.state = ST_ESTABLISHED;
    const uint8_t data[4] = { 1, 2, 3, 4 };
    nabto_stream_handle_data(&stream, 3, data, sizeof(data));
    BOOST_REQUIRE(stream.recvMax == 3u);
    BOOST_REQUIRE(stream.recvTop == 0u);

    const size_t tooSmall[] = { 20, 27, 28, 35 };
    for (size_t i = 0; i < sizeof(tooSmall)/sizeof(tooSmall[0]); i++) {
        size_t n = tooSmall[i];
        std::vector<uint8_t> buf(n, 0xff);
        BOOST_TEST(nabto_stream_add_ack_extension(&stream, buf.data(), buf.data() + n) == (uint8_t*)NULL, "n " << n);
    }

    // one block: ack gap 1 (segment 3), nack gap 2 (segments 2 and 1).
    std::vector<uint8_t> buf(36, 0xff);
    uint8_t* ptr = nabto_stream_add_ack_extension(&stream, buf.data(), buf.data() + 36);
    BOOST_TEST(ptr == buf.data() + 28);
    BOOST_TEST(readU16(buf.data() + 2) == 24);
    BOOST_TEST(buf[23] == 1);
    BOOST_TEST(buf[27] == 2);
}

BOOST_FIXTURE_TEST_CASE(create_ack_packet_with_tiny_buffer_returns_0, StreamFixture)
{
    // header 5 + ack extension 36 = 41 bytes of room needed.
    responder();
    stream.state = ST_ESTABLISHED;
    stream.sentPackets = 5;
    double cwnd = stream.cCtrl.cwnd;
    const size_t tooSmall[] = { 0, 1, 4, 5, 6, 24, 25, 40 };
    for (size_t i = 0; i < sizeof(tooSmall)/sizeof(tooSmall[0]); i++) {
        size_t n = tooSmall[i];
        std::vector<uint8_t> buf(n, 0xff);
        BOOST_TEST(nabto_stream_create_packet(&stream, buf.data(), n, ET_ACK) == 0u, "n " << n);
        BOOST_TEST(stream.sentPackets == 5u, "n " << n);
        BOOST_TEST(stream.cCtrl.cwnd == cwnd, "n " << n);
    }

    std::vector<uint8_t> buf(41, 0xff);
    BOOST_TEST(nabto_stream_create_packet(&stream, buf.data(), 41, ET_ACK) == 25u);
    BOOST_TEST(buf[0] == NABTO_STREAM_FLAG_ACK);
}

BOOST_FIXTURE_TEST_CASE(create_ack_packet_keeps_send_nonce_when_buffer_is_too_small, StreamFixture)
{
    // header 5 + nonce response 12 + ack extension 36 = 53 bytes of room
    // needed. A failed packet must leave the nonce pending for the retry.
    initiator();
    stream.state = ST_ESTABLISHED;
    stream.sendNonce = true;
    const size_t tooSmall[] = { 16, 17, 41, 52 };
    for (size_t i = 0; i < sizeof(tooSmall)/sizeof(tooSmall[0]); i++) {
        size_t n = tooSmall[i];
        std::vector<uint8_t> buf(n, 0xff);
        BOOST_TEST(nabto_stream_create_packet(&stream, buf.data(), n, ET_ACK) == 0u, "n " << n);
        BOOST_TEST(stream.sendNonce, "n " << n);
    }

    std::vector<uint8_t> buf(53, 0xff);
    BOOST_TEST(nabto_stream_create_packet(&stream, buf.data(), 53, ET_ACK) == 37u);
    BOOST_TEST(!stream.sendNonce);
    BOOST_TEST(readU16(buf.data() + 5) == NABTO_STREAM_EXTENSION_NONCE_RESPONSE);
    BOOST_TEST(readU16(buf.data() + 17) == NABTO_STREAM_EXTENSION_ACK);
}

BOOST_FIXTURE_TEST_CASE(create_rst_packet_with_tiny_buffer_returns_0, StreamFixture)
{
    for (size_t n = 0; n < 5; n++) {
        std::vector<uint8_t> buf(n, 0xff);
        BOOST_TEST(nabto_stream_create_packet(&stream, buf.data(), n, ET_RST) == 0u, "n " << n);
    }
    std::vector<uint8_t> buf(5, 0xff);
    BOOST_TEST(nabto_stream_create_packet(&stream, buf.data(), 5, ET_RST) == 5u);
    BOOST_TEST(buf[0] == NABTO_STREAM_FLAG_RST);
}

BOOST_AUTO_TEST_SUITE_END()
