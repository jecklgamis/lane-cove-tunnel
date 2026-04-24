#!/usr/bin/env bash
TUNNEL_NAME=${TUNNEL_NAME:-lanecove0}
PEER_PORT=${PEER_PORT:-5040}
PEER_IP=${PEER_IP:-10.9.0.1/24}
PEER_ROUTES=${PEER_ROUTES:-}

PEER_1RGS=()
i=1
while true; do
    pub_var="PEER_PUB_${i}"
    ep_var="PEER_ENDPOINT_${i}"
    routes_var="PEER_ALLOWED_IPS_${i}"
    pub="${!pub_var}"
    [[ -z "$pub" ]] && break
    ep="${!ep_var}"
    routes="${!routes_var}"
    PEER_1RGS+=(-P "$pub")
    [[ -n "$ep" ]] && PEER_1RGS+=(-E "$ep")
    for cidr in $routes; do
        PEER_1RGS+=(-R "$cidr")
    done
    i=$((i + 1))
done

if [[ ${#PEER_1RGS[@]} -eq 0 ]]; then
    echo "No peers configured. Set PEER_PUB_1, PEER_ENDPOINT_1, PEER_ALLOWED_IPS_1, etc."
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
./peer -i "${TUNNEL_NAME}" -p "${PEER_PORT}" -K peer.key "${PEER_1RGS[@]}" -v
