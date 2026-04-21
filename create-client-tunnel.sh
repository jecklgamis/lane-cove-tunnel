#!/usr/bin/env bash
TUNNEL_NAME=${TUNNEL_NAME:-lanecove-udp}
IP_ADDRESS=10.9.0.1
IP_ADDRESS_CIDR="$IP_ADDRESS/24"
REMOTE_SUBNET="10.10.0.0/24"

ip tuntap del "${TUNNEL_NAME}" mode tun
ip tuntap add "${TUNNEL_NAME}" mode tun && echo "Created tunnel $TUNNEL_NAME"
ip link set "${TUNNEL_NAME}" up && echo "Tunnel $TUNNEL_NAME is up"
ip addr add ${IP_ADDRESS} dev ${TUNNEL_NAME} && echo "Created tunnel ${TUNNEL_NAME} with ip address ${IP_ADDRESS}"
ip route add ${REMOTE_SUBNET} via ${IP_ADDRESS} && echo "Added route to $REMOTE_SUBNET network via ${TUNNEL_NAME}"
ifconfig $TUNNEL_NAME
ip route show
