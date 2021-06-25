#ifndef _NN_IP_ADDRESS_H
#define _NN_IP_ADDRESS_H

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum nn_ip_address_type {
    NN_IPV4,
    NN_IPV6
};

struct nn_ip_address {
    enum nn_ip_address_type type;
    union {
        uint8_t v4[4];
        uint8_t v6[16];
    } ip;
};

struct nn_endpoint {
    struct nn_ip_address ip;
    uint16_t port;
};

bool nn_ip_is_v4(const struct nn_ip_address* ip);

bool nn_ip_is_v6(const struct nn_ip_address* ip);

/**
 * Return true if the ip address is an ipv4 mapped ipv6 address.
 */
bool nn_ip_is_v4_mapped(const struct nn_ip_address* ip);

/**
 * Convert an ipv4 address to an ipv6 mapped ipv4 address.
 */
void nn_ip_convert_v4_to_v4_mapped(const struct nn_ip_address* v4, struct nn_ip_address* v6);

/**
 * Convert an v4 mapped ipv6 address to an ipv4 address.
 */
void nn_ip_convert_v4_mapped_to_v4(const struct nn_ip_address* v6, struct nn_ip_address* v4);

/**
 * print the ip into a null terminated static buffer. This buffer is
overwritten next time this function is called.
*/
const char* nn_ip_address_to_string(const struct nn_ip_address* ip);

/**
 * assign ipv4 address in host byte order to the ip address.
 */
void nn_ip_address_assign_v4(struct nn_ip_address* ip, uint32_t address);

/**
 * Read an ipv4 address from a string on the form a.b.c.d
 *
 * @return true iff the ip is read from the string.
 */
bool nn_ip_address_read_v4(const char* str, struct nn_ip_address* ip);

#ifdef __cplusplus
} //extern "C"
#endif

#endif
