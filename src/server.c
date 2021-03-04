#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <unistd.h>

#include <arpa/inet.h>

#include <threads.h>

#include "common.h"
#include "swtp.h"
#include "tun.h"

typedef struct {
    bool present;
    struct sockaddr socketAddress;
    socklen_t socketAddressLength;
    swtp_pipe_t pipe;
} client_t;

int main(int argc, const char **argv);
static int parseCommandLineParameters(int argc, const char **argv);
static int createServerSocket();
static void receiveLoop();
static int tunReadLoop(void *arg);
static int clockLoop(void *arg);
static int findClientBySocketAddress(const struct sockaddr *socketAddress, socklen_t socketAddressLength);
static int findClientByPipe(swtp_pipe_t *pipe);
static int addClientToClientList(const struct sockaddr *socketAddress, socklen_t socketAddressLength);
int swtp_onReceivePacket(swtp_pipe_t *pipe, swtp_packetType_t packetType, const void *packetBuffer, size_t packetSize);

static int tunDeviceFd = -1;
static int serverSocketFd = -1;
static char tunDeviceName[16] = "\0";
static int maxClients = 1;
static int maxReceiveWindowSize = 4;
static int maxSendWindowSize = 4;
static int serverPort = 5228;
static mtx_t clientListMutex;
static client_t *clientList;
static int clientListLength;
static thrd_t tunReaderThread;
static thrd_t clockThread;

int main(int argc, const char **argv) {
    if(parseCommandLineParameters(argc, argv)) {
        fprintf(stderr, "Command-line parameter parsing failed.\n");
        return EXIT_FAILURE;
    }

    tunDeviceFd = openTunDevice(tunDeviceName);

    if(tunDeviceFd < 0) {
        fprintf(stderr, "Failed to open tun device.\n");
        return EXIT_FAILURE;
    }

    if(createServerSocket()) {
        fprintf(stderr, "Failed to create server socket.\n");
        return EXIT_FAILURE;
    }

    clientList = calloc(maxClients * sizeof(client_t), 1);
    clientListLength = 0;

    if(mtx_init(&clientListMutex, mtx_plain) == thrd_error) {
        fprintf(stderr, "Failed to create client list mutex.\n");
        return EXIT_FAILURE;
    }

    if(thrd_create(&tunReaderThread, tunReadLoop, NULL) == thrd_error) {
        fprintf(stderr, "Failed to create tun reader thread.\n");
        return EXIT_FAILURE;
    }

    if(thrd_create(&clockThread, clockLoop, NULL) == thrd_error) {
        fprintf(stderr, "Failed to create clock thread.\n");
        return EXIT_FAILURE;
    }

    printf("Ready.\n");

    receiveLoop();

    close(serverSocketFd);

    return 0;
}

static int parseCommandLineParameters(int argc, const char **argv) {
    bool flag_maxClients = false;
    bool flag_maxReceiveWindowSize = false;
    bool flag_maxSendWindowSize = false;
    bool flag_serverPort = false;

    for(int i = 1; i < argc; i++) {
        if(flag_maxClients) {
            flag_maxClients = false;

            if(sscanf(argv[i], "%d", &maxClients) == EOF) {
                fprintf(stderr, "Failed to parse --max-clients option value.\n");
                return 1;
            }

            if(maxClients <= 0) {
                fprintf(stderr, "Invalid --max-clients option value.\n");
                return 1;
            }
        } else if(flag_maxReceiveWindowSize) {
            flag_maxReceiveWindowSize = false;

            if(sscanf(argv[i], "%d", &maxReceiveWindowSize) == EOF) {
                fprintf(stderr, "Failed to parse --max-rwnd option value.\n");
                return 1;
            }

            if(maxReceiveWindowSize <= 0 || maxReceiveWindowSize > SWTP_MAX_WINDOW_SIZE) {
                fprintf(stderr, "Invalid --max-rwnd option value.\n");
                return 1;
            }
        } else if(flag_maxSendWindowSize) {
            flag_maxSendWindowSize = false;

            if(sscanf(argv[i], "%d", &maxSendWindowSize) == EOF) {
                fprintf(stderr, "Failed to parse --max-swnd option value.\n");
                return 1;
            }

            if(maxSendWindowSize <= 0 || maxSendWindowSize > SWTP_MAX_WINDOW_SIZE) {
                fprintf(stderr, "Invalid --max-swnd option value.\n");
                return 1;
            }
        } else if(flag_serverPort) {
            flag_serverPort = false;

            if(sscanf(argv[i], "%d", &serverPort) == EOF) {
                fprintf(stderr, "Failed to parse --port option value.\n");
                return 1;
            }

            if(serverPort < 0 || serverPort > 65535) {
                fprintf(stderr, "Invalid --port option value.\n");
                return 1;
            }
        } else if(strcmp(argv[i], "--max-clients") == 0) {
            flag_maxClients = true;
        } else if(strcmp(argv[i], "--max-rwnd") == 0) {
            flag_maxReceiveWindowSize = true;
        } else if(strcmp(argv[i], "--max-swnd") == 0) {
            flag_maxSendWindowSize = true;
        } else {
            fprintf(stderr, "Failed to parse command-line parameter '%s'.\n", argv[i]);
            return 1;
        }
    }

    if(flag_maxClients) {
        fprintf(stderr, "--max-clients expected an integer value.\n");
        return 1;
    } else if(flag_maxReceiveWindowSize) {
        fprintf(stderr, "--max-rwnd expected an integer value.\n");
        return 1;
    } else if(flag_maxSendWindowSize) {
        fprintf(stderr, "--max-swnd expected an integer value.\n");
        return 1;
    } else if(flag_serverPort) {
        fprintf(stderr, "--port expected an integer value.\n");
        return 1;
    }

    return 0;
}

static int createServerSocket() {
    serverSocketFd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    if(serverSocketFd < 0) {
        return 1;
    }

    struct sockaddr_in socketAddress;

    memset(&socketAddress, 0, sizeof(socketAddress));

    socketAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    socketAddress.sin_family = AF_INET;
    socketAddress.sin_port = htons(serverPort);

    bind(serverSocketFd, (const struct sockaddr *)&socketAddress, sizeof(socketAddress));

    return 0;
}

static void receiveLoop() {
    while(true) {
        struct sockaddr_in socketAddress;

        socklen_t socketAddressLength = sizeof(socketAddress);

        uint8_t packetBuffer[1500];

        ssize_t packetSize = recvfrom(serverSocketFd, packetBuffer, SWTP_MAX_PACKET_SIZE, 0, (struct sockaddr *)&socketAddress, &socketAddressLength);

        if(packetSize < 0) {
            fprintf(stderr, "Receive loop thread error: %s\n", strerror(errno));
            break;
        }

        mtx_lock(&clientListMutex);

        int clientIndex = findClientBySocketAddress((const struct sockaddr *)&socketAddress, socketAddressLength);

        if(clientIndex == -1) {
            if(addClientToClientList((const struct sockaddr *)&socketAddress, socketAddressLength) == 0) {
                clientIndex = findClientBySocketAddress((const struct sockaddr *)&socketAddress, socketAddressLength);
            }
        }

        if(clientIndex >= 0) {
            swtp_onPacketReceived(&clientList[clientIndex].pipe, packetBuffer, packetSize);
        }

        mtx_unlock(&clientListMutex);
    }
}

static int tunReadLoop(void *arg) {
    (void)arg;

    uint8_t buffer[SWTP_MAX_PACKET_SIZE];

    while(true) {
        ssize_t packetSize = read(tunDeviceFd, buffer, SWTP_MAX_PACKET_SIZE);

        if(packetSize < 0) {
            return 1;
        }

        mtx_lock(&clientListMutex);

        for(int i = 0; i < clientListLength; i++) {
            swtp_send(&clientList[i].pipe, SWTP_PACKETTYPE_TUN, buffer, packetSize);
        }

        mtx_unlock(&clientListMutex);
    }

    return 0;
}

static int clockLoop(void *arg) {
    (void)arg;

    struct timespec waitingTime = {
        .tv_sec = 0,
        .tv_nsec = 1000000
    };

    while(true) {
        struct timespec currentTime;

        timespec_get(&currentTime, TIME_UTC);

        mtx_lock(&clientListMutex);

        for(int i = 0; i < clientListLength; i++) {
            swtp_onClockTick(&clientList[i].pipe, &currentTime);
        }

        mtx_unlock(&clientListMutex);

        thrd_sleep(&waitingTime, NULL);
    }

    return 0;
}

static int findClientBySocketAddress(const struct sockaddr *socketAddress, socklen_t socketAddressLength) {
    for(int i = 0; i < clientListLength; i++) {
        if(clientList[i].present) {
            if(clientList[i].socketAddressLength == socketAddressLength) {
                if(memcmp(socketAddress, &clientList[i].socketAddress, clientList[i].socketAddressLength) == 0) {
                    return i;
                }
            }
        }
    }

    return -1;
}

static int findClientByPipe(swtp_pipe_t *pipe) {
    for(int i = 0; i < clientListLength; i++) {
        if(clientList[i].present) {
            if(&clientList[i].pipe == pipe) {
                return i;
            }
        }
    }

    return -1;
}

static int addClientToClientList(const struct sockaddr *socketAddress, socklen_t socketAddressLength) {
    if(clientListLength >= maxClients) {
        return 1;
    }

    clientList[clientListLength].present = true;
    clientList[clientListLength].socketAddress = *socketAddress;
    clientList[clientListLength].socketAddressLength = socketAddressLength;
    swtp_init(&clientList[clientListLength].pipe);

    clientListLength++;

    return 0;
}

int swtp_onSendPacket(swtp_pipe_t *pipe, const void *packetBuffer, size_t packetSize) {
    int clientIndex = findClientByPipe(pipe);

    if(clientIndex != -1) {
        if(sendto(serverSocketFd, packetBuffer, packetSize, 0, &clientList[clientIndex].socketAddress, sizeof(struct sockaddr_in)) != (ssize_t)packetSize) {
            return 1;
        }
    }

    return 0;
}

int swtp_onReceivePacket(swtp_pipe_t *pipe, swtp_packetType_t packetType, const void *packetBuffer, size_t packetSize) {
    if(packetType == SWTP_PACKETTYPE_TUN) {
        write(tunDeviceFd, packetBuffer, packetSize);
    }
}
