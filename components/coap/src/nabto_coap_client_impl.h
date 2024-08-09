#ifndef _NABTO_COAP_CLIENT_IMPL_H_
#define _NABTO_COAP_CLIENT_IMPL_H_

#include <nabto_coap/nabto_coap_client.h>

enum nabto_coap_client_request_state {
    // the request is not ready to be sent yet
    NABTO_COAP_CLIENT_REQUEST_STATE_IDLE,

    // Send a coap request.
    NABTO_COAP_CLIENT_REQUEST_STATE_SEND_REQUEST,

    // Wait for ack on our request.
    NABTO_COAP_CLIENT_REQUEST_STATE_WAIT_ACK,

    // We have received an ack wait for response.
    NABTO_COAP_CLIENT_REQUEST_STATE_WAIT_RESPONSE,

    // The request is done, invoke a callback to inform the request owner that the request is done.
    NABTO_COAP_CLIENT_REQUEST_STATE_DONE_CALLBACK,

    // The request is done, wait for the client to free it.
    NABTO_COAP_CLIENT_REQUEST_STATE_DONE
};

struct nabto_coap_client_request;

struct nabto_coap_client_request {
    struct nabto_coap_client* client;
    struct nabto_coap_client_request* next;
    struct nabto_coap_client_request* prev;
    enum nabto_coap_client_request_state state;
    uint32_t timeoutStamp;
    uint16_t messageId;
    uint8_t retransmissions;
    nabto_coap_type type;
    nabto_coap_code method;
    nabto_coap_token token;

    // the path segments is owned by the caller
    size_t pathSegmentsLength;
    const char** pathSegments;

    uint32_t configuredTimeoutMilliseconds;

    bool hasContentFormat;
    uint16_t contentFormat;

    // The payload is copied into the request
    uint8_t* payload;
    size_t payloadLength;

    uint32_t block1Size; // (16 << block1size) is the actual block size.
    uint32_t block1Current;

    bool hasBlock2;
    uint32_t block2;

    // Called when we are done processing the request and it can be
    // freed appropriately
    nabto_coap_client_request_end_handler endHandler;
    void* endHandlerUserData;

    enum nabto_coap_client_status status;
    struct nabto_coap_client_response* response;
    void* connection;
};

struct nabto_coap_client_response {
    struct nabto_coap_client_request* request;
    nabto_coap_code code;
    bool hasContentFormat;
    uint16_t contentFormat;
    uint8_t* payload;
    size_t payloadLength;
    uint16_t messageId;
};

#endif
