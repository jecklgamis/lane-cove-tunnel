#!/usr/bin/env bash
set -ex
TUNNEL_NAME=${TUNNEL_NAME:-tun2}
IP_ADDRESS=10.10.0.1/24

sudo ip tuntap del "${TUNNEL_NAME}" mode tun
sudo ip tuntap add "${TUNNEL_NAME}" mode tun
sudo ip link set "${TUNNEL_NAME}" up
sudo ip addr add ${IP_ADDRESS} dev "${TUNNEL_NAME}"
echo "Creating tunnel ${TUNNEL_NAME} with ip address ${IP_ADDRESS}"
sudo ip route add 10.9.0.0/24 via 10.10.0.1
echo "Added route to 10.9.0.0/24 network via ${TUNNEL_NAME}"

