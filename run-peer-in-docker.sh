#!/usr/bin/env bash
set -e
TUNNEL_NAME=${TUNNEL_NAME:-lanecove.0}
PEER_PORT=${PEER_PORT:-5040}
PEER_IP=${PEER_IP:-10.9.0.1/24}
PEER_ROUTES=${PEER_ROUTES:-}

ENV_ARGS=()
i=1
while true; do
    pub_var="PEER_PUB_${i}"
    ep_var="PEER_ENDPOINT_${i}"
    routes_var="PEER_ALLOWED_IPS_${i}"
    [[ -z "${!pub_var}" ]] && break
    ENV_ARGS+=(-e "${pub_var}=${!pub_var}")
    [[ -n "${!ep_var}" ]] && ENV_ARGS+=(-e "${ep_var}=${!ep_var}")
    [[ -n "${!routes_var}" ]] && ENV_ARGS+=(-e "${routes_var}=${!routes_var}")
    i=$((i + 1))
done

docker build -f Dockerfile.peer -t lane-cove-tunnel-peer:latest .
docker run \
  --cap-add=NET_ADMIN \
  --device=/dev/net/tun \
  -e TUNNEL_NAME="${TUNNEL_NAME}" \
  -e PEER_PORT="${PEER_PORT}" \
  -e PEER_IP="${PEER_IP}" \
  -e PEER_ROUTES="${PEER_ROUTES}" \
  "${ENV_ARGS[@]}" \
  -p "${PEER_PORT}:${PEER_PORT}/udp" \
  -it lane-cove-tunnel-peer:latest
