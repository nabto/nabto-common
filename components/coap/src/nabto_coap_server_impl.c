#include <nabto_coap/nabto_coap_server.h>
#include "nabto_coap_server_impl.h"

#include <stdlib.h>
#include <nn/string.h>
#include <nn/string_map.h>

const char* unhandledRequest = "Request unhandled";

nabto_coap_error nabto_coap_server_init(struct nabto_coap_server* server, struct nn_log* logger, struct nn_allocator* allocator)
{
    memset(server, 0, sizeof(struct nabto_coap_server));
    server->logger = logger;
    server->allocator = *allocator;
    server->ackTimeout = NABTO_COAP_ACK_TIMEOUT;

    server->root = nabto_coap_router_node_new(server);
    if (server->root == NULL) {
        return NABTO_COAP_ERROR_OUT_OF_MEMORY;
    }
    return NABTO_COAP_ERROR_OK;
}

void nabto_coap_server_destroy(struct nabto_coap_server* server)
{
    nabto_coap_router_node_free(server, server->root);
}

nabto_coap_error nabto_coap_server_requests_init(struct nabto_coap_server_requests* requests, struct nabto_coap_server* server, nabto_coap_get_stamp getStamp, nabto_coap_notify_event notifyEvent, void* userData)
{
    memset(requests, 0, sizeof(struct nabto_coap_server_requests));
    requests->server = server;
    requests->getStamp = getStamp;
    requests->notifyEvent = notifyEvent;
    requests->userData = userData;
    requests->maxRequests = SIZE_MAX;

    // init requests list
    requests->requestsSentinel = server->allocator.calloc(1, sizeof(struct nabto_coap_server_request));
    if (requests->requestsSentinel == NULL) {
        return NABTO_COAP_ERROR_OUT_OF_MEMORY;
    }
    requests->requestsSentinel->next = requests->requestsSentinel;
    requests->requestsSentinel->prev = requests->requestsSentinel;

    // init observers list
    requests->observersSentinel = server->allocator.calloc(1, sizeof(struct nabto_coap_server_observer));
    if (requests->observersSentinel == NULL) {
        server->allocator.free(requests->requestsSentinel);
        requests->requestsSentinel = NULL;
        return NABTO_COAP_ERROR_OUT_OF_MEMORY;
    }
    requests->observersSentinel->next = requests->observersSentinel;
    requests->observersSentinel->prev = requests->observersSentinel;

    return NABTO_COAP_ERROR_OK;
}

/**
 * This function should be called after the user of the library has freed all
 * their coap server requests which they have references to.
 */
void nabto_coap_server_requests_destroy(struct nabto_coap_server_requests* requests)
{
    struct nabto_coap_server* server = requests->server;

    // Free all observers
    if (requests->observersSentinel) {
        struct nabto_coap_server_observer* obsIt = requests->observersSentinel->next;
        while (obsIt != requests->observersSentinel) {
            struct nabto_coap_server_observer* current = obsIt;
            obsIt = obsIt->next;
            nabto_coap_server_observer_free(current);
        }
        server->allocator.free(requests->observersSentinel);
        requests->observersSentinel = NULL;
    }

    struct nabto_coap_server_request* iterator = requests->requestsSentinel->next;
    while(iterator != requests->requestsSentinel) {
        struct nabto_coap_server_request* current = iterator;
        iterator = iterator->next;
        if (current->state == NABTO_COAP_SERVER_REQUEST_STATE_REQUEST) {
            current->isFreed = true;
        } else if (current->isFreed == false) {
            // A request is still owned by the user application. When the
            // application calls free on the request, bad things happens.
        }
        current->state = NABTO_COAP_SERVER_REQUEST_STATE_DONE;
        nabto_coap_server_free_request(current);
    }
    server->allocator.free(requests->requestsSentinel);

    requests->requestsSentinel = NULL;
}

void nabto_coap_server_limit_requests(struct nabto_coap_server_requests* requests, size_t limit)
{
    requests->maxRequests = limit;
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
        if (strlen(unhandledRequest) > (16u << request->response.block2Size)) {
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

void nabto_coap_server_handle_timeout(struct nabto_coap_server_requests* requests)
{
    uint32_t now = requests->getStamp(requests->userData);
    struct nabto_coap_server_request* request = requests->requestsSentinel->next;
    while(request != requests->requestsSentinel) {
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

    // Handle observer notification timeouts
    struct nabto_coap_server_observer* obs = requests->observersSentinel->next;
    while (obs != requests->observersSentinel) {
        struct nabto_coap_server_observer* current = obs;
        obs = obs->next;
        if (current->payload != NULL && !current->sendNow) {
            if (nabto_coap_is_stamp_less_equal(current->timeout, now)) {
                if (current->retransmissions > NABTO_COAP_MAX_RETRANSMITS) {
                    // Client is unreachable, remove observer
                    nabto_coap_server_observer_free(current);
                    return;
                }
                current->sendNow = true;
            }
        }
    }
}

enum nabto_coap_server_next_event nabto_coap_server_next_event(struct nabto_coap_server_requests* requests)
{
    if (requests->errorConnection != NULL ||
        requests->ackConnection != NULL) {
        return NABTO_COAP_SERVER_NEXT_EVENT_SEND;
    }

    struct nabto_coap_server_request* request = requests->requestsSentinel->next;
    while(request != requests->requestsSentinel) {
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

    // Check observers for sendNow
    struct nabto_coap_server_observer* obs = requests->observersSentinel->next;
    while (obs != requests->observersSentinel) {
        if (obs->sendNow) {
            return NABTO_COAP_SERVER_NEXT_EVENT_SEND;
        }
        obs = obs->next;
    }

    request = requests->requestsSentinel->next;
    while(request != requests->requestsSentinel) {
        if (request->state == NABTO_COAP_SERVER_REQUEST_STATE_RESPONSE) {
            // a response has a timeout.
            return NABTO_COAP_SERVER_NEXT_EVENT_WAIT;
        }
        request = request->next;
    }

    // Check observers for pending timeouts
    obs = requests->observersSentinel->next;
    while (obs != requests->observersSentinel) {
        if (obs->payload != NULL && !obs->sendNow) {
            return NABTO_COAP_SERVER_NEXT_EVENT_WAIT;
        }
        obs = obs->next;
    }

    return NABTO_COAP_SERVER_NEXT_EVENT_NOTHING;
}

bool nabto_coap_server_get_next_timeout(struct nabto_coap_server_requests* requests, uint32_t* nextTimeout)
{
    bool first = true;
    struct nabto_coap_server_request* request = requests->requestsSentinel->next;
    while(request != requests->requestsSentinel) {
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

    // Also check observer timeouts
    struct nabto_coap_server_observer* obs = requests->observersSentinel->next;
    while (obs != requests->observersSentinel) {
        if (obs->payload != NULL && !obs->sendNow) {
            if (first) {
                first = false;
                *nextTimeout = obs->timeout;
            } else {
                *nextTimeout = nabto_coap_stamp_min(*nextTimeout, obs->timeout);
            }
        }
        obs = obs->next;
    }

    return !first;
}


void* nabto_coap_server_get_connection_send(struct nabto_coap_server_requests* requests)
{
    if (requests->errorConnection) {
        return requests->errorConnection;
    }

    if (requests->ackConnection) {
        return requests->ackConnection;
    }

    struct nabto_coap_server_request* request = requests->requestsSentinel->next;
    while(request != requests->requestsSentinel) {
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

    // Check observers
    struct nabto_coap_server_observer* obs = requests->observersSentinel->next;
    while (obs != requests->observersSentinel) {
        if (obs->sendNow) {
            return obs->connection;
        }
        obs = obs->next;
    }

    return NULL;
}

uint8_t* nabto_coap_server_send_ack(struct nabto_coap_server_requests* requests, uint8_t* buffer, uint8_t* end)
{
    uint8_t* ptr = buffer;

    struct nabto_coap_message_header header;
    memset(&header, 0, sizeof(struct nabto_coap_message_header));
    header.type = NABTO_COAP_TYPE_ACK;
    header.code = NABTO_COAP_CODE_EMPTY;
    header.messageId = requests->ackMessageId;

    ptr = nabto_coap_encode_header(&header, ptr, end);

    requests->ackConnection = NULL;

    return ptr;
}

uint8_t* nabto_coap_server_send_error(struct nabto_coap_server_requests* requests, uint8_t* buffer, uint8_t* end)
{
    uint8_t* ptr = buffer;

    struct nabto_coap_message_header header;
    memset(&header, 0, sizeof(struct nabto_coap_message_header));
    header.type = NABTO_COAP_TYPE_NON;
    header.code = (nabto_coap_code)requests->errorCode;
    header.messageId = requests->errorMessageId;
    header.token = requests->errorToken;

    ptr = nabto_coap_encode_header(&header, ptr, end);

    ptr = nabto_coap_encode_payload(requests->errorPayload, requests->errorPayloadLength, ptr, end);

    requests->errorConnection = NULL;
    requests->errorCode = 0;
    requests->errorMessageId = 0;
    requests->errorPayload = NULL;
    requests->errorPayloadLength = 0;
    return ptr;
}


static uint8_t* nabto_coap_server_send_in_request_state(struct nabto_coap_server_requests* requests, struct nabto_coap_server_request* request, uint8_t* buffer, uint8_t* end)
{
    (void)requests;
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

static uint8_t* nabto_coap_server_send_in_response_state(struct nabto_coap_server_requests* requests, struct nabto_coap_server_request* request, uint8_t* buffer, uint8_t* end)
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

    // Include Observe option in initial response if observe was accepted
    if (request->observer != NULL) {
        ptr = nabto_coap_encode_varint_option(NABTO_COAP_OPTION_OBSERVE - currentOption, request->observer->sequenceNumber, ptr, end);
        currentOption = NABTO_COAP_OPTION_OBSERVE;
    }

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
        response->timeout = nabto_coap_server_stamp_now(requests);
        response->sendNow = false;
    } else {
        response->sendNow = false;
        response->timeout = nabto_coap_server_stamp_now(requests);
        response->timeout += (requests->server->ackTimeout) << response->retransmissions;
        response->retransmissions += 1;
    }

    return ptr;
}


static uint8_t* nabto_coap_server_send_observer_notification(struct nabto_coap_server_requests* requests, struct nabto_coap_server_observer* observer, uint8_t* buffer, uint8_t* end)
{
    uint8_t* ptr = buffer;
    struct nabto_coap_message_header header;
    memset(&header, 0, sizeof(struct nabto_coap_message_header));
    header.type = observer->notificationType;
    header.token = observer->token;
    header.code = observer->code;
    header.messageId = observer->messageId;

    ptr = nabto_coap_encode_header(&header, ptr, end);

    uint16_t currentOption = 0;

    // Observe option (6)
    ptr = nabto_coap_encode_varint_option(NABTO_COAP_OPTION_OBSERVE - currentOption, observer->sequenceNumber, ptr, end);
    currentOption = NABTO_COAP_OPTION_OBSERVE;

    // Content-Format option (12)
    if (observer->hasContentFormat) {
        uint16_t optionDelta = NABTO_COAP_OPTION_CONTENT_FORMAT - currentOption;
        ptr = nabto_coap_encode_varint_option(optionDelta, observer->contentFormat, ptr, end);
        currentOption = NABTO_COAP_OPTION_CONTENT_FORMAT;
    }

    // Payload
    ptr = nabto_coap_encode_payload(observer->payload, observer->payloadLength, ptr, end);

    if (observer->notificationType == NABTO_COAP_TYPE_CON) {
        observer->sendNow = false;
        observer->timeout = nabto_coap_server_stamp_now(requests);
        observer->timeout += (requests->server->ackTimeout) << observer->retransmissions;
        observer->retransmissions += 1;
    } else {
        // NON: no retransmission needed, clear notification state
        observer->sendNow = false;
        struct nabto_coap_server* server = requests->server;
        server->allocator.free(observer->payload);
        observer->payload = NULL;
        observer->payloadLength = 0;
    }

    return ptr;
}

uint8_t* nabto_coap_server_handle_send(struct nabto_coap_server_requests* requests, uint8_t* buffer, uint8_t* end)
{
    if (requests->errorConnection) {
        return nabto_coap_server_send_error(requests, buffer, end);
    }

    if (requests->ackConnection) {
        return nabto_coap_server_send_ack(requests, buffer, end);
    }

    struct nabto_coap_server_request* request = requests->requestsSentinel->next;
    while(request != requests->requestsSentinel) {
        if ((request->state == NABTO_COAP_SERVER_REQUEST_STATE_REQUEST ||
             request->state == NABTO_COAP_SERVER_REQUEST_STATE_USER) &&
            request->hasBlock1Ack)
        {
            return nabto_coap_server_send_in_request_state(requests, request, buffer, end);
        } else if (request->state == NABTO_COAP_SERVER_REQUEST_STATE_RESPONSE &&
                   request->response.sendNow)
        {
            return nabto_coap_server_send_in_response_state(requests, request, buffer, end);
        }
        request = request->next;
    }

    // Check observers
    struct nabto_coap_server_observer* obs = requests->observersSentinel->next;
    while (obs != requests->observersSentinel) {
        if (obs->sendNow) {
            return nabto_coap_server_send_observer_notification(requests, obs, buffer, end);
        }
        obs = obs->next;
    }

    return NULL;
}

void nabto_coap_server_free_request(struct nabto_coap_server_request* request)
{
    struct nabto_coap_server* server = request->requests->server;
    struct nabto_coap_server_requests* requests = request->requests;
    if (!request->isFreed || request->state != NABTO_COAP_SERVER_REQUEST_STATE_DONE) {
        // dont free before user frees and server is done
        return;
    }
    nabto_coap_server_remove_request_from_list(request);

    if (request->payload) {
        server->allocator.free(request->payload);
    }

    if (!request->response.staticPayload && request->response.payload) {
        server->allocator.free(request->response.payload);
    }

    struct nabto_coap_server_request_parameter* iterator = request->parameterSentinel.next;
    while(iterator != &request->parameterSentinel) {
        struct nabto_coap_server_request_parameter* current = iterator;
        iterator = iterator->next;
        server->allocator.free(current->value);
        server->allocator.free(current);
    }


    server->allocator.free(request);

    requests->activeRequests--;
}

uint32_t nabto_coap_server_stamp_now(struct nabto_coap_server_requests* requests)
{
    return requests->getStamp(requests->userData);
}

void nabto_coap_server_remove_resource(struct nabto_coap_server_resource* resource)
{
    resource->handler = NULL;
    resource->handlerUserData = NULL;
}

nabto_coap_error nabto_coap_server_add_resource(struct nabto_coap_server* server, nabto_coap_code method, const char** segments, nabto_coap_server_resource_handler handler, void* userData, struct nabto_coap_server_resource** resource)
{
    return nabto_coap_server_add_resource_into_tree(server, server->root, method, segments, handler, userData, resource);
}

void* nabto_coap_server_find_resource_data(struct nabto_coap_server* server, nabto_coap_code method, const char** segments, struct nn_string_map* parameters)
{
    struct nabto_coap_router_node* currentNode = server->root;
    size_t i = 0;
    const char* currentSegment = segments[i];

    while (currentSegment != NULL) {
        struct nabto_coap_router_path_segment* segment = nabto_coap_server_find_path_segment(currentNode, (const char*)currentSegment, strlen(currentSegment));
        if (segment) {
            // We found the current segment, moving on to the next or breaks if we reached the end.
            currentNode = segment->node;
            i++;
            currentSegment = segments[i];
        } else {
            // test if currentNode has a parameter
            if (currentNode->parameter.name != NULL) {
                if (parameters != NULL) {
                    struct nn_string_map_iterator it = nn_string_map_insert(parameters, currentNode->parameter.name, currentSegment);
                    if (nn_string_map_is_end(&it)) {
                        return NULL;
                    }
                }
                currentNode = currentNode->parameter.node;
            } else {
                // We did not find a matching segment nor a parameter. Resource does not exist
                return NULL;
            }
            i++;
            currentSegment = segments[i];
        }
    }

    if (method == NABTO_COAP_CODE_GET && currentNode->getHandler.handler) {
        return currentNode->getHandler.handlerUserData;
    } else if (method == NABTO_COAP_CODE_POST && currentNode->postHandler.handler) {
        return currentNode->postHandler.handlerUserData;
    } else if (method == NABTO_COAP_CODE_PUT && currentNode->putHandler.handler) {
        return currentNode->putHandler.handlerUserData;
    } else if (method == NABTO_COAP_CODE_DELETE && currentNode->deleteHandler.handler) {
        return currentNode->deleteHandler.handlerUserData;
    } else {
        return NULL;
    }
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

nabto_coap_error nabto_coap_server_add_resource_into_tree(struct nabto_coap_server* server, struct nabto_coap_router_node* parent, nabto_coap_code method, const char** path, nabto_coap_server_resource_handler handler, void* userData, struct nabto_coap_server_resource** userRes)
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
                char* parameterName = server->allocator.calloc(strlen(segment) - 1, 1); // remove start and end {}
                if (parameterName == NULL) {
                    return NABTO_COAP_ERROR_OUT_OF_MEMORY;
                }
                memcpy(parameterName, segment+1, strlen(segment)-2);
                parent->parameter.name = parameterName;
                parent->parameter.node = nabto_coap_router_node_new(server);
                if (parent->parameter.node == NULL) {
                    server->allocator.free(parameterName);
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
                pathSegment = nabto_coap_router_path_segment_new(server);
                if (pathSegment == NULL) {
                    return NABTO_COAP_ERROR_OUT_OF_MEMORY;
                }
                pathSegment->segment = nn_strdup(segment, &server->allocator);
                if (pathSegment->segment == NULL) {
                    server->allocator.free(pathSegment);
                    return NABTO_COAP_ERROR_OUT_OF_MEMORY;
                }
                pathSegment->node = nabto_coap_router_node_new(server);
                if (pathSegment->node == NULL) {
                    server->allocator.free(pathSegment->segment);
                    server->allocator.free(pathSegment);
                    return NABTO_COAP_ERROR_OUT_OF_MEMORY;
                }
                nabto_coap_router_insert_path_segment(parent->pathSegmentsSentinel.prev, pathSegment);
            }
            child = pathSegment->node;
        }

        return nabto_coap_server_add_resource_into_tree(server, child, method, path+1, handler, userData, userRes);
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
    struct nabto_coap_server* server = request->requests->server;
    request->response.payload = server->allocator.calloc(1, dataSize + 1);
    if (request->response.payload == NULL) {
        return NABTO_COAP_ERROR_OUT_OF_MEMORY;
    }
    memcpy(request->response.payload, data, dataSize);
    request->response.payloadLength = dataSize;
    if (dataSize > (16u << request->response.block2Size)) {
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
        request->requests->notifyEvent(request->requests->userData);
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

void nabto_coap_server_observer_remove_from_list(struct nabto_coap_server_observer* observer)
{
    struct nabto_coap_server_observer* prev = observer->prev;
    struct nabto_coap_server_observer* next = observer->next;
    prev->next = next;
    next->prev = prev;
    observer->next = observer;
    observer->prev = observer;
}

void nabto_coap_server_observer_free(struct nabto_coap_server_observer* observer)
{
    struct nabto_coap_server* server = observer->requests->server;
    nabto_coap_server_observer_remove_from_list(observer);
    if (observer->payload) {
        server->allocator.free(observer->payload);
    }
    server->allocator.free(observer);
}

bool nabto_coap_server_request_is_observe(struct nabto_coap_server_request* request)
{
    return request->isObserveRegister;
}

nabto_coap_error nabto_coap_server_request_accept_observe(struct nabto_coap_server_request* request)
{
    if (!request->isObserveRegister) {
        return NABTO_COAP_ERROR_INVALID_PARAMETER;
    }
    struct nabto_coap_server_requests* requests = request->requests;
    struct nabto_coap_server* server = requests->server;

    // Check if we already have an observer for this token+connection; if so, replace it
    struct nabto_coap_server_observer* existing = requests->observersSentinel->next;
    while (existing != requests->observersSentinel) {
        struct nabto_coap_server_observer* current = existing;
        existing = existing->next;
        if (current->connection == request->connection &&
            nabto_coap_token_equal(&current->token, &request->token))
        {
            nabto_coap_server_observer_free(current);
            break;
        }
    }

    struct nabto_coap_server_observer* observer = server->allocator.calloc(1, sizeof(struct nabto_coap_server_observer));
    if (observer == NULL) {
        return NABTO_COAP_ERROR_OUT_OF_MEMORY;
    }
    observer->requests = requests;
    observer->resource = request->resource;
    observer->connection = request->connection;
    observer->token = request->token;
    observer->sequenceNumber = 0;
    observer->notificationType = NABTO_COAP_TYPE_CON;

    // Insert into list
    struct nabto_coap_server_observer* sentinel = requests->observersSentinel;
    struct nabto_coap_server_observer* after = sentinel->next;
    sentinel->next = observer;
    observer->next = after;
    after->prev = observer;
    observer->prev = sentinel;

    request->observer = observer;
    return NABTO_COAP_ERROR_OK;
}

nabto_coap_error nabto_coap_server_resource_notify(
    struct nabto_coap_server_requests* requests,
    struct nabto_coap_server_resource* resource,
    nabto_coap_code code,
    uint16_t contentFormat,
    const void* payload,
    size_t payloadLength)
{
    struct nabto_coap_server* server = requests->server;
    struct nabto_coap_server_observer* obs = requests->observersSentinel->next;
    while (obs != requests->observersSentinel) {
        if (obs->resource == resource) {
            obs->sequenceNumber++;
            obs->code = code;
            obs->hasContentFormat = true;
            obs->contentFormat = contentFormat;

            // Free old payload if any
            if (obs->payload) {
                server->allocator.free(obs->payload);
                obs->payload = NULL;
                obs->payloadLength = 0;
            }

            if (payloadLength > 0 && payload != NULL) {
                obs->payload = server->allocator.calloc(1, payloadLength + 1);
                if (obs->payload == NULL) {
                    return NABTO_COAP_ERROR_OUT_OF_MEMORY;
                }
                memcpy(obs->payload, payload, payloadLength);
                obs->payloadLength = payloadLength;
            }

            obs->messageId = nabto_coap_server_next_message_id(requests);
            obs->retransmissions = 0;
            obs->sendNow = true;
        }
        obs = obs->next;
    }
    requests->notifyEvent(requests->userData);
    return NABTO_COAP_ERROR_OK;
}

void nabto_coap_server_remove_observer(struct nabto_coap_server_observer* observer)
{
    nabto_coap_server_observer_free(observer);
}

void nabto_coap_server_remove_connection(struct nabto_coap_server_requests* requests, void* connection)
{
    // Remove observers for this connection
    struct nabto_coap_server_observer* obsIt = requests->observersSentinel->next;
    while (obsIt != requests->observersSentinel) {
        struct nabto_coap_server_observer* currentObs = obsIt;
        obsIt = obsIt->next;
        if (currentObs->connection == connection) {
            nabto_coap_server_observer_free(currentObs);
        }
    }

    // Loop over all requests terminate all request where the
    // connection is dead. Or mark them as connection dead so they
    // will die when the user code resolves the request.

    struct nabto_coap_server_request* it = requests->requestsSentinel->next;
    while(it != requests->requestsSentinel) {
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
    if (requests->ackConnection == connection) {
        requests->ackConnection = NULL;
    }

    if (requests->errorConnection == connection) {
        requests->errorConnection = NULL;
    }
}

uint16_t nabto_coap_server_next_message_id(struct nabto_coap_server_requests* requests)
{
    requests->messageId++;
    return requests->messageId;
}

struct nabto_coap_router_node* nabto_coap_router_node_new(struct nabto_coap_server* server)
{
    struct nabto_coap_router_node* node = server->allocator.calloc(1, sizeof(struct nabto_coap_router_node));
    if (node == NULL) {
        return NULL;
    }
    node->pathSegmentsSentinel.next = &node->pathSegmentsSentinel;
    node->pathSegmentsSentinel.prev = &node->pathSegmentsSentinel;
    return node;
}

void nabto_coap_router_node_free(struct nabto_coap_server* server, struct nabto_coap_router_node* node)
{
    if (node->parameter.name != NULL) {
        nabto_coap_router_node_free(server, node->parameter.node);
        server->allocator.free(node->parameter.name);
    }

    struct nabto_coap_router_path_segment* iterator = node->pathSegmentsSentinel.next;
    while (iterator != &node->pathSegmentsSentinel) {
        struct nabto_coap_router_path_segment* current = iterator;
        iterator = iterator->next;
        nabto_coap_router_node_free(server, current->node);
        server->allocator.free(current->segment);
        server->allocator.free(current);
    }

    server->allocator.free(node);
}

struct nabto_coap_router_path_segment* nabto_coap_router_path_segment_new(struct nabto_coap_server* server)
{
    struct nabto_coap_router_path_segment* segment = server->allocator.calloc(1, sizeof(struct nabto_coap_router_path_segment));
    return segment;
}


void nabto_coap_router_path_segment_free(struct nabto_coap_server* server, struct nabto_coap_router_path_segment* segment)
{
    server->allocator.free(segment);
}
