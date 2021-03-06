#ifndef __LIBSWTP_SWTP_H_INCLUDED__
#define __LIBSWTP_SWTP_H_INCLUDED__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <threads.h>
#include <time.h>

#define SWTP_PORT 5228
#define SWTP_MAX_FRAME_SIZE 1500
#define SWTP_HEADER_SIZE 4
#define SWTP_MAX_PAYLOAD_SIZE (SWTP_MAX_FRAME_SIZE - SWTP_HEADER_SIZE)
#define SWTP_PING_TIMEOUT 5
#define SWTP_TIMEOUT 1
#define SWTP_MAXRETRY 3
#define SWTP_SUCCESS 0
#define SWTP_ERROR -1
#define SWTP_MAX_SEQUENCE_NUMBER 32767
#define SWTP_SEQUENCE_NUMBER_COUNT 32768
#define SWTP_MAX_WINDOW_SIZE 16384

#define ETHERTYPE_IPV4 0x0800
#define ETHERTYPE_IPV6 0x86dd
#define SWTLLP_SWTCP 0x00
#define SWTLLP_IPV4 0x01
#define SWTLLP_IPV6 0x02
#define MAXIMUM_MTU 1400
#define TUN_HEADER_SIZE 4
#define SWTLLP_HEADER_SIZE 1

enum {
    SWTP_DISCONNECTREASON_TIMEOUT,
    SWTP_DISCONNECTREASON_DISC
};

typedef struct {
    // The size of the frame (header included)
    size_t size;

    // The frame that is sent on the network
    struct {
        // The SWTP header
        uint8_t header[SWTP_HEADER_SIZE];

        // The payload carried by the SWTP frame (if any)
        uint8_t payload[SWTP_MAX_PAYLOAD_SIZE];
    } __attribute__((packed)) frame;

    // The time of the last time an attempt to send this frame was made.
    time_t lastSendAttemptTime;
} swtp_frame_t;

struct swtp_s;
typedef struct swtp_s swtp_t;

typedef void (*swtp_recvCallback_t)(swtp_t *swtp, const void *buffer, size_t size);
typedef void (*swtp_disconnectCallback_t)(swtp_t *swtp, int reason);

struct swtp_s {
    int socket;
    struct sockaddr socketAddress;
    swtp_recvCallback_t recvCallback;
    swtp_disconnectCallback_t disconnectCallback;

    mtx_t sendWindowMutex;

    swtp_frame_t *sendWindow;
    uint_least16_t sendWindowSize;
    uint_least16_t sendWindowStartIndex;
    uint_least16_t sendWindowStartSequenceNumber;
    uint_least16_t sendWindowLength;

    uint_least16_t expectedFrameNumber;

    time_t lastReceivedFrameTime;

    bool connected;
};

void swtp_init(swtp_t *swtp, int socket, const struct sockaddr *socketAddress);
int swtp_initSendWindow(swtp_t *swtp, uint_least16_t sendWindowSize);
void swtp_destroy(swtp_t *swtp);
int swtp_sendDataFrame(swtp_t *swtp, const void *buffer, size_t size);
swtp_frame_t *swtp_getSentFrame(const swtp_t *swtp, uint_least16_t seq);

/*
This function must be called by the application code whenever a SWTP packet is
received, so that it can "react".
*/
int swtp_onFrameReceived(swtp_t *swtp, const swtp_frame_t *frame);

/*
This function is responsible for checking the timeouts, therefore it must be
called periodically, between once and twice per second.
*/
int swtp_onTimerTick(swtp_t *swtp);

#endif
