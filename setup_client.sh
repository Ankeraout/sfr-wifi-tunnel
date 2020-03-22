ip link set dev tun0 mtu 1400
ip addr add 10.0.0.2/24 dev tun0
ip link set up tun0
