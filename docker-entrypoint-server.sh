#!/usr/bin/env bash
TUNNEL_NAME=${TUNNEL_NAME:-lanecove.0}
SERVER_PORT=${SERVER_PORT:-5040}
ALLOWED_IPS=${ALLOWED_IPS:-10.9.0.0/24}

CLIENT_PUB=$(openssl pkey -in client.crt -pubin -outform DER | tail -c 32 | od -An -tx1 | tr -d ' \n')
if [[ -z "${CLIENT_PUB}" ]]; then
    echo "Failed to extract client public key from client.crt"
    exit 1
fi
echo "Client public key: ${CLIENT_PUB}"

./create-server-tunnel.sh
nginx
./server -i "${TUNNEL_NAME}" -p "${SERVER_PORT}" -K server.key -A "${CLIENT_PUB}" -R "${ALLOWED_IPS}" -v
