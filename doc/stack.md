# Network stack
Layer | Explanation | Encrypted
-|-|-
?|User data|Depends on tunneling layer
IP|Operator's IP network|Depends on tunneling layer
To be determined|Encapsulation tunneling protocol to reach the operator's network. A layer-2 tunneling protocol would add an useless overhead here, so it is recommended to choose a layer-3 tunneling protocol for this. Also, the protocol that will be chosen here **should** (highly recommended!) implement encryption, because Wi-Fi frames on public Wi-Fi networks are not encrypted.|No
UDP|Used for communicating with the server|No
IP|Access point's private IP network (should be 192.168.2.0/24 with gateway at 192.168.2.1)|No
802.11|802.11 frame protocol|No
Wi-Fi|Physical wireless connection to the access point|No
