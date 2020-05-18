#ifndef _NABTO_COAP_CLIENT_H_
#define _NABTO_COAP_CLIENT_H_

#include "stdint.h"
#include "stdbool.h"
#include "nabto_coap.h"

#ifdef __cplusplus
extern "C" {
#endif

enum nabto_coap_client_next_event {
    // handle a callback
    NABTO_COAP_CLIENT_NEXT_EVENT_CALLBACK,
    // Ready to send the next packet
    NABTO_COAP_CLIENT_NEXT_EVENT_SEND,
    // Wait for a timeout.  We assume it does not make sense to wait
    // if we are sending a packet.
    NABTO_COAP_CLIENT_NEXT_EVENT_WAIT,
    // nothing to do
    NABTO_COAP_CLIENT_NEXT_EVENT_NOTHING
};

enum nabto_coap_client_status {
    NABTO_COAP_CLIENT_STATUS_OK,
    NABTO_COAP_CLIENT_STATUS_DECODE_ERROR,
    NABTO_COAP_CLIENT_STATUS_TIMEOUT,
    NABTO_COAP_CLIENT_STATUS_IN_PROGRESS,
    NABTO_COAP_CLIENT_STATUS_STOPPED
};


struct nabto_coap_client_settings {
    uint32_t ackTimeoutMilliseconds;
    uint8_t maxRetransmits;
};

struct nabto_coap_client_response;
struct nabto_coap_client_request;

// Called when a response to a request is ready and the request can be freed.
typedef void (*nabto_coap_client_request_end_handler)(struct nabto_coap_client_request* request, void* userData);

struct nabto_coap_client {
    struct nabto_coap_client_settings settings;
    // list of requests in the client
    struct nabto_coap_client_request* requestsSentinel;
    uint16_t messageIdCounter;
    uint64_t tokenCounter;

    // Notify the implementer that an event has happened.
    nabto_coap_notify_event notifyEvent;

    // Userdata from the implementer.
    void* userData;

    bool needSendRst;
    uint16_t messageIdRst;
    void* connectionRst;

    bool needSendAck;
    uint16_t messageIdAck;
    void* connectionAck;
};


/****************************************
 * called from clients using the module *
 ****************************************/
/**
 * Create a new coap request.
 *
 * @param path is encoded as null terminated strings
 * e.g. /string1/string2/string3 => const char** pathSegments =
 * {"string1", "string2", "string3"}; since these path segments is
 * likely to be in the flash they are required to be kept alive by the
 * caller until the request is freed.
 */
struct nabto_coap_client_request* nabto_coap_client_request_new(struct nabto_coap_client* client, nabto_coap_method method, size_t pathSegmentsLength, const char** pathSegments, nabto_coap_client_request_end_handler endHandler, void* endHandlerUserData, void* connection);

/**
 * Send the coap request, meaning we are finished with building the request
 */
void nabto_coap_client_request_send(struct nabto_coap_client_request* request);

/**
 * Cancel a sent request.
 */
void nabto_coap_client_request_cancel(struct nabto_coap_client_request* request);

/**
 * free a request
 */
void nabto_coap_client_request_free(struct nabto_coap_client_request* request);

/**
 * set the content format of the request body
 */
void nabto_coap_client_request_set_content_format(struct nabto_coap_client_request* request, uint16_t format);

nabto_coap_error nabto_coap_client_request_set_payload(struct nabto_coap_client_request* request, void* payload, size_t payloadLength);

void nabto_coap_client_request_set_nonconfirmable(struct nabto_coap_client_request* request);

/**
 * Sets the timeout for the server to provide a response within
 */
void nabto_coap_client_request_set_timeout(struct nabto_coap_client_request* request, uint32_t timeout);

/**
 * Remove a connection if it has been closed
 */
void nabto_coap_client_remove_connection(struct nabto_coap_client* client, void *connection);

/**
 * set the expeched content format of the response body.
 * this can be called several times.
 */
//void nabto_coap_client_request_accept(struct nabto_coap_client_request* request, uint16_t format);

//void nabto_coap_client_request_observe(struct nabto_coap_client_request* request);

/**
 * Return status of a request
 *
 * @return
 *   NABTO_COAP_CLIENT_STATUS_OK           if ok.
 *   NABTO_COAP_CLIENT_STATUS_TIMEOUT      if request has timedout.
 *   NABTO_COAP_CLIENT_STATUS_IN_PROGRESS  if request is in progress.
 */
enum nabto_coap_client_status nabto_coap_client_request_get_status(struct nabto_coap_client_request* request);

struct nabto_coap_client_response* nabto_coap_client_request_get_response(struct nabto_coap_client_request* request);

uint16_t nabto_coap_client_response_get_code(struct nabto_coap_client_response* response);
bool nabto_coap_client_response_get_content_format(struct nabto_coap_client_response* response, uint16_t* contentFormat);
bool nabto_coap_client_response_get_payload(struct nabto_coap_client_response* response, const uint8_t** payload, size_t* payloadLength);


/*****************************************
 * called from the module implementation *
 *****************************************/

nabto_coap_error nabto_coap_client_init(struct nabto_coap_client* client, nabto_coap_notify_event notifyEvent, void* userData);

void nabto_coap_client_destroy(struct nabto_coap_client* client);

/**
 * Stop the client, finish all outstanding handlers.
 *
 * All outstanding request will be freed by the requestor.
 */
void nabto_coap_client_stop(struct nabto_coap_client* client);

enum nabto_coap_client_next_event nabto_coap_client_get_next_event(struct nabto_coap_client* client, uint32_t now);

void nabto_coap_client_handle_timeout(struct nabto_coap_client* client, uint32_t now);
void nabto_coap_client_handle_callback(struct nabto_coap_client* client);
uint8_t* nabto_coap_client_create_packet(struct nabto_coap_client* client, uint32_t stamp, uint8_t* packet, uint8_t* end, void** connection);
void nabto_coap_client_packet_sent(struct nabto_coap_client* client);

uint32_t nabto_coap_client_get_next_timeout(struct nabto_coap_client* client, uint32_t now);

enum nabto_coap_client_status nabto_coap_client_handle_packet(struct nabto_coap_client* client, uint32_t now, const uint8_t* packet, size_t packetSize, void* connection);

#ifdef __cplusplus
}
#endif

#endif
