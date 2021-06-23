#include "nabto_coap_client_impl.h"

#include <stdlib.h>

static void nabto_coap_client_next_token(struct nabto_coap_client* client, nabto_coap_token* tokenOut);
static struct nabto_coap_client_request* nabto_coap_client_find_request(struct nabto_coap_client* client, struct nabto_coap_incoming_message* message, void* connection);

static bool nabto_coap_client_request_need_send(struct nabto_coap_client_request* request, uint32_t now);
static bool nabto_coap_client_request_need_wait(struct nabto_coap_client_request* requst);

static uint8_t* nabto_coap_client_request_create_packet(struct nabto_coap_client_request* request, uint32_t now, uint8_t* buffer, uint8_t* end, void** connection);

static uint16_t nabto_coap_client_next_message_id(struct nabto_coap_client* client);

/********************************************************************
 * Implementation of functions used from the coap client integrator *
 ********************************************************************/

nabto_coap_error nabto_coap_client_init(struct nabto_coap_client* client, nabto_coap_notify_event notifyEvent, void* userData)
{
    memset(client, 0, sizeof(struct nabto_coap_client));
    client->settings.ackTimeoutMilliseconds = 2000;
    client->settings.maxRetransmits = 6;
    client->messageIdCounter = 0;
    client->tokenCounter = 0;
    client->notifyEvent = notifyEvent;
    client->userData = userData;

    client->requestsSentinel = calloc(1, sizeof(struct nabto_coap_client_request));
    if (client->requestsSentinel == NULL) {
        return NABTO_COAP_ERROR_OUT_OF_MEMORY;
    }
    client->requestsSentinel->next = client->requestsSentinel;
    client->requestsSentinel->prev = client->requestsSentinel;
    return NABTO_COAP_ERROR_OK;
}

void nabto_coap_client_destroy(struct nabto_coap_client* client)
{
    free(client->requestsSentinel);
    client->requestsSentinel = NULL;
}

// insert request after e1 such that the chain e1->e2->e3 emerges
void nabto_coap_client_insert_request_into_list(struct nabto_coap_client_request* e1, struct nabto_coap_client_request* request)
{
    struct nabto_coap_client_request* e2 = request;
    struct nabto_coap_client_request* e3 = e1->next;
    e1->next = e2;
    e2->next = e3;
    e3->prev = e2;
    e2->prev = e1;
}

// remove request such that e1->request->e2 becomes e1->e2
void nabto_coap_client_remove_request_from_list(struct nabto_coap_client_request* request)
{
    struct nabto_coap_client_request* e1 = request->prev;
    struct nabto_coap_client_request* e2 = request->next;
    e1->next = e2;
    e2->prev = e1;
}

void nabto_coap_client_handle_callback(struct nabto_coap_client* client)
{
    struct nabto_coap_client_request* iterator = client->requestsSentinel->next;
    while(iterator != client->requestsSentinel) {
        if (iterator->state == NABTO_COAP_CLIENT_REQUEST_STATE_DONE_CALLBACK) {
            iterator->state = NABTO_COAP_CLIENT_REQUEST_STATE_DONE;
            iterator->endHandler(iterator, iterator->endHandlerUserData);
            // This potentially modifies the iterator if the user
            // decides to free the request in the callback.
            return;
        }
        iterator = iterator->next;
    }
}

enum nabto_coap_client_next_event nabto_coap_client_get_next_event(struct nabto_coap_client* client, uint32_t now)
{
    if (client->needSendRst) {
        return NABTO_COAP_CLIENT_NEXT_EVENT_SEND;
    }

    if (client->needSendAck) {
        return NABTO_COAP_CLIENT_NEXT_EVENT_SEND;
    }

    if (client->requestsSentinel->next == client->requestsSentinel) {
        return NABTO_COAP_CLIENT_NEXT_EVENT_NOTHING;
    }

    bool shouldWait = false;
    struct nabto_coap_client_request* iterator = client->requestsSentinel->next;
    while(iterator != client->requestsSentinel) {
        if (iterator->state == NABTO_COAP_CLIENT_REQUEST_STATE_DONE_CALLBACK) {
            return NABTO_COAP_CLIENT_NEXT_EVENT_CALLBACK;
        }
        if (nabto_coap_client_request_need_send(iterator, now)) {
            return NABTO_COAP_CLIENT_NEXT_EVENT_SEND;
        }
        if (nabto_coap_client_request_need_wait(iterator)) {
            shouldWait = true;
        }
        iterator = iterator->next;
    }

    if (shouldWait) {
        return NABTO_COAP_CLIENT_NEXT_EVENT_WAIT;
    } else {
        return NABTO_COAP_CLIENT_NEXT_EVENT_NOTHING;
    }
}

void nabto_coap_client_send_ack(struct nabto_coap_client* client, struct nabto_coap_incoming_message* message, void* connection)
{
    client->needSendAck = true;
    client->messageIdAck = message->messageId;
    client->connectionAck = connection;
}

enum nabto_coap_client_status nabto_coap_client_parse_and_handle_response(struct nabto_coap_client_request* request, struct nabto_coap_incoming_message* message, void* connection)
{
    struct nabto_coap_client_response* response = request->response;
    struct nabto_coap_client* client = request->client;
    if (response == NULL) {
        response = calloc(1, sizeof(struct nabto_coap_client_response));
        if (response == NULL) {
            return NABTO_COAP_CLIENT_STATUS_DECODE_ERROR;
        }
        response->request = request;
        request->response = response;
    }

    response->code = message->code;
    response->messageId = message->messageId;

    if (message->hasContentFormat) {
        response->hasContentFormat = true;
        response->contentFormat = message->contentFormat;
    }

    if (message->hasBlock2) {
        size_t offset = NABTO_COAP_BLOCK_OFFSET(message->block2);
        if (offset != response->payloadLength) {
            free(response);
            return NABTO_COAP_CLIENT_STATUS_DECODE_ERROR;
        }

        if (NABTO_COAP_BLOCK_MORE(message->block2) && message->payloadLength != NABTO_COAP_BLOCK_SIZE_ABSOLUTE(message->block2)) {
            free(response);
            return NABTO_COAP_CLIENT_STATUS_DECODE_ERROR;
        }

        void* payload = calloc(1, (response->payloadLength + message->payloadLength + 1));
        if (!payload) {
            free(response);
            return NABTO_COAP_CLIENT_STATUS_DECODE_ERROR;
        }
        if (response->payload) {
            memcpy(payload, response->payload, response->payloadLength);
            free(response->payload);
        }
        memcpy((uint8_t*)payload + response->payloadLength, message->payload, message->payloadLength);
        response->payload = payload;
        response->payloadLength = response->payloadLength + message->payloadLength;

        request->hasBlock2 = true;
        request->block2 = ((NABTO_COAP_BLOCK_NUM(message->block2) + 1) << 4) + (NABTO_COAP_BLOCK_SIZE(message->block2));
    } else {
        if (message->payloadLength > 0) {
            response->payload = (uint8_t*)calloc(1, message->payloadLength + 1);
            if (response->payload == NULL) {
                free(response);
                return NABTO_COAP_CLIENT_STATUS_DECODE_ERROR;
            }
            memcpy(response->payload, message->payload, message->payloadLength);
            response->payloadLength = message->payloadLength;
        }
    }

    if (message->hasBlock1) {
        request->block1Current += 1;
        if (request->block1Current * NABTO_COAP_BLOCK_SIZE_ABSOLUTE(request->block1Size) < request->payloadLength) {
            request->state = NABTO_COAP_CLIENT_REQUEST_STATE_SEND_REQUEST;
            request->messageId = nabto_coap_client_next_message_id(client);
            return NABTO_COAP_CLIENT_STATUS_OK;
        }
    }

    if (message->hasBlock2 && NABTO_COAP_BLOCK_MORE(message->block2)) {
        if (message->type == NABTO_COAP_TYPE_CON) {
            nabto_coap_client_send_ack(client, message, connection);
        }
        request->state = NABTO_COAP_CLIENT_REQUEST_STATE_SEND_REQUEST;
        request->messageId = nabto_coap_client_next_message_id(client);
    } else if (message->hasBlock1 && message->code == NABTO_COAP_CODE_CONTINUE) {
        request->block1Current += 1;
        if (request->block1Current * NABTO_COAP_BLOCK_SIZE_ABSOLUTE(request->block1Size) < request->payloadLength) {
            request->state = NABTO_COAP_CLIENT_REQUEST_STATE_SEND_REQUEST;
            request->messageId = nabto_coap_client_next_message_id(client);
        }
    } else {
        if (message->type == NABTO_COAP_TYPE_CON) {
            nabto_coap_client_send_ack(client, message, connection);
        }
        request->state = NABTO_COAP_CLIENT_REQUEST_STATE_DONE_CALLBACK;
    }

    return NABTO_COAP_CLIENT_STATUS_OK;
}

void nabto_coap_client_handle_ack(struct nabto_coap_client* client, struct nabto_coap_incoming_message* message, void* connection, uint32_t now)
{
    // acks is only correlated by message id, not by tokens.
    struct nabto_coap_client_request* iterator = client->requestsSentinel->next;
    while(iterator != client->requestsSentinel) {
        if (iterator->state == NABTO_COAP_CLIENT_REQUEST_STATE_WAIT_ACK) {
            if (iterator->messageId == message->messageId && iterator->connection == connection) {
                iterator->state = NABTO_COAP_CLIENT_REQUEST_STATE_WAIT_RESPONSE;
                iterator->timeoutStamp = now + iterator->configuredTimeoutMilliseconds;
                return;
            }
        }
        iterator = iterator->next;
    }
}

void nabto_coap_client_handle_rst(struct nabto_coap_client* client, struct nabto_coap_incoming_message* message, void* connection)
{
    // reset's is only correlated by message id, not by tokens.
    struct nabto_coap_client_request* iterator = client->requestsSentinel->next;
    while(iterator != client->requestsSentinel) {
        if (iterator->state == NABTO_COAP_CLIENT_REQUEST_STATE_WAIT_ACK ||
            iterator->state == NABTO_COAP_CLIENT_REQUEST_STATE_WAIT_RESPONSE)
        {
            if (iterator->messageId == message->messageId && iterator->connection == connection) {
                iterator->state = NABTO_COAP_CLIENT_REQUEST_STATE_DONE_CALLBACK;
                return;
            }
        }
        iterator = iterator->next;
    }

}

enum nabto_coap_client_status nabto_coap_client_handle_request_response(struct nabto_coap_client* client, struct nabto_coap_client_request* request, struct nabto_coap_incoming_message* message, void* connection)
{
    enum nabto_coap_client_status status;

    if (request->state == NABTO_COAP_CLIENT_REQUEST_STATE_WAIT_ACK ||
        request->state == NABTO_COAP_CLIENT_REQUEST_STATE_WAIT_RESPONSE)
    {
        // we are waiting for the response, handle it
        status = nabto_coap_client_parse_and_handle_response(request, message, connection);
        return status;
    } else {
        // we did not expext the packet
        if (message->type == NABTO_COAP_TYPE_NON || message->type == NABTO_COAP_TYPE_CON) {
            client->needSendRst = true;
            client->messageIdRst = message->messageId;
            client->connectionRst = request->connection;
            return NABTO_COAP_CLIENT_STATUS_OK;
        }
    }

    return NABTO_COAP_CLIENT_STATUS_OK;
}

enum nabto_coap_client_status nabto_coap_client_handle_packet(struct nabto_coap_client* client, uint32_t now, const uint8_t* packet, size_t packetSize, void* connection)
{
    struct nabto_coap_incoming_message message;
    if (!nabto_coap_parse_message(packet, packetSize, &message)) {
        return NABTO_COAP_CLIENT_STATUS_DECODE_ERROR;
    }

    // ack with empty is just acking a message but does not contain a response payload.
    if (message.type == NABTO_COAP_TYPE_ACK && message.code == NABTO_COAP_CODE_EMPTY) {
        nabto_coap_client_handle_ack(client, &message, connection, now);
        return NABTO_COAP_CLIENT_STATUS_OK;
    }

    if (message.type == NABTO_COAP_TYPE_RST) {
        nabto_coap_client_handle_rst(client, &message, connection);
        return NABTO_COAP_CLIENT_STATUS_OK;
    }

    // find request based on token.
    struct nabto_coap_client_request* request = nabto_coap_client_find_request(client, &message, connection);

    if (request) {
        return nabto_coap_client_handle_request_response(client, request, &message, connection);
    } else {
        if (message.type == NABTO_COAP_TYPE_NON || message.type == NABTO_COAP_TYPE_CON) {
            client->needSendRst = true;
            client->messageIdRst = message.messageId;
            client->connectionRst = connection;
            return NABTO_COAP_CLIENT_STATUS_OK;
        }
    }
    return NABTO_COAP_CLIENT_STATUS_OK;
}


struct nabto_coap_client_request* nabto_coap_client_find_request(struct nabto_coap_client* client, struct nabto_coap_incoming_message* message, void* connection)
{
    struct nabto_coap_client_request* request = client->requestsSentinel->next;
    while(request != client->requestsSentinel) {
        if (request->connection == connection &&
            nabto_coap_token_equal(&request->token, &message->token))
        {
            return request;
        }
        request = request->next;
    }
    return NULL;
}


void nabto_coap_client_next_token(struct nabto_coap_client* client, nabto_coap_token* tokenOut)
{
    client->tokenCounter++;
    uint64_t token = client->tokenCounter;
    tokenOut->tokenLength = 8;
    memcpy(tokenOut->token, &token, 8);
}

bool nabto_coap_client_request_need_send(struct nabto_coap_client_request* request, uint32_t now)
{
    (void)now;
    if (request->state == NABTO_COAP_CLIENT_REQUEST_STATE_SEND_REQUEST) {
        return true;
    }

    return false;
}

uint8_t* nabto_coap_client_create_packet(struct nabto_coap_client* client, uint32_t stamp, uint8_t* buffer, uint8_t* end, void** connection)
{
    uint8_t* ptr = (uint8_t*)buffer;

    if (client->needSendRst) {
        struct nabto_coap_message_header header;
        memset(&header, 0, sizeof(struct nabto_coap_message_header));
        header.type = NABTO_COAP_TYPE_RST;
        header.code = NABTO_COAP_CODE_EMPTY;
        header.messageId = client->messageIdRst;
        ptr = nabto_coap_encode_header(&header, ptr, end);

        client->needSendRst = false;
        *connection = client->connectionRst;
        return ptr;
    } else if (client->needSendAck) {
        struct nabto_coap_message_header header;
        memset(&header, 0, sizeof(struct nabto_coap_message_header));
        header.type = NABTO_COAP_TYPE_ACK;
        header.code = NABTO_COAP_CODE_EMPTY;
        header.messageId = client->messageIdAck;
        ptr = nabto_coap_encode_header(&header, ptr, end);

        client->needSendAck = false;
        *connection = client->connectionAck;
        return ptr;
    }

    struct nabto_coap_client_request* iterator = client->requestsSentinel->next;
    while(iterator != client->requestsSentinel) {
        if (nabto_coap_client_request_need_send(iterator, stamp)) {
            return nabto_coap_client_request_create_packet(iterator, stamp, buffer, end, connection);
        }
        iterator = iterator->next;
    }
    return NULL;
}

uint8_t* nabto_coap_client_request_create_packet(struct nabto_coap_client_request* request, uint32_t now, uint8_t* buffer, uint8_t* end, void** connection)
{
    struct nabto_coap_message_header header;
    memset(&header, 0, sizeof(struct nabto_coap_message_header));
    header.type = NABTO_COAP_TYPE_CON;
    header.code = request->method;
    header.messageId = request->messageId;
    header.token = request->token;
    *connection = request->connection;

    uint8_t* ptr = buffer;

    ptr = nabto_coap_encode_header(&header, ptr, end);

    uint16_t currentOption = 0;

    for (size_t i = 0; i < request->pathSegmentsLength; i++) {
        uint16_t optionDelta = NABTO_COAP_OPTION_URI_PATH - currentOption;
        const char* pathSegmentBegin = request->pathSegments[i];
        size_t pathSegmentLength = strlen(pathSegmentBegin);
        ptr = nabto_coap_encode_option(optionDelta, (const uint8_t*)pathSegmentBegin, pathSegmentLength, ptr, end);

        currentOption = NABTO_COAP_OPTION_URI_PATH;
    }

    if (request->hasContentFormat) {
        uint16_t optionDelta = NABTO_COAP_OPTION_CONTENT_FORMAT - currentOption;
        ptr = nabto_coap_encode_varint_option(optionDelta, request->contentFormat, ptr, end);
        currentOption = NABTO_COAP_OPTION_CONTENT_FORMAT;
    }

    if (request->hasBlock2) {
        uint16_t optionDelta = NABTO_COAP_OPTION_BLOCK2 - currentOption;
        ptr = nabto_coap_encode_varint_option(optionDelta, request->block2, ptr, end);
        currentOption = NABTO_COAP_OPTION_BLOCK2;
    }

    size_t blockSize = (16 << request->block1Size);
    size_t payloadOffset = (request->block1Current * blockSize);
    if (payloadOffset < request->payloadLength) {
        size_t payloadRestLength = request->payloadLength - payloadOffset;

        if (request->payloadLength > (16 << request->block1Size)) {
            // add block1 option
            uint32_t blockMore = 1;
            if (payloadRestLength <= blockSize) {
                blockMore = 0;
            }

            uint32_t blockOption = (request->block1Current << 4) + (blockMore << 3) + request->block1Size;

            uint16_t optionDelta = NABTO_COAP_OPTION_BLOCK1 - currentOption;
            ptr = nabto_coap_encode_varint_option(optionDelta, blockOption, ptr, end);
            currentOption = NABTO_COAP_OPTION_BLOCK1;
        }

        uint8_t* payloadRestStart = request->payload + payloadOffset;
        if (payloadRestLength > blockSize) {
            payloadRestLength = blockSize;
        }
        if (payloadOffset < request->payloadLength) {
            ptr = nabto_coap_encode_payload(payloadRestStart, payloadRestLength, ptr, end);
        }
    }

    struct nabto_coap_client* client = request->client;

    request->state = NABTO_COAP_CLIENT_REQUEST_STATE_WAIT_ACK;
    request->timeoutStamp = now + client->settings.ackTimeoutMilliseconds;
    request->retransmissions += 1;

    return ptr;
}

uint8_t* nabto_coap_client_create_ack_packet(struct nabto_coap_client_request* request, uint8_t* buffer, uint8_t* end)
{
    struct nabto_coap_message_header header;
    memset(&header, 0, sizeof(struct nabto_coap_message_header));
    uint8_t* ptr = buffer;

    header.type = NABTO_COAP_TYPE_ACK;
    header.code = NABTO_COAP_CODE_EMPTY;
    header.messageId = request->messageId;

    ptr = nabto_coap_encode_header(&header, ptr, end);

    return ptr;
}

bool nabto_coap_client_request_need_wait(struct nabto_coap_client_request* request)
{
    if (request->state == NABTO_COAP_CLIENT_REQUEST_STATE_WAIT_ACK) {
        return true;
    }
    if ( request->state == NABTO_COAP_CLIENT_REQUEST_STATE_WAIT_RESPONSE && request->configuredTimeoutMilliseconds != 0) {
        return true;
    }
    return false;
}

uint32_t nabto_coap_client_get_next_timeout(struct nabto_coap_client* client, uint32_t now)
{
    struct nabto_coap_client_request* request = client->requestsSentinel->next;
    uint32_t timeout = now + 42424242; // 42 million milliseconds is longer than any real timeout
    while(request != client->requestsSentinel)  {
        if (nabto_coap_client_request_need_wait(request)) {
            if (nabto_coap_is_stamp_less(request->timeoutStamp, timeout)) {
                timeout = request->timeoutStamp;
            }
        }
        request = request->next;
    }
    return timeout;
}

void nabto_coap_client_handle_timeout(struct nabto_coap_client* client, uint32_t now)
{
    struct nabto_coap_client_request* request = client->requestsSentinel->next;
    while(request != client->requestsSentinel)  {
        if (nabto_coap_client_request_need_wait(request)) {
            if (request->state == NABTO_COAP_CLIENT_REQUEST_STATE_WAIT_ACK) {
                if (nabto_coap_is_stamp_less_equal(request->timeoutStamp, now)) {
                    if (request->retransmissions > client->settings.maxRetransmits) {
                        request->status = NABTO_COAP_CLIENT_STATUS_TIMEOUT;
                        request->state = NABTO_COAP_CLIENT_REQUEST_STATE_DONE_CALLBACK;
                    } else {
                        request->retransmissions += 1;
                        request->state = NABTO_COAP_CLIENT_REQUEST_STATE_SEND_REQUEST;
                    }
                }
            } else if (request->state == NABTO_COAP_CLIENT_REQUEST_STATE_WAIT_RESPONSE) {
                if (nabto_coap_is_stamp_less_equal(request->timeoutStamp, now)) {
                    request->status = NABTO_COAP_CLIENT_STATUS_TIMEOUT;
                    request->state = NABTO_COAP_CLIENT_REQUEST_STATE_DONE_CALLBACK;
                    client->needSendRst = true;
                    client->messageIdRst = request->messageId;
                    client->connectionRst = request->connection;
                }
                /**
                 * we have received an ack on our con request, we are
                 * waiting for the server to create a response. There
                 * is no defined limit of how much time this exchange
                 * can take at most.
                 *
                 * The timeout should be set by the client which
                 * invoked the request.
                 */

            }
        }
        request = request->next;
    }
}

void nabto_coap_client_stop(struct nabto_coap_client* client)
{
    struct nabto_coap_client_request* request = client->requestsSentinel->next;
    while(request != client->requestsSentinel)  {
        if (request->state < NABTO_COAP_CLIENT_REQUEST_STATE_DONE_CALLBACK) {
            request->status = NABTO_COAP_CLIENT_STATUS_STOPPED;
            request->state = NABTO_COAP_CLIENT_REQUEST_STATE_DONE_CALLBACK;
        }
        request = request->next;
    }
}

/***********************************
 * Implementation of coap requests *
 ***********************************/

struct nabto_coap_client_request* nabto_coap_client_request_new(struct nabto_coap_client* client, nabto_coap_method method, size_t pathSegmentsLength, const char** pathSegments, nabto_coap_client_request_end_handler endHandler, void* endHandlerUserData, void* connection)
{
    struct nabto_coap_client_request* request = calloc(1, sizeof(struct nabto_coap_client_request));
    if (request == NULL) {
        return NULL;
    }
    request->state = NABTO_COAP_CLIENT_REQUEST_STATE_IDLE;
    request->type = NABTO_COAP_TYPE_CON;
    request->method = (nabto_coap_code)method;
    request->pathSegmentsLength = pathSegmentsLength;
    request->pathSegments = pathSegments;

    request->configuredTimeoutMilliseconds = 2*60*1000; // 2 min

    nabto_coap_client_next_token(client, &request->token);
    request->client = client;
    request->messageId = nabto_coap_client_next_message_id(client);

    nabto_coap_client_insert_request_into_list(client->requestsSentinel, request);

    request->endHandler = endHandler;
    request->endHandlerUserData = endHandlerUserData;

    request->block1Size = 5; // 512 byte blocks as default. 16 * 2^5.
    request->connection = connection;

    return request;
}

void nabto_coap_client_request_send(struct nabto_coap_client_request* request)
{
    struct nabto_coap_client* client = request->client;
    request->state = NABTO_COAP_CLIENT_REQUEST_STATE_SEND_REQUEST;
    client->notifyEvent(client->userData);
}

/**
 * Cancel a sent request.
 */
void nabto_coap_client_request_cancel(struct nabto_coap_client_request* request)
{
    struct nabto_coap_client* client = request->client;
    if (request->state == NABTO_COAP_CLIENT_REQUEST_STATE_SEND_REQUEST)
    {
        request->state = NABTO_COAP_CLIENT_REQUEST_STATE_DONE_CALLBACK;
        request->status = NABTO_COAP_CLIENT_STATUS_STOPPED;
        client->notifyEvent(client->userData);
    } else if (request->state == NABTO_COAP_CLIENT_REQUEST_STATE_WAIT_ACK ||
               request->state == NABTO_COAP_CLIENT_REQUEST_STATE_WAIT_RESPONSE)
    {
        client->needSendRst = true;
        client->messageIdRst = request->messageId;
        client->connectionRst = request->connection;
        request->state = NABTO_COAP_CLIENT_REQUEST_STATE_DONE_CALLBACK;
        request->status = NABTO_COAP_CLIENT_STATUS_STOPPED;
        client->notifyEvent(client->userData);
    }
}


void nabto_coap_client_response_free(struct nabto_coap_client_response* response) {
    if (response->payloadLength != 0) {
        free(response->payload);
    }
    free(response);
}

/**
 * free a request
 */
void nabto_coap_client_request_free(struct nabto_coap_client_request* request)
{
    if (request->response != NULL) {
        nabto_coap_client_response_free(request->response);
    }

    nabto_coap_client_remove_request_from_list(request);

    if (request->payloadLength) {
        free(request->payload);
    }

    free(request);
}

/**
 * set the content format of the request body
 */
void nabto_coap_client_request_set_content_format(struct nabto_coap_client_request* request, uint16_t format)
{
    request->hasContentFormat = true;
    request->contentFormat = format;
}

nabto_coap_error nabto_coap_client_request_set_payload(struct nabto_coap_client_request* request, void* payload, size_t payloadLength)
{
    if (request->payloadLength > 0) {
        return NABTO_COAP_ERROR_INVALID_PARAMETER;
    }
    if (payloadLength == 0) {
        return NABTO_COAP_ERROR_OK;
    }
    request->payload = calloc(1, payloadLength);
    if (request->payload == NULL) {
        return NABTO_COAP_ERROR_OUT_OF_MEMORY;
    }
    memcpy(request->payload, payload, payloadLength);
    request->payloadLength = payloadLength;
    return NABTO_COAP_ERROR_OK;
}

void nabto_coap_client_request_set_timeout(struct nabto_coap_client_request* request, uint32_t timeout)
{
    request->configuredTimeoutMilliseconds = timeout;
}

void nabto_coap_client_remove_connection(struct nabto_coap_client* client, void *connection)
{
    // Stop all outstanding requests for that connection. We assume
    // there is no time for the module to send RST's

    struct nabto_coap_client_request* request = client->requestsSentinel->next;
    while(request != client->requestsSentinel) {
        if (request->connection == connection) {
            if (request->state < NABTO_COAP_CLIENT_REQUEST_STATE_DONE_CALLBACK) {
                request->status = NABTO_COAP_CLIENT_STATUS_STOPPED;
                request->state = NABTO_COAP_CLIENT_REQUEST_STATE_DONE_CALLBACK;
            }
        }
        request = request->next;
    }
    client->notifyEvent(client->userData);
}


/**
 * set the expeched content format of the response body.
 * this can be called several times.
 */
//void nabto_coap_client_request_accept(struct nabto_coap_client_request* request, uint16_t format);


//void nabto_coap_client_request_observe(struct nabto_coap_client_request* request);

enum nabto_coap_client_status nabto_coap_client_request_get_status(struct nabto_coap_client_request* request)
{
    if (request->state == NABTO_COAP_CLIENT_REQUEST_STATE_DONE) {
        return request->status;
    } else {
        return NABTO_COAP_CLIENT_STATUS_IN_PROGRESS;
    }
}

struct nabto_coap_client_response* nabto_coap_client_request_get_response(struct nabto_coap_client_request* request)
{
    return request->response;
}

void nabto_coap_client_request_set_nonconfirmable(struct nabto_coap_client_request* request)
{
    request->type = NABTO_COAP_TYPE_NON;
}

uint16_t nabto_coap_client_response_get_code(struct nabto_coap_client_response* response)
{
    if (!response) {
        return 0;
    }
    uint8_t compactCode = response->code;
    return ((compactCode >> 5)) * 100 + (compactCode & 0x1f);
}

bool nabto_coap_client_response_get_content_format(struct nabto_coap_client_response* response, uint16_t* contentFormat)
{
    if (!response) {
        return false;
    }
    if (response->hasContentFormat) {
        *contentFormat = response->contentFormat;
        return true;
    }
    return false;
}

bool nabto_coap_client_response_get_payload(struct nabto_coap_client_response* response, const uint8_t** payload, size_t* payloadLength)
{
    if (!response) {
        return false;
    }

    if (response->payloadLength > 0) {
        *payload = response->payload;
        *payloadLength = response->payloadLength;
        return true;
    }
    return false;
}

static uint16_t nabto_coap_client_next_message_id(struct nabto_coap_client* client)
{
    client->messageIdCounter++;
    return client->messageIdCounter;
}
