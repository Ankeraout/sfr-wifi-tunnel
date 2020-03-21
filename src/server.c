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
#include <net/if.h>

#define MAX_CLIENTS 2
#define RECV_WINDOW 2

// Contains the client list
swtp_t *clientList[MAX_CLIENTS];

// Contains the current number of clients
int clientCount = 0;

// Contains the server socket
int serverSocket;

// Contains the tun device
int tunDevice;

// Contains the tun device name
char tunDeviceName[16];

int createServerSocket();
void mainServerLoop();
int tunReaderMainLoop(void *arg);

mtx_t clientListMutex;
thrd_t tunDeviceReaderThread;

int main(int argc, char **argv) {
    UNUSED_PARAMETER(argc);
    UNUSED_PARAMETER(argv);

    tunDevice = libtun_open(tunDeviceName);

    if(tunDevice < 0) {
        perror("Failed to open TUN device");
        return 1;
    }

    serverSocket = createServerSocket();

    if(serverSocket < 0) {
        perror("Failed to create server socket");
        return 1;
    }

    memset(clientList, 0, sizeof(clientList));
    clientCount = 0;

    if(mtx_init(&clientListMutex, mtx_plain) == thrd_error) {
        perror("Failed to create mutex");
        return 1;
    }

    if(thrd_create(&tunDeviceReaderThread, tunReaderMainLoop, NULL) == thrd_error) {
        perror("Failed to create tun reader thread");
        return 1;
    }

    printf("Ready.\n");

    mainServerLoop();

    close(serverSocket);
    
    return 0;
}

int tunReaderMainLoop(void *arg) {
    UNUSED_PARAMETER(arg);
    
    uint8_t buffer[SWTP_MAX_PAYLOAD_SIZE];

    while(true) {
        ssize_t packetSize = read(tunDevice, buffer, SWTP_MAX_PAYLOAD_SIZE);
        
        if(packetSize < 0) {
            break;
        }

        mtx_lock(&clientListMutex);

        for(int i = 0; i < MAX_CLIENTS; i++) {
            if(clientList[i]) {
                swtp_sendDataFrame(clientList[i], buffer, packetSize);
            }
        }

        mtx_unlock(&clientListMutex);
    }

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

void onDataFrameReceived(swtp_t *swtp, const void *buffer, size_t size) {
    UNUSED_PARAMETER(swtp);
    write(tunDevice, buffer, size);
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

    // Find a free slot for the client
    int freeSlot;

    for(freeSlot = 0; freeSlot < MAX_CLIENTS; freeSlot++) {
        if(!clientList[freeSlot]) {
            break;
        }
    }

    // Allocate memory for the SWTP structure
    swtp_t *swtp = malloc(sizeof(swtp_t));

    if(swtp == NULL) {
        return -1;
    }

    // Initialize SWTP structure
    swtp_init(swtp, serverSocket, socketAddress);

    if(swtp_initSendWindow(swtp, ntohs(*((uint16_t *)(frame->frame.header + 2)))) != SWTP_SUCCESS) {
        free(swtp);
        return -1;
    }

    swtp->lastReceivedFrameTime = time(NULL);
    swtp->connected = true;

    // Send SABM response
    uint32_t response = htonl(0x80000000 | RECV_WINDOW);
    sendto(serverSocket, &response, 4, 0, socketAddress, sizeof(struct sockaddr_in));

    // Register the client in the client list
    clientList[freeSlot] = swtp;
    clientCount++;

    printf("Accepted %s (recv window size=%d) as #%d\n", inet_ntoa((*(struct sockaddr_in *)socketAddress).sin_addr), swtp->sendWindowSize, freeSlot);

    // Register callback
    clientList[freeSlot]->recvCallback = onDataFrameReceived;

    return freeSlot;
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
        ssize_t packetSize = recvfrom(serverSocket, &buffer.frame, SWTP_MAX_FRAME_SIZE, 0, (struct sockaddr *)&socketAddress, &socketAddressLength);

        // If the packet has a negative size, then an error occurred.
        if(packetSize < 0) {
            perror("An error occurred in server main loop");

            // Exit the loop
            break;
        }

        buffer.size = packetSize;

        mtx_lock(&clientListMutex);

        // Search for the client
        int clientIndex = findClientBySocketAddress(&socketAddress, socketAddressLength);

        // If the client was not found
        if(clientIndex == -1) {
            // If there is no slot remaining
            if(clientCount < MAX_CLIENTS) {
                // If the packet is a SABM packet
                if((buffer.frame.header[0] & 0xf0) == 0x80) {
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
            if(swtp_onFrameReceived(clientList[clientIndex], &buffer) != SWTP_SUCCESS) {
                perror("SWTP failed to handle frame from client");
            }
        }

        mtx_unlock(&clientListMutex);
    }
}
