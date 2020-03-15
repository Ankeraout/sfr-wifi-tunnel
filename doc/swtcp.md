# SWTCP
## Introduction
SWTCP (which stands for SFR WiFi Tunnel Configuration Protocol) is a protocol that works over SWTLLP to configure a SWTP tunnel.
## Goals
The main goal of SWTCP is to add the possibility to exchange SWTP configuration packets.
## Assumptions
SWTCP works over SWTLLP, which works over SWTP, which means that:
  - The length of a SWTCP can be determined by a protocol under SWTCP.
  - The integrity of a SWTCP packet is already checked by a protocol under SWTCP.
## Implications
  - SWTCP packets do not include any packet length field in their header.
  - No data integrity check is done on SWTCP packets
## Packet format
Bytes are sent in network order (big-endian)
```
+-------------+---------+
| Packet type | Payload |
+-------------+---------+
```

Packet type is a 1-byte field that indicates the type of the SWTCP packet. 
It can take these values:
|Value | Packet direction | Packet type|
|------------------|-----------|--------|
|0x00 | Client &rarr; Server | IP configuration request|
|0x01 | Server &rarr; Client | Clear IP configuration|
|0x02 | Server &rarr; Client | Add IPv4 address|
|0x03 | Server &rarr; Client | Add IPv6 address|
|0x04 | Server &rarr; Client | Add IPv4 route|
|0x05 | Server &rarr; Client | Add IPv6 route|
|0x06 | Server &rarr; Client | Add IPv4 DNS server|
|0x07 | Server &rarr; Client | Add IPv6 DNS server|
|0x0e | Client &rarr; Server | Configuration command acknowledge|
|0x0f | Client &rarr; Server | Configuration command error|
|0x10 | Client &rarr; Server | Soft handover request|
|0x11 | Server &rarr; Client | Soft handover acknowledge|
|0x12 | Client &rarr; Server | Soft handover complete|
|0x13 | Client &rarr; Server | Hard handover request|
|0x14 | Server &rarr; Client | Hard handover complete|
|0x15 | Server &rarr; Client | Hard handover failed|
|0x20 | Client &rarr; Server | Get server properties|
|0x21 | Server &rarr; Client | Server properties|
|0x30 | Client &rarr; Server | Authentication request (+public key) (+challenge for server)|
|0x31 | Server &rarr; Client | Authentication refused|
|0x32 | Server &rarr; Client | Authentication acknowledge (+public key) (+challenge response) (+challenge for client)|
|0x33 | Client &rarr; Server | Client challenge response (+challenge response)|
|0x34 | Server &rarr; Client | Authentication complete (+secret key encrypted with the client's public key)|
|0x35 | Client &rlarr; Server | Initiate key exchange|

### 0x00 - IP configuration request
This packet is sent by the client to ask the server for IP configuration. It does not have any payload.

### 0x01 - Clear IP configuration
This packet is sent from the server to the client to ask it to clear the IP configuration of its TUN interface, which means:
  - Removal of all the IP addresses on the interface
  - Removal of all the IP routes previously added
  - Removal of all the DNS servers previously added
This packet doesn't have any payload.

### 0x02 - Add IPv4 address
This packet is sent from the server to the client to set the IPv4 address on the TUN interface.
The payload format is defined as such:
```
+--------------+-------------------------------------+
| IPv4 address (4 bytes) | IPv4 subnet mask (1 byte) |
+--------------+-------------------------------------+
```
The subnet mask contains the number of fixed bits in the network address.

Example: To add 192.168.0.123/24 to the client's TUN interface configuration, the server would send the following payload (hex): `C0 A8 00 7B 18`.

### 0x03 - Add IPv6 address
This packet is sent from the server to the client to set the IPv6 address on the TUN interface.
The payload format is defined as such:
```
+-------------------------+---------------------------+
| IPv6 address (16 bytes) | IPv6 subnet mask (1 byte) |
+-------------------------+---------------------------+
```
The subnet mask contains the number of fixed bits in the network address.

See "0x02 - Add IPv4 address" for a payload format example (adapt to IPv6)

### 0x04 - Add IPv4 route
TODO

### 0x05 - Add IPv6 route
TODO

### 0x06 - Add IPv4 DNS server
TODO

### 0x07 - Add IPv6 DNS server
TODO

### 0x0e - Configuration command acknowledge
This packet is sent by the client in response to a configuration command from the server, to indicate that the last command executed correctly.
This packet does not have any payload.

### 0x0f - Configuration command error
This packet is sent by the client in response to a configuration command from the server, to indicate that the last command failed.
This packet does not have any payload.

### 0x10 - Soft handover request
This packet is sent by the client to the server to indicate that the client's IP address is going to change.
The payload format is defined as such:
```
+----------------+--------------+
| New IP address | New UDP port |
+----------------+--------------+
```

### 0x11 - Soft handover acknowledge
This packet is sent by the server to the client in response to a soft handover request, to indicate that the handover request has been processed and that the client can use the new IP address.
This packet does not have any payload.

### 0x12 - Soft handover complete
This packet is sent by the client to the server from the new client's IP address, to indicate that the soft handover has been completed, and that the server can now "forget" the old client's IP address.
This packet does not have any payload.

### 0x13 - Hard handover request
This packet is sent by the client to the server to attempt to repair the SWTP tunnel after a connection loss and eventual IP address change.

*Note:* This could cause serious protocol security issues, therefore hard handover should be disabled for now.

### 0x14 - Hard handover complete
This packet is sent by the server to the client in order to confirm a hard handover. When this packet is sent, the tunnel is repaired and both the client and the server can start sending packets again.
This packet does not have any payload.

### 0x15 - Hard handover failed
This packet is sent by the server to the client in response to a hard handover request, in order to inform it that the requested hard handover is not possible.
This packet does not have any payload.

### 0x20 - Get server properties
This packet is sent by the client to the server immediately after SWTP tunnel establishment in order to determine the server configuration and how to react in the future.

This packet does not have any payload.

### 0x21 - Server properties
This packet is sent by the server to the client in response to a Get Server Properties packet.
The payload format is defined like this:
```
+------------------------+-------------------------------------+
| SWTCP version (1 byte) | Server configuration flags (1 byte) |
+------------------------+-------------------------------------+
```
The SWTCP version byte contains the version of SWTCP used by the server. If it is not compatible with the client's version of SWTCP, then the client should break the connection.

The Server configuration flags byte contains configuration flags. Here is the meaning of this byte:
Byte number|Role|Meaning if true|Meaning if false
-|-|-|-
0|Encryption required|Encryption is required|Encryption is not required
1|Encryption supported|Encryption is supported by the server|Encryption is not supported by the server
2|Reserved|-|-
3|Reserved|-|-
4|Reserved|-|-
5|Reserved|-|-
6|Reserved|-|-
7|Reserved|-|-

### 0x30 - Authentication request
This packet is sent by the client to the server in order to ask the server to proceed to SWTP traffic encryption.
This packet contains the client's public key and a challenge for the server.

The packet format is defined like this:
```
+-------------------------------------+---------------------+-----------------------------+
| Client's public key length (1 byte) | Client's public key | Server challenge (16 bytes) |
+-------------------------------------+---------------------+-----------------------------+
```

The client's public key is a RSA public key. The value of the public key length field are defined as :
Public key length byte value | Public key length (bits)
-----------------------------|-------------------------
0x00 | 1024
0x01 | 2048
0x02 | 3072
0x03 | 4096
Other values | Reserved

The server challenge is an array of 16 random bytes. This array must be encrypted by the server using the server's private key, and sent back to the client later in order to prove the server's identity.
### 0x31 - Authentication refused
This packet is sent by the server to the client in order to inform it that its authentication attempt has failed.
This can happen for example when the server only accepts some public keys and does not know the client's public key.
The client should disconnect after receiving this packet.
This packet does not have any payload.
### 0x32 - Authentication acknowledge
This packet is sent by the server to the client in response to the authentication request packet. Its role is to transmit the server's public key, and to prove the server's identity using the challenge sent by the client earlier.

The payload format is defined as such:
```
+-------------------------------------+---------------------+--------------------+----------------------+
| Server's public key length (1 byte) | Server's public key | Challenge response | Challenge for client |
+-------------------------------------+---------------------+--------------------+----------------------+
```

See "0x30 - Authentication request" packet to find more about the server's public key length field format.

The server's public key is a RSA public key.

The challenge response is the response to the client's challenge. This field has a variable length, depending on the server's key length. Its value is basically the client's challenge value encrypted using the server's private key. If the client can decrypt it using the server's public key, then the server is authenticated.

The challenge for client field contains an array of 16 random bytes, that is a challenge, this time for the client. It is used by the server to prove the client's identity. See "0x30 - Authentication request" for more information.
### 0x33 - Client challenge response
This packet contains the client challenge response, which is the challenge sent by the server encrypted using the client's private key. The client is correctly authenticated if the server can decrypt this value using the client's public key.

The packet format is defined as such:
```
+--------------------+
| Challenge response |
+--------------------+
```
The challenge response field has a variable length, depending on the client's key length.
### 0x34 - Authentication complete
This packet is sent by the server to the client to confirm the authentication. It contains a symmetric encryption algorithm secret key, encrypted using the client's public key to use for further exchanges.

The packet format is defined as such:
```
+------------+
| Secret key |
+------------+
```
The secret key's length is determined using the frame length.
### 0x35 - Initiate key exchange
This packet can be sent by any of the tunnel's ends, indicating that it requests a new key exchange. The traffic remains encrypted with the old secret key until the "Authentication complete" packet is sent.
