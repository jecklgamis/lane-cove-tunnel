#!/usr/bin/env bash
set -e
# Usage: ./run-as-relay.sh -i <tunnel> -k <keyfile> -p <crt_file:allowed_ips> [-p ...] [-port <port>] [-v]
# Example:
#   ./run-as-relay.sh -i lanecove0 -k relay.key \
#     -p peer-1.crt:10.9.0.2/32 \
#     -p peer-2.crt:10.9.0.3/32

usage() {
    echo "Usage: $0 -i <tunnel_name> -k <keyfile> -p <crt_file:cidr> [-p ...] [-port <port>] [-v]"
    echo "  -i      TUN interface name       e.g. lanecove0"
    echo "  -k      Private key file         e.g. relay.key"
    echo "  -p      Peer: <crt_file>:<cidr>  e.g. peer-1.crt:10.9.0.2/32 (repeatable)"
    echo "  -port   Listen port              (default: 5040)"
    echo "  -v      Verbose logging"
    echo ""
    echo "Example:"
    echo "  $0 -i lanecove0 -k relay.key -p peer-1.crt:10.9.0.2/32 -p peer-2.crt:10.9.0.3/32"
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
            IFS=':' read -r crt_file allowed_ips <<< "$2"
            [[ -z "$crt_file" || -z "$allowed_ips" ]] && {
                echo "Error: -p must be <crt_file>:<cidr>"
                exit 1
            }
            pub=$(extract_pub "$crt_file")
            PEER_ARGS+=(-P "$pub" -R "$allowed_ips")
            shift 2
            ;;
        *) usage ;;
    esac
done

[[ -z "$TUNNEL_NAME" ]] && { echo "Error: -i is required"; usage; }
[[ -z "$PEER_KEY" ]]    && { echo "Error: -k is required"; usage; }
[[ ${#PEER_ARGS[@]} -eq 0 ]] && { echo "Error: at least one -p is required"; usage; }

./peer -i "${TUNNEL_NAME}" -p "${PEER_PORT}" -K "${PEER_KEY}" "${PEER_ARGS[@]}" ${VERBOSE}
