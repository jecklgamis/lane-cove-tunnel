#!/usr/bin/env bash
TUNNEL_NAME=${TUNNEL_NAME:-lanecove.0}
SERVER_IP=${SERVER_IP:-some-remote-ip}
SERVER_PORT=${SERVER_PORT:-5040}
./create-client-tunnel.sh
envoy -c /etc/envoy/envoy.yaml -l error  2>&1 &
ENVOY_PID=$!
sleep 1
if ! kill -0 $ENVOY_PID 2>/dev/null; then
    echo "[ERROR] Envoy failed to start"
fi
./client -i "${TUNNEL_NAME}" -s "${SERVER_IP}" -p "${SERVER_PORT}" -K client.key -C server.crt -v
echo "Tunnel is connected. Try curl http://$SERVER_IP:$SERVER_PORT to reach out to the remote web server"
