#!/usr/bin/env bash
set -e
# Relay peer: inbound-only, routes between peerA and peerB.
# Required key files: relay.key, relay.crt, peer-a.crt, peer-b.crt
# Optional env vars:
#   TUNNEL_NAME  (default: lanecove.0)
#   PEER_PORT    (default: 5040)
#   RELAY_KEY    (default: relay.key)
#   RELAY_CRT    (default: relay.crt)
#   PEER_A_CRT   (default: peer-a.crt)
#   PEER_B_CRT   (default: peer-b.crt)

TUNNEL_NAME=${TUNNEL_NAME:-lanecove.0}
PEER_PORT=${PEER_PORT:-5040}
RELAY_KEY=${RELAY_KEY:-relay.key}
RELAY_CRT=${RELAY_CRT:-relay.crt}
PEER_A_CRT=${PEER_A_CRT:-peer-a.crt}
PEER_B_CRT=${PEER_B_CRT:-peer-b.crt}

OPENSSL=openssl
if [[ "$(uname)" == "Darwin" ]]; then
    BREW_OPENSSL="$(brew --prefix openssl 2>/dev/null)/bin/openssl"
    [[ -x "$BREW_OPENSSL" ]] && OPENSSL="$BREW_OPENSSL"
fi

extract_pub() { ${OPENSSL} pkey -in "$1" -pubin -outform DER | tail -c 32 | od -An -tx1 | tr -d ' \n'; }

PEER_A_PUB=$(extract_pub "$PEER_A_CRT")
PEER_B_PUB=$(extract_pub "$PEER_B_CRT")
echo "peerA public key: ${PEER_A_PUB}"
echo "peerB public key: ${PEER_B_PUB}"

docker build -f Dockerfile.peer \
  --build-arg KEY_FILE="${RELAY_KEY}" \
  --build-arg CRT_FILE="${RELAY_CRT}" \
  -t lane-cove-tunnel-relay:latest .

docker run \
  --cap-add=NET_ADMIN \
  --device=/dev/net/tun \
  -e TUNNEL_NAME="${TUNNEL_NAME}" \
  -e PEER_PORT="${PEER_PORT}" \
  -e PEER_IP="10.9.0.1/24" \
  -e PEER_PUB_1="${PEER_A_PUB}" \
  -e PEER_ALLOWED_IPS_1="10.9.0.2/32" \
  -e PEER_PUB_2="${PEER_B_PUB}" \
  -e PEER_ALLOWED_IPS_2="10.9.0.3/32" \
  -p "${PEER_PORT}:${PEER_PORT}/udp" \
  -it lane-cove-tunnel-relay:latest
