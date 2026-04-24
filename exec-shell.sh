#!/usr/bin/env bash
set -e
# Usage: ./exec-shell.sh <container_name>
# Example: ./exec-shell.sh lane-cove-tunnel-peer-1

usage() {
    echo "Usage: $0 <container_name>"
    echo "Example: $0 lane-cove-tunnel-peer-1"
    exit 1
}

[[ $# -lt 1 ]] && usage

CONTAINER_NAME="$1"

CONTAINER_ID=$(docker ps | grep "$CONTAINER_NAME" | awk '{print $1}')
if [[ -z "$CONTAINER_ID" ]]; then
    echo "No running container matching: $CONTAINER_NAME"
    exit 1
fi

echo "Opening shell in container: $CONTAINER_ID ($CONTAINER_NAME)"
docker exec -it "$CONTAINER_ID" /bin/bash
