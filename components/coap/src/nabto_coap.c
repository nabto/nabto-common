#include <coap/nabto_coap.h>

#include <stddef.h>

nabto_coap_code nabto_coap_uint16_to_code(uint16_t code)
{
    uint16_t class = code/100;
    code = code - ( class*100 );
    return (nabto_coap_code)(NABTO_COAP_CODE(class, code));
}


struct nabto_coap_option_iterator* nabto_coap_get_next_option(struct nabto_coap_option_iterator* iterator)
{
    if (iterator->buffer == NULL || iterator->buffer + 1 > iterator->bufferEnd) {
        return NULL;
    }

    const uint8_t* ptr = iterator->buffer;
    const uint8_t* end = iterator->bufferEnd;

    uint32_t length = ((*ptr) & 0x0F);
    uint32_t delta = (*ptr) >> 4;

    ptr += 1;

    if (delta == 13) {
        // one extra byte;
        if (end < ptr || end - ptr < 1) {
            return NULL;
        }
        delta = ((uint32_t)(*ptr)) + 13;
        ptr += 1;
    } else if (delta == 14) {
        // two extra bytes
        if (end < ptr || end - ptr < 2) {
            return NULL;
        }
        // 269 = 255 + 14
        delta = (((uint32_t)ptr[0]) << 8) + ((uint32_t)ptr[1]) + 269;
        ptr += 2;
    } else if (delta == 15) {
        return NULL;
    }

    if (length == 13) {
        // one extra bytes
        if (end < ptr || end - ptr < 1) {
            return NULL;
        }
        length = ((uint32_t)*ptr) + 13;
        ptr += 1;
    } else if (length == 14) {
        // two extra bytes
        if (end < ptr || end - ptr < 2) {
            return NULL;
        }
        length = (((uint32_t)ptr[0]) << 8) + ((uint32_t)ptr[1]) + 269;
        ptr += 2;
    } else if (length == 15) {
        // error
        return NULL;
    }

    if (length > (end - ptr)) {
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



    while (true) {
        if (end < ptr) {
            return false;
        }
        if (end - ptr == 0 || *ptr == 0xFF) {
            break;
        }

        uint32_t length = ((*ptr) & 0x0F);
        uint16_t delta = (*ptr) >> 4;

        ptr += 1;

        if (delta == 13) {
            // one extra byte;
            ptr += 1;
        } else if (delta == 14) {
            // two extra bytes
            ptr += 2;
        } else if (delta == 15) {
            return false;
        }

        if (length == 13) {
            // one extra bytes
            if (end < ptr || end - ptr < 1) {
                return false;
            }
            length = ((uint32_t)*ptr) + 13;
            ptr += 1;
        } else if (length == 14) {
            // two extra bytes
            if (end < ptr || end - ptr < 2) {
                return false;
            }
            length = (((uint32_t)ptr[0]) << 8) + ((uint32_t)ptr[1]) + 269;
            ptr += 2;
        } else if (length == 15) {
            // error
            return false;
        }
        // skip payload length
        if (end < ptr || end - ptr < length) {
            return false;
        }
        ptr += length;
    }

    msg->optionsLength = ptr - msg->options;

    struct nabto_coap_option_iterator iteratorData;
    struct nabto_coap_option_iterator* iterator = &iteratorData;

    nabto_coap_option_iterator_init(iterator, msg->options, msg->options + msg->optionsLength);
    iterator = nabto_coap_get_next_option(iterator);
    while (iterator != NULL) {
        uint32_t value;
        if (iterator->option == NABTO_COAP_OPTION_CONTENT_FORMAT) {
            if (!nabto_coap_parse_variable_int(iterator->optionDataBegin, iterator->optionDataEnd, 2, &value)) {
                return false;
            } else {
                msg->hasContentFormat = true;
                msg->contentFormat = (uint16_t)value;
            }
        } else if (iterator->option == NABTO_COAP_OPTION_BLOCK1) {
            if (!nabto_coap_parse_variable_int(iterator->optionDataBegin, iterator->optionDataEnd, 3, &value)) {
                return false;
            } else {
                msg->hasBlock1 = true;
                msg->block1 = value;
            }
        } else if (iterator->option == NABTO_COAP_OPTION_BLOCK2) {
            if (!nabto_coap_parse_variable_int(iterator->optionDataBegin, iterator->optionDataEnd, 3, &value)) {
                return false;
            } else {
                msg->hasBlock2 = true;
                msg->block2 = value;
            }
        }
        iterator = nabto_coap_get_next_option(iterator);
    }


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

    // never here.
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

uint8_t* nabto_coap_encode_option(uint16_t optionDelta, const uint8_t* optionData, size_t optionDataLength, uint8_t* buffer, uint8_t* bufferEnd)
{
    if (buffer == NULL ||
        (bufferEnd - buffer) < (ptrdiff_t)(5 + optionDataLength))
    {
        return NULL;
    }
    uint8_t* ptr = buffer + 1;
    uint8_t firstByte = 0;
    if (optionDelta < 13) {
        firstByte |= (uint8_t)(optionDelta << 4);
    } else if (optionDelta < (256 + 13) ) {
        firstByte |= (13 << 4);
        optionDelta -= 13;
        *ptr = (uint8_t)(optionDelta);
        ptr++;
    } else {
        firstByte |= (14 << 4);
        optionDelta -= (256 + 13);
        *ptr = (uint8_t)(optionDelta >> 8);
        ptr++;
        *ptr = (uint8_t)(optionDelta);
        ptr++;
    }

    if (optionDataLength < 13) {
        firstByte |= optionDataLength;
    } else if (optionDataLength < (256 + 13)) {
        firstByte |= 13;
        size_t adjustedLength = optionDataLength - 13;
        *ptr = (uint8_t)adjustedLength;
        ptr++;
    } else {
        firstByte |= 14;
        size_t adjustedLength = optionDataLength - (256 + 13);
        *ptr = (uint8_t)(adjustedLength >> 8);
        ptr++;
        *ptr = (uint8_t)(adjustedLength);
        ptr++;
    }

    *buffer = firstByte;

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
    return (((int32_t)s1 - (int32_t)s2) < 0);
}

bool nabto_coap_is_stamp_less_equal(uint32_t s1, uint32_t s2)
{
    int32_t diff = ((int32_t)s1) - ((int32_t)s2);
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
    return ((int32_t)s1) - ((int32_t)s2);
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
