#include <nn/ip_address.h>

#include <string.h>
#include <stdio.h>
#include <inttypes.h>

bool nn_ip_is_v4(const struct nn_ip_address* ip)
{
    return (ip->type == NN_IPV4);
}

bool nn_ip_is_v6(const struct nn_ip_address* ip)
{
    return (ip->type == NN_IPV6);
}

const char* nn_ip_address_to_string(const struct nn_ip_address* address)
{
    static char outputBuffer[40]; // 8*4 + 7 + 1
    memset(outputBuffer, 0, 40);
    if (address->type == NN_IPV4) {
        const uint8_t* ip = address->ip.v4;
        snprintf(outputBuffer, sizeof(outputBuffer), "%" PRIu8 ".%" PRIu8 ".%" PRIu8 ".%" PRIu8, ip[0], ip[1], ip[2], ip[3]);
    } else if (address->type == NN_IPV6) {
        const uint8_t* ip = address->ip.v6;
        snprintf(outputBuffer, sizeof(outputBuffer), "%02x%02x:" "%02x%02x:" "%02x%02x:" "%02x%02x:" "%02x%02x:" "%02x%02x:" "%02x%02x:" "%02x%02x", ip[0], ip[1], ip[2], ip[3], ip[4], ip[5], ip[6], ip[7], ip[8], ip[9], ip[10], ip[11], ip[12], ip[13], ip[14], ip[15]);
    }
    return outputBuffer;
}

void nn_ip_address_assign_v4(struct nn_ip_address* ip, uint32_t address)
{
    ip->type = NN_IPV4;
    ip->ip.v4[0] = (uint8_t)(address >> 24);
    ip->ip.v4[1] = (uint8_t)(address >> 16);
    ip->ip.v4[2] = (uint8_t)(address >> 8);
    ip->ip.v4[3] = (uint8_t)(address);
}

static const uint8_t ipv4MappedIpv6Prefix[12] = {0x00,0x00,0x00,0x00,
                                                 0x00,0x00,0x00,0x00,
                                                 0x00,0x00,0xFF,0xFF};
bool nn_ip_is_v4_mapped(const struct nn_ip_address* ip)
{
    if (nn_ip_is_v6(ip)) {
        const uint8_t* ptr = ip->ip.v6;
        if (memcmp(ptr, ipv4MappedIpv6Prefix, 12) == 0) {
            return true;
        }
    }
    return false;
}

void nn_ip_convert_v4_to_v4_mapped(const struct nn_ip_address* v4, struct nn_ip_address* v6)
{
    // convert v4 to v4 mapped ipv6 address.  ipv4 mapped ipv6
    // addresses consist of the prefix 0:0:0:0:0:FFFF and then the
    // ipv4 address.
    // v4 and v6 may be the same address, and ip.v4 and ip.v6 share a
    // union, so take a copy before the prefix overwrites the octets.
    uint8_t octets[4];
    memcpy(octets, v4->ip.v4, 4);

    v6->type = NN_IPV6;
    uint8_t* ptr = v6->ip.v6;
    // 80 bits of zeroes
    memcpy(ptr, ipv4MappedIpv6Prefix, 12);
    memcpy(ptr + 12, octets, 4);
}

void nn_ip_convert_v4_mapped_to_v4(const struct nn_ip_address* v6, struct nn_ip_address* v4)
{
    v4->type = NN_IPV4;
    memcpy(v4->ip.v4, v6->ip.v6+12, 4);
}

static bool is_digit(const char c)
{
    return c >= '0' && c <= '9';
}

static const char* read_octet(const char* ptr, uint8_t* octet)
{
    // read one decimal octet, at most three digits and at most 255, and
    // return the position after it. NULL if there is no valid octet.
    uint32_t n = 0;
    size_t digits = 0;
    while (is_digit(*ptr) && digits < 3) {
        n = (n * 10) + (uint32_t)((*ptr) - '0');
        ptr++;
        digits++;
    }
    if (digits == 0 || n > 255) {
        return NULL;
    }
    *octet = (uint8_t)n;
    return ptr;
}

bool nn_ip_address_read_v4(const char* str, struct nn_ip_address* ip)
{
    // read an ip of the form a.b.c.d, the whole string must be consumed.
    const char* ptr = str;
    uint8_t octets[4];
    for (size_t i = 0; i < 4; i++) {
        if (i > 0) {
            if (*ptr != '.') {
                return false;
            }
            ptr++;
        }
        ptr = read_octet(ptr, &octets[i]);
        if (ptr == NULL) {
            return false;
        }
    }
    if (*ptr != '\0') {
        return false;
    }

    memcpy(ip->ip.v4, octets, 4);
    ip->type = NN_IPV4;
    return true;
}
