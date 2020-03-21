#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdio.h>

#include <libswtp/swtp.h>

void swtp_init(swtp_t *swtp, int socket, const struct sockaddr *socketAddress) {
    memset(swtp, 0, sizeof(swtp_t));

    swtp->socket = socket;
    memcpy(&swtp->socketAddress, socketAddress, sizeof(struct sockaddr));
    swtp->lastReceivedFrameTime = time(NULL);
}

int swtp_initSendWindow(swtp_t *swtp, uint_least16_t sendWindowSize) {
    swtp->sendWindow = malloc(sizeof(swtp_frame_t) * sendWindowSize);

    if(swtp->sendWindow == NULL) {
        return SWTP_ERROR;
    }

    swtp->sendWindowSize = sendWindowSize;

    return SWTP_SUCCESS;
}

void swtp_destroy(swtp_t *swtp) {
    if(swtp->sendWindow) {
        free(swtp->sendWindow);
    }
}

int swtp_sendDataFrame(swtp_t *swtp, const void *buffer, size_t size) {
    // Check the frame size
    if(size > SWTP_MAX_PAYLOAD_SIZE) {
        printf("Maximum payload size exceeded. (%lu > %d)\n", size, SWTP_MAX_PAYLOAD_SIZE);
        return SWTP_ERROR;
    }

    // TODO: Wait for a slot to be available and acquire lock
    if(swtp->sendWindowLength >= swtp->sendWindowSize) {
        printf("Lost frame due to window saturation.\n");
        return SWTP_SUCCESS;
    }

    // Compute sequence numbers
    uint_least16_t sendSequenceNumber = htons((swtp->sendWindowStartSequenceNumber + swtp->sendWindowLength) & 0x7fff);
    uint_least16_t receiveSequenceNumber = htons(swtp->expectedFrameNumber);
    uint_least16_t sendWindowIndex = (swtp->sendWindowStartIndex + swtp->sendWindowLength) % swtp->sendWindowSize;

    // Reserve a slot in the send window
    swtp->sendWindowLength++;

    // Copy the payload of the frame to the send window
    memcpy(swtp->sendWindow[sendWindowIndex].frame.payload, buffer, size);
    swtp->sendWindow[sendWindowIndex].size = size + SWTP_HEADER_SIZE;
    
    // Set the sequence numbers in the buffer
    memcpy(swtp->sendWindow[sendWindowIndex].frame.header, &sendSequenceNumber, 2);
    memcpy(swtp->sendWindow[sendWindowIndex].frame.header + 2, &receiveSequenceNumber, 2);

    printf("< DATA %d\n", ntohs(sendSequenceNumber));

    // Send the data frame
    if(sendto(swtp->socket, &swtp->sendWindow[sendWindowIndex].frame, swtp->sendWindow[sendWindowIndex].size, 0, (struct sockaddr *)&swtp->socketAddress, sizeof(struct sockaddr_in)) < 0) {
        // TODO: Release the lock
        perror("Failed to send data frame");
        return SWTP_ERROR;
    }

    // TODO: Release the lock

    return SWTP_SUCCESS;
}

bool swtp_isSentFrameNumberValid(const swtp_t *swtp, uint_least16_t seq) {
    // Check that the sequence number is between the send window bounds
    if(seq > SWTP_MAX_SEQUENCE_NUMBER) {
        return NULL;
    }

    uint_least16_t windowStart = swtp->sendWindowStartSequenceNumber % SWTP_SEQUENCE_NUMBER_COUNT;
    uint_least16_t windowEnd = (windowStart + swtp->sendWindowLength) % SWTP_SEQUENCE_NUMBER_COUNT;

    if(windowEnd < windowStart) {
        return seq >= windowStart;
    } else {
        return (seq >= windowStart) && (seq < windowEnd);
    }
}

swtp_frame_t *swtp_getSentFrame(const swtp_t *swtp, uint_least16_t seq) {
    if(swtp_isSentFrameNumberValid(swtp, seq)) {
        return &swtp->sendWindow[((seq - swtp->sendWindowStartSequenceNumber) + swtp->sendWindowStartIndex) % swtp->sendWindowSize];
    } else {
        return NULL;
    }
}

static inline int swtp_sendRR(swtp_t *swtp) {
    uint32_t rr = htonl(0xe0000000 | swtp->expectedFrameNumber);

    printf("< RR %d\n", swtp->expectedFrameNumber);

    if(sendto(swtp->socket, &rr, SWTP_HEADER_SIZE, 0, &swtp->socketAddress, sizeof(struct sockaddr_in)) < 0) {
        perror("Failed to send RR");
        return SWTP_ERROR;
    }

    return SWTP_SUCCESS;
}

static inline void swtp_acknowledgeSentFrame(swtp_t *swtp, uint_least16_t sequenceNumber) {
    uint_least16_t acknowledgedFrameCount;

    if(sequenceNumber < swtp->sendWindowStartSequenceNumber) {
        acknowledgedFrameCount = SWTP_MAX_SEQUENCE_NUMBER - swtp->sendWindowStartSequenceNumber + sequenceNumber;
    } else {
        acknowledgedFrameCount = sequenceNumber - swtp->sendWindowStartSequenceNumber;
    }

    if(acknowledgedFrameCount > swtp->sendWindowLength) {
        // Ignore wrong acknowledgement
        printf("Ignored wrong acknowledgement\n");
        return;
    } else if(acknowledgedFrameCount == 0) {
        printf("Acknowledgement for 0 frames.\n");
        return;
    }

    printf("Acknowledged %d frames.\n", acknowledgedFrameCount);

    swtp->sendWindowLength -= acknowledgedFrameCount;
    swtp->sendWindowStartIndex += acknowledgedFrameCount;
    swtp->sendWindowStartIndex %= swtp->sendWindowSize;
    swtp->sendWindowStartSequenceNumber += acknowledgedFrameCount;
    swtp->sendWindowStartSequenceNumber %= SWTP_SEQUENCE_NUMBER_COUNT;
}

int swtp_onFrameReceived(swtp_t *swtp, const swtp_frame_t *frame) {
    // Determine the frame type
    if(frame->frame.header[0] & 0x80) {
        // Control frame
        switch((frame->frame.header[0] >> 4) & 0x07) {
            case 0: // SABM
                // TODO: What to do when receiving a SABM if the connection was already established?
                break;

            case 1: // DISC
                swtp->connected = false;
                // TODO: Stop all I/O operations
                break;

            case 2: // TEST
                // Send RR
                return swtp_sendRR(swtp);

            case 3: // Unknown, ignore
                break;
            
            case 4: // SREJ
                printf("> SREJ %d\n", ntohs(*(uint16_t *)(frame->frame.header + 2)));

                if(swtp_isSentFrameNumberValid(swtp, ntohs(*(uint16_t *)(frame->frame.header + 2)))) {
                    swtp_frame_t *rejectedFrame = swtp_getSentFrame(swtp, ntohs(*(uint16_t *)(frame->frame.header + 2)));
                    
                    printf("< DATA %d\n", ntohs(*(uint16_t *)(frame->frame.header + 2)));

                    if(sendto(swtp->socket, (const void *)&rejectedFrame->frame, rejectedFrame->size, 0, (struct sockaddr *)&swtp->socketAddress, sizeof(struct sockaddr_in)) < 0) {
                        perror("Failed to send data frame after SREJ");
                        return SWTP_ERROR;
                    }
                }
                break;

            case 5: // REJ
                {
                    printf("> REJ %d\n", ntohs(*(uint16_t *)(frame->frame.header + 2)));

                    uint_least16_t rejectedFrameIndex = ntohs(*(uint16_t *)(frame->frame.header + 2));

                    while(swtp_isSentFrameNumberValid(swtp, rejectedFrameIndex)) {
                        swtp_frame_t *rejectedFrame = swtp_getSentFrame(swtp, rejectedFrameIndex);

                        printf("< DATA %d\n", ntohs(*(uint16_t *)(rejectedFrame->frame.header + 2)));
                        
                        if(sendto(swtp->socket, (const void *)&rejectedFrame->frame, rejectedFrame->size, 0, (struct sockaddr *)&swtp->socketAddress, sizeof(struct sockaddr_in)) < 0) {
                            perror("Failed to send data frame after REJ");
                            return SWTP_ERROR;
                        }

                        rejectedFrameIndex++;
                        rejectedFrameIndex %= swtp->sendWindowSize;
                    }
                }
                break;

            case 6: // RR
                // TODO: acquire SWTP lock
                printf("> RR %d\n", ntohs(*(uint16_t *)(frame->frame.header + 2)));
                swtp_acknowledgeSentFrame(swtp, ntohs(*(uint16_t *)(frame->frame.header + 2)));
                // TODO: release SWTP lock
                break;

            case 7: // RNR
                // TODO: acquire SWTP lock
                printf("> RNR %d\n", ntohs(*(uint16_t *)(frame->frame.header + 2)));
                swtp_acknowledgeSentFrame(swtp, ntohs(*(uint16_t *)(frame->frame.header + 2)));
                // TODO: set a flag to stop sending
                // TODO: release SWTP lock
                break;
        }
    } else {
        // Data frame
        // TODO: acquire lock
        uint_least16_t frameSequenceNumber = ntohs(*(uint16_t *)frame->frame.header) & 0x7fff;

        printf("> DATA %d\n", frameSequenceNumber);

        // Make sure that the frame has the expected sequence number
        if(frameSequenceNumber != swtp->expectedFrameNumber) {
            // Send SREJ
            uint32_t srejBuffer = htonl(0xc0000000 | swtp->expectedFrameNumber);

            printf("< SREJ %d\n", swtp->expectedFrameNumber);

            if(sendto(swtp->socket, &srejBuffer, SWTP_HEADER_SIZE, 0, &swtp->socketAddress, sizeof(struct sockaddr_in)) < 0) {
                // TODO: release lock
                perror("Failed to send SREJ");
                return SWTP_ERROR;
            }
        } else {
            swtp->expectedFrameNumber++;
            swtp->expectedFrameNumber %= SWTP_SEQUENCE_NUMBER_COUNT;

            // Acknowledge the frame
            swtp_sendRR(swtp);

            // Call the callback
            if(swtp->recvCallback) {
                swtp->recvCallback(swtp, frame->frame.payload, frame->size - SWTP_HEADER_SIZE);
            }
        }

        // TODO: release lock
    }

    return SWTP_SUCCESS;
}
