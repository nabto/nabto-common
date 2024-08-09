#ifndef _NABTO_COAP_CLIENT_TEST_H_
#define _NABTO_COAP_CLIENT_TEST_H_

#include <nabto_coap/nabto_coap.h>

// define some function such that test instances can be created when testing code

#ifdef __cplusplus
extern "C" {
#endif

struct nabto_coap_client_response* nabto_coap_client_test_create_response();
void nabto_coap_client_test_response_free(struct nabto_coap_client_response* response);
void nabto_coap_client_test_response_set_code(struct nabto_coap_client_response* response, nabto_coap_code code);
void nabto_coap_client_test_response_set_payload(struct nabto_coap_client_response* response, const uint8_t* payload, size_t payloadLength);
void nabto_coap_client_test_response_set_content_format(struct nabto_coap_client_response* response, uint16_t contentFormat);


#ifdef __cplusplus
} // extern c
#endif

#endif
