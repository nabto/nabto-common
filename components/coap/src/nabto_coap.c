#include <nabto_coap/nabto_coap.h>

#include <stddef.h>

nabto_coap_code nabto_coap_uint16_to_code(uint16_t code)
{
    uint16_t class = code/100;
    code = code - ( class*100 );
    return (nabto_coap_code)(NABTO_COAP_CODE(class, code));
}


/**
 * Decode an option delta or length field, RFC 7252 section 3.1. The
 * nibble from the first byte of the option is followed by no, one or
 * two extension bytes at ptr.
 *
 * @return pointer past the extension bytes, or NULL if the nibble is
 * the reserved value 15 or the extension bytes are not present.
 */
static const uint8_t* decode_option_field(uint8_t nibble, const uint8_t* ptr, const uint8_t* end, uint32_t* value)
{
    if (nibble < 13) {
        *value = nibble;
        return ptr;
    } else if (nibble == 13) {
        // one extra byte
        if (end - ptr < 1) {
            return NULL;
        }
        *value = ((uint32_t)ptr[0]) + 13;
        return ptr + 1;
    } else if (nibble == 14) {
        // two extra bytes, 269 = 255 + 14
        if (end - ptr < 2) {
            return NULL;
        }
        *value = (((uint32_t)ptr[0]) << 8) + ((uint32_t)ptr[1]) + 269;
        return ptr + 2;
    } else {
        // reserved
        return NULL;
    }
}

struct nabto_coap_option_iterator* nabto_coap_get_next_option(struct nabto_coap_option_iterator* iterator)
{
    if (iterator->buffer == NULL || iterator->bufferEnd - iterator->buffer < 1) {
        return NULL;
    }

    const uint8_t* ptr = iterator->buffer;
    const uint8_t* end = iterator->bufferEnd;

    uint8_t first = *ptr;
    ptr += 1;

    uint32_t delta;
    uint32_t length;
    ptr = decode_option_field(first >> 4, ptr, end, &delta);
    if (ptr == NULL) {
        return NULL;
    }
    ptr = decode_option_field(first & 0x0F, ptr, end, &length);
    if (ptr == NULL) {
        return NULL;
    }

    if ((size_t)(end - ptr) < length) {
        return NULL;
    }

    delta += iterator->option;

    if (delta > 0xFFFF) {
        return NULL;
    }

    iterator->option = (uint16_t)delta;
    iterator->optionDataBegin = ptr;
    iterator->optionDataEnd = ptr + length;
    iterator->buffer = ptr + length;

    return iterator;
}

void nabto_coap_option_iterator_init(struct nabto_coap_option_iterator* iterator, const uint8_t* optionBegin, const uint8_t* optionEnd)
{
    memset(iterator, 0, sizeof(struct nabto_coap_option_iterator));
    iterator->buffer = optionBegin;
    iterator->bufferEnd = optionEnd;
}

struct nabto_coap_option_iterator* nabto_coap_get_option(nabto_coap_option optionNumber, struct nabto_coap_option_iterator* iterator)
{
    iterator = nabto_coap_get_next_option(iterator);
    while(iterator != NULL) {
        if (iterator->option == optionNumber) {
            return iterator;
        }
        iterator = nabto_coap_get_next_option(iterator);
    }
    return NULL;
}


bool nabto_coap_parse_message(const uint8_t* packet, size_t packetSize, struct nabto_coap_incoming_message* msg)
{
    memset(msg, 0, sizeof(struct nabto_coap_incoming_message));
    const uint8_t* end = packet + packetSize;
    const uint8_t* ptr = packet;
    if (packetSize < 4) {
        return false;
    }

    uint8_t first = packet[0];

    uint8_t version = first >> 6;
    msg->type = (nabto_coap_type)((first & 0x30)  >> 4);
    uint8_t tokenLength = (first & 0x0f);

    msg->code = (nabto_coap_code)(packet[1]);
    msg->messageId = ((uint16_t)packet[2] << 8) + ((uint16_t)packet[3]);

    ptr = packet + 4;

    if (version != 1) {
        return false;
    }

    if (tokenLength > 8) {
        return false;
    }

    msg->token.tokenLength = tokenLength;
    if (packetSize < tokenLength + 4u) {
        return false;
    }
    memcpy(msg->token.token, ptr, tokenLength);
    ptr += tokenLength;

    if (end - ptr < 1) {
        msg->options = NULL;
        msg->payload = NULL;
        msg->payloadLength = 0;
        return true;
    }

    msg->options = ptr;

    // Walk the options once: this both validates their framing and
    // decodes the non repeatable options the message struct carries.
    struct nabto_coap_option_iterator iteratorData;
    struct nabto_coap_option_iterator* iterator = &iteratorData;

    nabto_coap_option_iterator_init(iterator, msg->options, end);
    iterator = nabto_coap_get_next_option(iterator);
    while (iterator != NULL) {
        uint32_t value;
        switch (iterator->option) {
            case NABTO_COAP_OPTION_OBSERVE:
                if (!nabto_coap_parse_variable_int(iterator->optionDataBegin, iterator->optionDataEnd, 3, &value)) {
                    return false;
                }
                msg->hasObserve = true;
                msg->observe = value;
                break;
            case NABTO_COAP_OPTION_CONTENT_FORMAT:
                if (!nabto_coap_parse_variable_int(iterator->optionDataBegin, iterator->optionDataEnd, 2, &value)) {
                    return false;
                }
                msg->hasContentFormat = true;
                msg->contentFormat = (uint16_t)value;
                break;
            case NABTO_COAP_OPTION_BLOCK1:
                if (!nabto_coap_parse_variable_int(iterator->optionDataBegin, iterator->optionDataEnd, 3, &value)) {
                    return false;
                }
                if (msg->hasBlock1) {
                    msg->hasRepeatedBlockOption = true;
                }
                msg->hasBlock1 = true;
                msg->block1 = value;
                break;
            case NABTO_COAP_OPTION_BLOCK2:
                if (!nabto_coap_parse_variable_int(iterator->optionDataBegin, iterator->optionDataEnd, 3, &value)) {
                    return false;
                }
                if (msg->hasBlock2) {
                    msg->hasRepeatedBlockOption = true;
                }
                msg->hasBlock2 = true;
                msg->block2 = value;
                break;
            default:
                break;
        }
        iterator = nabto_coap_get_next_option(iterator);
    }

    // The iterator stops at the end of the packet, at the payload
    // marker, or at an option it could not decode. Only the first two
    // are a well formed option list.
    ptr = iteratorData.buffer;
    msg->optionsLength = ptr - msg->options;

    if (end - ptr == 0) {
        // end of options no payload
        msg->payload = NULL;
        msg->payloadLength = 0;
        return true;
    }
    if (*ptr == 0xFF) {
        // end of options marker;
        if (end - ptr < 2) {
            // there needs to be atleast one byte payload.
            return false;
        }
        ptr += 1;

        msg->payload = ptr;
        msg->payloadLength = end - ptr;
        return true;
    }

    // malformed option
    return false;
}

uint8_t* nabto_coap_encode_header(struct nabto_coap_message_header* header, uint8_t* buffer, uint8_t* bufferEnd)
{
    if (buffer == NULL ||
        (bufferEnd - buffer) < (ptrdiff_t)(4 + header->token.tokenLength))
    {
        return NULL;
    }
    uint8_t firstByte = 0x40; //01xxxxxx
    firstByte |= ((header->type << 4) & 0x30);
    firstByte |= (header->token.tokenLength & 0x0f);

    buffer[0] = firstByte;
    buffer[1] = header->code;
    buffer[2] = (uint8_t)(header->messageId >> 8);
    buffer[3] = (uint8_t)(header->messageId);
    uint8_t* ptr = buffer + 4;
    memcpy(ptr, header->token.token, header->token.tokenLength);
    ptr += header->token.tokenLength;
    return ptr;
}

/**
 * Encode an option delta or length field, RFC 7252 section 3.1. The
 * nibble for the first byte of the option is returned through nibble,
 * the no, one or two extension bytes are written at ptr.
 *
 * @return pointer past the extension bytes, or NULL if they do not fit
 * before end.
 */
static uint8_t* encode_option_field(uint32_t value, uint8_t* ptr, uint8_t* end, uint8_t* nibble)
{
    if (value < 13) {
        *nibble = (uint8_t)value;
        return ptr;
    } else if (value < 269) {
        if (end - ptr < 1) {
            return NULL;
        }
        *nibble = 13;
        ptr[0] = (uint8_t)(value - 13);
        return ptr + 1;
    } else {
        if (end - ptr < 2) {
            return NULL;
        }
        *nibble = 14;
        value -= 269;
        ptr[0] = (uint8_t)(value >> 8);
        ptr[1] = (uint8_t)(value);
        return ptr + 2;
    }
}

uint8_t* nabto_coap_encode_option(uint16_t optionDelta, const uint8_t* optionData, size_t optionDataLength, uint8_t* buffer, uint8_t* bufferEnd)
{
    // RFC 7252 section 3.1: the option length field with two extension
    // bytes covers 269..65804 (0xFFFF + 269); longer options cannot be
    // encoded. The delta is limited by its uint16_t type.
    if (optionDataLength > NABTO_COAP_MAX_OPTION_LENGTH) {
        return NULL;
    }
    if (buffer == NULL || bufferEnd - buffer < 1) {
        return NULL;
    }

    uint8_t deltaNibble;
    uint8_t lengthNibble;
    uint8_t* ptr = buffer + 1;
    ptr = encode_option_field(optionDelta, ptr, bufferEnd, &deltaNibble);
    if (ptr == NULL) {
        return NULL;
    }
    ptr = encode_option_field((uint32_t)optionDataLength, ptr, bufferEnd, &lengthNibble);
    if (ptr == NULL) {
        return NULL;
    }
    if ((size_t)(bufferEnd - ptr) < optionDataLength) {
        return NULL;
    }

    *buffer = (uint8_t)((deltaNibble << 4) | lengthNibble);

    if (optionDataLength > 0) {
        memcpy(ptr, optionData, optionDataLength);
    }
    return ptr + optionDataLength;
}

uint8_t* nabto_coap_encode_payload(const uint8_t* payloadBegin, size_t payloadLength, uint8_t* buffer, uint8_t* bufferEnd)
{
    if (payloadLength == 0 || payloadBegin == NULL) {
        return buffer;
    }

    if (buffer == NULL || bufferEnd - buffer < (ptrdiff_t)payloadLength + 1) {
        return NULL;
    }


    uint8_t* ptr = buffer;
    *ptr = 0xFF;
    ptr++;

    memcpy(ptr, payloadBegin, payloadLength);
    ptr += payloadLength;
    return ptr;
}

bool nabto_coap_is_stamp_less(uint32_t s1, uint32_t s2)
{
    return (int32_t)(s1 - s2) < 0;
}

bool nabto_coap_is_stamp_less_equal(uint32_t s1, uint32_t s2)
{
    int32_t diff = (int32_t)(s1 - s2);
    return diff <= 0;
}

uint32_t nabto_coap_stamp_min(uint32_t s1, uint32_t s2)
{
    if (nabto_coap_is_stamp_less_equal(s1, s2)) {
        return s1;
    } else {
        return s2;
    }
}

int32_t nabto_coap_stamp_diff(uint32_t s1, uint32_t s2)
{
    return (int32_t)(s1 - s2);
}

bool nabto_coap_token_equal(nabto_coap_token* t1, nabto_coap_token* t2)
{
    if (t1->tokenLength != t2->tokenLength) {
        return false;
    }
    return (memcmp(t1->token, t2->token, t1->tokenLength) == 0);
}

bool nabto_coap_parse_variable_int(const uint8_t* bufferStart, const uint8_t* bufferEnd, uint8_t maxBytes, uint32_t* result)
{
    size_t length;
    const uint8_t* ptr;
    if (bufferEnd - bufferStart < 0) {
        return false;
    }
    if (bufferEnd - bufferStart > maxBytes) {
        return false;
    }
    length = bufferEnd - bufferStart;
    ptr = bufferStart;
    *result = 0;
    while (length > 0) {
        *result = ((*result) << 8);
        *result += *ptr;
        length--;
        ptr++;
    }
    return true;
}

bool nabto_coap_encode_variable_int(uint8_t* bufferStart, uint8_t maxBufferLength, uint32_t value, size_t* encodedLength)
{
    uint8_t valueLengthBytes = 0;
    uint32_t valueCopy = value;
    uint8_t* ptr;

    while(valueCopy > 0) {
        valueLengthBytes += 1;
        valueCopy = (valueCopy >> 8);
    }

    if (valueLengthBytes > maxBufferLength) {
        return false;
    }

    ptr = bufferStart + valueLengthBytes;

    *encodedLength = valueLengthBytes;

    while(value > 0) {
        ptr--;
        *ptr = (value & 0xFF);
        value = (value >> 8);
    }

    if (value > 0) {
        return false;
    }

    return true;
}

uint8_t* nabto_coap_encode_varint_option(uint16_t optionDelta, const uint32_t value, uint8_t* buffer, uint8_t* bufferEnd)
{
    uint8_t encoded[4];
    size_t encodedLength;
    if(!nabto_coap_encode_variable_int(encoded, 4, value, &encodedLength)) {
        return NULL;
    };

    return nabto_coap_encode_option(optionDelta, encoded, encodedLength, buffer, bufferEnd);

}

const char* nabto_coap_error_to_string(nabto_coap_error err)
{
    switch (err) {
        case NABTO_COAP_ERROR_OK:
            return "NABTO_COAP_ERROR_OK";
        case NABTO_COAP_ERROR_OUT_OF_MEMORY:
            return "NABTO_COAP_ERROR_OUT_OF_MEMORY";
        case NABTO_COAP_ERROR_NO_CONNECTION:
            return "NABTO_COAP_ERROR_NO_CONNECTION";
        case NABTO_COAP_ERROR_INVALID_PARAMETER:
            return "NABTO_COAP_ERROR_INVALID_PARAMETER";
        default:
            return "Unknown nabto_coap_error error code";
    }
}
