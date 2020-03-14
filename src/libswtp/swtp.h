#ifndef __LIBSWTP_SWTP_H_INCLUDED__
#define __LIBSWTP_SWTP_H_INCLUDED__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define SWTP_PORT 5228
#define SWTP_MAX_PAYLOAD_SIZE 1500
#define SWTP_PING_TIMEOUT 30
#define SWTP_TIMEOUT 1
#define SWTP_MAXRETRY 3
#define SWTP_SUCCESS 0
#define SWTP_ERROR -1

struct swtp_s;

typedef void (*swtp_recvCallback)(void *arg, const void *buffer, size_t size);

typedef struct {
    size_t size;
    uint8_t payload[SWTP_MAX_PAYLOAD_SIZE];
} swtp_frame_t;

struct swtp_s {
    struct sockaddr socketAddress;

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
};

typedef struct swtp_s swtp_t;

int swtp_connect(swtp_t *swtp, const char *host);
int swtp_send(swtp_t *swtp, const void *buffer, size_t size);
int swtp_loop(swtp_t *swtp);
int swtp_disconnect(swtp_t *swtp);

#endif
