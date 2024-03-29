#ifndef _NABTO_MDNS_SERVER_H_
#define _NABTO_MDNS_SERVER_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <nn/ip_address.h>


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

struct nabto_mdns_server_context {
    const char* instanceName;
    struct nn_string_set* subtypes;
    struct nn_string_map* txtItems;
};

// Mdns server

/**
 * mDNS example questions
 *
 * Q: PTR _nabto._udp.local
 * A: PTR _nabto._udp.local -> actualservice._nabto._udp.local
 *
 * Q: PTR heatpump._sub._nabto._udp.local
 * A: PTR heatpump._sub._nabto._udp.local -> actualservice._nabto._udp.local
 *
 * Q: PTR <productid-deviceid>._sub._nabto._udp.local
 * Q: PTR <productid-deviceid>._sub._nabto._udp.local -> actualservice._nabto._udp.local
 *
 * Q: SRV actualservice._nabto._udp.local
 * A: SRV actualservice._nabto._udp.local -> actualhostname.local:port
 *
 * Q: TXT actualservice._nabto._udp.local
 * A: TXT actualservice._nabto._udp.local -> foo=bar, baz=quux
 *
 * Q: A actualhostname.local
 * A: A actualhostname.local -> ipv4
 *
 * Q: AAAA actualhostname.local
 * A: AAAA actualhostname.local -> ipv6
 *
 * Q: PTR _services._dns-sd._udp.local
 * A: PTR _services._dns-sd._udp.local -> _nabto._udp.local
 *
 * For each matching query, all of the above answers is given.
 */

/**
 * initialize the mdns server.
 * @param context     The context for the server
 */
void nabto_mdns_server_init(struct nabto_mdns_server_context* context);

/**
 * Add a service to the mdns server
 *
 * @param context  The server context
 * @param instanceName  The instance name pointer needs to be kept alive as long as the server is used.
 * @param subtypes  The subtypes pointer needs to be kept alive as long as the server is used.
 * @param txtItems  The txt items needs to be kept alive as long as the server is alive.
 */
void nabto_mdns_server_update_info(struct nabto_mdns_server_context* context,
                                   const char* instanceName,
                                   struct nn_string_set* subtypes,
                                   struct nn_string_map* txtItems);

/**
 * Hande incoming packet from multicast socket
 * @param context     The context for the server
 * @param buffer      Buffer containing the incoming packet
 * @param bufferSize  Size of incoming buffer
 * @param id          Place to put ID to reference in build packet function
 * @return true if a response packet should be build and send, false if not
 */
bool nabto_mdns_server_handle_packet(struct nabto_mdns_server_context* context,
                                     const uint8_t* buffer, size_t bufferSize, uint16_t* id);

/**
 * Build response packet to be sent om multicast socket
 *
 * @param context     The context for the server
 * @param id          Id from the request packet
 * @param unicastResponse make the response as a unicast response.
 * @param goodbye     Make the response a goodbye packet.
 * @param ips         List of IP addresses to advertise
 * @param ipsSize     Number of elements in the list of IPs
 * @param port        Port number to advertise
 * @param buffer      Buffer containing the incoming packet
 * @param bufferSize  Size of incoming buffer
 * @param written     Place to put the number of bytes written to the buffer
 * @return true if the response packet should be and sent, false if not
 */
bool nabto_mdns_server_build_packet(struct nabto_mdns_server_context* context,
                                    uint16_t id, bool unicastResponse, bool goodbye,
                                    const struct nn_ip_address* ips, const size_t ipsSize, uint16_t port,
                                    uint8_t* buffer, size_t bufferSize, size_t* written);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
