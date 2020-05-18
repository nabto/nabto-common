#ifndef _NABTO_COAP_SERVER_IMPL_H_
#define _NABTO_COAP_SERVER_IMPL_H_

#include <coap/nabto_coap_server.h>

#ifdef __cplusplus
extern "C" {
#endif


enum nabto_coap_server_request_state {
    NABTO_COAP_SERVER_REQUEST_STATE_REQUEST, // The request is being received
    NABTO_COAP_SERVER_REQUEST_STATE_USER, // The application server is handling the request
    NABTO_COAP_SERVER_REQUEST_STATE_RESPONSE, // The response is being sent
    NABTO_COAP_SERVER_REQUEST_STATE_DONE // The request is done and ready to be freed.
};

struct nabto_coap_router_parameter;

struct nabto_coap_server_request_parameter;
struct nabto_coap_server_request_parameter {
    struct nabto_coap_server_request_parameter* next;
    struct nabto_coap_server_request_parameter* prev;
    struct nabto_coap_router_parameter* parameter;
    char* value;
};

struct nabto_coap_server_request;

struct nabto_coap_server_response {
    struct nabto_coap_server* server;
    struct nabto_coap_server_request* request;
    bool sendNow;
    uint8_t retransmissions;
    uint32_t timeout;
    uint16_t messageId;
    nabto_coap_code code;
    bool hasContentFormat;
    uint16_t contentFormat;

    bool hasBlock2;

    uint8_t* payload;
    size_t payloadLength;
    bool staticPayload;

    uint32_t block2Size;
    uint32_t block2Current;
};

struct nabto_coap_server_request {
    struct nabto_coap_server* server;
    struct nabto_coap_server_request* next;
    struct nabto_coap_server_request* prev;
    struct nabto_coap_server_resource_handler* handler;
    enum nabto_coap_server_request_state state;
    uint16_t messageId;
    nabto_coap_token token;
    nabto_coap_type type;
    nabto_coap_code method;
    void* connection;
    bool hasContentFormat;
    uint16_t contentFormat;
    uint8_t* payload;

    size_t payloadLength;

    bool hasBlock1Ack;
    uint32_t block1Ack;

    bool handled;
    bool isFreed;

    struct nabto_coap_server_response response;
    struct nabto_coap_server_resource* resource;

    struct nabto_coap_server_request_parameter parameterSentinel;
};

#define NABTO_COAP_MAX_PATH_LENGTH 256

struct nabto_coap_router_node;

struct nabto_coap_server_resource {
    nabto_coap_server_resource_handler handler;
    void* handlerUserData;
};

struct nabto_coap_router_path_segment;

struct nabto_coap_router_path_segment {
    struct nabto_coap_router_path_segment* next;
    struct nabto_coap_router_path_segment* prev;
    char* segment;
    struct nabto_coap_router_node* node;
};

struct nabto_coap_router_parameter {
    char* name;
    struct nabto_coap_router_node* node;
};

struct nabto_coap_router_node {
    struct nabto_coap_router_path_segment pathSegmentsSentinel;
    struct nabto_coap_router_parameter parameter;
    struct nabto_coap_server_resource getHandler;
    struct nabto_coap_server_resource postHandler;
    struct nabto_coap_server_resource putHandler;
    struct nabto_coap_server_resource deleteHandler;
};

void nabto_coap_server_insert_request_into_list(struct nabto_coap_server_request* e1, struct nabto_coap_server_request* request);

struct nabto_coap_server_request* nabto_coap_server_find_request(struct nabto_coap_server* server, nabto_coap_token* token, void* conneciton);

struct nabto_coap_server_response* nabto_coap_server_find_response(struct nabto_coap_server* server, uint16_t messageId, void* conneciton);

bool nabto_coap_server_match_resource(struct nabto_coap_server_resource* resource, uint8_t* options);

void nabto_coap_server_free_request(struct nabto_coap_server_request* request);


uint16_t nabto_coap_server_next_message_id(struct nabto_coap_server* server);

struct nabto_coap_router_node* nabto_coap_router_node_new();
void nabto_coap_router_node_free(struct nabto_coap_router_node* node);

struct nabto_coap_router_path_segment* nabto_coap_router_path_segment_new();
void nabto_coap_router_path_segment_free(struct nabto_coap_router_path_segment* segment);

nabto_coap_error nabto_coap_server_add_resource_into_tree(struct nabto_coap_router_node* parent, nabto_coap_code method, const char** path, nabto_coap_server_resource_handler handler, void* userData, struct nabto_coap_server_resource** resource);

struct nabto_coap_router_path_segment* nabto_coap_server_find_path_segment(struct nabto_coap_router_node* node, const char* segment, size_t segmentLength);

struct nabto_coap_server_request_parameter* nabto_coap_server_request_parameter_new();

#ifdef __cplusplus
} // extern "C"
#endif

#endif
