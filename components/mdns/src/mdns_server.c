#include <mdns/mdns_server.h>

#include <nn/string_map.h>
#include <nn/string_set.h>

#include <stdbool.h>
#include <ctype.h>

static const uint8_t* nabto_mdns_server_uint8_read_forward(const uint8_t* buf, const uint8_t* end, uint8_t* val);
static const uint8_t* nabto_mdns_server_uint16_read_forward(const uint8_t* buf, const uint8_t* end, uint16_t* val);

static uint8_t* nabto_mdns_server_uint8_write_forward(uint8_t* buf, uint8_t* end, uint8_t val);
static uint8_t* nabto_mdns_server_uint16_write_forward(uint8_t* buf, uint8_t* end, uint16_t val);
static uint8_t* nabto_mdns_server_uint32_write_forward(uint8_t* buf, uint8_t* end, uint32_t val);

static uint8_t* nabto_mdns_server_data_write_forward(uint8_t* buf, uint8_t* end, const uint8_t* data, size_t dataLength);
static uint8_t* nabto_mdns_server_string_write_forward(uint8_t* buf, uint8_t* end, const char* string);

static uint8_t* nabto_mdns_server_encode_string(uint8_t* buf, uint8_t* end, const char* string);

void nabto_mdns_server_init(struct nabto_mdns_server_context* context)
{
    memset(context, 0, sizeof(struct nabto_mdns_server_context));
}

void nabto_mdns_server_update_info(struct nabto_mdns_server_context* context,
                                   const char* instanceName,
                                   struct nn_string_set* subtypes,
                                   struct nn_string_map* txtItems)
{
    context->instanceName = instanceName;
    context->subtypes = subtypes;
    context->txtItems = txtItems;
}


static bool match_label(const uint8_t* bufferLabel, size_t bufferLabelSize, const char* label)
{
    size_t labelSize = strlen(label);
    if (bufferLabelSize != labelSize) {
        return false;
    }

    for (size_t i = 0; i < labelSize; i++) {
        if (tolower(label[i]) != tolower(bufferLabel[i])) {
            return false;
        }
    }
    return true;
}

static bool match_name(const uint8_t* buffer, const uint8_t* end, const uint8_t* ptr, const char** toMatch)
{
    const char* label = *toMatch;
    if (label == NULL) {
        return true;
    }
    uint8_t length;
    ptr = nabto_mdns_server_uint8_read_forward(ptr, end, &length);
    if (!ptr) {
        return false;
    }
    if ((length & 0xC0) == 0xC0) {
        // this is a compression label
        uint8_t length2;
        ptr = nabto_mdns_server_uint8_read_forward(ptr, end, &length2);
        if (!ptr) {
            return false;
        }
        uint16_t offset = length2;
        offset += ((uint16_t)(length & ~0xC0)) << 8;

        size_t currentOffset = ptr - buffer;
        if (offset >= currentOffset) {
            // parse error compression labels needs to point to prior labels.
            return false;
        } else {
            return match_name(buffer, end, buffer+offset, toMatch);
        }
    } else {
        if (ptr + length > end) {
            return false;
        }

        if (match_label(ptr, length, label)) {
            size_t labelLength = strlen(label);

            return match_name(buffer, end, (ptr+labelLength), (toMatch+1));
        } else {
            return false;
        }
    }
}

static const uint8_t* skip_name(const uint8_t* ptr, const uint8_t* end)
{
    while (ptr != NULL) {
        uint8_t length;
        ptr = nabto_mdns_server_uint8_read_forward(ptr, end, &length);
        if (!ptr) {
            return ptr;
        }
        if ((length & 0xC0) == 0xC0) {
            // compression label, we have reached the end.
            // skip the next byte and return ptr as we have reached the end
            ptr = nabto_mdns_server_uint8_read_forward(ptr, end, &length);
            return ptr;
        } else if (length == 0) {
            return ptr;
        } else if (length > 63) {
            return NULL;
        } else {
            // skip length bytes
            ptr += length;
            if (ptr > end) {
                return NULL;
            }
        }
    }
    // never here
    return ptr;
}

bool nabto_mdns_server_handle_packet(struct nabto_mdns_server_context* context,
                                     const uint8_t* buffer, size_t bufferSize, uint16_t* id)
{
    if (context->instanceName == NULL) {
        return false; // not running 
    }
    uint16_t flags;
    // TODO: this discards const qualifier
    const uint8_t* ptr = buffer;
    const uint8_t* end = buffer + bufferSize;
    ptr = nabto_mdns_server_uint16_read_forward(ptr, end, id);
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

    const char* instanceName = context->instanceName;

    // skip answer, nameservers and additional resources
    ptr += 6;

    for (int i = 0; i < questions; i++) {

        if (match_name(buffer, end, ptr, (const char*[]){ "_nabto", "_udp", "local", NULL } )) {
            return true;
        }

        if (match_name(buffer, end, ptr, (const char*[]){ instanceName, "_nabto", "_udp", "local", NULL } )) {
            return true;
        }

        if (match_name(buffer, end, ptr, (const char*[]){ instanceName, "local", NULL } )) {
            return true;
        }

        if (match_name(buffer, end, ptr, (const char*[]){ "_services", "_dns-sd", "_udp", "local", NULL })) {
            return true;
        }

        const char* subtype;
        NN_STRING_SET_FOREACH(subtype, context->subtypes) {
            if (match_name(buffer, end, ptr, (const char*[]){subtype, "_sub", "_nabto", "_udp", "local", NULL })) {
                return true;
            }
        }

        // no match for this question.

        ptr = skip_name(ptr, end);
        // skip type, class
        uint16_t type;
        uint16_t class;
        ptr = nabto_mdns_server_uint16_read_forward(ptr, end, &type);
        ptr = nabto_mdns_server_uint16_read_forward(ptr, end, &class);
    }

    return false;
}

bool nabto_mdns_server_build_packet(struct nabto_mdns_server_context* context,
                                    uint16_t id, bool unicastResponse, bool goodbye,
                                    const struct nabto_mdns_ip_address* ips, const size_t ipsSize, uint16_t port,
                                    uint8_t* buffer, size_t bufferSize, size_t* written)

{
    if (context->instanceName == NULL) {
        return false; // not running 
    }
    uint8_t* ptr = buffer;
    uint8_t* end = buffer + bufferSize;

    uint16_t subLabel; // _sub._nabto._udp.local.
    uint16_t serviceLabel; // <uniqueid>._nabto._udp.local.
    uint16_t domainLabel; // <uniqueid>.local.
    uint16_t nabtoLabel; // _nabto._udp.local.
    uint16_t udpLabel; // _udp.local.
    uint16_t localLabel; // local.

    uint16_t cacheFlushBit;
    if (unicastResponse) {
        cacheFlushBit = 0;
    } else {
        cacheFlushBit = (1<<15);
    }

    const char* instanceName = context->instanceName;
    size_t instanceNameLength = strlen(context->instanceName);

    uint32_t ttl = 120;

    if (goodbye) {
        ttl = 0;
    }

    uint16_t records = 0;
    records += 1; // PTR _nabto._udp.local.
    records += 1; // PTR _services._dns-sd._udp.local.
    records += 1; // TXT p-abcdexyz-d-xyzabcde._nabto._udp.local.
    records += 1; // SRV p-abcdexyz-d-xyzabcde._nabto._udp.local.
    records += ipsSize; // A, AAAA p-abcdexyz-d-xyzabcde.local
    records += nn_string_set_size(context->subtypes); // additional subtypes

    // insert header
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, id);
    uint16_t flags = (1 << 15) + (1 << 10); // response flag & Authoritative flag
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, flags);
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, 0); // 0 questions
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, records);
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, 0); // 0 authority response records
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, 0); // 0 additional response records

    // insert ptr record for nabto
    nabtoLabel = 0xC000 + (uint16_t)(ptr - buffer);
    ptr = nabto_mdns_server_encode_string(ptr, end, "_nabto");
    udpLabel = 0xC000 + (uint16_t)(ptr - buffer);
    ptr = nabto_mdns_server_encode_string(ptr, end, "_udp");
    localLabel = 0xC000 + (uint16_t)(ptr - buffer);
    ptr = nabto_mdns_server_encode_string(ptr, end, "local");
    *ptr = 0; ptr++; // terminate labels
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, NABTO_MDNS_PTR);
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, 1); // IN class
    ptr = nabto_mdns_server_uint32_write_forward(ptr, end, ttl); // TTL
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, (uint16_t)(instanceNameLength+3)); // size of (service name length (1byte) + service name + compression label(2bytes))
    serviceLabel = 0xC000 + (uint16_t)(ptr - buffer);
    ptr = nabto_mdns_server_encode_string(ptr, end, instanceName);
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, 0xC00C); // Compression label (0xC000 + header size)

    // insert ptrs for subtypes

    subLabel = 0;
    const char* subtype;
    NN_STRING_SET_FOREACH(subtype, context->subtypes) {
        ptr = nabto_mdns_server_encode_string(ptr, end, subtype);
        if (subLabel == 0) {
            subLabel = 0xC000 + (uint16_t)(ptr - buffer);
            ptr = nabto_mdns_server_encode_string(ptr, end, "_sub");
            ptr = nabto_mdns_server_uint16_write_forward(ptr, end, nabtoLabel); // Compression label
        } else {
            ptr = nabto_mdns_server_uint16_write_forward(ptr, end, subLabel); // Compression label
        }

        ptr = nabto_mdns_server_uint16_write_forward(ptr, end, NABTO_MDNS_PTR);
        ptr = nabto_mdns_server_uint16_write_forward(ptr, end, 1); // IN class
        ptr = nabto_mdns_server_uint32_write_forward(ptr, end, ttl); // TTL
        ptr = nabto_mdns_server_uint16_write_forward(ptr, end, 2); // size of compression label
        ptr = nabto_mdns_server_uint16_write_forward(ptr, end, serviceLabel);
    }

    // insert ptr record for service discovery
    ptr = nabto_mdns_server_encode_string(ptr, end, "_services");
    ptr = nabto_mdns_server_encode_string(ptr, end, "_dns-sd");
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, udpLabel); // Compression label
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, NABTO_MDNS_PTR);
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, 1); // IN class
    ptr = nabto_mdns_server_uint32_write_forward(ptr, end, ttl); // TTL
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, 2); // size of compression label
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, 0xC00C); // Compression label (0xC000 + header size)

    // insert srv record
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, serviceLabel); // service name
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, NABTO_MDNS_SRV);
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, 1 + cacheFlushBit); // IN class + cache flush
    ptr = nabto_mdns_server_uint32_write_forward(ptr, end, ttl); // TTL
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, (uint16_t)(6+1+instanceNameLength+2)); // size of (prio(2bytes) + weight(2bytes) + port(2bytes) + hostname(length(1bytes + string)) + compression label(2bytes))
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, 0); // prio
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, 0); // weight
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, port); // port
    domainLabel = 0xC000 + (uint16_t)(ptr - buffer);
    ptr = nabto_mdns_server_encode_string(ptr, end, instanceName);
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, localLabel); // Compression label

    // insert txt record
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, serviceLabel); // Compression label
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, NABTO_MDNS_TXT);
    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, 1 + cacheFlushBit); // IN class + cache flush
    ptr = nabto_mdns_server_uint32_write_forward(ptr, end, ttl); // TTL
    size_t txtDataLen = 0;

    struct nn_string_map_iterator it;
    NN_STRING_MAP_FOREACH(it, context->txtItems) {
        // format length(1byte) name=value
        const char* key = nn_string_map_key(&it);
        const char* value = nn_string_map_value(&it);
        txtDataLen += 1; // length byte
        txtDataLen += strlen(key);
        txtDataLen += 1; // strlen("=")
        txtDataLen += strlen(value);
    }

    ptr = nabto_mdns_server_uint16_write_forward(ptr, end, (uint16_t)txtDataLen);

    NN_STRING_MAP_FOREACH(it, context->txtItems) {
        const char* key = nn_string_map_key(&it);
        const char* value = nn_string_map_value(&it);
        size_t l = strlen(key) + 1 + strlen(value);
        if (l > 255) {
            return false;
        }
        ptr = nabto_mdns_server_uint8_write_forward(ptr, end, (uint8_t)l);
        ptr = nabto_mdns_server_string_write_forward(ptr, end, key);
        ptr = nabto_mdns_server_string_write_forward(ptr, end, "=");
        ptr = nabto_mdns_server_string_write_forward(ptr, end, value);

    }

    // insert a(aaa) records
    for (uint8_t i = 0; i < ipsSize; i++) {
        ptr = nabto_mdns_server_uint16_write_forward(ptr, end, domainLabel);

        if (ips[i].type == NABTO_MDNS_IPV4) {
            ptr = nabto_mdns_server_uint16_write_forward(ptr, end, NABTO_MDNS_A);
            ptr = nabto_mdns_server_uint16_write_forward(ptr, end, 1 + cacheFlushBit); // IN class + cache flush
            ptr = nabto_mdns_server_uint32_write_forward(ptr, end, ttl); // TTL
            ptr = nabto_mdns_server_uint16_write_forward(ptr, end, 4); // size of ipv4
            ptr = nabto_mdns_server_data_write_forward(ptr, end, ips[i].v4.addr, 4);
        } else if (ips[i].type == NABTO_MDNS_IPV6) {
            ptr = nabto_mdns_server_uint16_write_forward(ptr, end, NABTO_MDNS_AAAA);
            ptr = nabto_mdns_server_uint16_write_forward(ptr, end, 1 + cacheFlushBit); // IN class + cache flush
            ptr = nabto_mdns_server_uint32_write_forward(ptr, end, ttl); // TTL
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
