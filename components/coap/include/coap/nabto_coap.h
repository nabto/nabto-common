#ifndef _NABTO_COAP_H_
#define _NABTO_COAP_H_

#include <stdint.h>
#include <string.h>
#include <stdbool.h>


#ifdef __cplusplus
extern "C" {
#endif

enum {
    NABTO_COAP_ACK_TIMEOUT = 2000,
    NABTO_COAP_MAX_RETRANSMITS = 4
};

typedef enum {
    NABTO_COAP_ERROR_OK,
    NABTO_COAP_ERROR_OUT_OF_MEMORY,
    NABTO_COAP_ERROR_NO_CONNECTION,
    NABTO_COAP_ERROR_INVALID_PARAMETER
} nabto_coap_error;

// read the error codes as NABTO_COAP_CODE(4,4) => 4.04, if we add a 0
// it is parsed as octets, this fails for a few error codes with
// numbers larger than 7
#define NABTO_COAP_CODE(class,code) (((class) << 5) + (code))

typedef enum {
    NABTO_COAP_CODE_EMPTY = NABTO_COAP_CODE(0,0),

    NABTO_COAP_CODE_GET = NABTO_COAP_CODE(0,1),
    NABTO_COAP_CODE_POST = NABTO_COAP_CODE(0,2),
    NABTO_COAP_CODE_PUT = NABTO_COAP_CODE(0,3),
    NABTO_COAP_CODE_DELETE = NABTO_COAP_CODE(0,4),

    NABTO_COAP_CODE_CREATED = NABTO_COAP_CODE(2,1),
    NABTO_COAP_CODE_DELETED = NABTO_COAP_CODE(2,2),
    NABTO_COAP_CODE_VALID = NABTO_COAP_CODE(2,3),
    NABTO_COAP_CODE_CHANGED = NABTO_COAP_CODE(2,4),
    NABTO_COAP_CODE_CONTENT = NABTO_COAP_CODE(2,5),
    NABTO_COAP_CODE_CONTINUE = NABTO_COAP_CODE(2,31),

    NABTO_COAP_CODE_BAD_REQUEST = NABTO_COAP_CODE(4,0),
    NABTO_COAP_CODE_UNAUTHORIZED = NABTO_COAP_CODE(4,1),
    NABTO_COAP_CODE_BAD_OPTION = NABTO_COAP_CODE(4,2),
    NABTO_COAP_CODE_FORBIDDEN = NABTO_COAP_CODE(4,3),
    NABTO_COAP_CODE_NOT_FOUND = NABTO_COAP_CODE(4,4),
    NABTO_COAP_CODE_METHOD_NOT_ALLOWED = NABTO_COAP_CODE(4,5),
    NABTO_COAP_CODE_NOT_ACCEPTABLE = NABTO_COAP_CODE(4,6),
    NABTO_COAP_CODE_REQUEST_ENTITY_INCOMPLETE = NABTO_COAP_CODE(4,8),
    NABTO_COAP_CODE_CONFLICT = NABTO_COAP_CODE(4,9),
    NABTO_COAP_CODE_PRECONDITION_FAILED = NABTO_COAP_CODE(4,12),
    NABTO_COAP_CODE_REQUEST_ENTITY_TOO_LARGE = NABTO_COAP_CODE(4,13),
    NABTO_COAP_CODE_UNSUPPORTED_CONTENT_FORMAT = NABTO_COAP_CODE(4,15),

    NABTO_COAP_CODE_INTERNAL_SERVER_ERROR = NABTO_COAP_CODE(5,0),
    NABTO_COAP_CODE_NOT_IMPLEMENTED = NABTO_COAP_CODE(5,1),
    NABTO_COAP_CODE_BAD_GATEWAY = NABTO_COAP_CODE(5,2),
    NABTO_COAP_CODE_SERVICE_UNAVAILABLE = NABTO_COAP_CODE(5,3),
    NABTO_COAP_CODE_GATEWAY_TIMEOUT = NABTO_COAP_CODE(5,4),
    NABTO_COAP_CODE_PROXYING_NOT_SUPPORTED = NABTO_COAP_CODE(5,5),
} nabto_coap_code;

typedef enum {
    NABTO_COAP_METHOD_GET = 1,
    NABTO_COAP_METHOD_POST = 2,
    NABTO_COAP_METHOD_PUT = 3,
    NABTO_COAP_METHOD_DELETE = 4
} nabto_coap_method;

typedef enum {
    NABTO_COAP_TYPE_CON = 0,
    NABTO_COAP_TYPE_NON = 1,
    NABTO_COAP_TYPE_ACK = 2,
    NABTO_COAP_TYPE_RST = 3
} nabto_coap_type;

typedef enum {
    NABTO_COAP_OPTION_IF_MATCH = 1,
    NABTO_COAP_OPTION_URI_HOST = 3,
    NABTO_COAP_OPTION_ETAG = 4,
    NABTO_COAP_OPTION_IF_NONE_MATCH = 5,
    NABTO_COAP_OPTION_URI_PORT = 7,
    NABTO_COAP_OPTION_LOCATION_PATH = 8,
    NABTO_COAP_OPTION_URI_PATH = 11,
    NABTO_COAP_OPTION_CONTENT_FORMAT = 12,
    NABTO_COAP_OPTION_MAX_AGE = 14,
    NABTO_COAP_OPTION_URI_QUERY = 15,
    NABTO_COAP_OPTION_ACCEPT = 17,
    NABTO_COAP_OPTION_LOCATION_QUERY = 20,
    NABTO_COAP_OPTION_BLOCK2 = 23,
    NABTO_COAP_OPTION_BLOCK1 = 27,
    NABTO_COAP_OPTION_SIZE2 = 28,
    NABTO_COAP_OPTION_PROXY_URI = 35,
    NABTO_COAP_OPTION_PROXY_SCHEME = 39,
    NABTO_COAP_OPTION_SIZE1 = 60
} nabto_coap_option;

typedef enum  {
    NABTO_COAP_CONTENT_FORMAT_TEXT_PLAIN_UTF8 = 0,
    NABTO_COAP_CONTENT_FORMAT_APPLICATION_OCTET_STREAM = 42,
    NABTO_COAP_CONTENT_FORMAT_APPLICATION_JSON = 50,
    NABTO_COAP_CONTENT_FORMAT_APPLICATION_CBOR = 60,
} nabto_coap_content_format;

typedef struct {
    uint8_t token[8];
    uint8_t tokenLength;
} nabto_coap_token;

struct nabto_coap_option_iterator {
    const uint8_t* buffer;
    const uint8_t* bufferEnd;
    uint16_t option;
    const uint8_t* optionDataBegin;
    const uint8_t* optionDataEnd;
};

struct nabto_coap_option_data {
    nabto_coap_option option;
    uint8_t* optionData;
    uint16_t optionLength;
};

nabto_coap_code nabto_coap_uint16_to_code(uint16_t code);

void nabto_coap_option_iterator_init(struct nabto_coap_option_iterator* iterator, const uint8_t* options, const uint8_t* optionsEnd);

struct nabto_coap_option_iterator* nabto_coap_get_next_option(struct nabto_coap_option_iterator* iterator);

/**
 * return NULL if no more options of this type exists.
 */
struct nabto_coap_option_iterator* nabto_coap_get_option(nabto_coap_option option, struct nabto_coap_option_iterator* iterator);

struct nabto_coap_incoming_message {
    nabto_coap_type type;
    nabto_coap_code code;
    uint16_t messageId;
    nabto_coap_token token;
    const uint8_t* options;
    size_t optionsLength;
    const uint8_t* payload;
    size_t payloadLength;
    // some nonrepeatable options which is nice to have decoded.
    bool hasContentFormat;
    uint16_t contentFormat;
    bool hasBlock1;
    uint32_t block1;
    bool hasBlock2;
    uint32_t block2;
};



#define NABTO_COAP_BLOCK_SIZE(value) ((value) & 0x7u)
#define NABTO_COAP_BLOCK_SIZE_ABSOLUTE(value) (16u << NABTO_COAP_BLOCK_SIZE(value))
#define NABTO_COAP_BLOCK_NUM(value) ((value) >> 4)
#define NABTO_COAP_BLOCK_MORE(value) (((value) & 0xF) >> 3)
#define NABTO_COAP_BLOCK_OFFSET(value) (NABTO_COAP_BLOCK_SIZE_ABSOLUTE(value) * NABTO_COAP_BLOCK_NUM(value))


bool nabto_coap_parse_message(const uint8_t* packet, size_t packetSize, struct nabto_coap_incoming_message* message);

bool nabto_coap_is_stamp_less(uint32_t s1, uint32_t s2);
bool nabto_coap_is_stamp_less_equal(uint32_t s1, uint32_t s2);

uint32_t nabto_coap_stamp_min(uint32_t s1, uint32_t s2);

/**
 * diff between two stamps.
 *
 * negative if s1 < s2
 */
int32_t nabto_coap_stamp_diff(uint32_t s1, uint32_t s2);

struct nabto_coap_message_header {
    nabto_coap_type type;
    nabto_coap_code code;
    uint16_t messageId;
    nabto_coap_token token;
};

uint8_t* nabto_coap_encode_header(struct nabto_coap_message_header* header, uint8_t* buffer, uint8_t* bufferEnd);

/**
 * Encode an option with the given delta.
 */
uint8_t* nabto_coap_encode_option(uint16_t optionDelta, const uint8_t* optionData, size_t optionDataLength, uint8_t* buffer, uint8_t* bufferEnd);

uint8_t* nabto_coap_encode_varint_option(uint16_t optionDelta, const uint32_t value, uint8_t* buffer, uint8_t* bufferEnd);

uint8_t* nabto_coap_encode_payload(const uint8_t* payloadBegin, size_t payloadLength, uint8_t* buffer, uint8_t* bufferEnd);

uint32_t nabto_coap_next_retransmission_timeout(uint8_t retransmissions);

bool nabto_coap_token_equal(nabto_coap_token* t1, nabto_coap_token* t2);

typedef uint32_t (*nabto_coap_get_stamp)(void* userData);
typedef void (*nabto_coap_notify_event)(void* userData);


bool nabto_coap_parse_variable_int(const uint8_t* bufferStart, const uint8_t* bufferEnd, uint8_t maxBytes, uint32_t* result);
bool nabto_coap_encode_variable_int(uint8_t* bufferStart, uint8_t maxBufferLength, uint32_t value, size_t* encodedLength);

const char* nabto_coap_error_to_string(nabto_coap_error err);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
