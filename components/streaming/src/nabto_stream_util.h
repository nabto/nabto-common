#ifndef _NABTO_STREAM_UTIL_H_
#define _NABTO_STREAM_UTIL_H_

#include "nabto_stream_window.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define NABTO_STREAM_MAX(x, y) (((x) > (y)) ? (x) : (y))
#define NABTO_STREAM_MIN(x, y) (((x) < (y)) ? (x) : (y))

void nabto_stream_init_send_segment(struct nabto_stream_send_segment* segment);
void nabto_stream_init_recv_segment(struct nabto_stream_recv_segment* segment);

// modify send lists
void nabto_stream_add_segment_to_send_list_before_elm(struct nabto_stream* stream, struct nabto_stream_send_segment* beforeThis, struct nabto_stream_send_segment* segment);
void nabto_stream_remove_segment_from_send_list(struct nabto_stream* stream, struct nabto_stream_send_segment* segment);

void nabto_stream_add_segment_to_resend_list_before_elm(struct nabto_stream_send_segment* beforeThis, struct nabto_stream_send_segment* segment);
void nabto_stream_remove_segment_from_resend_list(struct nabto_stream_send_segment* segment);

// modify send unacked list
void nabto_stream_add_segment_to_unacked_list_before_elm(struct nabto_stream* stream, struct nabto_stream_send_segment* beforeThis, struct nabto_stream_send_segment* segment);
void nabto_stream_remove_segment_from_unacked_list(struct nabto_stream* stream, struct nabto_stream_send_segment* segment);


// modify recv lists
void nabto_stream_add_segment_to_recv_list_before_elm(struct nabto_stream_recv_segment* beforeThis, struct nabto_stream_recv_segment* segment);
void nabto_stream_remove_segment_from_recv_list(struct nabto_stream_recv_segment* segment);

// wall clock timestamps used to calculate rtt, srtt, rto

nabto_stream_stamp nabto_stream_stamp_infinite();
nabto_stream_stamp nabto_stream_stamp_now();
nabto_stream_stamp nabto_stream_get_stamp(struct nabto_stream* stream);
nabto_stream_stamp nabto_stream_get_future_stamp(struct nabto_stream* stream, uint32_t milliseconds);

bool nabto_stream_stamp_less(nabto_stream_stamp s1, nabto_stream_stamp s2);
nabto_stream_stamp nabto_stream_stamp_less_of(const nabto_stream_stamp s1, const nabto_stream_stamp s2);
bool nabto_stream_is_stamp_passed(struct nabto_stream* stream, nabto_stream_stamp s);

int32_t nabto_stream_stamp_diff_now(struct nabto_stream* stream, nabto_stream_stamp s);

// logical timestamps, used as timestamps in packets.
uint32_t nabto_stream_logical_stamp_get(struct nabto_stream* stream);
bool nabto_stream_logical_stamp_less(uint32_t s1, uint32_t s2);
bool nabto_stream_logical_stamp_less_or_equal(uint32_t s1, uint32_t s2);

bool nabto_stream_sequence_less(uint32_t lhs, uint32_t rhs);
bool nabto_stream_sequence_less_equal(uint32_t lhs, uint32_t rhs);
uint32_t nabto_stream_sequence_max(uint32_t s1, uint32_t s2);

void nabto_stream_module_notify_event(struct nabto_stream* stream, enum nabto_stream_module_event event);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
