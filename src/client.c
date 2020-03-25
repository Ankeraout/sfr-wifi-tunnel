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
#include <sys/select.h>
#include <signal.h>
#include <netdb.h>

#define MAX_HOSTNAME_LENGTH 256

char serverHostname[MAX_HOSTNAME_LENGTH + 1];
int serverPort = SWTP_PORT;

int tunDevice;
char tunDeviceName[16];
int clientSocket;
int receiveWindowSize;
int maxSendWindowSize = 0;
swtp_t swtp;
mtx_t swtp_mutex;
thrd_t tunDeviceReaderThread;
thrd_t timerThread;

int connectToServer();
int tunReaderMainLoop(void *arg);
int timerThreadMainLoop(void *arg);
int mainLoop();
int parseCommandLineParameters(int argc, const char **argv);

int main(int argc, const char **argv) {
    if(parseCommandLineParameters(argc, argv)) {
        printf("Failed to parse command-line parameters.\n");
        return EXIT_FAILURE;
    }

    tunDevice = libtun_open(tunDeviceName);

    // Connect to the server
    if(connectToServer()) {
        perror("Server connection failed");
        return EXIT_FAILURE;
    }

    if(tunDevice < 0) {
        perror("Failed to open TUN device");
        return EXIT_FAILURE;
    }

    if(mtx_init(&swtp_mutex, mtx_plain)) {
        perror("Failed to create mutex");
        return EXIT_FAILURE;
    }

    if(thrd_create(&tunDeviceReaderThread, tunReaderMainLoop, NULL)) {
        perror("Failed to create tun device reader thread");
        return EXIT_FAILURE;
    }

    if(thrd_create(&timerThread, timerThreadMainLoop, NULL)) {
        perror("Failed to create timer thread");
        return EXIT_FAILURE;
    }

    return mainLoop();
}

int parseCommandLineParameters(int argc, const char **argv) {
    bool flag_windowSize = false;
    bool flag_serverHostname = false;
    bool flag_serverPort = false;
    bool flag_maxSendWindowSize = false;
    
    bool flag_windowSize_set = false;
    bool flag_serverHostname_set = false;

    for(int i = 1; i < argc; i++) {
        if(flag_windowSize) {
            flag_windowSize = false;
            
            if(sscanf(argv[i], "%d", &receiveWindowSize) == EOF) {
                printf("Failed to parse argument value to --receive-window-size.\n");
                return 1;
            }

            if(receiveWindowSize <= 0 || receiveWindowSize > SWTP_MAX_WINDOW_SIZE) {
                printf("Invalid value for --receive-window-size. Expected an integer between 1 and %d included.\n", SWTP_MAX_WINDOW_SIZE);
                return 1;
            }

            flag_windowSize_set = true;
        } else if(flag_maxSendWindowSize) {
            flag_maxSendWindowSize = false;

            if(sscanf(argv[i], "%d", &maxSendWindowSize) == EOF) {
                printf("Failed to parse argument value to --max-send-window-size.\n");
                return 1;
            }

            if(maxSendWindowSize <= 0 || maxSendWindowSize > SWTP_MAX_WINDOW_SIZE) {
                printf("Invalid value for --max-send-window-size. Expected an integer between 1 and %d included.\n", SWTP_MAX_WINDOW_SIZE);
                return 1;
            }
        } else if(flag_serverHostname) {
            flag_serverHostname = false;
            strncpy(serverHostname, argv[i], MAX_HOSTNAME_LENGTH);
            flag_serverHostname_set = true;
        } else if(flag_serverPort) {
            flag_serverPort = false;

            sscanf(argv[i], "%d", &serverPort);

            if(serverPort < 0 || serverPort > 65535) {
                printf("Invalid value for --server-port. Expected an integer between 0 and 65535 included.\n");
                return 1;
            }
        } else if(strcmp(argv[i], "--receive-window-size") == 0) {
            flag_windowSize = true;
        } else if(strcmp(argv[i], "--hostname") == 0) {
            flag_serverHostname = true;
        } else if(strcmp(argv[i], "--port") == 0) {
            flag_serverPort = true;
        } else if(strcmp(argv[i], "--max-send-window-size") == 0) {
            flag_maxSendWindowSize = true;
        } else {
            printf("Unknown argument \"%s\".", argv[i]);
            return 1;
        }
    }

    if(flag_windowSize) {
        printf("--receive-window-size expected an integer value.\n");
        return 1;
    } else if(flag_serverHostname) {
        printf("--hostname expected a hostname.\n");
        return 1;
    } else if(flag_serverPort) {
        printf("--port expected an integer value.\n");
        return 1;
    } else if(flag_maxSendWindowSize) {
        printf("--max-send-window-size expected an integer value.\n");
        return 1;
    } else if(!flag_windowSize_set) {
        printf("--receive-window-size was not set.\n");
        return 1;
    } else if(!flag_serverHostname_set) {
        printf("--hostname was not set.\n");
        return 1;
    }

    return 0;
}

int timerThreadMainLoop(void *arg) {
    UNUSED_PARAMETER(arg);

    while(swtp.connected) {
        if(swtp_onTimerTick(&swtp) != SWTP_SUCCESS) {
            break;
        }

        sleep(1);
    }

    return 0;
}

int mainLoop() {
    struct sockaddr_in serverAddress;

    swtp_frame_t buffer;
    socklen_t serverAddressLength;
    
    while(true) {
        ssize_t size = recvfrom(clientSocket, &buffer.frame, SWTP_MAX_FRAME_SIZE, 0, (struct sockaddr *)&serverAddress, &serverAddressLength);

        if(size < 0) {
            perror("Failed to read from client socket");
            return 1;
        }

        buffer.size = size;

        if(swtp_onFrameReceived(&swtp, &buffer) != SWTP_SUCCESS) {
            perror("SWTP failed to handle received frame");
            return 1;
        }
    }

    return 0;
}

int tunReaderMainLoop(void *arg) {
    UNUSED_PARAMETER(arg);
    
    uint8_t buffer[SWTP_MAX_PAYLOAD_SIZE];

    while(true) {
        ssize_t packetSize = read(tunDevice, buffer, SWTP_MAX_PAYLOAD_SIZE);

        if(packetSize < 0) {
            return 1;
        }
        
        if(swtp_sendDataFrame(&swtp, buffer, packetSize) != SWTP_SUCCESS) {
            return 1;
        }
    }

    return 0;
}

void onFrameReceived(swtp_t *swtp, const void *buffer, size_t size) {
    UNUSED_PARAMETER(swtp);
    write(tunDevice, buffer, size);
}

int resolveHostname(const char *hostname, in_addr_t *address) {
    struct hostent *hostEntry = gethostbyname(hostname);

    if(hostEntry == NULL) {
        return -1;
    }

    memcpy(address, hostEntry->h_addr_list[0], sizeof(in_addr_t));

    return 0;
}

void onDisconnect(swtp_t *swtp, int reason) {
    UNUSED_PARAMETER(swtp);
    UNUSED_PARAMETER(reason);

    printf("Connection lost.\n");

    exit(0);
}

int connectToServer() {
    // Open the socket
    clientSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    if(clientSocket < 0) {
        return -1;
    }

    struct sockaddr_in serverAddress;
    memset(&serverAddress, 0, sizeof(struct sockaddr_in));

    if(resolveHostname(serverHostname, &serverAddress.sin_addr.s_addr)) {
        return -1;
    }

    printf("Resolved %s to %s\n", serverHostname, inet_ntoa(serverAddress.sin_addr));

    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(5228);

    // Initialize the SWTP structure
    swtp_init(&swtp, clientSocket, (const struct sockaddr *)&serverAddress);

    // Send a SABM packet with the desired window size
    uint32_t sabmBuffer = htonl(0x80000000 | receiveWindowSize);

    printf("Connecting to %s:%d...\n", inet_ntoa(*(struct in_addr *)&serverAddress.sin_addr), serverPort);

    if(sendto(clientSocket, &sabmBuffer, 4, 0, &swtp.socketAddress, sizeof(struct sockaddr_in)) < 0) {
        return -1;
    }

    swtp_frame_t buffer;
    socklen_t serverAddressLength;
    buffer.size = recvfrom(clientSocket, &buffer.frame, SWTP_MAX_FRAME_SIZE, 0, (struct sockaddr *)&serverAddress, &serverAddressLength);
    
    // Expect a SABM packet
    if(buffer.size != 4) {
        printf("was not SABM (bad length %d)\n", (int)buffer.size);
        return -1;
    }

    sabmBuffer = ntohl(*(uint32_t *)buffer.frame.header);

    if((sabmBuffer & 0xffff8000) != 0x80000000) {
        printf("was not SABM (bad contents 0x%08x)\n", sabmBuffer);
        return -1;
    }

    int sendWindowSize = sabmBuffer & 0x00007fff;

    if(maxSendWindowSize > 0) {
        if(sendWindowSize < maxSendWindowSize) {
            printf("Reducing send window size from %d to %d.\n", sendWindowSize, maxSendWindowSize);
            sendWindowSize = maxSendWindowSize;
        }
    }

    if(swtp_initSendWindow(&swtp, sendWindowSize) != SWTP_SUCCESS) {
        perror("SWTP send window initialization failed");
        return -1;
    }

    // Set callbacks
    swtp.recvCallback = onFrameReceived;
    swtp.disconnectCallback = onDisconnect;

    printf("Connection established.\n");

    return 0;
}
