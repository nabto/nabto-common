#include <boost/test/unit_test.hpp>
#include <nabto_coap/nabto_coap_client.h>
#include "../src/nabto_coap_client_impl.h" // request internals for assertions

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

// Every allocation the client makes is counted so a test can tell
// that the client released everything it took.
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

uint32_t blockOption(uint32_t num, bool more, uint32_t szx)
{
    return (num << 4) | ((more ? 1u : 0u) << 3) | szx;
}

/**
 * The interesting parts of a packet the client wanted to send.
 */
struct SentMessage {
    nabto_coap_type type;
    nabto_coap_code code;
    uint16_t messageId;
    nabto_coap_token token;
    std::string payload;
    bool hasBlock2;
    uint32_t block2;
};

/**
 * Builds a response from the server to a request the client sent,
 * reusing the request's token.
 */
class ResponseBuilder {
 public:
    ResponseBuilder(nabto_coap_type type, nabto_coap_code code, uint16_t messageId, const nabto_coap_token& token)
    {
        struct nabto_coap_message_header header;
        memset(&header, 0, sizeof(header));
        header.type = type;
        header.code = code;
        header.messageId = messageId;
        header.token = token;
        ptr_ = nabto_coap_encode_header(&header, buffer_, end());
        BOOST_REQUIRE(ptr_ != NULL);
    }

    ResponseBuilder& block2(uint32_t num, bool more, uint32_t szx)
    {
        ptr_ = nabto_coap_encode_varint_option(NABTO_COAP_OPTION_BLOCK2 - currentOption_, blockOption(num, more, szx), ptr_, end());
        BOOST_REQUIRE(ptr_ != NULL);
        currentOption_ = NABTO_COAP_OPTION_BLOCK2;
        return *this;
    }

    ResponseBuilder& payload(const std::string& data)
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

/**
 * A client with one GET /test request whose end handler records the
 * status it completed with.
 */
class TestClient {
 public:
    TestClient()
        : allocationsBefore_(liveAllocations)
    {
        BOOST_REQUIRE(nabto_coap_client_init(&client, &countingAllocator, &TestClient::notifyEvent, this) == NABTO_COAP_ERROR_OK);
        request = nabto_coap_client_request_new(&client, NABTO_COAP_METHOD_GET, 1, path_, &TestClient::endHandler, this, connection());
        BOOST_REQUIRE(request != NULL);
    }
    ~TestClient()
    {
        if (request != NULL) {
            nabto_coap_client_request_free(request);
        }
        nabto_coap_client_destroy(&client);
        BOOST_TEST(liveAllocations == allocationsBefore_);
    }

    enum nabto_coap_client_status handlePacket(const std::vector<uint8_t>& packet)
    {
        return nabto_coap_client_handle_packet(&client, now, packet.data(), packet.size(), connection());
    }

    // Send everything the client has queued and return it.
    std::vector<SentMessage> drain()
    {
        std::vector<SentMessage> sent;
        while (nabto_coap_client_get_next_event(&client, now) == NABTO_COAP_CLIENT_NEXT_EVENT_SEND) {
            uint8_t buffer[1500];
            void* conn = NULL;
            uint8_t* ptr = nabto_coap_client_create_packet(&client, now, buffer, buffer + sizeof(buffer), &conn);
            BOOST_REQUIRE(ptr != NULL);
            BOOST_REQUIRE(conn == connection());
            struct nabto_coap_incoming_message msg;
            BOOST_REQUIRE(nabto_coap_parse_message(buffer, ptr - buffer, &msg));
            SentMessage m;
            m.type = msg.type;
            m.code = msg.code;
            m.messageId = msg.messageId;
            m.token = msg.token;
            if (msg.payload != NULL) {
                m.payload = std::string((const char*)msg.payload, msg.payloadLength);
            }
            m.hasBlock2 = msg.hasBlock2;
            m.block2 = msg.block2;
            sent.push_back(m);
        }
        return sent;
    }

    // Send the request and return the single packet it produced.
    SentMessage sendRequest()
    {
        nabto_coap_client_request_send(request);
        std::vector<SentMessage> sent = drain();
        BOOST_REQUIRE(sent.size() == 1);
        return sent[0];
    }

    // Run the end handler if the request has completed.
    bool runCallback()
    {
        if (nabto_coap_client_get_next_event(&client, now) != NABTO_COAP_CLIENT_NEXT_EVENT_CALLBACK) {
            return false;
        }
        nabto_coap_client_handle_callback(&client);
        return true;
    }

    void* connection() { return &connection_; }

    struct nabto_coap_client client;
    struct nabto_coap_client_request* request = NULL;
    size_t endHandlerCalls = 0;
    enum nabto_coap_client_status endStatus = NABTO_COAP_CLIENT_STATUS_IN_PROGRESS;
    uint32_t now = 1000;

 private:
    static void notifyEvent(void* userData) { (void)userData; }
    static void endHandler(struct nabto_coap_client_request* request, void* userData)
    {
        TestClient* self = static_cast<TestClient*>(userData);
        BOOST_TEST(request == self->request);
        self->endHandlerCalls++;
        self->endStatus = nabto_coap_client_request_get_status(request);
    }

    const char* path_[1] = { "test" };
    int connection_;
    size_t allocationsBefore_;
};

} // namespace

BOOST_AUTO_TEST_SUITE(coap_client)

// Audit M7 (sc-4821): the Block2 error exits freed the response but not
// the payload reassembled so far.
BOOST_AUTO_TEST_CASE(block2_error_releases_reassembled_payload)
{
    const std::string chunk(16, 'a');
    {
        // A block whose offset does not continue the payload.
        TestClient c;
        SentMessage req = c.sendRequest();
        BOOST_TEST(c.handlePacket(ResponseBuilder(NABTO_COAP_TYPE_ACK, NABTO_COAP_CODE_CONTENT, req.messageId, req.token).block2(0, true, 0).payload(chunk).build()) == NABTO_COAP_CLIENT_STATUS_OK);
        std::vector<SentMessage> sent = c.drain();
        BOOST_REQUIRE(sent.size() == 1);
        BOOST_TEST(sent[0].hasBlock2);
        BOOST_TEST(sent[0].block2 == blockOption(1, false, 0));
        BOOST_TEST(c.request->retransmissions == 0u);
        size_t allocationsWithResponse = liveAllocations;

        BOOST_TEST(c.handlePacket(ResponseBuilder(NABTO_COAP_TYPE_ACK, NABTO_COAP_CODE_CONTENT, sent[0].messageId, req.token).block2(5, true, 0).payload(chunk).build()) == NABTO_COAP_CLIENT_STATUS_DECODE_ERROR);
        BOOST_TEST(c.request->response == (struct nabto_coap_client_response*)NULL);
        // The response struct and its payload are both gone.
        BOOST_TEST(liveAllocations == allocationsWithResponse - 2);
    }
    {
        // A block with the more bit set that is shorter than the block size.
        TestClient c;
        SentMessage req = c.sendRequest();
        BOOST_TEST(c.handlePacket(ResponseBuilder(NABTO_COAP_TYPE_ACK, NABTO_COAP_CODE_CONTENT, req.messageId, req.token).block2(0, true, 0).payload(chunk).build()) == NABTO_COAP_CLIENT_STATUS_OK);
        std::vector<SentMessage> sent = c.drain();
        BOOST_REQUIRE(sent.size() == 1);
        size_t allocationsWithResponse = liveAllocations;

        BOOST_TEST(c.handlePacket(ResponseBuilder(NABTO_COAP_TYPE_ACK, NABTO_COAP_CODE_CONTENT, sent[0].messageId, req.token).block2(1, true, 0).payload("short").build()) == NABTO_COAP_CLIENT_STATUS_DECODE_ERROR);
        BOOST_TEST(c.request->response == (struct nabto_coap_client_response*)NULL);
        BOOST_TEST(liveAllocations == allocationsWithResponse - 2);
    }
}

// Audit M7 (sc-4821): a RST completed the request without setting a
// status, so it reported OK with no response.
BOOST_AUTO_TEST_CASE(rst_completes_request_with_reset_status)
{
    {
        // RST while waiting for the ACK.
        TestClient c;
        SentMessage req = c.sendRequest();
        BOOST_TEST(c.handlePacket(emptyPacket(NABTO_COAP_TYPE_RST, req.messageId)) == NABTO_COAP_CLIENT_STATUS_OK);
        BOOST_TEST(c.runCallback());
        BOOST_TEST(c.endHandlerCalls == 1u);
        BOOST_TEST(c.endStatus == NABTO_COAP_CLIENT_STATUS_RESET);
        BOOST_TEST(nabto_coap_client_request_get_response(c.request) == (struct nabto_coap_client_response*)NULL);
        BOOST_TEST(nabto_coap_client_get_next_event(&c.client, c.now) == NABTO_COAP_CLIENT_NEXT_EVENT_NOTHING);
    }
    {
        // RST of a separate response after the request was acked.
        TestClient c;
        SentMessage req = c.sendRequest();
        BOOST_TEST(c.handlePacket(emptyPacket(NABTO_COAP_TYPE_ACK, req.messageId)) == NABTO_COAP_CLIENT_STATUS_OK);
        BOOST_TEST(c.request->state == NABTO_COAP_CLIENT_REQUEST_STATE_WAIT_RESPONSE);
        BOOST_TEST(c.handlePacket(emptyPacket(NABTO_COAP_TYPE_RST, req.messageId)) == NABTO_COAP_CLIENT_STATUS_OK);
        BOOST_TEST(c.runCallback());
        BOOST_TEST(c.endHandlerCalls == 1u);
        BOOST_TEST(c.endStatus == NABTO_COAP_CLIENT_STATUS_RESET);
    }
}

// Audit M7 (sc-4821): each timeout counted two retransmissions, so
// maxRetransmits = 6 gave 3 retries, and the ACK timeout never grew.
// RFC 7252 section 4.2: the request is retransmitted MAX_RETRANSMIT
// times with the timeout doubled each time.
BOOST_AUTO_TEST_CASE(retransmit_count_equals_max_retransmits)
{
    TestClient c;
    BOOST_TEST(c.client.settings.maxRetransmits == NABTO_COAP_MAX_RETRANSMITS);
    BOOST_TEST(c.client.settings.ackTimeoutMilliseconds == NABTO_COAP_ACK_TIMEOUT);
    const uint32_t ackTimeout = c.client.settings.ackTimeoutMilliseconds;
    const uint8_t maxRetransmits = c.client.settings.maxRetransmits;

    SentMessage req = c.sendRequest();
    for (uint8_t retransmit = 0; retransmit < maxRetransmits; retransmit++) {
        uint32_t timeout = nabto_coap_client_get_next_timeout(&c.client, c.now);
        BOOST_TEST(timeout == c.now + (ackTimeout << retransmit));
        c.now = timeout;
        nabto_coap_client_handle_timeout(&c.client, c.now);
        std::vector<SentMessage> sent = c.drain();
        BOOST_REQUIRE(sent.size() == 1);
        BOOST_TEST(sent[0].type == NABTO_COAP_TYPE_CON);
        BOOST_TEST(sent[0].messageId == req.messageId);
        BOOST_TEST(c.request->retransmissions == retransmit + 1);
    }

    // The last retransmission waits its doubled timeout and then gives up.
    uint32_t timeout = nabto_coap_client_get_next_timeout(&c.client, c.now);
    BOOST_TEST(timeout == c.now + (ackTimeout << maxRetransmits));
    c.now = timeout;
    nabto_coap_client_handle_timeout(&c.client, c.now);
    BOOST_TEST(c.drain().empty());
    BOOST_TEST(c.runCallback());
    BOOST_TEST(c.endHandlerCalls == 1u);
    BOOST_TEST(c.endStatus == NABTO_COAP_CLIENT_STATUS_TIMEOUT);
}

// Audit M7 (sc-4821): nabto_coap_client_request_set_nonconfirmable had
// no effect, the request was always sent as CON.
BOOST_AUTO_TEST_CASE(nonconfirmable_request_is_sent_as_non)
{
    TestClient c;
    nabto_coap_client_request_set_nonconfirmable(c.request);
    nabto_coap_client_request_set_timeout(c.request, 5000);
    SentMessage req = c.sendRequest();
    BOOST_TEST(req.type == NABTO_COAP_TYPE_NON);
    BOOST_TEST(req.code == NABTO_COAP_CODE_GET);

    // No ACK is expected, so the request waits for the response with the
    // response timeout and is not retransmitted.
    BOOST_TEST(c.request->state == NABTO_COAP_CLIENT_REQUEST_STATE_WAIT_RESPONSE);
    BOOST_TEST(nabto_coap_client_get_next_timeout(&c.client, c.now) == c.now + 5000);
    c.now += NABTO_COAP_ACK_TIMEOUT;
    nabto_coap_client_handle_timeout(&c.client, c.now);
    BOOST_TEST(c.drain().empty());

    BOOST_TEST(c.handlePacket(ResponseBuilder(NABTO_COAP_TYPE_NON, NABTO_COAP_CODE_CONTENT, 0x4242, req.token).payload("hello").build()) == NABTO_COAP_CLIENT_STATUS_OK);
    BOOST_TEST(c.drain().empty()); // a NON response is not acked
    BOOST_TEST(c.runCallback());
    BOOST_TEST(c.endHandlerCalls == 1u);
    BOOST_TEST(c.endStatus == NABTO_COAP_CLIENT_STATUS_OK);
    struct nabto_coap_client_response* response = nabto_coap_client_request_get_response(c.request);
    BOOST_REQUIRE(response != NULL);
    BOOST_TEST(nabto_coap_client_response_get_code(response) == 205);
    const uint8_t* payload;
    size_t payloadLength;
    BOOST_TEST(nabto_coap_client_response_get_payload(response, &payload, &payloadLength));
    BOOST_TEST(std::string((const char*)payload, payloadLength) == "hello");
}

// Audit M7 (sc-4821): the response reassembled from Block2 blocks had
// no upper bound.
BOOST_AUTO_TEST_CASE(response_over_the_limit_is_rejected)
{
    const std::string chunk(16, 'a');
    {
        // Two blocks fit in 40 bytes, the third does not.
        TestClient c;
        nabto_coap_client_limit_response_size(&c.client, 40);
        SentMessage req = c.sendRequest();
        uint16_t messageId = req.messageId;
        for (uint32_t num = 0; num < 2; num++) {
            BOOST_TEST(c.handlePacket(ResponseBuilder(NABTO_COAP_TYPE_ACK, NABTO_COAP_CODE_CONTENT, messageId, req.token).block2(num, true, 0).payload(chunk).build()) == NABTO_COAP_CLIENT_STATUS_OK);
            std::vector<SentMessage> sent = c.drain();
            BOOST_REQUIRE(sent.size() == 1);
            BOOST_TEST(sent[0].block2 == blockOption(num + 1, false, 0));
            messageId = sent[0].messageId;
        }
        size_t allocationsWithResponse = liveAllocations;

        BOOST_TEST(c.handlePacket(ResponseBuilder(NABTO_COAP_TYPE_ACK, NABTO_COAP_CODE_CONTENT, messageId, req.token).block2(2, true, 0).payload(chunk).build()) == NABTO_COAP_CLIENT_STATUS_DECODE_ERROR);
        BOOST_TEST(c.request->response == (struct nabto_coap_client_response*)NULL);
        BOOST_TEST(liveAllocations == allocationsWithResponse - 2);
        BOOST_TEST(c.drain().empty());
    }
    {
        // A response of exactly the limit is accepted.
        TestClient c;
        nabto_coap_client_limit_response_size(&c.client, 32);
        SentMessage req = c.sendRequest();
        BOOST_TEST(c.handlePacket(ResponseBuilder(NABTO_COAP_TYPE_ACK, NABTO_COAP_CODE_CONTENT, req.messageId, req.token).block2(0, true, 0).payload(chunk).build()) == NABTO_COAP_CLIENT_STATUS_OK);
        std::vector<SentMessage> sent = c.drain();
        BOOST_REQUIRE(sent.size() == 1);
        BOOST_TEST(c.handlePacket(ResponseBuilder(NABTO_COAP_TYPE_ACK, NABTO_COAP_CODE_CONTENT, sent[0].messageId, req.token).block2(1, false, 0).payload(chunk).build()) == NABTO_COAP_CLIENT_STATUS_OK);
        BOOST_TEST(c.runCallback());
        BOOST_TEST(c.endStatus == NABTO_COAP_CLIENT_STATUS_OK);
        const uint8_t* payload;
        size_t payloadLength;
        BOOST_TEST(nabto_coap_client_response_get_payload(nabto_coap_client_request_get_response(c.request), &payload, &payloadLength));
        BOOST_TEST(payloadLength == 32u);
    }
    {
        // The limit also applies to a response sent in a single packet.
        TestClient c;
        nabto_coap_client_limit_response_size(&c.client, 8);
        SentMessage req = c.sendRequest();
        BOOST_TEST(c.handlePacket(ResponseBuilder(NABTO_COAP_TYPE_ACK, NABTO_COAP_CODE_CONTENT, req.messageId, req.token).payload(chunk).build()) == NABTO_COAP_CLIENT_STATUS_DECODE_ERROR);
        BOOST_TEST(c.request->response == (struct nabto_coap_client_response*)NULL);
    }
}

BOOST_AUTO_TEST_SUITE_END()
