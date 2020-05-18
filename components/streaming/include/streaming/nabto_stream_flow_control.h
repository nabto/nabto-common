#ifndef _NABTO_STREAM_FLOW_CONTROL_
#define _NABTO_STREAM_FLOW_CONTROL_

#include "nabto_stream_types.h"

struct nabto_stream;

/**
 * return true iff the sequence number can be sent.
 */
bool nabto_stream_flow_control_can_send(struct nabto_stream* stream, uint32_t seq);

/**
 * called when the recv window is reduced
 */
void nabto_stream_flow_control_advertised_window_reduced(struct nabto_stream* stream);

#endif
