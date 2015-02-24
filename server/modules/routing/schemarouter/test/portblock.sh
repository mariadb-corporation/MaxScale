#!/bin/bash
if [ $# -lt 1 ]
then
    echo "Usage $0 <port to block>"
    exit 1
fi
sudo iptables -I INPUT 1 -i lo -p tcp --dport $1 -j DROP
sudo iptables -I INPUT 1 -i lo -p tcp --sport $1 -j DROP
echo "Traffic to port $1 blocked."
