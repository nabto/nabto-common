/*
 * Copyright (C) Nabto - All Rights Reserved.
 */
#ifndef _NABTO_STREAM_H_
#define _NABTO_STREAM_H_
/**
 * @file
 * Nabto uServer stream  - Interface.
 *
 * Handling streams.
 */

#include "nabto_stream_types.h"
#include "nabto_stream_window.h"
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

struct nabto_stream;

/** Stream statisitics */
struct nabto_stream_stats {
    unsigned int sentPackets;
    unsigned int sentBytes;
    unsigned int sentResentPackets;
    unsigned int receivedPackets;
    unsigned int receivedBytes;
    unsigned int receivedResentPackets;
    unsigned int reorderedOrLostPackets;
    unsigned int userWrite;
    unsigned int userRead;
    nabto_stream_stamp streamStart;
    unsigned int timeouts;
    unsigned int timeFirstMBReceived; // ms, 0 if not set,
    unsigned int timeFirstMBSent; // ms, 0 if not set
    unsigned int rttMin;
    unsigned int rttMax;
    unsigned int rttAvg;
    unsigned int cwndMin;
    unsigned int cwndMax;
    unsigned int cwndAvg;
    unsigned int ssThresholdMin;
    unsigned int ssThresholdMax;
    unsigned int ssThresholdAvg;
    unsigned int flightSizeMin;
    unsigned int flightSizeMax;
    unsigned int flightSizeAvg;
    unsigned int sendSegmentAllocFailures;

};

#ifndef NABTO_STREAM_STATS_MAKE_PRINTABLE
#define NABTO_STREAM_STATS_MAKE_PRINTABLE(stats) (stats.sentPackets), (stats.sentBytes), (stats.sentResentPackets),(stats.receivedPackets), (stats.receivedBytes), (stats.receivedResentPackets), (stats.reorderedOrLostPackets), (stats.timeouts), (stats.rttAvg), (stats.cwndAvg), (stats.ssThresholdAvg), (stats.flightSizeAvg), (stats.sendSegmentAllocFailures)
#endif

#ifndef NABTO_STREAM_STATS_PRI
#define NABTO_STREAM_STATS_PRI "sentPackets: %u, sentBytes %u, sentResentPackets %u, receivedPackets %u, receivedBytes %u, receivedResentPackets %u, reorderedOrLostPackets %u, timeouts %u, rtt avg %u, cwnd avg %u, ssthreshold avg %u, flightSize avg %u, sendSegmentAllocFailures %u"
#endif

typedef enum
{
    NABTO_STREAM_STATUS_OK = 0,
    NABTO_STREAM_STATUS_CLOSED = 1,
    NABTO_STREAM_STATUS_EOF = 2,
    NABTO_STREAM_STATUS_ABORTED = 3,
    NABTO_STREAM_STATUS_INVALID_STATE = 4
} nabto_stream_status;

/**************** OPEN/CLOSE *******************************/

/**
 * open a stream
 *
 * Precondition: The stream structure has been initialized and
 * prepared for opening by giving it a tag and a connection.
 *
 * @param stream  stream resource
 * @param type
 */
nabto_stream_status nabto_stream_open(struct nabto_stream* stream, uint32_t contentType);

/**
 * Accept a stream
 *
 * return ok if the stream was accepted.
 * TODO: what if the user calls this function in an invalid state?
 */
nabto_stream_status nabto_stream_accept(struct nabto_stream* stream);

/**
 * Read the next byte from the stream
 */
nabto_stream_status nabto_stream_read_byte(struct nabto_stream* stream, uint8_t* byte);

/**
 * Read from the stream into a buffer
 *
 * @return NABTO_STREAM_STATUS_OK      if we read data or 0 bytes if no more data is available.
 *         NABTO_STREAM_STATUS_EOF     if the stream is eof.
 *         NABTO_STREAM_STATUS_ABORTED if the stream is aborted.
 */
nabto_stream_status nabto_stream_read_buffer(struct nabto_stream* stream, uint8_t* buffer, size_t bufferSize, size_t* readen);

/**
 * Write to a stream
 *
 * @return NABTO_STREAM_STATUS_OK              if ok
 *         NABTO_STREAM_STATUS_CLOSED          if stream is closed for further writing.
 *         NABTO_STREAM_STATUS_ABORTED         if stream is aborted.
 */
nabto_stream_status nabto_stream_write_buffer(struct nabto_stream* stream, const uint8_t* buffer, size_t bufferSize, size_t* written);

/**
 * Close a stream for more data to be written.
 *
 * When called the stream will try to send the rest of the queued data
 * in the stream, then send a fin, when the fin has been acked, the
 * stream is closed from this end. It will then wait for a fin from
 * the other end. When such a fin is returned the stream goes to the
 * CLOSED state. If there's a risk that not every data on the stream
 * has not been acked then the stream will report
 * NABTO_STREAM_STATUS_ABORTED
 *
 * @return NABTO_STREAM_STATUS_OK       if the stream is closing.
 *         NABTO_STREAM_STATUS_CLOSED   if the stream is closed for writing.
 *         NABTO_STREAM_STATUS_ABORTED  if the stream is likely to have been closed without acks on all data.
 *
 */
nabto_stream_status nabto_stream_close(struct nabto_stream* stream);

/**
 * Ask the stream if it is being stopped now, should it then manager
 * then send a rst packet on behalf of the stream. I.e. inform the
 * other end that the stream has been aborted.
 *
 * @return true iff a rst should be sent on behalf of the stream.
 */
bool nabto_stream_stop_should_send_rst(struct nabto_stream* stream);

/**
 * Set event callback for the stream.  This is used to implement an
 * event driven application.
 */
nabto_stream_status nabto_stream_set_application_event_callback(struct nabto_stream* stream, nabto_stream_application_event_callback cb, void* userData);

/**
 * Limit a stream to a maximum of send segments. If the new max is
 * less than the current used segments it will take some time for the
 * system to reach the new value.
 */
nabto_stream_status nabto_stream_set_max_send_segments(struct nabto_stream* stream, uint32_t maxSegments);

/**
 * Limit a stream to a maximum of recv segments. If the new max is
 * less then the current used segments, it will take some time for the
 * system to reach the new max value.
 */
nabto_stream_status nabto_stream_set_max_recv_segments(struct nabto_stream* stream, uint32_t recvSegments);

/**
 * Get Content type for a stream
 */
uint32_t nabto_stream_get_content_type(struct nabto_stream* stream);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
