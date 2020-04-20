#!/usr/bin/env bash
set -ex
TUNNEL_NAME=${TUNNEL_NAME:-tun2}
IP_ADDRESS=10.10.0.1/24

sudo ip tuntap del "${TUNNEL_NAME}" mode tun
sudo ip tuntap add "${TUNNEL_NAME}" mode tun
sudo ip link set ${TUNNEL_NAME} up
sudo ip addr add ${IP_ADDRESS} dev ${TUNNEL_NAME}
echo "Created tunnel ${TUNNEL_NAME} with ip address ${IP_ADDRESS}"
