#include "nabto_stream_log_helper.h"
#include "nabto_stream_window.h"
#include "nabto_stream_protocol.h"

#include <stdio.h>
#include <string.h>

const char* nabto_stream_stamp_to_string(nabto_stream_stamp s)
{
    static char buffer[64];
    memset(buffer, 0, 64);
    if (s.type == NABTO_STREAM_STAMP_INFINITE) {
        sprintf(buffer, "INFINITE");
    } else if (s.type == NABTO_STREAM_STAMP_NOW) {
        sprintf(buffer, "NOW");
    } else {
        sprintf(buffer, "%" NABTO_STREAM_PRIu32, s.stamp);
    }
    return buffer;
}

const char* nabto_stream_flags_to_string(uint8_t flags)
{
    if (flags == NABTO_STREAM_FLAG_RST) {
        return "RST";
    } else if (flags == NABTO_STREAM_FLAG_SYN) {
        return "SYN";
    } else if (flags == (NABTO_STREAM_FLAG_SYN | NABTO_STREAM_FLAG_ACK)) {
        return "SYN|ACK";
    } else if (flags == NABTO_STREAM_FLAG_ACK) {
        return "ACK";
    } else if (flags == (NABTO_STREAM_FLAG_FIN | NABTO_STREAM_FLAG_ACK)) {
        return "FIN|ACK";
    }
    return "Unknown";
}


const char* nabto_stream_application_event_type_to_string(nabto_stream_application_event_type event)
{
    switch(event) {
        case NABTO_STREAM_APPLICATION_EVENT_TYPE_OPENED: return "APPLICATION_EVENT_TYPE_OPENED";
        case NABTO_STREAM_APPLICATION_EVENT_TYPE_DATA_READY: return "APPLICATION_EVENT_TYPE_DATA_READY";
        case NABTO_STREAM_APPLICATION_EVENT_TYPE_DATA_WRITE: return "APPLICATION_EVENT_TYPE_DATA_WRITE";
        case NABTO_STREAM_APPLICATION_EVENT_TYPE_READ_CLOSED: return "APPLICATION_EVENT_TYPE_READ_CLOSED";
        case NABTO_STREAM_APPLICATION_EVENT_TYPE_WRITE_CLOSED: return "APPLICATION_EVENT_TYPE_WRITE_CLOSED";
        case NABTO_STREAM_APPLICATION_EVENT_TYPE_CLOSED: return "APPLICATION_EVENT_TYPE_CLOSED";
    }
    return "APPLICATION_EVENT_TYPE_UNKNOWN";
}

/**
 * The printable window statename. @param state  the state. @return the state name
 */
const char* nabto_stream_state_as_string(nabto_stream_state state)
{
    switch(state) {
        case ST_IDLE:               return "IDLE";
        case ST_SYN_SENT:           return "SYN_SENT";
        case ST_ACCEPT:             return "ACCEPT";
        case ST_WAIT_ACCEPT:        return "WAIT_ACCEPT";
        case ST_SYN_RCVD:           return "SYN_RCVD";
        case ST_ESTABLISHED:        return "ESTABLISHED";
        case ST_FIN_WAIT_1:         return "FIN_WAIT_1";
        case ST_FIN_WAIT_2:         return "FIN_WAIT_2";
        case ST_CLOSING:            return "CLOSING";
        case ST_TIME_WAIT:          return "TIME_WAIT";
        case ST_CLOSE_WAIT:         return "CLOSE_WAIT";
        case ST_LAST_ACK:           return "LAST_ACK";
        case ST_CLOSED:             return "CLOSED";
        case ST_ABORTED:            return "ABORTED";
        case ST_ABORTED_RST:        return "ABORTED_RST";
        default:                    return "w???";
    }
}

const char* nabto_stream_next_event_type_to_string(enum nabto_stream_next_event_type eventType)
{
    switch(eventType) {
        case ET_ACCEPT: return "ET_ACCEPT";
        case ET_ACK: return "ET_ACK";
        case ET_SYN: return "ET_SYN";
        case ET_SYN_ACK: return "ET_SYN_ACK";
        case ET_DATA: return "ET_DATA";
        case ET_TIMEOUT: return "ET_TIMEOUT";
        case ET_APPLICATION_EVENT: return "ET_APPLICATION_EVENT";
        case ET_TIME_WAIT: return "ET_TIME_WAIT";
        case ET_WAIT: return "ET_WAIT";
        case ET_RST: return "ET_RST";
        case ET_NOTHING: return "ET_NOTHING";
        case ET_CLOSED: return "CLOSED";
        case ET_RELEASED: return "RELEASED";
    }
    return "Not here";
}
