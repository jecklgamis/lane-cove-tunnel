#!/usr/bin/env bash
CONTAINER_ID=$(docker ps | grep lane-cove-tunnel-peer-2 | awk '{print $1}')
if [[ -z "$CONTAINER_ID" ]]; then
    echo "No running lane-cove-tunnel-peer-2 container found"
    exit 1
fi
TARGET_IP=${TARGET_IP:-10.9.0.2}
echo "Using container: $CONTAINER_ID"
echo "Pinging $TARGET_IP via tunnel..."
docker exec -it "$CONTAINER_ID" ping -c 2 "$TARGET_IP"
echo "Curling http://$TARGET_IP via tunnel..."
docker exec -it "$CONTAINER_ID" curl -s --max-time 5 "http://$TARGET_IP"
