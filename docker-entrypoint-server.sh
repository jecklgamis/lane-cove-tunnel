#!/usr/bin/env bash
TUNNEL_NAME=${TUNNEL_NAME:-lanecove.0}
SERVER_PORT=${SERVER_PORT:-5040}

SERVER_ARGS=()
CLIENT_SUBNETS=""

add_client() {
    local cert=$1 allowed_ips=$2 pub
    pub=$(openssl pkey -in "$cert" -pubin -outform DER | tail -c 32 | od -An -tx1 | tr -d ' \n')
    if [[ -z "$pub" ]]; then
        echo "Failed to extract public key from $cert"
        exit 1
    fi
    echo "Client public key ($cert): $pub"
    SERVER_ARGS+=(-A "$pub")
    for cidr in $allowed_ips; do
        SERVER_ARGS+=(-R "$cidr")
        CLIENT_SUBNETS="$CLIENT_SUBNETS $cidr"
    done
}

# Support numbered pairs: CLIENT_CERT_1/ALLOWED_IPS_1, CLIENT_CERT_2/ALLOWED_IPS_2, ...
i=1
while true; do
    cert_var="CLIENT_CERT_${i}"
    ips_var="ALLOWED_IPS_${i}"
    cert="${!cert_var}"
    ips="${!ips_var}"
    [[ -z "$cert" ]] && break
    add_client "$cert" "${ips:-10.9.${i}.0/24}"
    i=$((i + 1))
done

# Fall back to legacy single-client env vars
if [[ ${#SERVER_ARGS[@]} -eq 0 ]]; then
    add_client "${CLIENT_CERT:-client.crt}" "${ALLOWED_IPS:-10.9.0.0/24}"
fi

export CLIENT_SUBNETS
./create-server-tunnel.sh
nginx
./server -i "${TUNNEL_NAME}" -p "${SERVER_PORT}" -K server.key "${SERVER_ARGS[@]}" -v
