#include <coap/nabto_coap_client_test.h>
#include "nabto_coap_client_impl.h"

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
    response->payload = malloc(payloadLength);
    memcpy(response->payload, payload, payloadLength);
    response->payloadLength = payloadLength;
}
void nabto_coap_client_test_response_set_content_format(struct nabto_coap_client_response* response, uint16_t contentFormat)
{
    response->contentFormat = contentFormat;
    response->hasContentFormat = true;
}