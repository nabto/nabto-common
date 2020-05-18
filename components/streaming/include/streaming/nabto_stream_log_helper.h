#ifndef _NABTO_STREAM_LOG_HELPER_H_
#define _NABTO_STREAM_LOG_HELPER_H_

#include "nabto_stream_types.h"
#include "nabto_stream_window.h"

#ifdef __cplusplus
extern "C"
{
#endif

struct nabto_stream;

const char* nabto_stream_stamp_to_string(nabto_stream_stamp s);

const char* nabto_stream_flags_to_string(uint8_t flags);

const char* nabto_stream_application_event_type_to_string(nabto_stream_application_event_type event);

const char* nabto_stream_state_as_string(nabto_stream_state state);

const char* nabto_stream_next_event_type_to_string(enum nabto_stream_next_event_type eventType);

#ifdef __cplusplus
} // extern "C"
#endif


#endif
