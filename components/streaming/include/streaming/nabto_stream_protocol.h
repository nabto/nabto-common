#ifndef _NABTO_STREAM_PROTOCOL_H_
#define _NABTO_STREAM_PROTOCOL_H_


/**
 * all streaming packets starts with an uint16_t describing the stream
 * id.  clients is using even numbers and servers is using odd
 * numbers. A peer does know if it is the client or the server of a
 * connection.
 */

/**
 *  uint16_t streamId;
 *  uint8_t flags;
 *  uint32_t ack;
 *  uint32_t windowSize;
 *
 * flags:
 *  STREAM_FLAG_EMPTY 
 * 
 */

enum {
    NABTO_STREAM_FLAG_EMPTY = 0x00,
    NABTO_STREAM_FLAG_ACK   = 0x10,
    NABTO_STREAM_FLAG_RST   = 0x20,
    NABTO_STREAM_FLAG_FIN   = 0x40,
    NABTO_STREAM_FLAG_SYN   = 0x80
};

enum {
    NABTO_STREAM_APPLICATION_DATA_TYPE = 0x05
};

enum {
    NABTO_STREAM_EXTENSION_SEGMENT_SIZES   = 0x1000,
    NABTO_STREAM_EXTENSION_ACK             = 0x1001,
    NABTO_STREAM_EXTENSION_DATA            = 0x1002,
    NABTO_STREAM_EXTENSION_CONTENT_TYPE    = 0x1003,
    NABTO_STREAM_EXTENSION_FIN             = 0x1004,
    NABTO_STREAM_EXTENSION_EXTRA_DATA      = 0x1005,
    NABTO_STREAM_EXTENSION_SYN             = 0x1006
};

#define NABTO_STREAM_DATA_OVERHEAD 8

#define MAX_ACK_GAP_BLOCKS 64

#endif
