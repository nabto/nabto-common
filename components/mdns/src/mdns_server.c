#include "mdns_server.h"

static const uint8_t* nabto_mdns_server_uint8_read_forward(const uint8_t* buf, const uint8_t* end, uint8_t* val);
static const uint8_t* nabto_mdns_server_uint16_read_forward(const uint8_t* buf, const uint8_t* end, uint16_t* val);

static uint8_t* nabto_mdns_server_uint8_write_forward(uint8_t* buf, uint8_t* end, uint8_t val);
static uint8_t* nabto_mdns_server_uint16_write_forward(uint8_t* buf, uint8_t* end, uint16_t val);
static uint8_t* nabto_mdns_server_uint32_write_forward(uint8_t* buf, uint8_t* end, uint32_t val);

static uint8_t* nabto_mdns_server_data_write_forward(uint8_t* buf, uint8_t* end, const uint8_t* data, size_t dataLength);
static uint8_t* nabto_mdns_server_string_write_forward(uint8_t* buf, uint8_t* end, const char* string);

static uint8_t* nabto_mdns_server_encode_string(uint8_t* buf, uint8_t* end, const char* string);

void nabto_mdns_server_init(struct nabto_mdns_server_context* context,
                            const char* deviceId, const char* productId,
                            const char* serviceName, const char* hostname)
{
    context->deviceId = deviceId;
    context->productId = productId;
    context->service = serviceName;
    context->hostname = hostname;
}

bool nabto_mdns_server_handle_packet(struct nabto_mdns_server_context* context,
                                     const uint8_t* buffer, size_t bufferSize)
{
    uint16_t id;
    uint16_t flags;
    // TODO: this discards const qualifier
    const uint8_t* ptr = buffer;
    const uint8_t* end = buffer + bufferSize;
    ptr = nabto_mdns_server_uint16_read_forward(ptr, end, &id);
    ptr = nabto_mdns_server_uint16_read_forward(ptr, end, &flags);
    if (ptr == NULL) {
        return false;
    }
    if ((flags & 0x8000) == 0x8000) {
        // response flag is set
        return false;
    }
    uint16_t questions;
    ptr = nabto_mdns_server_uint16_read_forward(ptr, end, &questions);
    if (ptr == NULL) {
        return false;
    }

    // skip answer, nameservers and additional resources
    ptr += 6;

    for (int i = 0; i < questions; i++) {
        uint8_t length;
        ptr = nabto_mdns_server_uint8_read_forward(ptr, end, &length);
        if (ptr == NULL) {
            return false;
        }
        if (length == strlen("_nabto") && memcmp(ptr, "_nabto", strlen("_nabto")) == 0) {
            // nabto question
            return true;
        } else if (length == strlen("_services") && memcmp(ptr, "_services", strlen("_services")) == 0) {
            // services question
            return true;
        }

        while (length != 0) {
            ptr = nabto_mdns_server_uint8_read_forward(ptr, end, &length);
            if (ptr == NULL) {
                return false;
            }
            if (length == 0xC0) {
                // Compression label read rest of the offset.
                uint8_t offset;
                ptr = nabto_mdns_server_uint8_read_forward(ptr, end, &offset);
                break;
            }
        }
        // skip type and class
        if (ptr == NULL) {
            return false;
        }
        ptr += 4;

    }
    return false;
}

bool nabto_mdns_server_build_packet(struct nabto_mdns_server_context* context,
                                    const struct nabto_mdns_ip_address* ips, const size_t ipsSize, uint16_t port,
                                    uint8_t* buffer, size_t bufferSize, size_t* written)

{
    uint8_t* ptr = buffer;
    uint8_t* end = buffer + bufferSize;

    uint16_t serviceLabel;
    uint16_t domainLabel;
    uint16_t localLabel;
    uint16_t udpLabel;
    const char* deviceTxt = "deviceId=";
    const char* productTxt = "productId=";


    // insert header
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, 0); // ID is always 0
    uint16_t flags = (1 << 15) + (1 << 10); // response flag & Authoritative flag
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, flags);
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, 0); // 0 questions
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, (uint16_t)(4+ipsSize)); // 2x PTR, SRV, TXT, ipSizex a(aaa)
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, 0); // 0 authority response records
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, 0); // 0 additional response records

    // insert ptr record for nabto
    ptr = nabto_mdns_server_encode_string(ptr, end, "_nabto");
    udpLabel = 0xC000 + (uint16_t)(ptr - buffer);
    ptr = nabto_mdns_server_encode_string(ptr, end, "_udp");
    localLabel = 0xC000 + (uint16_t)(ptr - buffer);
    ptr = nabto_mdns_server_encode_string(ptr, end, "local");
    *ptr = 0; ptr++; // terminate labels
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, NABTO_MDNS_PTR);
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, 1); // IN class
    ptr = nabto_mdns_server_uint32_write_forward(ptr, end, 120); // TTL
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, (uint16_t)(strlen(context->service)+3)); // size of (service name length + service name + compression label)
    serviceLabel = 0xC000 + (uint16_t)(ptr - buffer);
    ptr = nabto_mdns_server_encode_string(ptr, end, context->service);
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, 0xC00C); // Compression label (0xC000 + header size)

    // insert ptr record for service discovery
    ptr = nabto_mdns_server_encode_string(ptr, end, "_services");
    ptr = nabto_mdns_server_encode_string(ptr, end, "_dns-sd");
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, udpLabel); // Compression label
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, NABTO_MDNS_PTR);
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, 1); // IN class
    ptr = nabto_mdns_server_uint32_write_forward(ptr, end, 120); // TTL
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, 2); // size of compression label
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, 0xC00C); // Compression label (0xC000 + header size)

    // insert srv record
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, serviceLabel); // service name
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, NABTO_MDNS_SRV);
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, 1 + (1<<15)); // IN class + cache flush
    ptr = nabto_mdns_server_uint32_write_forward(ptr, end, 120); // TTL
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, (uint16_t)(6+1+strlen(context->hostname)+2)); // size of (prio + weight + port + hostname + compression label)
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, 0); // prio
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, 0); // weight
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, port); // port
    domainLabel = 0xC000 + (uint16_t)(ptr - buffer);
    ptr = nabto_mdns_server_encode_string(ptr, end, context->hostname);
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, localLabel); // Compression label

    // insert txt record
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, serviceLabel); // Compression label
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, NABTO_MDNS_TXT);
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, 1 + (1<<15)); // IN class + cache flush
    ptr = nabto_mdns_server_uint32_write_forward(ptr, end, 120); // TTL
    size_t txtDataLen =
        1+strlen(deviceTxt)+strlen(context->deviceId) +
        1+strlen(productTxt)+strlen(context->productId);
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, (uint16_t)txtDataLen);
    // manually inserting strings to concat
    ptr = nabto_mdns_server_uint8_write_forward(ptr, end, (uint8_t)(strlen(deviceTxt)+strlen(context->deviceId)));
    ptr = nabto_mdns_server_string_write_forward(ptr, end, deviceTxt);
    ptr = nabto_mdns_server_string_write_forward(ptr, end, context->deviceId);

    ptr = nabto_mdns_server_uint8_write_forward(ptr, end, (uint8_t)(strlen(productTxt)+strlen(context->productId)));
    ptr = nabto_mdns_server_string_write_forward(ptr, end, productTxt);
    ptr = nabto_mdns_server_string_write_forward(ptr, end, context->productId);

    // insert a(aaa) records
    for (uint8_t i = 0; i < ipsSize; i++) {
        ptr = nabto_mdns_server_uint16_write_forward(ptr, end, domainLabel);

        if (ips[i].type == NABTO_MDNS_IPV4) {
            ptr = nabto_mdns_server_uint16_write_forward(ptr, end, NABTO_MDNS_A);
            ptr = nabto_mdns_server_uint16_write_forward(ptr, end, 1 + (1<<15)); // IN class + cache flush
            ptr = nabto_mdns_server_uint32_write_forward(ptr, end, 120); // TTL
            ptr = nabto_mdns_server_uint16_write_forward(ptr, end, 4); // size of ipv4
            ptr = nabto_mdns_server_data_write_forward(ptr, end, ips[i].v4.addr, 4);
        } else if (ips[i].type == NABTO_MDNS_IPV6) {
            ptr = nabto_mdns_server_uint16_write_forward(ptr, end, NABTO_MDNS_AAAA);
            ptr = nabto_mdns_server_uint16_write_forward(ptr, end, 1 + (1<<15)); // IN class + cache flush
            ptr = nabto_mdns_server_uint32_write_forward(ptr, end, 120); // TTL
            ptr = nabto_mdns_server_uint16_write_forward(ptr, end, 16); // size of ipv6
            ptr = nabto_mdns_server_data_write_forward(ptr, end, ips[i].v6.addr, 16);
        } else {
            // bad ip input, abort
            return false;
        }
    }
    *written = ptr - buffer;
    return true;
}


uint8_t* nabto_mdns_server_encode_string(uint8_t* buf, uint8_t* end, const char* string)
{
    size_t length = strlen(string);
    if (length > 255) {
        return NULL;
    }

    buf = nabto_mdns_server_uint8_write_forward(buf, end, (uint8_t)length);
    buf = nabto_mdns_server_string_write_forward(buf, end, string);
    return buf;
}

const uint8_t* nabto_mdns_server_uint8_read_forward(const uint8_t* buf, const uint8_t* end, uint8_t* val)
{
    if (buf == NULL || end < buf || end - buf < 1) {
        return NULL;
    }
    *val = *buf;
    return buf + 1;
}


const uint8_t* nabto_mdns_server_uint16_read_forward(const uint8_t* buf, const uint8_t* end, uint16_t* val)
{
    if (buf == NULL || end < buf || end - buf < 2) {
        return NULL;
    }
    *val = (uint16_t)(*buf) << 8;
    *val += *(buf+1);
    return buf + 2;
}

uint8_t* nabto_mdns_server_string_write_forward(uint8_t* buf, uint8_t* end, const char* val)
{
    size_t length = strlen(val);
    for (size_t i = 0; i < length; i++) {
        buf = nabto_mdns_server_uint8_write_forward(buf,end,(uint8_t)val[i]);
    }
    return buf;
}

uint8_t* nabto_mdns_server_data_write_forward(uint8_t* buf, uint8_t* end, const uint8_t* data, size_t dataLength)
{
    for (size_t i = 0; i < dataLength; i++) {
        buf = nabto_mdns_server_uint8_write_forward(buf,end,data[i]);
    }
    return buf;
}



uint8_t* nabto_mdns_server_uint8_write_forward(uint8_t* buf, uint8_t* end, uint8_t val)
{
    if (buf == NULL || end < buf || end - buf < 1) {
        return NULL;
    }
    *buf = val;
    buf++;
    return buf;
}

uint8_t* nabto_mdns_server_uint16_write_forward(uint8_t* buf, uint8_t* end, uint16_t val)
{
    uint8_t d0 = (uint8_t)(((val) >> 8) & 0xff);
    uint8_t d1 = (uint8_t)( (val)       & 0xff);
    buf = nabto_mdns_server_uint8_write_forward(buf, end, d0);
    buf = nabto_mdns_server_uint8_write_forward(buf, end, d1);
    return buf;
}

uint8_t* nabto_mdns_server_uint32_write_forward(uint8_t* buf, uint8_t* end, uint32_t val)
{
    uint8_t d0 = (uint8_t)(((val) >> 24) & 0xff);
    uint8_t d1 = (uint8_t)(((val) >> 16) & 0xff);
    uint8_t d2 = (uint8_t)(((val) >> 8)  & 0xff);
    uint8_t d3 = (uint8_t)( (val)        & 0xff);
    buf = nabto_mdns_server_uint8_write_forward(buf, end, d0);
    buf = nabto_mdns_server_uint8_write_forward(buf, end, d1);
    buf = nabto_mdns_server_uint8_write_forward(buf, end, d2);
    buf = nabto_mdns_server_uint8_write_forward(buf, end, d3);
    return buf;
}
