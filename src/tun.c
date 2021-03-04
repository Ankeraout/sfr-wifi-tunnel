#include <string.h>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/if.h>
#include <linux/if_tun.h>

int openTunDevice(char *deviceName) {
    struct ifreq ifr;

    int fd = open("/dev/net/tun", O_RDWR);

    if(fd < 0) {
        return fd;
    }

    memset(&ifr, 0, sizeof(ifr));

    ifr.ifr_flags = IFF_TUN;

    if(*deviceName) {
        strncpy(ifr.ifr_name, deviceName, IFNAMSIZ - 1);
    }

    int err = ioctl(fd, TUNSETIFF, &ifr);

    if(err) {
        close(fd);
        return err;
    }

    strcpy(deviceName, ifr.ifr_name);
    
    return fd;
}

int closeTunDevice(int fd) {
    return close(fd);
}
