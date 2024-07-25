/*
 * Copyright (C) Nabto - All Rights Reserved.
 */

/**
 * @file
 * Nabto uServer stream packet on unreliable con - Implementation.
 */

#include <streaming/nabto_stream.h>
#include <streaming/nabto_stream_window.h>
#include <streaming/nabto_stream_protocol.h>
#include <streaming/nabto_stream_interface.h>
#include <streaming/nabto_stream_util.h>
#include <streaming/nabto_stream_congestion_control.h>
#include <streaming/nabto_stream_flow_control.h>
#include <streaming/nabto_stream_memory.h>
#include <streaming/nabto_stream_log_helper.h>

#include <string.h>
#include <math.h>
#include <stdlib.h>

void nabto_stream_dump_state(struct nabto_stream* stream);


/******************************************************************************/
/******************************************************************************/


void nabto_stream_state_transition(struct nabto_stream* stream, nabto_stream_state new_state) {
    nabto_stream_state oldState = stream->state;
    if (stream->state == new_state) return;
    stream->state = new_state;
    //nabto_stream_dump_state(stream);
    stream->retransCount = 0;

    switch(new_state) {
        case ST_IDLE:
            break;
        case ST_SYN_SENT:
            stream->timeoutStamp = nabto_stream_stamp_now();
            break;
        case ST_ACCEPT:
            stream->timeoutStamp = nabto_stream_stamp_infinite();
            break;
        case ST_WAIT_ACCEPT:
            stream->timeoutStamp = nabto_stream_stamp_infinite();
            break;
        case ST_SYN_RCVD:
            stream->timeoutStamp = nabto_stream_stamp_now();
            break;

        case ST_ESTABLISHED:
            stream->applicationEvents.opened = true;
            stream->timeoutStamp = nabto_stream_stamp_infinite();
            break;
        case ST_FIN_WAIT_1:
            break;
        case ST_FIN_WAIT_2:
            stream->applicationEvents.writeClosed = true;
            break;
        case ST_CLOSING:
            break;
        case ST_TIME_WAIT:
            if (oldState == ST_FIN_WAIT_1 || oldState == ST_CLOSING) {
                stream->applicationEvents.writeClosed = true;
            }
            break;
        case ST_CLOSE_WAIT:
        case ST_LAST_ACK:
            break;
        case ST_CLOSED:
            if (oldState == ST_LAST_ACK) {
                stream->applicationEvents.writeClosed = true;
            }
            stream->applicationEvents.closed = true;
            //nabto_stream_send_stats(stream, NP_PAYLOAD_STATS_TYPE_DEVICE_STREAM_CLOSE);
            break;
        case ST_ABORTED:
            /**
             * Since we have aborted the stream in a unknown way so we
             * kills the connection. Such that the other end of the
             * stream knows that something not expected happened on
             * this stream.
             */
            // We cannot simply kill the connection since we maybe
            // just wanted to send an unsupported rst packet.
            // nabto_release_connection_req(stream->connection);
            stream->applicationEvents.closed = true;
            //nabto_stream_send_stats(stream, NP_PAYLOAD_STATS_TYPE_DEVICE_STREAM_CLOSE);
            break;
        case ST_ABORTED_RST:
            stream->applicationEvents.closed = true;
            break;
    default:
        break;
    }
}

/******************************************************************************/


void nabto_stream_init(struct nabto_stream* stream, struct nabto_stream_module* module, void* moduleUserData)
{
    memset(stream, 0, sizeof(struct nabto_stream));
    stream->module = module;
    stream->moduleUserData = moduleUserData;
    stream->unacked = &stream->unackedSentinel;
    stream->unacked->nextUnacked = stream->unacked;
    stream->unacked->prevUnacked = stream->unacked;
    stream->unackedSize = 0;

    stream->sendList = &stream->sendListSentinel;
    stream->sendList->nextSend = stream->sendList;
    stream->sendList->prevSend = stream->sendList;
    stream->sendListSize = 0;

    stream->resendList = &stream->resendListSentinel;
    stream->resendList->nextResend = stream->resendList;
    stream->resendList->prevResend = stream->resendList;

    stream->recvRead = &stream->recvReadSentinel;
    stream->recvRead->next = stream->recvRead;
    stream->recvRead->prev = stream->recvRead;

    stream->recvWindow = &stream->recvWindowSentinel;
    stream->recvWindow->next = stream->recvWindow;
    stream->recvWindow->prev = stream->recvWindow;

    stream->timeoutStamp = nabto_stream_stamp_infinite();

    stream->maxSendSegmentSize = NABTO_STREAM_DEFAULT_MAX_SEND_SEGMENT_SIZE;
    stream->maxRecvSegmentSize = NABTO_STREAM_DEFAULT_MAX_RECV_SEGMENT_SIZE;

    // set this value to something like 4294967286 and 2147483638 to
    // test that logical timestamps can wrap around.
    stream->logicalTimestamp = 0;


    stream->disableReplayProtection = false;

    stream->sendSegmentAllocationStamp = nabto_stream_stamp_infinite();
    stream->recvSegmentAllocationStamp = nabto_stream_stamp_infinite();

    nabto_stream_init_send_segment(&stream->finSegment);
    nabto_stream_congestion_control_init(stream);
}

void nabto_stream_init_initiator(struct nabto_stream* stream) {
    // the initiator shall not validate a nonce.
    stream->nonceValidated = true;
}

void nabto_stream_init_responder(struct nabto_stream* stream, uint8_t nonce[8])
{
    stream->nonceValidated = false;
    memcpy(stream->nonce, nonce, NABTO_STREAM_NONCE_SIZE);
}

void nabto_stream_destroy(struct nabto_stream* stream)
{
    // free all unacked segments
    {
        struct nabto_stream_send_segment* iterator = stream->unacked->nextUnacked;
        while (iterator != stream->unacked) {
            struct nabto_stream_send_segment* current = iterator;
            iterator = iterator->nextUnacked;
            nabto_stream_remove_segment_from_unacked_list(stream, current);
            nabto_stream_remove_segment_from_resend_list(current);
            nabto_stream_free_send_segment(stream, current);
        }
    }
    // free all unsent segments
    {
        struct nabto_stream_send_segment* iterator = stream->sendList->nextSend;
        while (iterator != stream->sendList) {
            struct nabto_stream_send_segment* current = iterator;
            iterator = iterator->nextSend;
            nabto_stream_remove_segment_from_send_list(stream, current);
            nabto_stream_free_send_segment(stream, current);
        }
    }

    // free all segments in recv read list
    {
        struct nabto_stream_recv_segment* iterator = stream->recvRead->next;
        while (iterator != stream->recvRead) {
            struct nabto_stream_recv_segment* current = iterator;
            iterator = iterator->next;
            nabto_stream_remove_segment_from_recv_list(current);
            nabto_stream_free_recv_segment(stream, current);
        }
    }

    // free all segments in recv window list
    {
        struct nabto_stream_recv_segment* iterator = stream->recvWindow->next;
        while (iterator != stream->recvWindow) {
            struct nabto_stream_recv_segment* current = iterator;
            iterator = iterator->next;
            nabto_stream_remove_segment_from_recv_list(current);
            nabto_stream_free_recv_segment(stream, current);
        }
    }

    if (stream->nextUnfilledSendSegment) {
        nabto_stream_free_send_segment(stream, stream->nextUnfilledSendSegment);
        stream->nextUnfilledSendSegment = NULL;
    }
}


void nabto_stream_set_start_sequence_number(struct nabto_stream* stream, uint32_t seq)
{
    // needs to be set before open/accept
    stream->startSequenceNumber = seq;
}

/******************************************************************************/
/******************************************************************************/

size_t nabto_stream_can_write(struct nabto_stream* stream)
{

    // else see if we can allocate a new segment and have a chance to
    // send it.

    if (stream->nextUnfilledSendSegment == NULL) {
        return 0;
    }

    if (nabto_stream_sequence_less_equal(stream->xmitMaxAllocated, stream->maxAdvertisedWindow) &&
        nabto_stream_congestion_control_accept_more_data(stream))
    {
        return stream->maxSendSegmentSize;
    }

    return 0;
}

// called from event loop which handles inputs and timeouts.
enum nabto_stream_next_event_type nabto_stream_next_event_to_handle(struct nabto_stream* stream)
{
    if (stream->sendSegmentAllocationStamp.type == NABTO_STREAM_STAMP_FUTURE) {
        nabto_stream_allocate_next_send_segment(stream);
    }
    if (stream->recvSegmentAllocationStamp.type == NABTO_STREAM_STAMP_FUTURE) {
        nabto_stream_allocate_next_recv_segment(stream);
    }

    if (stream->state == ST_IDLE) {
        return ET_NOTHING;
    }
    if (stream->state == ST_ACCEPT) {
        return ET_ACCEPT;
    }
    if (stream->state == ST_WAIT_ACCEPT) {
        return ET_NOTHING;
    }

    if (stream->state == ST_SYN_SENT && nabto_stream_is_stamp_passed(stream, stream->timeoutStamp)) {
        return ET_SYN;
    }

    if (stream->state == ST_SYN_RCVD && nabto_stream_is_stamp_passed(stream, stream->timeoutStamp)) {
        return ET_SYN_ACK;
    }

    // check if we can send data
    if (stream->state == ST_ESTABLISHED ||
        stream->state == ST_FIN_WAIT_1 ||
        stream->state == ST_CLOSE_WAIT ||
        stream->state == ST_LAST_ACK ||
        stream->state == ST_CLOSING)
    {
        if (stream->resendList->nextResend != stream->resendList) {
            if (nabto_stream_congestion_control_can_send(stream) &&
                nabto_stream_flow_control_can_send(stream, stream->resendList->nextResend->seq))
            {
                return ET_DATA;
            }
        }
        if (stream->sendList->nextSend != stream->sendList) {
            if (nabto_stream_congestion_control_can_send(stream) &&
                nabto_stream_flow_control_can_send(stream, stream->sendList->nextSend->seq))
            {
                return ET_DATA;
            }
        }
        if (stream->unacked->nextUnacked != stream->unacked) {
            if (nabto_stream_is_stamp_passed(stream, stream->timeoutStamp)) {
                return ET_TIMEOUT;
            }
        }
    }

    // check if we need to send an ACK imediately
    if (stream->state == ST_ESTABLISHED ||
        stream->state == ST_FIN_WAIT_1 ||
        stream->state == ST_FIN_WAIT_2 ||
        stream->state == ST_CLOSING ||
        stream->state == ST_TIME_WAIT ||
        stream->state == ST_CLOSE_WAIT ||
        stream->state == ST_LAST_ACK)
    {
        if (stream->sendAck ||
            stream->windowHasOpened ||
            stream->imediateAck)
        {
            return ET_ACK;
        }
    }

    if (stream->state == ST_ABORTED) {
        return ET_RST;
    }

    if (stream->applicationEvents.opened ||
        stream->applicationEvents.dataReady ||
        stream->applicationEvents.dataWrite ||
        stream->applicationEvents.readClosed ||
        stream->applicationEvents.writeClosed ||
        stream->applicationEvents.closed)
    {
        return ET_APPLICATION_EVENT;
    }

    // in state FIN_WAIT_1 we need ack on our fin.
    // in state CLOSING we need ack on our fin
    // in
    if (stream->state == ST_ESTABLISHED ||
        stream->state == ST_FIN_WAIT_2 ||
        stream->state == ST_CLOSE_WAIT)
    {
        if (stream->unacked == stream->unacked->nextUnacked &&
            stream->recvSegmentAllocationStamp.type == NABTO_STREAM_STAMP_INFINITE &&
            stream->sendSegmentAllocationStamp.type == NABTO_STREAM_STAMP_INFINITE)
        {
            // all our outstanding data has been acked wait for user input or network input.
            return ET_NOTHING;
        }
    }

    if (stream->state == ST_TIME_WAIT) {
        return ET_TIME_WAIT;
    }


    if (stream->state >= ST_CLOSED) {
        return ET_CLOSED;
    }

    return ET_WAIT;
}

nabto_stream_stamp nabto_stream_get_future_stamp_backoff(struct nabto_stream* stream)
{
    int factor = 16;
    if (stream->retransCount <= 4) {
        factor = 1 << stream->retransCount;
    }
    return nabto_stream_get_future_stamp(stream, ((NABTO_STREAM_DEFAULT_TIMEOUT) * factor));
}

void nabto_stream_event_handled(struct nabto_stream* stream, enum nabto_stream_next_event_type eventType)
{
    switch (eventType) {
        case ET_ACCEPT:
            if (stream->state == ST_ACCEPT) {
                SET_STATE(stream, ST_WAIT_ACCEPT);
            }
            break;
        case ET_SYN:
            // we sent a syn packet, wait for the syn | ack
            stream->timeoutStamp = nabto_stream_get_future_stamp_backoff(stream);
            stream->retransCount++;
            break;
        case ET_SYN_ACK:
            // we sent a syn | ack wait for an ack on the syn | ack
            stream->timeoutStamp = nabto_stream_get_future_stamp_backoff(stream);
            stream->retransCount++;
            break;
        case ET_ACK:
            // we sent an ack
            stream->imediateAck = false;
            stream->sendAck = false;
            break;
        case ET_DATA:
            // we sent some data, wait for ack on the data.
            stream->imediateAck = false;
            stream->timeoutStamp = nabto_stream_get_future_stamp(stream, stream->cCtrl.rto);
            stream->sendAck = false;
            stream->segmentSentAfterTimeout = true;
            break;
        case ET_TIMEOUT:
            if (stream->unacked->nextUnacked != stream->unacked) {
                stream->timeoutStamp = nabto_stream_get_future_stamp(stream, stream->cCtrl.rto);
            } else {
                stream->timeoutStamp = nabto_stream_stamp_infinite();
            }

            break;
        case ET_RST:
            // we sent an rst
            SET_STATE(stream, ST_ABORTED_RST);
            break;

        case ET_TIME_WAIT:
            SET_STATE(stream, ST_CLOSED);
            break;

        default:
            return;
    }
}


void nabto_stream_clear_resend_list(struct nabto_stream* stream)
{
    struct nabto_stream_send_segment* iterator = stream->resendList->nextResend;
    while (iterator != stream->resendList) {
        struct nabto_stream_send_segment* oldIterator = iterator;
        iterator = iterator->nextResend;
        nabto_stream_remove_segment_from_resend_list(oldIterator);
    }
}

/**
 * Move all unacked data segments to the resend queue and go back to
 * the slow start phase.
 */
void nabto_stream_handle_timeout(struct nabto_stream* stream)
{
    nabto_stream_clear_resend_list(stream);
    struct nabto_stream_send_segment* iterator = stream->unacked->nextUnacked;
    while (iterator != stream->unacked) {
        nabto_stream_mark_segment_for_retransmission(stream, iterator);
        iterator = iterator->nextUnacked;
    }
    nabto_stream_congestion_control_timeout(stream);

    stream->segmentSentAfterTimeout = false;

}

void nabto_stream_handle_time_wait(struct nabto_stream* stream)
{
    (void)stream;
    // do nothing action happens in event_handled.
}

void nabto_stream_handle_ack_on_syn_ack(struct nabto_stream* stream)
{
    SET_STATE(stream, ST_ESTABLISHED);
}

void nabto_stream_handle_ack_on_fin(struct nabto_stream* stream)
{
    /**
     * if we have sent a fin and waits for an ack on the fin we are in
     * one of these states.
     *
     * ST_FIN_WAIT_1
     * ST_CLOSING
     * ST_LAST_ACK
     */

    if (stream->state == ST_FIN_WAIT_1) {
        SET_STATE(stream, ST_FIN_WAIT_2);
    } else if (stream->state == ST_CLOSING) {
        SET_STATE(stream, ST_TIME_WAIT)
    } else if (stream->state == ST_LAST_ACK) {
        SET_STATE(stream, ST_CLOSED);
    }
}

void nabto_stream_add_fin_segment_to_send_lists(struct nabto_stream* stream)
{
    // we have received ack on all outstanding data and
    // the user has called close, add the fin segment to
    // the unacked list.
    stream->xmitMaxAllocated += 1;
    stream->finSegment.seq = stream->xmitMaxAllocated;
    stream->finSegment.state = B_DATA;
    nabto_stream_add_segment_to_send_list_before_elm(stream, stream->sendList, &stream->finSegment);
}

/**
 * iterate through the unacked sent segments
 *
 * When the function return stream->unacked there's no more unacked segments to ack.
 */
struct nabto_stream_send_segment* nabto_stream_handle_ack_iterator(struct nabto_stream* stream, struct nabto_stream_send_segment* iterator, uint32_t timestampEcho)
{
    if (iterator == &stream->finSegment) {
        nabto_stream_handle_ack_on_fin(stream);
    }

    if (iterator == &stream->finSegment) {
        NN_LOG_TRACE(stream->module->logger, NABTO_STREAM_LOG_MODULE, "acking fin segment %" NN_LOG_PRIu32, iterator->seq);
    } else {
        NN_LOG_TRACE(stream->module->logger, NABTO_STREAM_LOG_MODULE, "acking data segment %" NN_LOG_PRIu32, iterator->seq);
    }
    //ack segment
    nabto_stream_update_congestion_control_receive_stats(stream, iterator, timestampEcho);
    nabto_stream_congestion_control_handle_ack(stream, iterator);

    stream->applicationEvents.dataWrite = true;

    struct nabto_stream_send_segment* segment = iterator;
    iterator = iterator->prevUnacked;
    nabto_stream_segment_has_been_acked(stream, segment);



    if (stream->unacked == stream->unacked->nextUnacked &&
        stream->sendList == stream->sendList->nextSend)
    {
        if ((stream->state == ST_FIN_WAIT_1 || stream->state == ST_LAST_ACK || stream->state == ST_CLOSING) &&
            stream->finSegment.state == B_IDLE)
        {
            nabto_stream_add_fin_segment_to_send_lists(stream);
        } else {
            // no more unacked data nor fin segments outstanding stop processing for now.
            stream->timeoutStamp = nabto_stream_stamp_infinite();
        }
    }
    return iterator;
}

struct nabto_stream_send_segment* nabto_stream_handle_nack_iterator(struct nabto_stream* stream, struct nabto_stream_send_segment* iterator, uint32_t timestampEcho)
{
    if (iterator->logicalSentStamp < timestampEcho) {
        iterator->ackedAfter++;
        if (iterator->ackedAfter == 2) {
            nabto_stream_congestion_control_adjust_ssthresh_after_triple_ack(stream, iterator);
            stream->reorderedOrLostPackets++;
            nabto_stream_mark_segment_for_retransmission(stream, iterator);
        }
    }

    iterator = iterator->prevUnacked;
    return iterator;
}


void nabto_stream_segment_has_been_acked(struct nabto_stream* stream, struct nabto_stream_send_segment* segment)
{
    // if the segment is in the retransmission queue, remove it from the list.
    nabto_stream_remove_segment_from_resend_list(segment);
    nabto_stream_remove_segment_from_unacked_list(stream, segment);

    nabto_stream_free_send_segment(stream, segment);
}


/**
 * Handle receiving of an rst packet. In this case we should shutdown,
 * but not send an rst back.
 */
void nabto_stream_handle_rst(struct nabto_stream* stream)
{
    nabto_stream_state state = stream->state;
    if (state >= ST_CLOSED) {
        return;
    }
    if ((state == ST_LAST_ACK || state == ST_CLOSING) &&
        ((stream->unacked == stream->unacked->nextUnacked &&
          stream->sendList == stream->sendList->nextSend) ||
         stream->unacked->nextUnacked == &stream->finSegment))
    {
        // we have received a FIN from the other peer since we are in
        // ST_LAST_ACK, so the other peer has no more data to send to
        // us.
        //
        // There's no unacked data so this application do not have
        // enqueued any data which could be lost.  Or the rst is a
        // response to a retransmission of a fin packet.
        SET_STATE(stream, ST_CLOSED);
    } else if (state == ST_TIME_WAIT) {
        SET_STATE(stream, ST_CLOSED);
    } else {
        // do not send an RST
        SET_STATE(stream, ST_ABORTED_RST);
    }
}

bool nabto_stream_is_closed(struct nabto_stream* stream) {
    return stream->state == ST_CLOSED || stream->state == ST_ABORTED;
}

/**
 * This is called after tripple acks or after a timeout.
 */
void nabto_stream_mark_segment_for_retransmission(struct nabto_stream* stream, struct nabto_stream_send_segment* segment)
{
    if (segment->nextResend != segment) {
        // the segment is already in the retransmission queue.
        return;
    }

    // add the segment into an appropriate place in the resend queue
    struct nabto_stream_send_segment* iterator = stream->resendList->nextResend;
    while (iterator != stream->resendList &&
           nabto_stream_sequence_less(iterator->seq, segment->seq))
    {
        iterator = iterator->nextResend;
    }

    nabto_stream_add_segment_to_resend_list_before_elm(iterator, segment);
}



void nabto_stream_dump_state(struct nabto_stream* stream) {
    NN_LOG_TRACE(stream->module->logger, NABTO_STREAM_LOG_MODULE, "dump of nabto_stream");
    NN_LOG_TRACE(stream->module->logger, NABTO_STREAM_LOG_MODULE, " state %i", stream->state);

    NN_LOG_TRACE(stream->module->logger, NABTO_STREAM_LOG_MODULE, " stream ");
    NN_LOG_TRACE(stream->module->logger, NABTO_STREAM_LOG_MODULE, "  state %i", stream->state);
    NN_LOG_TRACE(stream->module->logger, NABTO_STREAM_LOG_MODULE, "  retransCount %i", stream->retransCount);
    NN_LOG_TRACE(stream->module->logger, NABTO_STREAM_LOG_MODULE, "  xmitMaxAllocated %" NN_LOG_PRIu32, stream->xmitMaxAllocated);
    NN_LOG_TRACE(stream->module->logger, NABTO_STREAM_LOG_MODULE, "  finSent %i", stream->finSegment.seq);
    NN_LOG_TRACE(stream->module->logger, NABTO_STREAM_LOG_MODULE, "  maxAdvertisedWindow %" NN_LOG_PRIu32, stream->maxAdvertisedWindow);
    NN_LOG_TRACE(stream->module->logger, NABTO_STREAM_LOG_MODULE, " congestion control");
    NN_LOG_TRACE(stream->module->logger, NABTO_STREAM_LOG_MODULE, "  srtt %f", stream->cCtrl.srtt);
    NN_LOG_TRACE(stream->module->logger, NABTO_STREAM_LOG_MODULE, "  rttVar %f", stream->cCtrl.rttVar);
    NN_LOG_TRACE(stream->module->logger, NABTO_STREAM_LOG_MODULE, "  rto %" NN_LOG_PRIu16, stream->cCtrl.rto);
    NN_LOG_TRACE(stream->module->logger, NABTO_STREAM_LOG_MODULE, "  cwnd %f", stream->cCtrl.cwnd);
    NN_LOG_TRACE(stream->module->logger, NABTO_STREAM_LOG_MODULE, "  ssThreshold %" NN_LOG_PRIu32, stream->cCtrl.ssThreshold);
    NN_LOG_TRACE(stream->module->logger, NABTO_STREAM_LOG_MODULE, "  flightSize %" NN_LOG_PRIu32, stream->cCtrl.flightSize);
}

void nabto_stat_init(struct nabto_stats* stat)
{
    stat->min = 0;
    stat->max = 0;
    stat->count = 0;
    stat->avg = 0;
}

void nabto_stream_stats_observe(struct nabto_stats* stat, double value)
{
    if (stat->min == 0 && stat->max == 0) {
        stat->min = stat->max = value;
    }

    if (value < stat->min) {
        stat->min = value;
    }
    if (value > stat->max) {
        stat->max = value;
    }
    stat->count += 1;
    stat->avg += (value - stat->avg) / stat->count;
}

uint32_t nabto_stream_get_duration(struct nabto_stream* stream)
{
    uint32_t duration = nabto_stream_stamp_diff_now(stream, stream->streamStart);
    return duration;
}

void nabto_stream_handle_syn(struct nabto_stream* stream, struct nabto_stream_header* hdr, struct nabto_stream_syn_request* req)
{
    // a stream can handle a syn packet in the state ST_IDLE, after
    // ST_IDLE a syn packet is a retransmission.
    if (stream->state == ST_IDLE) {
        SET_STATE(stream, ST_ACCEPT);
        stream->contentType = req->contentType;
        stream->maxSendSegmentSize = NABTO_STREAM_MIN(stream->maxSendSegmentSize, req->maxSendSegmentSize);
        stream->maxSendSegmentSize = NABTO_STREAM_MIN(stream->maxRecvSegmentSize, req->maxRecvSegmentSize);

        stream->recvMax = req->seq;
        stream->recvMaxAllocated = req->seq;
        stream->recvTop = req->seq;
        stream->timestampToEcho = hdr->timestampValue;
        if (!stream->disableReplayProtection) {
            if (!req->hasNonceCapability) {
                stream->disableReplayProtection = true;
            }
        }

    } else if (stream->state == ST_WAIT_ACCEPT) {
        // do nothing, wait for the user to accept the stream.
    } else if (stream->state == ST_SYN_RCVD) {
        // this us a syn retransmission, force sending a new syn_ack
        stream->timeoutStamp = nabto_stream_stamp_now();
    }
}

void nabto_stream_handle_syn_ack(struct nabto_stream* stream, struct nabto_stream_header* hdr, struct nabto_stream_syn_ack_request* req)
{
    if (!stream->disableReplayProtection) {
        if (!req->hasNonce) {
            // this is only relevant for testing purposes such that it can be
            // testet that both ends agreed on whether replay protection was
            // used or not.
            stream->disableReplayProtection = true;
        } else {
            stream->sendNonce = true;
            memcpy(stream->nonce, req->nonce, NABTO_STREAM_NONCE_SIZE);
        }
    }
    // syn ack is only possible if we have sent a syn first
    if (stream->state == ST_SYN_SENT) {
        // answer on our syn
        SET_STATE(stream, ST_ESTABLISHED);
        stream->maxSendSegmentSize = NABTO_STREAM_MIN(stream->maxSendSegmentSize, req->maxSendSegmentSize);
        stream->maxSendSegmentSize = NABTO_STREAM_MIN(stream->maxRecvSegmentSize, req->maxRecvSegmentSize);
        stream->imediateAck = true;
        // syn | ack packet has sequence 0
        stream->recvMax = req->seq;
        stream->recvMaxAllocated = req->seq;
        stream->recvTop = req->seq;
        stream->timestampToEcho = hdr->timestampValue;
        nabto_stream_allocate_next_send_segment(stream);
        nabto_stream_allocate_next_recv_segment(stream);
    } else if (stream->state == ST_ESTABLISHED) {
        // A retransmission, trigger sending a new ack.
        stream->imediateAck = true;
    }
}

struct nabto_stream_recv_segment* nabto_stream_find_recv_buffer(struct nabto_stream* stream, uint32_t seq)
{
    while (nabto_stream_sequence_less(stream->recvMaxAllocated, seq)) {
        if (!nabto_stream_allocate_next_recv_segment(stream)) {
            return NULL;
        }
    }

    struct nabto_stream_recv_segment* iterator = stream->recvWindow->next;

    while (iterator != stream->recvWindow) {
        if (seq == iterator->seq) {
            return iterator;
        }
        iterator = iterator->next;
    }
    return NULL;
}



void nabto_stream_handle_fin(struct nabto_stream* stream, uint32_t seq)
{
    nabto_stream_state state = stream->state;

    if (state == ST_ESTABLISHED ||
        state == ST_FIN_WAIT_1 ||
        state == ST_FIN_WAIT_2)
    {
        struct nabto_stream_recv_segment* segment = nabto_stream_find_recv_buffer(stream, seq);
        if (!segment) {
            return;
        }

        segment->isFin = true;
        segment->state = RB_DATA;
        segment->used = 0;
        stream->recvMax = nabto_stream_sequence_max(stream->recvMax, seq);
        stream->imediateAck = true;
        nabto_stream_move_segments_from_recv_window_to_recv_read(stream);

        if (state == ST_ESTABLISHED) {
            SET_STATE(stream, ST_CLOSE_WAIT);
        } else if (stream->state == ST_FIN_WAIT_1) {
            SET_STATE(stream, ST_CLOSING);
        } else if (stream->state == ST_FIN_WAIT_2) {
            SET_STATE(stream, ST_TIME_WAIT);
        }
    } else {
        // this is a retransmission of a fin, resend an ack.
        stream->imediateAck = true;
    }
}


void nabto_stream_handle_data(struct nabto_stream* stream, uint32_t seq, const uint8_t* data, uint16_t dataLength)
{
    NN_LOG_TRACE(stream->module->logger, NABTO_STREAM_LOG_MODULE, "handle data segment: %" NN_LOG_PRIu32 " size: %" NN_LOG_PRIu16, seq, dataLength);
    if (dataLength > stream->maxRecvSegmentSize) {
        // invalid data drop it.
        return;
    }

    if (stream->state == ST_ESTABLISHED ||
        stream->state == ST_CLOSE_WAIT ||
        stream->state == ST_FIN_WAIT_1 ||
        stream->state == ST_FIN_WAIT_2)
    {
        if (nabto_stream_sequence_less(stream->recvTop, seq)) {
            // we are expecting this sequence number to be in the recvWindow.
            struct nabto_stream_recv_segment* recvBuffer = nabto_stream_find_recv_buffer(stream, seq);
            if (!recvBuffer) {
                NN_LOG_TRACE(stream->module->logger, NABTO_STREAM_LOG_MODULE, "data outside of recv window %" NN_LOG_PRIu32 " data is dropped", seq);
                stream->imediateAck = true;
            } else {
                if (recvBuffer->state != RB_IDLE) {
                    //retransmission
                    stream->imediateAck = true;
                } else {
                    recvBuffer->state = RB_DATA;
                    recvBuffer->size = dataLength;
                    recvBuffer->used = 0;
                    memcpy(recvBuffer->buf, data, dataLength);
                    stream->receivedBytes += dataLength;
                    if (stream->timeFirstMBReceived == 0 && stream->receivedBytes >= 1048576) {
                        stream->timeFirstMBReceived = nabto_stream_get_duration(stream);
                    }
                    stream->recvMax = nabto_stream_sequence_max(stream->recvMax, seq);

                    stream->imediateAck = true;
                    nabto_stream_move_segments_from_recv_window_to_recv_read(stream);
                }
            }
        } else {
            NN_LOG_TRACE(stream->module->logger, NABTO_STREAM_LOG_MODULE, "sequence number: %" NN_LOG_PRIu32 " is from the past, send a new ack", seq);
            // retransmitted data from past
            stream->imediateAck = true;
        }
    }
}

void nabto_stream_move_segments_from_recv_window_to_recv_read(struct nabto_stream* stream)
{
    struct nabto_stream_recv_segment* iterator = stream->recvWindow->next;
    while( iterator != stream->recvWindow && iterator->state == RB_DATA) {
        NN_LOG_TRACE(stream->module->logger, NABTO_STREAM_LOG_MODULE, "moving seq: %" NN_LOG_PRIu32 " to recvRead", iterator->seq);
        struct nabto_stream_recv_segment* next = iterator->next;
        nabto_stream_remove_segment_from_recv_list(iterator);
        nabto_stream_add_segment_to_recv_list_before_elm(stream->recvRead, iterator);
        stream->recvTop = iterator->seq;
        iterator = next;
        stream->applicationEvents.dataReady = true;
    }
}

uint32_t nabto_stream_get_recv_window_size(struct nabto_stream* stream)
{
    // return current recv window size relative to recvTop.
    if (stream->recvMaxAllocated == stream->recvMax) {
        return 0;
    }
    return NABTO_STREAM_WINDOW_SIZE_INF;
}

// return first recv segment with data.
struct nabto_stream_recv_segment* nabto_stream_recv_max_segment(struct nabto_stream* stream)
{
    struct nabto_stream_recv_segment* iterator = stream->recvWindow->prev;
    while(iterator->state == RB_IDLE && iterator != stream->recvWindow) {
        iterator = iterator->prev;
    }
    return iterator;
}


nabto_stream_stamp nabto_stream_next_event(struct nabto_stream * stream)
{
    nabto_stream_stamp min = nabto_stream_stamp_infinite();

    min = nabto_stream_stamp_less_of(min, stream->timeoutStamp);
    min = nabto_stream_stamp_less_of(min, stream->recvSegmentAllocationStamp);
    min = nabto_stream_stamp_less_of(min, stream->sendSegmentAllocationStamp);
    return min;
}

void nabto_stream_dispatch_event(struct nabto_stream* stream)
{
    if (!stream->applicationEventCallback) {
        return;
    }
    if (stream->applicationEvents.opened) {
        stream->applicationEvents.opened = false;
        stream->applicationEventCallback(NABTO_STREAM_APPLICATION_EVENT_TYPE_OPENED, stream->applicationEventCallbackData);
        return;
    }
    if (stream->applicationEvents.dataReady) {
        stream->applicationEvents.dataReady = false;
        stream->applicationEventCallback(NABTO_STREAM_APPLICATION_EVENT_TYPE_DATA_READY, stream->applicationEventCallbackData);
        return;
    }
    if (stream->applicationEvents.dataWrite) {
        stream->applicationEvents.dataWrite = false;
        stream->applicationEventCallback(NABTO_STREAM_APPLICATION_EVENT_TYPE_DATA_WRITE, stream->applicationEventCallbackData);
        return;
    }
    if (stream->applicationEvents.readClosed) {
        stream->applicationEvents.readClosed = false;
        stream->applicationEventCallback(NABTO_STREAM_APPLICATION_EVENT_TYPE_READ_CLOSED, stream->applicationEventCallbackData);
        return;
    }
    if (stream->applicationEvents.writeClosed) {
        stream->applicationEvents.writeClosed = false;
        stream->applicationEventCallback(NABTO_STREAM_APPLICATION_EVENT_TYPE_WRITE_CLOSED, stream->applicationEventCallbackData);
        return;
    }
    if (stream->applicationEvents.closed) {
        stream->applicationEvents.closed = false;
        stream->applicationEventCallback(NABTO_STREAM_APPLICATION_EVENT_TYPE_CLOSED, stream->applicationEventCallbackData);
        return;
    }
}


void nabto_stream_connection_died(struct nabto_stream* stream)
{
    if (stream->state == ST_LAST_ACK) {
        SET_STATE(stream, ST_CLOSED);
    } else if (stream->state < ST_CLOSED) {
        SET_STATE(stream, ST_ABORTED_RST);
    }
}
