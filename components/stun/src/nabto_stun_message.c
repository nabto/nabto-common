
#include <stun/nabto_stun_message.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t STUN_MAGIC_COOKIE_BYTES[4] = { 0x21, 0x12, 0xA4, 0x42 };

uint8_t* nabto_stun_buf_write_forward(uint8_t* buf, uint8_t* val, uint16_t size)
{
    int i;
    for (i = 0; i < size; i++) {
        *(buf+i) = *(val+i);
    }
    return buf+size;
}

uint8_t* nabto_stun_uint16_write_forward(uint8_t* buf, uint16_t val)
{
    uint8_t d0 = (uint8_t)(((val) >> 8) & 0xff);
    uint8_t d1 = (uint8_t)( (val)       & 0xff);
    *buf = d0;
    buf++;
    *buf = d1;
    buf++;
    return buf;
}

uint8_t* nabto_stun_uint32_write_forward(uint8_t* buf, uint32_t val)
{
    uint8_t d0 = (uint8_t)(((val) >> 24) & 0xff);
    uint8_t d1 = (uint8_t)(((val) >> 16) & 0xff);
    uint8_t d2 = (uint8_t)(((val) >> 8)  & 0xff);
    uint8_t d3 = (uint8_t)( (val)        & 0xff);
    *buf = d0;
    buf++;
    *buf = d1;
    buf++;
    *buf = d2;
    buf++;
    *buf = d3;
    buf++;
    return buf;
}

uint16_t nabto_stun_uint16_read(const uint8_t* buf)
{
    uint16_t res = ((uint16_t)(buf[0])) << 8;
    return res + (uint16_t)(buf[1]);
}

uint32_t nabto_stun_uint32_read(const uint8_t* buf)
{
    uint32_t res = ((uint32_t)buf[0]) << 24;
    res = res + (((uint32_t)buf[1]) << 16);
    res = res + (((uint32_t)buf[2]) << 8);
    res = res + ((uint32_t)buf[3]);
    return res;
}

void nabto_stun_init_message(const struct nabto_stun_module* mod, struct nabto_stun_message* msg, bool changeAddr, bool changePort, enum nabto_stun_socket sock, struct nabto_stun_endpoint ep, uint8_t maxRetransmissions, void* modUserData)
{
    msg->state = NONE;
    msg->changeAddress = changeAddr;
    msg->changePort = changePort;
    msg->testEp = ep;
    msg->sock = sock;
    mod->get_rand(msg->transactionId, 12, modUserData);
    msg->retransmissions = 0;
    msg->maxRetransmissions = maxRetransmissions;
}

void nabto_stun_message_reset_transaction_id(const struct nabto_stun_module* mod, struct nabto_stun_message* msg, void* data)
{
    mod->get_rand(msg->transactionId, 12, data);
}

uint16_t nabto_stun_write_message(uint8_t* buf, uint16_t size, struct nabto_stun_message* msg)
{
    uint8_t* ptr = buf;
    uint32_t bits = 0;

    ptr = nabto_stun_uint16_write_forward(ptr, STUN_MESSAGE_BINDING_REQUEST);
    ptr = nabto_stun_uint16_write_forward(ptr, 8);
    ptr = nabto_stun_uint32_write_forward(ptr, STUN_MAGIC_COOKIE);
    ptr = nabto_stun_buf_write_forward(ptr, msg->transactionId, 12);

    if (msg->changePort) {
        bits |= (1<<1);
    }
    if (msg->changeAddress) {
        bits |= (1<<2);
    }
    ptr = nabto_stun_uint16_write_forward(ptr, STUN_ATTRIBUTE_CHANGE_REQUEST);
    ptr = nabto_stun_uint16_write_forward(ptr, 4);
    ptr = nabto_stun_uint32_write_forward(ptr, bits);
    return (uint16_t)(ptr-buf);
}

bool nabto_stun_parse_address(const uint8_t* buf, struct nabto_stun_endpoint* ep)
{
    uint16_t family = nabto_stun_uint16_read(buf);
    uint16_t port = nabto_stun_uint16_read(buf + 2);
    ep->port = port;
    if (family == STUN_ADDRESS_FAMILY_V4) {
        ep->addr.type = NABTO_STUN_IPV4;
        memcpy(ep->addr.v4.addr,(buf + 4), 4);
        return true;
    } else if (family == STUN_ADDRESS_FAMILY_V6) {
        //TODO: PARSE IPv6 AND RETURN TRUE
        return false;
    } else {
        return false;
    }
}

// TODO: use logging from nabto_stun_log instead of printf
#include <stdio.h>
bool nabto_stun_decode_message(struct nabto_stun_message* msg, const uint8_t* buf, uint16_t size)
{
    const uint8_t* ptr = buf;
    uint16_t type = 0;
    uint16_t length = 0;
    uint16_t n = 0;
    if (size < 20) {
        return false;
    }
    type = nabto_stun_uint16_read(ptr);
    ptr += 2;
    length = nabto_stun_uint16_read(ptr);
    ptr += 2;
    if (type != STUN_MESSAGE_BINDING_RESPONSE_SUCCESS) {
        printf("not binding response\n");
        return false;
    }
    ptr += 16;
    while (n < length) {
        uint16_t attType = nabto_stun_uint16_read(ptr+n);
        uint16_t attLen = nabto_stun_uint16_read(ptr+n+2);

        if ( attType == STUN_ATTRIBUTE_XOR_MAPPED_ADDRESS_ALT || attType == STUN_ATTRIBUTE_XOR_MAPPED_ADDRESS ) {
            uint16_t family = nabto_stun_uint16_read(ptr + n + 4);
            uint16_t port = nabto_stun_uint16_read(ptr + n + 6);
            port = port^(STUN_MAGIC_COOKIE >> 16);
            msg->mappedEp.port = port;
            if (family == STUN_ADDRESS_FAMILY_V4) {
                memcpy(msg->mappedEp.addr.v4.addr, ptr + n + 8, 4);
                for (size_t i = 0; i < 4; i++) {
                    msg->mappedEp.addr.v4.addr[i] ^= STUN_MAGIC_COOKIE_BYTES[i];
                }
                msg->mappedEp.addr.type = NABTO_STUN_IPV4;
            } else if (family == STUN_ADDRESS_FAMILY_V6) {
                printf("family V6\n");
                //todo: PARSE IPv6 AND REMOVE RETURN
                return false;
            } else {
                printf("family OTHER\n");
                return false;
            }
        } else if (attType == STUN_ATTRIBUTE_RESPONSE_ORIGIN) {
            if(!nabto_stun_parse_address(ptr+n+4, &msg->serverEp)) {
                printf("parse response origin\n");
                return false;
            }
        } else if (attType == STUN_ATTRIBUTE_OTHER_ADDRESS) {
            if(!nabto_stun_parse_address(ptr+n+4, &msg->altServerEp)) {
                printf("parse other address\n");
                return false;
            }
        }
        if (attLen % 4 != 0) {
            n += attLen + 4 + (4-(attLen % 4));
        } else {
            n += attLen + 4;
        }
    }
    return true;

}

#ifdef __cplusplus
} // extern "C"
#endif
