#include <nn/log.h>

#include <stddef.h>

void nn_log_init(struct nn_log* logger, nn_log_print logPrint, void* userData)
{
    logger->logPrint = logPrint;
    logger->userData = userData;
}

void nn_log_adapter(struct nn_log* logger, enum nn_log_severity severity, const char* module, const char* file, int line, const char* fmt, ...)
{
    if (logger == NULL) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    logger->logPrint(logger->userData, severity, module, file, line, fmt, args);
    va_end(args);
}
