#include <streaming/nabto_stream_packet.h>
#include <streaming/nabto_stream_window.h>
#include <streaming/nabto_stream_protocol.h>
#include <streaming/nabto_stream_interface.h>
#include <streaming/nabto_stream_util.h>
#include <streaming/nabto_stream_flow_control.h>
#include <streaming/nabto_stream_log_helper.h>

#include "stddef.h"

static uint8_t* nabto_stream_write_data_to_packet(struct nabto_stream* stream, uint8_t* ptr, const uint8_t* end, size_t* segmentsWritten, uint32_t logicalTimestamp);

static const uint8_t* nabto_stream_read_uint8(const uint8_t* ptr, const uint8_t* end, uint8_t* val)
{
    if (ptr == NULL || (ptr + 1) > end) {
        return NULL;
    }
    *val = ptr[0];
    return ptr + 1;
}

static const uint8_t* nabto_stream_read_uint16(const uint8_t* ptr, const uint8_t* end, uint16_t* val)
{
    if (ptr == NULL || (ptr + 2) > end) {
        return NULL;
    }
    *val =
        ((uint16_t)ptr[0]) << 8 |
        ((uint16_t)ptr[1]);
    return ptr + 2;
}

static const uint8_t* nabto_stream_read_uint32(const uint8_t* ptr, const uint8_t* end, uint32_t* val)
{
    if (ptr == NULL || (ptr + 4) > end) {
        return NULL;
    }
    *val =
        ((uint32_t)ptr[0]) << 24 |
        ((uint32_t)ptr[1]) << 16 |
        ((uint32_t)ptr[2]) << 8 |
        ((uint32_t)ptr[3]);
    return ptr + 4;
}

static const uint8_t* nabto_stream_read_nonce(const uint8_t* ptr, const uint8_t* end, uint8_t* nonce)
{
    if (ptr == NULL || (ptr + NABTO_STREAM_NONCE_SIZE) > end) {
        return NULL;
    }
    memcpy(nonce, ptr, NABTO_STREAM_NONCE_SIZE);
    return ptr + 8;
}

static uint8_t* nabto_stream_write_uint8(uint8_t* ptr, const uint8_t* end, uint8_t val)
{
    if (ptr == NULL || (ptr + 1) > end) {
        return NULL;
    }
    ptr[0] = val;
    return ptr + 1;
}

static uint8_t* nabto_stream_write_uint16(uint8_t* ptr, const uint8_t* end, uint16_t val)
{
    if (ptr == NULL || (ptr + 2) > end) {
        return NULL;
    }
    ptr[0] = (uint8_t)(val >> 8);
    ptr[1] = (uint8_t)val;
    return ptr + 2;
}

static uint8_t* nabto_stream_write_uint32(uint8_t* ptr, const uint8_t* end, uint32_t val)
{
    if (ptr == NULL || (ptr + 4) > end) {
        return NULL;
    }
    ptr[0] = (uint8_t)(val >> 24);
    ptr[1] = (uint8_t)(val >> 16);
    ptr[2] = (uint8_t)(val >> 8);
    ptr[3] = (uint8_t)val;
    return ptr + 4;
}

static uint8_t* nabto_stream_write_nonce(uint8_t* ptr, const uint8_t* end, uint8_t* nonce)
{
    if (ptr == NULL || (ptr + 8) > end) {
        return NULL;
    }
    memcpy(ptr, nonce, 8);
    return ptr + 8;
}

static uint8_t* nabto_stream_encode_buffer(uint8_t* ptr, const uint8_t* end, const uint8_t* buffer, size_t bufferSize)
{
    if (ptr == NULL || (ptr + bufferSize) > end) {
        return NULL;
    }
    memcpy(ptr, buffer, bufferSize);
    return ptr + bufferSize;
}

void nabto_stream_handle_packet(struct nabto_stream* stream, const uint8_t* packet, size_t packetSize)
{
    const uint8_t* end = packet + packetSize;
    const uint8_t* ptr = packet;
    struct nabto_stream_header hdr;

    nabto_stream_dump_packet(stream, packet, packetSize, "Handling Packet");

    ptr = nabto_stream_read_uint8(ptr, end, &hdr.flags);
    ptr = nabto_stream_read_uint32(ptr, end, &hdr.timestampValue);
    if (ptr == NULL) {
        return;
    }

    if (nabto_stream_logical_stamp_less(stream->timestampToEcho, hdr.timestampValue)) {
        stream->timestampToEcho = hdr.timestampValue;
    }

    if (hdr.flags == NABTO_STREAM_FLAG_SYN) {
        nabto_stream_parse_syn(stream, ptr, end, &hdr);
    } else if (hdr.flags == (NABTO_STREAM_FLAG_SYN | NABTO_STREAM_FLAG_ACK)) {
        nabto_stream_parse_syn_ack(stream, ptr, end, &hdr);
    } else if (hdr.flags == NABTO_STREAM_FLAG_ACK) {
        // ack packets can contain stream data.
        nabto_stream_parse_ack(stream, ptr, end, &hdr);
    } else if (hdr.flags == NABTO_STREAM_FLAG_RST) {
        nabto_stream_handle_rst(stream);
    } else {
        // unknown packet
    }
}

void nabto_stream_parse_syn(struct nabto_stream* stream, const uint8_t* ptr, const uint8_t* end, struct nabto_stream_header* hdr)
{
    struct nabto_stream_syn_request req;
    req.contentType = 0;
    req.maxSendSegmentSize = NABTO_STREAM_DEFAULT_MAX_SEND_SEGMENT_SIZE;
    req.maxRecvSegmentSize = NABTO_STREAM_DEFAULT_MAX_RECV_SEGMENT_SIZE;

    bool hasSeq = false;

    do {
        uint16_t type;
        uint16_t length;
        ptr = nabto_stream_read_uint16(ptr, end, &type);
        ptr = nabto_stream_read_uint16(ptr, end, &length);
        if (ptr == NULL || length > (end - ptr)) {
            break;
        }

        if (type == NABTO_STREAM_EXTENSION_CONTENT_TYPE && length >= 4) {
            nabto_stream_read_uint32(ptr, end, &req.contentType);
        } else if (type == NABTO_STREAM_EXTENSION_SEGMENT_SIZES && length >= 4) {
            const uint8_t* extPtr = ptr;
            extPtr = nabto_stream_read_uint16(extPtr, end, &req.maxSendSegmentSize);
            extPtr = nabto_stream_read_uint16(extPtr, end, &req.maxRecvSegmentSize);
        } else if (type == NABTO_STREAM_EXTENSION_SYN) {
            const uint8_t* extPtr = ptr;
            extPtr = nabto_stream_read_uint32(extPtr, end, &req.seq);
            hasSeq = true;
        } else if (type == NABTO_STREAM_EXTENSION_NONCE_CAPABILITY) {
            const uint8_t* extPtr = ptr;
            req.hasNonceCapability = true;
        }

        ptr += length;
    } while (ptr < end);

    if (!hasSeq) {
        NN_LOG_ERROR(stream->module->logger, NABTO_STREAM_LOG_MODULE, "invalid syn packet");
        return;
    }

    nabto_stream_handle_syn(stream, hdr, &req);
}

void nabto_stream_parse_syn_ack(struct nabto_stream* stream, const uint8_t* ptr, const uint8_t* end, struct nabto_stream_header* hdr)
{
    struct nabto_stream_syn_ack_request req;
    req.maxSendSegmentSize = NABTO_STREAM_DEFAULT_MAX_SEND_SEGMENT_SIZE;
    req.maxRecvSegmentSize = NABTO_STREAM_DEFAULT_MAX_RECV_SEGMENT_SIZE;

    bool hasSegmentSizes = false;
    bool hasSeq = false;

    do {
        uint16_t type;
        uint16_t length;
        ptr = nabto_stream_read_uint16(ptr, end, &type);
        ptr = nabto_stream_read_uint16(ptr, end, &length);
        if (ptr == NULL || length > (end - ptr)) {
            break;
        }

        if (type == NABTO_STREAM_EXTENSION_SEGMENT_SIZES && length >= 4) {
            const uint8_t* extPtr = ptr;
            extPtr = nabto_stream_read_uint16(extPtr, end, &req.maxSendSegmentSize);
            extPtr = nabto_stream_read_uint16(extPtr, end, &req.maxRecvSegmentSize);
            hasSegmentSizes = true;
        } else if (type == NABTO_STREAM_EXTENSION_ACK) {
            nabto_stream_parse_ack_extension(stream, ptr, length, hdr);
        } else if (type == NABTO_STREAM_EXTENSION_SYN) {
            const uint8_t* extPtr = ptr;
            extPtr = nabto_stream_read_uint32(extPtr, end, &req.seq);
            hasSeq = true;
        } else if (type == NABTO_STREAM_EXTENSION_NONCE) {
            const uint8_t* extPtr = ptr;
            req.hasNonce = true;
            extPtr = nabto_stream_read_nonce(extPtr, end, req.nonce);
        }
        ptr += length;
    } while (ptr < end);

    if (!hasSegmentSizes || !hasSeq) {
        NN_LOG_ERROR(stream->module->logger, NABTO_STREAM_LOG_MODULE, "invalid syn|ack packet");
        return;
    }

    nabto_stream_handle_syn_ack(stream, hdr, &req);
}

/**
 * loop through all extensions, try to find a nonce_response and validate the nonce.
 */
void nabto_stream_parse_nonce_response(struct nabto_stream* stream, const uint8_t* ptr, const uint8_t* end, struct nabto_stream_header* hdr)
{
    do {
        uint16_t type;
        uint16_t length;
        ptr = nabto_stream_read_uint16(ptr, end, &type);
        ptr = nabto_stream_read_uint16(ptr, end, &length);
        if (ptr == NULL || length > (end - ptr)) {
            break;
        }

        if (type == NABTO_STREAM_EXTENSION_NONCE_RESPONSE) {
            const uint8_t* extPtr = ptr;
            const uint8_t* extEnd = extPtr + length;
            uint8_t nonce[8];
            extPtr = nabto_stream_read_nonce(extPtr, extEnd, nonce);
            if (extPtr != NULL) {
                if (memcmp(nonce, stream->nonce, 8) == 0) {
                    stream->nonceValidated = true;
                }
            }

        }

        ptr += length;
    } while (ptr < end);
}

void nabto_stream_parse_acking(struct nabto_stream* stream, const uint8_t* ptr, const uint8_t* end, struct nabto_stream_header* hdr)
{
    do {
        uint16_t type;
        uint16_t length;
        ptr = nabto_stream_read_uint16(ptr, end, &type);
        ptr = nabto_stream_read_uint16(ptr, end, &length);
        if (ptr == NULL || length > (end - ptr)) {
            break;
        }

        if (type == NABTO_STREAM_EXTENSION_ACK) {
            nabto_stream_parse_ack_extension(stream, ptr, length, hdr);
        }

        ptr += length;
    } while (ptr < end);
}

void nabto_stream_parse_ack_extension(struct nabto_stream* stream, const uint8_t* ptr, uint16_t length, struct nabto_stream_header* hdr)
{
    if (stream->state == ST_SYN_RCVD) {
        nabto_stream_handle_ack_on_syn_ack(stream);
    }

    const uint8_t* end = ptr + length;

    uint32_t maxAcked;
    uint32_t windowSize;
    uint32_t tsEcr;
    uint32_t delay;

    if ((length/8 * 8) != length) {
        NN_LOG_ERROR(stream->module->logger, NABTO_STREAM_LOG_MODULE, "invalid number of ack gaps. %" NN_LOG_PRIu16 " is not 16 + 8*n", length);
    }

    ptr = nabto_stream_read_uint32(ptr, end, &maxAcked);
    ptr = nabto_stream_read_uint32(ptr, end, &windowSize);
    ptr = nabto_stream_read_uint32(ptr, end, &tsEcr);
    ptr = nabto_stream_read_uint32(ptr, end, &delay);
    if (ptr == NULL) {
        return;
    }

    if (nabto_stream_sequence_less_equal(stream->maxAcked, maxAcked)) {

        uint32_t oldAdvertisedWindow = stream->maxAdvertisedWindow;
        stream->maxAdvertisedWindow = maxAcked + windowSize;

        if (nabto_stream_sequence_less(stream->maxAdvertisedWindow, oldAdvertisedWindow)) {
            NN_LOG_TRACE(stream->module->logger, NABTO_STREAM_LOG_MODULE, "reduce maxAdvertisedWindow to: %" NN_LOG_PRIu32, stream->maxAdvertisedWindow);
            nabto_stream_flow_control_advertised_window_reduced(stream);
        }
    }

    // maxAcked is the maximum acked segment. Gaps between maxAcked and the
    // max cumulative sequence number is acked and nacked below. Set ackIterator
    // to first unacked segment which sequence number is less or equal than maxAcked.
    struct nabto_stream_send_segment* ackIterator = stream->unacked->nextUnacked;

    // list It is cheaper to scan for the segment just above the maxAcked than
    // to iterate back from the top of the list of unacked segments.
    while (ackIterator != stream->unacked &&
           nabto_stream_sequence_less_equal(ackIterator->seq, maxAcked))
    {
        ackIterator = ackIterator->nextUnacked;
    }

    // now ACK iterator points just above the highest unacked segment below maxAcked.
    ackIterator = ackIterator->prevUnacked;

    // The gaps structure defines segments which has been acked and segments
    // which nas not been acked. No gaps will be present if all the data has
    // been acked upto maxAcked.
    //
    // If the packet did not have room for all the gaps, then the first nack gap
    // will contains segments already acked. This means an ACK is definitively
    // an ack but an NACK can be on data already acked. An NACK on already acked
    // data does not mean that this data has to be retransmitted.


    // to be adjusted below.
    uint32_t maxCumulativeAcked = maxAcked;

    uint16_t gaps = (length - 16)/8;
    if (gaps > 0) {
        uint16_t i;
        for (i = 0; i < gaps; i++) {
            uint32_t ackGap = 0;
            uint32_t nackGap = 0;
            ptr = nabto_stream_read_uint32(ptr, end, &ackGap);
            ptr = nabto_stream_read_uint32(ptr, end, &nackGap);

            uint32_t ackTop = maxCumulativeAcked;
            uint32_t nackTop = ackTop - ackGap;
            uint32_t nackBottom = nackTop - nackGap;
            // Mark all segments between ackTop down to nackTop as acked. The sequence number ackTop is not included.
            ackIterator = nabto_stream_ack_block(stream, ackIterator, nackTop, tsEcr);
            // Mark all segments from nackTop down to nackBottom as not acked. The sequence number nackTop is not included.
            ackIterator = nabto_stream_nack_block(stream, ackIterator, nackBottom, tsEcr);

            maxCumulativeAcked = nackBottom;
        }
    }

    // everything from current ackIterator downto stream->unacked is acked since
    // everything not unacked has been given in the nack blocks.

    while(ackIterator != stream->unacked) {
        ackIterator = nabto_stream_handle_ack_iterator(stream, ackIterator, tsEcr);
    }
}

struct nabto_stream_send_segment* nabto_stream_ack_block(struct nabto_stream* stream, struct nabto_stream_send_segment* ackIterator, uint32_t bottom, uint32_t tsEcr)
{
    // iterate as long as there are more unacked segments an as long as the
    // bottom sequence number is > the current unacked segments sequence
    // number.
    while(ackIterator != stream->unacked && nabto_stream_sequence_less(bottom, ackIterator->seq)) {
        ackIterator = nabto_stream_handle_ack_iterator(stream, ackIterator, tsEcr);
    }
    return ackIterator;
}

struct nabto_stream_send_segment* nabto_stream_nack_block(struct nabto_stream* stream, struct nabto_stream_send_segment* ackIterator, uint32_t bottom, uint32_t tsEcr)
{
    while(ackIterator != stream->unacked && nabto_stream_sequence_less(bottom, ackIterator->seq)) {
        ackIterator = nabto_stream_handle_nack_iterator(stream, ackIterator, tsEcr);
    }
    return ackIterator;
}


void nabto_stream_parse_ack(struct nabto_stream* stream, const uint8_t* ptr, const uint8_t* end, struct nabto_stream_header* hdr)
{
    if (!stream->disableReplayProtection) {
        if (!stream->nonceValidated) {
            nabto_stream_parse_nonce_response(stream, ptr, end, hdr);
        }
    }

    if (!stream->disableReplayProtection) {
        if (!stream->nonceValidated) {
            // the first ack we receive as a responder needs to contain a valid nonce. If replay protection is enabled.
            return;
        }
    }

    nabto_stream_parse_acking(stream, ptr, end, hdr);

    do {
        uint16_t type;
        uint16_t length;
        ptr = nabto_stream_read_uint16(ptr, end, &type);
        ptr = nabto_stream_read_uint16(ptr, end, &length);
        if (ptr == NULL || length > (end - ptr)) {
            break;
        }

        if (type == NABTO_STREAM_EXTENSION_DATA) {
            nabto_stream_parse_data_extension(stream, ptr, length);
        } else if (type == NABTO_STREAM_EXTENSION_FIN) {
            nabto_stream_parse_fin_extension(stream, ptr, length);
        } else if (type == NABTO_STREAM_EXTENSION_NONCE_RESPONSE) {
            // TODO handle nonce response.
        } else {
            // unknown extension
        }

        ptr += length;
    } while (ptr < end);
}

void nabto_stream_parse_data_extension(struct nabto_stream* stream, const uint8_t* ptr, uint16_t length)
{
    const uint8_t* end = ptr + length;
    uint32_t seq;
    ptr = nabto_stream_read_uint32(ptr, end, &seq);
    if (ptr == NULL) {
        return;
    }
    nabto_stream_handle_data(stream, seq, ptr, length - 4);
}

void nabto_stream_parse_fin_extension(struct nabto_stream* stream, const uint8_t* ptr, uint16_t length)
{
    const uint8_t* end = ptr + length;
    uint32_t seq;
    ptr = nabto_stream_read_uint32(ptr, end, &seq);
    if (ptr == NULL) {
        return;
    }
    nabto_stream_handle_fin(stream, seq);
}

/***************************************
 * CREATE PACKETS                      *
 ***************************************/

/**
 * return > 0 if packet has been created and put into buffer
 */
size_t nabto_stream_create_packet(struct nabto_stream* stream, uint8_t* buffer, size_t bufferSize, enum nabto_stream_next_event_type eventType)
{
    size_t packetSize = 0;
    switch (eventType) {
        case ET_SYN:
            packetSize = nabto_stream_create_syn_packet(stream, buffer, bufferSize);
            break;
        case ET_SYN_ACK:
            packetSize = nabto_stream_create_syn_ack_packet(stream, buffer, bufferSize);
            break;
        case ET_ACK:
            packetSize = nabto_stream_create_ack_packet(stream, buffer, bufferSize);
            break;
        case ET_DATA:
            packetSize = nabto_stream_create_ack_packet(stream, buffer, bufferSize);
            break;
        case ET_RST:
            packetSize = nabto_stream_create_rst_packet(buffer, bufferSize);
            break;
        default:
            packetSize = 0;
    }
    nabto_stream_dump_packet(stream, buffer, packetSize, "Created Packet");
    return packetSize;
}

uint8_t* nabto_stream_write_header(uint8_t* ptr, const uint8_t* end, uint8_t type, uint32_t logicalTimestamp)
{
    ptr = nabto_stream_write_uint8(ptr, end, type);
    ptr = nabto_stream_write_uint32(ptr, end, logicalTimestamp);
    return ptr;
}

size_t nabto_stream_create_syn_packet(struct nabto_stream* stream, uint8_t* buffer, size_t bufferSize)
{
    const uint8_t* end = buffer + bufferSize;
    uint8_t* ptr = buffer;
    uint32_t logicalTimestamp = nabto_stream_logical_stamp_get(stream);

    ptr = nabto_stream_write_header(ptr, end, NABTO_STREAM_FLAG_SYN, logicalTimestamp);

    // syn extension
    ptr = nabto_stream_write_uint16(ptr, end, NABTO_STREAM_EXTENSION_SYN);
    ptr = nabto_stream_write_uint16(ptr, end, 4);
    ptr = nabto_stream_write_uint32(ptr, end, stream->startSequenceNumber);

    // if we have a max send/receive segment size write it here
    ptr = nabto_stream_write_uint16(ptr, end, NABTO_STREAM_EXTENSION_SEGMENT_SIZES);
    ptr = nabto_stream_write_uint16(ptr, end, 4);
    ptr = nabto_stream_write_uint16(ptr, end, stream->maxSendSegmentSize);
    ptr = nabto_stream_write_uint16(ptr, end, stream->maxRecvSegmentSize);

    // write content type of the stream
    ptr = nabto_stream_write_uint16(ptr, end, NABTO_STREAM_EXTENSION_CONTENT_TYPE);
    ptr = nabto_stream_write_uint16(ptr, end, 4);
    ptr = nabto_stream_write_uint32(ptr, end, stream->contentType);

    // write nonce
    if (!stream->disableReplayProtection) {
        ptr = nabto_stream_write_uint16(ptr, end, NABTO_STREAM_EXTENSION_NONCE_CAPABILITY);
        ptr = nabto_stream_write_uint16(ptr, end, 0);
    }

    if (ptr == NULL) {
        return 0;
    }

    stream->timeoutStamp = nabto_stream_get_future_stamp(stream, NABTO_STREAM_DEFAULT_TIMEOUT);
    stream->retransCount++;

    ptrdiff_t s = ptr - buffer;
    return (size_t)s;
}

size_t nabto_stream_create_syn_ack_packet(struct nabto_stream* stream, uint8_t* buffer, size_t bufferSize)
{
    const uint8_t* end = buffer + bufferSize;
    uint8_t* ptr = buffer;
    uint32_t logicalTimestamp = nabto_stream_logical_stamp_get(stream);

    ptr = nabto_stream_write_header(ptr, end, NABTO_STREAM_FLAG_SYN | NABTO_STREAM_FLAG_ACK, logicalTimestamp);

    // add ack data.
    ptr = nabto_stream_add_ack_extension(stream, ptr, end);

    // syn extension
    ptr = nabto_stream_write_uint16(ptr, end, NABTO_STREAM_EXTENSION_SYN);
    ptr = nabto_stream_write_uint16(ptr, end, 4);
    ptr = nabto_stream_write_uint32(ptr, end, stream->startSequenceNumber);

    // we have choosen the max segment sizes based on the input from the syn packet.
    ptr = nabto_stream_write_uint16(ptr, end, NABTO_STREAM_EXTENSION_SEGMENT_SIZES);
    ptr = nabto_stream_write_uint16(ptr, end, 4);
    ptr = nabto_stream_write_uint16(ptr, end, stream->maxSendSegmentSize);
    ptr = nabto_stream_write_uint16(ptr, end, stream->maxRecvSegmentSize);

    if (!stream->disableReplayProtection) {
        ptr = nabto_stream_write_uint16(ptr, end, NABTO_STREAM_EXTENSION_NONCE);
        ptr = nabto_stream_write_uint16(ptr, end, 8);
        ptr = nabto_stream_write_nonce(ptr, end, stream->nonce);
    }

    if (ptr == NULL) {
        return 0;
    }

    ptrdiff_t s = ptr - buffer;
    return (size_t)s;
}

// write fin or data extension
uint8_t* nabto_stream_write_segment(struct nabto_stream* stream, uint8_t* ptr, const uint8_t* end, struct nabto_stream_send_segment* current, uint32_t logicalTimestamp)
{
    if (&stream->finSegment == current) {
        ptr = nabto_stream_write_uint16(ptr, end, NABTO_STREAM_EXTENSION_FIN);
        ptr = nabto_stream_write_uint16(ptr, end, 4);
        ptr = nabto_stream_write_uint32(ptr, end, current->seq);
    } else {
        ptr = nabto_stream_write_uint16(ptr, end, NABTO_STREAM_EXTENSION_DATA);
        ptr = nabto_stream_write_uint16(ptr, end, current->used + 4);
        ptr = nabto_stream_write_uint32(ptr, end, current->seq);
        ptr = nabto_stream_encode_buffer(ptr, end, current->buf, current->used);
    }

    current->sentStamp = nabto_stream_get_stamp(stream);
    current->logicalSentStamp = logicalTimestamp;
    current->ackedAfter = 0;

    return ptr;
}

size_t nabto_stream_create_ack_packet(struct nabto_stream* stream, uint8_t* buffer, size_t bufferSize)
{
    const uint8_t* end = buffer + bufferSize;
    uint8_t* ptr = buffer;
    uint32_t logicalTimestamp = nabto_stream_logical_stamp_get(stream);

    ptr = nabto_stream_write_header(ptr, end, NABTO_STREAM_FLAG_ACK, logicalTimestamp);

    if (ptr == NULL) {
        return 0;
    }

    if (stream->sendNonce) {
        ptr = nabto_stream_add_nonce_response_extension(stream, ptr, end);
        stream->sendNonce = false;
    }

    // add ack data.
    ptr = nabto_stream_add_ack_extension(stream, ptr, end);

    size_t segmentsWritten = 0;
    ptr = nabto_stream_write_data_to_packet(stream, ptr, end, &segmentsWritten, logicalTimestamp);

    stream->cCtrl.cwnd -= segmentsWritten;
    /* if (stream->cCtrl.cwnd < 0) { */
    /*     stream->cCtrl.cwnd = 0; */
    /* } */

    ptrdiff_t s = ptr - buffer;
    return (size_t)s;
}

uint8_t* nabto_stream_write_data_to_packet(struct nabto_stream* stream, uint8_t* ptr, const uint8_t* end, size_t* segmentsWritten, uint32_t logicalTimestamp)
{
    *segmentsWritten = 0;
    // add resending data
    while (stream->resendList->nextResend != stream->resendList &&
           nabto_stream_flow_control_can_send(stream, stream->resendList->nextResend->seq))
    {
        ptrdiff_t roomLeft = end - ptr;

        struct nabto_stream_send_segment* current = stream->resendList->nextResend;
        if (roomLeft < current->used + NABTO_STREAM_DATA_OVERHEAD) {
            // packet is full return.
            return ptr;
        }
        ptr = nabto_stream_write_segment(stream, ptr, end, current, logicalTimestamp);
        *segmentsWritten += 1;

        // remove segment from resend list.
        nabto_stream_remove_segment_from_resend_list(current);
    }

    while (stream->sendList->nextSend != stream->sendList &&
           nabto_stream_flow_control_can_send(stream, stream->sendList->nextSend->seq))
    {
        ptrdiff_t roomLeft = end - ptr;

        struct nabto_stream_send_segment* current = stream->sendList->nextSend;
        if (roomLeft < current->used + NABTO_STREAM_DATA_OVERHEAD) {
            // packet is full return.
            return ptr;
        }
        ptr = nabto_stream_write_segment(stream, ptr, end, current, logicalTimestamp);
        *segmentsWritten += 1;
        stream->cCtrl.flightSize += 1;
        NN_LOG_TRACE(stream->module->logger, NABTO_STREAM_LOG_MODULE, "new fligtSize: %d", stream->cCtrl.flightSize);

        // remove segment from send list.
        nabto_stream_remove_segment_from_send_list(stream, current);
        nabto_stream_add_segment_to_unacked_list_before_elm(stream, stream->unacked, current);
    }
    return ptr;
}

uint8_t* nabto_stream_add_nonce_response_extension(struct nabto_stream* stream, uint8_t* ptr, const uint8_t* end)
{
    ptr = nabto_stream_write_uint16(ptr, end, NABTO_STREAM_EXTENSION_NONCE_RESPONSE);
    ptr = nabto_stream_write_uint16(ptr, end, 8);
    ptr = nabto_stream_write_nonce(ptr, end, stream->nonce);
}

struct gapBlock {
    uint32_t ackGap;
    uint32_t nackGap;
};

uint8_t* nabto_stream_add_ack_extension(struct nabto_stream* stream, uint8_t* ptr, const uint8_t* end)
{
    ptr = nabto_stream_write_uint16(ptr, end, NABTO_STREAM_EXTENSION_ACK);
    uint8_t* ackLength = ptr;
    ptr += 2;
    uint8_t* extBegin = ptr;

    ptr = nabto_stream_write_uint32(ptr, end, stream->recvMax);
    ptr = nabto_stream_write_uint32(ptr, end, nabto_stream_get_recv_window_size(stream));
    ptr = nabto_stream_write_uint32(ptr, end, stream->timestampToEcho);

    uint32_t delay = 0;
    ptr = nabto_stream_write_uint32(ptr, end, delay);

    uint16_t gapBlocksCount = 0;

    struct gapBlock gapBlocks[MAX_ACK_GAP_BLOCKS];
    memset(gapBlocks, 0, sizeof(gapBlocks));

    size_t maxGapBlocks = MAX_ACK_GAP_BLOCKS;
    if ( (size_t)((end - ptr)/8) < maxGapBlocks) {
        maxGapBlocks = (size_t)((end - ptr)/8);
    }

    // assert(maxGapBlocks > 2);

    if (stream->recvMax != stream->recvTop) {
        // there is holes in the recv window. fill in ack and nack gap data.

        // iterator is the last segment with state RB_DATA in the window.
        struct nabto_stream_recv_segment* iterator = nabto_stream_recv_max_segment(stream);

        while (iterator != stream->recvWindow) {
            if (gapBlocksCount >= maxGapBlocks) {
                // consolidate block 1 into block 0 and move all the
                // other blocks one up. This is important such that we
                // both acks the newest received data and the oldest
                // data in the window.
                gapBlocks[0].nackGap += gapBlocks[1].ackGap;
                gapBlocks[0].nackGap += gapBlocks[1].nackGap;
                memmove(&gapBlocks[1], &gapBlocks[2], sizeof(struct gapBlock)*(MAX_ACK_GAP_BLOCKS-2));
                gapBlocksCount -= 1;
            }

            uint32_t ackGap = 0;
            uint32_t nackGap = 0;

            // iterate to missing segment
            while (iterator->state == RB_DATA && iterator != stream->recvWindow) {
                iterator = iterator->prev;
                ackGap++;
            }

            // iterate to acked segment or recvTop
            while (iterator->state == RB_IDLE && iterator != stream->recvWindow) {
                iterator = iterator->prev;
                nackGap++;
            }

            gapBlocks[gapBlocksCount].ackGap = ackGap;
            gapBlocks[gapBlocksCount].nackGap = nackGap;

            gapBlocksCount++;
        }

        for (uint16_t i = 0; i < gapBlocksCount; i++) {
            ptr = nabto_stream_write_uint32(ptr, end, gapBlocks[i].ackGap);
            ptr = nabto_stream_write_uint32(ptr, end, gapBlocks[i].nackGap);
        }
    }
    // write length
    uint16_t length = (uint16_t)(ptr - extBegin);
    nabto_stream_write_uint16(ackLength, ackLength+2, length);

    return ptr;
}

size_t nabto_stream_create_rst_packet(uint8_t* buffer, size_t bufferSize)
{
    const uint8_t* end = buffer + bufferSize;
    uint8_t* ptr = buffer;
    ptr = nabto_stream_write_header(ptr, end, NABTO_STREAM_FLAG_RST, 0);
    ptrdiff_t s = ptr - buffer;
    return (size_t)s;
}

/**
 * helper function to dump packets.
 */
void nabto_stream_dump_packet(struct nabto_stream* stream, const uint8_t* buffer, size_t bufferSize, const char* extraDescription)
{
    const uint8_t* ptr = buffer;
    const uint8_t* end = buffer + bufferSize;

    uint8_t flags = 0;
    uint32_t tsVal;
    ptr = nabto_stream_read_uint8(ptr, end, &flags);
    ptr = nabto_stream_read_uint32(ptr, end, &tsVal);

    if (ptr == NULL) {
        NN_LOG_TRACE(stream->module->logger, NABTO_STREAM_LOG_MODULE, "Invalid packet, packetSize %" NN_LOG_PRIsize, bufferSize);
        return;
    }

    NN_LOG_TRACE(stream->module->logger, NABTO_STREAM_LOG_MODULE, "%s, Packet type %s, size %" NN_LOG_PRIsize ", timestamp: %" NN_LOG_PRIu32,
                           extraDescription,
                           nabto_stream_flags_to_string(flags),
                           bufferSize, tsVal);


    while (ptr < end) {
        uint16_t extensionType = 0;
        uint16_t length = 0;
        ptr = nabto_stream_read_uint16(ptr, end, &extensionType);
        ptr = nabto_stream_read_uint16(ptr, end, &length);
        if (ptr == NULL) {
            NN_LOG_TRACE(stream->module->logger, NABTO_STREAM_LOG_MODULE, "  Invalid extension formatting in packet");
            return;
        }
        if (ptr + length > end) {
            NN_LOG_TRACE(stream->module->logger, NABTO_STREAM_LOG_MODULE, "  Extension is larger than the packet");
            return;
        }

        const uint8_t* extPtr = ptr;
        const uint8_t* extEnd = extPtr + length;

        if (extensionType == NABTO_STREAM_EXTENSION_ACK) {
            uint32_t maxAcked = 0;
            uint32_t windowSize = 0;
            uint32_t tsEcr = 0;
            uint32_t delay = 0;
            extPtr = nabto_stream_read_uint32(extPtr, extEnd, &maxAcked);
            extPtr = nabto_stream_read_uint32(extPtr, extEnd, &windowSize);
            extPtr = nabto_stream_read_uint32(extPtr, extEnd, &tsEcr);
            extPtr = nabto_stream_read_uint32(extPtr, extEnd, &delay);
            if (extPtr == NULL) {
                NN_LOG_TRACE(stream->module->logger, NABTO_STREAM_LOG_MODULE, "  Bad format of ACK packet");
                return;
            }
            size_t rest = extEnd - extPtr;
            size_t gaps = rest/8;
            NN_LOG_TRACE(stream->module->logger, NABTO_STREAM_LOG_MODULE, "  Ack Extension. MaxAcked: %" NN_LOG_PRIu32 ", windowSize: %" NN_LOG_PRIu32 ", timestampEcho %" NN_LOG_PRIu32 ", delay: %" NN_LOG_PRIu32 ", sack pairs: %" NN_LOG_PRIsize,
                                   maxAcked, windowSize, tsEcr, delay, gaps);
            for (size_t i = 0; i < gaps; i++) {
                uint32_t ackGap = 0;
                uint32_t nackGap = 0;
                extPtr = nabto_stream_read_uint32(extPtr, extEnd, &ackGap);
                extPtr = nabto_stream_read_uint32(extPtr, extEnd, &nackGap);
                NN_LOG_TRACE(stream->module->logger, NABTO_STREAM_LOG_MODULE, "    Sack ackGap: %" NN_LOG_PRIu32 ", nackGap: %" NN_LOG_PRIu32, ackGap, nackGap);
            }
        } else if (extensionType == NABTO_STREAM_EXTENSION_DATA) {
            uint32_t seq = 0;
            extPtr = nabto_stream_read_uint32(extPtr, extEnd, &seq);
            NN_LOG_TRACE(stream->module->logger, NABTO_STREAM_LOG_MODULE, "  Data extension. Sequence number: %" NN_LOG_PRIu32 ", data length: %" NN_LOG_PRIu16, seq, length - 4);
        } else if (extensionType == NABTO_STREAM_EXTENSION_FIN) {
            uint32_t finSeq = 0;
            extPtr = nabto_stream_read_uint32(extPtr, extEnd, &finSeq);
            NN_LOG_TRACE(stream->module->logger, NABTO_STREAM_LOG_MODULE, "  Fin extension. fin sequence: %" NN_LOG_PRIu32, finSeq);
        } else if (extensionType == NABTO_STREAM_EXTENSION_SEGMENT_SIZES) {
            uint16_t maxSendSegmentSize = 0;
            uint16_t maxRecvSegmentSize = 0;
            extPtr = nabto_stream_read_uint16(extPtr, extEnd, &maxSendSegmentSize);
            extPtr = nabto_stream_read_uint16(extPtr, extEnd, &maxRecvSegmentSize);
            NN_LOG_TRACE(stream->module->logger, NABTO_STREAM_LOG_MODULE, "  Segment Sizes extension. MaxSendSegmentSize: %" NN_LOG_PRIu16 " macRecvSegmentSize: %" NN_LOG_PRIu16, maxSendSegmentSize, maxRecvSegmentSize);
        } else if (extensionType == NABTO_STREAM_EXTENSION_CONTENT_TYPE) {
            uint32_t contentType = 0;
            extPtr = nabto_stream_read_uint32(extPtr, extEnd, &contentType);
            NN_LOG_TRACE(stream->module->logger, NABTO_STREAM_LOG_MODULE, "  Content Type extension. ContentType: %" NN_LOG_PRIu32, contentType);
        } else if (extensionType == NABTO_STREAM_EXTENSION_EXTRA_DATA) {
            NN_LOG_TRACE(stream->module->logger, NABTO_STREAM_LOG_MODULE, "  Extra Data extension dataLength: %" NN_LOG_PRIu16, length);
        } else if (extensionType == NABTO_STREAM_EXTENSION_SYN) {
            uint32_t synSeq = 0;
            extPtr = nabto_stream_read_uint32(extPtr, extEnd, &synSeq);
            NN_LOG_TRACE(stream->module->logger, NABTO_STREAM_LOG_MODULE, "  Syn sequence number: %" NN_LOG_PRIu32, synSeq);
        } else if (extensionType == NABTO_STREAM_EXTENSION_NONCE_CAPABILITY) {
            NN_LOG_TRACE(stream->module->logger, NABTO_STREAM_LOG_MODULE, "  Nonce Capability extension");
        } else if (extensionType == NABTO_STREAM_EXTENSION_NONCE) {
            NN_LOG_TRACE(stream->module->logger, NABTO_STREAM_LOG_MODULE, "  Nonce extension");
        } else if (extensionType == NABTO_STREAM_EXTENSION_NONCE_RESPONSE) {
            NN_LOG_TRACE(stream->module->logger, NABTO_STREAM_LOG_MODULE, "  Nonce Response extension");
        } else {
            NN_LOG_TRACE(stream->module->logger, NABTO_STREAM_LOG_MODULE, "  Unknown extension type: %" NN_LOG_PRIu16 ", length: %" NN_LOG_PRIu16, extensionType, length);
        }

        // iterate to next extension
        ptr = ptr + length;
    }
}
