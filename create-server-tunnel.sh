#!/usr/bin/env bash
set -e
TUNNEL_NAME=${TUNNEL_NAME:-lanecove}
CURRENT_IP=$(ip route get 1.1.1.1 | grep -oP 'src \K[\d.]+')
LAST_OCTET=$(echo "$CURRENT_IP" | cut -d. -f4)
IP_ADDRESS="10.10.0.${LAST_OCTET}/24"

echo "CURRENT_IP is $CURRENT_IP"
echo "Overlay network IP_ADDRESS is $IP_ADDRESS"

#ip tuntap del "${TUNNEL_NAME}" mode tun
ip tuntap add "${TUNNEL_NAME}" mode tun
ip link set "${TUNNEL_NAME}" up
ip addr add ${IP_ADDRESS} dev "${TUNNEL_NAME}"
echo "Creating tunnel ${TUNNEL_NAME} with ip address ${IP_ADDRESS}"
ip route add 10.9.0.0/24 via 10.10.0.1
echo "Added route to 10.9.0.0/24 network via ${TUNNEL_NAME}"

