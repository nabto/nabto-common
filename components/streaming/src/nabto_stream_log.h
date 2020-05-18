#ifndef _NABTO_STREAM_LOG_H_
#define _NABTO_STREAM_LOG_H_

#include <stdarg.h>
#include <inttypes.h>

#ifdef __cplusplus
extern "C"
{
#endif

enum nabto_stream_log_level {
    NABTO_STREAM_LOG_LEVEL_INFO,
    NABTO_STREAM_LOG_LEVEL_TRACE,
    NABTO_STREAM_LOG_LEVEL_DEBUG,
    NABTO_STREAM_LOG_LEVEL_ERROR
};

struct nabto_stream;

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#define HAS_VARIADIC_MACROS
#endif

#ifndef HAS_VARIADIC_MACROS
void nabto_stream_log_trace_adapter(struct nabto_stream* stream, const char* fmt, ...);
void nabto_stream_log_info_adapter(struct nabto_stream* stream, const char* fmt, ...);
void nabto_stream_log_debug_adapter(struct nabto_stream* stream, const char* fmt, ...);
void nabto_stream_log_error_adapter(struct nabto_stream* stream, const char* fmt, ...);
#else
void nabto_stream_log_adapter(struct nabto_stream* stream, const char* file, int line, enum nabto_stream_log_level level, const char* fmt, ...);
#endif

#define VA_ARGS(...) , ##__VA_ARGS__

#if defined(HAS_VARIADIC_MACROS)

#define NABTO_STREAM_LOG_TRACE(stream, fmt, ...) do { nabto_stream_log_adapter(stream, __FILE__, __LINE__, NABTO_STREAM_LOG_LEVEL_TRACE, fmt VA_ARGS(__VA_ARGS__)); } while (0);
#define NABTO_STREAM_LOG_INFO(stream, fmt, ...) do { nabto_stream_log_adapter(stream, __FILE__, __LINE__, NABTO_STREAM_LOG_LEVEL_INFO, fmt VA_ARGS(__VA_ARGS__)); } while (0);
#define NABTO_STREAM_LOG_DEBUG(stream, fmt, ...) do { nabto_stream_log_adapter(stream, __FILE__, __LINE__, NABTO_STREAM_LOG_LEVEL_DEBUG, fmt VA_ARGS(__VA_ARGS__)); } while (0);
#define NABTO_STREAM_LOG_ERROR(stream, fmt, ...) do { nabto_stream_log_adapter(stream, __FILE__, __LINE__, NABTO_STREAM_LOG_LEVEL_ERROR, fmt VA_ARGS(__VA_ARGS__)); } while (0);

#else

#define NABTO_STREAM_LOG_TRACE nabto_stream_log_trace_adapter
#define NABTO_STREAM_LOG_INFO  nabto_stream_log_info_adapter
#define NABTO_STREAM_LOG_DEBUG nabto_stream_log_debug_adapter
#define NABTO_STREAM_LOG_ERROR nabto_stream_log_error_adapter

#endif

#define NABTO_STREAM_PRIu8  PRIu8
#define NABTO_STREAM_PRIu16 PRIu16
#define NABTO_STREAM_PRIu32 PRIu32
#define NABTO_STREAM_PRIsize "zu"




#ifdef __cplusplus
} // extern "C"
#endif


#endif
