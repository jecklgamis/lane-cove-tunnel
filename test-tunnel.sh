#!/usr/bin/env bash
set -e
# Usage: ./test-tunnel.sh <container_name> <target_ip>
# Example: ./test-tunnel.sh lane-cove-tunnel-peer-1 10.9.0.3

usage() {
    echo "Usage: $0 <container_name> <target_ip>"
    echo "Example: $0 lane-cove-tunnel-peer-1 10.9.0.3"
    exit 1
}

[[ $# -lt 2 ]] && usage

CONTAINER_NAME="$1"
TARGET_IP="$2"

CONTAINER_ID=$(docker ps | grep "$CONTAINER_NAME" | awk '{print $1}')
if [[ -z "$CONTAINER_ID" ]]; then
    echo "No running container matching: $CONTAINER_NAME"
    exit 1
fi

echo "Using container: $CONTAINER_ID ($CONTAINER_NAME)"
echo "Pinging $TARGET_IP via tunnel..."
docker exec -it "$CONTAINER_ID" ping -c 2 "$TARGET_IP"
echo "Curling http://$TARGET_IP via tunnel..."
docker exec -it "$CONTAINER_ID" curl -s --max-time 5 "http://$TARGET_IP"
