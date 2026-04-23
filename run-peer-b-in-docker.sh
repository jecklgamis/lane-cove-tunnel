#!/usr/bin/env bash
set -a
# peerB: connects outbound to the relay, overlay IP 10.9.0.3.
# Required key files: peer-b.key, peer-b.crt, relay.crt
# Required env vars:
#   RELAY_IP     — public IP of the relay
# Optional env vars:
#   TUNNEL_NAME  (default: lanecove0)
#   PEER_PORT    (default: 5040)
#   RELAY_PORT   (default: 5040)
#   PEER_B_KEY   (default: peer-b.key)
#   PEER_B_CRT   (default: peer-b.crt)
#   RELAY_CRT    (default: relay.crt)
#   ENVOY_HOST_PORT      (default: 15040)
#   ENVOY_UPSTREAM_HOST  — upstream host for Envoy TCP proxy (required for Envoy)
#   ENVOY_UPSTREAM_PORT  (default: 80)

TUNNEL_NAME=${TUNNEL_NAME:-lanecove0}
PEER_PORT=${PEER_PORT:-5040}
RELAY_IP=${RELAY_IP:-$(ipconfig getifaddr en0)}
RELAY_IP=${RELAY_IP:-$(ipconfig getifaddr en1)}
RELAY_IP=${RELAY_IP:?RELAY_IP could not be determined (set it explicitly)}
RELAY_PORT=${RELAY_PORT:-5040}
PEER_B_KEY=${PEER_B_KEY:-peer-b.key}
PEER_B_CRT=${PEER_B_CRT:-peer-b.crt}
RELAY_CRT=${RELAY_CRT:-relay.crt}

OPENSSL=openssl
if [[ "$(uname)" == "Darwin" ]]; then
    BREW_OPENSSL="$(brew --prefix openssl 2>/dev/null)/bin/openssl"
    [[ -x "$BREW_OPENSSL" ]] && OPENSSL="$BREW_OPENSSL"
fi

RELAY_PUB=$(${OPENSSL} pkey -in "$RELAY_CRT" -pubin -outform DER | tail -c 32 | od -An -tx1 | tr -d ' \n')
echo "Relay public key: ${RELAY_PUB}"
echo "Connecting to relay: ${RELAY_IP}:${RELAY_PORT}"

docker build -f Dockerfile.peer \
  --build-arg KEY_FILE="${PEER_B_KEY}" \
  --build-arg CRT_FILE="${PEER_B_CRT}" \
  -t lane-cove-tunnel-peer-b:latest .

PEER_B_HOST_PORT=${PEER_B_HOST_PORT:-5043}
ENVOY_HOST_PORT=${ENVOY_HOST_PORT:-15040}
ENVOY_UPSTREAM_HOST=10.9.0.2
ENVOY_UPSTREAM_PORT=${ENVOY_UPSTREAM_PORT:-80}

docker run \
  --cap-add=NET_ADMIN \
  --device=/dev/net/tun \
  -p "${PEER_B_HOST_PORT}:${PEER_PORT}/udp" \
  -p "15043:15040" \
  -p "15053:15050" \
  -p "9902:9901" \
  -e TUNNEL_NAME="${TUNNEL_NAME}" \
  -e ENVOY_UPSTREAM_HOST="${ENVOY_UPSTREAM_HOST}" \
  -e PEER_PORT="${PEER_PORT}" \
  -e PEER_IP="10.9.0.3/24" \
  -e PEER_PUB_1="${RELAY_PUB}" \
  -e PEER_ENDPOINT_1="${RELAY_IP}:${RELAY_PORT}" \
  -e PEER_ALLOWED_IPS_1="10.9.0.0/24" \
  -it lane-cove-tunnel-peer-b:latest
