
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

void nabto_stun_init_message(const struct nabto_stun_module* mod, struct nabto_stun_message* msg, bool changeAddr, bool changePort, enum nabto_stun_socket sock, struct nn_endpoint ep, uint8_t maxRetransmissions, void* modUserData)
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
    (void)size;
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

bool nabto_stun_parse_address(const uint8_t* buf, struct nn_endpoint* ep)
{
    uint16_t family = nabto_stun_uint16_read(buf);
    uint16_t port = nabto_stun_uint16_read(buf + 2);
    ep->port = port;
    if (family == STUN_ADDRESS_FAMILY_V4) {
        ep->ip.type = NN_IPV4;
        memcpy(ep->ip.ip.v4,(buf + 4), 4);
        return true;
    } else if (family == STUN_ADDRESS_FAMILY_V6) {
        //TODO: PARSE IPv6 AND RETURN TRUE
        return false;
    } else {
        return false;
    }
}

// TODO: use logging from nabto_stun_log instead of printf
//#include <stdio.h>
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
    if (type != STUN_MESSAGE_BINDING_RESPONSE_SUCCESS ||
        size < length+20) { // message length + header must fit in packet
        //printf("not binding response\n");
        return false;
    }
    // skip magic cookie and transaction ID
    ptr += 16;
    while (n+4 <= length) { // if the packet contains another attribute, we must have 4 bytes for its header
        uint16_t attType = nabto_stun_uint16_read(ptr+n);
        uint16_t attLen = nabto_stun_uint16_read(ptr+n+2);

        if (n+4+attLen > length) {
            // the specified attribute length does not fit in the packet
            return false;
        }

        if ( (attType == STUN_ATTRIBUTE_XOR_MAPPED_ADDRESS_ALT || attType == STUN_ATTRIBUTE_XOR_MAPPED_ADDRESS)
             && attLen >= 8 ) { // attLen >= 4B header + IPv4
            uint16_t family = nabto_stun_uint16_read(ptr + n + 4);
            uint16_t port = nabto_stun_uint16_read(ptr + n + 6);
            port = port^(STUN_MAGIC_COOKIE >> 16);
            msg->mappedEp.port = port;
            if (family == STUN_ADDRESS_FAMILY_V4) { // IPv4 length already checked
                memcpy(msg->mappedEp.ip.ip.v4, ptr + n + 8, 4);
                for (size_t i = 0; i < 4; i++) {
                    msg->mappedEp.ip.ip.v4[i] ^= STUN_MAGIC_COOKIE_BYTES[i];
                }
                msg->mappedEp.ip.type = NN_IPV4;
            } else if (family == STUN_ADDRESS_FAMILY_V6 && attLen >= 20) { // IPv6 requires more attLen
                //printf("family V6\n");
                //todo: PARSE IPv6 AND REMOVE RETURN
                return false;
            } else {
                //printf("family OTHER\n");
                return false;
            }
        } else if (attType == STUN_ATTRIBUTE_RESPONSE_ORIGIN && attLen >= 4) {
            // TODO: When implementing IPv6, attLen should be checked based on IP version
            if(!nabto_stun_parse_address(ptr+n+4, &msg->serverEp)) {
                //printf("parse response origin\n");
                return false;
            }
        } else if (attType == STUN_ATTRIBUTE_OTHER_ADDRESS && attLen >= 4) {
            // TODO: When implementing IPv6, attLen should be checked based on IP version
            if(!nabto_stun_parse_address(ptr+n+4, &msg->altServerEp)) {
                //printf("parse other address\n");
                return false;
            }
        }
        if (attLen % 4 != 0) { // advance length + header + padding
            n += attLen + 4 + (4-(attLen % 4));
        } else { // advance length + header
            n += attLen + 4;
        }
    }
    return true;

}

#ifdef __cplusplus
} // extern "C"
#endif
