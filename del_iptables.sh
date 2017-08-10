#! /bin/bash

if [ "$#" -lt 1 ];then
    echo "Execute as: $0 <dev>"
    exit 1
fi

echo "Disabling IPTABLES rules"
sudo iptables -t nat -D POSTROUTING -s 172.21.20.0/24 -o $1 -j MASQUERADE
echo "Disabling IPv4 forwarding for network interfaces"
sudo sysctl -w net.ipv4.ip_forward=0
