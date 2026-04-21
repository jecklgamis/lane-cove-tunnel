#!/usr/bin/env bash
CONTAINER_ID=$(docker ps | grep lane-cove-tunnel-udp-client | awk '{print $1 }')
docker exec -it $CONTAINER_ID /bin/bash
