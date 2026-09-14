#include <boost/test/unit_test.hpp>
#include <nabto_coap/nabto_coap_server.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct nn_allocator defaultAllocator = {
    &calloc,
    &free
};

nabto_coap_token makeToken(const std::string& s)
{
    nabto_coap_token t;
    memset(&t, 0, sizeof(t));
    t.tokenLength = (uint8_t)s.size();
    memcpy(t.token, s.data(), s.size());
    return t;
}

uint32_t block1Option(uint32_t num, bool more, uint32_t szx)
{
    return (num << 4) | ((more ? 1u : 0u) << 3) | szx;
}

/**
 * Builds a request with a single Uri-Path segment, /test by default.
 */
class RequestBuilder {
 public:
    RequestBuilder(nabto_coap_type type, nabto_coap_code code, uint16_t messageId, const std::string& token, const std::string& path = "test")
    {
        struct nabto_coap_message_header header;
        memset(&header, 0, sizeof(header));
        header.type = type;
        header.code = code;
        header.messageId = messageId;
        header.token = makeToken(token);
        ptr_ = nabto_coap_encode_header(&header, buffer_, end());
        BOOST_REQUIRE(ptr_ != NULL);
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
        ptr_ = nabto_coap_encode_varint_option(NABTO_COAP_OPTION_BLOCK1 - currentOption_, block1Option(num, more, szx), ptr_, end());
        BOOST_REQUIRE(ptr_ != NULL);
        currentOption_ = NABTO_COAP_OPTION_BLOCK1;
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

std::vector<uint8_t> ackPacket(uint16_t messageId)
{
    uint8_t buffer[16];
    struct nabto_coap_message_header header;
    memset(&header, 0, sizeof(header));
    header.type = NABTO_COAP_TYPE_ACK;
    header.code = NABTO_COAP_CODE_EMPTY;
    header.messageId = messageId;
    uint8_t* ptr = nabto_coap_encode_header(&header, buffer, buffer + sizeof(buffer));
    BOOST_REQUIRE(ptr != NULL);
    return std::vector<uint8_t>(buffer, ptr);
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
};

/**
 * A server with GET /test and POST /test whose handler records the
 * request and leaves it to the test to respond, so the request stays
 * pending while the client retransmits.
 */
class TestServer {
 public:
    TestServer()
    {
        BOOST_REQUIRE(nabto_coap_server_init(&server, NULL, &defaultAllocator) == NABTO_COAP_ERROR_OK);
        BOOST_REQUIRE(nabto_coap_server_requests_init(&requests, &server, &TestServer::getStamp, &TestServer::notifyEvent, this) == NABTO_COAP_ERROR_OK);
        const char* path[] = { "test", NULL };
        struct nabto_coap_server_resource* resource;
        BOOST_REQUIRE(nabto_coap_server_add_resource(&server, NABTO_COAP_CODE_GET, path, &TestServer::handler, this, &resource) == NABTO_COAP_ERROR_OK);
        BOOST_REQUIRE(nabto_coap_server_add_resource(&server, NABTO_COAP_CODE_POST, path, &TestServer::handler, this, &resource) == NABTO_COAP_ERROR_OK);
    }
    ~TestServer()
    {
        nabto_coap_server_requests_destroy(&requests);
        nabto_coap_server_destroy(&server);
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

    void* connection() { return &connection_; }

    struct nabto_coap_server server;
    struct nabto_coap_server_requests requests;
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
    BOOST_TEST(sent[0].block1 == block1Option(0, true, 0));
    BOOST_TEST(s.handlerCalls == 0u);

    // The client did not get the Continue and retransmits the chunk.
    s.handlePacket(first);
    sent = s.drain();
    BOOST_REQUIRE(sent.size() == 1);
    BOOST_TEST(sent[0].type == NABTO_COAP_TYPE_ACK);
    BOOST_TEST(sent[0].code == NABTO_COAP_CODE_CONTINUE);
    BOOST_TEST(sent[0].messageId == 0x2000);
    BOOST_TEST(sent[0].hasBlock1);
    BOOST_TEST(sent[0].block1 == block1Option(0, true, 0));
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

BOOST_AUTO_TEST_SUITE_END()
