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
    bool hasBlock1;
    uint32_t block1;
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

    // Option 27, so it has to be added after block2 (23).
    ResponseBuilder& block1(uint32_t num, bool more, uint32_t szx)
    {
        ptr_ = nabto_coap_encode_varint_option(NABTO_COAP_OPTION_BLOCK1 - currentOption_, blockOption(num, more, szx), ptr_, end());
        BOOST_REQUIRE(ptr_ != NULL);
        currentOption_ = NABTO_COAP_OPTION_BLOCK1;
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
    explicit TestClient(nabto_coap_method method = NABTO_COAP_METHOD_GET, const std::string& body = std::string())
        : allocationsBefore_(liveAllocations)
    {
        BOOST_REQUIRE(nabto_coap_client_init(&client, &countingAllocator, &TestClient::notifyEvent, this) == NABTO_COAP_ERROR_OK);
        request = nabto_coap_client_request_new(&client, method, 1, path_, &TestClient::endHandler, this, connection());
        BOOST_REQUIRE(request != NULL);
        if (!body.empty()) {
            BOOST_REQUIRE(nabto_coap_client_request_set_payload(request, (void*)body.data(), body.size()) == NABTO_COAP_ERROR_OK);
        }
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
            m.hasBlock1 = msg.hasBlock1;
            m.block1 = msg.block1;
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
// Audit N14: RFC 7959 section 2.5, "the client SHOULD heed the preference
// indicated and, for all further blocks, use the block size preferred by
// the server or a smaller one." block1Size was written in exactly one
// place, request_new, so a server that asked for smaller blocks was
// ignored and the transfer could never complete against it. Progress was
// a block counter, which is why the smaller size could not be adopted:
// the counter's unit changes with it. It is a byte offset now.
//
// Section 2.5 figure 3 is the shape: the server keeps everything it was
// sent and only asks for the rest in smaller pieces, so the next block
// number is the byte offset in the new size.
BOOST_AUTO_TEST_CASE(block1_adopts_a_smaller_block_size_from_the_continue)
{
    std::string body(1200, 'b');
    for (size_t i = 0; i < body.size(); i++) { body[i] = (char)('a' + (i % 26)); }
    TestClient c(NABTO_COAP_METHOD_POST, body);

    SentMessage chunk0 = c.sendRequest();
    BOOST_TEST(chunk0.hasBlock1);
    BOOST_TEST(chunk0.block1 == blockOption(0, true, 5));
    BOOST_TEST(chunk0.payload == body.substr(0, 512));

    // The server keeps the 512 bytes but wants 64 byte blocks from here.
    BOOST_TEST(c.handlePacket(ResponseBuilder(NABTO_COAP_TYPE_ACK, NABTO_COAP_CODE_CONTINUE, chunk0.messageId, chunk0.token).block1(0, true, 2).build()) == NABTO_COAP_CLIENT_STATUS_OK);
    std::vector<SentMessage> sent = c.drain();
    BOOST_REQUIRE(sent.size() == 1);
    // Byte offset 512 counted in 64 byte blocks is block 8.
    BOOST_TEST(sent[0].block1 == blockOption(8, true, 2));
    BOOST_TEST(sent[0].payload == body.substr(512, 64));

    // And it stays at the smaller size.
    BOOST_TEST(c.handlePacket(ResponseBuilder(NABTO_COAP_TYPE_ACK, NABTO_COAP_CODE_CONTINUE, sent[0].messageId, chunk0.token).block1(8, true, 2).build()) == NABTO_COAP_CLIENT_STATUS_OK);
    sent = c.drain();
    BOOST_REQUIRE(sent.size() == 1);
    BOOST_TEST(sent[0].block1 == blockOption(9, true, 2));
    BOOST_TEST(sent[0].payload == body.substr(576, 64));
}

// The server resends its 2.31 Continue for a retransmitted chunk, and the
// client matches responses by token alone, so a stale Continue for the
// previous chunk arrives while the next one is in flight. Advancing on it
// would skip a chunk; treating it as an error would abort a healthy
// transfer on a lossy link.
BOOST_AUTO_TEST_CASE(stale_block1_continue_is_ignored)
{
    const std::string body(1200, 'b');
    TestClient c(NABTO_COAP_METHOD_POST, body);

    SentMessage chunk0 = c.sendRequest();
    BOOST_TEST(c.handlePacket(ResponseBuilder(NABTO_COAP_TYPE_ACK, NABTO_COAP_CODE_CONTINUE, chunk0.messageId, chunk0.token).block1(0, true, 5).build()) == NABTO_COAP_CLIENT_STATUS_OK);
    std::vector<SentMessage> sent = c.drain();
    BOOST_REQUIRE(sent.size() == 1);
    BOOST_TEST(sent[0].block1 == blockOption(1, true, 5));
    SentMessage chunk1 = sent[0];

    // The Continue for chunk 0 again, while chunk 1 is in flight.
    BOOST_TEST(c.handlePacket(ResponseBuilder(NABTO_COAP_TYPE_ACK, NABTO_COAP_CODE_CONTINUE, chunk0.messageId, chunk0.token).block1(0, true, 5).build()) == NABTO_COAP_CLIENT_STATUS_OK);
    BOOST_TEST(c.drain().empty());

    // The real Continue for chunk 1 still moves the transfer on, to the
    // last chunk, which is short and clears the more bit.
    BOOST_TEST(c.handlePacket(ResponseBuilder(NABTO_COAP_TYPE_ACK, NABTO_COAP_CODE_CONTINUE, chunk1.messageId, chunk0.token).block1(1, true, 5).build()) == NABTO_COAP_CLIENT_STATUS_OK);
    sent = c.drain();
    BOOST_REQUIRE(sent.size() == 1);
    BOOST_TEST(sent[0].block1 == blockOption(2, false, 5));
    BOOST_TEST(sent[0].payload == body.substr(1024));
}

// A 2.31 Continue on the last chunk is a server bug -- there is nothing
// left to continue -- but it left the request in WAIT_ACK with the body
// fully sent, so it retransmitted empty requests for a minute before
// timing out. It completes at once now, with whatever the server said.
BOOST_AUTO_TEST_CASE(continue_on_the_last_block1_chunk_completes_the_request)
{
    const std::string body(600, 'b');
    TestClient c(NABTO_COAP_METHOD_POST, body);

    SentMessage chunk0 = c.sendRequest();
    BOOST_TEST(c.handlePacket(ResponseBuilder(NABTO_COAP_TYPE_ACK, NABTO_COAP_CODE_CONTINUE, chunk0.messageId, chunk0.token).block1(0, true, 5).build()) == NABTO_COAP_CLIENT_STATUS_OK);
    std::vector<SentMessage> sent = c.drain();
    BOOST_REQUIRE(sent.size() == 1);
    BOOST_TEST(sent[0].block1 == blockOption(1, false, 5));

    // The whole body is sent, and the server answers Continue anyway.
    BOOST_TEST(c.handlePacket(ResponseBuilder(NABTO_COAP_TYPE_ACK, NABTO_COAP_CODE_CONTINUE, sent[0].messageId, chunk0.token).block1(1, false, 5).build()) == NABTO_COAP_CLIENT_STATUS_OK);
    BOOST_TEST(c.drain().empty());
    BOOST_TEST(c.runCallback());
    BOOST_TEST(c.endHandlerCalls == 1u);
    BOOST_TEST(c.endStatus == NABTO_COAP_CLIENT_STATUS_OK);
    BOOST_TEST(nabto_coap_client_response_get_code(nabto_coap_client_request_get_response(c.request)) == 231);
}

// RFC 7959 section 2.9.3: "a 4.13 response with a smaller SZX in its
// Block1 Option than requested is a hint to try a smaller SZX". The
// server discards what it has, so the transfer starts over at block zero,
// once: a server that keeps answering 4.13 must not loop the client.
BOOST_AUTO_TEST_CASE(request_entity_too_large_with_a_smaller_szx_restarts_once)
{
    const std::string body(1200, 'b');
    TestClient c(NABTO_COAP_METHOD_POST, body);

    SentMessage chunk0 = c.sendRequest();
    BOOST_TEST(chunk0.block1 == blockOption(0, true, 5));

    BOOST_TEST(c.handlePacket(ResponseBuilder(NABTO_COAP_TYPE_ACK, NABTO_COAP_CODE_REQUEST_ENTITY_TOO_LARGE, chunk0.messageId, chunk0.token).block1(0, false, 2).build()) == NABTO_COAP_CLIENT_STATUS_OK);
    std::vector<SentMessage> sent = c.drain();
    BOOST_REQUIRE(sent.size() == 1);
    BOOST_TEST(sent[0].block1 == blockOption(0, true, 2));
    BOOST_TEST(sent[0].payload == body.substr(0, 64));
    BOOST_TEST(sent[0].messageId != chunk0.messageId);
    BOOST_TEST(c.endHandlerCalls == 0u);

    // A second hint is not taken; the error is the answer.
    BOOST_TEST(c.handlePacket(ResponseBuilder(NABTO_COAP_TYPE_ACK, NABTO_COAP_CODE_REQUEST_ENTITY_TOO_LARGE, sent[0].messageId, chunk0.token).block1(0, false, 1).build()) == NABTO_COAP_CLIENT_STATUS_OK);
    BOOST_TEST(c.drain().empty());
    BOOST_TEST(c.runCallback());
    BOOST_TEST(c.endStatus == NABTO_COAP_CLIENT_STATUS_OK);
    BOOST_TEST(nabto_coap_client_response_get_code(nabto_coap_client_request_get_response(c.request)) == 413);
}

// Audit N15: RFC 7959 section 3.3 annotates the combined example with
// "(no payload for requests with Block2 with NUM != 0)". Whether the body
// went out again was decided only by where the Block1 offset had reached,
// so it was right by accident for a body that went through Block1 and
// wrong for one that fit in a single block: that body, and its
// Content-Format, were repeated on every follow-up request.
BOOST_AUTO_TEST_CASE(block2_followup_request_carries_no_request_body)
{
    const std::string body = "request body";
    const std::string response(32, 'r'); // two 16 byte blocks
    TestClient c(NABTO_COAP_METHOD_POST, body);
    nabto_coap_client_request_set_content_format(c.request, 50);

    SentMessage req = c.sendRequest();
    BOOST_TEST(req.payload == body);
    BOOST_TEST(!req.hasBlock1);

    BOOST_TEST(c.handlePacket(ResponseBuilder(NABTO_COAP_TYPE_ACK, NABTO_COAP_CODE_CONTENT, req.messageId, req.token).block2(0, true, 0).payload(response.substr(0, 16)).build()) == NABTO_COAP_CLIENT_STATUS_OK);

    std::vector<SentMessage> sent = c.drain();
    BOOST_REQUIRE(sent.size() == 1);
    BOOST_TEST(sent[0].hasBlock2);
    BOOST_TEST(sent[0].block2 == blockOption(1, false, 0));
    BOOST_TEST(sent[0].payload.empty());
    BOOST_TEST(!sent[0].hasBlock1);

    BOOST_TEST(c.handlePacket(ResponseBuilder(NABTO_COAP_TYPE_ACK, NABTO_COAP_CODE_CONTENT, sent[0].messageId, req.token).block2(1, false, 0).payload(response.substr(16)).build()) == NABTO_COAP_CLIENT_STATUS_OK);
    BOOST_TEST(c.runCallback());
    BOOST_TEST(c.endStatus == NABTO_COAP_CLIENT_STATUS_OK);

    const uint8_t* payload;
    size_t payloadLength;
    BOOST_REQUIRE(nabto_coap_client_response_get_payload(nabto_coap_client_request_get_response(c.request), &payload, &payloadLength));
    BOOST_TEST(std::string((const char*)payload, payloadLength) == response);
}

// Audit N13: RFC 7959 section 2.1, a Block option must not occur twice.
// The client cannot tell which occurrence the server meant, so the
// response is rejected and reset rather than reassembled from one of them.
BOOST_AUTO_TEST_CASE(response_with_repeated_block2_option_is_reset)
{
    const std::string chunk(16, 'a');
    TestClient c;
    SentMessage req = c.sendRequest();
    BOOST_TEST(c.handlePacket(ResponseBuilder(NABTO_COAP_TYPE_CON, NABTO_COAP_CODE_CONTENT, req.messageId, req.token).block2(0, true, 0).block2(3, true, 0).payload(chunk).build()) == NABTO_COAP_CLIENT_STATUS_DECODE_ERROR);
    BOOST_TEST(c.request->response == (struct nabto_coap_client_response*)NULL);

    std::vector<SentMessage> sent = c.drain();
    BOOST_REQUIRE(sent.size() == 1);
    BOOST_TEST(sent[0].type == NABTO_COAP_TYPE_RST);
    BOOST_TEST(sent[0].messageId == req.messageId);
}

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
        // RFC 7252 section 4.2: a CON is either acked or reset. Once
        // the ack has arrived a RST with the request's id is stale and
        // the request keeps waiting for its separate response.
        TestClient c;
        SentMessage req = c.sendRequest();
        BOOST_TEST(c.handlePacket(emptyPacket(NABTO_COAP_TYPE_ACK, req.messageId)) == NABTO_COAP_CLIENT_STATUS_OK);
        BOOST_TEST(c.request->state == NABTO_COAP_CLIENT_REQUEST_STATE_WAIT_RESPONSE);
        BOOST_TEST(c.handlePacket(emptyPacket(NABTO_COAP_TYPE_RST, req.messageId)) == NABTO_COAP_CLIENT_STATUS_OK);
        BOOST_TEST(!c.runCallback());
        BOOST_TEST(c.request->state == NABTO_COAP_CLIENT_REQUEST_STATE_WAIT_RESPONSE);
        BOOST_TEST(c.handlePacket(ResponseBuilder(NABTO_COAP_TYPE_NON, NABTO_COAP_CODE_CONTENT, 0x4242, req.token).build()) == NABTO_COAP_CLIENT_STATUS_OK);
        BOOST_TEST(c.runCallback());
        BOOST_TEST(c.endHandlerCalls == 1u);
        BOOST_TEST(c.endStatus == NABTO_COAP_CLIENT_STATUS_OK);
    }
    {
        // RFC 7252 section 4.3: a NON request can be reset at any time.
        TestClient c;
        nabto_coap_client_request_set_nonconfirmable(c.request);
        SentMessage req = c.sendRequest();
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

// Stamps are ordered by their signed 32 bit difference, so a deadline
// 2^31 ms or more ahead reads as already expired. The back-off stops
// doubling before that, whatever maxRetransmits is set to, and a shift
// by 32 or more never happens.
BOOST_AUTO_TEST_CASE(ack_backoff_stops_before_the_stamp_range)
{
    {
        // A base timeout past the range is clamped to it.
        TestClient c;
        c.client.settings.ackTimeoutMilliseconds = 0x80000000u;
        c.sendRequest();
        BOOST_TEST(c.request->timeoutStamp == c.now + (1u << 30));
        nabto_coap_client_handle_timeout(&c.client, c.now + (1u << 30) - 1);
        BOOST_TEST(c.drain().empty());
        nabto_coap_client_handle_timeout(&c.client, c.now + (1u << 30));
        BOOST_TEST(c.drain().size() == 1u);
    }
    {
        // So is a response timeout past the range, for an acked CON
        // request and for a NON request.
        TestClient c;
        nabto_coap_client_request_set_timeout(c.request, 0x80000000u);
        SentMessage req = c.sendRequest();
        BOOST_TEST(c.handlePacket(emptyPacket(NABTO_COAP_TYPE_ACK, req.messageId)) == NABTO_COAP_CLIENT_STATUS_OK);
        BOOST_TEST(c.request->timeoutStamp == c.now + (1u << 30));
    }
    {
        TestClient c;
        nabto_coap_client_request_set_nonconfirmable(c.request);
        nabto_coap_client_request_set_timeout(c.request, 0x80000000u);
        c.sendRequest();
        BOOST_TEST(c.request->timeoutStamp == c.now + (1u << 30));
    }

    TestClient c;
    c.client.settings.maxRetransmits = 40;
    const uint32_t ackTimeout = c.client.settings.ackTimeoutMilliseconds;

    SentMessage req = c.sendRequest();
    uint32_t expected = ackTimeout;
    for (uint8_t retransmit = 0; retransmit < 40; retransmit++) {
        // get_next_timeout caps its wake-up hint at ~11.8 hours, so read
        // the deadline itself.
        uint32_t timeout = c.request->timeoutStamp;
        BOOST_TEST(timeout == c.now + expected);
        BOOST_TEST(expected < (1u << 31));
        // Nothing is due before the deadline.
        nabto_coap_client_handle_timeout(&c.client, c.now + expected - 1);
        BOOST_TEST(c.drain().empty());
        c.now = timeout;
        nabto_coap_client_handle_timeout(&c.client, c.now);
        std::vector<SentMessage> sent = c.drain();
        BOOST_REQUIRE(sent.size() == 1);
        BOOST_TEST(sent[0].messageId == req.messageId);
        if (expected < (1u << 30)) {
            expected *= 2;
        }
    }
    // 2000 << 20 is the first value at or above 2^30; it is held there.
    BOOST_TEST(expected == (ackTimeout << 20));
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

// A RST only ever answers a received message (RFC 7252 section 4.2),
// so neither a response timeout nor a cancel may send one carrying
// the request's own message id, whatever the request type.
BOOST_AUTO_TEST_CASE(timeout_and_cancel_never_send_rst)
{
    {
        // Response timeout of a NON request.
        TestClient c;
        nabto_coap_client_request_set_nonconfirmable(c.request);
        nabto_coap_client_request_set_timeout(c.request, 5000);
        c.sendRequest();
        c.now += 5000;
        nabto_coap_client_handle_timeout(&c.client, c.now);
        BOOST_TEST(c.drain().empty());
        BOOST_TEST(c.runCallback());
        BOOST_TEST(c.endStatus == NABTO_COAP_CLIENT_STATUS_TIMEOUT);
    }
    {
        // Response timeout of an acked CON request.
        TestClient c;
        nabto_coap_client_request_set_timeout(c.request, 5000);
        SentMessage req = c.sendRequest();
        BOOST_TEST(c.handlePacket(emptyPacket(NABTO_COAP_TYPE_ACK, req.messageId)) == NABTO_COAP_CLIENT_STATUS_OK);
        c.now += 5000;
        nabto_coap_client_handle_timeout(&c.client, c.now);
        BOOST_TEST(c.drain().empty());
        BOOST_TEST(c.runCallback());
        BOOST_TEST(c.endStatus == NABTO_COAP_CLIENT_STATUS_TIMEOUT);
    }
    {
        // Cancel of a NON request.
        TestClient c;
        nabto_coap_client_request_set_nonconfirmable(c.request);
        c.sendRequest();
        nabto_coap_client_request_cancel(c.request);
        BOOST_TEST(c.drain().empty());
        BOOST_TEST(c.runCallback());
        BOOST_TEST(c.endStatus == NABTO_COAP_CLIENT_STATUS_STOPPED);
    }
    {
        // Cancel of a CON request waiting for its ack.
        TestClient c;
        c.sendRequest();
        nabto_coap_client_request_cancel(c.request);
        BOOST_TEST(c.drain().empty());
        BOOST_TEST(c.runCallback());
        BOOST_TEST(c.endStatus == NABTO_COAP_CLIENT_STATUS_STOPPED);
    }
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
