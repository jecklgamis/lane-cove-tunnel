#!/usr/bin/env bash
CONTAINER_ID=$(docker ps | grep lane-cove-tunnel-peer-a | awk '{print $1}')
if [[ -z "$CONTAINER_ID" ]]; then
    echo "No running lane-cove-tunnel-peer-a container found"
    exit 1
fi
docker exec -it "$CONTAINER_ID" /bin/bash
