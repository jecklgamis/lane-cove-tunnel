#!/usr/bin/env bash
set -ex
TUNNEL_NAME=${TUNNEL_NAME:-tun2}
SERVER_PORT=${SERVER_PORT:-5050}
docker run \
  --rm \
  --cap-add=NET_ADMIN \
  --device=/dev/net/tun \
  -e TUNNEL_NAME="${TUNNEL_NAME}" \
  -e SERVER_PORT="${SERVER_PORT}" \
  -p "${SERVER_PORT}:${SERVER_PORT}" \
  -p 80:80 \
  -it lane-cove-tunnel-server:latest



