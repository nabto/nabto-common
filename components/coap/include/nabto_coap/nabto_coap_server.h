#ifndef _NABTO_COAP_SERVER_H_
#define _NABTO_COAP_SERVER_H_

#include <nn/allocator.h>
#include <nn/log.h>
#include <nn/string_map.h>

#include "nabto_coap.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NABTO_COAP_SERVER_LOG_MODULE "coap_server"
struct nabto_coap_server;

enum nabto_coap_server_next_event {
    NABTO_COAP_SERVER_NEXT_EVENT_SEND,
    NABTO_COAP_SERVER_NEXT_EVENT_WAIT,
    NABTO_COAP_SERVER_NEXT_EVENT_NOTHING
};

typedef void* (*nabto_coap_alloc)(size_t size);
typedef void (*nabto_coap_free)(void* data);

struct nabto_coap_server_request;
struct nabto_coap_server_response;
struct nabto_coap_server_resource;
struct nabto_coap_server_request_parameter;

struct nabto_coap_server {
    struct nn_log* logger;
    struct nn_allocator allocator;
    struct nabto_coap_router_node* root;
    uint32_t ackTimeout;
};

/**
 * For each server there can be several request contexts. Each requests context
 * handles all the retransmissions etc for the group of requests. On some
 * systems it can be beneficial to have a requests context pr connection.
 */
struct nabto_coap_server_requests {
    struct nabto_coap_server* server;
    struct nabto_coap_server_request* requestsSentinel;

    nabto_coap_get_stamp getStamp;
    // notify implementation that an event has potentially occured.
    nabto_coap_notify_event notifyEvent;

    // userData for getStamp and notifyEvent
    void* userData;

    uint16_t messageId;

    // if we need to send an error back. If errorConnection is non
    // null we need to send an error back else there's no error to
    // send back.
    void*            errorConnection;
    uint8_t          errorCode;
    nabto_coap_token errorToken;
    uint16_t         errorMessageId;
    const void*      errorPayload;
    size_t           errorPayloadLength;

    uint16_t ackMessageId;
    void* ackConnection;

    size_t maxRequests; // max concurrent requests
    size_t activeRequests;
};

nabto_coap_error nabto_coap_server_init(struct nabto_coap_server* server, struct nn_log* logger, struct nn_allocator* allocator);

nabto_coap_error nabto_coap_server_requests_init(struct nabto_coap_server_requests* requests, struct nabto_coap_server* server, nabto_coap_get_stamp getStamp, nabto_coap_notify_event notifyEvent, void* userData);

void nabto_coap_server_destroy(struct nabto_coap_server* server);

void nabto_coap_server_requests_destroy(struct nabto_coap_server_requests* requests);


void nabto_coap_server_limit_requests(struct nabto_coap_server_requests* requests, size_t limit);

#define NABTO_COAP_SERVER_LOG_TRACE(fmt, args) do { printf(fmt, args); } while(0);

/**
 * Add resource handlers to a server
 */
typedef void (*nabto_coap_server_resource_handler)(struct nabto_coap_server_request* request, void* userData);

/**
 * Add a resource to the coap server.
 *
 * The path of the url is split into a null terminated segments
 * array. i.e. /iam/users/{user}/roles/{role} is encoded as { "iam",
 * "users", "{user}", "roles", "{role}", NULL }
 */
nabto_coap_error nabto_coap_server_add_resource(struct nabto_coap_server* server, nabto_coap_code method, const char** segments, nabto_coap_server_resource_handler handler, void* userData, struct nabto_coap_server_resource** resource);

void nabto_coap_server_remove_resource(struct nabto_coap_server_resource* resource);

/**
 * Find the userdata of a previously added resource.
 * parameters must be an initialized string map
 */
void* nabto_coap_server_find_resource_data(struct nabto_coap_server* server, nabto_coap_code method, const char** segments, struct nn_string_map* parameters);


nabto_coap_error nabto_coap_server_send_error_response(struct nabto_coap_server_request* request, nabto_coap_code status, const char* description);

void nabto_coap_server_response_set_code(struct nabto_coap_server_request* request, nabto_coap_code code);
void nabto_coap_server_response_set_code_human(struct nabto_coap_server_request* request, uint16_t humanCode);

nabto_coap_error nabto_coap_server_response_set_payload(struct nabto_coap_server_request* request, const void* data, size_t dataSize);

void nabto_coap_server_response_set_content_format(struct nabto_coap_server_request* request, uint16_t format);

nabto_coap_error nabto_coap_server_response_ready(struct nabto_coap_server_request* request);

void nabto_coap_server_request_free(struct nabto_coap_server_request* request);

/**
 * request functions
 */

/**
 * Get content format, if no content format is present return -1 else
 * a contentFormat between 0 and 2^16-1 is returned.
 */
int32_t nabto_coap_server_request_get_content_format(struct nabto_coap_server_request* request);

bool nabto_coap_server_request_get_payload(struct nabto_coap_server_request* request, void** payload, size_t* payloadLength);

void* nabto_coap_server_request_get_connection(struct nabto_coap_server_request* request);

const char* nabto_coap_server_request_get_parameter(struct nabto_coap_server_request* request, const char* parameter);

/**
 * handle packets and events
 */
void nabto_coap_server_handle_packet(struct nabto_coap_server_requests* requests, void* connection, const uint8_t* packet, size_t packetSize);

void nabto_coap_server_remove_connection(struct nabto_coap_server_requests* requests, void* connection);

enum nabto_coap_server_next_event nabto_coap_server_next_event(struct nabto_coap_server_requests* requests);

/**
 * @return The ptr where bytes have been written till in the buffer.
 *   The number of bytes written is calculated as (ptr - buffer). If
 *   the function returns NULL there is not a valid packet in the
 *   buffer. If non null is returned, the ptr is between buffer and
 *   end.
 */
uint8_t* nabto_coap_server_handle_send(struct nabto_coap_server_requests* requests, uint8_t* buffer, uint8_t* end);
void nabto_coap_server_handle_timeout(struct nabto_coap_server_requests* requests);

bool nabto_coap_server_get_next_timeout(struct nabto_coap_server_requests* requests, uint32_t* nextTimeout);
int32_t nabto_coap_server_stamp_diff_now(struct nabto_coap_server_requests* requests, uint32_t stamp);
uint32_t nabto_coap_server_stamp_now(struct nabto_coap_server_requests* requests);


// get the connection the server wants to send data on.
void* nabto_coap_server_get_connection_send(struct nabto_coap_server_requests* requests);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
