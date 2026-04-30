#!/usr/bin/env bash
set -e
# Usage: ./run-as-peer.sh -i <tunnel> -k <keyfile> -p <crt_file:host:port:allowed_ips> [-p ...] [-port <port>] [-v]
# Example:
#   ./run-as-peer.sh -i lanecove0 -k peer-1.key \
#     -p relay.crt:<relay-ip>:5040:10.9.0.0/24

usage() {
    echo "Usage: $0 -i <tunnel_name> -k <keyfile> -p <crt_file:host:port:cidr> [-p ...] [-port <port>] [-v]"
    echo "  -i      TUN interface name              e.g. lanecove0"
    echo "  -k      Private key file                e.g. peer-1.key"
    echo "  -p      Peer: <crt_file:host:port:cidr> e.g. relay.crt:1.2.3.4:5040:10.9.0.0/24 (repeatable)"
    echo "  -port   Listen port                     (default: 5040)"
    echo "  -v      Verbose logging"
    echo ""
    echo "Example:"
    echo "  $0 -i lanecove0 -k peer-1.key -p relay.crt:1.2.3.4:5040:10.9.0.0/24"
    exit 1
}

source "$(dirname "$0")/common.sh"

TUNNEL_NAME=""
PEER_KEY=""
PEER_PORT=5040
VERBOSE=""
PEER_ARGS=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        -i)     TUNNEL_NAME="$2"; shift 2 ;;
        -k)     PEER_KEY="$2"; shift 2 ;;
        -port)  PEER_PORT="$2"; shift 2 ;;
        -v)     VERBOSE="-v"; shift ;;
        -p)
            IFS=':' read -r crt_file endpoint_host endpoint_port allowed_ips <<< "$2"
            [[ -z "$crt_file" || -z "$endpoint_host" || -z "$endpoint_port" || -z "$allowed_ips" ]] && {
                echo "Error: -p must be <crt_file>:<host>:<port>:<cidr>"
                exit 1
            }
            pub=$(extract_pub "$crt_file")
            PEER_ARGS+=(-P "$pub" -E "${endpoint_host}:${endpoint_port}" -R "$allowed_ips")
            shift 2
            ;;
        *) usage ;;
    esac
done

[[ -z "$TUNNEL_NAME" ]] && { echo "Error: -i is required"; usage; }
[[ -z "$PEER_KEY" ]]    && { echo "Error: -k is required"; usage; }
[[ ${#PEER_ARGS[@]} -eq 0 ]] && { echo "Error: at least one -p is required"; usage; }

./peer -i "${TUNNEL_NAME}" -p "${PEER_PORT}" -K "${PEER_KEY}" "${PEER_ARGS[@]}" ${VERBOSE}
