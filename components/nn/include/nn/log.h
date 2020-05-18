#ifndef _NN_LOG_H_
#define _NN_LOG_H_

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

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

/**
 * Internal adapter for chaning the macto to varargs
 */
void nn_log_adapter(struct nn_log* logger, enum nn_log_severity severity, const char* module, const char* file, int line, const char* fmt, ...);

#define VA_ARGS(...) , ##__VA_ARGS__

#define NN_LOG_ERROR(logger, module, fmt, ...) do { nn_log_adapter(logger, NN_LOG_SEVERITY_ERROR, module, __FILE__, __LINE__, fmt VA_ARGS(__VA_ARGS__)); } while (0)

#define NN_LOG_WARN(logger, module, fmt, ...) do { nn_log_adapter(logger, NN_LOG_SEVERITY_WARN, module, __FILE__, __LINE__, fmt VA_ARGS(__VA_ARGS__)); } while (0)

#define NN_LOG_INFO(logger, module, fmt, ...) do { nn_log_adapter(logger, NN_LOG_SEVERITY_INFO, module, __FILE__, __LINE__, fmt VA_ARGS(__VA_ARGS__)); } while (0)

#define NN_LOG_TRACE(logger, module, fmt, ...) do { nn_log_adapter(logger, NN_LOG_SEVERITY_TRACE, module, __FILE__, __LINE__, fmt VA_ARGS(__VA_ARGS__)); } while (0)

#ifdef __cplusplus
} //extern "C"
#endif

#endif
