#!/usr/bin/env bash
set -e
# Usage: ./run-peer-in-docker.sh -i <tunnel> -k <keyfile> -c <crtfile> --peer-ip <ip/cidr>
#          --host-port <udp_port> -p <relay_crt:host:port:cidr>
#          [--envoy-upstream <host>] [--envoy-port <port>]
#          [--tcp-proxy-port <host_port>] [--http-proxy-port <host_port>] [--admin-port <host_port>]
#          [-port <peer_port>] [-v]
# Example:
#   ./run-peer-in-docker.sh -i lanecove0 -k peer-1.key -c peer-1.crt \
#     --peer-ip 10.9.0.2/24 --host-port 5042 \
#     -p relay.crt:1.2.3.4:5040:10.9.0.0/24 \
#     --envoy-upstream 10.9.0.3 \
#     --tcp-proxy-port 15042 --http-proxy-port 15052 --admin-port 9901

usage() {
    echo "Usage: $0 -i <tunnel> -k <keyfile> -c <crtfile> --peer-ip <ip/cidr> --host-port <port>"
    echo "          -p <relay_crt:host:port:cidr> [-p ...] [options]"
    echo ""
    echo "Required:"
    echo "  -i                TUN interface name        e.g. lanecove0"
    echo "  -k                Private key file          e.g. peer-1.key"
    echo "  -c                Certificate file          e.g. peer-1.crt"
    echo "  --peer-ip         Overlay IP/CIDR           e.g. 10.9.0.2/24"
    echo "  --host-port       Host UDP port             e.g. 5042"
    echo "  -p                Peer: <crt:host:port:cidr> e.g. relay.crt:1.2.3.4:5040:10.9.0.0/24 (repeatable)"
    echo ""
    echo "Optional:"
    echo "  -port             Container listen port     (default: 5040)"
    echo "  --envoy-upstream  Envoy upstream host       e.g. 10.9.0.3 (if unset, Envoy not started)"
    echo "  --envoy-port      Envoy upstream port       (default: 80)"
    echo "  --tcp-proxy-port  Host port for TCP proxy   e.g. 15042"
    echo "  --http-proxy-port Host port for HTTP proxy  e.g. 15052"
    echo "  --admin-port      Host port for Envoy admin e.g. 9901"
    echo "  -v                Verbose logging"
    exit 1
}

source "$(dirname "$0")/common.sh"

TUNNEL_NAME=""
PEER_KEY=""
PEER_CRT=""
PEER_IP=""
HOST_PORT=""
PEER_PORT=5040
CONTAINER_NAME=""
ENVOY_UPSTREAM_HOST=""
ENVOY_UPSTREAM_PORT=80
TCP_PROXY_PORT=""
HTTP_PROXY_PORT=""
ADMIN_PORT=""
PEER_ARGS=()
peer_idx=1

while [[ $# -gt 0 ]]; do
    case "$1" in
        -i)                 TUNNEL_NAME="$2"; shift 2 ;;
        -k)                 PEER_KEY="$2"; shift 2 ;;
        -c)                 PEER_CRT="$2"; shift 2 ;;
        --peer-ip)          PEER_IP="$2"; shift 2 ;;
        --host-port)        HOST_PORT="$2"; shift 2 ;;
        -port)              PEER_PORT="$2"; shift 2 ;;
        --envoy-upstream)   ENVOY_UPSTREAM_HOST="$2"; shift 2 ;;
        --envoy-port)       ENVOY_UPSTREAM_PORT="$2"; shift 2 ;;
        --tcp-proxy-port)   TCP_PROXY_PORT="$2"; shift 2 ;;
        --http-proxy-port)  HTTP_PROXY_PORT="$2"; shift 2 ;;
        --admin-port)       ADMIN_PORT="$2"; shift 2 ;;
        --name)             CONTAINER_NAME="$2"; shift 2 ;;
        -v)                 VERBOSE="-v"; shift ;;
        -p)
            IFS=':' read -r crt_file endpoint_host endpoint_port allowed_ips <<< "$2"
            [[ -z "$crt_file" || -z "$endpoint_host" || -z "$endpoint_port" || -z "$allowed_ips" ]] && {
                echo "Error: -p must be <crt_file>:<host>:<port>:<cidr>"
                exit 1
            }
            [[ ! -f "$crt_file" ]] && { echo "Error: cert file not found: $crt_file"; exit 1; }
            pub=$(extract_pub "$crt_file")
            PEER_ARGS+=(
                -e "PEER_PUB_${peer_idx}=${pub}"
                -e "PEER_ENDPOINT_${peer_idx}=${endpoint_host}:${endpoint_port}"
                -e "PEER_ALLOWED_IPS_${peer_idx}=${allowed_ips}"
            )
            peer_idx=$((peer_idx + 1))
            shift 2
            ;;
        *) usage ;;
    esac
done

[[ -z "$TUNNEL_NAME" ]] && { echo "Error: -i is required"; usage; }
[[ -z "$PEER_KEY" ]]    && { echo "Error: -k is required"; usage; }
[[ -z "$PEER_CRT" ]]    && { echo "Error: -c is required"; usage; }
[[ -z "$PEER_IP" ]]     && { echo "Error: --peer-ip is required"; usage; }
[[ -z "$HOST_PORT" ]]   && { echo "Error: --host-port is required"; usage; }
[[ ${#PEER_ARGS[@]} -eq 0 ]] && { echo "Error: at least one -p is required"; usage; }
[[ ! -f "$PEER_KEY" ]]  && { echo "Error: key file not found: $PEER_KEY"; exit 1; }
[[ ! -f "$PEER_CRT" ]]  && { echo "Error: cert file not found: $PEER_CRT"; exit 1; }

PORT_ARGS=(-p "${HOST_PORT}:${PEER_PORT}/udp")
[[ -n "$TCP_PROXY_PORT" ]]  && PORT_ARGS+=(-p "${TCP_PROXY_PORT}:15040")
[[ -n "$HTTP_PROXY_PORT" ]] && PORT_ARGS+=(-p "${HTTP_PROXY_PORT}:15050")
[[ -n "$ADMIN_PORT" ]]      && PORT_ARGS+=(-p "${ADMIN_PORT}:9901")

ENVOY_ARGS=()
[[ -n "$ENVOY_UPSTREAM_HOST" ]] && ENVOY_ARGS+=(
    -e "ENVOY_UPSTREAM_HOST=${ENVOY_UPSTREAM_HOST}"
    -e "ENVOY_UPSTREAM_PORT=${ENVOY_UPSTREAM_PORT}"
)

NAME_ARG=()
if [[ -n "$CONTAINER_NAME" ]]; then
    NAME_ARG=(--name "$CONTAINER_NAME")
    docker rm -f "$CONTAINER_NAME" 2>/dev/null || true
fi

docker run \
  "${NAME_ARG[@]}" \
  --cap-add=NET_ADMIN \
  --device=/dev/net/tun \
  -v "$(pwd)/${PEER_KEY}:/app/peer.key:ro" \
  -v "$(pwd)/${PEER_CRT}:/app/peer.crt:ro" \
  "${PORT_ARGS[@]}" \
  -e TUNNEL_NAME="${TUNNEL_NAME}" \
  -e PEER_PORT="${PEER_PORT}" \
  -e PEER_IP="${PEER_IP}" \
  "${PEER_ARGS[@]}" \
  "${ENVOY_ARGS[@]}" \
  -it lane-cove-tunnel-peer:latest
