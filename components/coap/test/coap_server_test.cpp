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

    void handlePacket(const std::vector<uint8_t>& packet)
    {
        nabto_coap_server_handle_packet(&requests, connection(), packet.data(), packet.size());
    }

    // Send everything the server has queued and return it.
    std::vector<SentMessage> drain()
    {
        std::vector<SentMessage> sent;
        while (nabto_coap_server_next_event(&requests) == NABTO_COAP_SERVER_NEXT_EVENT_SEND) {
            BOOST_REQUIRE(nabto_coap_server_get_connection_send(&requests) == connection());
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
    size_t allocationsBefore_;
};

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

    s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_GET, 0xe002, "t1").block2(1, 5).build());
    sent = s.drain();
    BOOST_REQUIRE(sent.size() == 1);
    BOOST_TEST(sent[0].code == NABTO_COAP_CODE_CONTENT);
    BOOST_TEST(sent[0].hasBlock2);
    BOOST_TEST(sent[0].block2 == blockOption(1, false, 5));
    BOOST_TEST(sent[0].payload == body.substr(512));
    s.handlePacket(ackPacket(sent[0].messageId));
    BOOST_TEST(s.requests.activeRequests == 0u);

    s.now += NABTO_COAP_ACK_TIMEOUT;
    nabto_coap_server_handle_timeout(&s.requests);
    BOOST_TEST(s.drain().empty());
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
    s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_GET, 0xf302, "t1").block2(1, 4).build());
    std::vector<SentMessage> sent = s.drain();
    BOOST_REQUIRE(sent.size() == 1);
    BOOST_TEST(sent[0].code == NABTO_COAP_CODE_CONTENT);
    BOOST_TEST(sent[0].hasBlock2);
    BOOST_TEST(sent[0].block2 == blockOption(1, true, 4));
    BOOST_TEST(sent[0].payload == body.substr(256, 256));
    s.handlePacket(ackPacket(sent[0].messageId));
    BOOST_TEST(s.requests.activeRequests == 1u);

    // The last 256 byte block completes the exchange.
    s.handlePacket(RequestBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_GET, 0xf303, "t1").block2(3, 4).build());
    sent = s.drain();
    BOOST_REQUIRE(sent.size() == 1);
    BOOST_TEST(sent[0].code == NABTO_COAP_CODE_CONTENT);
    BOOST_TEST(sent[0].hasBlock2);
    BOOST_TEST(sent[0].block2 == blockOption(3, false, 4));
    BOOST_TEST(sent[0].payload == body.substr(768));
    s.handlePacket(ackPacket(sent[0].messageId));
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

BOOST_AUTO_TEST_SUITE_END()
