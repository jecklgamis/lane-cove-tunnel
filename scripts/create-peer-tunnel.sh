#!/usr/bin/env bash
set -e
if [[ $# -ge 2 ]]; then
    TUNNEL_NAME="$1"
    PEER_IP="$2"
    shift 2
    PEER_ROUTES="$*"
elif [[ -z "${TUNNEL_NAME:-}" || -z "${PEER_IP:-}" ]]; then
    echo "Usage: $0 <tunnel_name> <peer_ip/cidr> [route1 route2 ...]"
    echo "Example: $0 lanecove0 10.9.0.2/24 10.9.0.0/24"
    exit 1
fi

log() { echo "$(date '+%Y-%m-%d %H:%M:%S') [INFO]  $*"; }

log "Creating TUN interface: $TUNNEL_NAME"
ip tuntap del "$TUNNEL_NAME" mode tun 2>/dev/null || true
ip tuntap add "$TUNNEL_NAME" mode tun user "${SUDO_USER:-$(whoami)}"
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
