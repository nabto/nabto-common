#include <boost/test/unit_test.hpp>
#include <nabto_stun/nabto_stun_client.h>

#include <cstring>
#include <random>
#include <vector>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t MAGIC_COOKIE[4] = { 0x21, 0x12, 0xA4, 0x42 };
const uint8_t* nabto_stun_read_uint16(const uint8_t* ptr, const uint8_t* end, uint16_t* val);
const uint8_t* nabto_stun_read_uint32(const uint8_t* ptr, const uint8_t* end, uint32_t* val);
uint8_t* nabto_stun_uint16_write_forward(uint8_t* buf, uint8_t* end, uint16_t val);
uint8_t* nabto_stun_uint32_write_forward(uint8_t* buf, uint8_t* end, uint32_t val);

#ifdef __cplusplus
} //extern "C"
#endif




namespace nabto {
namespace test {

class StunTestFixture {
 public:
    StunTestFixture()
    {
        logger_.logPrint = &StunTestFixture::logPrint;
        logger_.userData = this;
        module.get_stamp = &StunTestFixture::moduleGetStamp;
        module.logger = &logger_;
        module.get_rand = &StunTestFixture::moduleGetRand;
        eps[0].ip = s1Ip;
        eps[0].port = sp1;
        eps[1].ip = s2Ip;
        eps[1].port = sp2;
    }

    void startStunAnalysis(bool simple)
    {
        lastTransId = 0;
        now = 0;
        nabto_stun_init(&stun_, &module, this, eps, 2);
        nabto_stun_async_analyze(&stun_, simple);
    }

    void validateMessage(struct nn_ip_address ip, uint16_t port, bool changeAddr, bool changePort)
    {
        if(!nabto_stun_get_data_endpoint(&stun_, &dst)) {
            BOOST_REQUIRE(false);
            return;
        }
        BOOST_TEST(dst.ip.type == ip.type);
        BOOST_TEST(dst.ip.ip.v4[0] == ip.ip.v4[0]);
        BOOST_TEST(dst.ip.ip.v4[1] == ip.ip.v4[1]);
        BOOST_TEST(dst.ip.ip.v4[2] == ip.ip.v4[2]);
        BOOST_TEST(dst.ip.ip.v4[3] == ip.ip.v4[3]);
        BOOST_TEST(dst.port == port);

        reqSize = nabto_stun_get_send_data(&stun_, reqBuf, 512);
        BOOST_REQUIRE(reqSize == 28);
        memcpy(lastReqId, reqBuf + 8, 12);
        uint16_t tmp;
        BOOST_TEST((nabto_stun_read_uint16(reqBuf, reqBuf+reqSize, &tmp) != NULL));
        BOOST_TEST(tmp == STUN_MESSAGE_BINDING_REQUEST);
        BOOST_TEST((nabto_stun_read_uint16(reqBuf+2, reqBuf+reqSize, &tmp) != NULL));
        BOOST_TEST(tmp == 8);
        BOOST_TEST(reqBuf[4] == MAGIC_COOKIE[0]);
        BOOST_TEST(reqBuf[5] == MAGIC_COOKIE[1]);
        BOOST_TEST(reqBuf[6] == MAGIC_COOKIE[2]);
        BOOST_TEST(reqBuf[7] == MAGIC_COOKIE[3]);
        BOOST_TEST((nabto_stun_read_uint16(reqBuf+20, reqBuf+reqSize, &tmp) != NULL));
        BOOST_TEST(tmp == STUN_ATTRIBUTE_CHANGE_REQUEST);
        BOOST_TEST((nabto_stun_read_uint16(reqBuf+22, reqBuf+reqSize, &tmp) != NULL));
        BOOST_TEST(tmp == 4);
        uint32_t bits = 0;
        if (changePort) {
            bits |= (1<<1);
        }
        if (changeAddr) {
            bits |= (1<<2);
        }
        uint32_t msgBits;
        BOOST_TEST((nabto_stun_read_uint32(reqBuf+24, reqBuf+reqSize, &msgBits) != NULL));
        BOOST_TEST(msgBits == bits);
    }

    void writeResponse(uint8_t id, struct nn_endpoint mapped,
                       struct nn_endpoint origin, struct nn_endpoint other)
    {
        uint8_t transactionId[12] = { id };
        writeResponse(transactionId, mapped, origin, other);
    }

    void writeResponse(const uint8_t* transactionId, struct nn_endpoint mapped,
                       struct nn_endpoint origin, struct nn_endpoint other)
    {
        memset(respBuf, 0, 512);
        uint8_t* ptr = respBuf;
        uint8_t* end = respBuf + sizeof(respBuf);
        ptr = nabto_stun_uint16_write_forward(ptr, end, STUN_MESSAGE_BINDING_RESPONSE_SUCCESS);
        ptr = nabto_stun_uint16_write_forward(ptr, end, 36);
        ptr = nabto_stun_uint32_write_forward(ptr, end, STUN_MAGIC_COOKIE);
        memcpy(ptr, transactionId, 12); ptr += 12;
        ptr = nabto_stun_uint16_write_forward(ptr, end, STUN_ATTRIBUTE_RESPONSE_ORIGIN);
        ptr = nabto_stun_uint16_write_forward(ptr, end, 8);
        ptr = nabto_stun_uint16_write_forward(ptr, end, STUN_ADDRESS_FAMILY_V4);
        ptr = nabto_stun_uint16_write_forward(ptr, end, origin.port);
        *ptr = origin.ip.ip.v4[0]; ptr++;
        *ptr = origin.ip.ip.v4[1]; ptr++;
        *ptr = origin.ip.ip.v4[2]; ptr++;
        *ptr = origin.ip.ip.v4[3]; ptr++;
        ptr = nabto_stun_uint16_write_forward(ptr, end, STUN_ATTRIBUTE_OTHER_ADDRESS);
        ptr = nabto_stun_uint16_write_forward(ptr, end, 8);
        ptr = nabto_stun_uint16_write_forward(ptr, end, STUN_ADDRESS_FAMILY_V4);
        ptr = nabto_stun_uint16_write_forward(ptr, end, other.port);
        *ptr = other.ip.ip.v4[0]; ptr++;
        *ptr = other.ip.ip.v4[1]; ptr++;
        *ptr = other.ip.ip.v4[2]; ptr++;
        *ptr = other.ip.ip.v4[3]; ptr++;
        ptr = nabto_stun_uint16_write_forward(ptr, end, STUN_ATTRIBUTE_XOR_MAPPED_ADDRESS_ALT);
        ptr = nabto_stun_uint16_write_forward(ptr, end, 8);
        ptr = nabto_stun_uint16_write_forward(ptr, end, STUN_ADDRESS_FAMILY_V4);
        ptr = nabto_stun_uint16_write_forward(ptr, end, mapped.port^(STUN_MAGIC_COOKIE >> 16));
        *ptr = mapped.ip.ip.v4[0]^MAGIC_COOKIE[0]; ptr++;
        *ptr = mapped.ip.ip.v4[1]^MAGIC_COOKIE[1]; ptr++;
        *ptr = mapped.ip.ip.v4[2]^MAGIC_COOKIE[2]; ptr++;
        *ptr = mapped.ip.ip.v4[3]^MAGIC_COOKIE[3]; ptr++;
        respSize = ptr-respBuf;
    }

    static uint32_t moduleGetStamp(void* data)
    {
        StunTestFixture* self = (StunTestFixture*)data;
        return self->now;
    }

    static bool moduleGetRand(uint8_t* buf, uint16_t size, void* data)
    {
        StunTestFixture* self = (StunTestFixture*)data;
        self->lastTransId++;
        memset(buf, 0, size);
        buf[0] = self->lastTransId;
        return true;
    }

    static void logPrint(void* userData, enum nn_log_severity severity, const char* module, const char* file, int line, const char* fmt, va_list args)
    {

    }

    uint8_t lastTransId = 0; // the first rand gives 1, which the initial test uses for every endpoint and retransmission
    struct nn_endpoint dst;
    uint8_t reqBuf[512];
    uint8_t lastReqId[12]; // transaction id of the last request written by validateMessage
    uint16_t reqSize;
    uint8_t respBuf[512];
    uint16_t respSize;
    struct nabto_stun stun_;
    struct nn_endpoint eps[2];
    struct nabto_stun_module module;
    struct nn_log logger_;
    uint32_t now = 0;


    struct nn_ip_address mappedIp = {NN_IPV4, { {125, 0, 0, 1 }}};
    struct nn_ip_address s1Ip = {NN_IPV4, { {126, 0, 0, 1} }};
    struct nn_ip_address s2Ip = {NN_IPV4, { {127, 0, 0, 1} }};
    uint16_t mp1 = 4242;
    uint16_t mp2 = 4343;
    uint16_t sp1 = 4444;
    uint16_t sp2 = 4545;
};

} } // namespaces




BOOST_FIXTURE_TEST_SUITE(stun, nabto::test::StunTestFixture)

BOOST_AUTO_TEST_CASE(client_simple)
{
    struct nn_endpoint ep = {mappedIp, mp1};
    startStunAnalysis(true);
    enum nabto_stun_next_event_type event = nabto_stun_next_event_to_handle(&stun_);
    BOOST_REQUIRE(event == STUN_ET_SEND_PRIMARY);
    validateMessage(eps[0].ip, eps[0].port, false, false);
    BOOST_TEST(reqBuf[8] == 1); // the initial test has one trans ID
    event = nabto_stun_next_event_to_handle(&stun_);
    BOOST_TEST(event == STUN_ET_SEND_PRIMARY);
    validateMessage(eps[1].ip, eps[1].port, false, false);
    BOOST_TEST(reqBuf[8] == 1); // the same one for every endpoint
    event = nabto_stun_next_event_to_handle(&stun_);
    BOOST_TEST(event == STUN_ET_WAIT);

    writeResponse(1, ep, eps[0], eps[1]);
    nabto_stun_handle_packet(&stun_, respBuf, respSize);
    event = nabto_stun_next_event_to_handle(&stun_);
    BOOST_TEST(event == STUN_ET_COMPLETED);
    struct nabto_stun_result* r = nabto_stun_get_result(&stun_);
    BOOST_TEST(r->extEp.port == ep.port);
    for (int i = 0; i < 4; i++) {
        BOOST_TEST(r->extEp.ip.ip.v4[i] == ep.ip.ip.v4[i]);
    }
}

BOOST_AUTO_TEST_CASE(client_simple_missing_attribute)
{
    struct nn_endpoint ep = {mappedIp, mp1};
    startStunAnalysis(true);
    enum nabto_stun_next_event_type event = nabto_stun_next_event_to_handle(&stun_);
    BOOST_REQUIRE(event == STUN_ET_SEND_PRIMARY);
    validateMessage(eps[0].ip, eps[0].port, false, false);
    BOOST_TEST(reqBuf[8] == 1); // the initial test has one trans ID
    event = nabto_stun_next_event_to_handle(&stun_);
    BOOST_TEST(event == STUN_ET_SEND_PRIMARY);
    validateMessage(eps[1].ip, eps[1].port, false, false);
    BOOST_TEST(reqBuf[8] == 1); // the same one for every endpoint
    event = nabto_stun_next_event_to_handle(&stun_);
    BOOST_TEST(event == STUN_ET_WAIT);

    writeResponse(1, ep, eps[0], eps[1]);
    nabto_stun_handle_packet(&stun_, respBuf, respSize-12);
    event = nabto_stun_next_event_to_handle(&stun_);
    BOOST_TEST(event == STUN_ET_WAIT);
    nabto_stun_handle_packet(&stun_, respBuf, respSize);
    event = nabto_stun_next_event_to_handle(&stun_);
    BOOST_TEST(event == STUN_ET_COMPLETED);
    struct nabto_stun_result* r = nabto_stun_get_result(&stun_);
    BOOST_TEST(r->extEp.port == ep.port);
    for (int i = 0; i < 4; i++) {
        BOOST_TEST(r->extEp.ip.ip.v4[i] == ep.ip.ip.v4[i]);
    }
}

BOOST_AUTO_TEST_CASE(client_retry_initial_packet)
{
    startStunAnalysis(true);
    enum nabto_stun_next_event_type event;
    for (int n = 0; n < NABTO_STUN_MAX_RETRIES_ACCEPTED; n++) {
        event = nabto_stun_next_event_to_handle(&stun_);
        BOOST_REQUIRE(event == STUN_ET_SEND_PRIMARY);
        validateMessage(eps[0].ip, eps[0].port, false, false);
        BOOST_TEST(reqBuf[8] == 1); // retransmissions keep the trans ID
        event = nabto_stun_next_event_to_handle(&stun_);
        BOOST_TEST(event == STUN_ET_SEND_PRIMARY);
        validateMessage(eps[1].ip, eps[1].port, false, false);
        BOOST_TEST(reqBuf[8] == 1);
        event = nabto_stun_next_event_to_handle(&stun_);
        BOOST_TEST(event == STUN_ET_WAIT);
        nabto_stun_handle_wait_event(&stun_); // handle wait does not verify timeout for initial test so no need to advance `now`
    }
    event = nabto_stun_next_event_to_handle(&stun_);
    BOOST_REQUIRE(event == STUN_ET_FAILED);
}

BOOST_AUTO_TEST_CASE(client_retry_initial_packet_continue)
{
    struct nn_endpoint ep = {mappedIp, mp1};
    startStunAnalysis(true);
    enum nabto_stun_next_event_type event;
    event = nabto_stun_next_event_to_handle(&stun_);
    BOOST_REQUIRE(event == STUN_ET_SEND_PRIMARY);
    validateMessage(eps[0].ip, eps[0].port, false, false);
    BOOST_TEST(reqBuf[8] == 1); // the initial test has one trans ID
    event = nabto_stun_next_event_to_handle(&stun_);
    BOOST_TEST(event == STUN_ET_SEND_PRIMARY);
    validateMessage(eps[1].ip, eps[1].port, false, false);
    BOOST_TEST(reqBuf[8] == 1); // the same one for every endpoint
    event = nabto_stun_next_event_to_handle(&stun_);
    BOOST_TEST(event == STUN_ET_WAIT);
    nabto_stun_handle_wait_event(&stun_); // handle wait does not verify timeout for initial test so no need to advance `now`

    event = nabto_stun_next_event_to_handle(&stun_);
    BOOST_REQUIRE(event == STUN_ET_SEND_PRIMARY);
    validateMessage(eps[0].ip, eps[0].port, false, false);
    BOOST_TEST(reqBuf[8] == 1); // retransmissions keep the trans ID
    event = nabto_stun_next_event_to_handle(&stun_);
    BOOST_TEST(event == STUN_ET_SEND_PRIMARY);
    validateMessage(eps[1].ip, eps[1].port, false, false);
    BOOST_TEST(reqBuf[8] == 1);
    event = nabto_stun_next_event_to_handle(&stun_);
    BOOST_TEST(event == STUN_ET_WAIT);

    writeResponse(1, ep, eps[0], eps[1]);
    nabto_stun_handle_packet(&stun_, respBuf, respSize);
    event = nabto_stun_next_event_to_handle(&stun_);
    BOOST_TEST(event == STUN_ET_COMPLETED);
    struct nabto_stun_result* r = nabto_stun_get_result(&stun_);
    BOOST_TEST(r->extEp.port == ep.port);
    for (int i = 0; i < 4; i++) {
        BOOST_TEST(r->extEp.ip.ip.v4[i] == ep.ip.ip.v4[i]);
    }
}

BOOST_AUTO_TEST_CASE(client_full_cone_nat)
{
    // Full cone:
    // local port P is mapped to Pm even if this mapping exists for other remote IP/Ports
    // opening mapping P->Pm lets packets from S<ANY>P<ANY> to port Pm through the router
    struct nn_endpoint ep = {mappedIp, mp1};
    startStunAnalysis(false);
    // initial test
    enum nabto_stun_next_event_type event = nabto_stun_next_event_to_handle(&stun_);
    BOOST_REQUIRE(event == STUN_ET_SEND_PRIMARY);
    validateMessage(eps[0].ip, eps[0].port, false, false);
    BOOST_TEST(reqBuf[8] == 1); // the initial test has one trans ID
    event = nabto_stun_next_event_to_handle(&stun_);
    BOOST_TEST(event == STUN_ET_SEND_PRIMARY);
    validateMessage(eps[1].ip, eps[1].port, false, false);
    BOOST_TEST(reqBuf[8] == 1); // the same one for every endpoint
    event = nabto_stun_next_event_to_handle(&stun_);
    BOOST_TEST(event == STUN_ET_WAIT);
    writeResponse(1, ep, eps[0], eps[1]);
    nabto_stun_handle_packet(&stun_, respBuf, respSize);

    // test 2
    event = nabto_stun_next_event_to_handle(&stun_);
    BOOST_TEST(event == STUN_ET_SEND_PRIMARY);
    validateMessage(eps[0].ip, sp2, false, false);
    BOOST_TEST(reqBuf[8] == 2);
    writeResponse(2, ep, eps[0], eps[1]);
    nabto_stun_handle_packet(&stun_, respBuf, respSize);

    // test 3
    event = nabto_stun_next_event_to_handle(&stun_);
    BOOST_TEST(event == STUN_ET_SEND_PRIMARY);
    validateMessage(s2Ip, sp1, false, false);
    BOOST_TEST(reqBuf[8] == 3);
    writeResponse(3, ep, eps[1], eps[0]);
    nabto_stun_handle_packet(&stun_, respBuf, respSize);

    // test 4
    event = nabto_stun_next_event_to_handle(&stun_);
    BOOST_TEST(event == STUN_ET_SEND_SECONDARY);
    validateMessage(eps[0].ip, sp1, false, true);
    BOOST_TEST(reqBuf[8] == 4);
    writeResponse(4, ep, eps[0], eps[1]);
    nabto_stun_handle_packet(&stun_, respBuf, respSize);

    // test 5
    event = nabto_stun_next_event_to_handle(&stun_);
    BOOST_TEST(event == STUN_ET_SEND_SECONDARY);
    validateMessage(eps[0].ip, sp1, true, false);
    BOOST_TEST(reqBuf[8] == 5);
    writeResponse(5, ep, eps[1], eps[0]);
    nabto_stun_handle_packet(&stun_, respBuf, respSize);

    // test 6
    event = nabto_stun_next_event_to_handle(&stun_);
    BOOST_TEST(event == STUN_ET_SEND_SECONDARY);
    validateMessage(eps[0].ip, sp1, false, false);
    BOOST_TEST(reqBuf[8] == 6);

    // tests 2-6 is sent in parallel so we do not expect wait untill now
    event = nabto_stun_next_event_to_handle(&stun_);
    BOOST_TEST(event == STUN_ET_WAIT);

    writeResponse(6, ep, eps[0], eps[1]);
    nabto_stun_handle_packet(&stun_, respBuf, respSize);

    // test 7
    event = nabto_stun_next_event_to_handle(&stun_);
    BOOST_TEST(event == STUN_ET_SEND_SECONDARY);
    validateMessage(eps[1].ip, sp1, false, false);
    BOOST_TEST(reqBuf[8] == 7);
    writeResponse(7, ep, eps[1], eps[0]);
    nabto_stun_handle_packet(&stun_, respBuf, respSize);

    event = nabto_stun_next_event_to_handle(&stun_);
    BOOST_TEST(event == STUN_ET_COMPLETED);

    struct nabto_stun_result* r = nabto_stun_get_result(&stun_);
    BOOST_TEST(r->extEp.port == ep.port);
    for (int i = 0; i < 4; i++) {
        BOOST_TEST(r->extEp.ip.ip.v4[i] == ep.ip.ip.v4[i]);
    }
    BOOST_TEST(r->mapping == STUN_INDEPENDENT);
    BOOST_TEST(r->filtering == STUN_INDEPENDENT);
    BOOST_TEST(r->defectNat == false);
}

BOOST_AUTO_TEST_CASE(client_initial_test_ignores_wrong_transaction_id)
{
    // A binding response that does not carry the transaction id of the
    // initial request is not ours and must not start the nat analysis with
    // whatever server addresses it names.
    struct nn_endpoint ep = {mappedIp, mp1};
    struct nn_endpoint spoofed = {{NN_IPV4, { {10, 0, 0, 1} }}, 3478};
    startStunAnalysis(false);
    BOOST_REQUIRE(nabto_stun_next_event_to_handle(&stun_) == STUN_ET_SEND_PRIMARY);
    validateMessage(eps[0].ip, eps[0].port, false, false);
    BOOST_REQUIRE(nabto_stun_next_event_to_handle(&stun_) == STUN_ET_SEND_PRIMARY);
    validateMessage(eps[1].ip, eps[1].port, false, false);
    BOOST_REQUIRE(nabto_stun_next_event_to_handle(&stun_) == STUN_ET_WAIT);

    uint8_t wrongId[12];
    memcpy(wrongId, lastReqId, 12);
    wrongId[11] ^= 0x01;
    writeResponse(wrongId, ep, spoofed, spoofed);
    nabto_stun_handle_packet(&stun_, respBuf, respSize);
    BOOST_TEST(nabto_stun_next_event_to_handle(&stun_) == STUN_ET_WAIT);
    BOOST_TEST(nabto_stun_get_result(&stun_) == (struct nabto_stun_result*)NULL);
    BOOST_TEST(!nabto_stun_get_data_endpoint(&stun_, &dst));

    // the real response is still accepted and test 2 goes to the real server
    writeResponse(lastReqId, ep, eps[0], eps[1]);
    nabto_stun_handle_packet(&stun_, respBuf, respSize);
    BOOST_TEST(nabto_stun_next_event_to_handle(&stun_) == STUN_ET_SEND_PRIMARY);
    validateMessage(eps[0].ip, sp2, false, false);
}

BOOST_AUTO_TEST_CASE(client_initial_test_ignores_wrong_magic_cookie)
{
    struct nn_endpoint ep = {mappedIp, mp1};
    startStunAnalysis(true);
    BOOST_REQUIRE(nabto_stun_next_event_to_handle(&stun_) == STUN_ET_SEND_PRIMARY);
    validateMessage(eps[0].ip, eps[0].port, false, false);
    BOOST_REQUIRE(nabto_stun_next_event_to_handle(&stun_) == STUN_ET_SEND_PRIMARY);
    validateMessage(eps[1].ip, eps[1].port, false, false);
    BOOST_REQUIRE(nabto_stun_next_event_to_handle(&stun_) == STUN_ET_WAIT);

    writeResponse(lastReqId, ep, eps[0], eps[1]);
    respBuf[4] ^= 0xff;
    nabto_stun_handle_packet(&stun_, respBuf, respSize);
    BOOST_TEST(nabto_stun_next_event_to_handle(&stun_) == STUN_ET_WAIT);
    BOOST_TEST(nabto_stun_get_result(&stun_) == (struct nabto_stun_result*)NULL);

    respBuf[4] ^= 0xff;
    nabto_stun_handle_packet(&stun_, respBuf, respSize);
    BOOST_TEST(nabto_stun_next_event_to_handle(&stun_) == STUN_ET_COMPLETED);
    struct nabto_stun_result* r = nabto_stun_get_result(&stun_);
    BOOST_REQUIRE(r != NULL);
    BOOST_TEST(r->extEp.port == ep.port);
}

BOOST_AUTO_TEST_CASE(client_initial_test_accepts_response_for_second_endpoint)
{
    // The initial request goes to every endpoint; a response from the second
    // one, carrying the id of the request sent to it, completes the test.
    struct nn_endpoint ep = {mappedIp, mp1};
    startStunAnalysis(true);
    BOOST_REQUIRE(nabto_stun_next_event_to_handle(&stun_) == STUN_ET_SEND_PRIMARY);
    validateMessage(eps[0].ip, eps[0].port, false, false);
    uint8_t firstId[12];
    memcpy(firstId, lastReqId, 12);
    BOOST_REQUIRE(nabto_stun_next_event_to_handle(&stun_) == STUN_ET_SEND_PRIMARY);
    validateMessage(eps[1].ip, eps[1].port, false, false);
    BOOST_TEST(memcmp(firstId, lastReqId, 12) == 0);
    BOOST_REQUIRE(nabto_stun_next_event_to_handle(&stun_) == STUN_ET_WAIT);

    writeResponse(lastReqId, ep, eps[1], eps[0]);
    nabto_stun_handle_packet(&stun_, respBuf, respSize);
    BOOST_TEST(nabto_stun_next_event_to_handle(&stun_) == STUN_ET_COMPLETED);
    struct nabto_stun_result* r = nabto_stun_get_result(&stun_);
    BOOST_REQUIRE(r != NULL);
    BOOST_TEST(r->extEp.port == ep.port);
    for (int i = 0; i < 4; i++) {
        BOOST_TEST(r->extEp.ip.ip.v4[i] == ep.ip.ip.v4[i]);
    }
}

BOOST_AUTO_TEST_CASE(client_retry_keeps_transaction_id)
{
    // rfc 5389 section 6: resends of the same request reuse the same
    // transaction id, so a late response to the first round is accepted.
    struct nn_endpoint ep = {mappedIp, mp1};
    startStunAnalysis(true);
    BOOST_REQUIRE(nabto_stun_next_event_to_handle(&stun_) == STUN_ET_SEND_PRIMARY);
    validateMessage(eps[0].ip, eps[0].port, false, false);
    uint8_t firstId[12];
    memcpy(firstId, lastReqId, 12);
    BOOST_REQUIRE(nabto_stun_next_event_to_handle(&stun_) == STUN_ET_SEND_PRIMARY);
    validateMessage(eps[1].ip, eps[1].port, false, false);
    BOOST_REQUIRE(nabto_stun_next_event_to_handle(&stun_) == STUN_ET_WAIT);
    nabto_stun_handle_wait_event(&stun_);

    BOOST_REQUIRE(nabto_stun_next_event_to_handle(&stun_) == STUN_ET_SEND_PRIMARY);
    validateMessage(eps[0].ip, eps[0].port, false, false);
    BOOST_TEST(memcmp(firstId, lastReqId, 12) == 0);
    BOOST_REQUIRE(nabto_stun_next_event_to_handle(&stun_) == STUN_ET_SEND_PRIMARY);
    validateMessage(eps[1].ip, eps[1].port, false, false);
    BOOST_TEST(memcmp(firstId, lastReqId, 12) == 0);
    BOOST_REQUIRE(nabto_stun_next_event_to_handle(&stun_) == STUN_ET_WAIT);

    writeResponse(firstId, ep, eps[0], eps[1]);
    nabto_stun_handle_packet(&stun_, respBuf, respSize);
    BOOST_TEST(nabto_stun_next_event_to_handle(&stun_) == STUN_ET_COMPLETED);
}

BOOST_AUTO_TEST_CASE(client_no_data_endpoint_after_last_initial_send)
{
    // Once every initial request has been sent the client waits, and there
    // is no endpoint to send to. The endpoints live in a heap array of exactly
    // numEps entries so an index of numEps is visible to the sanitizer.
    std::vector<struct nn_endpoint> heapEps(eps, eps + 2);
    nabto_stun_init(&stun_, &module, this, heapEps.data(), (uint8_t)heapEps.size());
    nabto_stun_async_analyze(&stun_, true);
    for (size_t i = 0; i < heapEps.size(); i++) {
        BOOST_REQUIRE(nabto_stun_next_event_to_handle(&stun_) == STUN_ET_SEND_PRIMARY);
        BOOST_TEST(nabto_stun_get_data_endpoint(&stun_, &dst));
        BOOST_TEST(dst.port == heapEps[i].port);
        BOOST_TEST(nabto_stun_get_send_data(&stun_, reqBuf, sizeof(reqBuf)) == STUN_BINDING_REQUEST_SIZE);
    }
    BOOST_TEST(nabto_stun_next_event_to_handle(&stun_) == STUN_ET_WAIT);
    BOOST_TEST(!nabto_stun_get_data_endpoint(&stun_, &dst));
}

BOOST_AUTO_TEST_CASE(client_get_send_data_rejects_too_small_buffer)
{
    startStunAnalysis(true);
    BOOST_REQUIRE(nabto_stun_next_event_to_handle(&stun_) == STUN_ET_SEND_PRIMARY);
    std::vector<uint8_t> buf(STUN_BINDING_REQUEST_SIZE - 1);
    BOOST_TEST(nabto_stun_get_send_data(&stun_, buf.data(), (uint16_t)buf.size()) == 0);
}

BOOST_AUTO_TEST_SUITE_END()
