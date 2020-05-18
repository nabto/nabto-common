#include <streaming/nabto_stream_congestion_control.h>
#include <streaming/nabto_stream_window.h>
#include <streaming/nabto_stream_util.h>

#include <math.h>
#include <stdlib.h>

/**
 * Count not sent data
 */
uint32_t nabto_stream_congestion_control_not_sent_segments(struct nabto_stream* stream)
{
    return (uint32_t)stream->sendListSize;
}

void nabto_stream_congestion_control_init(struct nabto_stream* stream)
{
    stream->cCtrl.isFirstAck = true;
    stream->cCtrl.cwnd = NABTO_STREAM_CWND_INITIAL_VALUE;
    stream->cCtrl.srtt = NABTO_STREAM_DEFAULT_TIMEOUT;
    stream->cCtrl.rto =  NABTO_STREAM_DEFAULT_TIMEOUT;
    stream->cCtrl.ssThreshold = NABTO_STREAM_SLOW_START_INITIAL_VALUE;
}

void nabto_stream_congestion_control_adjust_ssthresh_after_triple_ack(struct nabto_stream* stream, struct nabto_stream_send_segment* segment) {
    if (!stream->cCtrl.lostSegment) {
        stream->cCtrl.ssThreshold = (uint32_t)NABTO_STREAM_MAX(stream->cCtrl.flightSize/2.0, NABTO_STREAM_SLOW_START_MIN_VALUE);
        NABTO_STREAM_LOG_TRACE(stream, "Setting ssThreshold: %" PRIu32 ", flightSize: %" PRIu32, stream->cCtrl.ssThreshold, stream->cCtrl.flightSize);
        nabto_stream_stats_observe(&stream->ccStats.ssThreshold, stream->cCtrl.ssThreshold);
        stream->cCtrl.lostSegment = true;
        stream->cCtrl.lostSegmentSeq = segment->seq;
    }
}

/**
 * Called after a streaming data packet has been resent.
 */
void nabto_stream_congestion_control_timeout(struct nabto_stream* stream) {
    NABTO_STREAM_LOG_TRACE(stream, "nabto_stream_congestion_control_timeout");
    // In the case of dual resending we cannot simply use cwnd as
    // a measure for the flight size since we could have reset it
    // in a previous resending.

    // After a timeout start with a fresh slow start
    stream->cCtrl.cwnd = NABTO_STREAM_CWND_INITIAL_VALUE;
    stream->cCtrl.ssThreshold = NABTO_STREAM_SLOW_START_INITIAL_VALUE;

    // A problem with karns algorithm is that a huge increase
    // of the delay can not be reflected in the
    // calculated srtt since all packets will timeout.
    // And hence no cleanly acked packets can be used for
    // the calculation of the srtt.

    stream->cCtrl.rto += stream->cCtrl.rto; // aka rto = rto*2 => exp backoff
    stream->cCtrl.isFirstAck = true;

    if (stream->cCtrl.rto > NABTO_STREAM_MAX_RETRANSMISSION_TIME) {
        stream->cCtrl.rto = NABTO_STREAM_MAX_RETRANSMISSION_TIME;
    }

    stream->timeouts++;
}


void nabto_stream_congestion_control_handle_ack(struct nabto_stream* stream, struct nabto_stream_send_segment* segment) {
    // nabto_stream_window.c will set the buffer to IDLE when this function
    // returns.

    if (stream->cCtrl.lostSegment && segment->seq == stream->cCtrl.lostSegmentSeq) {
        stream->cCtrl.lostSegment = false;
    }

    if (nabto_stream_congestion_control_use_slow_start(stream)) {
        NABTO_STREAM_LOG_TRACE(stream, "slow starting! %f", stream->cCtrl.cwnd);
        stream->cCtrl.cwnd += 2;
    } else {
        // congestion avoidance
        // flight size is never 0
        stream->cCtrl.cwnd += 1 + (1.0/stream->cCtrl.flightSize);
    }

    /**
     * Idea, limit cwnd by flight size, such that in a case where
     * the flight size decreases the streaming will perform a slow
     * start instead of a burst into the network.
     *
     * Problem, If all the acks comes in a burst the flight size
     * collapses and the streaming stalls. Conclusion, pacing is the
     * way forward.
     */

    stream->cCtrl.flightSize -= 1;

    nabto_stream_stats_observe(&stream->ccStats.cwnd, stream->cCtrl.cwnd);

    NABTO_STREAM_LOG_TRACE(stream, "adjusting cwnd: %f, ssThreshold: %" NABTO_STREAM_PRIu32 ", flightSize %d", stream->cCtrl.cwnd, stream->cCtrl.ssThreshold, stream->cCtrl.flightSize);

}


/**
 * Called before a data packet is sent to test if it's allowed to be sent.
 */
bool nabto_stream_congestion_control_can_send(struct nabto_stream* stream)
{
    // is_in_cwnd(stream);
    return (stream->cCtrl.cwnd > 0);
}

bool nabto_stream_congestion_control_use_slow_start(struct nabto_stream* stream) {
    return stream->cCtrl.flightSize < stream->cCtrl.ssThreshold;
}



/**
 * Accept more data if there is a chance that we can send it in the
 * near future.
 */
bool nabto_stream_congestion_control_accept_more_data(struct nabto_stream* stream) {
    if (stream->sendListSize <= stream->cCtrl.cwnd &&
        stream->sendListSize <= NABTO_STREAM_MAX_SEND_LIST_SIZE &&
        stream->cCtrl.flightSize <= NABTO_STREAM_MAX_FLIGHT_SIZE)
    {
        return true;
    } else {
        NABTO_STREAM_LOG_TRACE(stream, "Stream  does not accept more data notSent: %" NABTO_STREAM_PRIu32 ", cwnd %f", nabto_stream_congestion_control_not_sent_segments(stream), stream->cCtrl.cwnd);
        return false;
    }
}

void nabto_stream_update_congestion_control_receive_stats(struct nabto_stream* stream, struct nabto_stream_send_segment* segment, uint32_t timestampEcho)
{
    // only update timestamp if the ack is from a later time than the
    // last (re)transmission of the segment. If the segment has been
    // retransmitted but an ack is coming back with a timestamp
    // matching an earlier transmission, then we are not using the
    // packet to measure the rtt.

    if (nabto_stream_logical_stamp_less_or_equal(segment->logicalSentStamp, timestampEcho)) {
        // the ack is from after last retransmission of the packet so
        // we can use the timestamp to calculate rto
        nabto_stream_stamp sentTime = segment->sentStamp;
        double time = abs(nabto_stream_stamp_diff_now(stream, sentTime)); // now-sentTime

        if (stream->cCtrl.isFirstAck) {
            /**
             * RFC 2988:
             * SRTT <- R
             * RTTVAR <- R/2
             * RTO <- SRTT + max(G, k*RTTVAR)
             * k = 4
             * let G = 0;
             */
            stream->cCtrl.srtt = time;
            stream->cCtrl.rttVar = time/2;
            stream->cCtrl.isFirstAck = false;
        } else {
            /**
             * RFC 2988
             * RTTVAR <- (1-alpha) * RTTVAR + beta * | SRTT - R' |
             * SRTT <- (1-alpha) * SRTT + alpha * R'
             * alpha = 1/8, betal = 1/4
             */
            double alpha = 1.0/8;
            double beta = 1.0/4;
            stream->cCtrl.rttVar = (1.0-beta) * stream->cCtrl.rttVar + beta * fabs(stream->cCtrl.srtt - time);
            stream->cCtrl.srtt = (1.0-alpha) * stream->cCtrl.srtt + alpha * time;
        }
        nabto_stream_stats_observe(&stream->ccStats.rtt, time);
        stream->cCtrl.rto = (uint32_t)(stream->cCtrl.srtt + 4.0*stream->cCtrl.rttVar);

        NABTO_STREAM_LOG_TRACE(stream, "packet time %f, stream->srtt %f, stream->rttVar %f, stream->rto %" NABTO_STREAM_PRIu16, time, stream->cCtrl.srtt, stream->cCtrl.rttVar, stream->cCtrl.rto);


        /**
         * This will circumvent retransmissions in the case where
         * the client only sends acks for every second packet
         */
        stream->cCtrl.rto += 500;

        if (stream->cCtrl.rto > NABTO_STREAM_MAX_RETRANSMISSION_TIME) {
            stream->cCtrl.rto = NABTO_STREAM_MAX_RETRANSMISSION_TIME;
        }

        /**
         * RFC 2988 (2.4)
         * if rto < 1s, set rto = 1s
         *
         * This part of the RFC is omitted such that we are more robust
         * for bad networks.
         */
    }
}
