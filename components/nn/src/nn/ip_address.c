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
        sprintf(outputBuffer, "%u.%u.%u.%u", (unsigned int)(ip[0]), (unsigned int)(ip[1]), (unsigned int)(ip[2]), (unsigned int)(ip[3]));
    } else if (address->type == NN_IPV6) {
        const uint8_t* ip = address->ip.v6;
        sprintf(outputBuffer, "%02x%02x:" "%02x%02x:" "%02x%02x:" "%02x%02x:" "%02x%02x:" "%02x%02x:" "%02x%02x:" "%02x%02x", ip[0], ip[1], ip[2], ip[3], ip[4], ip[5], ip[6], ip[7], ip[8], ip[9], ip[10], ip[11], ip[12], ip[13], ip[14], ip[15]);
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
    v6->type = NN_IPV6;
    uint8_t* ptr = v6->ip.v6;
    // 80 bits of zeroes
    memcpy(ptr, ipv4MappedIpv6Prefix, 12);
    memcpy(ptr + 12, v4->ip.v4, 4);
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

static const char* read_number(const char* ptr, uint32_t* number)
{
    // read the number and return the position after the number.
    uint32_t n = 0;
    uint32_t base = 10;
    for(;;) {
        if (is_digit(*ptr)) {
            n = (n * base) + (uint32_t)((*ptr) - '0');
            ptr++;
        } else {
            *number = n;
            return ptr;
        }
    }
}

bool nn_ip_address_read_v4(const char* str, struct nn_ip_address* ip)
{
    // read an ip of the form a.b.c.d
    const char* ptr = str;
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    ptr = read_number(ptr, &a);
    if (*ptr != '.') {
        return false;
    }
    ptr++;
    ptr = read_number(ptr, &b);
    if (*ptr != '.') {
        return false;
    }
    ptr++;
    ptr = read_number(ptr, &c);
    if (*ptr != '.') {
        return false;
    }
    ptr++;
    ptr = read_number(ptr, &d);

    ip->ip.v4[0] = (uint8_t)a;
    ip->ip.v4[1] = (uint8_t)b;
    ip->ip.v4[2] = (uint8_t)c;
    ip->ip.v4[3] = (uint8_t)d;

    ip->type = NN_IPV4;
    return true;
}
