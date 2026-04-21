#!/usr/bin/env bash
TUNNEL_NAME=${TUNNEL_NAME:-lanecove-udp}
SERVER_PORT=${SERVER_PORT:-5040}
./create-server-tunnel.sh
nginx
./server -i "${TUNNEL_NAME}" -p "${SERVER_PORT}" -v
