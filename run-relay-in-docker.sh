#!/usr/bin/env bash
set -e
# Usage: ./run-relay-in-docker.sh -i <tunnel> -k <keyfile> -c <crtfile> --peer-ip <ip/cidr>
#          -p <peer_crt:cidr> [-p ...] [-port <port>] [-v]
# Example:
#   ./run-relay-in-docker.sh -i lanecove0 -k relay.key -c relay.crt \
#     --peer-ip 10.9.0.1/24 \
#     -p peer-1.crt:10.9.0.2/32 \
#     -p peer-2.crt:10.9.0.3/32

usage() {
    echo "Usage: $0 -i <tunnel> -k <keyfile> -c <crtfile> --peer-ip <ip/cidr> -p <crt:cidr> [-p ...] [-port <port>] [-v]"
    echo ""
    echo "Required:"
    echo "  -i          TUN interface name   e.g. lanecove0"
    echo "  -k          Private key file     e.g. relay.key"
    echo "  -c          Certificate file     e.g. relay.crt"
    echo "  --peer-ip   Overlay IP/CIDR      e.g. 10.9.0.1/24"
    echo "  -p          Peer: <crt>:<cidr>   e.g. peer-1.crt:10.9.0.2/32 (repeatable)"
    echo ""
    echo "Optional:"
    echo "  -port       Listen port          (default: 5040)"
    echo "  -v          Verbose logging"
    exit 1
}

source "$(dirname "$0")/common.sh"

TUNNEL_NAME=""
PEER_KEY=""
PEER_CRT=""
PEER_IP=""
PEER_PORT=5040
CONTAINER_NAME=""
PEER_ARGS=()
peer_idx=1

while [[ $# -gt 0 ]]; do
    case "$1" in
        -i)         TUNNEL_NAME="$2"; shift 2 ;;
        -k)         PEER_KEY="$2"; shift 2 ;;
        -c)         PEER_CRT="$2"; shift 2 ;;
        --peer-ip)  PEER_IP="$2"; shift 2 ;;
        -port)      PEER_PORT="$2"; shift 2 ;;
        -v)         VERBOSE="-v"; shift ;;
        --name)     CONTAINER_NAME="$2"; shift 2 ;;
        -p)
            IFS=':' read -r crt_file allowed_ips <<< "$2"
            [[ -z "$crt_file" || -z "$allowed_ips" ]] && {
                echo "Error: -p must be <crt_file>:<cidr>"
                exit 1
            }
            [[ ! -f "$crt_file" ]] && { echo "Error: cert file not found: $crt_file"; exit 1; }
            pub=$(extract_pub "$crt_file")
            PEER_ARGS+=(
                -e "PEER_PUB_${peer_idx}=${pub}"
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
[[ ${#PEER_ARGS[@]} -eq 0 ]] && { echo "Error: at least one -p is required"; usage; }
[[ ! -f "$PEER_KEY" ]]  && { echo "Error: key file not found: $PEER_KEY"; exit 1; }
[[ ! -f "$PEER_CRT" ]]  && { echo "Error: cert file not found: $PEER_CRT"; exit 1; }

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
  -p "${PEER_PORT}:${PEER_PORT}/udp" \
  -e TUNNEL_NAME="${TUNNEL_NAME}" \
  -e PEER_PORT="${PEER_PORT}" \
  -e PEER_IP="${PEER_IP}" \
  "${PEER_ARGS[@]}" \
  -it lane-cove-tunnel-peer:latest
