#ifndef NABTO_STUN_DEFINES_H
#define NABTO_STUN_DEFINES_H

#include "nabto_stun_types.h"
#include "nabto_stun_ip_address.h"
#include "nabto_stun_log.h"

// Some packets in the filtering tests is expexted to be dropped by
// the firewall. Retry these packets less times.
#ifndef NABTO_STUN_MAX_RETRIES_DROPPED
#define NABTO_STUN_MAX_RETRIES_DROPPED 3
#endif

// If we expect to get a response to a request try a few more times
// before giving up.
#ifndef NABTO_STUN_MAX_RETRIES_ACCEPTED
#define NABTO_STUN_MAX_RETRIES_ACCEPTED 7
#endif

#ifndef NABTO_STUN_SEND_TIMEOUT
#define NABTO_STUN_SEND_TIMEOUT 500
#endif

#ifndef NABTO_STUN_PORT
#define NABTO_STUN_PORT 3478
#endif

#ifndef NABTO_STUN_ALT_PORT
#define NABTO_STUN_ALT_PORT 3479
#endif

enum nabto_stun_socket {
    PRIMARY,
    SECONDARY
};


enum nabto_stun_state {
    STUN_NOT_STARTED,
    STUN_INITIAL_TEST,
    STUN_TESTING,
    STUN_DEFECT_ROUTER_TEST,
    STUN_COMPLETED,
    STUN_FAILED // Stun test failed
};

enum nabto_stun_next_event_type {
    STUN_ET_SEND_PRIMARY, // send packet on the primary socket
    STUN_ET_SEND_SECONDARY, // send packet on the secondarysocket
    STUN_ET_WAIT,    // wait for timeout
    STUN_ET_NO_EVENT, // Stun is not running and requires no further events
    STUN_ET_COMPLETED, // Stun test is completed, and a result is waiting
    STUN_ET_FAILED // Stun test failed
};

enum nabto_stun_nat_result {
    STUN_INDEPENDENT = 0x01,
    STUN_ADDR_DEPENDENT = 0x02,
    STUN_PORT_DEPENDENT = 0x03
};

struct nabto_stun_result {
    enum nabto_stun_nat_result mapping;
    enum nabto_stun_nat_result filtering;
    bool defectNat;
    struct nabto_stun_endpoint extEp;
};

struct nabto_stun_module {
    uint32_t (*get_stamp)(void* userData);
    // userData in front since 
    void (*log)(const char* file, int line, enum nabto_stun_log_level level, const char* fmt, va_list args, void* userData);
    // This function must put 'size' number of random bytes into 'buf'
    bool (*get_rand)(uint8_t* buf, uint16_t size, void* userData);
};    



#endif // NABTO_STUN_DEFINES_H
