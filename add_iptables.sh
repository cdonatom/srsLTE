#! /bin/bash

echo "Enabling IPTABLES rules"
sudo iptables -t nat -A POSTROUTING -s 172.21.20.0/24 -o eth0 -j MASQUERADE
echo "Enabling IPv4 forwarding for network interfaces"
sudo sysctl -w net.ipv4.ip_forward=1

