# Summary
SWTP is a network tunnelling protocol designed for VPNs.
Its aim is to reduce network overhead compared to a classical VPN over TCP,
while making sure that packets are not lost.

# Packet format
Every SWTP packet has a 32-bit header that indicates the role of the packet:

```
Bit  | 10987654321098765432109876543210
-----+---------------------------------
DATA | 00sssssssssssssssrrrrrrrrrrrrrrr
-----+---------------------------------
TEST | 01sssssssssssssssrrrrrrrrrrrrrrr
-----+---------------------------------
REJ  | 10000000000000000rrrrrrrrrrrrrrr
-----+---------------------------------
ACK  | 11000000000000000rrrrrrrrrrrrrrr
```

*Note:* The data packets are an exception to this rule. Their header is
64 bits long. The next 32 bits are used to indicate the type of the carried
packet.

## Data packets
Data packets contain 4 main fields:
- Sent packet number (s): The number of the packet in the sender's window
- Received packet number (r): The number of the next expected packet by the
    sender
- Packet type: the type of the packet. The following values are defined:
    - 0: SWTCP
    - 1: IP
- Payload: user-defined

Packet sequence numbers are 15-bit wide, allowing packet numerotation from 0 to
32767. This is a relatively large value that can prevent network speed
limitations caused by window saturation.

## Control packets

##