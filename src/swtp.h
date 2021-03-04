#ifndef __SWTP_H__
#define __SWTP_H__

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include <threads.h>

#define SWTP_MAX_PACKET_SIZE 1400
#define SWTP_MAX_WINDOW_SIZE 32768
#define SWTP_INITIAL_WINDOW_SIZE 1
#define SWTP_DEFAULT_RETRANSMIT_TIMEOUT 1000
#define SWTP_DEFAULT_DISCONNECT_TIMEOUT 10000
#define SWTP_DEFAULT_PING_TIMEOUT 2000
#define SWTP_DEFAULT_PING_PERIOD 1000

typedef enum {
    SWTP_PACKETTYPE_SWTCP,
    SWTP_PACKETTYPE_TUN
} swtp_packetType_t;

typedef struct {
    uint8_t buffer[SWTP_MAX_PACKET_SIZE];
    int length;
    struct timespec lastSendTime;
    swtp_packetType_t type;
    int sequenceNumber;
} swtp_packet_t;

typedef struct {
    int sWndSize;
    int sWndStartIndex;
    int sWndStartSequence;
    int sWndNextIndex;
    int sWndLength;
    int sWndNextSequence;
    int rWndNextIndex;
    swtp_packet_t *sWndBuffer;
    int retransmitTimeout;
    int disconnectTimeout;
    int pingTimeout;
    int pingPeriod;
    int pingsSent;
    struct timespec lastPacketReceivedTime;
    mtx_t mutex;
} swtp_pipe_t;

int swtp_init(swtp_pipe_t *pipe);
int swtp_onPacketReceived(swtp_pipe_t *pipe, const void *packetBuffer, size_t packetSize);
int swtp_onClockTick(swtp_pipe_t *pipe, const struct timespec *timespec);
int swtp_send(swtp_pipe_t *pipe, swtp_packetType_t packetType, const void *packetBuffer, size_t packetSize);

// Implementation defined
int swtp_onSendPacket(swtp_pipe_t *pipe, const void *packetBuffer, size_t packetSize);
int swtp_onReceivePacket(swtp_pipe_t *pipe, swtp_packetType_t packetType, const void *packetBuffer, size_t packetSize);

#endif
