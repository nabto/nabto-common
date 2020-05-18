#ifndef _NABTO_MDNS_SERVER_H_
#define _NABTO_MDNS_SERVER_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>



#ifdef __cplusplus
extern "C" {
#endif

enum nabto_mdns_record_types {
    NABTO_MDNS_A    = 1,
    NABTO_MDNS_PTR  = 12,
    NABTO_MDNS_TXT  = 16,
    NABTO_MDNS_AAAA = 28,
    NABTO_MDNS_SRV  = 33
};

enum nabto_mdns_ip_address_type {
    NABTO_MDNS_IPV4,
    NABTO_MDNS_IPV6
};

// network order ip address
struct nabto_mdns_ipv4_address {
    uint8_t addr[4];
};

// network order ipv6 address
struct nabto_mdns_ipv6_address {
    uint8_t addr[16];
};

struct nabto_mdns_ip_address {
    enum nabto_mdns_ip_address_type type;
    union {
        struct nabto_mdns_ipv4_address v4;
        struct nabto_mdns_ipv6_address v6;
    };
};

struct nabto_mdns_server_context {
    const char* deviceId;
    const char* productId;
    const char* service;
    const char* hostname;
};

/**
 * initialize the mdns server.
 * @param context     The context for the server
 * @param deviceId    Pointer to null terminated device ID string, must be kept alive while server is in use
 * @param productId   Pointer to null terminated product ID string, must be kept alive while server is in use
 * @param serviceName Pointer to null terminated service name string, must be kept alive while server is in use
 * @param hostname    Pointer to null terminated hostname string, must be kept alive while server is in use
  */
void nabto_mdns_server_init(struct nabto_mdns_server_context* context,
                            const char* deviceId, const char* productId,
                            const char* serviceName, const char* hostname);

/**
 * Hande incoming packet from multicast socket
 * @param context     The context for the server
 * @param buffer      Buffer containing the incoming packet
 * @param bufferSize  Size of incoming buffer
 * @return true if a response packet should be build and send, false if not
 */
bool nabto_mdns_server_handle_packet(struct nabto_mdns_server_context* context,
                                     const uint8_t* buffer, size_t bufferSize);

/**
 * Build response packet to be sent om multicast socket
 * @param context     The context for the server
 * @param ips         List of IP addresses to advertise
 * @param ipsSize     Number of elements in the list of IPs
 * @param port        Port number to advertise
 * @param buffer      Buffer containing the incoming packet
 * @param bufferSize  Size of incoming buffer
 * @param written     Place to put the number of bytes written to the buffer
 * @return true if the response packet should be and sent, false if not
 */
bool nabto_mdns_server_build_packet(struct nabto_mdns_server_context* context,
                                    const struct nabto_mdns_ip_address* ips, const size_t ipsSize, uint16_t port,
                                    uint8_t* buffer, size_t bufferSize, size_t* written);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
