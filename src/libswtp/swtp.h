#ifndef __LIBSWTP_SWTP_H_INCLUDED__
#define __LIBSWTP_SWTP_H_INCLUDED__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define SWTP_MAX_PAYLOAD_SIZE 1500

typedef struct {
    size_t size;
    uint8_t payload[SWTP_MAX_PAYLOAD_SIZE];
} swtp_frame_t;

typedef struct {
    int socket;

    uint_least16_t frameSendSequenceNumber : 15;
    uint_least16_t frameReceiveSequenceNumber : 15;

    swtp_frame_t *sendWindow;
    uint_least16_t sendWindowSize;
    uint_least16_t sendWindowStartIndex;
    uint_least16_t sendWindowStartSequenceNumber;
    uint_least16_t sendWindowLength;

    swtp_frame_t *recvWindow;
    uint_least16_t recvWindowSize;
    uint_least16_t recvWindowStartIndex;
    uint_least16_t recvWindowStartSequenceNumber;
    uint_least16_t recvWindowLength;

    time_t lastReceivedFrameTime;

    bool connected;
} swtp_t;

#endif
