#!/usr/bin/env bash
set -e
TUNNEL_NAME=${TUNNEL_NAME:-lanecove.0}
CLIENT_IP=${CLIENT_IP:-10.9.0.1/24}
REMOTE_SUBNET=${REMOTE_SUBNET:-10.10.0.0/24}

ip tuntap del "$TUNNEL_NAME" mode tun 2>/dev/null || true
ip tuntap add "$TUNNEL_NAME" mode tun
ip link set "$TUNNEL_NAME" up
ip addr add "$CLIENT_IP" dev "$TUNNEL_NAME"
ip route add "$REMOTE_SUBNET" via "${CLIENT_IP%/*}"
