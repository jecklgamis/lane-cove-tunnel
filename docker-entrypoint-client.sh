#!/usr/bin/env bash
TUNNEL_NAME=${TUNNEL_NAME:-lanecove-udp}
SERVER_IP=${SERVER_IP:-some-remote-ip}
SERVER_PORT=${SERVER_PORT:-5040}
./create-client-tunnel.sh
./client -i "${TUNNEL_NAME}" -s "${SERVER_IP}" -p "${SERVER_PORT}" -K client.key -C server.crt -v
echo "Tunnel is connected. Try curl http://$SERVER_IP:$SERVER_PORT to reach out to the remote web server"
