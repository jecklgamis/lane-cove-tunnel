#!/usr/bin/env bash
set -e
TUNNEL_NAME=${TUNNEL_NAME:-lanecove.0}
PEER_IP=${PEER_IP:-10.9.0.1/24}
PEER_ROUTES=${PEER_ROUTES:-}

ip tuntap del "$TUNNEL_NAME" mode tun 2>/dev/null || true
ip tuntap add "$TUNNEL_NAME" mode tun
ip link set "$TUNNEL_NAME" up
ip addr add "$PEER_IP" dev "$TUNNEL_NAME"
for route in $PEER_ROUTES; do
    ip route add "$route" via "${PEER_IP%/*}"
done
