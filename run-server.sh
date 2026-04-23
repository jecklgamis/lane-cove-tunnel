#!/usr/bin/env bash
TUNNEL_NAME=${TUNNEL_NAME:-lanecove.0}
SERVER_PORT=${SERVER_PORT:-5040}
./server -i "${TUNNEL_NAME}" -p "${SERVER_PORT}" -v
