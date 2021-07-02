#ifndef _NN_LOG_H_
#define _NN_LOG_H_

#include <stdarg.h>
#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NN_LOG_PRIu8  PRIu8
#define NN_LOG_PRIu16 PRIu16
#define NN_LOG_PRIu32 PRIu32
#define NN_LOG_PRIu64 PRIu64
#define NN_LOG_PRIsize "zu"


enum nn_log_severity {
    NN_LOG_SEVERITY_ERROR = 0x01,
    NN_LOG_SEVERITY_WARN = 0x02,
    NN_LOG_SEVERITY_INFO = 0x04,
    NN_LOG_SEVERITY_TRACE = 0x08
};

/**
 * Function which is called for each log line which needs to be printed.
 */
typedef void (*nn_log_print)(void* userData, enum nn_log_severity severity, const char* module, const char* file, int line, const char* fmt, va_list args);

struct nn_log {
    nn_log_print logPrint;
    void* userData;
};

/**
 * Initialize the logger with the given logFunction and userData.
 */
void nn_log_init(struct nn_log* logger, nn_log_print logPrint, void* userData);

#ifdef HAS_NO_VARIADIC_MACROS

void nn_log_error_adapter(struct nn_log* logger, const char* module, const char* fmt, ...);
void nn_log_warn_adapter(struct nn_log* logger, const char* module, const char* fmt, ...);
void nn_log_info_adapter(struct nn_log* logger, const char* module, const char* fmt, ...);
void nn_log_trace_adapter(struct nn_log* logger, const char* module, const char* fmt, ...);

#ifndef NN_LOG_ERROR
#define NN_LOG_ERROR nn_log_error_adapter
#endif

#ifndef NN_LOG_WARN
#define NN_LOG_WARN nn_log_warn_adapter
#endif

#ifndef NN_LOG_INFO
#define NN_LOG_INFO nn_log_info_adapter
#endif

#ifndef NN_LOG_TRACE
#define NN_LOG_TRACE nn_log_trace_adapter
#endif

// RAW is used for 3rdparty library logging where the library severity should be passed through
#ifndef NN_LOG_RAW
// RAW logging requires varadic macros
#define NN_LOG_RAW(logger, severity, module, line, file, fmt, ...)
#endif
#else

// has variadic macros

/**
 * Internal adapter for chaning the macto to varargs
 */
void nn_log_adapter(struct nn_log* logger, enum nn_log_severity severity, const char* module, const char* file, int line, const char* fmt, ...);

#define VA_ARGS(...) , ##__VA_ARGS__

#ifndef NN_LOG_ERROR
#define NN_LOG_ERROR(logger, module, fmt, ...) do { nn_log_adapter(logger, NN_LOG_SEVERITY_ERROR, module, __FILE__, __LINE__, fmt VA_ARGS(__VA_ARGS__)); } while (0)
#endif

#ifndef NN_LOG_WARN
#define NN_LOG_WARN(logger, module, fmt, ...) do { nn_log_adapter(logger, NN_LOG_SEVERITY_WARN, module, __FILE__, __LINE__, fmt VA_ARGS(__VA_ARGS__)); } while (0)
#endif

#ifndef NN_LOG_INFO
#define NN_LOG_INFO(logger, module, fmt, ...) do { nn_log_adapter(logger, NN_LOG_SEVERITY_INFO, module, __FILE__, __LINE__, fmt VA_ARGS(__VA_ARGS__)); } while (0)
#endif

#ifndef NN_LOG_TRACE
#define NN_LOG_TRACE(logger, module, fmt, ...) do { nn_log_adapter(logger, NN_LOG_SEVERITY_TRACE, module, __FILE__, __LINE__, fmt VA_ARGS(__VA_ARGS__)); } while (0)
#endif

// RAW is used for 3rdparty library logging where the library log parameters should be passed through
#ifndef NN_LOG_RAW
#define NN_LOG_RAW(logger, severity, module, line, file, fmt, ...) do { nn_log_adapter(logger, severity, module, file, line, fmt VA_ARGS(__VA_ARGS__)); } while(0)
#endif

#endif

#ifdef __cplusplus
} //extern "C"
#endif

#endif
