#include <streaming/nabto_stream_flow_control.h>
#include <streaming/nabto_stream_window.h>
#include <streaming/nabto_stream_util.h>

void nabto_stream_flow_control_advertised_window_reduced(struct nabto_stream* stream)
{
    // move all segments from unacked which is above the
    // maxAdvertisedWindow to the send list both the unacked list and
    // the send list is sorted

    struct nabto_stream_send_segment* iterator = stream->unacked->prevUnacked;

    while(iterator != stream->unacked && nabto_stream_sequence_less(stream->maxAdvertisedWindow, iterator->seq)) {
        struct nabto_stream_send_segment* segment = iterator;
        iterator = iterator->prevUnacked;
        // move segment to start of recvList as the recvList is sorted
        nabto_stream_remove_segment_from_unacked_list(stream, segment);

        nabto_stream_remove_segment_from_resend_list(segment);

        nabto_stream_add_segment_to_send_list_before_elm(stream, stream->sendList->nextSend, segment);
    }
}

bool nabto_stream_flow_control_can_send(struct nabto_stream* stream, uint32_t seq)
{
    // either seq is less than the maxAdvertisedWindow or seq is the
    // next segment above maxAdvertisedWindow but an ack with a window
    // opened event has been lost from the other peer. This can only
    // send one segment after a timeout.

    if (nabto_stream_sequence_less_equal(seq, stream->maxAdvertisedWindow)) {
        return true;
    } else if (!stream->segmentSentAfterTimeout) {
        return true;
    }

    return false;
}
