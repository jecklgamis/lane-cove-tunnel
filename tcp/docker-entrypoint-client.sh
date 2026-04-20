#!/usr/bin/env bash
set -ex
TUNNEL_NAME=${TUNNEL_NAME:-lanecove}
SERVER_IP=${SERVER_IP:-some-remote-ip}
SERVER_PORT=${SERVER_PORT:-5050}
./create-client-tunnel.sh
./tcp_client -i "${TUNNEL_NAME}" -s "${SERVER_IP}" -p "${SERVER_PORT}" -v
echo "Tunnel is connected. Try curl http://$SERVER_IP:$SERVER_PORT to reach out to the remote web server"