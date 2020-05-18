#ifndef _NABTO_STREAM_MEMORY_H_
#define _NABTO_STREAM_MEMORY_H_

#include "nabto_stream_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

struct nabto_stream;
struct nabto_stream_send_segment;
struct nabto_stream_recv_segment;

void nabto_stream_send_segment_available(struct nabto_stream* stream);
void nabto_stream_recv_segment_available(struct nabto_stream* stream);

// helper functions to alloc and free send and receive segments
void nabto_stream_allocate_next_send_segment(struct nabto_stream* stream);
bool nabto_stream_allocate_next_recv_segment(struct nabto_stream* stream);
void nabto_stream_free_send_segment(struct nabto_stream* stream, struct nabto_stream_send_segment* segment);
void nabto_stream_free_recv_segment(struct nabto_stream* stream, struct nabto_stream_recv_segment* segment);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
