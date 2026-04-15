#!/usr/bin/env bash
set -ex
TUNNEL_NAME=${TUNNEL_NAME:-tun2}
SERVER_PORT=${SERVER_PORT:-5050}
./create-server-tunnel.sh
nginx
./tcp_server -i "${TUNNEL_NAME}" -p "${SERVER_PORT}" -v
