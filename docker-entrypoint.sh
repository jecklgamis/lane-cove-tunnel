#!/usr/bin/env bash
TUNNEL_NAME=${TUNNEL_NAME:-lanecove0}
PEER_IP=${PEER_IP:-10.9.0.1/24}
PEER_ROUTES=${PEER_ROUTES:-}
PEER_CONFIG=${PEER_CONFIG:-peer.yaml}

if [[ ! -f "$PEER_CONFIG" ]]; then
    echo "Config file not found: $PEER_CONFIG"
    exit 1
fi

./create-peer-tunnel.sh "${TUNNEL_NAME}" "${PEER_IP}" ${PEER_ROUTES}
HOST_IP=${PEER_IP%%/*}
export HOST_IP
envsubst < index.html.tmpl > /var/www/html/index.html
nginx
if [[ -n "${ENVOY_UPSTREAM_HOST:-}" ]]; then
    ENVOY_UPSTREAM_PORT=${ENVOY_UPSTREAM_PORT:-80}
    export ENVOY_UPSTREAM_HOST ENVOY_UPSTREAM_PORT
    envsubst < envoy.yaml.tmpl > envoy.yaml
    echo "$(date '+%Y-%m-%d %H:%M:%S') [INFO]  Starting Envoy: TCP listener 0.0.0.0:15040 -> ${ENVOY_UPSTREAM_HOST}:${ENVOY_UPSTREAM_PORT}"
    envoy -c envoy.yaml --log-level error --log-path envoy.log &
fi
exec ./peer -c "${PEER_CONFIG}"
