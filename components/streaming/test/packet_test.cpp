#include <boost/test/unit_test.hpp>
#include <nabto_stream/nabto_stream.h>
#include <nabto_stream/nabto_stream_memory.h>
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
 * Module user data for a fake module whose recv segment allocator counts
 * calls and fails after recvCap allocations, so a runaway allocation loop
 * fails an assertion instead of exhausting memory.
 */
struct CountingUserData {
    uint32_t stamp = 0;
    size_t recvAllocCalls = 0; // every call, failed ones included
    size_t recvAllocs = 0;     // successful calls
    size_t recvFrees = 0;
    size_t recvCap = 0;
};

uint32_t countingGetStamp(void* userData)
{
    CountingUserData* d = (CountingUserData*)userData;
    return d->stamp++;
}

struct nabto_stream_recv_segment* countingAllocRecvSegment(size_t bufferSize, void* userData)
{
    CountingUserData* d = (CountingUserData*)userData;
    d->recvAllocCalls++;
    if (d->recvAllocs >= d->recvCap) {
        return NULL;
    }
    d->recvAllocs++;
    return allocRecvSegment(bufferSize, NULL);
}

void countingFreeRecvSegment(struct nabto_stream_recv_segment* segment, void* userData)
{
    CountingUserData* d = (CountingUserData*)userData;
    d->recvFrees++;
    freeRecvSegment(segment, NULL);
}

/**
 * An established responder stream with recvTop = recvMax = recvMaxAllocated
 * = 0 and a counting allocator that fails after recvCap allocations. The
 * default cap leaves room for the whole recv window plus a few segments.
 */
struct CountingFixture {
    explicit CountingFixture(size_t recvCap = NABTO_STREAM_MAX_RECV_SEGMENTS + 16)
    {
        counts.recvCap = recvCap;
        memset(&module, 0, sizeof(module));
        module.get_stamp = &countingGetStamp;
        module.logger = NULL;
        module.alloc_send_segment = &allocSendSegment;
        module.free_send_segment = &freeSendSegment;
        module.alloc_recv_segment = &countingAllocRecvSegment;
        module.free_recv_segment = &countingFreeRecvSegment;
        module.notify_event = &notifyEvent;
        nabto_stream_init(&stream, &module, &counts);
        uint8_t nonce[NABTO_STREAM_NONCE_SIZE];
        memcpy(nonce, NONCE, sizeof(nonce));
        nabto_stream_init_responder(&stream, nonce);
        stream.state = ST_ESTABLISHED;
        stream.nonceValidated = true;
    }

    ~CountingFixture()
    {
        nabto_stream_destroy(&stream);
    }

    struct nabto_stream_module module;
    struct nabto_stream stream;
    CountingUserData counts;
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

uint32_t readU32(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

/** Data extension payload: seq followed by `size` bytes of data. */
std::vector<uint8_t> dataPayload(uint32_t seq, size_t size)
{
    std::vector<uint8_t> out = PacketBuilder::be32(seq);
    out.resize(4 + size, 0xab);
    return out;
}

std::vector<uint8_t> dataPacket(uint32_t seq, size_t size)
{
    return PacketBuilder(NABTO_STREAM_FLAG_ACK, 42)
        .ext(NABTO_STREAM_EXTENSION_DATA, dataPayload(seq, size))
        .build();
}

std::vector<uint8_t> finPacket(uint32_t seq)
{
    return PacketBuilder(NABTO_STREAM_FLAG_ACK, 42)
        .ext(NABTO_STREAM_EXTENSION_FIN, seq)
        .build();
}

/** A syn from a peer that sends segments up to `send` and receives up to `recv`. */
std::vector<uint8_t> synPacket(uint32_t seq, uint16_t send, uint16_t recv)
{
    return PacketBuilder(NABTO_STREAM_FLAG_SYN, 42)
        .ext(NABTO_STREAM_EXTENSION_SEGMENT_SIZES, segmentSizes(send, recv))
        .ext(NABTO_STREAM_EXTENSION_SYN, seq)
        .build();
}

std::vector<uint8_t> synAckPacket(uint32_t seq, uint16_t send, uint16_t recv)
{
    std::vector<uint8_t> nonce(NONCE, NONCE + NABTO_STREAM_NONCE_SIZE);
    return PacketBuilder(NABTO_STREAM_FLAG_SYN | NABTO_STREAM_FLAG_ACK, 42)
        .ext(NABTO_STREAM_EXTENSION_SEGMENT_SIZES, segmentSizes(send, recv))
        .ext(NABTO_STREAM_EXTENSION_SYN, seq)
        .ext(NABTO_STREAM_EXTENSION_NONCE, nonce)
        .build();
}

/** The packet a stream's own syn / syn|ack writer produces. */
std::vector<uint8_t> createSyn(struct nabto_stream* stream)
{
    std::vector<uint8_t> buf(256);
    size_t n = nabto_stream_create_syn_packet(stream, buf.data(), buf.size());
    BOOST_REQUIRE(n > 0);
    buf.resize(n);
    return buf;
}

std::vector<uint8_t> createSynAck(struct nabto_stream* stream)
{
    std::vector<uint8_t> buf(256);
    size_t n = nabto_stream_create_syn_ack_packet(stream, buf.data(), buf.size());
    BOOST_REQUIRE(n > 0);
    buf.resize(n);
    return buf;
}

/**
 * The window size field of the ack extension the stream would send now.
 * Tops up the idle recv segment first, as the integrators do before
 * building any packet.
 */
uint32_t advertisedWindow(struct nabto_stream* stream)
{
    nabto_stream_recv_segment_available(stream);
    // 20 bytes of header and fixed fields plus room for all gap blocks.
    std::vector<uint8_t> buf(20 + 8 * MAX_ACK_GAP_BLOCKS, 0xff);
    BOOST_REQUIRE(nabto_stream_add_ack_extension(stream, buf.data(), buf.data() + buf.size()) != (uint8_t*)NULL);
    return readU32(buf.data() + 8);
}

void handle(struct nabto_stream* stream, const std::vector<uint8_t>& packet)
{
    nabto_stream_handle_packet(stream, packet.data(), packet.size());
}

/**
 * An established initiator stream whose peer advertised `window` segments
 * above maxAcked 0 in its syn|ack, with replay protection disabled so the
 * acks built here need no nonce response. startSequenceNumber is 0, so the
 * first data segment has seq 1.
 */
struct SenderFixture {
    /**
     * Complete the handshake as the initiator. The syn|ack carries the
     * peer's logical stamp `peerStampBase` and the acks that follow count
     * on from it. A base at or above 2^31 compares as "before 0" in the
     * wrap safe stamp comparison, which is what the wrap-around case needs.
     */
    explicit SenderFixture(uint32_t window, uint32_t peerStampBase = 42)
        : peerStamp(peerStampBase)
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
        nabto_stream_init_initiator(&stream);
        nabto_stream_open(&stream, 0);
        std::vector<uint8_t> synAck = PacketBuilder(NABTO_STREAM_FLAG_SYN | NABTO_STREAM_FLAG_ACK, peerStamp)
            .ext(NABTO_STREAM_EXTENSION_ACK, ackPayload(0, window, 0))
            .ext(NABTO_STREAM_EXTENSION_SEGMENT_SIZES, segmentSizes(200, 200))
            .ext(NABTO_STREAM_EXTENSION_SYN, 0u)
            .build();
        handle(&stream, synAck);
        BOOST_REQUIRE(stream.state == ST_ESTABLISHED);
        BOOST_REQUIRE(stream.maxAdvertisedWindow == window);
    }

    ~SenderFixture()
    {
        nabto_stream_destroy(&stream);
    }

    /** Queue `n` segments of 10 bytes each. */
    void write(size_t n)
    {
        for (size_t i = 0; i < n; i++) {
            const uint8_t data[10] = { 0 };
            size_t written = 0;
            BOOST_REQUIRE(nabto_stream_write_buffer(&stream, data, sizeof(data), &written) == NABTO_STREAM_STATUS_OK);
            BOOST_REQUIRE(written == sizeof(data));
        }
    }

    /** Build one data packet, sending whatever flow control allows. */
    void send()
    {
        std::vector<uint8_t> buf(1500, 0xff);
        BOOST_REQUIRE(nabto_stream_create_packet(&stream, buf.data(), buf.size(), ET_DATA) > 0u);
    }

    /**
     * Deliver an ack with no gap blocks: everything up to maxAcked is acked.
     * Each ack carries the next peer stamp, as a peer sends them in order;
     * pass an older stamp to deliver an ack the network held back.
     */
    void ack(uint32_t maxAcked, uint32_t window)
    {
        ack(maxAcked, window, ++peerStamp);
    }

    void ack(uint32_t maxAcked, uint32_t window, uint32_t stamp)
    {
        std::vector<uint8_t> packet = PacketBuilder(NABTO_STREAM_FLAG_ACK, stamp)
            .ext(NABTO_STREAM_EXTENSION_ACK, ackPayload(maxAcked, window, 0))
            .build();
        handle(&stream, packet);
    }

    std::vector<uint32_t> unacked() const
    {
        std::vector<uint32_t> out;
        for (const struct nabto_stream_send_segment* s = stream.unacked->nextUnacked; s != stream.unacked; s = s->nextUnacked) {
            out.push_back(s->seq);
        }
        return out;
    }

    std::vector<uint32_t> sendList() const
    {
        std::vector<uint32_t> out;
        for (const struct nabto_stream_send_segment* s = stream.sendList->nextSend; s != stream.sendList; s = s->nextSend) {
            out.push_back(s->seq);
        }
        return out;
    }

    struct nabto_stream_module module;
    struct nabto_stream stream;
    uint32_t stamp = 0;
    /** The peer's logical stamp on its last packet, the syn|ack included. */
    uint32_t peerStamp;
};

std::vector<uint32_t> seqs(uint32_t from, uint32_t to)
{
    std::vector<uint32_t> out;
    for (uint32_t s = from; s <= to; s++) {
        out.push_back(s);
    }
    return out;
}

/**
 * Data of exactly the negotiated recv segment size lands in a recv segment
 * allocated at that size; one byte more is dropped before touching any
 * segment. seq is the peer's last used sequence number.
 */
void checkDataOfNegotiatedRecvSize(struct nabto_stream* stream, uint32_t seq)
{
    const uint16_t size = stream->maxRecvSegmentSize;
    BOOST_REQUIRE(stream->recvWindow->next != stream->recvWindow);
    BOOST_TEST(stream->recvWindow->next->capacity == size);

    handle(stream, dataPacket(seq + 1, size));
    BOOST_TEST(stream->recvTop == seq + 1);
    BOOST_TEST(stream->recvRead->next->size == size);

    handle(stream, dataPacket(seq + 2, size + 1u));
    BOOST_TEST(stream->recvMax == seq + 1);
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

// H4: recv window bound

BOOST_AUTO_TEST_CASE(data_far_above_recv_top_allocates_nothing)
{
    // The old receiver allocated one segment per sequence number up to the
    // one in the packet, stopping only when the allocator failed.
    const uint32_t seqs[] = { 0x7fffffffu, 0x80000000u, NABTO_STREAM_MAX_RECV_SEGMENTS + 1 };
    for (size_t i = 0; i < sizeof(seqs)/sizeof(seqs[0]); i++) {
        uint32_t seq = seqs[i];
        CountingFixture f;
        handle(&f.stream, dataPacket(seq, 1));

        BOOST_TEST(f.counts.recvAllocs == 0u, "seq " << seq);
        BOOST_TEST(f.stream.recvMaxAllocated == 0u, "seq " << seq);
        BOOST_TEST(f.stream.recvMax == 0u, "seq " << seq);
        BOOST_TEST(f.stream.recvTop == 0u, "seq " << seq);
        BOOST_TEST(f.stream.imediateAck, "seq " << seq);
        BOOST_TEST(f.stream.recvSegmentAllocationStamp.type == NABTO_STREAM_STAMP_INFINITE, "seq " << seq);
    }
}

BOOST_AUTO_TEST_CASE(fin_far_above_recv_top_allocates_nothing)
{
    const uint32_t seqs[] = { 0x7fffffffu, NABTO_STREAM_MAX_RECV_SEGMENTS + 1 };
    for (size_t i = 0; i < sizeof(seqs)/sizeof(seqs[0]); i++) {
        uint32_t seq = seqs[i];
        CountingFixture f;
        handle(&f.stream, finPacket(seq));

        BOOST_TEST(f.counts.recvAllocs == 0u, "seq " << seq);
        BOOST_TEST(f.stream.recvMax == 0u, "seq " << seq);
        BOOST_TEST(f.stream.state == ST_ESTABLISHED, "seq " << seq);
    }
}

BOOST_AUTO_TEST_CASE(data_at_window_edge_is_accepted_and_one_past_is_dropped)
{
    const uint32_t window = NABTO_STREAM_MAX_RECV_SEGMENTS;
    CountingFixture f;
    handle(&f.stream, dataPacket(window, 4));
    BOOST_TEST(f.counts.recvAllocs == window);
    BOOST_TEST(f.stream.recvMaxAllocated == window);
    BOOST_TEST(f.stream.recvMax == window);
    BOOST_TEST(f.stream.recvTop == 0u);

    handle(&f.stream, dataPacket(window + 1, 4));
    BOOST_TEST(f.counts.recvAllocs == window);
    BOOST_TEST(f.stream.recvMaxAllocated == window);
    BOOST_TEST(f.stream.recvMax == window);

    // the fin is subject to the same bound.
    handle(&f.stream, finPacket(window + 1));
    BOOST_TEST(f.counts.recvAllocs == window);
    BOOST_TEST(f.stream.state == ST_ESTABLISHED);

    // destroy frees the whole window (the fixture's second destroy is a no-op).
    nabto_stream_destroy(&f.stream);
    BOOST_TEST(f.counts.recvFrees == window);
}

BOOST_AUTO_TEST_CASE(advertised_window_tracks_recv_top)
{
    // The ack carries recvMax as maxAcked, so the window field must be the
    // bound minus the segments already received above recvTop: the peer
    // computes maxAcked + window = recvTop + bound. The old code advertised
    // a constant 424242.
    const uint32_t window = NABTO_STREAM_MAX_RECV_SEGMENTS;
    CountingFixture f;
    BOOST_TEST(advertisedWindow(&f.stream) == window);
    BOOST_TEST(f.counts.recvAllocs == 1u);

    // out of order data with a hole at 1 shrinks the window to zero.
    const uint32_t checkpoints[] = { 2, window / 2, window };
    for (uint32_t seq = 2; seq <= window; seq++) {
        handle(&f.stream, dataPacket(seq, 4));
        for (size_t i = 0; i < sizeof(checkpoints)/sizeof(checkpoints[0]); i++) {
            if (seq == checkpoints[i]) {
                BOOST_TEST(advertisedWindow(&f.stream) == window - seq, "seq " << seq);
            }
        }
    }
    BOOST_TEST(f.stream.recvMax == window);
    BOOST_TEST(f.stream.recvTop == 0u);
    // the top up keeps one idle segment past the window, which is never
    // filled because find_recv_buffer rejects it.
    BOOST_TEST(f.counts.recvAllocs == window + 1);

    // filling the hole reopens the whole window.
    handle(&f.stream, dataPacket(1, 4));
    BOOST_TEST(f.stream.recvTop == window);
    BOOST_TEST(advertisedWindow(&f.stream) == window);
}

BOOST_AUTO_TEST_CASE(closed_window_rejects_data_without_calling_the_allocator)
{
    // A recv segment could not be allocated: the window is advertised as
    // closed, and data above recvMax is dropped without asking the
    // allocator again, so a flood cannot keep pushing the retry.
    CountingFixture f(1);
    handle(&f.stream, dataPacket(1, 4));
    BOOST_TEST(f.stream.recvTop == 1u);
    BOOST_TEST(advertisedWindow(&f.stream) == 0u);
    BOOST_REQUIRE(f.stream.recvSegmentAllocationStamp.type == NABTO_STREAM_STAMP_FUTURE);
    size_t allocCalls = f.counts.recvAllocCalls;
    uint32_t retryStamp = f.stream.recvSegmentAllocationStamp.stamp;
    f.stream.imediateAck = false;

    handle(&f.stream, dataPacket(2, 4));
    BOOST_TEST(f.stream.recvMax == 1u);
    BOOST_TEST(f.counts.recvAllocCalls == allocCalls);
    BOOST_TEST(f.stream.recvSegmentAllocationStamp.type == NABTO_STREAM_STAMP_FUTURE);
    BOOST_TEST(f.stream.recvSegmentAllocationStamp.stamp == retryStamp);
    BOOST_TEST(f.stream.imediateAck);
    f.stream.imediateAck = false;

    // the retry succeeds: the window opens and an ack is forced.
    f.counts.recvCap = 2;
    nabto_stream_next_event_to_handle(&f.stream);
    BOOST_TEST(f.counts.recvAllocs == 2u);
    BOOST_TEST(f.stream.recvSegmentAllocationStamp.type == NABTO_STREAM_STAMP_INFINITE);
    BOOST_TEST(f.stream.imediateAck);
    BOOST_TEST(advertisedWindow(&f.stream) == (uint32_t)NABTO_STREAM_MAX_RECV_SEGMENTS);

    handle(&f.stream, dataPacket(2, 4));
    BOOST_TEST(f.stream.recvTop == 2u);
}

BOOST_AUTO_TEST_CASE(two_segments_in_one_packet_are_both_accepted)
{
    // After the first segment fills the pre-allocated idle segment there is
    // no idle segment left; that must not read as a closed window for the
    // second segment of the same packet.
    CountingFixture f;
    nabto_stream_recv_segment_available(&f.stream);
    BOOST_REQUIRE(f.stream.recvMaxAllocated == 1u);
    std::vector<uint8_t> packet = PacketBuilder(NABTO_STREAM_FLAG_ACK, 42)
        .ext(NABTO_STREAM_EXTENSION_DATA, dataPayload(1, 4))
        .ext(NABTO_STREAM_EXTENSION_DATA, dataPayload(2, 4))
        .build();
    handle(&f.stream, packet);

    BOOST_TEST(f.stream.recvTop == 2u);
    BOOST_TEST(f.stream.recvMax == 2u);
    BOOST_TEST(f.counts.recvAllocs == 2u);
}

// M3: stale acks and flight size on window reduction

BOOST_AUTO_TEST_CASE(reordered_stale_ack_does_not_reduce_the_window)
{
    // Segments 1..4 are in flight. The ack for 2 arrives before the ack for
    // 1; the older one advertises a smaller window edge (the receiver's
    // allocator was momentarily empty). It must not move 3 and 4 back to
    // the send list.
    SenderFixture f(4);
    f.write(4);
    f.send();
    BOOST_REQUIRE(f.unacked() == seqs(1, 4));
    BOOST_REQUIRE(f.stream.cCtrl.flightSize == 4u);

    f.ack(2, 4);
    BOOST_TEST(f.stream.maxAdvertisedWindow == 6u);
    BOOST_TEST(f.unacked() == seqs(3, 4));
    BOOST_TEST(f.stream.cCtrl.flightSize == 2u);

    f.ack(1, 0, f.peerStamp - 1);
    BOOST_TEST(f.stream.maxAdvertisedWindow == 6u);
    BOOST_TEST(f.unacked() == seqs(3, 4));
    BOOST_TEST(f.sendList().empty());
    BOOST_TEST(f.stream.cCtrl.flightSize == 2u);

    f.ack(4, 4);
    BOOST_TEST(f.unacked().empty());
    BOOST_TEST(f.stream.cCtrl.flightSize == 0u);
}

BOOST_AUTO_TEST_CASE(ack_with_equal_max_acked_and_larger_window_is_applied)
{
    // The receiver reopens its window without having received more data:
    // same maxAcked, larger window. That ack is not stale.
    SenderFixture f(2);
    f.write(2);
    f.send();
    BOOST_REQUIRE(f.unacked() == seqs(1, 2));

    f.ack(2, 0);
    BOOST_TEST(f.stream.maxAdvertisedWindow == 2u);
    f.ack(2, 4);
    BOOST_TEST(f.stream.maxAdvertisedWindow == 6u);
}

BOOST_AUTO_TEST_CASE(reordered_zero_window_ack_with_equal_max_acked_does_not_close_the_window)
{
    // The receiver fails to allocate a recv segment and sends ack(2, 0),
    // then recovers and sends ack(2, 4) without having received more data.
    // The two reorder on the network: the recovery arrives first and the
    // zero window last, with the same maxAcked but an older stamp. The
    // superseded zero window must not move 3 and 4 back to the send list.
    SenderFixture f(4);
    f.write(4);
    f.send();
    BOOST_REQUIRE(f.unacked() == seqs(1, 4));
    BOOST_REQUIRE(f.stream.cCtrl.flightSize == 4u);

    f.ack(2, 4);
    BOOST_TEST(f.stream.maxAdvertisedWindow == 6u);
    BOOST_TEST(f.unacked() == seqs(3, 4));
    BOOST_TEST(f.stream.cCtrl.flightSize == 2u);

    f.ack(2, 4);
    f.ack(2, 0, f.peerStamp - 1);
    BOOST_TEST(f.stream.maxAdvertisedWindow == 6u);
    BOOST_TEST(f.unacked() == seqs(3, 4));
    BOOST_TEST(f.sendList().empty());
    BOOST_TEST(f.stream.cCtrl.flightSize == 2u);

    f.ack(4, 4);
    BOOST_TEST(f.unacked().empty());
    BOOST_TEST(f.stream.cCtrl.flightSize == 0u);
}

BOOST_AUTO_TEST_CASE(syn_ack_window_is_applied_when_the_peer_stamps_start_in_the_upper_half)
{
    // The peer starts its logical stamps at 4294967286, which the wrap safe
    // comparison places before our initial timestampToEcho of 0. The
    // syn|ack's window advertisement must still be applied: it is the
    // packet which anchors the stamp, not a reordered one.
    SenderFixture f(4, 4294967286u);
    BOOST_TEST(f.stream.maxAdvertisedWindow == 4u);
    BOOST_TEST(f.stream.timestampToEcho == 4294967286u);

    f.write(4);
    f.send();
    BOOST_REQUIRE(f.unacked() == seqs(1, 4));

    f.ack(2, 4);
    BOOST_TEST(f.peerStamp == 4294967287u);
    BOOST_TEST(f.stream.maxAdvertisedWindow == 6u);
    BOOST_TEST(f.unacked() == seqs(3, 4));
}

BOOST_AUTO_TEST_CASE(window_reduction_keeps_flight_size_in_step_with_unacked)
{
    // A genuine reduction moves 3 and 4 from unacked back to the send list;
    // they leave the flight and re-enter it when they are sent again, so
    // the flight size must be 0 once everything is acked.
    SenderFixture f(4);
    f.write(4);
    f.send();
    BOOST_REQUIRE(f.stream.cCtrl.flightSize == 4u);

    f.ack(2, 0);
    BOOST_TEST(f.stream.maxAdvertisedWindow == 2u);
    BOOST_TEST(f.unacked().empty());
    BOOST_TEST(f.sendList() == seqs(3, 4));
    BOOST_TEST(f.stream.cCtrl.flightSize == 0u);

    f.ack(2, 4);
    f.send();
    BOOST_TEST(f.unacked() == seqs(3, 4));
    BOOST_TEST(f.sendList().empty());
    BOOST_TEST(f.stream.cCtrl.flightSize == 2u);

    f.ack(4, 4);
    BOOST_TEST(f.unacked().empty());
    BOOST_TEST(f.stream.cCtrl.flightSize == 0u);
}

// M2: segment size negotiation

BOOST_FIXTURE_TEST_CASE(responder_negotiates_send_and_recv_segment_sizes, StreamFixture)
{
    // Our send size is bounded by the peer's recv size and our recv size by
    // the peer's send size. The sizes are picked so that the old code (both
    // assignments to maxSendSegmentSize) ends at 300/900 and a same-field
    // pairing at 200/300.
    responder();
    stream.maxSendSegmentSize = 250;
    stream.maxRecvSegmentSize = 900;
    handle(&stream, synPacket(0x11223344u, 200, 300));

    BOOST_TEST(stream.state == ST_ACCEPT);
    BOOST_TEST(stream.maxSendSegmentSize == 250);
    BOOST_TEST(stream.maxRecvSegmentSize == 200);

    // accept allocates the first segments from the negotiated sizes.
    nabto_stream_accept(&stream);
    BOOST_TEST(stream.nextUnfilledSendSegment->capacity == 250);
    stream.state = ST_ESTABLISHED;
    stream.nonceValidated = true;
    checkDataOfNegotiatedRecvSize(&stream, 0x11223344u);
}

BOOST_FIXTURE_TEST_CASE(initiator_negotiates_send_and_recv_segment_sizes, StreamFixture)
{
    initiator();
    nabto_stream_open(&stream, 0);
    stream.maxSendSegmentSize = 250;
    stream.maxRecvSegmentSize = 900;
    handle(&stream, synAckPacket(0x11223344u, 200, 300));

    BOOST_TEST(stream.state == ST_ESTABLISHED);
    BOOST_TEST(stream.maxSendSegmentSize == 250);
    BOOST_TEST(stream.maxRecvSegmentSize == 200);
    BOOST_TEST(stream.nextUnfilledSendSegment->capacity == 250);
    checkDataOfNegotiatedRecvSize(&stream, 0x11223344u);
}

BOOST_AUTO_TEST_CASE(both_ends_agree_on_segment_sizes_after_handshake)
{
    // Run the real syn and syn|ack writers between two streams: what one
    // end sends must be what the other end receives, in both directions.
    StreamFixture initiator;
    StreamFixture responder;
    initiator.initiator();
    responder.responder();
    nabto_stream_open(&initiator.stream, 0);
    initiator.stream.maxSendSegmentSize = 250;
    initiator.stream.maxRecvSegmentSize = 900;
    responder.stream.maxSendSegmentSize = 300;
    responder.stream.maxRecvSegmentSize = 220;

    handle(&responder.stream, createSyn(&initiator.stream));
    BOOST_REQUIRE(responder.stream.state == ST_ACCEPT);
    nabto_stream_accept(&responder.stream);
    handle(&initiator.stream, createSynAck(&responder.stream));
    BOOST_REQUIRE(initiator.stream.state == ST_ESTABLISHED);

    BOOST_TEST(responder.stream.maxSendSegmentSize == 300);
    BOOST_TEST(responder.stream.maxRecvSegmentSize == 220);
    BOOST_TEST(initiator.stream.maxSendSegmentSize == 220);
    BOOST_TEST(initiator.stream.maxRecvSegmentSize == 300);
}

BOOST_AUTO_TEST_SUITE_END()
