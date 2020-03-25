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
#include <signal.h>

// Contains the client list
swtp_t **clientList;

// Contains the maximum number of clients simultaneously connected.
int clientListSize;

// Contains the current number of clients
int clientCount = 0;

// Contains the server socket
int serverSocket;

// Contains the tun device
int tunDevice;

// Contains the tun device name
char tunDeviceName[16];

// Contains the maximum size of the receive window of the server.
int receiveWindowSize;

// Contains the maximum size of the send window of the server. Any value <= 0
// means unspecified.
int sendWindowMaxSize = 0;

int parseCommandLineParameters(int argc, const char **argv);
int createServerSocket();
void mainServerLoop();
int tunReaderMainLoop(void *arg);
int timerThreadMainLoop(void *arg);

mtx_t clientListMutex;
thrd_t tunDeviceReaderThread;
thrd_t timerThread;

int main(int argc, const char **argv) {
    if(parseCommandLineParameters(argc, argv)) {
        printf("Failed to parse command-line parameters.\n");
        return EXIT_FAILURE;
    }

    clientList = malloc(sizeof(swtp_t *) * clientListSize);

    if(!clientList) {
        perror("Failed to allocate memory for the client list");
        return EXIT_FAILURE;
    }

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

    memset(clientList, 0, sizeof(swtp_t *) * clientListSize);
    clientCount = 0;

    if(mtx_init(&clientListMutex, mtx_plain) == thrd_error) {
        perror("Failed to create mutex");
        return 1;
    }

    if(thrd_create(&tunDeviceReaderThread, tunReaderMainLoop, NULL) == thrd_error) {
        perror("Failed to create tun reader thread");
        return 1;
    }

    if(thrd_create(&timerThread, timerThreadMainLoop, NULL)) {
        perror("Failed to create timer thread");
        return EXIT_FAILURE;
    }

    printf("Ready.\n");

    mainServerLoop();

    close(serverSocket);
    
    return 0;
}

int parseCommandLineParameters(int argc, const char **argv) {
    bool flag_maxClients = false;
    bool flag_receiveWindowSize = false;
    bool flag_maxSendWindowSize = false;
    
    bool flag_maxClients_set = false;
    bool flag_windowSize_set = false;

    for(int i = 1; i < argc; i++) {
        if(flag_maxClients) {
            flag_maxClients = false;
            
            if(sscanf(argv[i], "%d", &clientListSize) == EOF) {
                printf("Failed to parse argument value to --max-clients.\n");
                return 1;
            }

            if(clientListSize <= 0) {
                printf("Invalid value for --max-clients. Expected a strictly positive integer.\n");
                return 1;
            }

            flag_maxClients_set = true;
        } else if(flag_receiveWindowSize) {
            flag_receiveWindowSize = false;
            
            if(sscanf(argv[i], "%d", &receiveWindowSize) == EOF) {
                printf("Failed to parse argument value to --max-recv-window-size.\n");
                return 1;
            }

            if(receiveWindowSize <= 0 || receiveWindowSize > SWTP_MAX_WINDOW_SIZE) {
                printf("Invalid value for --max-recv-window-size. Expected an integer between 1 and %d included.\n", SWTP_MAX_WINDOW_SIZE);
                return 1;
            }

            flag_windowSize_set = true;
        } else if(flag_maxSendWindowSize) {
            flag_maxSendWindowSize = false;

            if(sscanf(argv[i], "%d", &sendWindowMaxSize) == EOF) {
                printf("Failed to parse argument value to --max-send-window-size.\n");
                return 1;
            }

            if(sendWindowMaxSize <= 0 || sendWindowMaxSize > SWTP_MAX_WINDOW_SIZE) {
                printf("Invalid value for --max-send-window-size. Expected an integer between 1 and %d included.\n", SWTP_MAX_WINDOW_SIZE);
                return 1;
            }
        } else if(strcmp(argv[i], "--max-clients") == 0) {
            flag_maxClients = true;
        } else if(strcmp(argv[i], "--max-recv-window-size") == 0) {
            flag_receiveWindowSize = true;
        } else if(strcmp(argv[i], "--max-send-window-size") == 0) {
            flag_maxSendWindowSize = true;
        } else {
            printf("Unknown argument \"%s\".", argv[i]);
            return 1;
        }
    }

    if(flag_maxClients) {
        printf("--max-clients expected an integer value.\n");
        return 1;
    } else if(flag_receiveWindowSize) {
        printf("--max-recv-window-size expected an integer value.\n");
        return 1;
    } else if(flag_maxSendWindowSize) {
        printf("--max-send-window-size expected an integer value.\n");
        return 1;
    } else if(!flag_maxClients_set) {
        printf("--max-clients was not set.\n");
        return 1;
    } else if(!flag_windowSize_set) {
        printf("--max-recv-window-size was not set.\n");
        return 1;
    }

    return 0;
}

int timerThreadMainLoop(void *arg) {
    UNUSED_PARAMETER(arg);

    while(true) {
        for(int i = 0; i < clientListSize; i++) {
            if(clientList[i]) {
                if(swtp_onTimerTick(clientList[i]) != SWTP_SUCCESS) {
                    // TODO: what to do when an error occurs?
                }
            }
        }
        
        sleep(1);
    }

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

        for(int i = 0; i < clientListSize; i++) {
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
    for(int i = 0; i < clientListSize; i++) {
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
    for(int i = 0; i < clientListSize; i++) {
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

void onDisconnect(swtp_t *swtp, int reason) {
    mtx_lock(&clientListMutex);
    
    int clientId = findClientByData(swtp);

    clientCount--;
    clientList[clientId] = NULL;
    
    swtp_destroy(swtp);
    free(swtp);

    mtx_unlock(&clientListMutex);

    printf("Client #0 disconnected (reason=%d)\n", reason);
}

/*
    Accepts a client's connection by sending a SABM response, stores the client
    entry in the client table, and return its index in the table. If an error
    occurred, -1 will be returned.
*/
int acceptClientSABM(const struct sockaddr *socketAddress, const swtp_frame_t *frame) {
    // Check if there's enough space in the client table
    if(clientCount >= clientListSize) {
        return -1;
    }

    // Find a free slot for the client
    int freeSlot;

    for(freeSlot = 0; freeSlot < clientListSize; freeSlot++) {
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

    int sendWindowSize = ntohs(*((uint16_t *)(frame->frame.header + 2)));

    if(sendWindowMaxSize > 0) {
        if(sendWindowSize > sendWindowMaxSize) {
            printf("Reducing client receive window size from %d to %d.\n", sendWindowSize, sendWindowMaxSize);
            sendWindowSize = sendWindowMaxSize;
        }
    }

    if(swtp_initSendWindow(swtp, sendWindowSize) != SWTP_SUCCESS) {
        free(swtp);
        return -1;
    }

    swtp->lastReceivedFrameTime = time(NULL);

    // Send SABM response
    uint32_t response = htonl(0x80000000 | receiveWindowSize);
    sendto(serverSocket, &response, 4, 0, socketAddress, sizeof(struct sockaddr_in));

    // Register the client in the client list
    clientList[freeSlot] = swtp;
    clientCount++;

    printf("Accepted %s (recv window size=%d) as #%d\n", inet_ntoa((*(struct sockaddr_in *)socketAddress).sin_addr), swtp->sendWindowSize, freeSlot);

    // Register callbacks
    clientList[freeSlot]->recvCallback = onDataFrameReceived;
    clientList[freeSlot]->disconnectCallback = onDisconnect;

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
            if(clientCount < clientListSize) {
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
