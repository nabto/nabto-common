#include "nabto_stream_util.h"
#include "nabto_stream_protocol.h"

#include <string.h>
#include <stdio.h>

void nabto_stream_init_send_segment(struct nabto_stream_send_segment* segment)
{
    segment->nextSend = segment;
    segment->prevSend = segment;
    segment->nextResend = segment;
    segment->prevResend = segment;
    segment->nextUnacked = segment;
    segment->prevUnacked = segment;
}

void nabto_stream_init_recv_segment(struct nabto_stream_recv_segment* segment)
{
    segment->next = segment;
    segment->prev = segment;
}


// used to add an element to the end of the send or resend list.
void nabto_stream_add_segment_to_send_list_before_elm(struct nabto_stream* stream, struct nabto_stream_send_segment* beforeThis, struct nabto_stream_send_segment* segment)
{
    struct nabto_stream_send_segment* oldPrev = beforeThis->prevSend;
    segment->nextSend = beforeThis;
    segment->prevSend = oldPrev;
    beforeThis->prevSend = segment;
    oldPrev->nextSend = segment;

    stream->sendListSize += 1;
}

void nabto_stream_remove_segment_from_send_list(struct nabto_stream* stream, struct nabto_stream_send_segment* segment)
{
    struct nabto_stream_send_segment* next = segment->nextSend;
    struct nabto_stream_send_segment* prev = segment->prevSend;

    next->prevSend = prev;
    prev->nextSend = next;

    segment->nextSend = segment;
    segment->prevSend = segment;

    stream->sendListSize -= 1;
}

void nabto_stream_add_segment_to_resend_list_before_elm(struct nabto_stream_send_segment* beforeThis, struct nabto_stream_send_segment* segment)
{
    struct nabto_stream_send_segment* before = beforeThis->prevResend;
    struct nabto_stream_send_segment* after = beforeThis;

    before->nextResend = segment;
    segment->nextResend = after;
    after->prevResend = segment;
    segment->prevResend = before;
}

void nabto_stream_remove_segment_from_resend_list(struct nabto_stream_send_segment* segment)
{
    struct nabto_stream_send_segment* next = segment->nextResend;
    struct nabto_stream_send_segment* prev = segment->prevResend;

    next->prevResend = prev;
    prev->nextResend = next;

    segment->nextResend = segment;
    segment->prevResend = segment;
}

// used to modify unacked list
void nabto_stream_add_segment_to_unacked_list_before_elm(struct nabto_stream* stream, struct nabto_stream_send_segment* beforeThis, struct nabto_stream_send_segment* segment)
{
    struct nabto_stream_send_segment* before = beforeThis->prevUnacked;
    struct nabto_stream_send_segment* after = beforeThis;

    before->nextUnacked = segment;
    segment->nextUnacked = after;
    after->prevUnacked = segment;
    segment->prevUnacked = before;

    stream->unackedSize += 1;
}

void nabto_stream_remove_segment_from_unacked_list(struct nabto_stream* stream, struct nabto_stream_send_segment* segment)
{
    struct nabto_stream_send_segment* next = segment->nextUnacked;
    struct nabto_stream_send_segment* prev = segment->prevUnacked;

    next->prevUnacked = prev;
    prev->nextUnacked = next;

    segment->nextUnacked = segment;
    segment->prevUnacked = segment;

    stream->unackedSize -= 1;
}


// used to add an element to the end of the recvRead or recvWindow list.
void nabto_stream_add_segment_to_recv_list_before_elm(struct nabto_stream_recv_segment* beforeThis, struct nabto_stream_recv_segment* segment)
{
    struct nabto_stream_recv_segment* oldPrev = beforeThis->prev;
    segment->next = beforeThis;
    segment->prev = oldPrev;
    beforeThis->prev = segment;
    oldPrev->next = segment;
}

void nabto_stream_remove_segment_from_recv_list(struct nabto_stream_recv_segment* segment)
{
    struct nabto_stream_recv_segment* next = segment->next;
    struct nabto_stream_recv_segment* prev = segment->prev;

    next->prev = prev;
    prev->next = next;

    segment->next = segment;
    segment->prev = segment;
}

nabto_stream_stamp nabto_stream_stamp_infinite()
{
    nabto_stream_stamp stamp;
    stamp.type = NABTO_STREAM_STAMP_INFINITE;
    return stamp;
}

nabto_stream_stamp nabto_stream_stamp_now()
{
    nabto_stream_stamp stamp;
    stamp.type = NABTO_STREAM_STAMP_NOW;
    return stamp;
}

nabto_stream_stamp nabto_stream_get_stamp(struct nabto_stream* stream)
{
    nabto_stream_stamp stamp;
    stamp.type = NABTO_STREAM_STAMP_FUTURE;
    stamp.stamp = stream->module->get_stamp(stream->moduleUserData);
    return stamp;
}

nabto_stream_stamp nabto_stream_get_future_stamp(struct nabto_stream* stream, uint32_t milliseconds)
{
    nabto_stream_stamp stamp;
    stamp.type = NABTO_STREAM_STAMP_FUTURE;
    stamp.stamp = stream->module->get_stamp(stream->moduleUserData);
    stamp.stamp += milliseconds;
    return stamp;
}

bool nabto_stream_stamp_less(nabto_stream_stamp s1, nabto_stream_stamp s2)
{
    if (s1.type == NABTO_STREAM_STAMP_INFINITE) {
        return false;
    } else if (s1.type == NABTO_STREAM_STAMP_NOW) {
        return true;
    } else if (s2.type == NABTO_STREAM_STAMP_INFINITE) {
        return true;
    } else if (s2.type == NABTO_STREAM_STAMP_NOW) {
        return false;
    } else {
        return (((int32_t)s1.stamp) - ((int32_t)s2.stamp)) < 0;
    }
}


nabto_stream_stamp nabto_stream_stamp_less_of(const nabto_stream_stamp s1, const nabto_stream_stamp s2)
{
    if (nabto_stream_stamp_less(s1, s2)) {
        return s1;
    } else {
        return s2;
    }
}


// passed or equal
bool nabto_stream_is_stamp_passed(struct nabto_stream* stream, nabto_stream_stamp s)
{
    if (s.type == NABTO_STREAM_STAMP_NOW) {
        return true;
    } else if (s.type == NABTO_STREAM_STAMP_INFINITE) {
        return false;
    } else {
        int32_t now = stream->module->get_stamp(stream->moduleUserData);
        int32_t stamp = s.stamp;
        int32_t diff = stamp - now;
        // if now > stamp
        return (diff <= 0);
    }
}

int32_t nabto_stream_stamp_diff_now(struct nabto_stream* stream, nabto_stream_stamp s)
{
    if (s.type == NABTO_STREAM_STAMP_NOW) {
        return 0;
    } else if (s.type == NABTO_STREAM_STAMP_INFINITE) {
        return INT32_MAX;
    } else {
        int32_t now = stream->module->get_stamp(stream->moduleUserData);
        int32_t stamp = s.stamp;
        int32_t diff = stamp - now;
        return diff;
    }
}


/**********************
 * Logical timestamps *
 **********************/
uint32_t nabto_stream_logical_stamp_get(struct nabto_stream* stream)
{
    return stream->logicalTimestamp++;
}

bool nabto_stream_logical_stamp_less(uint32_t s1, uint32_t s2)
{
    return ((int32_t)s1 - (int32_t)s2) < 0;
}

bool nabto_stream_logical_stamp_less_or_equal(uint32_t s1, uint32_t s2)
{
    return ((int32_t)s1 - (int32_t)s2) <= 0;
}


bool nabto_stream_sequence_less(uint32_t lhs, uint32_t rhs)
{
    int32_t s1 = (int32_t)lhs;
    int32_t s2 = (int32_t)rhs;
    return (s1 - s2) < 0;
}

bool nabto_stream_sequence_less_equal(uint32_t lhs, uint32_t rhs)
{
    return (nabto_stream_sequence_less(lhs, rhs) || lhs == rhs);
}

uint32_t nabto_stream_sequence_max(uint32_t s1, uint32_t s2)
{
    if (nabto_stream_sequence_less(s1, s2)) {
        return s2;
    } else {
        return s1;
    }
}


void nabto_stream_module_notify_event(struct nabto_stream* stream, enum nabto_stream_module_event event)
{
    stream->module->notify_event(event, stream->moduleUserData);
}
