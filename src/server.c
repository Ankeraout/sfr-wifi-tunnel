#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <libtun/libtun.h>
#include <common.h>
#include <threads.h>
#include <string.h>
#include <libswtp/swtp.h>

#define MAX_CLIENTS 2
#define RECV_WINDOW 10

// Contains the client list
swtp_t *clientList[MAX_CLIENTS];

// Contains the current number of clients
int clientCount = 0;

// Contains the server socket
int serverSocket;

int createServerSocket();
void mainServerLoop();

int main(int argc, char **argv) {
    UNUSED_PARAMETER(argc);
    UNUSED_PARAMETER(argv);

    serverSocket = createServerSocket();

    if(serverSocket < 0) {
        perror("Failed to create server socket");
        return 1;
    }

    memset(clientList, 0, sizeof(clientList));
    clientCount = 0;

    mainServerLoop();

    close(serverSocket);
    
    return 0;
}

int createServerSocket() {
    int sock_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    if(sock_fd < 0) {
        return -1;
    }

    struct sockaddr_in socketAddress;
    memset(&socketAddress, 0, sizeof(socketAddress));
    socketAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    socketAddress.sin_family = AF_INET;
    socketAddress.sin_port = htons(SWTP_PORT);

    bind(sock_fd, (const struct sockaddr *)&socketAddress, sizeof(socketAddress));

    return sock_fd;
}

/*
    Searches for the client with the given socket address in the given client
    list. If the client exists in the list, return its index. Else return -1.
*/
int findClientBySocketAddress(struct sockaddr_in *socketAddress, socklen_t socketAddressLength) {
    for(int i = 0; i < MAX_CLIENTS; i++) {
        if(clientList[i]) {
            if(memcmp(socketAddress, &clientList[i]->socketAddress, socketAddressLength) == 0) {
                return i;
            }
        }
    }

    return -1;
}

/*
    Searches for the client with the given pointer and returns its index in the
    client table. If the client does not exist, this function returns -1.
*/
int findClientByData(swtp_t *client) {
    for(int i = 0; i < MAX_CLIENTS; i++) {
        if(clientList[i] == client) {
            return i;
        }
    }

    return -1;
}

/*
    Accepts a client's connection by sending a SABM response, stores the client
    entry in the client table, and return its index in the table. If an error
    occurred, -1 will be returned.
*/
int acceptClientSABM(const struct sockaddr *socketAddress, const swtp_frame_t *frame) {
    // Check if there's enough space in the client table
    if(clientCount >= MAX_CLIENTS) {
        return -1;
    }

    int freeSlot = 0;

    for(int i = 0; i < MAX_CLIENTS; i++) {
        if(clientList[i] == NULL) {
            freeSlot = i;
            break;
        }
    }

    swtp_t *swtp = malloc(sizeof(swtp_t));

    if(swtp == NULL) {
        return -1;
    }

    // Initialize SWTP structure
    memcpy(&swtp->socketAddress, socketAddress, sizeof(struct sockaddr));
    swtp->frameSendSequenceNumber = 0;
    swtp->frameReceiveSequenceNumber = 0;

    swtp->sendWindowSize = ntohl(*(uint32_t *)frame->payload) & 0x00007fff;
    swtp->sendWindow = malloc(sizeof(swtp_frame_t) * swtp->sendWindowSize);

    if(swtp->sendWindow == NULL) {
        free(swtp);
        return -1;
    }

    swtp->sendWindowStartIndex = 0;
    swtp->sendWindowStartSequenceNumber = 0;
    swtp->sendWindowLength = 0;

    swtp->recvWindowSize = RECV_WINDOW;
    swtp->recvWindow = malloc(sizeof(swtp_frame_t) * swtp->recvWindowSize);

    if(swtp->sendWindow == NULL) {
        free(swtp->sendWindow);
        free(swtp);
        return -1;
    }

    swtp->recvWindowStartIndex = 0;
    swtp->recvWindowStartSequenceNumber = 0;
    swtp->recvWindowLength = 0;

    swtp->lastReceivedFrameTime = time(NULL);
    swtp->connected = true;

    // Send SABM response
    uint32_t response = htonl(0x80000000 | RECV_WINDOW);
    sendto(serverSocket, &response, 4, 0, socketAddress, sizeof(struct sockaddr_in));

    // Register the client in the client list
    clientList[freeSlot] = swtp;
    clientCount++;

    printf("> SABM %d\n", swtp->sendWindowSize);
    printf("Accepted %s (send window size=%d) as #%d\n", inet_ntoa((*(struct sockaddr_in *)socketAddress).sin_addr), swtp->sendWindowSize, freeSlot);
    printf("< SABM %d\n", swtp->recvWindowSize);

    return freeSlot;
}

void onFrameReceived_DATA(swtp_t *client, swtp_frame_t *frame) {
    uint32_t frameHeader = ntohl(*(uint32_t *)frame->payload);
    uint_least16_t header_s = (frameHeader & 0x7fff0000) >> 16;
    uint_least16_t header_r = frameHeader & 0x00007fff;

    uint_least16_t expectedFrameNumber = (client->recvWindowStartIndex + client->recvWindowLength) & 0x7fff;

    printf("> DATA (%d, %d)\n", header_s, header_r);

    if(header_s != expectedFrameNumber) {
        printf("< REJ %d\n", expectedFrameNumber);
        uint32_t rejFrame = htonl(0xd0000000 | expectedFrameNumber);
        sendto(serverSocket, &rejFrame, 4, 0, (const struct sockaddr *)&client->socketAddress, sizeof(client->socketAddress));
    } else {
        if(client->recvWindowLength < client->recvWindowSize) {
            printf("< ACK %d\n", (header_s + 1) & 0x7fff);
            memcpy(&client->recvWindow[(client->recvWindowStartIndex + client->recvWindowLength) % client->recvWindowSize], frame, sizeof(swtp_frame_t));
            client->recvWindowLength++;
            uint32_t ackFrame = htonl(0xe0000000 | ((header_s + 1) & 0x7fff));
            sendto(serverSocket, &ackFrame, 4, 0, (const struct sockaddr *)&client->socketAddress, sizeof(client->socketAddress));
        } else {
            printf("Receive window full, rejected frame\n");
        }
    }
}

void onFrameReceived_SABM(swtp_t *client, swtp_frame_t *frame) {
    UNUSED_PARAMETER(client);
    UNUSED_PARAMETER(frame);

    printf("> SABM\n");
}

void onFrameReceived_DISC(swtp_t *client, swtp_frame_t *frame) {
    UNUSED_PARAMETER(frame);

    int clientIndex = findClientByData(client);

    free(client->recvWindow);
    free(client->sendWindow);
    free(client);
    
    clientList[clientIndex] = NULL;

    printf("> DISC\n");
    printf("Client #%d explicitely disconnected.\n", clientIndex);
}

void onFrameReceived_TEST(swtp_t *client, swtp_frame_t *frame) {
    UNUSED_PARAMETER(frame);
    
    printf("> TEST\n");

    uint_least16_t expectedFrameNumber = (client->recvWindowStartIndex + client->recvWindowLength) & 0x7fff;

    printf("< ACK %d\n", expectedFrameNumber & 0x7fff);
    uint32_t ackFrame = htonl(0xe0000000 | expectedFrameNumber);
    sendto(serverSocket, &ackFrame, 4, 0, (const struct sockaddr *)&client->socketAddress, sizeof(client->socketAddress));
}

void onFrameReceived_SREJ(swtp_t *client, swtp_frame_t *frame) {
    UNUSED_PARAMETER(client);
    UNUSED_PARAMETER(frame);
    
    printf("> SREJ\n");
}

void onFrameReceived_REJ(swtp_t *client, swtp_frame_t *frame) {
    UNUSED_PARAMETER(client);
    UNUSED_PARAMETER(frame);
    
    printf("> REJ\n");
}

void onFrameReceived_ACK(swtp_t *client, swtp_frame_t *frame) {
    UNUSED_PARAMETER(client);
    UNUSED_PARAMETER(frame);
    
    printf("> ACK\n");
}

void onFrameReceived(swtp_t *client, swtp_frame_t *frame) {
    switch(frame->payload[0] >> 4) {
        case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7: onFrameReceived_DATA(client, frame); break;
        case 8: onFrameReceived_SABM(client, frame); break;
        case 9: onFrameReceived_DISC(client, frame); break;
        case 10: onFrameReceived_TEST(client, frame); break;
        case 12: onFrameReceived_SREJ(client, frame); break;
        case 13: onFrameReceived_REJ(client, frame); break;
        case 14: onFrameReceived_ACK(client, frame); break;
        default: printf("Ignored wrong frame\n");
    }
}

void mainServerLoop() {
    while(true) {
        // Contains the address of the last client who sent a datagram.
        struct sockaddr_in socketAddress;

        // Contains the size of the socketAddress structure.
        socklen_t socketAddressLength = sizeof(socketAddress);
        
        // Contains the datagram from the client.
        swtp_frame_t buffer;

        // Receive the datagram.
        ssize_t packetSize = recvfrom(serverSocket, buffer.payload, SWTP_MAX_PAYLOAD_SIZE, 0, (struct sockaddr *)&socketAddress, &socketAddressLength);

        // If the packet has a negative size, then an error occurred.
        if(packetSize < 0) {
            perror("An error occurred in server main loop");

            // Exit the loop
            break;
        }

        // Search for the client
        int clientIndex = findClientBySocketAddress(&socketAddress, socketAddressLength);

        // If the client was not found
        if(clientIndex == -1) {
            // If there is no slot remaining
            if(clientCount < MAX_CLIENTS) {
                // If the packet is a SABM packet
                if((buffer.payload[0] & 0xf0) == 0x80) {
                    // Accept the client
                    if(acceptClientSABM((const struct sockaddr *)&socketAddress, &buffer) < 0) {
                        perror("Failed to accept a client");
                    }
                } else {
                    printf("Refused a client because the received packet was incorrect.\n");
                }
            } else {
                printf("Refused a client because the client list was full.\n");
            }
        } else {
            clientList[clientIndex]->lastReceivedFrameTime = time(NULL);
            onFrameReceived(clientList[clientIndex], &buffer);
        }
    }
}
