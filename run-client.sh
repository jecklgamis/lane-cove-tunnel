#!/usr/bin/env bash
TUNNEL_NAME=${TUNNEL_NAME:-lanecove.0}
SERVER_HOST=${SERVER_HOST:-some-remote-ip:5040}
SERVER_PORT=${SERVER_PORT:-5040}
./client -i "${TUNNEL_NAME}" -s "${SERVER_HOST}" -p "${SERVER_PORT}" -v
echo "Tunnel is connected. Try curl http://$SERVER_HOST:$SERVER_PORT to reach out to the remote web server"
