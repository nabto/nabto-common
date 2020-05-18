#include "nabto_stream_log.h"

#include "nabto_stream_window.h"

#ifndef HAS_VARIADIC_MACROS
void nabto_stream_log_trace_adapter(struct nabto_stream* stream, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    stream->module->log("", 0, NABTO_STREAM_LOG_LEVEL_TRACE, fmt, args, stream->moduleUserData);
    va_end(args);
}
void nabto_stream_log_info_adapter(struct nabto_stream* stream, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    stream->module->log("", 0, NABTO_STREAM_LOG_LEVEL_INFO, fmt, args, stream->moduleUserData);
    va_end(args);

}
void nabto_stream_log_debug_adapter(struct nabto_stream* stream, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    stream->module->log("", 0, NABTO_STREAM_LOG_LEVEL_DEBUG, fmt, args, stream->moduleUserData);
    va_end(args);

}
void nabto_stream_log_error_adapter(struct nabto_stream* stream, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    stream->module->log("", 0, NABTO_STREAM_LOG_LEVEL_ERROR, fmt, args, stream->moduleUserData);
    va_end(args);

}
#else

void nabto_stream_log_adapter(struct nabto_stream* stream, const char* file, int line, enum nabto_stream_log_level level, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    stream->module->log(file, line, level, fmt, args, stream->moduleUserData);
    va_end(args);
}

#endif
