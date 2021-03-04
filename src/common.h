#ifndef __COMMON_H__
#define __COMMON_H__

#define UNUSED_PARAMETER(x) ((void)x)

#include <stdint.h>

#include "swtp.h"

enum {
    ETHERTYPE_IPV4 = 0x0800
};

swtp_packetType_t detectPacketType(const void *buffer);

#endif
