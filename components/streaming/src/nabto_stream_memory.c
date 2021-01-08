#include <streaming/nabto_stream_memory.h>
#include <streaming/nabto_stream_window.h>
#include <streaming/nabto_stream_util.h>


void nabto_stream_free_send_segment(struct nabto_stream* stream, struct nabto_stream_send_segment* segment)
{
    if (segment == &stream->finSegment) {
        // do nothing
    } else {
        stream->module->free_send_segment(segment, stream->moduleUserData);
    }
}

void nabto_stream_free_recv_segment(struct nabto_stream* stream, struct nabto_stream_recv_segment* segment)
{
    stream->module->free_recv_segment(segment, stream->moduleUserData);
}



void nabto_stream_allocate_next_send_segment(struct nabto_stream* stream)
{
    if (stream->nextUnfilledSendSegment != NULL) {
        return;
    }
    stream->nextUnfilledSendSegment = stream->module->alloc_send_segment(stream->maxSendSegmentSize, stream->moduleUserData);
    if (stream->nextUnfilledSendSegment == NULL) {
        NN_LOG_TRACE(stream->module->logger, NABTO_STREAM_LOG_MODULE, "Can not allocate send segment going to NEED_SEND_SEGMENT state");
        stream->sendSegmentAllocationStamp = nabto_stream_get_future_stamp(stream, NABTO_STREAM_SEGMENT_ALLOCATION_RETRY_INTERVAL);
        return;
    }
    if (stream->sendSegmentAllocationStamp.type == NABTO_STREAM_STAMP_FUTURE) {
        // notify the producers that data can be written to the stream.
        stream->applicationEvents.dataWrite = true;
    }
    stream->sendSegmentAllocationStamp = nabto_stream_stamp_infinite();

    nabto_stream_init_send_segment(stream->nextUnfilledSendSegment);
}

bool nabto_stream_allocate_next_recv_segment(struct nabto_stream* stream)
{
    struct nabto_stream_recv_segment* segment = stream->module->alloc_recv_segment(stream->maxRecvSegmentSize, stream->moduleUserData);

    if (segment == NULL) {
        NN_LOG_TRACE(stream->module->logger, NABTO_STREAM_LOG_MODULE, "Can not allocate recv segment going to NEED_RECV_SEGMENT state");
        stream->recvSegmentAllocationStamp = nabto_stream_get_future_stamp(stream, NABTO_STREAM_SEGMENT_ALLOCATION_RETRY_INTERVAL);
        return false;
    }
    if (stream->recvSegmentAllocationStamp.type == NABTO_STREAM_STAMP_FUTURE) {
        stream->imediateAck = true;
    }
    stream->recvSegmentAllocationStamp = nabto_stream_stamp_infinite();
    stream->recvMaxAllocated++;
    segment->seq = stream->recvMaxAllocated;
    nabto_stream_init_recv_segment(segment);

    nabto_stream_add_segment_to_recv_list_before_elm(stream->recvWindow, segment);
    return true;
}

void nabto_stream_send_segment_available(struct nabto_stream* stream)
{
    if (stream->state > ST_SYN_SENT &&
        stream->state < ST_CLOSED) {
        if (stream->nextUnfilledSendSegment != NULL) {
            return;
        }
        nabto_stream_allocate_next_send_segment(stream);
    }
}

void nabto_stream_recv_segment_available(struct nabto_stream* stream)
{
    if (stream->state > ST_SYN_SENT &&
        stream->state < ST_CLOSED)
    {
        if (stream->recvMaxAllocated != stream->recvMax) {
            return;
        }
        nabto_stream_allocate_next_recv_segment(stream);
    }
}
