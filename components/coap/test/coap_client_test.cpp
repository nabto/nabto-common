#include <boost/test/unit_test.hpp>
#include <nabto_coap/nabto_coap_client.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

struct nn_allocator defaultAllocator = {
    &calloc,
    &free
};

void notifyEvent(void* userData) { (void)userData; }
void endHandler(struct nabto_coap_client_request* request, void* userData) { (void)request; (void)userData; }

/**
 * A client whose sendRequest creates a GET /test request and returns
 * the parsed packet the client would put on the wire for it.
 */
class TestClient {
 public:
    TestClient()
    {
        BOOST_REQUIRE(nabto_coap_client_init(&client, &defaultAllocator, &notifyEvent, this) == NABTO_COAP_ERROR_OK);
    }
    ~TestClient()
    {
        nabto_coap_client_destroy(&client);
    }

    struct nabto_coap_incoming_message sendRequest()
    {
        const char* path[] = { "test" };
        struct nabto_coap_client_request* request = nabto_coap_client_request_new(&client, NABTO_COAP_METHOD_GET, 1, path, &endHandler, this, connection());
        BOOST_REQUIRE(request != NULL);
        nabto_coap_client_request_send(request);
        BOOST_REQUIRE(nabto_coap_client_get_next_event(&client, 0) == NABTO_COAP_CLIENT_NEXT_EVENT_SEND);

        uint8_t buffer[1500];
        void* sentOn = NULL;
        uint8_t* ptr = nabto_coap_client_create_packet(&client, 0, buffer, buffer + sizeof(buffer), &sentOn);
        BOOST_REQUIRE(ptr != NULL);
        BOOST_TEST(sentOn == connection());
        struct nabto_coap_incoming_message msg;
        BOOST_REQUIRE(nabto_coap_parse_message(buffer, ptr - buffer, &msg));
        // the token lives in msg itself, so it survives the free
        nabto_coap_client_request_free(request);
        return msg;
    }

    void* connection() { return &connectionMarker; }

    struct nabto_coap_client client;
    int connectionMarker;
};

std::string tokenOf(const struct nabto_coap_incoming_message& msg)
{
    return std::string((const char*)msg.token.token, msg.token.tokenLength);
}

std::string tokenOf(uint64_t counter)
{
    return std::string((const char*)&counter, sizeof(counter));
}

} // namespace

BOOST_AUTO_TEST_SUITE(coap_client)

// RFC 7252 section 4.4: the initial message id should be randomized,
// section 5.3.1: tokens should be nontrivial and randomized. The
// values set by the integrator are used for the first request and
// incremented from there.
BOOST_AUTO_TEST_CASE(initial_message_id_and_token_are_used_for_requests)
{
    TestClient c;
    nabto_coap_client_set_initial_ids(&c.client, 0xa5c3, 0x0123456789abcdefULL);

    struct nabto_coap_incoming_message msg = c.sendRequest();
    BOOST_TEST(msg.type == NABTO_COAP_TYPE_CON);
    BOOST_TEST(msg.messageId == 0xa5c3);
    BOOST_TEST(msg.token.tokenLength == 8);
    BOOST_TEST(tokenOf(msg) == tokenOf(0x0123456789abcdefULL));

    msg = c.sendRequest();
    BOOST_TEST(msg.messageId == 0xa5c4);
    BOOST_TEST(tokenOf(msg) == tokenOf(0x0123456789abcdf0ULL));
}

// The message id is a 16 bit counter and wraps.
BOOST_AUTO_TEST_CASE(message_id_wraps)
{
    TestClient c;
    nabto_coap_client_set_initial_ids(&c.client, 0xffff, 1);

    BOOST_TEST(c.sendRequest().messageId == 0xffff);
    BOOST_TEST(c.sendRequest().messageId == 0x0000);
}

BOOST_AUTO_TEST_SUITE_END()
