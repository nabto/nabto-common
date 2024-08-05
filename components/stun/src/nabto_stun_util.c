#ifndef _NABTO_STUN_UTIL_H_
#define _NABTO_STUN_UTIL_H_

#include <nabto_stun/nabto_stun_defines.h>

const char* nabto_stun_next_event_type_to_string(enum nabto_stun_next_event_type event)
{
    switch(event) {
        case STUN_ET_SEND_PRIMARY:
            return "STUN_ET_SEND_PRIMARY";
        case STUN_ET_SEND_SECONDARY:
            return "STUN_ET_SEND_SECONDARY";
        case STUN_ET_WAIT:
            return "STUN_ET_WAIT";
        case STUN_ET_NO_EVENT:
            return "STUN_ET_NO_EVENT";
        case STUN_ET_COMPLETED:
            return "STUN_ET_COMPLETED";
        case STUN_ET_FAILED:
            return "STUN_ET_FAILED";
        default:
            return "Unknown next stun event type";
    }
}

#endif
