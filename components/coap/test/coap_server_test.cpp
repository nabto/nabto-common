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

const uint8_t URI_PATH_TO_BLOCK1 = NABTO_COAP_OPTION_BLOCK1 - NABTO_COAP_OPTION_URI_PATH;

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
 * Builds a request for the resource /test.
 */
class RequestBuilder {
 public:
    RequestBuilder(nabto_coap_type type, nabto_coap_code code, uint16_t messageId, const std::string& token)
    {
        struct nabto_coap_message_header header;
        memset(&header, 0, sizeof(header));
        header.type = type;
        header.code = code;
        header.messageId = messageId;
        header.token = makeToken(token);
        ptr_ = nabto_coap_encode_header(&header, buffer_, end());
        BOOST_REQUIRE(ptr_ != NULL);
        const char* path = "test";
        ptr_ = nabto_coap_encode_option(NABTO_COAP_OPTION_URI_PATH, (const uint8_t*)path, strlen(path), ptr_, end());
        BOOST_REQUIRE(ptr_ != NULL);
    }

    RequestBuilder& block1(uint32_t num, bool more, uint32_t szx)
    {
        ptr_ = nabto_coap_encode_varint_option(URI_PATH_TO_BLOCK1, block1Option(num, more, szx), ptr_, end());
        BOOST_REQUIRE(ptr_ != NULL);
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
            m.hasBlock1 = msg.hasBlock1;
            m.block1 = msg.block1;
            sent.push_back(m);
        }
        return sent;
    }

    // Respond to the pending request and complete the exchange so the
    // request is released before the server is torn down.
    void respondAndFinish(nabto_coap_code code)
    {
        BOOST_REQUIRE(request != NULL);
        nabto_coap_server_response_set_code(request, code);
        BOOST_REQUIRE(nabto_coap_server_response_ready(request) == NABTO_COAP_ERROR_OK);
        nabto_coap_server_request_free(request);
        request = NULL;

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
        BOOST_TEST(requests.activeRequests == 0u);
    }

    void* connection() { return &connection_; }

    struct nabto_coap_server server;
    struct nabto_coap_server_requests requests;
    struct nabto_coap_server_request* request = NULL;
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

BOOST_AUTO_TEST_SUITE_END()
