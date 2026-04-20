#!/usr/bin/env bash
set -e
TUNNEL_NAME=${TUNNEL_NAME:-lanecove}
IP_ADDRESS=10.9.0.1
IP_ADDRESS_CIDR="$IP_ADDRESS/24"

ip tuntap del "$TUNNEL_NAME" mode tun
ip tuntap add "$TUNNEL_NAME" mode tun && echo "Created tunnel $TUNNEL_NAME"
ip link set "$TUNNEL_NAME" up && echo "$TUNNEL_NAME is up"
ip addr add ${IP_ADDRESS} dev "$TUNNEL_NAME" && echo "Assigned $IP_ADDRESS to $TUNNEL_NAME device"
ip route add $IP_ADDRESS_CIDR via $IP_ADDRESS && echo "Added $IP_ADDRESS_CIDR route via $IP_ADDRESS"
echo "Created route to 10.10.0.0/24 network via $TUNNEL_NAME"
