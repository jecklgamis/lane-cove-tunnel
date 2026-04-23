#!/usr/bin/env bash
set -e
TUNNEL_NAME=${TUNNEL_NAME:-lanecove0}
PEER_IP=${PEER_IP:-10.9.0.1/24}
PEER_ROUTES=${PEER_ROUTES:-}

log() { echo "$(date '+%Y-%m-%d %H:%M:%S') [INFO]  $*"; }

log "Creating TUN interface: $TUNNEL_NAME"
ip tuntap del "$TUNNEL_NAME" mode tun 2>/dev/null || true
ip tuntap add "$TUNNEL_NAME" mode tun
log "Bringing up $TUNNEL_NAME"
ip link set "$TUNNEL_NAME" up
log "Assigning IP $PEER_IP to $TUNNEL_NAME"
ip addr add "$PEER_IP" dev "$TUNNEL_NAME"
for route in $PEER_ROUTES; do
    log "Adding route $route via ${PEER_IP%/*}"
    ip route add "$route" via "${PEER_IP%/*}"
done
log "Disabling send_redirects"
sysctl -w net.ipv4.conf.all.send_redirects=0 >/dev/null 2>&1 || true
sysctl -w "net.ipv4.conf.${TUNNEL_NAME}.send_redirects=0" >/dev/null 2>&1 || true
nmcli device set "$TUNNEL_NAME" managed no 2>/dev/null || true
log "Tunnel $TUNNEL_NAME ready"
