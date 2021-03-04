#include <stdio.h>

#include "arpa/inet.h"

#include "common.h"
#include "swtp.h"

swtp_packetType_t detectPacketType(const void *buffer) {
    uint16_t etherType = ntohs(((uint16_t *)buffer)[1]);

    switch(etherType) {
        case ETHERTYPE_IPV4: return SWTP_PACKETTYPE_IPV4;
        default: return SWTP_PACKETTYPE_UNKNOWN;
    }
}
