#!/usr/bin/env bash
TUNNEL_NAME=${TUNNEL_NAME:-lanecove}
SERVER_IP=${SERVER_IP:-$(ipconfig getifaddr en0)}
SERVER_IP=${SERVER_IP:-$(ipconfig getifaddr en1)}
SERVER_PORT=${SERVER_PORT:-5050}
docker build -f Dockerfile.client -t lane-cove-tunnel-client:latest .
echo "Using remote address for the tunnel: $SERVER_IP:$SERVER_PORT"
docker run \
  --rm \
  --cap-add=NET_ADMIN \
  --device=/dev/net/tun \
  -e TUNNEL_NAME="${TUNNEL_NAME}" \
  -e SERVER_IP="${SERVER_IP}" \
  -e SERVER_PORT="${SERVER_PORT}" \
  --add-host="host.docker.internal:host-gateway" \
  -it lane-cove-tunnel-client:latest
