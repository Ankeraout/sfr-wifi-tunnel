#ifndef __TUN_H__
#define __TUN_H__

extern int openTunDevice(char *deviceName);
extern int closeTunDevice(int fd);

#endif
