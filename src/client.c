#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>

#include <arpa/inet.h>

#include <threads.h>

#include "common.h"
#include "swtp.h"
#include "tun.h"

int main(int argc, const char *argv[]);
static int parseCommandLineParameters(int argc, const char **argv);
static int sendConfigurationPacket();
static int createClientSocket();
static void receiveLoop();
static int tunReadLoop(void *arg);
static int clockLoop(void *arg);
int swtp_onSendPacket(swtp_pipe_t *pipe, const void *packetBuffer, size_t packetSize);
int swtp_onReceivePacket(swtp_pipe_t *pipe, swtp_packetType_t packetType, const void *packetBuffer, size_t packetSize);

static int tunDeviceFd = -1;
static int clientSocketFd = -1;
static char tunDeviceName[16] = "\0";
static int maxReceiveWindowSize = 4;
static int maxSendWindowSize = 4;
static int serverPort = 5228;
static thrd_t tunReaderThread;
static thrd_t clockThread;
static swtp_pipe_t clientPipe;
static struct sockaddr_in serverAddress;

int main(int argc, const char *argv[]) {
    if(parseCommandLineParameters(argc, argv)) {
        fprintf(stderr, "Command-line parameter parsing failed.\n");
        return EXIT_FAILURE;
    }

    tunDeviceFd = openTunDevice(tunDeviceName);

    if(tunDeviceFd < 0) {
        fprintf(stderr, "Failed to open tun device.\n");
        return EXIT_FAILURE;
    }

    if(createClientSocket()) {
        fprintf(stderr, "Failed to create server socket.\n");
        return EXIT_FAILURE;
    }

    if(swtp_init(&clientPipe)) {
        fprintf(stderr, "Failed to initialize client pipe.\n");
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

    sendConfigurationPacket();
    
    receiveLoop();
}

static int parseCommandLineParameters(int argc, const char **argv) {
    bool flag_maxReceiveWindowSize = false;
    bool flag_maxSendWindowSize = false;
    bool flag_serverPort = false;

    for(int i = 1; i < argc; i++) {
        if(flag_maxReceiveWindowSize) {
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
        } else if(strcmp(argv[i], "--max-rwnd") == 0) {
            flag_maxReceiveWindowSize = true;
        } else if(strcmp(argv[i], "--max-swnd") == 0) {
            flag_maxSendWindowSize = true;
        } else {
            fprintf(stderr, "Failed to parse command-line parameter '%s'.\n", argv[i]);
            return 1;
        }
    }

    if(flag_maxReceiveWindowSize) {
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

static int sendConfigurationPacket() {
    return swtp_send(&clientPipe, SWTP_PACKETTYPE_SWTCP, NULL, 0);
}

int swtp_onSendPacket(swtp_pipe_t *pipe, const void *packetBuffer, size_t packetSize) {
    (void)pipe;
    (void)packetBuffer;
    (void)packetSize;

    sendto(clientSocketFd, packetBuffer, packetSize, 0, (const struct sockaddr *)&serverAddress, sizeof(serverAddress));

    return 0;
}

static int createClientSocket() {
    serverAddress.sin_addr.s_addr = htonl(0xc0a8020e);
    //serverAddress.sin_addr.s_addr = htonl(0x7f000001);
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(serverPort);

    clientSocketFd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    if(clientSocketFd < 0) {
        return 1;
    }

    return 0;
}

static void receiveLoop() {
    while(true) {
        struct sockaddr_in socketAddress;

        socklen_t socketAddressLength = sizeof(socketAddress);

        uint8_t packetBuffer[1500];

        ssize_t packetSize = recvfrom(clientSocketFd, packetBuffer, SWTP_MAX_PACKET_SIZE, 0, (struct sockaddr *)&socketAddress, &socketAddressLength);

        if(packetSize < 0) {
            fprintf(stderr, "Receive loop thread error: %s\n", strerror(errno));
            break;
        }

        swtp_onPacketReceived(&clientPipe, packetBuffer, packetSize);
    }
}

static int tunReadLoop(void *arg) {
    (void)arg;

    uint8_t buffer[SWTP_MAX_PACKET_SIZE + 4];

    while(true) {
        ssize_t packetSize = read(tunDeviceFd, buffer, SWTP_MAX_PACKET_SIZE + 4);

        if(packetSize < 0) {
            return 1;
        }

        switch(detectPacketType(buffer)) {
            case SWTP_PACKETTYPE_IPV4: swtp_send(&clientPipe, SWTP_PACKETTYPE_IPV4, buffer + 4, packetSize - 4); break;
        }
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

        swtp_onClockTick(&clientPipe, &currentTime);

        thrd_sleep(&waitingTime, NULL);
    }

    return 0;
}

int swtp_onReceivePacket(swtp_pipe_t *pipe, swtp_packetType_t packetType, const void *packetBuffer, size_t packetSize) {
    if((swtp_packetType_t)packetType == SWTP_PACKETTYPE_IPV4) {
        uint8_t buffer[packetSize + 4];

        uint16_t etherType = htons(ETHERTYPE_IPV4);

        memcpy(buffer + 2, &etherType, 2);
        memcpy(buffer + 4, packetBuffer, packetSize);

        write(tunDeviceFd, buffer, packetSize + 4);
    }

    return 0;
}
