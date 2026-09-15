#include "nabto_coap_server_impl.h"

#include <stdlib.h>

/**
 * Functions for handling incoming coap data
 */

static const char* unsupportedCriticalOption = "Unsupported critical option";
static const char* outOfResources = "Out of resources";
static const char* wrongPayloadLength = "Wrong payload length";
static const char* badBlockOption = "Bad block option";
static const char* requestTooLarge = "Request entity too large";


static struct nabto_coap_server_request* nabto_coap_server_handle_new_request(struct nabto_coap_server_requests* requests, struct nabto_coap_incoming_message* message, void* connection);
static void nabto_coap_server_handle_ack(struct nabto_coap_server_requests* requests, struct nabto_coap_server_request* request, struct nabto_coap_incoming_message* message);
static void nabto_coap_server_handle_rst(struct nabto_coap_server_requests* requests, uint16_t messageId, void* connection);
static struct nabto_coap_server_request* nabto_coap_server_request_new(struct nabto_coap_server_requests* requests);
static struct nabto_coap_server_resource* nabto_coap_server_find_resource(struct nabto_coap_server* server, struct nabto_coap_incoming_message* message, struct nabto_coap_server_request_parameter* parameters);

static bool nabto_coap_server_validate_critical_options(struct nabto_coap_incoming_message* message);

static void nabto_coap_server_handle_data_for_response(struct nabto_coap_server_requests* requests, struct nabto_coap_server_request* request, struct nabto_coap_incoming_message* message);
static void nabto_coap_server_handle_data_for_request(struct nabto_coap_server_requests* requests, struct nabto_coap_server_request* request, struct nabto_coap_incoming_message* message);

/**
 * Make an error response to some condition. Dropped if an error is
 * already waiting to be sent.
 * @param errorDescription  keep this pointer alive forever.
 */
static void nabto_coap_server_make_error_response(struct nabto_coap_server_requests* requests, void* connection, struct nabto_coap_incoming_message* message, nabto_coap_code code, const char* errorDescription);

/**
 * Queue an empty ACK for a CON message. Dropped if an ACK is already
 * waiting to be sent.
 */
static void nabto_coap_server_queue_ack(struct nabto_coap_server_requests* requests, void* connection, uint16_t messageId);

/**
 * Queue a RST rejecting a message. Dropped if a RST is already
 * waiting to be sent.
 */
static void nabto_coap_server_queue_rst(struct nabto_coap_server_requests* requests, void* connection, uint16_t messageId);

/**
 * A CON notification with the observer's current message id has been
 * sent and not yet acknowledged: it is waiting for the ACK, or timed
 * out and is queued for retransmission. Only such a notification is
 * matched by an ACK or RST (RFC 7252 section 4.2); before the first
 * notification the message id is 0, and a queued notification that was
 * never sent has an id the client cannot have seen.
 */
static bool nabto_coap_server_observer_notification_in_flight(struct nabto_coap_server_observer* observer)
{
    return observer->waitingForAck || (observer->sendNow && observer->retransmissions > 0);
}


void nabto_coap_server_handle_packet(struct nabto_coap_server_requests* requests, void* connection, const uint8_t* packet, size_t packetSize)
{
    struct nabto_coap_incoming_message msg;
    if (!nabto_coap_parse_message(packet, packetSize, &msg)) {
        return;
    }


    // RFC 7252 section 5.4.1: a request with an unrecognized critical
    // option is answered with 4.02 Bad Option if it is CON, and
    // rejected with a RST (section 4.3) if it is NON.
    if (msg.type == NABTO_COAP_TYPE_CON ||
        msg.type == NABTO_COAP_TYPE_NON)
    {
        if (!nabto_coap_server_validate_critical_options(&msg)) {
            if (msg.type == NABTO_COAP_TYPE_CON) {
                nabto_coap_server_make_error_response(requests, connection, &msg, NABTO_COAP_CODE_BAD_OPTION, unsupportedCriticalOption);
            } else {
                nabto_coap_server_queue_rst(requests, connection, msg.messageId);
            }
            return;
        }
    }

    struct nabto_coap_server_request* request = nabto_coap_server_find_request(requests, &msg.token, connection);

    if (msg.type == NABTO_COAP_TYPE_CON || msg.type == NABTO_COAP_TYPE_NON) {

        if (request && request->messageId == msg.messageId) {
            // Duplicate of a request we have already processed. RFC 7252
            // section 4.5: acknowledge it with the same ACK as the original,
            // but process the request only once.
            if (msg.type == NABTO_COAP_TYPE_CON) {
                if (request->state == NABTO_COAP_SERVER_REQUEST_STATE_REQUEST &&
                    NABTO_COAP_BLOCK_MORE(request->block1Ack))
                {
                    // An intermediate Block1 chunk was originally answered
                    // with a piggybacked 2.31 Continue, resend that.
                    request->hasBlock1Ack = true;
                } else {
                    nabto_coap_server_queue_ack(requests, connection, msg.messageId);
                }
            }
            return;
        }

        if (!request) {
            request = nabto_coap_server_handle_new_request(requests, &msg, connection);
            if (!request) {
                // error is handled inside the function.
                return;
            }
        }

        if (request->state == NABTO_COAP_SERVER_REQUEST_STATE_REQUEST) {
            nabto_coap_server_handle_data_for_request(requests, request, &msg);
            return;
        } else if (request->state == NABTO_COAP_SERVER_REQUEST_STATE_RESPONSE) {
            nabto_coap_server_handle_data_for_response(requests, request, &msg);
            return;
        } else {
            // we should not handle new data in this state.
            return;
        }
    } else if (msg.type == NABTO_COAP_TYPE_ACK) {
        // acks does not contain tokens, so find the appropriate response using messageId and connection
        struct nabto_coap_server_response* response = nabto_coap_server_find_response(requests, msg.messageId, connection);
        if (response) {
            nabto_coap_server_handle_ack(requests, response->request, &msg);
            return;
        }
        // Check if ACK is for an observer notification in flight
        struct nabto_coap_server_observer* obs = requests->observersSentinel->next;
        while (obs != requests->observersSentinel) {
            if (nabto_coap_server_observer_notification_in_flight(obs) && obs->connection == connection && obs->messageId == msg.messageId) {
                // Notification was acknowledged, clear in-flight state.
                struct nabto_coap_server* server = requests->server;
                if (obs->payload) {
                    server->allocator.free(obs->payload);
                    obs->payload = NULL;
                    obs->payloadLength = 0;
                }
                obs->sendNow = false;
                obs->waitingForAck = false;

                // If a newer notification was coalesced while the CON was
                // in flight, promote it now and wake the event loop so
                // it is sent as a fresh CON.
                if (obs->pendingValid) {
                    nabto_coap_server_observer_promote_pending(requests, obs);
                    requests->notifyEvent(requests->userData);
                }
                return;
            }
            obs = obs->next;
        }
    } else if (msg.type == NABTO_COAP_TYPE_RST) {
        nabto_coap_server_handle_rst(requests, msg.messageId, connection);
    }
}

bool nabto_coap_server_validate_critical_options(struct nabto_coap_incoming_message* message)
{
    struct nabto_coap_option_iterator iteratorData;
    struct nabto_coap_option_iterator* iterator = &iteratorData;
    nabto_coap_option_iterator_init(iterator, message->options, message->options + message->optionsLength);
    iterator = nabto_coap_get_next_option(iterator);

    while(iterator != NULL) {
        if (iterator->option % 2 == 1) {
            switch (iterator->option) {
                // handled options
                case NABTO_COAP_OPTION_URI_PATH:
                case NABTO_COAP_OPTION_BLOCK1:
                case NABTO_COAP_OPTION_BLOCK2:
                    // accepted but ignored
                case NABTO_COAP_OPTION_URI_HOST:
                case NABTO_COAP_OPTION_URI_PORT:
                    break;
                default: return false;
            }
        }
        iterator = nabto_coap_get_next_option(iterator);
    }
    return true;
}

void nabto_coap_server_make_error_response(struct nabto_coap_server_requests* requests, void* connection, struct nabto_coap_incoming_message* message, nabto_coap_code code, const char* errorDescription)
{
    if (requests->errorConnection != NULL) {
        // An error is already waiting to be sent, keep that one. A CON
        // client retransmits its request and gets its error then.
        return;
    }
    requests->errorConnection = connection;
    requests->errorCode = code;
    requests->errorToken = message->token;
    if (message->type == NABTO_COAP_TYPE_CON) {
        // piggybacked response, RFC 7252 section 5.2.1
        requests->errorType = NABTO_COAP_TYPE_ACK;
        requests->errorMessageId = message->messageId;
    } else {
        // RFC 7252 section 5.2.3
        requests->errorType = NABTO_COAP_TYPE_NON;
        requests->errorMessageId = nabto_coap_server_next_message_id(requests);
    }
    if (errorDescription != NULL) {
        requests->errorPayload = errorDescription;
        requests->errorPayloadLength = strlen(errorDescription);
    } else {
        requests->errorPayload = NULL;
        requests->errorPayloadLength = 0;
    }
}

void nabto_coap_server_queue_ack(struct nabto_coap_server_requests* requests, void* connection, uint16_t messageId)
{
    if (requests->ackConnection != NULL) {
        // An ACK is already waiting to be sent, keep that one. The
        // client retransmits and is acked then.
        return;
    }
    requests->ackConnection = connection;
    requests->ackMessageId = messageId;
}

void nabto_coap_server_queue_rst(struct nabto_coap_server_requests* requests, void* connection, uint16_t messageId)
{
    if (requests->rstConnection != NULL) {
        // A RST is already waiting to be sent, keep that one.
        return;
    }
    requests->rstConnection = connection;
    requests->rstMessageId = messageId;
}

void nabto_coap_server_handle_data_for_request(struct nabto_coap_server_requests* requests, struct nabto_coap_server_request* request, struct nabto_coap_incoming_message* message)
{
    struct nabto_coap_server* server = requests->server;
    bool block1Done = true;

    // RFC 7959 section 2.2: SZX 7 is reserved and MUST lead to 4.00 Bad
    // Request, also in a Block2 the client sends to suggest a response
    // block size, which is otherwise ignored.
    if ((message->hasBlock1 && NABTO_COAP_BLOCK_SIZE(message->block1) == 7) ||
        (message->hasBlock2 && NABTO_COAP_BLOCK_SIZE(message->block2) == 7))
    {
        nabto_coap_server_make_error_response(requests, request->connection, message, NABTO_COAP_CODE_BAD_REQUEST, badBlockOption);
        // User will never see this request, so we free for him
        request->isFreed = true;
        request->state = NABTO_COAP_SERVER_REQUEST_STATE_DONE;
        nabto_coap_server_free_request(request);
        return;
    }

    // RFC 7959 section 2.9.3: 4.13 Request Entity Too Large "can be
    // returned at any time by a server that does not currently have the
    // resources to store blocks for a block-wise request payload
    // transfer". The error is the final response for this token.
    if (request->payloadLength + message->payloadLength > requests->maxRequestPayload) {
        nabto_coap_server_make_error_response(requests, request->connection, message, NABTO_COAP_CODE_REQUEST_ENTITY_TOO_LARGE, requestTooLarge);
        // User will never see this request, so we free for him
        request->isFreed = true;
        request->state = NABTO_COAP_SERVER_REQUEST_STATE_DONE;
        nabto_coap_server_free_request(request);
        return;
    }

    if (message->hasBlock1) {
        uint32_t offset = NABTO_COAP_BLOCK_OFFSET(message->block1);
        if (request->payloadLength != offset) {
            nabto_coap_server_make_error_response(requests, request->connection, message, NABTO_COAP_CODE_REQUEST_ENTITY_INCOMPLETE, NULL);
            request->isFreed = true;
            request->state = NABTO_COAP_SERVER_REQUEST_STATE_DONE;
            nabto_coap_server_free_request(request);
            return;
        }

        uint32_t more = NABTO_COAP_BLOCK_MORE(message->block1);
        if (more) {
            if (message->payloadLength != NABTO_COAP_BLOCK_SIZE_ABSOLUTE(message->block1)) {
                nabto_coap_server_make_error_response(requests, request->connection, message, NABTO_COAP_CODE_BAD_REQUEST, wrongPayloadLength);
                // User will never see this request, so we free for him
                request->isFreed = true;
                request->state = NABTO_COAP_SERVER_REQUEST_STATE_DONE;
                nabto_coap_server_free_request(request);
                return;
            }
        }
        void* newPayload = server->allocator.calloc(1, request->payloadLength + message->payloadLength + 1);
        if (!newPayload) {
            nabto_coap_server_make_error_response(requests, request->connection, message, NABTO_COAP_CODE_SERVICE_UNAVAILABLE, outOfResources);
            // User will never see this request, so we free for him
            request->isFreed = true;
            request->state = NABTO_COAP_SERVER_REQUEST_STATE_DONE;
            nabto_coap_server_free_request(request);
            return;
        }

        if (request->payloadLength > 0) {
            memcpy(newPayload, request->payload, request->payloadLength);
            server->allocator.free(request->payload);
        }
        memcpy((uint8_t*)newPayload+request->payloadLength, message->payload, message->payloadLength);
        request->payload = newPayload;
        request->payloadLength = request->payloadLength + message->payloadLength;

        request->messageId = message->messageId;
        // Send continue as the ack code so setting the more bit to 1
        // even in the last chunk.  The more bit means that the
        // response code will come in another response from the
        // server.
        request->block1Ack = (NABTO_COAP_BLOCK_NUM(message->block1) << 4) + (NABTO_COAP_BLOCK_SIZE(message->block1));
        if (more) {
            request->block1Ack += (1 << 3);
            request->hasBlock1Ack = true;
            block1Done = false;
        }
    } else {
        if (message->payload && message->payloadLength) {
            if (request->payload != NULL) {
                server->allocator.free(request->payload);
            }
            request->payload = server->allocator.calloc(1, message->payloadLength+1);
            if (request->payload == NULL) {
                request->isFreed = true;
                request->state = NABTO_COAP_SERVER_REQUEST_STATE_DONE;
                nabto_coap_server_free_request(request);
                return;
            }
            memcpy(request->payload, message->payload, message->payloadLength);
            request->payloadLength = message->payloadLength;
        }
    }

    if (message->hasContentFormat) {
        request->hasContentFormat = true;
        request->contentFormat = message->contentFormat;
    }

    if (message->type == NABTO_COAP_TYPE_CON && !request->hasBlock1Ack) {
        nabto_coap_server_queue_ack(requests, request->connection, message->messageId);
    }

    if (block1Done) {
        struct nabto_coap_server_resource* resource = request->resource;
        request->state = NABTO_COAP_SERVER_REQUEST_STATE_USER;
        resource->handler(request, resource->handlerUserData);
    }
}

void nabto_coap_server_handle_data_for_response(struct nabto_coap_server_requests* requests, struct nabto_coap_server_request* request, struct nabto_coap_incoming_message* message)
{
    if (message->hasBlock2) {
        struct nabto_coap_server_response* response = &request->response;

        // RFC 7959 section 2.2: SZX 7 is reserved and MUST lead to 4.00
        // Bad Request. A block past the end of the payload is a bad
        // request as well; the send path would otherwise read past the
        // payload buffer. The error is the final response for this token,
        // so the exchange is over.
        uint32_t offset = 0;
        bool badBlock = (NABTO_COAP_BLOCK_SIZE(message->block2) == 7);
        if (!badBlock) {
            offset = NABTO_COAP_BLOCK_OFFSET(message->block2);
            badBlock = (offset != 0 && offset >= response->payloadLength);
        }
        if (badBlock) {
            nabto_coap_server_make_error_response(requests, request->connection, message, NABTO_COAP_CODE_BAD_REQUEST, badBlockOption);
            request->state = NABTO_COAP_SERVER_REQUEST_STATE_DONE;
            nabto_coap_server_free_request(request);
            return;
        }

        response->block2Current = NABTO_COAP_BLOCK_NUM(message->block2);
        response->block2Size = NABTO_COAP_BLOCK_SIZE(message->block2);
        response->messageId = nabto_coap_server_next_message_id(requests);
        response->sendNow = true;
        response->retransmissions = 0;
    }
}


struct nabto_coap_server_request* nabto_coap_server_handle_new_request(struct nabto_coap_server_requests* requests, struct nabto_coap_incoming_message* message, void* connection)
{
    struct nabto_coap_server* server = requests->server;
    {
        struct nabto_coap_server_resource* resource = nabto_coap_server_find_resource(server, message, NULL);

        if (!resource) {
            nabto_coap_server_make_error_response(requests, connection, message, NABTO_COAP_CODE_NOT_FOUND, NULL);
            return NULL;
        }
    }

    if(requests->activeRequests >= requests->maxRequests) {
        nabto_coap_server_make_error_response(requests, connection, message, NABTO_COAP_CODE_SERVICE_UNAVAILABLE, outOfResources);
        return NULL;
    }
    struct nabto_coap_server_request* request = nabto_coap_server_request_new(requests);
    if (!request) {
        nabto_coap_server_make_error_response(requests, connection, message, NABTO_COAP_CODE_SERVICE_UNAVAILABLE, outOfResources);
        return NULL;
    }
    requests->activeRequests++;

    struct nabto_coap_server_resource* resource = nabto_coap_server_find_resource(server, message, &request->parameterSentinel);
    if (resource == NULL) {
        // we already know the resource exist, so NULL can only mean the parameter value could not be allocated.
        nabto_coap_server_make_error_response(requests, connection, message, NABTO_COAP_CODE_SERVICE_UNAVAILABLE, outOfResources);
        // User will never see this request, so we free for him
        request->isFreed = true;
        request->state = NABTO_COAP_SERVER_REQUEST_STATE_DONE;
        nabto_coap_server_free_request(request);
        return NULL;
    }

    request->connection = connection;

    request->type = message->type;
    request->method = message->code;
    request->token = message->token;
    request->messageId = message->messageId;
    request->resource = resource;

    // Detect observe registration (GET + Observe=0)
    if (message->hasObserve && message->observe == 0 && message->code == NABTO_COAP_CODE_GET) {
        request->isObserveRegister = true;
    }

    // Handle observe deregistration (GET + Observe=1): remove matching observer
    if (message->hasObserve && message->observe == 1 && message->code == NABTO_COAP_CODE_GET) {
        struct nabto_coap_server_observer* obs = requests->observersSentinel->next;
        while (obs != requests->observersSentinel) {
            struct nabto_coap_server_observer* current = obs;
            obs = obs->next;
            if (current->connection == connection &&
                nabto_coap_token_equal(&current->token, &message->token))
            {
                nabto_coap_server_observer_free(current);
                break;
            }
        }
    }

    nabto_coap_server_insert_request_into_list(requests->requestsSentinel, request);

    return request;
}

void nabto_coap_server_handle_ack(struct nabto_coap_server_requests* requests, struct nabto_coap_server_request* request, struct nabto_coap_incoming_message* message)
{
    (void)requests;
    struct nabto_coap_server_response* response = &request->response;

    if (response->messageId != message->messageId) {
        return;
    }

    size_t blockSize = (16 << response->block2Size);
    if (response->payloadLength <= blockSize) {
        request->state = NABTO_COAP_SERVER_REQUEST_STATE_DONE;
        nabto_coap_server_free_request(request);
        return;
    }

    // handle block2 ack
    response->block2Current += 1;

    // The last block ends exactly at payloadLength when the payload is a
    // multiple of the block size, so >= is the completion test.
    if (response->block2Current * blockSize >= response->payloadLength) {
        request->state = NABTO_COAP_SERVER_REQUEST_STATE_DONE;
        nabto_coap_server_free_request(request);
        return;
    }

    // else wait for the client to ask for the next block.
}


void nabto_coap_server_handle_rst(struct nabto_coap_server_requests* requests, uint16_t messageId, void* connection)
{
    struct nabto_coap_server_request* request = requests->requestsSentinel->next;
    while(request != requests->requestsSentinel) {
        if (request->connection == connection) {
            if (request->response.messageId == messageId) {
                request->state = NABTO_COAP_SERVER_REQUEST_STATE_DONE;
                nabto_coap_server_free_request(request);
                return;
            }
        }
        request = request->next;
    }

    // Check if RST matches an observer notification in flight
    struct nabto_coap_server_observer* obs = requests->observersSentinel->next;
    while (obs != requests->observersSentinel) {
        struct nabto_coap_server_observer* current = obs;
        obs = obs->next;
        if (nabto_coap_server_observer_notification_in_flight(current) && current->connection == connection && current->messageId == messageId) {
            nabto_coap_server_observer_free(current);
            return;
        }
    }
}

struct nabto_coap_server_request* nabto_coap_server_find_request(struct nabto_coap_server_requests* requests, nabto_coap_token* token, void* connection)
{
    struct nabto_coap_server_request* request = requests->requestsSentinel->next;
    while(request != requests->requestsSentinel) {
        if (nabto_coap_token_equal(&request->token, token) &&
            connection == request->connection)
        {
            return request;
        }
        request = request->next;
    }
    return NULL;
}

struct nabto_coap_server_response* nabto_coap_server_find_response(struct nabto_coap_server_requests* requests, uint16_t messageId, void* connection)
{
    struct nabto_coap_server_request* request = requests->requestsSentinel->next;
    while(request != requests->requestsSentinel) {
        if(request->connection == connection &&
           request->response.messageId == messageId)
        {
            return &request->response;
        }
        request = request->next;
    }
    return NULL;
}

struct nabto_coap_server_resource* nabto_coap_server_find_resource(struct nabto_coap_server* server, struct nabto_coap_incoming_message* msg, struct nabto_coap_server_request_parameter* parameters)
{
    struct nabto_coap_option_iterator itData;
    struct nabto_coap_option_iterator* iterator = &itData;
    struct nabto_coap_router_node* currentNode = server->root;
    nabto_coap_option_iterator_init(iterator, msg->options, msg->options+msg->optionsLength);
    iterator = nabto_coap_get_option(NABTO_COAP_OPTION_URI_PATH, iterator);
    while (iterator != NULL) {

        size_t optionLength = iterator->optionDataEnd - iterator->optionDataBegin;

        struct nabto_coap_router_path_segment* segment = nabto_coap_server_find_path_segment(currentNode, (const char*)iterator->optionDataBegin, optionLength);
        if (segment) {
            currentNode = segment->node;
        } else {
            // test if currentNode has a parameter
            if (currentNode->parameter.name != NULL) {
                if (parameters != NULL) {
                    struct nabto_coap_server_request_parameter* parameter = nabto_coap_server_request_parameter_new(server);
                    if (parameter == NULL) {
                        // calloc failed, return NULL to signal this. This only happens if we already found the resource in a previous call with parameters == NULL, so it is possible to return meaningfull error.
                        return NULL;
                    }
                    parameter->parameter = &currentNode->parameter;
                    parameter->value = server->allocator.calloc(1, optionLength + 1);
                    if (parameter->value == NULL) {
                        // calloc failed, return NULL to signal this. This only happens if we already found the resource in a previous call with parameters == NULL, so it is possible to return meaningfull error.
                        server->allocator.free(parameter);
                        return NULL;
                    }
                    memcpy(parameter->value, iterator->optionDataBegin, optionLength);

                    struct nabto_coap_server_request_parameter* before = parameters->prev;
                    struct nabto_coap_server_request_parameter* after = before->next;
                    before->next = parameter;
                    parameter->next = after;
                    after->prev = parameter;
                    parameter->prev = before;
                }
                currentNode = currentNode->parameter.node;
            } else {
                // no name match nor a parameter at this level, conclude the resource does not exists.
                return NULL;
            }
        }

        iterator = nabto_coap_get_option(NABTO_COAP_OPTION_URI_PATH, iterator);
    }

    if (msg->code == NABTO_COAP_CODE_GET && currentNode->getHandler.handler) {
        return &currentNode->getHandler;
    } else if (msg->code == NABTO_COAP_CODE_POST && currentNode->postHandler.handler) {
        return &currentNode->postHandler;
    } else if (msg->code == NABTO_COAP_CODE_PUT && currentNode->putHandler.handler) {
        return &currentNode->putHandler;
    } else if (msg->code == NABTO_COAP_CODE_DELETE && currentNode->deleteHandler.handler) {
        return &currentNode->deleteHandler;
    } else {
        return NULL;
    }
}

struct nabto_coap_server_request* nabto_coap_server_request_new(struct nabto_coap_server_requests* requests)
{
    struct nabto_coap_server* server = requests->server;
    struct nabto_coap_server_request* request = server->allocator.calloc(1, sizeof(struct nabto_coap_server_request));
    if (request == NULL) {
        return NULL;
    }

    request->parameterSentinel.next = &request->parameterSentinel;
    request->parameterSentinel.prev = &request->parameterSentinel;
    request->isFreed = false;
    request->requests = requests;

    request->response.request = request;
    request->response.messageId = nabto_coap_server_next_message_id(requests);
    request->response.block2Size = 5; // 512 byte blocks
    return request;
}


struct nabto_coap_server_request_parameter* nabto_coap_server_request_parameter_new(struct nabto_coap_server* server)
{
    return (struct nabto_coap_server_request_parameter*)server->allocator.calloc(1, sizeof(struct nabto_coap_server_request_parameter));
}
