# Network stack
## User plan
Layer | Explanation | Encrypted
-|-|-
?|User data|Depends on tunneling layer
IP|Operator's IP network|Depends on tunneling layer
SWTLLP|The logical link protocol that works over SWTP|Depends on tunneling layer
SWTP|Encapsulation tunneling protocol to reach the operator's network.|No by default, but can be configured by SWTLLP
UDP|Used for communicating with the server|No
IP|Access point's private IP network (should be 192.168.2.0/24 with gateway at 192.168.2.1)|No
802.11|802.11 frame protocol|No
Wi-Fi|Physical wireless connection to the access point|No

## Control plan

Layer | Explanation | Encrypted
-|-|-
SWTCP|The SWTP tunnel configuration protocol|Depends on tunneling layer
SWTLLP|The logical link protocol that works over SWTP|Depends on tunneling layer
SWTP|Encapsulation tunneling protocol to reach the operator's network.|No by default, but can be configured by SWTLLP
UDP|Used for communicating with the server|No
IP|Access point's private IP network (should be 192.168.2.0/24 with gateway at 192.168.2.1)|No
802.11|802.11 frame protocol|No
Wi-Fi|Physical wireless connection to the access point|No
