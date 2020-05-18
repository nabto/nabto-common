/*
 * Copyright (C) Nabto - All Rights Reserved.
 */
#ifndef _NABTO_STUN_LOG_H_
#define _NABTO_STUN_LOG_H_

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

enum nabto_stun_log_level {
    NABTO_STUN_LOG_LEVEL_INFO,
    NABTO_STUN_LOG_LEVEL_TRACE,
    NABTO_STUN_LOG_LEVEL_DEBUG,
    NABTO_STUN_LOG_LEVEL_ERROR
};

struct nabto_stun;

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#define HAS_VARIADIC_MACROS
#endif

#ifndef HAS_VARIADIC_MACROS
void nabto_stun_log_trace_adapter(struct nabto_stun* stun, const char* fmt, ...);
void nabto_stun_log_info_adapter(struct nabto_stun* stun, const char* fmt, ...);
void nabto_stun_log_debug_adapter(struct nabto_stun* stun, const char* fmt, ...);
void nabto_stun_log_error_adapter(struct nabto_stun* stun, const char* fmt, ...);
#else
void nabto_stun_log_adapter(struct nabto_stun* stun, const char* file, int line, enum nabto_stun_log_level level, const char* fmt, ...);
#endif

#define VA_ARGS(...) , ##__VA_ARGS__

#if defined(HAS_VARIADIC_MACROS)

#define NABTO_STUN_LOG_TRACE(stun, fmt, ...) do { nabto_stun_log_adapter(stun, __FILE__, __LINE__, NABTO_STUN_LOG_LEVEL_TRACE, fmt VA_ARGS(__VA_ARGS__)); } while (0);
#define NABTO_STUN_LOG_INFO(stun, fmt, ...) do { nabto_stun_log_adapter(stun, __FILE__, __LINE__, NABTO_STUN_LOG_LEVEL_INFO, fmt VA_ARGS(__VA_ARGS__)); } while (0);
#define NABTO_STUN_LOG_DEBUG(stun, fmt, ...) do { nabto_stun_log_adapter(stun, __FILE__, __LINE__, NABTO_STUN_LOG_LEVEL_DEBUG, fmt VA_ARGS(__VA_ARGS__)); } while (0);
#define NABTO_STUN_LOG_ERROR(stun, fmt, ...) do { nabto_stun_log_adapter(stun, __FILE__, __LINE__, NABTO_STUN_LOG_LEVEL_ERROR, fmt VA_ARGS(__VA_ARGS__)); } while (0);

#else

#define NABTO_STUN_LOG_TRACE nabto_stun_log_trace_adapter
#define NABTO_STUN_LOG_INFO  nabto_stun_log_info_adapter
#define NABTO_STUN_LOG_DEBUG nabto_stun_log_debug_adapter
#define NABTO_STUN_LOG_ERROR nabto_stun_log_error_adapter

#endif

#define NABTO_STUN_PRIu8  "d"
#define NABTO_STUN_PRIu16 "d"
#define NABTO_STUN_PRIu32 "d"




#ifdef __cplusplus
} // extern "C"
#endif


#endif
