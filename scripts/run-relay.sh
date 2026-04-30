#!/usr/bin/env bash
set -ea
# Relay peer: inbound-only, routes between peer1 and peer2.
# Required key files: relay.key, relay.crt, peer-1.crt, peer-2.crt
# Optional env vars:
#   TUNNEL_NAME  (default: lanecove0)
#   PEER_PORT    (default: 5040)
#   RELAY_KEY    (default: relay.key)
#   RELAY_CRT    (default: relay.crt)
#   PEER_1_CRT   (default: peer-1.crt)
#   PEER_2_CRT   (default: peer-2.crt)

TUNNEL_NAME=${TUNNEL_NAME:-lanecove0}
PEER_PORT=${PEER_PORT:-5040}
RELAY_KEY=${RELAY_KEY:-relay.key}
RELAY_CRT=${RELAY_CRT:-relay.crt}
PEER_1_CRT=${PEER_1_CRT:-peer-1.crt}
PEER_2_CRT=${PEER_2_CRT:-peer-2.crt}

OPENSSL=openssl
if [[ "$(uname)" == "Darwin" ]]; then
    BREW_OPENSSL="$(brew --prefix openssl 2>/dev/null)/bin/openssl"
    [[ -x "$BREW_OPENSSL" ]] && OPENSSL="$BREW_OPENSSL"
fi

extract_pub() { ${OPENSSL} pkey -in "$1" -pubin -outform DER | tail -c 32 | od -An -tx1 | tr -d ' \n'; }

PEER_1_PUB=$(extract_pub "$PEER_1_CRT")
PEER_2_PUB=$(extract_pub "$PEER_2_CRT")
echo "peer1 public key: ${PEER_1_PUB}"
echo "peer2 public key: ${PEER_2_PUB}"

export PEER_PUB_1=$PEER_1_PUB
export PEER_PUB_2=$PEER_2_PUB
export PEER_ALLOWED_IPS_1="10.9.0.2/32"
export PEER_ALLOWED_IPS_2="10.9.0.3/32"

export PEER_KEY=relay.key
./run-peer-1s-relay.sh