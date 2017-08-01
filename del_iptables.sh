#! /bin/bash
echo "Disabling IPTABLES rules"
sudo iptables -t nat -D POSTROUTING -s 172.21.20.0/24 -o eth0 -j MASQUERADE
echo "Disabling IPv4 forwarding for network interfaces"
sudo sysctl -w net.ipv4.ip_forward=0
