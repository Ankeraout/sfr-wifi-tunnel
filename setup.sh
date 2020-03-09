ip addr add 10.0.0.2/24 dev tun0
ip link set up tun0
ip route add 10.0.1.0/24 via 10.0.0.2 dev tun0