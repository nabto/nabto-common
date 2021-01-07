#include <coap/nabto_coap_server.h>
#include "nabto_coap_server_impl.h"

#include <stdlib.h>

const char* unhandledRequest = "Request unhandled";

nabto_coap_error nabto_coap_server_init(struct nabto_coap_server* server, nabto_coap_get_stamp getStamp, nabto_coap_notify_event notifyEvent, void* userData)
{
    memset(server, 0, sizeof(struct nabto_coap_server));
    server->getStamp = getStamp;
    server->notifyEvent = notifyEvent;
    server->userData = userData;
    server->ackTimeout = NABTO_COAP_ACK_TIMEOUT;
    server->maxRequests = SIZE_MAX;

    // init requests list
    server->requestsSentinel = calloc(1, sizeof(struct nabto_coap_server_request));
    if (server->requestsSentinel == NULL) {
        return NABTO_COAP_ERROR_OUT_OF_MEMORY;
    }
    server->requestsSentinel->next = server->requestsSentinel;
    server->requestsSentinel->prev = server->requestsSentinel;

    server->root = nabto_coap_router_node_new();
    if (server->root == NULL) {
        free(server->requestsSentinel);
        return NABTO_COAP_ERROR_OUT_OF_MEMORY;
    }
    return NABTO_COAP_ERROR_OK;
}

void nabto_coap_server_destroy(struct nabto_coap_server* server)
{
    struct nabto_coap_server_request* iterator = server->requestsSentinel->next;
    while(iterator != server->requestsSentinel) {
        struct nabto_coap_server_request* current = iterator;
        iterator = iterator->next;
        current->state = NABTO_COAP_SERVER_REQUEST_STATE_DONE;
        nabto_coap_server_free_request(current);
    }
    free(server->requestsSentinel);

    nabto_coap_router_node_free(server->root);
    server->requestsSentinel = NULL;
}

void nabto_coap_server_limit_requests(struct nabto_coap_server* server, size_t limit)
{
    server->maxRequests = limit;
}


void nabto_coap_server_request_free(struct nabto_coap_server_request* request)
{
    if (request->state == NABTO_COAP_SERVER_REQUEST_STATE_REQUEST ||
        request->state == NABTO_COAP_SERVER_REQUEST_STATE_USER)
    {
        request->response.staticPayload = true;
        nabto_coap_server_response_set_code(request, NABTO_COAP_CODE_INTERNAL_SERVER_ERROR);
        request->response.payload = (void*)unhandledRequest;
        request->response.payloadLength = strlen(unhandledRequest);
        if (strlen(unhandledRequest) > (16 << request->response.block2Size)) {
            request->response.hasBlock2 = true;
            request->response.block2Current = 0;
        }
        nabto_coap_server_response_ready(request);
    }
    request->isFreed = true;
    nabto_coap_server_free_request(request);
}

// insert request after e1 such that the chain e1->e2->e3 emerge
void nabto_coap_server_insert_request_into_list(struct nabto_coap_server_request* e1, struct nabto_coap_server_request* request)
{
    struct nabto_coap_server_request* e2 = request;
    struct nabto_coap_server_request* e3 = e1->next;
    e1->next = e2;
    e2->next = e3;
    e3->prev = e2;
    e2->prev = e1;
}

// remove request such that e1->request->e2 becomes e1->e2
void nabto_coap_server_remove_request_from_list(struct nabto_coap_server_request* request)
{
    struct nabto_coap_server_request* e1 = request->prev;
    struct nabto_coap_server_request* e2 = request->next;
    e1->next = e2;
    e2->prev = e1;
    request->prev = request;
    request->next = request;
}

void nabto_coap_server_handle_timeout(struct nabto_coap_server* server)
{
    uint32_t now = server->getStamp(server->userData);
    struct nabto_coap_server_request* request = server->requestsSentinel->next;
    while(request != server->requestsSentinel) {
        if (nabto_coap_is_stamp_less_equal(request->response.timeout, now))
        {
            if (request->response.retransmissions > NABTO_COAP_MAX_RETRANSMITS) {
                request->state = NABTO_COAP_SERVER_REQUEST_STATE_DONE;
                nabto_coap_server_free_request(request);
                return; // free modifies the pointer we need to
                        // schedule a new timeout of more requests has
                        // timed out.
            }
            request->response.sendNow = true;

        }
        request = request->next;
    }
}

enum nabto_coap_server_next_event nabto_coap_server_next_event(struct nabto_coap_server* server)
{
    if (server->errorConnection != NULL ||
        server->ackConnection != NULL) {
        return NABTO_COAP_SERVER_NEXT_EVENT_SEND;
    }

    struct nabto_coap_server_request* request = server->requestsSentinel->next;
    while(request != server->requestsSentinel) {
        if ((request->state == NABTO_COAP_SERVER_REQUEST_STATE_REQUEST ||
             request->state == NABTO_COAP_SERVER_REQUEST_STATE_USER) &&
            request->hasBlock1Ack)
        {
            return NABTO_COAP_SERVER_NEXT_EVENT_SEND;
        } else if (request->state == NABTO_COAP_SERVER_REQUEST_STATE_RESPONSE &&
                   request->response.sendNow)
        {
            return NABTO_COAP_SERVER_NEXT_EVENT_SEND;
        }
        request = request->next;
    }

    request = server->requestsSentinel->next;
    while(request != server->requestsSentinel) {
        if (request->state == NABTO_COAP_SERVER_REQUEST_STATE_RESPONSE) {
            // a response has a timeout.
            return NABTO_COAP_SERVER_NEXT_EVENT_WAIT;
        }
        request = request->next;
    }

    return NABTO_COAP_SERVER_NEXT_EVENT_NOTHING;
}

bool nabto_coap_server_get_next_timeout(struct nabto_coap_server* server, uint32_t* nextTimeout)
{
    bool first = true;
    struct nabto_coap_server_request* request = server->requestsSentinel->next;
    while(request != server->requestsSentinel) {
        if (request->state == NABTO_COAP_SERVER_REQUEST_STATE_RESPONSE) {
            if (first) {
                first = false;
                *nextTimeout = request->response.timeout;
            } else {
                *nextTimeout = nabto_coap_stamp_min(*nextTimeout, request->response.timeout);
            }
        }
        request = request->next;
    }
    return !first;
}


void* nabto_coap_server_get_connection_send(struct nabto_coap_server* server)
{
    if (server->errorConnection) {
        return server->errorConnection;
    }

    if (server->ackConnection) {
        return server->ackConnection;
    }

    struct nabto_coap_server_request* request = server->requestsSentinel->next;
    while(request != server->requestsSentinel) {
        if ((request->state == NABTO_COAP_SERVER_REQUEST_STATE_REQUEST ||
             request->state == NABTO_COAP_SERVER_REQUEST_STATE_USER) &&
            request->hasBlock1Ack)
        {
            return request->connection;
        } else if (request->state == NABTO_COAP_SERVER_REQUEST_STATE_RESPONSE &&
                   request->response.sendNow)
        {
            return request->connection;
        }
        request = request->next;
    }
    return NULL;
}

uint8_t* nabto_coap_server_send_ack(struct nabto_coap_server* server, uint8_t* buffer, uint8_t* end)
{
    uint8_t* ptr = buffer;

    struct nabto_coap_message_header header;
    memset(&header, 0, sizeof(struct nabto_coap_message_header));
    header.type = NABTO_COAP_TYPE_ACK;
    header.code = NABTO_COAP_CODE_EMPTY;
    header.messageId = server->ackMessageId;

    ptr = nabto_coap_encode_header(&header, ptr, end);

    server->ackConnection = NULL;

    return ptr;
}

uint8_t* nabto_coap_server_send_error(struct nabto_coap_server* server, uint8_t* buffer, uint8_t* end)
{
    uint8_t* ptr = buffer;

    struct nabto_coap_message_header header;
    memset(&header, 0, sizeof(struct nabto_coap_message_header));
    header.type = NABTO_COAP_TYPE_NON;
    header.code = (nabto_coap_code)server->errorCode;
    header.messageId = server->errorMessageId;
    header.token = server->errorToken;

    ptr = nabto_coap_encode_header(&header, ptr, end);

    ptr = nabto_coap_encode_payload(server->errorPayload, server->errorPayloadLength, ptr, end);

    server->errorConnection = NULL;
    server->errorCode = 0;
    server->errorMessageId = 0;
    server->errorPayload = NULL;
    server->errorPayloadLength = 0;
    return ptr;
}


static uint8_t* nabto_coap_server_send_in_request_state(struct nabto_coap_server* server, struct nabto_coap_server_request* request, uint8_t* buffer, uint8_t* end)
{
    uint8_t* ptr = buffer;
    struct nabto_coap_message_header header;
    memset(&header, 0, sizeof(struct nabto_coap_message_header));
    header.type = request->type;
    header.token = request->token;

    if (request->hasBlock1Ack) {
        header.type = NABTO_COAP_TYPE_ACK;
        header.code = NABTO_COAP_CODE_CONTINUE;
        header.messageId = request->messageId;

        ptr = nabto_coap_encode_header(&header, ptr, end);
        ptr = nabto_coap_encode_varint_option(NABTO_COAP_OPTION_BLOCK1, request->block1Ack, ptr, end);

        request->hasBlock1Ack = false;

        return ptr;
    }
    return NULL;
}

static uint8_t* nabto_coap_server_send_in_response_state(struct nabto_coap_server* server, struct nabto_coap_server_request* request, uint8_t* buffer, uint8_t* end)
{
    uint8_t* ptr = buffer;
    struct nabto_coap_server_response* response = &request->response;
    struct nabto_coap_message_header header;
    memset(&header, 0, sizeof(struct nabto_coap_message_header));
    header.type = request->type;
    header.token = request->token;
    header.code = response->code;
    header.messageId = response->messageId;

    ptr = nabto_coap_encode_header(&header, ptr, end);

    uint16_t currentOption = 0;

    if (response->hasContentFormat) {
        uint16_t optionDelta = NABTO_COAP_OPTION_CONTENT_FORMAT - currentOption;
        ptr = nabto_coap_encode_varint_option(optionDelta, response->contentFormat, ptr, end);
        currentOption = NABTO_COAP_OPTION_CONTENT_FORMAT;
    }

    size_t blockSize = (16 << response->block2Size);
    size_t payloadOffset = (response->block2Current * blockSize);
    size_t payloadRestLength = response->payloadLength - payloadOffset;

    if (response->payloadLength > blockSize) {
        uint32_t blockMore = 1;
        if (payloadRestLength <= blockSize) {
            blockMore = 0;
        }

        uint32_t blockOption = (response->block2Current << 4) + (blockMore << 3) + response->block2Size;
        uint16_t optionDelta = NABTO_COAP_OPTION_BLOCK2 - currentOption;
        ptr = nabto_coap_encode_varint_option(optionDelta, blockOption, ptr, end);
        currentOption = NABTO_COAP_OPTION_BLOCK2;
    }

    // we should send a block1 option back if the request had block1 options and this is the first packet in the response.
    if (request->block1Ack > 0 && payloadOffset == 0) {
        uint16_t optionDelta = NABTO_COAP_OPTION_BLOCK1 - currentOption;
        ptr = nabto_coap_encode_varint_option(optionDelta, request->block1Ack, ptr, end);
        currentOption = NABTO_COAP_OPTION_BLOCK1;
    }

    if (payloadRestLength > blockSize) {
        payloadRestLength = blockSize;
    }
    uint8_t* payloadRestStart = response->payload + payloadOffset;
    ptr = nabto_coap_encode_payload(payloadRestStart, payloadRestLength, ptr, end);

    if (request->type == NABTO_COAP_TYPE_NON) {
        // NONs should not be retransmitted let it expire asap
        response->retransmissions += NABTO_COAP_MAX_RETRANSMITS + 2; // large enough to expire
        response->timeout = nabto_coap_server_stamp_now(server);
        response->sendNow = false;
    } else {
        response->sendNow = false;
        response->timeout = nabto_coap_server_stamp_now(server);
        response->timeout += (server->ackTimeout) << response->retransmissions;
        response->retransmissions += 1;
    }

    return ptr;
}


uint8_t* nabto_coap_server_handle_send(struct nabto_coap_server* server, uint8_t* buffer, uint8_t* end)
{
    if (server->errorConnection) {
        return nabto_coap_server_send_error(server, buffer, end);
    }

    if (server->ackConnection) {
        return nabto_coap_server_send_ack(server, buffer, end);
    }

    struct nabto_coap_server_request* request = server->requestsSentinel->next;
    while(request != server->requestsSentinel) {
        if ((request->state == NABTO_COAP_SERVER_REQUEST_STATE_REQUEST ||
             request->state == NABTO_COAP_SERVER_REQUEST_STATE_USER) &&
            request->hasBlock1Ack)
        {
            return nabto_coap_server_send_in_request_state(server, request, buffer, end);
        } else if (request->state == NABTO_COAP_SERVER_REQUEST_STATE_RESPONSE &&
                   request->response.sendNow)
        {
            return nabto_coap_server_send_in_response_state(server, request, buffer, end);
        }
        request = request->next;
    }

    return NULL;
}

void nabto_coap_server_free_request(struct nabto_coap_server_request* request)
{
    struct nabto_coap_server* server = request->server;
    if (!request->isFreed || request->state != NABTO_COAP_SERVER_REQUEST_STATE_DONE) {
        // dont free before user frees and server is done
        return;
    }
    nabto_coap_server_remove_request_from_list(request);

    if (request->payload) {
        free(request->payload);
    }

    if (!request->response.staticPayload && request->response.payload) {
        free(request->response.payload);
    }

    struct nabto_coap_server_request_parameter* iterator = request->parameterSentinel.next;
    while(iterator != &request->parameterSentinel) {
        struct nabto_coap_server_request_parameter* current = iterator;
        iterator = iterator->next;
        free(current->value);
        free(current);
    }


    free(request);

    server->activeRequests--;
}

uint32_t nabto_coap_server_stamp_now(struct nabto_coap_server* server)
{
    return server->getStamp(server->userData);
}

void nabto_coap_server_remove_resource(struct nabto_coap_server_resource* resource)
{
    resource->handler = NULL;
    resource->handlerUserData = NULL;
}

nabto_coap_error nabto_coap_server_add_resource(struct nabto_coap_server* server, nabto_coap_code method, const char** segments, nabto_coap_server_resource_handler handler, void* userData, struct nabto_coap_server_resource** resource)
{
    return nabto_coap_server_add_resource_into_tree(server->root, method, segments, handler, userData, resource);
}

struct nabto_coap_router_path_segment* nabto_coap_server_find_path_segment(struct nabto_coap_router_node* node, const char* segment, size_t segmentLength)
{
    struct nabto_coap_router_path_segment* iterator = node->pathSegmentsSentinel.next;
    while(iterator != &node->pathSegmentsSentinel) {
        size_t currentLength = strlen(iterator->segment);
        if (currentLength == segmentLength && memcmp(iterator->segment, segment, segmentLength) == 0) {
            return iterator;
        }
        iterator = iterator->next;
    }
    return NULL;
}

void nabto_coap_router_insert_path_segment(struct nabto_coap_router_path_segment* afterThis, struct nabto_coap_router_path_segment* segment)
{
    struct nabto_coap_router_path_segment* before = afterThis;
    struct nabto_coap_router_path_segment* after = afterThis->next;

    before->next = segment;
    segment->next = after;
    after->prev = segment;
    segment->prev = before;
}

nabto_coap_error nabto_coap_server_add_resource_into_tree(struct nabto_coap_router_node* parent, nabto_coap_code method, const char** path, nabto_coap_server_resource_handler handler, void* userData, struct nabto_coap_server_resource** userRes)
{
    if (*path == NULL) {
        struct nabto_coap_server_resource* resource = NULL;
        if (method == NABTO_COAP_CODE_GET) {
            resource = &parent->getHandler;
        } else if (method == NABTO_COAP_CODE_POST) {
            resource = &parent->postHandler;
        } else if (method == NABTO_COAP_CODE_PUT) {
            resource = &parent->putHandler;
        } else if (method == NABTO_COAP_CODE_DELETE) {
            resource = &parent->deleteHandler;
        } else {
        }
        if (resource) {
            resource->handler = handler;
            resource->handlerUserData = userData;
            *userRes = resource;
        }
    } else {
        const char* segment = *path;
        struct nabto_coap_router_node* child;
        // build further tree
        if (*segment == '{') {
            // this is a parameter
            if (parent->parameter.name == NULL) {
                char* parameterName = calloc(strlen(segment) - 1, 1); // remove start and end {}
                if (parameterName == NULL) {
                    return NABTO_COAP_ERROR_OUT_OF_MEMORY;
                }
                memcpy(parameterName, segment+1, strlen(segment)-2);
                parent->parameter.name = parameterName;
                parent->parameter.node = nabto_coap_router_node_new();
                if (parent->parameter.node == NULL) {
                    free(parameterName);
                    parent->parameter.name = NULL;
                    return NABTO_COAP_ERROR_OUT_OF_MEMORY;
                }
            } else if (strncmp(parent->parameter.name, segment+1, strlen(segment)-2) != 0) {
                // This is an unknown parameter on a parent where a parameter already exists
                return NABTO_COAP_ERROR_INVALID_PARAMETER;
            }
            child = parent->parameter.node;

        } else {
            struct nabto_coap_router_path_segment* pathSegment = nabto_coap_server_find_path_segment(parent, segment, strlen(segment));
            if (pathSegment == NULL) {
                pathSegment = nabto_coap_router_path_segment_new();
                if (pathSegment == NULL) {
                    return NABTO_COAP_ERROR_OUT_OF_MEMORY;
                }
                pathSegment->segment = strdup(segment);
                if (pathSegment->segment == NULL) {
                    free(pathSegment);
                    return NABTO_COAP_ERROR_OUT_OF_MEMORY;
                }
                pathSegment->node = nabto_coap_router_node_new();
                if (pathSegment->node == NULL) {
                    free(pathSegment->segment);
                    free(pathSegment);
                    return NABTO_COAP_ERROR_OUT_OF_MEMORY;
                }
                nabto_coap_router_insert_path_segment(parent->pathSegmentsSentinel.prev, pathSegment);
            }
            child = pathSegment->node;
        }

        return nabto_coap_server_add_resource_into_tree(child, method, path+1, handler, userData, userRes);
    }
    return NABTO_COAP_ERROR_OK;
}

nabto_coap_error nabto_coap_server_send_error_response(struct nabto_coap_server_request* request, nabto_coap_code status, const char* description)
{
    nabto_coap_server_response_set_code(request, status);
    if (description != NULL) {
        // rfc 7252 section 5.5.2 says a diagnostic message is utf8 but no content format should be given
        nabto_coap_error err = nabto_coap_server_response_set_payload(request, description, strlen(description));
        if (err != NABTO_COAP_ERROR_OK) {
            return err;
        }
    }
    return nabto_coap_server_response_ready(request);
}


void nabto_coap_server_response_set_code(struct nabto_coap_server_request* request, nabto_coap_code code)
{
    request->response.code = code;
}

void nabto_coap_server_response_set_code_human(struct nabto_coap_server_request* request, uint16_t humanCode)
{
    int code = humanCode % 100;
    int klass = humanCode / 100;
    nabto_coap_server_response_set_code(request, (nabto_coap_code)(NABTO_COAP_CODE(klass, code)));
}

nabto_coap_error nabto_coap_server_response_set_payload(struct nabto_coap_server_request* request, const void* data, size_t dataSize)
{
    request->response.payload = calloc(1, dataSize + 1);
    if (request->response.payload == NULL) {
        return NABTO_COAP_ERROR_OUT_OF_MEMORY;
    }
    memcpy(request->response.payload, data, dataSize);
    request->response.payloadLength = dataSize;
    if (dataSize > (16 << request->response.block2Size)) {
        request->response.hasBlock2 = true;
        request->response.block2Current = 0;
    }
    return NABTO_COAP_ERROR_OK;
}

void nabto_coap_server_response_set_content_format(struct nabto_coap_server_request* request, uint16_t contentFormat)
{
    request->response.hasContentFormat = true;
    request->response.contentFormat = contentFormat;

}

nabto_coap_error nabto_coap_server_response_ready(struct nabto_coap_server_request* request)
{
    if (request->connection == NULL) {
        // the connection is removed.
        request->state = NABTO_COAP_SERVER_REQUEST_STATE_DONE;
        //nabto_coap_server_free_request(request);
        return NABTO_COAP_ERROR_NO_CONNECTION;
    } else {
        request->state = NABTO_COAP_SERVER_REQUEST_STATE_RESPONSE;
        request->response.sendNow = true;
        request->server->notifyEvent(request->server->userData);
        return NABTO_COAP_ERROR_OK;
    }
}

int32_t nabto_coap_server_request_get_content_format(struct nabto_coap_server_request* request)
{
    if (request->hasContentFormat) {
        return request->contentFormat;
    }
    return -1;
}

bool nabto_coap_server_request_get_payload(struct nabto_coap_server_request* request, void** payload, size_t* payloadLength)
{
    *payload = request->payload;
    *payloadLength = request->payloadLength;
    return (request->payloadLength > 0);
}

void* nabto_coap_server_request_get_connection(struct nabto_coap_server_request* request)
{
    return request->connection;
}

const char* nabto_coap_server_request_get_parameter(struct nabto_coap_server_request* request, const char* parameter)
{
    struct nabto_coap_server_request_parameter* iterator = request->parameterSentinel.next;
    while(iterator != &request->parameterSentinel) {
        if (strcmp(iterator->parameter->name, parameter) == 0) {
            return iterator->value;
        }
        iterator = iterator->next;
    }
    return NULL;
}

void nabto_coap_server_remove_connection(struct nabto_coap_server* server, void* connection)
{
    // Loop over all requests terminate all request where the
    // connection is dead. Or mark them as connection dead so they
    // will die when the user code resolves the request.

    struct nabto_coap_server_request* it = server->requestsSentinel->next;
    while(it != server->requestsSentinel) {
        struct nabto_coap_server_request* current = it;
        it = it->next;

        if (current->connection == connection) {
            if (current->state == NABTO_COAP_SERVER_REQUEST_STATE_REQUEST) {
                current->isFreed = true;
                current->state = NABTO_COAP_SERVER_REQUEST_STATE_DONE;
                nabto_coap_server_free_request(current);
            } else  if (current->state == NABTO_COAP_SERVER_REQUEST_STATE_RESPONSE) {
                current->state = NABTO_COAP_SERVER_REQUEST_STATE_DONE;
                nabto_coap_server_free_request(current);
            } else {
                current->connection = NULL;
                // remove the connection when the response is ready.
            }
        }
    }
    if (server->ackConnection == connection) {
        server->ackConnection = NULL;
    }

    if (server->errorConnection == connection) {
        server->errorConnection = NULL;
    }
}

uint16_t nabto_coap_server_next_message_id(struct nabto_coap_server* server)
{
    server->messageId++;
    return server->messageId;
}

struct nabto_coap_router_node* nabto_coap_router_node_new()
{
    struct nabto_coap_router_node* node = calloc(1, sizeof(struct nabto_coap_router_node));
    if (node == NULL) {
        return NULL;
    }
    node->pathSegmentsSentinel.next = &node->pathSegmentsSentinel;
    node->pathSegmentsSentinel.prev = &node->pathSegmentsSentinel;
    return node;
}

void nabto_coap_router_node_free(struct nabto_coap_router_node* node)
{
    if (node->parameter.name != NULL) {
        nabto_coap_router_node_free(node->parameter.node);
        free(node->parameter.name);
    }

    struct nabto_coap_router_path_segment* iterator = node->pathSegmentsSentinel.next;
    while (iterator != &node->pathSegmentsSentinel) {
        struct nabto_coap_router_path_segment* current = iterator;
        iterator = iterator->next;
        nabto_coap_router_node_free(current->node);
        free(current->segment);
        free(current);
    }

    free(node);
}

struct nabto_coap_router_path_segment* nabto_coap_router_path_segment_new()
{
    struct nabto_coap_router_path_segment* segment = calloc(1, sizeof(struct nabto_coap_router_path_segment));
    return segment;
}


void nabto_coap_router_path_segment_free(struct nabto_coap_router_path_segment* segment)
{
    free(segment);
}
