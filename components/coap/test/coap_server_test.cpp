#include <boost/test/unit_test.hpp>
#include <nabto_coap/nabto_coap_server.h>
#include "../src/nabto_coap_server_impl.h" // request internals for assertions

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

// Every allocation the server makes is counted so a test can tell
// that the server released everything it took.
size_t liveAllocations = 0;

void* countingCalloc(size_t n, size_t size)
{
    void* p = calloc(n, size);
    if (p != NULL) {
        liveAllocations++;
    }
    return p;
}

void countingFree(void* p)
{
    if (p != NULL) {
        liveAllocations--;
    }
    free(p);
}

struct nn_allocator countingAllocator = {
    &countingCalloc,
    &countingFree
};

nabto_coap_token makeToken(const std::string& s)
{
    nabto_coap_token t;
    memset(&t, 0, sizeof(t));
    t.tokenLength = (uint8_t)s.size();
    memcpy(t.token, s.data(), s.size());
    return t;
}

uint32_t blockOption(uint32_t num, bool more, uint32_t szx)
{
    return (num << 4) | ((more ? 1u : 0u) << 3) | szx;
}

/**
 * Builds a request with a single Uri-Path segment, /test by default.
 */
class RequestBuilder {
 public:
    // observe >= 0 adds an Observe option with that value.
    RequestBuilder(nabto_coap_type type, nabto_coap_code code, uint16_t messageId, const std::string& token, const std::string& path = "test", int observe = -1)
    {
        struct nabto_coap_message_header header;
        memset(&header, 0, sizeof(header));
        header.type = type;
        header.code = code;
        header.messageId = messageId;
        header.token = makeToken(token);
        ptr_ = nabto_coap_encode_header(&header, buffer_, end());
        BOOST_REQUIRE(ptr_ != NULL);
        if (observe >= 0) {
            ptr_ = nabto_coap_encode_varint_option(NABTO_COAP_OPTION_OBSERVE - currentOption_, (uint32_t)observe, ptr_, end());
            BOOST_REQUIRE(ptr_ != NULL);
            currentOption_ = NABTO_COAP_OPTION_OBSERVE;
        }
        ptr_ = nabto_coap_encode_option(NABTO_COAP_OPTION_URI_PATH - currentOption_, (const uint8_t*)path.data(), path.size(), ptr_, end());
        BOOST_REQUIRE(ptr_ != NULL);
        currentOption_ = NABTO_COAP_OPTION_URI_PATH;
    }

    // Uri-Query is critical and not understood by the server.
    RequestBuilder& uriQuery(const std::string& query)
    {
        ptr_ = nabto_coap_encode_option(NABTO_COAP_OPTION_URI_QUERY - currentOption_, (const uint8_t*)query.data(), query.size(), ptr_, end());
        BOOST_REQUIRE(ptr_ != NULL);
        currentOption_ = NABTO_COAP_OPTION_URI_QUERY;
        return *this;
    }

    RequestBuilder& block1(uint32_t num, bool more, uint32_t szx)
    {
        ptr_ = nabto_coap_encode_varint_option(NABTO_COAP_OPTION_BLOCK1 - currentOption_, blockOption(num, more, szx), ptr_, end());
        BOOST_REQUIRE(ptr_ != NULL);
        currentOption_ = NABTO_COAP_OPTION_BLOCK1;
        return *this;
    }

    RequestBuilder& block2(uint32_t num, uint32_t szx)
    {
        ptr_ = nabto_coap_encode_varint_option(NABTO_COAP_OPTION_BLOCK2 - currentOption_, blockOption(num, false, szx), ptr_, end());
        BOOST_REQUIRE(ptr_ != NULL);
        currentOption_ = NABTO_COAP_OPTION_BLOCK2;
        return *this;
    }

    RequestBuilder& payload(const std::string& data)
    {
        ptr_ = nabto_coap_encode_payload((const uint8_t*)data.data(), data.size(), ptr_, end());
        BOOST_REQUIRE(ptr_ != NULL);
        return *this;
    }

    std::vector<uint8_t> build() const { return std::vector<uint8_t>(buffer_, (const uint8_t*)ptr_); }

 private:
    uint8_t* end() { return buffer_ + sizeof(buffer_); }
    uint8_t buffer_[512];
    uint8_t* ptr_;
    uint16_t currentOption_ = 0;
};

std::vector<uint8_t> emptyPacket(nabto_coap_type type, uint16_t messageId)
{
    uint8_t buffer[16];
    struct nabto_coap_message_header header;
    memset(&header, 0, sizeof(header));
    header.type = type;
    header.code = NABTO_COAP_CODE_EMPTY;
    header.messageId = messageId;
    uint8_t* ptr = nabto_coap_encode_header(&header, buffer, buffer + sizeof(buffer));
    BOOST_REQUIRE(ptr != NULL);
    return std::vector<uint8_t>(buffer, ptr);
}

std::vector<uint8_t> ackPacket(uint16_t messageId)
{
    return emptyPacket(NABTO_COAP_TYPE_ACK, messageId);
}

std::vector<uint8_t> rstPacket(uint16_t messageId)
{
    return emptyPacket(NABTO_COAP_TYPE_RST, messageId);
}

/**
 * The interesting parts of a packet the server wanted to send.
 */
struct SentMessage {
    nabto_coap_type type;
    nabto_coap_code code;
    uint16_t messageId;
    std::string token;
    std::string payload;
    bool hasBlock1;
    uint32_t block1;
    bool hasBlock2;
    uint32_t block2;
    bool hasObserve;
    uint32_t observe;
};

/**
 * A server with GET /test and POST /test whose handler records the
 * request and leaves it to the test to respond, so the request stays
 * pending while the client retransmits.
 */
class TestServer {
 public:
    TestServer()
        : allocationsBefore_(liveAllocations)
    {
        BOOST_REQUIRE(nabto_coap_server_init(&server, NULL, &countingAllocator) == NABTO_COAP_ERROR_OK);
        BOOST_REQUIRE(nabto_coap_server_requests_init(&requests, &server, &TestServer::getStamp, &TestServer::notifyEvent, this) == NABTO_COAP_ERROR_OK);
        const char* path[] = { "test", NULL };
        struct nabto_coap_server_resource* resource;
        BOOST_REQUIRE(nabto_coap_server_add_resource(&server, NABTO_COAP_CODE_GET, path, &TestServer::handler, this, &getResource) == NABTO_COAP_ERROR_OK);
        BOOST_REQUIRE(nabto_coap_server_add_resource(&server, NABTO_COAP_CODE_POST, path, &TestServer::handler, this, &resource) == NABTO_COAP_ERROR_OK);
    }
    ~TestServer()
    {
        nabto_coap_server_requests_destroy(&requests);
        nabto_coap_server_destroy(&server);
        BOOST_TEST(liveAllocations == allocationsBefore_);
    }

    // conn defaults to the first connection; pass connection2() to act
    // as a second, independent client of the same requests context.
    void handlePacket(const std::vector<uint8_t>& packet, void* conn = NULL)
    {
        nabto_coap_server_handle_packet(&requests, conn ? conn : connection(), packet.data(), packet.size());
    }

    // Send everything the server has queued and return it. Everything
    // queued must be for conn.
    std::vector<SentMessage> drain(void* conn = NULL)
    {
        std::vector<SentMessage> sent;
        while (nabto_coap_server_next_event(&requests) == NABTO_COAP_SERVER_NEXT_EVENT_SEND) {
            BOOST_REQUIRE(nabto_coap_server_get_connection_send(&requests) == (conn ? conn : connection()));
            uint8_t buffer[1500];
            uint8_t* ptr = nabto_coap_server_handle_send(&requests, buffer, buffer + sizeof(buffer));
            BOOST_REQUIRE(ptr != NULL);
            struct nabto_coap_incoming_message msg;
            BOOST_REQUIRE(nabto_coap_parse_message(buffer, ptr - buffer, &msg));
            SentMessage m;
            m.type = msg.type;
            m.code = msg.code;
            m.messageId = msg.messageId;
            m.token = std::string((const char*)msg.token.token, msg.token.tokenLength);
            if (msg.payload != NULL) {
                m.payload = std::string((const char*)msg.payload, msg.payloadLength);
            }
            m.hasBlock1 = msg.hasBlock1;
            m.block1 = msg.block1;
            m.hasBlock2 = msg.hasBlock2;
            m.block2 = msg.block2;
            m.hasObserve = msg.hasObserve;
            m.observe = msg.observe;
            sent.push_back(m);
        }
        return sent;
    }

    // Ask the server to send into a buffer of the given size; returns
    // what handle_send returned.
    uint8_t* sendInto(size_t size)
    {
        uint8_t buffer[64];
        BOOST_REQUIRE(size <= sizeof(buffer));
        return nabto_coap_server_handle_send(&requests, buffer, buffer + size);
    }

    // Respond to the most recent pending request and complete the
    // exchange so the request is released before the server is torn down.
    void respondAndFinish(nabto_coap_code code)
    {
        respond(request, code);
        request = NULL;
        BOOST_TEST(requests.activeRequests == 0u);
    }

    // Respond to one pending request and complete its exchange.
    void respond(struct nabto_coap_server_request* r, nabto_coap_code code)
    {
        BOOST_REQUIRE(r != NULL);
        nabto_coap_server_response_set_code(r, code);
        BOOST_REQUIRE(nabto_coap_server_response_ready(r) == NABTO_COAP_ERROR_OK);
        nabto_coap_server_request_free(r);

        std::vector<SentMessage> sent = drain();
        BOOST_REQUIRE(sent.size() == 1);
        BOOST_TEST(sent[0].code == code);
        if (sent[0].type == NABTO_COAP_TYPE_CON) {
            handlePacket(ackPacket(sent[0].messageId));
        } else {
            // NON responses are released on the next timeout.
            now += NABTO_COAP_ACK_TIMEOUT;
            nabto_coap_server_handle_timeout(&requests);
        }
    }

    // Respond to one pending request and hand it back to the server
    // without sending or acknowledging anything.
    void respondNoAck(struct nabto_coap_server_request* r, nabto_coap_code code)
    {
        BOOST_REQUIRE(r != NULL);
        nabto_coap_server_response_set_code(r, code);
        BOOST_REQUIRE(nabto_coap_server_response_ready(r) == NABTO_COAP_ERROR_OK);
        nabto_coap_server_request_free(r);
    }

    // Advance the clock past every possible retransmission backoff and
    // run one timeout tick.
    void timeoutTick()
    {
        now += NABTO_COAP_ACK_TIMEOUT << NABTO_COAP_MAX_RETRANSMITS;
        nabto_coap_server_handle_timeout(&requests);
    }

    // Issue a CON GET with the given token, answer it with a payload
    // (empty payload: no payload at all) and send block 0. With ack the
    // client ACKs it, so the request sits in RESPONSE state waiting for
    // the next block; without, the response is still in flight.
    void startBlock2Response(const std::string& token, const std::string& body, uint16_t messageId, bool ack = true)
    {
        handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_GET, messageId, token).build());
        BOOST_TEST(drain().size() == 1u); // empty ACK
        BOOST_REQUIRE(request != NULL);
        if (!body.empty()) {
            BOOST_REQUIRE(nabto_coap_server_response_set_payload(request, body.data(), body.size()) == NABTO_COAP_ERROR_OK);
        }
        nabto_coap_server_response_set_code(request, NABTO_COAP_CODE_CONTENT);
        BOOST_REQUIRE(nabto_coap_server_response_ready(request) == NABTO_COAP_ERROR_OK);
        nabto_coap_server_request_free(request);
        request = NULL;

        std::vector<SentMessage> sent = drain();
        BOOST_REQUIRE(sent.size() == 1);
        BOOST_TEST(sent[0].type == NABTO_COAP_TYPE_CON);
        BOOST_TEST(sent[0].code == NABTO_COAP_CODE_CONTENT);
        BOOST_TEST(sent[0].hasBlock2 == (body.size() > 512));
        BOOST_TEST(sent[0].payload == body.substr(0, 512));
        if (ack) {
            handlePacket(ackPacket(sent[0].messageId));
        }
    }

    // Ask for a further Block2 block with a CON GET and return the CON
    // block, after checking the empty ACK that precedes it.
    SentMessage requestBlock2(const std::string& token, uint16_t messageId, uint32_t num, uint32_t szx)
    {
        handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_GET, messageId, token).block2(num, szx).build());
        std::vector<SentMessage> sent = drain();
        BOOST_REQUIRE(sent.size() == 2);
        BOOST_TEST(sent[0].type == NABTO_COAP_TYPE_ACK);
        BOOST_TEST(sent[0].code == NABTO_COAP_CODE_EMPTY);
        BOOST_TEST(sent[0].messageId == messageId);
        BOOST_TEST(sent[1].type == NABTO_COAP_TYPE_CON);
        BOOST_TEST(sent[1].code == NABTO_COAP_CODE_CONTENT);
        BOOST_TEST(sent[1].token == token);
        BOOST_TEST(sent[1].hasBlock2);
        return sent[1];
    }

    // Issue a CON GET with Observe=0 and accept the registration; the
    // request is left with the test to answer.
    void registerObserver(const std::string& token, uint16_t messageId)
    {
        handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_GET, messageId, token, "test", 0).build());
        BOOST_TEST(drain().size() == 1u); // empty ACK
        BOOST_REQUIRE(request != NULL);
        BOOST_REQUIRE(nabto_coap_server_request_accept_observe(request) == NABTO_COAP_ERROR_OK);
    }

    size_t observerCount()
    {
        size_t count = 0;
        struct nabto_coap_server_observer* obs = requests.observersSentinel->next;
        while (obs != requests.observersSentinel) {
            count++;
            obs = obs->next;
        }
        return count;
    }

    void* connection() { return &connection_; }
    void* connection2() { return &connection2_; }

    struct nabto_coap_server server;
    struct nabto_coap_server_requests requests;
    struct nabto_coap_server_resource* getResource = NULL;
    struct nabto_coap_server_request* request = NULL; // most recent
    std::vector<struct nabto_coap_server_request*> pendingRequests;
    size_t handlerCalls = 0;
    uint32_t now = 1000;

 private:
    static uint32_t getStamp(void* userData) { return static_cast<TestServer*>(userData)->now; }
    static void notifyEvent(void* userData) { (void)userData; }
    static void handler(struct nabto_coap_server_request* request, void* userData)
    {
        TestServer* self = static_cast<TestServer*>(userData);
        self->handlerCalls++;
        self->request = request;
        self->pendingRequests.push_back(request);
    }

    int connection_;
    int connection2_;
    size_t allocationsBefore_;
};

// The route tree tests never dispatch a request, they only build
// resources.
void unusedHandler(struct nabto_coap_server_request* request, void* userData)
{
    (void)request;
    (void)userData;
}

} // namespace

BOOST_AUTO_TEST_SUITE(coap_server)

// RFC 7252 section 4.5: a duplicate CON request must be acknowledged
// with the same ACK as the original, but processed only once.
BOOST_AUTO_TEST_CASE(retransmitted_con_request_is_acked_again)
{
    TestServer s;
    std::vector<uint8_t> req = RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_GET, 0x1234, "t1").build();

    s.handlePacket(req);
    std::vector<SentMessage> sent = s.drain();
    BOOST_REQUIRE(sent.size() == 1);
    BOOST_TEST(sent[0].type == NABTO_COAP_TYPE_ACK);
    BOOST_TEST(sent[0].code == NABTO_COAP_CODE_EMPTY);
    BOOST_TEST(sent[0].messageId == 0x1234);
    BOOST_TEST(s.handlerCalls == 1u);

    // The client did not get the ACK and retransmits.
    s.handlePacket(req);
    sent = s.drain();
    BOOST_REQUIRE(sent.size() == 1);
    BOOST_TEST(sent[0].type == NABTO_COAP_TYPE_ACK);
    BOOST_TEST(sent[0].code == NABTO_COAP_CODE_EMPTY);
    BOOST_TEST(sent[0].messageId == 0x1234);
    BOOST_TEST(s.handlerCalls == 1u);

    s.respondAndFinish(NABTO_COAP_CODE_CONTENT);
}

// An intermediate Block1 chunk is acknowledged with a piggybacked 2.31
// Continue (RFC 7959 section 2.5), so that is the ACK a duplicate of
// it must get.
BOOST_AUTO_TEST_CASE(retransmitted_block1_chunk_gets_continue_again)
{
    TestServer s;
    const std::string chunk0(16, 'a');
    const std::string chunk1(16, 'b');
    std::vector<uint8_t> first = RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_POST, 0x2000, "t2").block1(0, true, 0).payload(chunk0).build();

    s.handlePacket(first);
    std::vector<SentMessage> sent = s.drain();
    BOOST_REQUIRE(sent.size() == 1);
    BOOST_TEST(sent[0].type == NABTO_COAP_TYPE_ACK);
    BOOST_TEST(sent[0].code == NABTO_COAP_CODE_CONTINUE);
    BOOST_TEST(sent[0].messageId == 0x2000);
    BOOST_TEST(sent[0].hasBlock1);
    BOOST_TEST(sent[0].block1 == blockOption(0, true, 0));
    BOOST_TEST(s.handlerCalls == 0u);

    // The client did not get the Continue and retransmits the chunk.
    s.handlePacket(first);
    sent = s.drain();
    BOOST_REQUIRE(sent.size() == 1);
    BOOST_TEST(sent[0].type == NABTO_COAP_TYPE_ACK);
    BOOST_TEST(sent[0].code == NABTO_COAP_CODE_CONTINUE);
    BOOST_TEST(sent[0].messageId == 0x2000);
    BOOST_TEST(sent[0].hasBlock1);
    BOOST_TEST(sent[0].block1 == blockOption(0, true, 0));
    BOOST_TEST(s.handlerCalls == 0u);

    // The last chunk completes the request; the duplicate must not
    // have been appended to the body.
    s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_POST, 0x2001, "t2").block1(1, false, 0).payload(chunk1).build());
    sent = s.drain();
    BOOST_REQUIRE(sent.size() == 1);
    BOOST_TEST(sent[0].type == NABTO_COAP_TYPE_ACK);
    BOOST_TEST(sent[0].code == NABTO_COAP_CODE_EMPTY);
    BOOST_TEST(sent[0].messageId == 0x2001);
    BOOST_REQUIRE(s.handlerCalls == 1u);
    void* payload;
    size_t payloadLength;
    BOOST_TEST(nabto_coap_server_request_get_payload(s.request, &payload, &payloadLength));
    BOOST_TEST(payloadLength == 32u);
    BOOST_TEST(std::string((const char*)payload, payloadLength) == chunk0 + chunk1);

    s.respondAndFinish(NABTO_COAP_CODE_CHANGED);
}

// NON requests are never acknowledged, and a duplicate is processed only once.
BOOST_AUTO_TEST_CASE(retransmitted_non_request_is_ignored)
{
    TestServer s;
    std::vector<uint8_t> req = RequestBuilder(NABTO_COAP_TYPE_NON, NABTO_COAP_CODE_GET, 0x3000, "t3").build();

    s.handlePacket(req);
    BOOST_TEST(s.drain().empty());
    BOOST_TEST(s.handlerCalls == 1u);

    s.handlePacket(req);
    BOOST_TEST(s.drain().empty());
    BOOST_TEST(s.handlerCalls == 1u);

    s.respondAndFinish(NABTO_COAP_CODE_CONTENT);
}

// The server keeps at most one pending error. A second error before
// the first has been sent must not overwrite it; the second client
// retransmits and gets its error afterwards.
BOOST_AUTO_TEST_CASE(pending_error_is_not_overwritten)
{
    TestServer s;
    std::vector<uint8_t> first = RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_GET, 0x4001, "t1", "nope").build();
    std::vector<uint8_t> second = RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_GET, 0x4002, "t2", "nope").build();

    s.handlePacket(first);
    s.handlePacket(second);
    std::vector<SentMessage> sent = s.drain();
    BOOST_REQUIRE(sent.size() == 1);
    BOOST_TEST(sent[0].code == NABTO_COAP_CODE_NOT_FOUND);
    BOOST_TEST(sent[0].messageId == 0x4001);
    BOOST_TEST(sent[0].token == "t1");

    s.handlePacket(second);
    sent = s.drain();
    BOOST_REQUIRE(sent.size() == 1);
    BOOST_TEST(sent[0].code == NABTO_COAP_CODE_NOT_FOUND);
    BOOST_TEST(sent[0].messageId == 0x4002);
    BOOST_TEST(sent[0].token == "t2");
    BOOST_TEST(s.handlerCalls == 0u);
}

// Same for the pending empty ACK: two CON requests before the event
// loop runs must not lose the first ACK.
BOOST_AUTO_TEST_CASE(pending_ack_is_not_overwritten)
{
    TestServer s;
    std::vector<uint8_t> first = RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_GET, 0x5001, "t1").build();
    std::vector<uint8_t> second = RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_GET, 0x5002, "t2").build();

    s.handlePacket(first);
    s.handlePacket(second);
    BOOST_TEST(s.handlerCalls == 2u);
    std::vector<SentMessage> sent = s.drain();
    BOOST_REQUIRE(sent.size() == 1);
    BOOST_TEST(sent[0].type == NABTO_COAP_TYPE_ACK);
    BOOST_TEST(sent[0].code == NABTO_COAP_CODE_EMPTY);
    BOOST_TEST(sent[0].messageId == 0x5001);

    // The second client retransmits and is acked without being processed again.
    s.handlePacket(second);
    sent = s.drain();
    BOOST_REQUIRE(sent.size() == 1);
    BOOST_TEST(sent[0].type == NABTO_COAP_TYPE_ACK);
    BOOST_TEST(sent[0].code == NABTO_COAP_CODE_EMPTY);
    BOOST_TEST(sent[0].messageId == 0x5002);
    BOOST_TEST(s.handlerCalls == 2u);

    BOOST_REQUIRE(s.pendingRequests.size() == 2);
    s.respond(s.pendingRequests[0], NABTO_COAP_CODE_CONTENT);
    s.respond(s.pendingRequests[1], NABTO_COAP_CODE_CONTENT);
    BOOST_TEST(s.requests.activeRequests == 0u);
}

// RFC 7252 section 5.2.1: the error response to a CON request is
// piggybacked in the ACK, carrying the request's message id and token.
BOOST_AUTO_TEST_CASE(error_to_con_request_is_piggybacked_ack)
{
    TestServer s;
    s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_GET, 0x6001, "t1", "nope").build());
    std::vector<SentMessage> sent = s.drain();
    BOOST_REQUIRE(sent.size() == 1);
    BOOST_TEST(sent[0].type == NABTO_COAP_TYPE_ACK);
    BOOST_TEST(sent[0].code == NABTO_COAP_CODE_NOT_FOUND);
    BOOST_TEST(sent[0].messageId == 0x6001);
    BOOST_TEST(sent[0].token == "t1");
    BOOST_TEST(sent[0].payload.empty());
}

// RFC 7252 section 5.2.3: the error response to a NON request is a
// NON with a fresh message id, matched by token.
BOOST_AUTO_TEST_CASE(error_to_non_request_is_non_with_fresh_message_id)
{
    TestServer s;
    s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_NON, NABTO_COAP_CODE_GET, 0x7001, "t1", "nope").build());
    std::vector<SentMessage> sent = s.drain();
    BOOST_REQUIRE(sent.size() == 1);
    BOOST_TEST(sent[0].type == NABTO_COAP_TYPE_NON);
    BOOST_TEST(sent[0].code == NABTO_COAP_CODE_NOT_FOUND);
    BOOST_TEST(sent[0].messageId != 0x7001);
    BOOST_TEST(sent[0].token == "t1");
}

// RFC 7252 section 5.4.1: an unrecognized critical option in a CON
// request gets 4.02 Bad Option; in a NON request it is rejected with
// a matching RST (section 4.3).
BOOST_AUTO_TEST_CASE(unknown_critical_option_gets_bad_option)
{
    TestServer s;
    s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_GET, 0x8001, "t1").uriQuery("a=b").build());
    std::vector<SentMessage> sent = s.drain();
    BOOST_REQUIRE(sent.size() == 1);
    BOOST_TEST(sent[0].type == NABTO_COAP_TYPE_ACK);
    BOOST_TEST(sent[0].code == NABTO_COAP_CODE_BAD_OPTION);
    BOOST_TEST(sent[0].messageId == 0x8001);
    BOOST_TEST(sent[0].token == "t1");
    BOOST_TEST(sent[0].payload == "Unsupported critical option");
    BOOST_TEST(s.handlerCalls == 0u);

    s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_NON, NABTO_COAP_CODE_GET, 0x8002, "t2").uriQuery("a=b").build());
    sent = s.drain();
    BOOST_REQUIRE(sent.size() == 1);
    BOOST_TEST(sent[0].type == NABTO_COAP_TYPE_RST);
    BOOST_TEST(sent[0].code == NABTO_COAP_CODE_EMPTY);
    BOOST_TEST(sent[0].messageId == 0x8002);
    BOOST_TEST(sent[0].token.empty());
    BOOST_TEST(sent[0].payload.empty());
    BOOST_TEST(s.handlerCalls == 0u);
}

// Same drop rule for the pending RST as for errors and ACKs.
BOOST_AUTO_TEST_CASE(pending_rst_is_not_overwritten)
{
    TestServer s;
    std::vector<uint8_t> first = RequestBuilder(NABTO_COAP_TYPE_NON, NABTO_COAP_CODE_GET, 0xa001, "t1").uriQuery("a=b").build();
    std::vector<uint8_t> second = RequestBuilder(NABTO_COAP_TYPE_NON, NABTO_COAP_CODE_GET, 0xa002, "t2").uriQuery("a=b").build();

    s.handlePacket(first);
    s.handlePacket(second);
    std::vector<SentMessage> sent = s.drain();
    BOOST_REQUIRE(sent.size() == 1);
    BOOST_TEST(sent[0].type == NABTO_COAP_TYPE_RST);
    BOOST_TEST(sent[0].messageId == 0xa001);

    s.handlePacket(second);
    sent = s.drain();
    BOOST_REQUIRE(sent.size() == 1);
    BOOST_TEST(sent[0].type == NABTO_COAP_TYPE_RST);
    BOOST_TEST(sent[0].messageId == 0xa002);
    BOOST_TEST(s.handlerCalls == 0u);
}

// A pending reply that does not fit the send buffer stays pending so
// the integrator can retry with a larger buffer.
BOOST_AUTO_TEST_CASE(pending_reply_is_kept_when_send_buffer_is_too_small)
{
    {
        TestServer s;
        s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_GET, 0xb001, "t1", "nope").build());
        BOOST_TEST(s.sendInto(2) == (uint8_t*)NULL);
        BOOST_TEST(nabto_coap_server_next_event(&s.requests) == NABTO_COAP_SERVER_NEXT_EVENT_SEND);
        std::vector<SentMessage> sent = s.drain();
        BOOST_REQUIRE(sent.size() == 1);
        BOOST_TEST(sent[0].code == NABTO_COAP_CODE_NOT_FOUND);
        BOOST_TEST(sent[0].messageId == 0xb001);
    }
    {
        TestServer s;
        s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_GET, 0xb002, "t1").build());
        BOOST_TEST(s.sendInto(2) == (uint8_t*)NULL);
        BOOST_TEST(nabto_coap_server_next_event(&s.requests) == NABTO_COAP_SERVER_NEXT_EVENT_SEND);
        std::vector<SentMessage> sent = s.drain();
        BOOST_REQUIRE(sent.size() == 1);
        BOOST_TEST(sent[0].type == NABTO_COAP_TYPE_ACK);
        BOOST_TEST(sent[0].code == NABTO_COAP_CODE_EMPTY);
        BOOST_TEST(sent[0].messageId == 0xb002);
        s.respondAndFinish(NABTO_COAP_CODE_CONTENT);
    }
    {
        TestServer s;
        s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_NON, NABTO_COAP_CODE_GET, 0xb003, "t1").uriQuery("a=b").build());
        BOOST_TEST(s.sendInto(2) == (uint8_t*)NULL);
        BOOST_TEST(nabto_coap_server_next_event(&s.requests) == NABTO_COAP_SERVER_NEXT_EVENT_SEND);
        std::vector<SentMessage> sent = s.drain();
        BOOST_REQUIRE(sent.size() == 1);
        BOOST_TEST(sent[0].type == NABTO_COAP_TYPE_RST);
        BOOST_TEST(sent[0].messageId == 0xb003);
    }
}

// An error without a description must not carry the payload of an
// earlier error.
BOOST_AUTO_TEST_CASE(error_without_description_has_no_stale_payload)
{
    TestServer s;
    s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_GET, 0x9001, "t1").uriQuery("a=b").build());
    std::vector<SentMessage> sent = s.drain();
    BOOST_REQUIRE(sent.size() == 1);
    BOOST_TEST(sent[0].payload == "Unsupported critical option");

    s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_GET, 0x9002, "t2", "nope").build());
    sent = s.drain();
    BOOST_REQUIRE(sent.size() == 1);
    BOOST_TEST(sent[0].code == NABTO_COAP_CODE_NOT_FOUND);
    BOOST_TEST(sent[0].payload.empty());
}

// Two CON responses whose retransmissions run out on the same tick
// must both be released; the tick must not stop at the first one.
BOOST_AUTO_TEST_CASE(timeout_tick_continues_after_a_response_expires)
{
    TestServer s;
    s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_GET, 0x7001, "t1").build());
    BOOST_TEST(s.drain().size() == 1u); // empty ACK
    s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_GET, 0x7002, "t2").build());
    BOOST_TEST(s.drain().size() == 1u); // empty ACK
    BOOST_REQUIRE(s.pendingRequests.size() == 2u);
    s.respondNoAck(s.pendingRequests[0], NABTO_COAP_CODE_CONTENT);
    s.respondNoAck(s.pendingRequests[1], NABTO_COAP_CODE_CONTENT);
    s.request = NULL;

    std::vector<SentMessage> sent = s.drain();
    BOOST_REQUIRE(sent.size() == 2u);
    BOOST_TEST(sent[0].type == NABTO_COAP_TYPE_CON);
    BOOST_TEST(sent[1].type == NABTO_COAP_TYPE_CON);

    // The client never ACKs; both responses are retransmitted together.
    for (int i = 0; i < NABTO_COAP_MAX_RETRANSMITS; i++) {
        s.timeoutTick();
        sent = s.drain();
        BOOST_REQUIRE(sent.size() == 2u);
        BOOST_TEST(sent[0].token + sent[1].token == "t2t1"); // newest request first
    }
    BOOST_TEST(s.requests.activeRequests == 2u);

    // Retransmissions exhausted: both are given up on in one tick.
    s.timeoutTick();
    BOOST_TEST(s.drain().empty());
    BOOST_TEST(s.requests.activeRequests == 0u);
}

// An observer notification due for retransmission is still handled on
// a tick where a response is given up on.
BOOST_AUTO_TEST_CASE(observer_timeout_is_handled_when_a_response_expires)
{
    TestServer s;

    // A response that will be given up on after the retransmissions.
    s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_GET, 0x7101, "t1").build());
    BOOST_TEST(s.drain().size() == 1u); // empty ACK
    s.respondNoAck(s.request, NABTO_COAP_CODE_CONTENT);
    s.request = NULL;
    BOOST_TEST(s.drain().size() == 1u);
    for (int i = 0; i < NABTO_COAP_MAX_RETRANSMITS; i++) {
        s.timeoutTick();
        BOOST_TEST(s.drain().size() == 1u);
    }

    // An observer with a CON notification in flight.
    s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_GET, 0x7102, "t2", "test", 0).build());
    BOOST_TEST(s.drain().size() == 1u); // empty ACK
    BOOST_REQUIRE(s.request != NULL);
    BOOST_REQUIRE(nabto_coap_server_request_accept_observe(s.request) == NABTO_COAP_ERROR_OK);
    s.respond(s.request, NABTO_COAP_CODE_CONTENT);
    s.request = NULL;
    BOOST_TEST(s.requests.activeRequests == 1u);
    const std::string notification = "hello";
    BOOST_REQUIRE(nabto_coap_server_resource_notify(&s.requests, s.getResource, NABTO_COAP_CODE_CONTENT, 0, notification.data(), notification.size()) == NABTO_COAP_ERROR_OK);
    std::vector<SentMessage> sent = s.drain();
    BOOST_REQUIRE(sent.size() == 1u);
    BOOST_TEST(sent[0].type == NABTO_COAP_TYPE_CON);
    BOOST_TEST(sent[0].token == "t2");
    BOOST_TEST(sent[0].payload == notification);
    uint16_t notificationId = sent[0].messageId;

    // The same tick releases the response and retransmits the notification.
    s.timeoutTick();
    BOOST_TEST(s.requests.activeRequests == 0u);
    sent = s.drain();
    BOOST_REQUIRE(sent.size() == 1u);
    BOOST_TEST(sent[0].type == NABTO_COAP_TYPE_CON);
    BOOST_TEST(sent[0].token == "t2");
    BOOST_TEST(sent[0].messageId == notificationId);
    BOOST_TEST(sent[0].payload == notification);
    s.handlePacket(ackPacket(notificationId));
}

// A request still with the application has no response timeout, so a
// timeout tick must leave it alone.
BOOST_AUTO_TEST_CASE(timeout_tick_ignores_request_owned_by_user)
{
    TestServer s;
    s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_GET, 0x7201, "t1").build());
    BOOST_TEST(s.drain().size() == 1u); // empty ACK
    BOOST_REQUIRE(s.request != NULL);

    s.timeoutTick();
    BOOST_TEST(nabto_coap_server_next_event(&s.requests) == NABTO_COAP_SERVER_NEXT_EVENT_NOTHING);
    BOOST_TEST(s.request->response.sendNow == false);
    BOOST_TEST(s.drain().empty());
    BOOST_TEST(s.requests.activeRequests == 1u);

    s.respondAndFinish(NABTO_COAP_CODE_CONTENT);
}

// Setting the payload a second time replaces the first one and
// releases its buffer.
BOOST_AUTO_TEST_CASE(set_payload_twice_replaces_first_payload)
{
    TestServer s;
    s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_GET, 0xc001, "t1").build());
    BOOST_TEST(s.drain().size() == 1u);
    BOOST_REQUIRE(s.request != NULL);

    BOOST_REQUIRE(nabto_coap_server_response_set_payload(s.request, "first", 5) == NABTO_COAP_ERROR_OK);
    BOOST_REQUIRE(nabto_coap_server_response_set_payload(s.request, "second", 6) == NABTO_COAP_ERROR_OK);
    nabto_coap_server_response_set_code(s.request, NABTO_COAP_CODE_CONTENT);
    BOOST_REQUIRE(nabto_coap_server_response_ready(s.request) == NABTO_COAP_ERROR_OK);
    nabto_coap_server_request_free(s.request);
    s.request = NULL;

    std::vector<SentMessage> sent = s.drain();
    BOOST_REQUIRE(sent.size() == 1);
    BOOST_TEST(sent[0].code == NABTO_COAP_CODE_CONTENT);
    BOOST_TEST(sent[0].payload == "second");
    s.handlePacket(ackPacket(sent[0].messageId));
    BOOST_TEST(s.requests.activeRequests == 0u);
}

// Freeing a request the handler never answered sends 5.00; a payload
// the handler had set is released, not leaked.
BOOST_AUTO_TEST_CASE(free_of_unanswered_request_releases_payload)
{
    TestServer s;
    s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_GET, 0xd001, "t1").build());
    BOOST_TEST(s.drain().size() == 1u);
    BOOST_REQUIRE(s.request != NULL);

    BOOST_REQUIRE(nabto_coap_server_response_set_payload(s.request, "unsent", 6) == NABTO_COAP_ERROR_OK);
    nabto_coap_server_request_free(s.request);
    s.request = NULL;

    std::vector<SentMessage> sent = s.drain();
    BOOST_REQUIRE(sent.size() == 1);
    BOOST_TEST(sent[0].code == NABTO_COAP_CODE_INTERNAL_SERVER_ERROR);
    BOOST_TEST(sent[0].payload == "Request unhandled");
    s.handlePacket(ackPacket(sent[0].messageId));
    BOOST_TEST(s.requests.activeRequests == 0u);
}

// A payload that is an exact multiple of the block size is complete
// once the last block is acked; the request must not linger and send
// an empty block on timeout.
BOOST_AUTO_TEST_CASE(block2_response_of_exact_block_multiple_completes_on_last_ack)
{
    TestServer s;
    const std::string body(1024, 'x'); // two 512 byte blocks
    s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_GET, 0xe001, "t1").build());
    BOOST_TEST(s.drain().size() == 1u);
    BOOST_REQUIRE(s.request != NULL);

    BOOST_REQUIRE(nabto_coap_server_response_set_payload(s.request, body.data(), body.size()) == NABTO_COAP_ERROR_OK);
    nabto_coap_server_response_set_code(s.request, NABTO_COAP_CODE_CONTENT);
    BOOST_REQUIRE(nabto_coap_server_response_ready(s.request) == NABTO_COAP_ERROR_OK);
    nabto_coap_server_request_free(s.request);
    s.request = NULL;

    std::vector<SentMessage> sent = s.drain();
    BOOST_REQUIRE(sent.size() == 1);
    BOOST_TEST(sent[0].code == NABTO_COAP_CODE_CONTENT);
    BOOST_TEST(sent[0].hasBlock2);
    BOOST_TEST(sent[0].block2 == blockOption(0, true, 5));
    BOOST_TEST(sent[0].payload == body.substr(0, 512));
    s.handlePacket(ackPacket(sent[0].messageId));

    SentMessage block = s.requestBlock2("t1", 0xe002, 1, 5);
    BOOST_TEST(block.block2 == blockOption(1, false, 5));
    BOOST_TEST(block.payload == body.substr(512));
    s.handlePacket(ackPacket(block.messageId));
    BOOST_TEST(s.requests.activeRequests == 0u);

    s.now += NABTO_COAP_ACK_TIMEOUT;
    nabto_coap_server_handle_timeout(&s.requests);
    BOOST_TEST(s.drain().empty());
}

// Audit N5 (sc-4862): RFC 7252 section 4.2, a CON request for a further
// Block2 block is a CON of its own and must be acknowledged; the server
// only sent the CON block. An RFC client kept retransmitting the
// request, and each retransmit set the block up again with a fresh
// message id and a reset retransmission count. A retransmit is now a
// duplicate (section 4.5): acked again, but the block is set up once.
BOOST_AUTO_TEST_CASE(con_request_for_next_block2_block_is_acked)
{
    TestServer s;
    const std::string body(1024, 'x'); // two 512 byte blocks
    s.startBlock2Response("t1", body, 0xe101);

    SentMessage block = s.requestBlock2("t1", 0xe102, 1, 5);
    BOOST_TEST(block.block2 == blockOption(1, false, 5));
    BOOST_TEST(block.payload == body.substr(512));

    // The client did not get the ACK and retransmits the block request.
    s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_GET, 0xe102, "t1").block2(1, 5).build());
    std::vector<SentMessage> sent = s.drain();
    BOOST_REQUIRE(sent.size() == 1);
    BOOST_TEST(sent[0].type == NABTO_COAP_TYPE_ACK);
    BOOST_TEST(sent[0].code == NABTO_COAP_CODE_EMPTY);
    BOOST_TEST(sent[0].messageId == 0xe102);

    // The block in flight is still the one sent first.
    s.handlePacket(ackPacket(block.messageId));
    BOOST_TEST(s.requests.activeRequests == 0u);
}

// Audit N11 (sc-4869): sendNow and retransmissions could not tell "sent,
// waiting for ACK" from "ACKed, waiting for the next block request", so a
// duplicate ACK matched the already acknowledged block again, advanced
// block2Current a second time and ended the response early. Duplicate ACKs
// are ordinary: a client acks every copy of a CON it receives.
BOOST_AUTO_TEST_CASE(duplicate_ack_does_not_advance_block2_response)
{
    TestServer s;
    const std::string body(1024, 'x'); // two 512 byte blocks
    s.startBlock2Response("t1", body, 0xe301);

    // The ACK for block 0 arrives twice.
    s.handlePacket(ackPacket(s.requests.messageId));
    BOOST_TEST(s.requests.activeRequests == 1u);
    BOOST_TEST(s.handlerCalls == 1u);
    BOOST_TEST(s.drain().empty());

    // Block 1 is still there to be fetched, from the same request.
    SentMessage block = s.requestBlock2("t1", 0xe302, 1, 5);
    BOOST_TEST(block.block2 == blockOption(1, false, 5));
    BOOST_TEST(block.payload == body.substr(512));
    BOOST_TEST(s.handlerCalls == 1u);
    s.handlePacket(ackPacket(block.messageId));
    BOOST_TEST(s.requests.activeRequests == 0u);
}

// Once a block has been acknowledged the server waits for the client to
// ask for the next one. The retransmission deadline must not fire in the
// meantime: retransmitting the acked block is pointless and pushing the
// next block unsolicited is not how RFC 7959 drives a Block2 transfer.
BOOST_AUTO_TEST_CASE(acked_block2_block_is_not_retransmitted)
{
    TestServer s;
    const std::string body(1024, 'x');
    s.startBlock2Response("t1", body, 0xe311);

    s.now += NABTO_COAP_ACK_TIMEOUT << NABTO_COAP_MAX_RETRANSMITS;
    nabto_coap_server_handle_timeout(&s.requests);
    BOOST_TEST(s.drain().empty());
    BOOST_TEST(s.requests.activeRequests == 1u);

    SentMessage block = s.requestBlock2("t1", 0xe312, 1, 5);
    BOOST_TEST(block.payload == body.substr(512));
    s.handlePacket(ackPacket(block.messageId));
    BOOST_TEST(s.requests.activeRequests == 0u);
}

// A client that acks a block and then never asks for the next one would
// otherwise hold the request for the life of the connection, so the
// acked-and-quiet state gets the same 64 s deadline as a Block1 transfer
// being received (sc-4860).
BOOST_AUTO_TEST_CASE(acked_block2_transfer_that_goes_quiet_is_reaped)
{
    const uint32_t transferTimeout = NABTO_COAP_ACK_TIMEOUT << (NABTO_COAP_MAX_RETRANSMITS + 1);
    TestServer s;
    const std::string body(1024, 'x');
    s.startBlock2Response("t1", body, 0xe321);

    uint32_t nextTimeout = 0;
    BOOST_TEST(nabto_coap_server_get_next_timeout(&s.requests, &nextTimeout));
    BOOST_TEST(nextTimeout == s.now + transferTimeout);

    // Just before the deadline the transfer is kept.
    s.now += transferTimeout - 1;
    nabto_coap_server_handle_timeout(&s.requests);
    BOOST_TEST(s.requests.activeRequests == 1u);
    BOOST_TEST(s.drain().empty());

    // At the deadline it is discarded, with nothing sent.
    s.now += 1;
    nabto_coap_server_handle_timeout(&s.requests);
    BOOST_TEST(s.requests.activeRequests == 0u);
    BOOST_TEST(s.handlerCalls == 1u);
    BOOST_TEST(s.drain().empty());
}

// The N1 property (sc-4858) inside the Block2 flow: handle_data_for_response
// assigns a fresh message id for the next block, and that id must not be
// matchable until the block carrying it has actually been sent. The ids are
// a predictable counter, so a client can name the next one.
BOOST_AUTO_TEST_CASE(ack_for_unsent_block2_message_id_is_ignored)
{
    TestServer s;
    const std::string body(1024, 'x');
    s.startBlock2Response("t1", body, 0xe331);

    // Set block 1 up but do not let the server send it yet.
    s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_GET, 0xe332, "t1").block2(1, 5).build());
    uint16_t unsent = s.requests.messageId;
    s.handlePacket(ackPacket(unsent));
    BOOST_TEST(s.requests.activeRequests == 1u);

    // The block is still queued and still carries the whole tail.
    std::vector<SentMessage> sent = s.drain();
    BOOST_REQUIRE(sent.size() == 2);
    BOOST_TEST(sent[1].messageId == unsent);
    BOOST_TEST(sent[1].block2 == blockOption(1, false, 5));
    BOOST_TEST(sent[1].payload == body.substr(512));
    s.handlePacket(ackPacket(unsent));
    BOOST_TEST(s.requests.activeRequests == 0u);
}

// A NON response is never retransmitted, but it has been sent, so RFC 7252
// section 4.3 lets the peer reject it with a RST and that must end the
// request rather than leave it for the expiry tick.
BOOST_AUTO_TEST_CASE(rst_for_sent_non_response_ends_the_request)
{
    TestServer s;
    s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_NON, NABTO_COAP_CODE_GET, 0xe341, "t1").build());
    BOOST_REQUIRE(s.request != NULL);
    s.respondNoAck(s.request, NABTO_COAP_CODE_CONTENT);
    s.request = NULL;

    std::vector<SentMessage> sent = s.drain();
    BOOST_REQUIRE(sent.size() == 1);
    BOOST_TEST(sent[0].type == NABTO_COAP_TYPE_NON);
    BOOST_TEST(s.requests.activeRequests == 1u);

    s.handlePacket(rstPacket(sent[0].messageId));
    BOOST_TEST(s.requests.activeRequests == 0u);
}


// Audit N6 (sc-4863): RFC 7959 section 2.4 lets a client state the block
// size it wants, and the block it wants, on the request itself ("early
// negotiation"), and "A server MUST use the block size indicated or a
// smaller size." The option was parsed for its reserved-SZX check and
// then discarded: the response always started at block 0 with 512 byte
// blocks, so a client that negotiates early, or that uses a fresh token
// per block, could never complete the transfer.
BOOST_AUTO_TEST_CASE(block2_on_the_first_request_is_honoured)
{
    const std::string body(2048, 'x');

    { // A smaller block size is adopted for the whole response.
        TestServer s;
        s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_GET, 0xe401, "t1").block2(0, 2).build());
        BOOST_TEST(s.drain().size() == 1u); // empty ACK
        BOOST_REQUIRE(s.request != NULL);
        BOOST_REQUIRE(nabto_coap_server_response_set_payload(s.request, body.data(), body.size()) == NABTO_COAP_ERROR_OK);
        nabto_coap_server_response_set_code(s.request, NABTO_COAP_CODE_CONTENT);
        BOOST_REQUIRE(nabto_coap_server_response_ready(s.request) == NABTO_COAP_ERROR_OK);
        nabto_coap_server_request_free(s.request);
        s.request = NULL;

        std::vector<SentMessage> sent = s.drain();
        BOOST_REQUIRE(sent.size() == 1);
        BOOST_TEST(sent[0].code == NABTO_COAP_CODE_CONTENT);
        BOOST_TEST(sent[0].block2 == blockOption(0, true, 2));
        BOOST_TEST(sent[0].payload == body.substr(0, 64));
        s.handlePacket(ackPacket(sent[0].messageId));

        SentMessage block = s.requestBlock2("t1", 0xe402, 1, 2);
        BOOST_TEST(block.block2 == blockOption(1, true, 2));
        BOOST_TEST(block.payload == body.substr(64, 64));
        s.handlePacket(rstPacket(block.messageId));
        BOOST_TEST(s.requests.activeRequests == 0u);
    }

    { // A larger block size than our own is not adopted, and the block
      // number is converted into the size we do use.
        TestServer s;
        s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_GET, 0xe411, "t1").block2(1, 6).build());
        BOOST_TEST(s.drain().size() == 1u);
        BOOST_REQUIRE(s.request != NULL);
        BOOST_REQUIRE(nabto_coap_server_response_set_payload(s.request, body.data(), body.size()) == NABTO_COAP_ERROR_OK);
        nabto_coap_server_response_set_code(s.request, NABTO_COAP_CODE_CONTENT);
        BOOST_REQUIRE(nabto_coap_server_response_ready(s.request) == NABTO_COAP_ERROR_OK);
        nabto_coap_server_request_free(s.request);
        s.request = NULL;

        std::vector<SentMessage> sent = s.drain();
        BOOST_REQUIRE(sent.size() == 1);
        // Byte offset 1024 in 512 byte blocks is block 2, not block 1.
        BOOST_TEST(sent[0].block2 == blockOption(2, true, 5));
        BOOST_TEST(sent[0].payload == body.substr(1024, 512));
        s.handlePacket(rstPacket(sent[0].messageId));
        BOOST_TEST(s.requests.activeRequests == 0u);
    }

    { // A fresh token asking for a later block is served that block.
        TestServer s;
        s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_GET, 0xe421, "t9").block2(2, 5).build());
        BOOST_TEST(s.drain().size() == 1u);
        BOOST_REQUIRE(s.request != NULL);
        BOOST_REQUIRE(nabto_coap_server_response_set_payload(s.request, body.data(), body.size()) == NABTO_COAP_ERROR_OK);
        nabto_coap_server_response_set_code(s.request, NABTO_COAP_CODE_CONTENT);
        BOOST_REQUIRE(nabto_coap_server_response_ready(s.request) == NABTO_COAP_ERROR_OK);
        nabto_coap_server_request_free(s.request);
        s.request = NULL;

        std::vector<SentMessage> sent = s.drain();
        BOOST_REQUIRE(sent.size() == 1);
        BOOST_TEST(sent[0].block2 == blockOption(2, true, 5));
        BOOST_TEST(sent[0].payload == body.substr(1024, 512));
        s.handlePacket(rstPacket(sent[0].messageId));
        BOOST_TEST(s.requests.activeRequests == 0u);
    }
}

// A block at or past the end of the body is a 4.00, the same answer the
// later-block path gives (audit H1), and the application's payload is
// released rather than sent from the wrong offset.
BOOST_AUTO_TEST_CASE(block2_on_the_first_request_past_the_end_gets_bad_request)
{
    TestServer s;
    const std::string body(600, 'x');
    s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_GET, 0xe431, "t1").block2(2, 5).build());
    BOOST_TEST(s.drain().size() == 1u);
    BOOST_REQUIRE(s.request != NULL);
    BOOST_REQUIRE(nabto_coap_server_response_set_payload(s.request, body.data(), body.size()) == NABTO_COAP_ERROR_OK);
    nabto_coap_server_response_set_code(s.request, NABTO_COAP_CODE_CONTENT);
    BOOST_REQUIRE(nabto_coap_server_response_ready(s.request) == NABTO_COAP_ERROR_OK);
    nabto_coap_server_request_free(s.request);
    s.request = NULL;

    std::vector<SentMessage> sent = s.drain();
    BOOST_REQUIRE(sent.size() == 1);
    BOOST_TEST(sent[0].code == NABTO_COAP_CODE_BAD_REQUEST);
    BOOST_TEST(sent[0].payload == "Bad block option");
    BOOST_TEST(!sent[0].hasBlock2);
    s.handlePacket(ackPacket(sent[0].messageId));
    BOOST_TEST(s.requests.activeRequests == 0u);
}

// A request the application never answers is completed with a synthetic
// 5.00, which is not an answer to the client's block request: the block
// negotiation must not turn it into a 4.00 about a body it never had.
BOOST_AUTO_TEST_CASE(unanswered_request_with_block2_still_gets_internal_error)
{
    TestServer s;
    s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_GET, 0xe441, "t1").block2(3, 5).build());
    BOOST_TEST(s.drain().size() == 1u);
    BOOST_REQUIRE(s.request != NULL);
    nabto_coap_server_request_free(s.request);
    s.request = NULL;

    std::vector<SentMessage> sent = s.drain();
    BOOST_REQUIRE(sent.size() == 1);
    BOOST_TEST(sent[0].code == NABTO_COAP_CODE_INTERNAL_SERVER_ERROR);
    BOOST_TEST(sent[0].payload == "Request unhandled");
    s.handlePacket(ackPacket(sent[0].messageId));
    BOOST_TEST(s.requests.activeRequests == 0u);
}

// Audit H1 (sc-4811): the block number of a Block2 request was used as
// an offset into the response payload without a bounds check, so a
// client could make the server send heap memory past the payload. RFC
// 7959 has no code for a block past the end; 4.00 Bad Request is the
// code it prescribes for the sibling case of a reserved block size, and
// the error ends the exchange.
BOOST_AUTO_TEST_CASE(block2_request_past_end_of_payload_gets_bad_request)
{
    // Block 2 starts exactly at the end of the payload, block 3 one
    // block past it.
    for (uint32_t num = 2; num <= 3; num++) {
        TestServer s;
        const std::string body(1024, 'x'); // two 512 byte blocks
        s.startBlock2Response("t1", body, 0xf001);
        BOOST_TEST(s.requests.activeRequests == 1u);

        s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_GET, 0xf002, "t1").block2(num, 5).build());
        std::vector<SentMessage> sent = s.drain();
        BOOST_REQUIRE(sent.size() == 1);
        BOOST_TEST(sent[0].type == NABTO_COAP_TYPE_ACK);
        BOOST_TEST(sent[0].code == NABTO_COAP_CODE_BAD_REQUEST);
        BOOST_TEST(sent[0].messageId == 0xf002);
        BOOST_TEST(sent[0].token == "t1");
        BOOST_TEST(sent[0].payload == "Bad block option");
        BOOST_TEST(s.requests.activeRequests == 0u);
    }
    {
        // The largest offset a client can ask for, as a NON.
        TestServer s;
        const std::string body(1024, 'x');
        s.startBlock2Response("t1", body, 0xf003);

        s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_NON, NABTO_COAP_CODE_GET, 0xf004, "t1").block2(0xFFFFF, 6).build());
        std::vector<SentMessage> sent = s.drain();
        BOOST_REQUIRE(sent.size() == 1);
        BOOST_TEST(sent[0].type == NABTO_COAP_TYPE_NON);
        BOOST_TEST(sent[0].code == NABTO_COAP_CODE_BAD_REQUEST);
        BOOST_TEST(sent[0].messageId != 0xf004);
        BOOST_TEST(sent[0].token == "t1");
        BOOST_TEST(s.requests.activeRequests == 0u);
    }
}

// A response without a payload has nothing at block 1; the server must
// not read from a NULL payload pointer plus an offset. Such a response
// completes on its ACK, so the block request arrives while it is in
// flight.
BOOST_AUTO_TEST_CASE(block2_request_for_response_without_payload_gets_bad_request)
{
    TestServer s;
    s.startBlock2Response("t1", "", 0xf101, false);
    BOOST_TEST(s.requests.activeRequests == 1u);

    s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_GET, 0xf102, "t1").block2(1, 5).build());
    std::vector<SentMessage> sent = s.drain();
    BOOST_REQUIRE(sent.size() == 1);
    BOOST_TEST(sent[0].type == NABTO_COAP_TYPE_ACK);
    BOOST_TEST(sent[0].code == NABTO_COAP_CODE_BAD_REQUEST);
    BOOST_TEST(sent[0].messageId == 0xf102);
    BOOST_TEST(s.requests.activeRequests == 0u);
}

// RFC 7959 section 2.2: SZX 7 is reserved, MUST NOT be sent and MUST
// lead to 4.00 Bad Request upon reception in a request. This covers a
// Block2 request for the next block, a Block1 chunk, and a Block2 in a
// new request suggesting a response block size.
BOOST_AUTO_TEST_CASE(reserved_block_size_gets_bad_request)
{
    {
        TestServer s;
        const std::string body(1024, 'x');
        s.startBlock2Response("t1", body, 0xf201);

        s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_GET, 0xf202, "t1").block2(0, 7).build());
        std::vector<SentMessage> sent = s.drain();
        BOOST_REQUIRE(sent.size() == 1);
        BOOST_TEST(sent[0].type == NABTO_COAP_TYPE_ACK);
        BOOST_TEST(sent[0].code == NABTO_COAP_CODE_BAD_REQUEST);
        BOOST_TEST(sent[0].messageId == 0xf202);
        BOOST_TEST(sent[0].token == "t1");
        BOOST_TEST(s.requests.activeRequests == 0u);
    }
    {
        TestServer s;
        const std::string chunk(16, 'a');
        s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_POST, 0xf203, "t2").block1(0, true, 7).payload(chunk).build());
        std::vector<SentMessage> sent = s.drain();
        BOOST_REQUIRE(sent.size() == 1);
        BOOST_TEST(sent[0].type == NABTO_COAP_TYPE_ACK);
        BOOST_TEST(sent[0].code == NABTO_COAP_CODE_BAD_REQUEST);
        BOOST_TEST(sent[0].messageId == 0xf203);
        BOOST_TEST(sent[0].token == "t2");
        BOOST_TEST(s.handlerCalls == 0u);
        BOOST_TEST(s.requests.activeRequests == 0u);
    }
    {
        TestServer s;
        s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_GET, 0xf204, "t3").block2(0, 7).build());
        std::vector<SentMessage> sent = s.drain();
        BOOST_REQUIRE(sent.size() == 1);
        BOOST_TEST(sent[0].type == NABTO_COAP_TYPE_ACK);
        BOOST_TEST(sent[0].code == NABTO_COAP_CODE_BAD_REQUEST);
        BOOST_TEST(sent[0].messageId == 0xf204);
        BOOST_TEST(sent[0].token == "t3");
        BOOST_TEST(s.handlerCalls == 0u);
        BOOST_TEST(s.requests.activeRequests == 0u);
    }
}

// RFC 7959 section 2.4: the client may switch to a smaller block size
// for later blocks; the block number is then in units of the new size.
// The offset check must keep serving such requests.
BOOST_AUTO_TEST_CASE(block2_request_with_smaller_block_size_is_served)
{
    TestServer s;
    std::string body;
    for (int i = 0; i < 1024; i++) {
        body.push_back((char)('a' + (i / 256)));
    }
    s.startBlock2Response("t1", body, 0xf301);

    // Block 1 of 256 bytes is the second half of the first 512 byte block.
    SentMessage block = s.requestBlock2("t1", 0xf302, 1, 4);
    BOOST_TEST(block.block2 == blockOption(1, true, 4));
    BOOST_TEST(block.payload == body.substr(256, 256));
    s.handlePacket(ackPacket(block.messageId));
    BOOST_TEST(s.requests.activeRequests == 1u);

    // The last 256 byte block completes the exchange.
    block = s.requestBlock2("t1", 0xf303, 3, 4);
    BOOST_TEST(block.block2 == blockOption(3, false, 4));
    BOOST_TEST(block.payload == body.substr(768));
    s.handlePacket(ackPacket(block.messageId));
    BOOST_TEST(s.requests.activeRequests == 0u);
}

// Audit H3 (sc-4813): the body of a Block1 request was reassembled
// without any bound, so a client could grow it a chunk at a time until
// the device ran out of memory, before the handler ever saw the request.
// RFC 7959 section 2.9.3: 4.13 Request Entity Too Large "can be returned
// at any time by a server that does not currently have the resources to
// store blocks for a block-wise request payload transfer".
BOOST_AUTO_TEST_CASE(request_body_over_the_limit_gets_request_entity_too_large)
{
    const std::string chunk(16, 'a');
    {
        // Two chunks fit in 40 bytes, the third does not.
        TestServer s;
        nabto_coap_server_limit_request_size(&s.requests, 40);
        for (uint32_t num = 0; num < 2; num++) {
            s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_POST, (uint16_t)(0xf400 + num), "t1").block1(num, true, 0).payload(chunk).build());
            std::vector<SentMessage> sent = s.drain();
            BOOST_REQUIRE(sent.size() == 1);
            BOOST_TEST(sent[0].code == NABTO_COAP_CODE_CONTINUE);
            BOOST_TEST(sent[0].hasBlock1);
            BOOST_TEST(sent[0].block1 == blockOption(num, true, 0));
        }
        BOOST_TEST(s.requests.activeRequests == 1u);

        s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_POST, 0xf402, "t1").block1(2, true, 0).payload(chunk).build());
        std::vector<SentMessage> sent = s.drain();
        BOOST_REQUIRE(sent.size() == 1);
        BOOST_TEST(sent[0].type == NABTO_COAP_TYPE_ACK);
        BOOST_TEST(sent[0].code == NABTO_COAP_CODE_REQUEST_ENTITY_TOO_LARGE);
        BOOST_TEST(sent[0].messageId == 0xf402);
        BOOST_TEST(sent[0].token == "t1");
        BOOST_TEST(sent[0].payload == "Request entity too large");
        BOOST_TEST(s.handlerCalls == 0u);
        BOOST_TEST(s.requests.activeRequests == 0u);
    }
    {
        // A body of exactly the limit is accepted.
        TestServer s;
        nabto_coap_server_limit_request_size(&s.requests, 32);
        s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_POST, 0xf403, "t2").block1(0, true, 0).payload(chunk).build());
        std::vector<SentMessage> sent = s.drain();
        BOOST_REQUIRE(sent.size() == 1);
        BOOST_TEST(sent[0].code == NABTO_COAP_CODE_CONTINUE);

        s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_POST, 0xf404, "t2").block1(1, false, 0).payload(chunk).build());
        sent = s.drain();
        BOOST_REQUIRE(sent.size() == 1);
        BOOST_TEST(sent[0].type == NABTO_COAP_TYPE_ACK);
        BOOST_TEST(sent[0].code == NABTO_COAP_CODE_EMPTY);
        BOOST_REQUIRE(s.handlerCalls == 1u);
        void* payload;
        size_t payloadLength;
        BOOST_TEST(nabto_coap_server_request_get_payload(s.request, &payload, &payloadLength));
        BOOST_TEST(payloadLength == 32u);

        s.respondAndFinish(NABTO_COAP_CODE_CHANGED);
    }
    {
        // The limit also applies to a body sent in a single packet.
        TestServer s;
        nabto_coap_server_limit_request_size(&s.requests, 8);
        s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_POST, 0xf405, "t3").payload(chunk).build());
        std::vector<SentMessage> sent = s.drain();
        BOOST_REQUIRE(sent.size() == 1);
        BOOST_TEST(sent[0].type == NABTO_COAP_TYPE_ACK);
        BOOST_TEST(sent[0].code == NABTO_COAP_CODE_REQUEST_ENTITY_TOO_LARGE);
        BOOST_TEST(sent[0].messageId == 0xf405);
        BOOST_TEST(sent[0].token == "t3");
        BOOST_TEST(s.handlerCalls == 0u);
        BOOST_TEST(s.requests.activeRequests == 0u);
    }
    {
        // A NON chunk over the limit gets the error as a NON with its
        // own message id.
        TestServer s;
        nabto_coap_server_limit_request_size(&s.requests, 8);
        s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_NON, NABTO_COAP_CODE_POST, 0xf406, "t4").block1(0, true, 0).payload(chunk).build());
        std::vector<SentMessage> sent = s.drain();
        BOOST_REQUIRE(sent.size() == 1);
        BOOST_TEST(sent[0].type == NABTO_COAP_TYPE_NON);
        BOOST_TEST(sent[0].code == NABTO_COAP_CODE_REQUEST_ENTITY_TOO_LARGE);
        BOOST_TEST(sent[0].messageId != 0xf406);
        BOOST_TEST(sent[0].token == "t4");
        BOOST_TEST(s.handlerCalls == 0u);
        BOOST_TEST(s.requests.activeRequests == 0u);
    }
}

// Audit N3 (sc-4860): a request being received had no deadline, so a
// client that sent the first Block1 chunk with the more bit set and
// then went quiet held the request, its parameters and the partial body
// until its connection was removed. The transfer is now discarded when
// no chunk has been heard for 64 s (RFC 7959 section 2.5 lets the
// server discard partial state at any time); a late chunk gets 4.08
// Request Entity Incomplete from the offset check.
BOOST_AUTO_TEST_CASE(block1_transfer_in_progress_times_out)
{
    const uint32_t transferTimeout = NABTO_COAP_ACK_TIMEOUT << (NABTO_COAP_MAX_RETRANSMITS + 1);
    const std::string chunk(16, 'a');
    TestServer s;
    s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_POST, 0xf500, "t1").block1(0, true, 0).payload(chunk).build());
    std::vector<SentMessage> sent = s.drain();
    BOOST_REQUIRE(sent.size() == 1);
    BOOST_TEST(sent[0].code == NABTO_COAP_CODE_CONTINUE);
    BOOST_TEST(s.requests.activeRequests == 1u);

    // The event loop is told to wait for the deadline.
    BOOST_TEST(nabto_coap_server_next_event(&s.requests) == NABTO_COAP_SERVER_NEXT_EVENT_WAIT);
    uint32_t nextTimeout = 0;
    BOOST_TEST(nabto_coap_server_get_next_timeout(&s.requests, &nextTimeout));
    BOOST_TEST(nextTimeout == s.now + transferTimeout);

    // Just before the deadline the transfer is kept.
    s.now += transferTimeout - 1;
    nabto_coap_server_handle_timeout(&s.requests);
    BOOST_TEST(s.requests.activeRequests == 1u);

    // At the deadline it is discarded without the handler ever seeing it.
    s.now += 1;
    nabto_coap_server_handle_timeout(&s.requests);
    BOOST_TEST(s.requests.activeRequests == 0u);
    BOOST_TEST(s.handlerCalls == 0u);
    BOOST_TEST(s.drain().empty());
    BOOST_TEST(nabto_coap_server_next_event(&s.requests) == NABTO_COAP_SERVER_NEXT_EVENT_NOTHING);
    BOOST_TEST(!nabto_coap_server_get_next_timeout(&s.requests, &nextTimeout));

    // A late chunk starts over and is rejected for the gap.
    s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_POST, 0xf501, "t1").block1(1, true, 0).payload(chunk).build());
    sent = s.drain();
    BOOST_REQUIRE(sent.size() == 1);
    BOOST_TEST(sent[0].type == NABTO_COAP_TYPE_ACK);
    BOOST_TEST(sent[0].code == NABTO_COAP_CODE_REQUEST_ENTITY_INCOMPLETE);
    BOOST_TEST(sent[0].messageId == 0xf501);
    BOOST_TEST(sent[0].token == "t1");
    BOOST_TEST(s.handlerCalls == 0u);
    BOOST_TEST(s.requests.activeRequests == 0u);
}

// Each chunk heard on a Block1 transfer moves its deadline, a
// retransmitted chunk included: a client whose 2.31 Continue was lost
// retransmits the same chunk with back-off for longer than the deadline
// and must not lose the transfer for it.
BOOST_AUTO_TEST_CASE(block1_chunk_refreshes_the_transfer_deadline)
{
    const uint32_t transferTimeout = NABTO_COAP_ACK_TIMEOUT << (NABTO_COAP_MAX_RETRANSMITS + 1);
    const std::string chunk(16, 'a');
    TestServer s;
    s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_POST, 0xf510, "t1").block1(0, true, 0).payload(chunk).build());
    std::vector<SentMessage> sent = s.drain();
    BOOST_REQUIRE(sent.size() == 1);
    BOOST_TEST(sent[0].code == NABTO_COAP_CODE_CONTINUE);

    // The next chunk arrives well within the deadline and moves it.
    s.now += transferTimeout / 2;
    s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_POST, 0xf511, "t1").block1(1, true, 0).payload(chunk).build());
    sent = s.drain();
    BOOST_REQUIRE(sent.size() == 1);
    BOOST_TEST(sent[0].code == NABTO_COAP_CODE_CONTINUE);
    BOOST_TEST(sent[0].block1 == blockOption(1, true, 0));
    uint32_t nextTimeout = 0;
    BOOST_TEST(nabto_coap_server_get_next_timeout(&s.requests, &nextTimeout));
    BOOST_TEST(nextTimeout == s.now + transferTimeout);

    // Past the deadline of chunk 0, within that of chunk 1.
    s.now += transferTimeout / 2 + 1;
    nabto_coap_server_handle_timeout(&s.requests);
    BOOST_TEST(s.requests.activeRequests == 1u);

    // A retransmit of chunk 1 gets its 2.31 again and moves the deadline too.
    s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_POST, 0xf511, "t1").block1(1, true, 0).payload(chunk).build());
    sent = s.drain();
    BOOST_REQUIRE(sent.size() == 1);
    BOOST_TEST(sent[0].code == NABTO_COAP_CODE_CONTINUE);
    BOOST_TEST(sent[0].messageId == 0xf511);
    BOOST_TEST(nabto_coap_server_get_next_timeout(&s.requests, &nextTimeout));
    BOOST_TEST(nextTimeout == s.now + transferTimeout);
    s.now += transferTimeout - 1;
    nabto_coap_server_handle_timeout(&s.requests);
    BOOST_TEST(s.requests.activeRequests == 1u);

    // The last chunk completes the transfer, which then has no deadline
    // while the handler holds it.
    s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_POST, 0xf512, "t1").block1(2, false, 0).payload(chunk).build());
    sent = s.drain();
    BOOST_REQUIRE(sent.size() == 1);
    BOOST_TEST(sent[0].type == NABTO_COAP_TYPE_ACK);
    BOOST_TEST(sent[0].code == NABTO_COAP_CODE_EMPTY);
    BOOST_REQUIRE(s.handlerCalls == 1u);
    void* payload;
    size_t payloadLength;
    BOOST_TEST(nabto_coap_server_request_get_payload(s.request, &payload, &payloadLength));
    BOOST_TEST(payloadLength == 48u);
    s.now += 2 * transferTimeout;
    nabto_coap_server_handle_timeout(&s.requests);
    BOOST_TEST(s.requests.activeRequests == 1u);
    BOOST_TEST(nabto_coap_server_next_event(&s.requests) == NABTO_COAP_SERVER_NEXT_EVENT_NOTHING);

    s.respondAndFinish(NABTO_COAP_CODE_CHANGED);
}

// Audit H2 (sc-4812): an observer's message id is 0 until the first
// notification, and a RST used to be matched against it regardless, so
// a RST with message id 0 freed a freshly registered observer while the
// request that registered it was still retransmitting its initial
// response with a pointer to it. A RST only matches a notification in
// flight (RFC 7252 section 4.2); the observer survives and the
// retransmit still carries the Observe option.
BOOST_AUTO_TEST_CASE(rst_before_any_notification_keeps_observer)
{
    TestServer s;
    s.registerObserver("t1", 0x1001);
    s.respondNoAck(s.request, NABTO_COAP_CODE_CONTENT);
    s.request = NULL;
    std::vector<SentMessage> sent = s.drain();
    BOOST_REQUIRE(sent.size() == 1);
    BOOST_TEST(sent[0].type == NABTO_COAP_TYPE_CON);
    BOOST_TEST(sent[0].code == NABTO_COAP_CODE_CONTENT);
    BOOST_TEST(sent[0].hasObserve);
    BOOST_TEST(sent[0].observe == 0u);

    s.handlePacket(rstPacket(0));
    BOOST_TEST(s.observerCount() == 1u);

    // The client did not ACK the response; the retransmit reads the
    // observer.
    s.timeoutTick();
    sent = s.drain();
    BOOST_REQUIRE(sent.size() == 1);
    BOOST_TEST(sent[0].type == NABTO_COAP_TYPE_CON);
    BOOST_TEST(sent[0].code == NABTO_COAP_CODE_CONTENT);
    BOOST_TEST(sent[0].token == "t1");
    BOOST_TEST(sent[0].hasObserve);
    BOOST_TEST(sent[0].observe == 0u);
    s.handlePacket(ackPacket(sent[0].messageId));
    BOOST_TEST(s.requests.activeRequests == 0u);
    BOOST_TEST(s.observerCount() == 1u);
}

// Audit H2 (sc-4812): the observer is removed while the initial
// response is still in flight, by the client rejecting a notification
// with a RST (its normal way to deregister) or by the application. The
// request must drop its pointer to the observer: the handle it can get
// is NULL and the retransmit goes out without an Observe option.
BOOST_AUTO_TEST_CASE(observer_removed_while_initial_response_in_flight)
{
    for (int byApplication = 0; byApplication <= 1; byApplication++) {
        TestServer s;
        s.registerObserver("t1", 0x1101);
        // The application answers but keeps the request.
        nabto_coap_server_response_set_code(s.request, NABTO_COAP_CODE_CONTENT);
        BOOST_REQUIRE(nabto_coap_server_response_ready(s.request) == NABTO_COAP_ERROR_OK);
        std::vector<SentMessage> sent = s.drain();
        BOOST_REQUIRE(sent.size() == 1);
        BOOST_TEST(sent[0].hasObserve);
        BOOST_TEST(sent[0].observe == 0u);

        const std::string notification = "hello";
        BOOST_REQUIRE(nabto_coap_server_resource_notify(&s.requests, s.getResource, NABTO_COAP_CODE_CONTENT, 0, notification.data(), notification.size()) == NABTO_COAP_ERROR_OK);
        sent = s.drain();
        BOOST_REQUIRE(sent.size() == 1);
        BOOST_TEST(sent[0].type == NABTO_COAP_TYPE_CON);
        BOOST_TEST(sent[0].token == "t1");
        BOOST_TEST(sent[0].payload == notification);
        BOOST_TEST(sent[0].hasObserve);
        BOOST_TEST(sent[0].observe == 1u);

        if (byApplication) {
            struct nabto_coap_server_observer* observer = nabto_coap_server_request_get_observer(s.request);
            BOOST_REQUIRE(observer != NULL);
            nabto_coap_server_remove_observer(observer);
        } else {
            s.handlePacket(rstPacket(sent[0].messageId));
        }
        BOOST_TEST(s.observerCount() == 0u);
        BOOST_TEST(nabto_coap_server_request_get_observer(s.request) == (struct nabto_coap_server_observer*)NULL);

        // The client did not ACK the initial response.
        s.timeoutTick();
        sent = s.drain();
        BOOST_REQUIRE(sent.size() == 1);
        BOOST_TEST(sent[0].type == NABTO_COAP_TYPE_CON);
        BOOST_TEST(sent[0].code == NABTO_COAP_CODE_CONTENT);
        BOOST_TEST(sent[0].token == "t1");
        BOOST_TEST(!sent[0].hasObserve);

        nabto_coap_server_request_free(s.request);
        s.request = NULL;
        s.handlePacket(ackPacket(sent[0].messageId));
        BOOST_TEST(s.requests.activeRequests == 0u);
    }
}

// Audit H2 (sc-4812): removing the connection frees its observers but
// leaves a request the application still holds alive; that request
// must not hand out the freed observer.
BOOST_AUTO_TEST_CASE(remove_connection_clears_observer_of_pending_request)
{
    TestServer s;
    s.registerObserver("t1", 0x1201);
    BOOST_TEST(s.observerCount() == 1u);

    nabto_coap_server_remove_connection(&s.requests, s.connection());
    BOOST_TEST(s.observerCount() == 0u);
    BOOST_TEST(nabto_coap_server_request_get_observer(s.request) == (struct nabto_coap_server_observer*)NULL);

    nabto_coap_server_response_set_code(s.request, NABTO_COAP_CODE_CONTENT);
    BOOST_TEST(nabto_coap_server_response_ready(s.request) == NABTO_COAP_ERROR_NO_CONNECTION);
    nabto_coap_server_request_free(s.request);
    s.request = NULL;
    BOOST_TEST(s.requests.activeRequests == 0u);
    BOOST_TEST(s.drain().empty());
}

// A notification whose ACK is late enough for the timeout to queue a
// retransmit is still in flight: the delayed ACK cancels the
// retransmit and a RST still deregisters. An ACK for a notification
// that is queued but has never been sent matches nothing.
BOOST_AUTO_TEST_CASE(delayed_ack_or_rst_matches_queued_notification_retransmit)
{
    for (int rst = 0; rst <= 1; rst++) {
        TestServer s;
        s.registerObserver("t1", 0x1301);
        s.respond(s.request, NABTO_COAP_CODE_CONTENT);
        s.request = NULL;
        BOOST_TEST(s.requests.activeRequests == 0u);

        const std::string notification = "hello";
        BOOST_REQUIRE(nabto_coap_server_resource_notify(&s.requests, s.getResource, NABTO_COAP_CODE_CONTENT, 0, notification.data(), notification.size()) == NABTO_COAP_ERROR_OK);
        uint16_t nid = s.requests.observersSentinel->next->messageId;
        // Not sent yet, so an ACK with its id is not for it.
        s.handlePacket(ackPacket(nid));
        std::vector<SentMessage> sent = s.drain();
        BOOST_REQUIRE(sent.size() == 1);
        BOOST_TEST(sent[0].type == NABTO_COAP_TYPE_CON);
        BOOST_TEST(sent[0].messageId == nid);
        BOOST_TEST(sent[0].payload == notification);

        // The timeout queues a retransmit; the client's reply arrives
        // before it is sent.
        s.timeoutTick();
        BOOST_TEST(nabto_coap_server_next_event(&s.requests) == NABTO_COAP_SERVER_NEXT_EVENT_SEND);
        if (rst) {
            s.handlePacket(rstPacket(nid));
            BOOST_TEST(s.observerCount() == 0u);
        } else {
            s.handlePacket(ackPacket(nid));
            BOOST_TEST(s.observerCount() == 1u);
        }
        BOOST_TEST(s.drain().empty());
    }
}

// Audit N2 (sc-4859): observers outlive their request, so they have a
// limit of their own. At the limit a fresh registration is refused and
// the request is answered as a plain GET; re-registering a token that
// is already observing replaces its observer and is not refused, even
// when the limit has been lowered below the count; once an observer is
// gone a new registration is accepted again.
BOOST_AUTO_TEST_CASE(observer_limit_refuses_registration_until_one_is_removed)
{
    TestServer s;
    nabto_coap_server_limit_observers(&s.requests, 2);
    s.registerObserver("t1", 0x1401);
    s.respond(s.request, NABTO_COAP_CODE_CONTENT);
    s.registerObserver("t2", 0x1402);
    s.respond(s.request, NABTO_COAP_CODE_CONTENT);
    s.request = NULL;
    BOOST_TEST(s.observerCount() == 2u);
    BOOST_TEST(s.requests.activeObservers == 2u);

    // Third token: refused, the count stays at 2 and the response
    // carries no Observe option.
    s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_GET, 0x1403, "t3", "test", 0).build());
    BOOST_TEST(s.drain().size() == 1u); // empty ACK
    BOOST_REQUIRE(s.request != NULL);
    BOOST_TEST(nabto_coap_server_request_accept_observe(s.request) == NABTO_COAP_ERROR_OUT_OF_MEMORY);
    BOOST_TEST(nabto_coap_server_request_get_observer(s.request) == (struct nabto_coap_server_observer*)NULL);
    BOOST_TEST(s.observerCount() == 2u);
    BOOST_TEST(s.requests.activeObservers == 2u);
    nabto_coap_server_response_set_code(s.request, NABTO_COAP_CODE_SERVICE_UNAVAILABLE);
    BOOST_REQUIRE(nabto_coap_server_response_ready(s.request) == NABTO_COAP_ERROR_OK);
    nabto_coap_server_request_free(s.request);
    s.request = NULL;
    std::vector<SentMessage> sent = s.drain();
    BOOST_REQUIRE(sent.size() == 1);
    BOOST_TEST(sent[0].code == NABTO_COAP_CODE_SERVICE_UNAVAILABLE);
    BOOST_TEST(sent[0].token == "t3");
    BOOST_TEST(!sent[0].hasObserve);
    s.handlePacket(ackPacket(sent[0].messageId));

    // t1 again: a replacement, accepted at the limit.
    s.registerObserver("t1", 0x1404);
    s.respond(s.request, NABTO_COAP_CODE_CONTENT);
    s.request = NULL;
    BOOST_TEST(s.observerCount() == 2u);
    BOOST_TEST(s.requests.activeObservers == 2u);

    // Deregister t2, then t3 fits.
    s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_GET, 0x1405, "t2", "test", 1).build());
    BOOST_TEST(s.drain().size() == 1u); // empty ACK
    BOOST_TEST(s.observerCount() == 1u);
    BOOST_TEST(s.requests.activeObservers == 1u);
    s.respond(s.request, NABTO_COAP_CODE_CONTENT);
    s.registerObserver("t3", 0x1406);
    s.respond(s.request, NABTO_COAP_CODE_CONTENT);
    s.request = NULL;
    BOOST_TEST(s.observerCount() == 2u);
    BOOST_TEST(s.requests.activeObservers == 2u);

    // The limit lowered below the count: t1 is still replaced, t4 is
    // refused.
    nabto_coap_server_limit_observers(&s.requests, 1);
    s.registerObserver("t1", 0x1407);
    s.respond(s.request, NABTO_COAP_CODE_CONTENT);
    s.request = NULL;
    BOOST_TEST(s.observerCount() == 2u);
    s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_GET, 0x1408, "t4", "test", 0).build());
    BOOST_TEST(s.drain().size() == 1u); // empty ACK
    BOOST_REQUIRE(s.request != NULL);
    BOOST_TEST(nabto_coap_server_request_accept_observe(s.request) == NABTO_COAP_ERROR_OUT_OF_MEMORY);
    BOOST_TEST(s.observerCount() == 2u);
    BOOST_TEST(s.requests.activeObservers == 2u);
    s.respond(s.request, NABTO_COAP_CODE_SERVICE_UNAVAILABLE);
    s.request = NULL;
    BOOST_TEST(s.requests.activeRequests == 0u);
}

// Audit N1 (sc-4858): a request gets its response message id when it
// is created, before anything is sent, and an ACK or RST used to be
// matched against it in every state. For a request still being
// received it set the state to DONE without the user ever owning it,
// so nothing freed it: the request, its body and its maxRequests slot
// were pinned until reboot, and further chunks were dropped. Ids are
// sequential, so a client can guess the next one. An ACK or RST only
// matches a response in flight; the transfer completes as usual.
BOOST_AUTO_TEST_CASE(rst_or_ack_for_unsent_response_id_keeps_block1_request)
{
    for (int rst = 0; rst <= 1; rst++) {
        TestServer s;
        const std::string chunk0(16, 'a');
        const std::string chunk1(16, 'b');
        uint16_t messageId = (uint16_t)(0xb100 + 2 * rst);

        s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_POST, messageId, "t1").block1(0, true, 0).payload(chunk0).build());
        std::vector<SentMessage> sent = s.drain();
        BOOST_REQUIRE(sent.size() == 1);
        BOOST_TEST(sent[0].code == NABTO_COAP_CODE_CONTINUE);
        BOOST_TEST(s.handlerCalls == 0u);
        BOOST_TEST(s.requests.activeRequests == 1u);

        uint16_t responseId = s.requests.requestsSentinel->next->response.messageId;
        s.handlePacket(rst ? rstPacket(responseId) : ackPacket(responseId));
        BOOST_TEST(s.drain().empty());
        BOOST_TEST(s.requests.activeRequests == 1u);

        s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_POST, (uint16_t)(messageId + 1), "t1").block1(1, false, 0).payload(chunk1).build());
        sent = s.drain();
        BOOST_REQUIRE(sent.size() == 1);
        BOOST_TEST(sent[0].type == NABTO_COAP_TYPE_ACK);
        BOOST_TEST(sent[0].code == NABTO_COAP_CODE_EMPTY);
        BOOST_REQUIRE(s.handlerCalls == 1u);
        void* payload;
        size_t payloadLength;
        BOOST_TEST(nabto_coap_server_request_get_payload(s.request, &payload, &payloadLength));
        BOOST_TEST(std::string((const char*)payload, payloadLength) == chunk0 + chunk1);

        s.respondAndFinish(NABTO_COAP_CODE_CHANGED);
    }
}

// A response that is ready but not yet sent has an id the client has
// not seen either; an ACK with it matches nothing and the response is
// sent as usual.
BOOST_AUTO_TEST_CASE(ack_for_unsent_response_is_ignored)
{
    TestServer s;
    s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_GET, 0xb200, "t1").build());
    BOOST_TEST(s.drain().size() == 1u); // empty ACK
    BOOST_REQUIRE(s.request != NULL);
    uint16_t responseId = s.request->response.messageId;
    s.respondNoAck(s.request, NABTO_COAP_CODE_CONTENT);
    s.request = NULL;

    s.handlePacket(ackPacket(responseId));
    BOOST_TEST(s.requests.activeRequests == 1u);

    std::vector<SentMessage> sent = s.drain();
    BOOST_REQUIRE(sent.size() == 1);
    BOOST_TEST(sent[0].type == NABTO_COAP_TYPE_CON);
    BOOST_TEST(sent[0].code == NABTO_COAP_CODE_CONTENT);
    BOOST_TEST(sent[0].messageId == responseId);

    s.handlePacket(ackPacket(responseId));
    BOOST_TEST(s.requests.activeRequests == 0u);
}

// Audit N9 (sc-4866): the message id counter is shared by every
// connection of a requests context, so a client can predict the ids
// handed out to another one. It cannot use them: an ACK or a RST is
// correlated with the message it answers by message id and endpoint
// (RFC 7252 section 4.4), and the library uses the connection pointer
// the integrator passes to nabto_coap_server_handle_packet as the
// endpoint, so a reply on one connection never matches a response in
// flight on another.
BOOST_AUTO_TEST_CASE(ack_or_rst_from_another_connection_does_not_match_a_response)
{
    for (int rst = 0; rst <= 1; rst++) {
        TestServer s;
        s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_GET, 0xc101, "t1").build());
        BOOST_TEST(s.drain().size() == 1u); // empty ACK
        BOOST_REQUIRE(s.request != NULL);
        s.respondNoAck(s.request, NABTO_COAP_CODE_CONTENT);
        s.request = NULL;

        std::vector<SentMessage> sent = s.drain();
        BOOST_REQUIRE(sent.size() == 1);
        BOOST_TEST(sent[0].type == NABTO_COAP_TYPE_CON);
        BOOST_TEST(s.requests.activeRequests == 1u);
        const std::vector<uint8_t> reply = rst ? rstPacket(sent[0].messageId) : ackPacket(sent[0].messageId);

        // The other connection knows the id and replies to it.
        s.handlePacket(reply, s.connection2());
        BOOST_TEST(s.requests.activeRequests == 1u);
        BOOST_TEST(s.drain().empty());

        // The same reply on the connection the request belongs to ends
        // the exchange.
        s.handlePacket(reply);
        BOOST_TEST(s.requests.activeRequests == 0u);
    }
}

// Audit N9 (sc-4866): the same for a notification in flight, which a
// RST deregisters (RFC 7252 section 4.2). The observer belongs to a
// connection, so only a RST on that connection reaches it.
BOOST_AUTO_TEST_CASE(rst_from_another_connection_keeps_an_observer)
{
    TestServer s;
    s.registerObserver("t1", 0xc201);
    s.respond(s.request, NABTO_COAP_CODE_CONTENT);
    s.request = NULL;
    BOOST_TEST(s.requests.activeRequests == 0u);

    const std::string notification = "hello";
    BOOST_REQUIRE(nabto_coap_server_resource_notify(&s.requests, s.getResource, NABTO_COAP_CODE_CONTENT, 0, notification.data(), notification.size()) == NABTO_COAP_ERROR_OK);
    std::vector<SentMessage> sent = s.drain();
    BOOST_REQUIRE(sent.size() == 1);
    BOOST_TEST(sent[0].type == NABTO_COAP_TYPE_CON);
    BOOST_TEST(sent[0].payload == notification);

    s.handlePacket(rstPacket(sent[0].messageId), s.connection2());
    BOOST_TEST(s.observerCount() == 1u);

    s.handlePacket(rstPacket(sent[0].messageId));
    BOOST_TEST(s.observerCount() == 0u);
}

// Audit N9 (sc-4866): the ids two connections see come from the one
// counter, so the gaps in the ids a client sees tell it how many
// messages the device sent to other clients in between. Accepted, with
// the reasoning in the README; this pins the behaviour so that giving
// the connections separate counters is a deliberate change.
BOOST_AUTO_TEST_CASE(message_ids_are_shared_across_connections)
{
    TestServer s;
    s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_GET, 0xc301, "t1").build());
    BOOST_TEST(s.drain().size() == 1u); // empty ACK
    BOOST_REQUIRE(s.request != NULL);
    s.respondNoAck(s.request, NABTO_COAP_CODE_CONTENT);
    s.request = NULL;
    std::vector<SentMessage> sent = s.drain();
    BOOST_REQUIRE(sent.size() == 1);
    uint16_t firstId = sent[0].messageId;

    s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_GET, 0xc302, "t2").build(), s.connection2());
    BOOST_TEST(s.drain(s.connection2()).size() == 1u); // empty ACK
    BOOST_REQUIRE(s.request != NULL);
    s.respondNoAck(s.request, NABTO_COAP_CODE_CONTENT);
    s.request = NULL;
    sent = s.drain(s.connection2());
    BOOST_REQUIRE(sent.size() == 1);
    BOOST_TEST(sent[0].messageId == (uint16_t)(firstId + 1));

    s.handlePacket(ackPacket(firstId));
    s.handlePacket(ackPacket(sent[0].messageId), s.connection2());
    BOOST_TEST(s.requests.activeRequests == 0u);
}

// A parameter segment is written {name}. A segment which only looks
// like the start of one has no name to copy, so it is refused instead
// of being run through the arithmetic of a well formed parameter.
BOOST_AUTO_TEST_CASE(malformed_parameter_segment_is_rejected)
{
    size_t allocationsBefore = liveAllocations;
    struct nabto_coap_server server;
    BOOST_REQUIRE(nabto_coap_server_init(&server, NULL, &countingAllocator) == NABTO_COAP_ERROR_OK);
    struct nabto_coap_server_resource* resource = NULL;

    const char* onlyBrace[] = { "{", NULL };
    BOOST_TEST(nabto_coap_server_add_resource(&server, NABTO_COAP_CODE_GET, onlyBrace, &unusedHandler, NULL, &resource) == NABTO_COAP_ERROR_INVALID_PARAMETER);

    const char* emptyName[] = { "{}", NULL };
    BOOST_TEST(nabto_coap_server_add_resource(&server, NABTO_COAP_CODE_GET, emptyName, &unusedHandler, NULL, &resource) == NABTO_COAP_ERROR_INVALID_PARAMETER);

    const char* noEndBrace[] = { "{user", NULL };
    BOOST_TEST(nabto_coap_server_add_resource(&server, NABTO_COAP_CODE_GET, noEndBrace, &unusedHandler, NULL, &resource) == NABTO_COAP_ERROR_INVALID_PARAMETER);

    const char* user[] = { "iam", "{user}", NULL };
    BOOST_TEST(nabto_coap_server_add_resource(&server, NABTO_COAP_CODE_GET, user, &unusedHandler, NULL, &resource) == NABTO_COAP_ERROR_OK);

    // A parameter on a node which already has one has to carry the
    // same name, and a prefix of that name is not the same name.
    const char* prefixOfUser[] = { "iam", "{us}", NULL };
    BOOST_TEST(nabto_coap_server_add_resource(&server, NABTO_COAP_CODE_POST, prefixOfUser, &unusedHandler, NULL, &resource) == NABTO_COAP_ERROR_INVALID_PARAMETER);

    const char* sameUser[] = { "iam", "{user}", NULL };
    BOOST_TEST(nabto_coap_server_add_resource(&server, NABTO_COAP_CODE_POST, sameUser, &unusedHandler, NULL, &resource) == NABTO_COAP_ERROR_OK);

    nabto_coap_server_destroy(&server);
    BOOST_TEST(liveAllocations == allocationsBefore);
}

BOOST_AUTO_TEST_SUITE_END()
