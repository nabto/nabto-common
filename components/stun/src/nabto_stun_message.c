
#include <nabto_stun/nabto_stun_message.h>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t STUN_MAGIC_COOKIE_BYTES[4] = { 0x21, 0x12, 0xA4, 0x42 };

uint8_t* nabto_stun_uint8_write_forward(uint8_t* buf, uint8_t* end, uint8_t val)
{
    if (buf == NULL || end < buf || end - buf < 1) {
        return NULL;
    }
    *buf = val;
    return buf + 1;
}

uint8_t* nabto_stun_buf_write_forward(uint8_t* buf, uint8_t* end, const uint8_t* val, uint16_t size)
{
    for (uint16_t i = 0; i < size; i++) {
        buf = nabto_stun_uint8_write_forward(buf, end, val[i]);
    }
    return buf;
}

uint8_t* nabto_stun_uint16_write_forward(uint8_t* buf, uint8_t* end, uint16_t val)
{
    uint8_t d0 = (uint8_t)(((val) >> 8) & 0xff);
    uint8_t d1 = (uint8_t)( (val)       & 0xff);
    buf = nabto_stun_uint8_write_forward(buf, end, d0);
    buf = nabto_stun_uint8_write_forward(buf, end, d1);
    return buf;
}

uint8_t* nabto_stun_uint32_write_forward(uint8_t* buf, uint8_t* end, uint32_t val)
{
    uint8_t d0 = (uint8_t)(((val) >> 24) & 0xff);
    uint8_t d1 = (uint8_t)(((val) >> 16) & 0xff);
    uint8_t d2 = (uint8_t)(((val) >> 8)  & 0xff);
    uint8_t d3 = (uint8_t)( (val)        & 0xff);
    buf = nabto_stun_uint8_write_forward(buf, end, d0);
    buf = nabto_stun_uint8_write_forward(buf, end, d1);
    buf = nabto_stun_uint8_write_forward(buf, end, d2);
    buf = nabto_stun_uint8_write_forward(buf, end, d3);
    return buf;
}

const uint8_t* nabto_stun_read_uint8(const uint8_t* ptr, const uint8_t* end, uint8_t* val)
{
    if (ptr == NULL || end < ptr || end - ptr < 1) {
        return NULL;
    }
    *val = *ptr;
    return ptr + 1;
}

const uint8_t* nabto_stun_read_buf(const uint8_t* ptr, const uint8_t* end, uint8_t* val, uint16_t size)
{
    for (uint16_t i = 0; i < size; i++) {
        ptr = nabto_stun_read_uint8(ptr, end, &val[i]);
    }
    return ptr;
}

const uint8_t* nabto_stun_read_uint16(const uint8_t* ptr, const uint8_t* end, uint16_t* val)
{
    uint8_t d0;
    uint8_t d1;
    ptr = nabto_stun_read_uint8(ptr, end, &d0);
    ptr = nabto_stun_read_uint8(ptr, end, &d1);
    if (ptr == NULL) {
        return NULL;
    }
    *val = ((uint16_t)d0) << 8 | ((uint16_t)d1);
    return ptr;
}

const uint8_t* nabto_stun_read_uint32(const uint8_t* ptr, const uint8_t* end, uint32_t* val)
{
    uint8_t d[4];
    ptr = nabto_stun_read_buf(ptr, end, d, 4);
    if (ptr == NULL) {
        return NULL;
    }
    *val =
        ((uint32_t)d[0]) << 24 |
        ((uint32_t)d[1]) << 16 |
        ((uint32_t)d[2]) << 8 |
        ((uint32_t)d[3]);
    return ptr;
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
    uint8_t* ptr = buf;
    uint8_t* end = buf + size;
    uint32_t bits = 0;

    ptr = nabto_stun_uint16_write_forward(ptr, end, STUN_MESSAGE_BINDING_REQUEST);
    ptr = nabto_stun_uint16_write_forward(ptr, end, 8);
    ptr = nabto_stun_uint32_write_forward(ptr, end, STUN_MAGIC_COOKIE);
    ptr = nabto_stun_buf_write_forward(ptr, end, msg->transactionId, 12);

    if (msg->changePort) {
        bits |= (1<<1);
    }
    if (msg->changeAddress) {
        bits |= (1<<2);
    }
    ptr = nabto_stun_uint16_write_forward(ptr, end, STUN_ATTRIBUTE_CHANGE_REQUEST);
    ptr = nabto_stun_uint16_write_forward(ptr, end, 4);
    ptr = nabto_stun_uint32_write_forward(ptr, end, bits);
    if (ptr == NULL) { // buffer too small
        return 0;
    }
    return (uint16_t)(ptr-buf);
}

// TODO: use logging from nabto_stun_log instead of printf
//#include <stdio.h>

// end is the end of the attribute value, not of the packet.
const uint8_t* nabto_stun_read_endpoint(const uint8_t* buf, const uint8_t* end, struct nn_endpoint* ep, bool xored)
{
    const uint8_t* ptr = buf;
    uint8_t reserved;
    uint8_t family;
    uint16_t port;
    ptr = nabto_stun_read_uint8(ptr, end, &reserved); // first byte must be ignored according to the RFC.
    ptr = nabto_stun_read_uint8(ptr, end, &family);
    ptr = nabto_stun_read_uint16(ptr, end, &port);
    if (ptr == NULL) {
        //printf("family/port read failed\n");
        return NULL;
    }

    if (family == STUN_ADDRESS_FAMILY_V4) {
        if (end - buf != 8) { // no room for IP
            //printf("attLen %ld is not 8\n", (end-buf));
            ptr = NULL;
        } else {
            ep->port = port;
            ep->ip.type = NN_IPV4;
            ptr = nabto_stun_read_buf(ptr, end, ep->ip.ip.v4, 4);
            if (xored) {
                ep->port ^= (STUN_MAGIC_COOKIE >> 16);
                for (size_t i = 0; i < 4; i++) {
                    ep->ip.ip.v4[i] ^= STUN_MAGIC_COOKIE_BYTES[i];
                }
            }
        }
    } else if (family == STUN_ADDRESS_FAMILY_V6) {
        ptr = (end - buf) != 20 ? NULL : end; // IPv6 not supported but ptr must be advanced to continue parsing
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


        if ( attType == STUN_ATTRIBUTE_XOR_MAPPED_ADDRESS_ALT || attType == STUN_ATTRIBUTE_XOR_MAPPED_ADDRESS) {
            attPtr = nabto_stun_read_endpoint(attPtr, attPtr + attLen, &msg->mappedEp, true);
        } else if (attType == STUN_ATTRIBUTE_RESPONSE_ORIGIN) {
            attPtr = nabto_stun_read_endpoint(attPtr, attPtr + attLen, &msg->serverEp, false);
        } else if (attType == STUN_ATTRIBUTE_OTHER_ADDRESS) {
            attPtr = nabto_stun_read_endpoint(attPtr, attPtr + attLen, &msg->altServerEp, false);
        }
        if (attPtr == NULL) { // if read_endpoint returned NULL it means invalid formatting
            //printf("read_endpoint returned NULL\n");
            return false;
        }

        ptr += attLen;
        // advance ptr by padding to a 4 byte boundary. The packet may end
        // inside the padding of its last attribute, so do not go past end.
        uint16_t padding = (4 - (attLen % 4)) % 4;
        if (padding > (end - ptr)) {
            padding = (uint16_t)(end - ptr);
        }
        ptr += padding;
    }
    return true;

}

#ifdef __cplusplus
} // extern "C"
#endif
