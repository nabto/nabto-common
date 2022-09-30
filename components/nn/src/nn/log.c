#include <nn/log.h>

#include <stddef.h>

void nn_log_init(struct nn_log* logger, nn_log_print logPrint, void* userData)
{
    logger->logPrint = logPrint;
    logger->userData = userData;
}

#ifdef HAS_NO_VARIADIC_MACROS

void nn_log_error_adapter(struct nn_log* logger, const char* module, const char* fmt, ...)
{
    if (logger == NULL) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    logger->logPrint(logger->userData, NN_LOG_SEVERITY_ERROR, module, "", 0, fmt, args);
    va_end(args);
}
void nn_log_warn_adapter(struct nn_log* logger, const char* module, const char* fmt, ...)
{
    if (logger == NULL) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    logger->logPrint(logger->userData, NN_LOG_SEVERITY_WARN, module, "", 0, fmt, args);
    va_end(args);
}
void nn_log_info_adapter(struct nn_log* logger, const char* module, const char* fmt, ...)
{
    if (logger == NULL) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    logger->logPrint(logger->userData, NN_LOG_SEVERITY_INFO, module, "", 0, fmt, args);
    va_end(args);
}
void nn_log_trace_adapter(struct nn_log* logger, const char* module, const char* fmt, ...)
{
    if (logger == NULL) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    logger->logPrint(logger->userData, NN_LOG_SEVERITY_TRACE, module, "", 0, fmt, args);
    va_end(args);
}

#else
void nn_log_adapter(struct nn_log* logger, enum nn_log_severity severity, const char* module, const char* file, int line, ...)
{
    if (logger == NULL) {
        return;
    }
    va_list args;

    va_start(args, line);
    const char* fmt = va_arg(args, const char*);
    logger->logPrint(logger->userData, severity, module, file, line, fmt, args);
    va_end(args);
}
#endif
