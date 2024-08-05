#ifndef _NABTO_STREAM_CONGESTION_CONTROL_H_
#define _NABTO_STREAM_CONGESTION_CONTROL_H_

#include "nabto_stream_types.h"

/**
 * Congestion control ensures that we limit the amount of data sent to
 * the network to some amount which the network supports.
 */

struct nabto_stream;
struct nabto_stream_send_segment;

uint32_t nabto_stream_congestion_control_not_sent_segments(struct nabto_stream* stream);

void nabto_stream_update_congestion_control_receive_stats(struct nabto_stream * stream, struct nabto_stream_send_segment* segment, uint32_t timestampEcho);

void nabto_stream_congestion_control_adjust_ssthresh_after_triple_ack(struct nabto_stream* stream, struct nabto_stream_send_segment* seqment);

void nabto_stream_congestion_control_timeout(struct nabto_stream * stream);

void nabto_stream_congestion_control_init(struct nabto_stream* stream);

bool nabto_stream_congestion_control_use_slow_start(struct nabto_stream* stream);

bool nabto_stream_congestion_control_accept_more_data(struct nabto_stream* stream);

/**
 * Called when an ack is handled.
 */
void nabto_stream_congestion_control_handle_ack(struct nabto_stream* stream, struct nabto_stream_send_segment* segment);

/**
 * Called before a data packet is sent to test if it's allowed to be sent.
 */
bool nabto_stream_congestion_control_can_send(struct nabto_stream* stream);

#endif
