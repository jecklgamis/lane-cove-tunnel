#!/usr/bin/env bash
set -ex
TUNNEL_NAME=${TUNNEL_NAME:-tun2}
IP_ADDRESS=10.9.0.1/24

echo "Creating tunnel ${TUNNEL_NAME}"
sudo openvpn --rmtun --dev "${TUNNEL_NAME}"
sudo openvpn --mktun --dev "${TUNNEL_NAME}"
sudo ip link set "${TUNNEL_NAME}" up
sudo ip addr add ${IP_ADDRESS} dev "${TUNNEL_NAME}"
ip link show