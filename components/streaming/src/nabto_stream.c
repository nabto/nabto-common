/*
 * Copyright (C) Nabto - All Rights Reserved.
 */

/**
 * @file
 * Nabto uServer stream - Implementation.
 */

#include <streaming/nabto_stream.h>
#include <streaming/nabto_stream_window.h>
#include <streaming/nabto_stream_interface.h>
#include <streaming/nabto_stream_util.h>
#include <streaming/nabto_stream_memory.h>
#include <streaming/nabto_stream_log_helper.h>

#include <string.h>

int nabto_stream_get_stats(struct nabto_stream* stream, struct nabto_stream_stats* stats) {
    (void)stream; (void)stats;
    /* stream->stats.rttMin = (uint16_t)(stream->ccStats.rtt.min); */
    /* stream->stats.rttMax = (uint16_t)(stream->ccStats.rtt.max); */
    /* stream->stats.rttAvg = (uint16_t)(stream->ccStats.rtt.avg); */
    /* stream->stats.cwndMin = (uint16_t)(stream->ccStats.cwnd.min); */
    /* stream->stats.cwndMax = (uint16_t)(stream->ccStats.cwnd.max); */
    /* stream->stats.cwndAvg = (uint16_t)(stream->ccStats.cwnd.avg); */
    /* stream->stats.ssThresholdMin = (uint16_t)(stream->ccStats.ssThreshold.min); */
    /* stream->stats.ssThresholdMax = (uint16_t)(stream->ccStats.ssThreshold.max); */
    /* stream->stats.ssThresholdAvg = (uint16_t)(stream->ccStats.ssThreshold.avg); */
    /* stream->stats.flightSizeMin = (uint16_t)(stream->ccStats.flightSize.min); */
    /* stream->stats.flightSizeMax = (uint16_t)(stream->ccStats.flightSize.max); */
    /* stream->stats.flightSizeAvg = (uint16_t)(stream->ccStats.flightSize.avg); */
    /* *stats = stream->stats; */
    return 0;
}


nabto_stream_status nabto_stream_open(struct nabto_stream* stream, uint32_t contentType)
{
    SET_STATE(stream, ST_SYN_SENT);

    stream->timeoutStamp = nabto_stream_stamp_now();
    stream->xmitMaxAllocated = stream->startSequenceNumber;
    stream->maxAcked = stream->startSequenceNumber - 1;
    stream->contentType = contentType;
    nabto_stream_module_notify_event(stream, NABTO_STREAM_MODULE_EVENT_OPENED);
    return NABTO_STREAM_STATUS_OK;
}

nabto_stream_status nabto_stream_accept(struct nabto_stream* stream)
{
    if (!
        (stream->state == ST_WAIT_ACCEPT ||
         stream->state == ST_ACCEPT))
    {
        return NABTO_STREAM_STATUS_OK;
    }

    stream->xmitMaxAllocated = stream->startSequenceNumber;
    stream->maxAcked = stream->startSequenceNumber - 1;
    nabto_stream_allocate_next_send_segment(stream);
    nabto_stream_allocate_next_recv_segment(stream);

    SET_STATE(stream, ST_SYN_RCVD);
    nabto_stream_module_notify_event(stream, NABTO_STREAM_MODULE_EVENT_ACCEPTED);
    return NABTO_STREAM_STATUS_OK;
}

nabto_stream_status nabto_stream_write_buffer(struct nabto_stream* stream, const uint8_t* buffer, size_t bufferSize, size_t* written)
{
    if (stream->state == ST_ABORTED ||
        stream->state == ST_ABORTED_RST)
    {
        return NABTO_STREAM_STATUS_ABORTED;
    }

    NN_LOG_TRACE(stream->module->logger, NABTO_STREAM_LOG_MODULE, "nabto_stream_write_buffer %d", bufferSize);
    size_t queued = 0;

    if (!
        (stream->state == ST_ESTABLISHED ||
         stream->state == ST_CLOSE_WAIT ||
         stream->state == ST_SYN_RCVD)
        )
    {
        // invalid state the stream is either not opened yet or has been closed.
        return NABTO_STREAM_STATUS_CLOSED;
    }

    if (nabto_stream_can_write(stream) > 0) {
    // TODO add into last unsent segment if there's room for it.
        while (bufferSize)
        {
            struct nabto_stream_send_segment* segment = stream->nextUnfilledSendSegment;
            if (!segment) {
                break;
            }
            stream->nextUnfilledSendSegment = NULL;
            nabto_stream_allocate_next_send_segment(stream);
            stream->xmitMaxAllocated++;
            segment->seq = stream->xmitMaxAllocated;

            uint16_t sz;
            sz = (uint16_t)NABTO_STREAM_MIN(bufferSize, stream->maxSendSegmentSize);
            NN_LOG_TRACE(stream->module->logger, NABTO_STREAM_LOG_MODULE, "-------- nabto_stream_write %i bytes, seq=%" NN_LOG_PRIu32, sz, segment->seq);

            memcpy(segment->buf, (const void*) buffer, sz);
            segment->used = sz;
            segment->state = B_DATA;

            queued += sz;
            bufferSize -= sz;
            buffer += sz;

            if (stream->timeoutStamp.type == NABTO_STREAM_STAMP_INFINITE)
            {
                // Restart the data timeout timer.
                NN_LOG_TRACE(stream->module->logger, NABTO_STREAM_LOG_MODULE, "restart retransmission timer %" NN_LOG_PRIu16, stream->cCtrl.rto);
                stream->timeoutStamp = nabto_stream_get_future_stamp(stream, stream->cCtrl.rto);
            }

            // add segment to end of send queue
            nabto_stream_add_segment_to_send_list_before_elm(stream, stream->sendList, segment);
        }
    }

    if (queued) {
        nabto_stream_module_notify_event(stream, NABTO_STREAM_MODULE_EVENT_DATA_WRITTEN);
    }

    stream->sentBytes += queued;
    if (stream->timeFirstMBSent == 0 && stream->sentBytes >= 1048576) {
        stream->timeFirstMBSent = nabto_stream_get_duration(stream);
    }
    *written = queued;
    return NABTO_STREAM_STATUS_OK;
}


bool nabto_stream_has_more_read_data(struct nabto_stream* stream)
{
    if (stream->state == ST_ABORTED ||
        stream->state == ST_ABORTED_RST)
    {
        return false;
    }
    {
        struct nabto_stream_recv_segment* segment = stream->recvRead->next;
        if (segment == stream->recvRead) {
            return false;
        }
        if (segment->isFin) {
            return false;
        }
    }
    return true;
}

nabto_stream_status nabto_stream_read_buffer_once(struct nabto_stream* stream, uint8_t* buffer, size_t bufferSize, size_t* readen)
{
    if (stream->state == ST_ABORTED ||
        stream->state == ST_ABORTED_RST)
    {
        return NABTO_STREAM_STATUS_ABORTED;
    }

    {
        struct nabto_stream_recv_segment* segment = stream->recvRead->next;
        size_t avail;
        if (segment == stream->recvRead) {
            // no more data
            *readen = 0;
            if (stream->state >= ST_CLOSED) {
                return NABTO_STREAM_STATUS_CLOSED;
            } else {
                return NABTO_STREAM_STATUS_OK;
            }
        }

        if (segment->isFin) {
            return NABTO_STREAM_STATUS_EOF;
        }

        avail = (size_t)NABTO_STREAM_MIN((uint16_t)(segment->size - segment->used), bufferSize);

        memcpy(buffer, segment->buf + segment->used, avail);

        segment->used += (uint16_t)avail;
        *readen = avail;
        NN_LOG_TRACE(stream->module->logger, NABTO_STREAM_LOG_MODULE, "Retrieving data from seq=%" NN_LOG_PRIu32 " size=%i", segment->seq, (uint16_t)avail);
        if (segment->size == segment->used) {
            nabto_stream_remove_segment_from_recv_list(segment);
            nabto_stream_free_recv_segment(stream, segment);
            nabto_stream_module_notify_event(stream, NABTO_STREAM_MODULE_EVENT_DATA_READ);
        }

        return NABTO_STREAM_STATUS_OK;
    }
}

nabto_stream_status nabto_stream_read_buffer(struct nabto_stream* stream, uint8_t* buffer, size_t bufferSize, size_t* readen)
{
    size_t totalRead = 0;
    size_t read = 0;
    nabto_stream_status status = NABTO_STREAM_STATUS_OK;
    do {
        status = nabto_stream_read_buffer_once(stream, buffer, bufferSize, &read);
        if (status == NABTO_STREAM_STATUS_OK) {
            buffer += read;
            bufferSize -= read;
            totalRead += read;
        }
    } while (status == NABTO_STREAM_STATUS_OK && nabto_stream_has_more_read_data(stream) && bufferSize > 0);

    *readen = totalRead;

    return status;
}

nabto_stream_status nabto_stream_close(struct nabto_stream* stream)
{
    NN_LOG_TRACE(stream->module->logger, NABTO_STREAM_LOG_MODULE, "nabto_stream_close in state %s", nabto_stream_state_as_string(stream->state));
    nabto_stream_state state = stream->state;

    if (state == ST_IDLE) {
        return NABTO_STREAM_STATUS_INVALID_STATE;
    }

    if (state == ST_ABORTED_RST ||
        state == ST_ABORTED)
    {
        return NABTO_STREAM_STATUS_ABORTED;
    }

    if (state == ST_CLOSE_WAIT || state == ST_ESTABLISHED) {

        if (stream->unacked == stream->unacked->nextUnacked &&
            stream->sendList == stream->sendList->nextSend)
        {
            nabto_stream_add_fin_segment_to_send_lists(stream);
        }

        if (state == ST_CLOSE_WAIT) {
            SET_STATE(stream, ST_LAST_ACK);
        }

        if (state == ST_ESTABLISHED) {
            SET_STATE(stream, ST_FIN_WAIT_1);
        }
    } else {
        if (state < ST_ESTABLISHED) {
            // break opening protocol, send an rst
            SET_STATE(stream, ST_ABORTED);
        }
    }

    // return closed if we are in a state where our fin has been acked.
    if (state == ST_CLOSED ||
        state == ST_FIN_WAIT_2 ||
        state == ST_TIME_WAIT)
    {
        return NABTO_STREAM_STATUS_CLOSED;
    }

    // closing is in progress
    nabto_stream_module_notify_event(stream, NABTO_STREAM_MODULE_EVENT_CLOSE_CALLED);
    return NABTO_STREAM_STATUS_OK;
}

bool nabto_stream_stop_should_send_rst(struct nabto_stream* stream)
{
    if (stream->state == ST_CLOSED || stream->state == ST_IDLE || stream->state == ST_ABORTED_RST) {
        return false;
    }
    return true;
}

nabto_stream_status nabto_stream_set_application_event_callback(struct nabto_stream* stream, nabto_stream_application_event_callback cb, void* userData)
{
    stream->applicationEventCallbackData = userData;
    stream->applicationEventCallback = cb;
    return NABTO_STREAM_STATUS_OK;
}

uint32_t nabto_stream_get_content_type(struct nabto_stream* stream)
{
    return stream->contentType;
}
