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

// TODO: implement concurrency

#define MAX_RECEIVE_WINDOW_SIZE 8
#define READTIMEOUT_TIMED_OUT 1
#define READTIMEOUT_READ 0
#define READTIMEOUT_IO_ERROR -1

int tunDevice;
char tunDeviceName[16];
int clientSocket;
swtp_t swtp;
mtx_t swtp_mutex;
thrd_t tunDeviceReaderThread;

int connectToServer();
int tunReaderMainLoop(void *arg);
int mainLoop();
void alarmHandler(int signalNumber);

int main() {
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
        perror("Failed to create thread");
        return EXIT_FAILURE;
    }

    // Prepare alarm timeout
    signal(SIGALRM, alarmHandler);
    alarm(1);

    return mainLoop();
}

void alarmHandler(int signalNumber) {
    UNUSED_PARAMETER(signalNumber);

    swtp_onTimerTick(&swtp);

    alarm(1);
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

int connectToServer() {
    // Open the socket
    clientSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    if(clientSocket < 0) {
        return -1;
    }

    struct sockaddr_in serverAddress;
    memset(&serverAddress, 0, sizeof(struct sockaddr_in));

    serverAddress.sin_addr.s_addr = htonl(
        (192 << 24)
        | (168 << 16)
        | (1 << 8)
        | 20
    );

    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(5228);

    // Initialize the SWTP structure
    swtp_init(&swtp, clientSocket, (const struct sockaddr *)&serverAddress);

    // Send a SABM packet with the desired window size
    uint32_t sabmBuffer = htonl(0x80000000 | MAX_RECEIVE_WINDOW_SIZE);

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

    swtp.connected = true;

    if(swtp_initSendWindow(&swtp, sabmBuffer & 0x00007fff) != SWTP_SUCCESS) {
        perror("SWTP send window initialization failed");
        return -1;
    }

    // Set callback
    swtp.recvCallback = onFrameReceived;

    printf("Connection established.\n");

    return 0;
}
