#!/usr/bin/env bash
set -ex
TUNNEL_NAME=${TUNNEL_NAME:-tun2}
SERVER_IP=${SERVER_IP:-some-remote-ip}
SERVER_PORT=${SERVER_PORT:-5050}
./tcp_client -i "${TUNNEL_NAME}" -s "${SERVER_IP}" -p "${SERVER_PORT}" -v
