# SWTLLP
## Introduction
SWTLLP (which stands for SFR WiFi Tunnel Logical Link Protocol) is a protocol which works over SWTP. Its role is to identify the type of the payload.

## Goals
The only goal of SWTLLP is to carry both the payload and its type. This will allow a SWTP daemon to determine what to do with the packet.

## Assumptions
SWTLLP works over SWTP, which means that:
  - The length of a SWTLLP can be determined by a protocol under SWTLLP.
  - The integrity of a SWTLLP packet is already checked by a protocol under SWTLLP.
  - Any payload can have any type.

## Implications
  - SWTLLP has a single header field, which is the type of the payload.

## Frame format
Bytes are sent in network-order (big-endian)
```
+---------------------------------+---------+
| Protocol type (8 bits / 1 byte) | Payload |
+---------------------------------+---------+
```

The protocol type field contains a value that indicates the type of the payload:
Protocol type field value|Payload type
-------------------------|------------
0x00|SWTCP packet
0x01|IPv4 packet
0x02|IPv6 packet
Any other value|Reserved
