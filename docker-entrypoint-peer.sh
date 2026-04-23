#!/usr/bin/env bash
TUNNEL_NAME=${TUNNEL_NAME:-lanecove.0}
PEER_PORT=${PEER_PORT:-5040}
PEER_IP=${PEER_IP:-10.9.0.1/24}
PEER_ROUTES=${PEER_ROUTES:-}

PEER_ARGS=()
i=1
while true; do
    pub_var="PEER_PUB_${i}"
    ep_var="PEER_ENDPOINT_${i}"
    routes_var="PEER_ALLOWED_IPS_${i}"
    pub="${!pub_var}"
    [[ -z "$pub" ]] && break
    ep="${!ep_var}"
    routes="${!routes_var}"
    PEER_ARGS+=(-P "$pub")
    [[ -n "$ep" ]] && PEER_ARGS+=(-E "$ep")
    for cidr in $routes; do
        PEER_ARGS+=(-R "$cidr")
    done
    i=$((i + 1))
done

if [[ ${#PEER_ARGS[@]} -eq 0 ]]; then
    echo "No peers configured. Set PEER_PUB_1, PEER_ENDPOINT_1, PEER_ALLOWED_IPS_1, etc."
    exit 1
fi

export PEER_IP PEER_ROUTES
./create-peer-tunnel.sh
nginx
./peer -i "${TUNNEL_NAME}" -p "${PEER_PORT}" -K peer.key "${PEER_ARGS[@]}" -v
