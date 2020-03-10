#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <libtun/libtun.h>
#include <common.h>

void ipv4_callback(const uint8_t *buffer, const size_t size);
void ipv6_callback(const uint8_t *buffer, const size_t size);
void mainLoop(int tun_fd);

int main(int argc, char **argv) {
    UNUSED_PARAMETER(argc);
    UNUSED_PARAMETER(argv);

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
        uint16_t etherType = ntohs(*(uint16_t *)(packetBuffer + 2));

        switch(etherType) {
            case 0x0800: ipv4_callback(packetBuffer + 4, packetSize); break;
            case 0x86dd: ipv6_callback(packetBuffer + 4, packetSize); break;
            default: printf("Ignored frame with unknown ethertype 0x%04x\n", etherType); break;
        }
    }
}

void ipv4_callback(const uint8_t *buffer, const size_t size) {
    UNUSED_PARAMETER(size);

    printf("Source IP address     : %d.%d.%d.%d\n", buffer[12], buffer[13], buffer[14], buffer[15]);
    printf("Destination IP address: %d.%d.%d.%d\n", buffer[16], buffer[17], buffer[18], buffer[19]);
}

void ipv6_callback(const uint8_t *buffer, const size_t size) {
    UNUSED_PARAMETER(buffer);
    UNUSED_PARAMETER(size);

    printf("IPv6 packet\n");
}
