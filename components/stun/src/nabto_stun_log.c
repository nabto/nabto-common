#include "nabto_stun_log.h"

#include "nabto_stun_client.h"

#ifndef HAS_VARIADIC_MACROS
void nabto_stun_log_trace_adapter(struct nabto_stun* stun, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    stun->module->log("", 0, NABTO_STUN_LOG_LEVEL_TRACE, fmt, args, stun->moduleUserData);
    va_end(args);
}
void nabto_stun_log_info_adapter(struct nabto_stun* stun, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    stun->module->log("", 0, NABTO_STUN_LOG_LEVEL_INFO, fmt, args, stun->moduleUserData);
    va_end(args);

}
void nabto_stun_log_debug_adapter(struct nabto_stun* stun, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    stun->module->log("", 0, NABTO_STUN_LOG_LEVEL_DEBUG, fmt, args, stun->moduleUserData);
    va_end(args);

}
void nabto_stun_log_error_adapter(struct nabto_stun* stun, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    stun->module->log("", 0, NABTO_STUN_LOG_LEVEL_ERROR, fmt, args, stun->moduleUserData);
    va_end(args);

}
#else

void nabto_stun_log_adapter(struct nabto_stun* stun, const char* file, int line, enum nabto_stun_log_level level, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    stun->module->log(file, line, level, fmt, args, stun->moduleUserData);
    va_end(args);
}

#endif
