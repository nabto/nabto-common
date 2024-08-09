#ifndef _NABTO_STEAM_PACKET_H_
#define _NABTO_STEAM_PACKET_H_

#include "nabto_stream_types.h"
#include "nabto_stream_window.h"

#include <string.h>

#ifdef __cplusplus
extern "C"
{
#endif

struct nabto_stream_header;
struct nabto_stream;

void nabto_stream_handle_packet(struct nabto_stream* stream, const uint8_t* packet, size_t packetSize);

void nabto_stream_parse_syn(struct nabto_stream* stream, const uint8_t* ptr, const uint8_t* end, struct nabto_stream_header* hdr);

void nabto_stream_parse_syn_ack(struct nabto_stream* stream, const uint8_t* ptr, const uint8_t* end, struct nabto_stream_header* hdr);

void nabto_stream_parse_ack(struct nabto_stream* stream, const uint8_t* ptr, const uint8_t* end, struct nabto_stream_header* hdr);

void nabto_stream_parse_data_extension(struct nabto_stream* stream, const uint8_t* ptr, uint16_t length);
void nabto_stream_parse_fin_extension(struct nabto_stream* stream, const uint8_t* ptr, uint16_t length);
void nabto_stream_parse_ack_extension(struct nabto_stream* stream, const uint8_t* ptr, uint16_t length, struct nabto_stream_header* hdr);

// Acknowledge every unacked segment with a sequence number greater than bottom. Starting from ackIterator.
struct nabto_stream_send_segment* nabto_stream_ack_block(struct nabto_stream* stream, struct nabto_stream_send_segment* ackIterator, uint32_t bottom, uint32_t tsEcr);
struct nabto_stream_send_segment* nabto_stream_nack_block(struct nabto_stream* stream, struct nabto_stream_send_segment* ackIterator, uint32_t bottom, uint32_t tsEcr);

// PACKET CREATE FUNCTIONS
size_t nabto_stream_create_packet(struct nabto_stream* stream, uint8_t* buffer, size_t bufferSize, enum nabto_stream_next_event_type eventType);

uint8_t* nabto_stream_write_header(uint8_t* ptr, const uint8_t* end, uint8_t type, uint32_t logicalTimestamp);
size_t nabto_stream_create_syn_packet(struct nabto_stream* stream, uint8_t* buffer, size_t bufferSize);
size_t nabto_stream_create_syn_ack_packet(struct nabto_stream* stream, uint8_t* buffer, size_t bufferSize);
size_t nabto_stream_create_ack_packet(struct nabto_stream* stream, uint8_t* buffer, size_t bufferSize);
uint8_t* nabto_stream_add_ack_extension(struct nabto_stream* stream, uint8_t* ptr, const uint8_t* end);
uint8_t* nabto_stream_add_nonce_response_extension(struct nabto_stream* stream, uint8_t* ptr, const uint8_t* end);
size_t nabto_stream_create_rst_packet(uint8_t* buffer, size_t bufferSize);

void nabto_stream_dump_packet(struct nabto_stream* stream, const uint8_t* buffer, size_t bufferSize, const char* extraDescription);

#ifdef __cplusplus
}
#endif


#endif
