/*
 * Copyright (C) 2008-2014 Nabto - All Rights Reserved.
 */
/**
 * @file
 * Nabto uServer stream packet events from an unreliable con - Prototypes.
 *
 * Handling data packets from an unreliable  stream.
 */

#ifndef _NABTO_STREAM_WINDOW_H_
#define _NABTO_STREAM_WINDOW_H_

#include "nabto_stream_config.h"
#include "nabto_stream_types.h"
#include "nabto_stream_log.h"


#include <stddef.h>
#include <stdarg.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif



enum {
    NABTO_STREAM_DEFAULT_TIMEOUT = 1000,
    NABTO_STREAM_MAX_RETRANSMISSION_TIME = 16000,
    NABTO_STREAM_DELAYED_ACK_WAIT = 25,
    NABTO_STREAM_DEFAULT_MAX_SEND_SEGMENT_SIZE = 256,
    NABTO_STREAM_DEFAULT_MAX_RECV_SEGMENT_SIZE = 256,
    NABTO_STREAM_WINDOW_SIZE_INF = 424242,
    NABTO_STREAM_SLOW_START_INITIAL_VALUE = 0x7fffffff,
    NABTO_STREAM_SLOW_START_MIN_VALUE = 2*4, /* 2 packets with up to 4 segments in each packet */
    NABTO_STREAM_SEGMENT_ALLOCATION_RETRY_INTERVAL = 20,
    NABTO_STREAM_CWND_INITIAL_VALUE = 2*4, /* 2 packets with up to 4 segments in each packet */
    NABTO_STREAM_MAX_FLIGHT_SIZE = 10000,
    NABTO_STREAM_MAX_SEND_LIST_SIZE = 100
};

struct nabto_stream_syn_extra_data {
    uint8_t* data;
    size_t dataLength;
};

struct nabto_stream_syn_req {
    uint32_t ackSeq;
    uint16_t sendChunkSize;
    uint16_t recvChunkSize;
    uint32_t initialRecvWindowSize;
    uint32_t payloadPrototolIdentifier;
    struct nabto_stream_syn_extra_data extraData;
};


enum nabto_stream_timestamp_type {
    NABTO_STREAM_STAMP_NOW, // timestamp is timed out now.
    NABTO_STREAM_STAMP_INFINITE, // timestamp is infinite in the future
    NABTO_STREAM_STAMP_FUTURE
};


typedef struct {
    enum nabto_stream_timestamp_type type;
    uint32_t stamp;
} nabto_stream_stamp;

struct nabto_stream_header {
    uint8_t flags;
    uint32_t timestampValue;
};

struct nabto_stream_syn_request {
    uint32_t seq;
    uint32_t contentType;
    uint16_t maxSendSegmentSize;
    uint16_t maxRecvSegmentSize;
};

struct nabto_stream_syn_ack_request {
    uint32_t seq;
    uint16_t maxSendSegmentSize;
    uint16_t maxRecvSegmentSize;
};

enum nabto_stream_next_event_type {
    ET_ACCEPT, // add the stream to the accept queue.
    ET_ACK, // send ack imediately
    ET_SYN, // send a syn packet
    ET_SYN_ACK, // send a syn | ack packet
    ET_DATA, // send a data packet
    ET_TIMEOUT, // handle a timeout
    ET_RST, // send rst
    ET_APPLICATION_EVENT, // handle an event
    ET_TIME_WAIT, // handle ET_TIME_WAIT state
    ET_WAIT, // wait for a timeout to happen.
    ET_NOTHING, // no next event, need user action or network input.
    ET_CLOSED // no more actions the stream is closed.
};

/** Stream Transfer Control Block states */
typedef enum {
    ST_IDLE,          ///< Before being used. Means it's a fresh resource.
    ST_ACCEPT,        ///< add the stream to the accept queue
    ST_WAIT_ACCEPT,   ///< Waiting for the stream to be accepted by the user.
    ST_SYN_SENT,      ///< Opened by this end.
    ST_SYN_RCVD,      ///< Opened as listener.
    ST_ESTABLISHED,   ///< Open waiting for, close or FIN.

    ST_FIN_WAIT_1,    ///< We have received a close from the user.
    ST_FIN_WAIT_2,    ///< Our FIN has been acked. Waiting for a FIN from the peer.
    ST_CLOSING,       ///< We have received a close from the user and afterwards we got a FIN from the peer.
    ST_TIME_WAIT,     ///< Waiting for retransmitted FINs, e.g. if the last sent ack to the peer was lost.

    ST_CLOSE_WAIT,    ///< We have received a FIN from the peer and waits for the user to close the stream.
    ST_LAST_ACK,      ///< We have received a close from the User but waits for our FIN to be acknowledged.

    ST_CLOSED,        ///< The stream is closed, the application can now safely release the stream resource.
    // if the connection is force closed from the user goto this
    // state. when the rst packet is sent go to ST_ABORTED_RST
    ST_ABORTED, ///< The stream has been uncleanly closed, it's possible that data loss has happened.
    // If the connection is aborted not by an rst packet go to this state.
    ST_ABORTED_RST ///< The stream has been uncleanly closed, it's possible that data loss has happened. Go to this state after we have received an RST packet or when we have sent an RST packet. no further packets is sent from this state.
} nabto_stream_state;


typedef enum {
    RB_IDLE,
    RB_DATA
} nabto_stream_recv_buffer_state;

struct nabto_stream_recv_segment;

/** Receive buffer */
struct nabto_stream_recv_segment {
    uint32_t seq;                   /**< sequence number                  */
    nabto_stream_recv_buffer_state state;
    bool     isFin;                 /**< true if it's a fin segment       */
    uint16_t capacity;              /**< capacity of the buffer           */
    uint16_t size;                  /**< number of bytes in buffer        */
    uint16_t used;                  /**< number of bytes read by the user */
    uint8_t* buf;                   /**< buffer                           */

    // next in the list of received segments
    struct nabto_stream_recv_segment* next;
    struct nabto_stream_recv_segment* prev;
};


/** Buffer state */
typedef enum {
    B_IDLE, /**< unused        */
    B_DATA, /**< data not sent */
    B_SENT  /**< data sent     */
} b_state_t;


struct nabto_stream_send_segment;

/** Transmit buffer */
struct nabto_stream_send_segment {
    uint32_t      seq;                   /**< sequence number        */
    uint16_t      used;                  /**< number of bytes        */
    uint16_t      capacity;              /**< capacity of the buffer */
    uint8_t*      buf;                   /**< buffer                 */
    b_state_t     state;                 /**< state                  */
    nabto_stream_stamp sentStamp;        /**< When was the data sent. Used to measure rtt */
    uint32_t      logicalSentStamp;
    uint16_t      ackedAfter;            /**< Number of buffers which
                                          * has been acked which is
                                          * after this buffer in the
                                          * window. */

    /**
     * A segment starts in the send list, when it's sent it's added to
     * the unacked list, from the unacked list the segment can be
     * added to the resend list if it has to be resend.
     */
    // Send list.
    struct nabto_stream_send_segment* nextSend;
    struct nabto_stream_send_segment* prevSend;

    // Unacked list.
    struct nabto_stream_send_segment* nextUnacked;
    struct nabto_stream_send_segment* prevUnacked;

    // Resend list
    struct nabto_stream_send_segment* nextResend;
    struct nabto_stream_send_segment* prevResend;

};


// structure which can tell some statistics about a double.

struct nabto_stats {
    double min;
    double max;
    double avg;
    double count;
};

typedef struct {
    struct nabto_stats rtt; // rount trip time
    struct nabto_stats cwnd; // congestion window size.
    struct nabto_stats ssThreshold; // slow start threshold
    struct nabto_stats flightSize; // data on the line
} nabto_stream_congestion_control_stats;

typedef struct {
    double        srtt;          ///< Smoothed round trip time.
    double        rttVar;        ///< Round trip time variance.
    uint32_t      rto;           ///< Retransmission timeout.
    bool          isFirstAck;    ///< True when the first ack has not
                                 ///been received. Or after a timeout.
    double        cwnd;          ///< Tokens available for sending data
    uint32_t      ssThreshold;   ///< Slow start threshold
    uint32_t      flightSize;    ///< Gauge of sent but not acked buffers. Aka flight size.
    bool          lostSegment;   ///< True if a segment has been lost
                                 ///and we are running the fast
                                 ///retransmit / fast recovery
                                 ///algorithm
    uint32_t      lostSegmentSeq; ///< sequence number of lost segment, when it is acked we reset the lostSegment variable.
} nabto_stream_congestion_control;

enum nabto_stream_module_event {
    NABTO_STREAM_MODULE_EVENT_DATA_WRITTEN,
    NABTO_STREAM_MODULE_EVENT_DATA_READ,
    NABTO_STREAM_MODULE_EVENT_CLOSE_CALLED,
    NABTO_STREAM_MODULE_EVENT_OPENED,
    NABTO_STREAM_MODULE_EVENT_ACCEPTED,
};


struct nabto_stream_module {
    uint32_t (*get_stamp)(void* userData);
    // userData in front since
    void (*log)(const char* file, int line, enum nabto_stream_log_level level, const char* fmt, va_list args, void* userData);
    struct nabto_stream_send_segment* (*alloc_send_segment)(size_t bufferSize, void* userData);
    void (*free_send_segment)(struct nabto_stream_send_segment* segment, void* userData);
    /* void (*add_to_send_segment_wait_list)(struct nabto_stream* stream, void* userData); */
    /* void (*remove_from_send_segment_wait_list)(struct nabto_stream* stream, void* userData); */

    struct nabto_stream_recv_segment* (*alloc_recv_segment)(size_t bufferSize, void* userData);
    void (*free_recv_segment)(struct nabto_stream_recv_segment* segment, void* userData);
    /* void (*add_to_recv_segment_wait_list)(struct nabto_stream* stream, void* userData); */
    /* void (*remove_from_recv_segment_wait_list)(struct nabto_stream* stream, void* userData); */

    /**
     * Notify the stream backend that the user did something which
     * altered the stream state.
     */
    void (*notify_event)(enum nabto_stream_module_event event, void* userData);
};


typedef enum {
    /**
     * A stream we have called nabto_stream_open on has been opened.
     */
    NABTO_STREAM_APPLICATION_EVENT_TYPE_OPENED,
    /**
     * Data is ready on the stream for the application to consume.
     */
    NABTO_STREAM_APPLICATION_EVENT_TYPE_DATA_READY,
    /**
     * More data can be written to the stream.
     */
    NABTO_STREAM_APPLICATION_EVENT_TYPE_DATA_WRITE,
    /**
     * No more data can be read from the stream since the stream has
     * been closed by the other end.
     */
    NABTO_STREAM_APPLICATION_EVENT_TYPE_READ_CLOSED,
    /**
     * No more data can be written from our end to the stream since we
     * have closed the stream.
     */
    NABTO_STREAM_APPLICATION_EVENT_TYPE_WRITE_CLOSED,
    /**
     * The stream is closed, no more data can be written or readen
     */
    NABTO_STREAM_APPLICATION_EVENT_TYPE_CLOSED,
} nabto_stream_application_event_type;


typedef void (*nabto_stream_application_event_callback)(nabto_stream_application_event_type eventType, void* data);

/** Stream Transfer Control Block */
struct nabto_stream {

    struct nabto_stream_module*        module;
    void* moduleUserData;
    uint32_t contentType;
    uint16_t maxSendSegmentSize;
    uint16_t maxRecvSegmentSize;
    nabto_stream_state                 state;            /**< state                   */

    uint16_t                           retransCount;     /**< Retransmission count,
                                                              First used for retransmission of SYN,
                                                              Then retransmission of FIN, and lastly used for
                                                              waiting in ST_TIME_WAIT. */
    /**
     * The sequence number to use for the syn packet aka the start
     * sequence number. This is primarily used in situations where we
     * want to test sequence numbers wrapping around.
     **/
    uint32_t                           startSequenceNumber;

    /**
     * The timeoutStamp is used to signal if the stream window should
     * do something. It's both used as regular timeout and a timeout
     * telling something to happen imidiately. This timeout is not
     * used for retransmission of regular data.
     */
    nabto_stream_stamp                   timeoutStamp;

    bool                                 segmentSentAfterTimeout;
    uint32_t                             maxAdvertisedWindow;

    uint32_t                             timestampToEcho;          /**< The latest seen timestamp from the other end. */

    bool                                 windowHasOpened;     // the window of the receiver has opened up for more data to be sent.

    bool                                 sendAck;
    bool                                 imediateAck;
    uint32_t                             logicalTimestamp;

    /**
     * data between xmitFirst and xmitLastSent has not been acked yet
     * invariant xmitSeq >= xmitLastSent >= xmitFirst
     *
     * ready data = xmitFist .. xmitSeq
     *
     * data ready for first transmission = xmitLastSent .. xmitSeq
     *
     * not acked data = xmitFirst .. xmitLastSent
     *
     * old xmitCount = xmitSeq - xmitFirst
     */
    // List of all unacked sent segments.
    struct nabto_stream_send_segment unackedSentinel;
    struct nabto_stream_send_segment* unacked;
    size_t unackedSize;

    // Segments queued, but not yet sent
    struct nabto_stream_send_segment sendListSentinel;
    struct nabto_stream_send_segment* sendList;
    size_t sendListSize;

    // Segments queued to be resent. These segments is also in the unacked list.
    struct nabto_stream_send_segment resendListSentinel;
    struct nabto_stream_send_segment* resendList;

    /**
     * When sending a fin packet use this segment.
     */
    struct nabto_stream_send_segment finSegment;

    struct nabto_stream_send_segment* nextUnfilledSendSegment;
    // if the allocation fails this is the next time to try to
    // allocate a send segment;
    nabto_stream_stamp                sendSegmentAllocationStamp;

    uint32_t                        maxAcked;               /**< max acked sequence number, not cumulative acked.*/
    uint32_t                        xmitMaxAllocated;       /**< max allocated seq of xmit buffers */

    // next segment to receive data from, from the application. the
    // segment is removed when the data is used. If there's no data
    // the variable is NULL.

    // cumulative acknowledged recv data ready to be consumed by the
    // reader.
    struct nabto_stream_recv_segment recvReadSentinel;
    struct nabto_stream_recv_segment* recvRead;

    // recv window partially filled with data from the other peer.
    struct nabto_stream_recv_segment recvWindowSentinel;
    struct nabto_stream_recv_segment* recvWindow;

    // If the allocation of a recv segment fails, this is the time
    // where we try to allocate a segment again.
    nabto_stream_stamp                recvSegmentAllocationStamp;

    uint32_t                        recvTop;                /**< highest cumulative filled recv slots. */
    uint32_t                        recvMax;                /**< max sequence number of used  recv slot. aka the value of maxAcked to send. */
    uint32_t                        recvMaxAllocated;

    nabto_stream_congestion_control cCtrl;
    nabto_stream_congestion_control_stats ccStats;

    // Receiving packets designated by a sequence number 'seq'
    // -------------------------------------------------------
    // The window is the 'seq' range [recvNext .. recvNext+cfg.recvWinSize[
    //
    // seq in range 'window' are valid received packets
    //
    // The user is reading data from slot 'seq', thus this slot isn't acked to the peer yet.
    // This means that packets in 'window' may be received more than once, if retransmitted by the peer
    //
    // If the slot 'recvNext' is empty, the peer is known to have sent packets in range
    // [recvNext-cfg.recvWinSize .. recvNext[
    // and if the slot 'recvNext' isn't empty, the peer is known to have sent packet in range
    // ]recvNext-cfg.recvWinSize .. recvNext]
    // Receiving these packets may be caused by the ack's to the peer being lost,
    // thus a new ack should be sent.


    struct {
        // stream has been opened
        bool                          opened : 1;
        // data is ready to be read by the application
        bool                          dataReady : 1;
        // data can be writen to the stream
        bool                          dataWrite : 1;
        // no more data can be read
        bool                          readClosed : 1;
        // the fin has been acked by the remote peer
        bool                          writeClosed : 1;
        // stream is closed in both directions.
        bool                          closed : 1;
    } applicationEvents;

    nabto_stream_application_event_callback applicationEventCallback;
    void* applicationEventCallbackData;

    uint64_t receivedBytes;
    uint64_t sentBytes;
    uint32_t timeFirstMBReceived; // in ms
    uint32_t timeFirstMBSent; // in ms
    uint32_t reorderedOrLostPackets;
    uint32_t timeouts;
    nabto_stream_stamp streamStart;
};

/******************************************************************************/

/**
 * Determine number of bytes that can be written
 */
size_t nabto_stream_can_write(struct nabto_stream * stream);

/******************************************************************************/

void nabto_stream_handle_syn(struct nabto_stream* stream, struct nabto_stream_header* hdr, struct nabto_stream_syn_request* req);
void nabto_stream_handle_syn_ack(struct nabto_stream* stream, struct nabto_stream_header* hdr, struct nabto_stream_syn_ack_request* req);

void nabto_stream_handle_fin(struct nabto_stream* stream, uint32_t seq);

void nabto_stream_handle_data(struct nabto_stream* stream, uint32_t seq, const uint8_t* ptr, uint16_t length);

void nabto_stream_handle_rst(struct nabto_stream* stream);

/**
 * Notify stream that it's connection has been released.
 */
void nabto_stream_on_connection_released(struct nabto_stream * stream);

bool nabto_stream_is_closed(struct nabto_stream * stream);

/**
 * Update time stamp with the time until next event
 */
nabto_stream_stamp nabto_stream_next_event(struct nabto_stream * stream);

/******************************************************************************/
/******************************************************************************/


/**
 * Send stream statistics packet
 */
void nabto_stream_send_stats(struct nabto_stream* stream, uint8_t event);


/**
 * observe a value.
 */
void nabto_stream_stats_observe(struct nabto_stats* stat, double value);

/**
 * get stream duration in ms
 */
uint32_t nabto_stream_get_duration(struct nabto_stream * stream);

void nabto_stream_init(struct nabto_stream* stream, struct nabto_stream_module* module, void* moduleUserData);
void nabto_stream_destroy(struct nabto_stream* stream);

void nabto_stream_segment_has_been_acked(struct nabto_stream* stream, struct nabto_stream_send_segment* segment);

void nabto_stream_move_segments_from_recv_window_to_recv_read(struct nabto_stream* stream);

uint32_t nabto_stream_get_recv_window_size(struct nabto_stream* stream);

struct nabto_stream_send_segment* nabto_stream_handle_ack_iterator(struct nabto_stream* stream, uint32_t seq, struct nabto_stream_send_segment* iterator, uint32_t timestampEcho);

struct nabto_stream_send_segment* nabto_stream_handle_nack_iterator(struct nabto_stream* stream, uint32_t seq, struct nabto_stream_send_segment* iterator, uint32_t timestampEcho);

struct nabto_stream_recv_segment* nabto_stream_recv_max_segment(struct nabto_stream* stream);

enum nabto_stream_next_event_type nabto_stream_next_event_to_handle(struct nabto_stream* stream);

void nabto_stream_event_handled(struct nabto_stream* stream, enum nabto_stream_next_event_type eventType);

void nabto_stream_dispatch_event(struct nabto_stream* stream);

void nabto_stream_handle_ack_on_syn_ack(struct nabto_stream* stream);

void nabto_stream_handle_timeout(struct nabto_stream* stream);

void nabto_stream_set_start_sequence_number(struct nabto_stream* stream, uint32_t seq);

void nabto_stream_handle_time_wait(struct nabto_stream* stream);

void nabto_stream_connection_died(struct nabto_stream* stream);

void nabto_stream_add_fin_segment_to_send_lists(struct nabto_stream* stream);

void nabto_stream_state_transition(struct nabto_stream* stream, nabto_stream_state new_state);

void nabto_stream_mark_segment_for_retransmission(struct nabto_stream* stream, struct nabto_stream_send_segment* xbuf);


/**
 * Set state and log state change. @param s  the stream. @param newst  the new state
 */
#define SET_STATE(s, newst) \
    if (s->state != newst) { \
        NABTO_STREAM_LOG_TRACE(s, " STATE: %s -> %s", nabto_stream_state_as_string(s->state), nabto_stream_state_as_string(newst)); \
        nabto_stream_state_transition(s, newst); \
    }


#ifdef __cplusplus
} // extern "C"
#endif

#endif
