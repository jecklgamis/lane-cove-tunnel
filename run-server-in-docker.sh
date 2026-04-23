#!/usr/bin/env bash
set -e
TUNNEL_NAME=${TUNNEL_NAME:-lanecove.0}
SERVER_PORT=${SERVER_PORT:-5040}

# Build -e flags for numbered client pairs: CLIENT_CERT_1/ALLOWED_IPS_1, ...
ENV_ARGS=()
i=1
while true; do
    cert_var="CLIENT_CERT_${i}"
    ips_var="ALLOWED_IPS_${i}"
    [[ -z "${!cert_var}" ]] && break
    ENV_ARGS+=(-e "${cert_var}=${!cert_var}")
    [[ -n "${!ips_var}" ]] && ENV_ARGS+=(-e "${ips_var}=${!ips_var}")
    i=$((i + 1))
done

# Fall back to legacy single-client vars
if [[ ${#ENV_ARGS[@]} -eq 0 ]]; then
    ENV_ARGS+=(-e "CLIENT_CERT=${CLIENT_CERT:-client.crt}")
    ENV_ARGS+=(-e "ALLOWED_IPS=${ALLOWED_IPS:-10.9.0.0/24}")
fi

docker build -f Dockerfile.server -t lane-cove-tunnel-udp-server:latest .
docker run \
  --cap-add=NET_ADMIN \
  --device=/dev/net/tun \
  -e TUNNEL_NAME="${TUNNEL_NAME}" \
  -e SERVER_PORT="${SERVER_PORT}" \
  "${ENV_ARGS[@]}" \
  -p "${SERVER_PORT}:${SERVER_PORT}/udp" \
  -p 80:80 \
  -it lane-cove-tunnel-udp-server:latest
