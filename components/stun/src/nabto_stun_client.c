#include <stun/nabto_stun_client.h>
#include <string.h>

#ifndef NABTO_STUN_SEND_TIMEOUT
#define NABTO_STUN_SEND_TIMEOUT 500
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define LOG_MODULE "stun"

void nabto_stun_init(struct nabto_stun* stun, struct nabto_stun_module* mod, void* modUserData, struct nabto_stun_endpoint* eps, uint8_t numEps)
{
    stun->module = mod;
    stun->moduleUserData = modUserData;
    stun->eps = eps;
    stun->numEps = numEps;
    stun->state = STUN_NOT_STARTED;
    stun->nextEvent = STUN_ET_NO_EVENT;
}

void nabto_stun_async_analyze(struct nabto_stun* stun, bool simple)//, nabto_stun_analysis_callback cb, void* data)
{
//    stun->cb = cb;
//    stun->cbData = data;
    stun->simple = simple;
    stun->nextEvent = STUN_ET_SEND_PRIMARY;
    stun->state = STUN_INITIAL_TEST;
    stun->initialTestsSent = 0;
    stun->nextTimeout = NABTO_STUN_SEND_TIMEOUT;
    stun->nextTest = &stun->test1;
    nabto_stun_init_message(stun->module, &stun->test1, false, false, PRIMARY, stun->eps[0], NABTO_STUN_MAX_RETRIES_ACCEPTED, stun->moduleUserData);
}

bool nabto_stun_get_data_endpoint(struct nabto_stun* stun, struct nabto_stun_endpoint* ep)
{
    switch(stun->state) {
        case STUN_NOT_STARTED:
            return false;
        case STUN_INITIAL_TEST:
            ep->addr = stun->eps[stun->initialTestsSent].addr;
            ep->port = stun->eps[stun->initialTestsSent].port;
            return true;
        case STUN_TESTING:
            *ep = stun->nextTest->testEp;
            return true;
        case STUN_DEFECT_ROUTER_TEST:
            *ep = stun->defectRouterTest.testEp;
            return true;
        case STUN_COMPLETED:
            return false;
        default:
            return false;
    }
}

uint16_t nabto_stun_get_send_data(struct nabto_stun* stun, uint8_t* buf, uint16_t size)
{
    uint16_t ret = 0;
    switch(stun->state) {
        case STUN_INITIAL_TEST:
            if (stun->initialTestsSent < stun->numEps) {
                nabto_stun_message_reset_transaction_id(stun->module, &stun->test1, stun->moduleUserData);
                ret = nabto_stun_write_message(buf, size, &stun->test1);
                stun->test1.stamp = stun->module->get_stamp(stun->moduleUserData);
                stun->test1.state = SENT;
                if (stun->initialTestsSent == stun->numEps-1) {
                    stun->nextEvent = STUN_ET_WAIT;
                }
                stun->initialTestsSent++;
            } else {
                stun->nextEvent = STUN_ET_WAIT;
            }
            nabto_stun_set_next_test(stun);
            return ret;
        case STUN_TESTING:
            ret = nabto_stun_write_message(buf, size, stun->nextTest);
            stun->nextTest->stamp = stun->module->get_stamp(stun->moduleUserData);
            stun->nextTest->state = SENT;
            stun->nextTest->retransmissions++;
            nabto_stun_set_next_test(stun);
            return ret;

        case STUN_DEFECT_ROUTER_TEST:
            ret = nabto_stun_write_message(buf, size, &stun->defectRouterTest);
            stun->defectRouterTest.stamp = stun->module->get_stamp(stun->moduleUserData);
            stun->defectRouterTest.state = SENT;
            stun->defectRouterTest.retransmissions++;
            stun->nextEvent = STUN_ET_WAIT;
            stun->nextTimeout = NABTO_STUN_SEND_TIMEOUT;
            return ret;

        case STUN_COMPLETED:
        case STUN_NOT_STARTED:
        default:
            stun->nextEvent = STUN_ET_NO_EVENT;
            return 0;
    }
}

void nabto_stun_compute_result_and_stop(struct nabto_stun* stun)
{
    stun->result.extEp = stun->test1.mappedEp;
    if (stun->test1.mappedEp.port != stun->tests[0].mappedEp.port) {
        stun->result.mapping = STUN_PORT_DEPENDENT;
    } else if (stun->test1.mappedEp.port != stun->tests[1].mappedEp.port) {
        stun->result.mapping = STUN_ADDR_DEPENDENT;
    } else {
        stun->result.mapping = STUN_INDEPENDENT;
    }

    if (stun->tests[3].state == COMPLETED) {
        stun->result.filtering = STUN_INDEPENDENT;
    } else if (stun->tests[2].state == COMPLETED) {
        stun->result.filtering = STUN_ADDR_DEPENDENT;
    } else {
        stun->result.filtering = STUN_PORT_DEPENDENT;
    }

    stun->result.defectNat = false;
    if (stun->defectRouterTest.state == COMPLETED &&
        stun->result.mapping == STUN_INDEPENDENT &&
        (stun->defectRouterTest.mappedEp.port != stun->tests[4].mappedEp.port))
    {
        stun->result.defectNat = true;
    }
    stun->state = STUN_COMPLETED;
    stun->nextEvent = STUN_ET_COMPLETED;
}

struct nabto_stun_result* nabto_stun_get_result(struct nabto_stun* stun)
{
    if (stun->state == STUN_COMPLETED) {
        return &stun->result;
    }
    return NULL;
}

void nabto_stun_handle_wait_event(struct nabto_stun* stun)
{
    switch (stun->state) {
        case STUN_INITIAL_TEST:
            stun->initialTestsSent = 0;
            stun->nextEvent = STUN_ET_SEND_PRIMARY;
            stun->test1.retransmissions++;
            if (stun->test1.retransmissions >= stun->test1.maxRetransmissions) {
                stun->state = STUN_FAILED;
                stun->nextEvent = STUN_ET_FAILED;
            }
           return;
        case STUN_TESTING:
            nabto_stun_set_next_test(stun);
            return;
        case STUN_DEFECT_ROUTER_TEST:
            if(stun->defectRouterTest.retransmissions >= stun->defectRouterTest.maxRetransmissions) {
                stun->defectRouterTest.state = FAILED;
                stun->state = STUN_COMPLETED;
                nabto_stun_compute_result_and_stop(stun);
            } else {
                stun->nextEvent = STUN_ET_SEND_SECONDARY;
            }
            return;

        case STUN_COMPLETED:

        default:
            stun->nextEvent = STUN_ET_NO_EVENT;
    }
}

bool nabto_stun_check_transaction_id(struct nabto_stun_message* test, const uint8_t* packet, uint16_t size)
{
    if(size < 20) {
        return false;
    }
    return (memcmp(test->transactionId, packet+8, 12) == 0);
}

void nabto_stun_handle_packet(struct nabto_stun* stun, const uint8_t* buf, uint16_t size)
{
    NN_LOG_TRACE(stun->module->logger, LOG_MODULE, "nabto_stun_handle_packet, size: %u", size);
    switch(stun->state) {
        case STUN_INITIAL_TEST:
            if(nabto_stun_decode_message(&stun->test1, buf, size)) {
                stun->result.extEp = stun->test1.mappedEp;
                if (stun->simple) {
                    stun->state = STUN_COMPLETED;
                    stun->nextEvent = STUN_ET_COMPLETED;
                } else {
                    stun->state = STUN_TESTING;
                    stun->nextEvent = STUN_ET_SEND_PRIMARY;
                    nabto_stun_init_tests(stun);
                    nabto_stun_set_next_test(stun);

                }
                stun->test1.state = COMPLETED;

            } else {
                NN_LOG_TRACE(stun->module->logger, LOG_MODULE, "decode initial test message failed");
            }
            break;
        case STUN_TESTING:
            for (uint8_t i = 0; i < 5; i++) {
                if(nabto_stun_check_transaction_id(&stun->tests[i], buf, size)) {
                    // found correct transaction ID
                    if(nabto_stun_decode_message(&stun->tests[i], buf, size)) {
                        stun->tests[i].state = COMPLETED;
                        return;
                    } else {
                        NN_LOG_ERROR(stun->module->logger, LOG_MODULE, "decode test message failed");
                    }
                }
            }
            break;

        case STUN_DEFECT_ROUTER_TEST:
            if (nabto_stun_check_transaction_id(&stun->defectRouterTest, buf, size)) {
                if (nabto_stun_decode_message(&stun->defectRouterTest, buf, size)) {
                    stun->defectRouterTest.state = COMPLETED;
                    nabto_stun_compute_result_and_stop(stun);
                    return;
                } else {
                    NN_LOG_ERROR(stun->module->logger, LOG_MODULE, "decode defect router test message failed");
                }
            }
            break;

        case STUN_COMPLETED:
            break;

        default:
            break;
    }

}

void nabto_stun_init_tests(struct nabto_stun* stun)
{
    // todo: STUN_PORT should be taken from the ep used for the initial test.
    // todo: STUN_ALT_PORT should be taken from the ADDRESS_CHANGED attribute of the response to the initial test
    struct nabto_stun_endpoint s1p1 = stun->test1.serverEp;
    s1p1.port = NABTO_STUN_PORT;
    struct nabto_stun_endpoint s1p2 = stun->test1.serverEp;
    s1p2.port = NABTO_STUN_ALT_PORT;
    struct nabto_stun_endpoint s2p1 = stun->test1.altServerEp;
    s2p1.port = NABTO_STUN_PORT;

    nabto_stun_init_message(stun->module, &stun->tests[0], false, false, PRIMARY, s1p2, NABTO_STUN_MAX_RETRIES_ACCEPTED, stun->moduleUserData);
    nabto_stun_init_message(stun->module, &stun->tests[1], false, false, PRIMARY, s2p1, NABTO_STUN_MAX_RETRIES_ACCEPTED, stun->moduleUserData);
    nabto_stun_init_message(stun->module, &stun->tests[2], false, true , SECONDARY, s1p1, NABTO_STUN_MAX_RETRIES_DROPPED, stun->moduleUserData);
    nabto_stun_init_message(stun->module, &stun->tests[3], true , false, SECONDARY, s1p1, NABTO_STUN_MAX_RETRIES_DROPPED, stun->moduleUserData);
    nabto_stun_init_message(stun->module, &stun->tests[4], false , false , SECONDARY, s1p1, NABTO_STUN_MAX_RETRIES_ACCEPTED, stun->moduleUserData);
    nabto_stun_init_message(stun->module, &stun->defectRouterTest, false, false, SECONDARY, s2p1, NABTO_STUN_MAX_RETRIES_ACCEPTED, stun->moduleUserData);
}

void nabto_stun_set_next_test(struct nabto_stun* stun)
{
    uint32_t stamp = stun->module->get_stamp(stun->moduleUserData);
    uint32_t waitTime = 0;
    switch (stun->state) {
        case STUN_INITIAL_TEST:
            stun->nextTest = &stun->test1;
            stun->nextTimeout = NABTO_STUN_SEND_TIMEOUT;
            break;
        case STUN_TESTING:
            stun->nextTest = NULL;
            for (int i = 0; i < 5; i++) {
                if (stun->tests[i].state == NONE) {
                    stun->nextTest = &stun->tests[i];
                    if (stun->nextTest->sock == PRIMARY) {
                        stun->nextEvent = STUN_ET_SEND_PRIMARY;
                    } else {
                        stun->nextEvent = STUN_ET_SEND_SECONDARY;
                    }
                    return;
                } else if (stun->tests[i].retransmissions >= stun->tests[i].maxRetransmissions) {
                    stun->tests[i].state = FAILED;
                }
                if (stun->tests[i].state == SENT) {
                    stun->nextTest = &stun->tests[i];
                }
            }
            if (stun->nextTest == NULL) { // Previous for loop did not find any in send or return trough state NONE
                // All tests are either COMPLETED or FAILED.
                stun->state = STUN_DEFECT_ROUTER_TEST;
                stun->nextTest = &stun->defectRouterTest;
                stun->nextEvent = STUN_ET_SEND_SECONDARY;
                return;
            }
            for (int i = 0; i < 5; i++) {
                if (stun->tests[i].state == SENT && stun->tests[i].stamp < stun->nextTest->stamp) {
                    stun->nextTest = &stun->tests[i];
                }
            }
            waitTime = stamp - stun->nextTest->stamp;
            if (waitTime > NABTO_STUN_SEND_TIMEOUT) {
                if (stun->nextTest->sock == PRIMARY) {
                    stun->nextEvent = STUN_ET_SEND_PRIMARY;
                } else {
                    stun->nextEvent = STUN_ET_SEND_SECONDARY;
                }
                stun->nextTimeout = 0;
            } else {
                stun->nextEvent = STUN_ET_WAIT;
                stun->nextTimeout = NABTO_STUN_SEND_TIMEOUT - waitTime;
            }
            break;
        case STUN_DEFECT_ROUTER_TEST:
            stun->nextTest = &stun->defectRouterTest;
            break;
        case STUN_COMPLETED:
        default:
            stun->nextTest = NULL;
    }
}

enum nabto_stun_next_event_type nabto_stun_next_event_to_handle(struct nabto_stun* stun)
{
    return stun->nextEvent;
}

uint32_t nabto_stun_get_timeout_ms(struct nabto_stun* stun)
{
    return stun->nextTimeout;
}

#ifdef __cplusplus
} // extern "C"
#endif
