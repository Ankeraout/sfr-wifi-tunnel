ip link set dev tun0 mtu 1492
ip addr add 10.0.0.1/24 dev tun0
ip link set up tun0
