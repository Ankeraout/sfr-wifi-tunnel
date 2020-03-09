#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

#include <libtun/libtun.h>

void ipv4_callback(const uint8_t *buffer, const size_t size);
void ipv6_callback(const uint8_t *buffer, const size_t size);
void mainLoop(int tun_fd);

int main(int argc, char **argv) {
    char deviceName[16] = "";

    int tun_fd = libtun_open(deviceName);

    if(tun_fd < 0) {
        perror("Failed to allocate tun device");
        return 1;
    }

    printf("Allocated tun device: %s\n", deviceName);

    mainLoop(tun_fd);

    libtun_close(tun_fd);

    return 0;
}

void mainLoop(int tun_fd) {
    uint8_t packetBuffer[1500];

    while(true) {
        size_t packetSize = (size_t)read(tun_fd, packetBuffer, 1500) - 4;

        printf("Received packet of size %d\n", (int)packetSize);
        
        for(int i = 0; i < packetSize; i++) {
            printf("%02x ", packetBuffer[i]);
        }

        printf("\n");

        switch(packetBuffer[4] >> 4) {
            case 4: // IPv4
            ipv4_callback(packetBuffer + 4, packetSize);
            break;

            case 6: // IPv6
            ipv6_callback(packetBuffer + 4, packetSize);
            break;

            default: // Other versions of IP
            printf("IP version %d\n", packetBuffer[4] >> 4);
            break;
        }
    }
}

void ipv4_callback(const uint8_t *buffer, const size_t size) {
    printf("Source IP address     : %d.%d.%d.%d\n", buffer[12], buffer[13], buffer[14], buffer[15]);
    printf("Destination IP address: %d.%d.%d.%d\n", buffer[16], buffer[17], buffer[18], buffer[19]);
}

void ipv6_callback(const uint8_t *buffer, const size_t size) {
    printf("IPv6 packet\n");
}
