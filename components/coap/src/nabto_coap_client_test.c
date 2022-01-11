#include <coap/nabto_coap_client_test.h>
#include "nabto_coap_client_impl.h"

// this code is used to create a response which unit tests can use when testing
// decoding, so it is using the default calloc/free allocations.
#include <stdlib.h>

struct nabto_coap_client_response* nabto_coap_client_test_create_response()
{
    struct nabto_coap_client_response* response = (struct nabto_coap_client_response*)calloc(1, sizeof(struct nabto_coap_client_response));
    return response;
}

void nabto_coap_client_test_response_set_code(struct nabto_coap_client_response* response, nabto_coap_code code)
{
    response->code = code;
}
void nabto_coap_client_test_response_set_payload(struct nabto_coap_client_response* response, const uint8_t* payload, size_t payloadLength)
{
    response->payload = calloc(1,payloadLength);
    memcpy(response->payload, payload, payloadLength);
    response->payloadLength = payloadLength;
}
void nabto_coap_client_test_response_set_content_format(struct nabto_coap_client_response* response, uint16_t contentFormat)
{
    response->contentFormat = contentFormat;
    response->hasContentFormat = true;
}

void nabto_coap_client_test_response_free(struct nabto_coap_client_response* response)
{
    free(response->payload);
    free(response);
}
