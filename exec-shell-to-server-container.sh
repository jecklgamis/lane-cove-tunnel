#!/usr/bin/env bash
CONTAINER_ID=$(docker ps | grep lane-cove-tunnel-udp-server | awk '{print $1 }')
docker exec -it $CONTAINER_ID /bin/bash
