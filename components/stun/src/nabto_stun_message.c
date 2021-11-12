
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

const uint8_t* nabto_stun_read_uint16(const uint8_t* ptr, const uint8_t* end, uint16_t* val)
{
    if (ptr == NULL || (ptr + 2) > end) {
        return NULL;
    }
    *val =
        ((uint16_t)ptr[0]) << 8 |
        ((uint16_t)ptr[1]);
    return ptr + 2;
}

const uint8_t* nabto_stun_read_uint32(const uint8_t* ptr, const uint8_t* end, uint32_t* val)
{
    if (ptr == NULL || (ptr + 4) > end) {
        return NULL;
    }
    *val =
        ((uint32_t)ptr[0]) << 24 |
        ((uint32_t)ptr[1]) << 16 |
        ((uint32_t)ptr[2]) << 8 |
        ((uint32_t)ptr[3]);
    return ptr + 4;
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

// TODO: use logging from nabto_stun_log instead of printf
//#include <stdio.h>

const uint8_t* nabto_stun_read_endpoint(const uint8_t* buf, uint16_t attLen, struct nn_endpoint* ep, bool xored)
{
    const uint8_t* ptr = buf;
    uint8_t family;
    uint16_t port;
    ptr += 1; // first byte must be ignored according to the RFC.
    family = *ptr; ptr += 1;
    ptr = nabto_stun_read_uint16(ptr, buf + attLen, &port);
    if (ptr == NULL) {
        //printf("family/port read failed\n");
        return NULL;
    }

    if (family == STUN_ADDRESS_FAMILY_V4) {
        if (attLen != 8) { // no room for IP
            //printf("attLen %d is not 8\n", attLen);
            ptr = NULL;
        } else {
            ep->port = port;
            ep->ip.type = NN_IPV4;
            memcpy(ep->ip.ip.v4, ptr, 4);
            if (xored) {
                ep->port ^= (STUN_MAGIC_COOKIE >> 16);
                for (size_t i = 0; i < 4; i++) {
                    ep->ip.ip.v4[i] ^= STUN_MAGIC_COOKIE_BYTES[i];
                }
            }
            ptr += 4;
        }
    } else if (family == STUN_ADDRESS_FAMILY_V6) {
        ptr = attLen != 20 ? NULL : ptr+16; // IPv6 not supported but ptr must be advanced to continue parsing
    } else {
        ptr = NULL; // RFC only defines attribute length for IPv4 and IPv6
    }
    return ptr;
}

bool nabto_stun_decode_message(struct nabto_stun_message* msg, const uint8_t* buf, uint16_t size)
{
    const uint8_t* ptr = buf;
    const uint8_t* end = buf+size;
    uint16_t type = 0;
    uint16_t length = 0;

    ptr = nabto_stun_read_uint16(ptr, end, &type);
    ptr = nabto_stun_read_uint16(ptr, end, &length);
    if (ptr == NULL || // above reads failed
        type != STUN_MESSAGE_BINDING_RESPONSE_SUCCESS || // invalid type
        size < length+20) { // message length + header must fit in packet
        //printf("not binding response (%d vs %d). size: %d, length+20: %d\n", type, STUN_MESSAGE_BINDING_RESPONSE_SUCCESS, size, length+20);
        return false;
    }
    // skip magic cookie and transaction ID
    ptr += 16;
    while (ptr != NULL && ptr < end) {
        uint16_t attType;
        uint16_t attLen;
        ptr = nabto_stun_read_uint16(ptr, end, &attType);
        ptr = nabto_stun_read_uint16(ptr, end, &attLen);

        if (ptr == NULL) {
            // read from buffer failed, so we have reached the end

            return true;
        } else if (attLen > (end-ptr)) { // no room in packet, invalid packet
            //printf("invalid AttLen: attLen %d > length %ld\n", attLen, (end-ptr));
            return false;
        }

        const uint8_t* attPtr = ptr;
        const uint8_t* attEnd = ptr + attLen;


        if ( attType == STUN_ATTRIBUTE_XOR_MAPPED_ADDRESS_ALT || attType == STUN_ATTRIBUTE_XOR_MAPPED_ADDRESS) {
            attPtr = nabto_stun_read_endpoint(attPtr, attLen, &msg->mappedEp, true);
        } else if (attType == STUN_ATTRIBUTE_RESPONSE_ORIGIN) {
            attPtr = nabto_stun_read_endpoint(attPtr, attLen, &msg->serverEp, false);
        } else if (attType == STUN_ATTRIBUTE_OTHER_ADDRESS) {
            attPtr = nabto_stun_read_endpoint(attPtr, attLen, &msg->altServerEp, false);
        }
        if (attPtr == NULL) { // if read_endpoint returned NULL it means invalid formatting
            //printf("read_endpoint returned NULL\n");
            return false;
        }

        if (attLen % 4 == 0) {
            ptr += attLen;
        } else {
            // advance ptr by padding
            ptr += attLen + (4-(attLen % 4));
        }
    }
    return true;

}

#ifdef __cplusplus
} // extern "C"
#endif
