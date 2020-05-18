#include "nabto_coap_server_impl.h"

#include <stdlib.h>

/**
 * Functions for handling incoming coap data
 */

static const char* unsupportedCriticalOption = "Unsupported critical option";
static const char* outOfResources = "Out of resources";
static const char* wrongPayloadLength = "Wrong payload length";


static struct nabto_coap_server_request* nabto_coap_server_handle_new_request(struct nabto_coap_server* server, struct nabto_coap_incoming_message* message, void* connection);
static void nabto_coap_server_handle_ack(struct nabto_coap_server* server, struct nabto_coap_server_request* request, struct nabto_coap_incoming_message* message);
static void nabto_coap_server_handle_rst(struct nabto_coap_server* server, uint16_t messageId, void* connection);
static struct nabto_coap_server_request* nabto_coap_server_request_new(struct nabto_coap_server* server);
static struct nabto_coap_server_resource* nabto_coap_server_find_resource(struct nabto_coap_server* server, struct nabto_coap_incoming_message* message, struct nabto_coap_server_request_parameter* parameters);

static bool nabto_coap_server_validate_critical_options(struct nabto_coap_incoming_message* message);

static void nabto_coap_server_handle_data_for_response(struct nabto_coap_server* server, struct nabto_coap_server_request* request, struct nabto_coap_incoming_message* message);
static void nabto_coap_server_handle_data_for_request(struct nabto_coap_server* server, struct nabto_coap_server_request* request, struct nabto_coap_incoming_message* message);

/**
 * Make an error response to some condition
 * @param errorDescription  keep this pointer alive forever.
 */
static void nabto_coap_server_make_error_response(struct nabto_coap_server* server, void* connection, struct nabto_coap_incoming_message* message, nabto_coap_code code, const char* errorDescription);


void nabto_coap_server_handle_packet(struct nabto_coap_server* server, void* connection, const uint8_t* packet, size_t packetSize)
{
    struct nabto_coap_incoming_message msg;
    if (!nabto_coap_parse_message(packet, packetSize, &msg)) {
        return;
    }


    // validate options deny request if we do not know a critical option.
    if (msg.type == NABTO_COAP_TYPE_CON ||
        msg.type == NABTO_COAP_TYPE_NON ||
        msg.type == NABTO_COAP_TYPE_ACK)
    {
        if (!nabto_coap_server_validate_critical_options(&msg)) {
            nabto_coap_server_make_error_response(server, connection, &msg, NABTO_COAP_CODE_BAD_REQUEST, unsupportedCriticalOption);
            return;
        }
    }

    struct nabto_coap_server_request* request = nabto_coap_server_find_request(server, &msg.token, connection);

    if (msg.type == NABTO_COAP_TYPE_CON || msg.type == NABTO_COAP_TYPE_NON) {

        if (request && request->messageId == msg.messageId) {
            // retransmission of a request.
            if (msg.type == NABTO_COAP_TYPE_CON) {
                server->ackConnection = connection;
                server->ackMessageId = msg.messageId;
            }
            return;
        }

        if (!request) {
            request = nabto_coap_server_handle_new_request(server, &msg, connection);
            if (!request) {
                // error is handled inside the function.
                return;
            }
        }

        if (request->state == NABTO_COAP_SERVER_REQUEST_STATE_REQUEST) {
            nabto_coap_server_handle_data_for_request(server, request, &msg);
            return;
        } else if (request->state == NABTO_COAP_SERVER_REQUEST_STATE_RESPONSE) {
            nabto_coap_server_handle_data_for_response(server, request, &msg);
            return;
        } else {
            // we should not handle new data in this state.
            return;
        }
    } else if (msg.type == NABTO_COAP_TYPE_ACK) {
        // acks does not contain tokens, so find the appropriate response using messageId and connection
        struct nabto_coap_server_response* response = nabto_coap_server_find_response(server, msg.messageId, connection);
        if (response) {
            nabto_coap_server_handle_ack(server, response->request, &msg);
            return;
        }
    } else if (msg.type == NABTO_COAP_TYPE_RST) {
        nabto_coap_server_handle_rst(server, msg.messageId, connection);
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

void nabto_coap_server_make_error_response(struct nabto_coap_server* server, void* connection, struct nabto_coap_incoming_message* message, nabto_coap_code code, const char* errorDescription)
{
    server->errorConnection = connection;
    server->errorCode = code;
    server->errorToken = message->token;
    server->errorMessageId = message->messageId;
    if (errorDescription != NULL) {
        server->errorPayload = errorDescription;
        server->errorPayloadLength = strlen(errorDescription);
    }
    return;

}

void nabto_coap_server_handle_data_for_request(struct nabto_coap_server* server, struct nabto_coap_server_request* request, struct nabto_coap_incoming_message* message)
{

    bool block1Done = true;

    if (message->hasBlock1) {
        uint32_t offset = NABTO_COAP_BLOCK_OFFSET(message->block1);
        if (request->payloadLength != offset) {
            nabto_coap_server_make_error_response(server, request->connection, message, NABTO_COAP_CODE_REQUEST_ENTITY_INCOMPLETE, NULL);
            request->isFreed = true;
            request->state = NABTO_COAP_SERVER_REQUEST_STATE_DONE;
            nabto_coap_server_free_request(request);
            return;
        }

        uint32_t more = NABTO_COAP_BLOCK_MORE(message->block1);
        if (more) {
            if (message->payloadLength != NABTO_COAP_BLOCK_SIZE_ABSOLUTE(message->block1)) {
                nabto_coap_server_make_error_response(server, request->connection, message, NABTO_COAP_CODE_BAD_REQUEST, wrongPayloadLength);
                // User will never see this request, so we free for him
                request->isFreed = true;
                request->state = NABTO_COAP_SERVER_REQUEST_STATE_DONE;
                nabto_coap_server_free_request(request);
                return;
            }
        }
        void* newPayload = calloc(1, request->payloadLength + message->payloadLength + 1);
        if (!newPayload) {
            nabto_coap_server_make_error_response(server, request->connection, message, NABTO_COAP_CODE_SERVICE_UNAVAILABLE, outOfResources);
            // User will never see this request, so we free for him
            request->isFreed = true;
            request->state = NABTO_COAP_SERVER_REQUEST_STATE_DONE;
            nabto_coap_server_free_request(request);
            return;
        }

        if (request->payloadLength > 0) {
            memcpy(newPayload, request->payload, request->payloadLength);
            free(request->payload);
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
                free(request->payload);
            }
            request->payload = calloc(1, message->payloadLength+1);
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


    if (block1Done) {
        struct nabto_coap_server_resource* resource = request->resource;
        request->state = NABTO_COAP_SERVER_REQUEST_STATE_USER;
        resource->handler(request, resource->handlerUserData);
    }

    if (message->type == NABTO_COAP_TYPE_CON && !request->hasBlock1Ack) {
        server->ackConnection = request->connection;
        server->ackMessageId = message->messageId;
    }
}

void nabto_coap_server_handle_data_for_response(struct nabto_coap_server* server, struct nabto_coap_server_request* request, struct nabto_coap_incoming_message* message)
{
    if (message->hasBlock2) {
        struct nabto_coap_server_response* response = &request->response;
        response->block2Current = NABTO_COAP_BLOCK_NUM(message->block2);
        response->block2Size = NABTO_COAP_BLOCK_SIZE(message->block2);
        response->messageId = nabto_coap_server_next_message_id(server);
        response->sendNow = true;
    }
}


struct nabto_coap_server_request* nabto_coap_server_handle_new_request(struct nabto_coap_server* server, struct nabto_coap_incoming_message* message, void* connection)
{
    {
        struct nabto_coap_server_resource* resource = nabto_coap_server_find_resource(server, message, NULL);

        if (!resource) {
            nabto_coap_server_make_error_response(server, connection, message, NABTO_COAP_CODE_NOT_FOUND, NULL);
            return NULL;
        }
    }

    struct nabto_coap_server_request* request = nabto_coap_server_request_new(server);
    if (!request) {
        nabto_coap_server_make_error_response(server, connection, message, NABTO_COAP_CODE_SERVICE_UNAVAILABLE, outOfResources);
        return NULL;
    }

    struct nabto_coap_server_resource* resource = nabto_coap_server_find_resource(server, message, &request->parameterSentinel);
    if (resource == NULL) {
        // we already know the resource exist, so NULL can only mean the parameter value could not be allocated.
        nabto_coap_server_make_error_response(server, connection, message, NABTO_COAP_CODE_SERVICE_UNAVAILABLE, outOfResources);
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
    request->resource = resource;

    nabto_coap_server_insert_request_into_list(server->requestsSentinel, request);

    return request;
}

void nabto_coap_server_handle_ack(struct nabto_coap_server* server, struct nabto_coap_server_request* request, struct nabto_coap_incoming_message* message)
{
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

    if (response->block2Current * blockSize > response->payloadLength) {
        request->state = NABTO_COAP_SERVER_REQUEST_STATE_DONE;
        nabto_coap_server_free_request(request);
        return;
    }

    // else wait for the client to ask for the next block.
}


void nabto_coap_server_handle_rst(struct nabto_coap_server* server, uint16_t messageId, void* connection)
{
    struct nabto_coap_server_request* request = server->requestsSentinel->next;
    while(request != server->requestsSentinel) {
        if (request->connection == connection) {
            if (request->response.messageId == messageId) {
                request->state = NABTO_COAP_SERVER_REQUEST_STATE_DONE;
                nabto_coap_server_free_request(request);
                return;
            }
        }
        request = request->next;
    }
}

struct nabto_coap_server_request* nabto_coap_server_find_request(struct nabto_coap_server* server, nabto_coap_token* token, void* connection)
{
    struct nabto_coap_server_request* request = server->requestsSentinel->next;
    while(request != server->requestsSentinel) {
        if (nabto_coap_token_equal(&request->token, token) &&
            connection == request->connection)
        {
            return request;
        }
        request = request->next;
    }
    return NULL;
}

struct nabto_coap_server_response* nabto_coap_server_find_response(struct nabto_coap_server* server, uint16_t messageId, void* connection)
{
    struct nabto_coap_server_request* request = server->requestsSentinel->next;
    while(request != server->requestsSentinel) {
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
                    struct nabto_coap_server_request_parameter* parameter = nabto_coap_server_request_parameter_new();
                    if (parameter == NULL) {
                        // calloc failed, return NULL to signal this. This only happens if we already found the resource in a previous call with parameters == NULL, so it is possible to return meaningfull error.
                        return NULL;
                    }
                    parameter->parameter = &currentNode->parameter;
                    parameter->value = calloc(1, optionLength + 1);
                    if (parameter->value == NULL) {
                        // calloc failed, return NULL to signal this. This only happens if we already found the resource in a previous call with parameters == NULL, so it is possible to return meaningfull error.
                        free(parameter);
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

struct nabto_coap_server_request* nabto_coap_server_request_new(struct nabto_coap_server* server)
{
    struct nabto_coap_server_request* request = calloc(1, sizeof(struct nabto_coap_server_request));
    if (request == NULL) {
        return NULL;
    }

    request->parameterSentinel.next = &request->parameterSentinel;
    request->parameterSentinel.prev = &request->parameterSentinel;
    request->isFreed = false;
    request->server = server;

    request->response.request = request;
    request->response.server = server;
    request->response.messageId = nabto_coap_server_next_message_id(server);
    request->response.block2Size = 5; // 512 byte blocks
    return request;
}


struct nabto_coap_server_request_parameter* nabto_coap_server_request_parameter_new()
{
    return calloc(1, sizeof(struct nabto_coap_server_request_parameter));
}
