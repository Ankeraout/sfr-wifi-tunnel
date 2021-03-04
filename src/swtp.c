#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>

#include "swtp.h"

#define debug printf

int swtp_init(swtp_pipe_t *pipe);
int swtp_onPacketReceived(swtp_pipe_t *pipe, const void *packetBuffer, size_t packetSize);
int swtp_onClockTick(swtp_pipe_t *pipe, const struct timespec *timespec);
int swtp_send(swtp_pipe_t *pipe, swtp_packetType_t packetType, const void *packetBuffer, size_t packetSize);
static int swtp_onPacketReceived_data(swtp_pipe_t *pipe, const void *packetBuffer, size_t packetSize);
static int swtp_onPacketReceived_test(swtp_pipe_t *pipe, const void *packetBuffer, size_t packetSize);
static int swtp_onPacketReceived_rej(swtp_pipe_t *pipe, const void *packetBuffer, size_t packetSize);
static int swtp_onPacketReceived_ack(swtp_pipe_t *pipe, const void *packetBuffer, size_t packetSize);
static int swtp_sendACK(swtp_pipe_t *pipe);
static int swtp_sendTEST(swtp_pipe_t *pipe);
static void swtp_acknowledge(swtp_pipe_t *pipe, int sequenceNumber);
static void swtp_retransmit(swtp_pipe_t *pipe);
static void swtp_sendPacket(swtp_pipe_t *pipe, swtp_packet_t *packet);
static int timeDiff(const struct timespec *t1, const struct timespec *t2);

int swtp_init(swtp_pipe_t *pipe) {
    struct timespec currentTime;

    timespec_get(&currentTime, TIME_UTC);

    pipe->sWndSize = 1;
    pipe->sWndStartIndex = 0;
    pipe->sWndStartSequence = 0;
    pipe->sWndNextIndex = 0;
    pipe->sWndLength = 0;
    pipe->sWndBuffer = malloc(sizeof(swtp_packet_t) * SWTP_INITIAL_WINDOW_SIZE);
    pipe->rWndNextIndex = 0;
    pipe->retransmitTimeout = SWTP_DEFAULT_RETRANSMIT_TIMEOUT;
    pipe->disconnectTimeout = SWTP_DEFAULT_DISCONNECT_TIMEOUT;
    pipe->pingTimeout = SWTP_DEFAULT_PING_TIMEOUT;
    pipe->pingPeriod = SWTP_DEFAULT_PING_PERIOD;
    pipe->pingsSent = 0;
    pipe->lastPacketReceivedTime = currentTime;
    
    mtx_init(&pipe->mutex, mtx_plain);

    debug("swtp: new tunnel created\n");

    return 0;
}

int swtp_onPacketReceived(swtp_pipe_t *pipe, const void *packetBuffer, size_t packetSize) {
    struct timespec currentTime;

    timespec_get(&currentTime, TIME_UTC);

    pipe->lastPacketReceivedTime = currentTime;
    pipe->pingsSent = 0;

    uint32_t header = ntohl(*(uint32_t *)packetBuffer);

    switch(header >> 30) {
        case 0: return swtp_onPacketReceived_data(pipe, packetBuffer, packetSize);
        case 1: return swtp_onPacketReceived_test(pipe, packetBuffer, packetSize);
        case 2: return swtp_onPacketReceived_rej(pipe, packetBuffer, packetSize);
        case 3: return swtp_onPacketReceived_ack(pipe, packetBuffer, packetSize);
    }

    return 0;
}

static int swtp_onPacketReceived_data(swtp_pipe_t *pipe, const void *packetBuffer, size_t packetSize) {
    uint32_t header = ntohl(*(uint32_t *)packetBuffer);

    uint16_t sendSequenceNumber = (header & 0x3fff8000) >> 15;
    uint16_t receiveSequenceNumber = header & 0x00007fff;

    debug("swtp: > DATA 0x%04x 0x%04x\n", sendSequenceNumber, receiveSequenceNumber);

    mtx_lock(&pipe->mutex);

    swtp_acknowledge(pipe, receiveSequenceNumber);

    if(sendSequenceNumber == pipe->rWndNextIndex) {
        pipe->rWndNextIndex = (pipe->rWndNextIndex + 1) & 0x7fff;

        swtp_sendACK(pipe);

        mtx_unlock(&pipe->mutex);

        uint32_t packetType = ntohl(((uint32_t *)packetBuffer)[1]);

        swtp_onReceivePacket(pipe, packetType, ((uint8_t *)packetBuffer) + 8, packetSize - 8);
    } else {
        debug("swtp: < REJ 0x%04x\n", pipe->rWndNextIndex);

        uint32_t buffer = htonl(0x80000000 | pipe->rWndNextIndex);

        swtp_onSendPacket(pipe, &buffer, 4);

        mtx_unlock(&pipe->mutex);
    }

    return 0;
}

static int swtp_onPacketReceived_test(swtp_pipe_t *pipe, const void *packetBuffer, size_t packetSize) {
    mtx_lock(&pipe->mutex);

    uint32_t header = ntohl(*(uint32_t *)packetBuffer);

    uint16_t receiveSequenceNumber = header & 0x00007fff;

    debug("swtp: > TEST 0x%04x\n", receiveSequenceNumber);

    swtp_acknowledge(pipe, receiveSequenceNumber);

    swtp_sendACK(pipe);
    
    mtx_unlock(&pipe->mutex);

    return 0;
}

static int swtp_onPacketReceived_rej(swtp_pipe_t *pipe, const void *packetBuffer, size_t packetSize) {
    mtx_lock(&pipe->mutex);

    uint32_t header = ntohl(*(uint32_t *)packetBuffer);

    uint16_t receiveSequenceNumber = header & 0x00007fff;

    debug("swtp: > REJ 0x%04x\n", receiveSequenceNumber);

    swtp_acknowledge(pipe, receiveSequenceNumber);
    swtp_retransmit(pipe);

    mtx_unlock(&pipe->mutex);

    return 0;
}

static int swtp_onPacketReceived_ack(swtp_pipe_t *pipe, const void *packetBuffer, size_t packetSize) {
    mtx_lock(&pipe->mutex);

    uint32_t header = ntohl(*(uint32_t *)packetBuffer);

    uint16_t receiveSequenceNumber = header & 0x00007fff;

    debug("swtp: > ACK 0x%04x\n", receiveSequenceNumber);

    swtp_acknowledge(pipe, receiveSequenceNumber);

    mtx_unlock(&pipe->mutex);

    return 0;
}

int swtp_onClockTick(swtp_pipe_t *pipe, const struct timespec *timespec) {
    mtx_lock(&pipe->mutex);

    if(pipe->sWndLength > 0) {
        if(timeDiff(timespec, &pipe->sWndBuffer[0].lastSendTime) >= pipe->retransmitTimeout) {
            swtp_retransmit(pipe);
        }
    }

    if((timeDiff(&pipe->lastPacketReceivedTime, timespec) - (pipe->pingsSent * pipe->pingPeriod)) >= pipe->pingTimeout) {
        swtp_sendTEST(pipe);
    }

    if(timeDiff(&pipe->lastPacketReceivedTime, timespec) >= pipe->disconnectTimeout) {
        // TODO: disconnect
    }

    mtx_unlock(&pipe->mutex);

    return 0;
}

int swtp_send(swtp_pipe_t *pipe, swtp_packetType_t packetType, const void *packetBuffer, size_t packetSize) {
    struct timespec currentTime;

    timespec_get(&currentTime, TIME_UTC);

    mtx_lock(&pipe->mutex);

    if(pipe->sWndSize <= pipe->sWndLength) {
        // Window is full, discard packet
        mtx_unlock(&pipe->mutex);
        return 0;
    }

    // Add packet to window
    memcpy(pipe->sWndBuffer[pipe->sWndNextIndex].buffer, packetBuffer, packetSize);
    pipe->sWndBuffer[pipe->sWndNextIndex].lastSendTime = currentTime;
    pipe->sWndBuffer[pipe->sWndNextIndex].length = packetSize;
    pipe->sWndBuffer[pipe->sWndNextIndex].type = packetType;
    pipe->sWndBuffer[pipe->sWndNextIndex].sequenceNumber = pipe->sWndNextSequence;

    swtp_sendPacket(pipe, &pipe->sWndBuffer[pipe->sWndNextIndex]);

    pipe->sWndNextSequence = (pipe->sWndNextSequence + 1) & 0x7fff;
    pipe->sWndNextIndex = (pipe->sWndNextIndex + 1) % pipe->sWndSize;
    pipe->sWndLength++;

    mtx_unlock(&pipe->mutex);

    return 0;
}

static int swtp_sendACK(swtp_pipe_t *pipe) {
    uint32_t buffer = htonl(0xc0000000 | pipe->rWndNextIndex);

    debug("swtp: < ACK 0x%04x\n", pipe->rWndNextIndex);

    swtp_onSendPacket(pipe, &buffer, 4);

    return 0;
}

static int swtp_sendTEST(swtp_pipe_t *pipe) {
    uint32_t buffer = htonl(0x40000000 | pipe->rWndNextIndex);

    debug("swtp: < TEST 0x%04x\n", pipe->rWndNextIndex);

    pipe->pingsSent++;

    return swtp_onSendPacket(pipe, &buffer, 4);
}

static void swtp_retransmit(swtp_pipe_t *pipe) {
    for(int i = 0; i < pipe->sWndLength; i++) {
        swtp_sendPacket(pipe, &pipe->sWndBuffer[(pipe->sWndStartIndex + i) % pipe->sWndSize]);
    }
}

static void swtp_acknowledge(swtp_pipe_t *pipe, int sequenceNumber) {
    int lastAcknowledgedFrameNumber = (sequenceNumber - 1) & 0x7fff;

    // Make sure that the sequence number is in the window
    int startSequenceNumber = pipe->sWndStartSequence;
    int endSequenceNumber = (startSequenceNumber + pipe->sWndLength) & 0x7fff;

    if(lastAcknowledgedFrameNumber < startSequenceNumber) {
        if(startSequenceNumber < endSequenceNumber) {
            return;
        } else if(lastAcknowledgedFrameNumber < startSequenceNumber && lastAcknowledgedFrameNumber >= endSequenceNumber) {
            return;
        }
    } else if(lastAcknowledgedFrameNumber >= endSequenceNumber) {
        return;
    }

    // Acknowledge the packets one by one
    while(pipe->sWndStartSequence != sequenceNumber) {
        pipe->sWndStartIndex = (pipe->sWndStartIndex + 1) % pipe->sWndSize;
        pipe->sWndStartSequence = (pipe->sWndStartSequence + 1) & 0x7fff;
        pipe->sWndLength--;
    }
}

static void swtp_sendPacket(swtp_pipe_t *pipe, swtp_packet_t *packet) {
    struct timespec currentTime;

    timespec_get(&currentTime, TIME_UTC);

    packet->lastSendTime = currentTime;

    uint32_t header = htonl((packet->sequenceNumber << 15) | pipe->rWndNextIndex);
    uint32_t packetTypeConverted = htonl(packet->type);

    uint8_t buffer[packet->length + 8];

    memcpy(buffer, &header, 4);
    memcpy(buffer + 4, &packetTypeConverted, 4);
    memcpy(buffer + 8, packet->buffer, packet->length);

    swtp_onSendPacket(pipe, buffer, packet->length + 8);

    debug("swtp: < DATA 0x%04x 0x%04x\n", packet->sequenceNumber, pipe->rWndNextIndex);
}

static int timeDiff(const struct timespec *t1, const struct timespec *t2) {
    const struct timespec *first = NULL;
    const struct timespec *second = NULL;
    
    if(t1->tv_sec < t2->tv_sec) {
        first = t1;
        second = t2;
    } else if(t1->tv_sec > t2->tv_sec) {
        first = t2;
        second = t1;
    } else if(t1->tv_nsec < t2->tv_nsec) {
        first = t1;
        second = t2;
    } else {
        first = t2;
        second = t1;
    }

    int seconds = second->tv_sec - first->tv_sec;
    int nanos = second->tv_nsec - first->tv_nsec;

    int millis = (seconds * 1000) + (nanos / 1000000);

    return millis;
}
