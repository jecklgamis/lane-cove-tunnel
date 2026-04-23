#!/usr/bin/env bash
set -ex
TUNNEL_NAME=${TUNNEL_NAME:-lanecove.0}
SERVER_PORT=${SERVER_PORT:-5040}
docker build -f Dockerfile.server -t lane-cove-tunnel-udp-server:latest .
docker run \
  --cap-add=NET_ADMIN \
  --device=/dev/net/tun \
  -e TUNNEL_NAME="${TUNNEL_NAME}" \
  -e SERVER_PORT="${SERVER_PORT}" \
  -p "${SERVER_PORT}:${SERVER_PORT}/udp" \
  -p 80:80 \
  -it lane-cove-tunnel-udp-server:latest
