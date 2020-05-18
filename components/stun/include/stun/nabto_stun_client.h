#ifndef NABTO_STUN_CLIENT_H
#define NABTO_STUN_CLIENT_H

#include "nabto_stun_defines.h"
#include "nabto_stun_message.h"

#ifdef __cplusplus
extern "C" {
#endif

struct nabto_stun {
    struct nabto_stun_module* module;
    struct nabto_stun_result result;
    void* moduleUserData;
    enum nabto_stun_next_event_type nextEvent;
    enum nabto_stun_state state;
    bool simple;

    struct nabto_stun_endpoint* eps;
    uint8_t numEps;
    uint16_t initialPort;
    uint8_t initialTestsSent;

    struct nabto_stun_message* nextTest;
    uint32_t nextTimeout;

    struct nabto_stun_message test1;
    struct nabto_stun_message tests[5];
    struct nabto_stun_message defectRouterTest;
};


void nabto_stun_init(struct nabto_stun* stun, struct nabto_stun_module* module, void* modUserData, struct nabto_stun_endpoint* eps, uint8_t numEps);

/**
 * If simple is set to true we only find the global ip and port and
 * skip the firewall analysis.
 */
void nabto_stun_async_analyze(struct nabto_stun* stun, bool simple);

struct nabto_stun_result* nabto_stun_get_result(struct nabto_stun* stun);

bool nabto_stun_check_transaction_id(struct nabto_stun_message* test, const uint8_t* packet, uint16_t size);

void nabto_stun_handle_packet(struct nabto_stun* stun, const uint8_t* buf, uint16_t size);

void nabto_stun_handle_wait_event(struct nabto_stun* stun);

bool nabto_stun_get_data_endpoint(struct nabto_stun* stun, struct nabto_stun_endpoint* ep);

uint16_t nabto_stun_get_send_data(struct nabto_stun* stun, uint8_t* buf, uint16_t size);

enum nabto_stun_next_event_type nabto_stun_next_event_to_handle(struct nabto_stun* stun);

uint32_t nabto_stun_get_timeout_ms(struct nabto_stun* stun);

void nabto_stun_init_tests(struct nabto_stun* stun);

void nabto_stun_set_next_test(struct nabto_stun* stun);


#ifdef __cplusplus
} // extern "C"
#endif

#endif // NABTO_STUN_CLIENT_H
