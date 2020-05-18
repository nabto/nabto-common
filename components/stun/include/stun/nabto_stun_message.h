#ifndef NABTO_STUN_MESSAGE_H
#define NABTO_STUN_MESSAGE_H

#include "nabto_stun_defines.h"

enum {
    STUN_MESSAGE_BINDING_REQUEST = 0x0001,
    STUN_MESSAGE_BINDING_RESPONSE_SUCCESS = 0x0101
};

enum {
    STUN_ATTRIBUTE_MAPPED_ADDRESS = 0x0001,
    STUN_ATTRIBUTE_RESPONSE_ADDRESS = 0x0002,
    STUN_ATTRIBUTE_CHANGE_REQUEST = 0x0003,
    STUN_ATTRIBUTE_SOURCE_ADDRESS = 0x0004,
    STUN_ATTRIBUTE_CHANGED_ADDRESS = 0x0005,
    STUN_ATTRIBUTE_XOR_MAPPED_ADDRESS = 0x0020,
    STUN_ATTRIBUTE_XOR_MAPPED_ADDRESS_ALT = 0x8020,
    STUN_ATTRIBUTE_RESPONSE_ORIGIN = 0x802b,
    STUN_ATTRIBUTE_OTHER_ADDRESS = 0x802c
};

enum {
    STUN_MAGIC_COOKIE = 0x2112A442
};

extern uint8_t STUN_MAGIC_COOKIE_BYTES[4];

enum {
    STUN_ADDRESS_FAMILY_V4 = 0x01,
    STUN_ADDRESS_FAMILY_V6 = 0x02
};

enum nabto_stun_message_state {
    NONE,
    SENT,
    FAILED,
    COMPLETED
};

struct nabto_stun_message {
    enum nabto_stun_message_state state;

    bool changeAddress;
    bool changePort;
    enum nabto_stun_socket sock;
    struct nabto_stun_endpoint testEp;

    uint8_t transactionId[12];
    uint16_t port;
    uint32_t stamp;
    uint8_t retransmissions;
    uint8_t maxRetransmissions;

    struct nabto_stun_endpoint mappedEp;
    struct nabto_stun_endpoint serverEp;
    struct nabto_stun_endpoint altServerEp;
};

void nabto_stun_init_message(const struct nabto_stun_module* mod, struct nabto_stun_message* msg, bool changeAddr, bool changePort, enum nabto_stun_socket sock, struct nabto_stun_endpoint ep, uint8_t maxRetransmissions, void* modUserData);

void nabto_stun_message_reset_transaction_id(const struct nabto_stun_module* mod, struct nabto_stun_message* msg, void* modUserData);

uint16_t nabto_stun_write_message(uint8_t* buf, uint16_t size, struct nabto_stun_message* msg);

bool nabto_stun_decode_message(struct nabto_stun_message* msg, const uint8_t* buf, uint16_t size);


#endif // NABTO_STUN_MESSAGE_H
