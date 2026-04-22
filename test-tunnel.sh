#!/usr/bin/env bash
CONTAINER_ID=$(docker ps | grep lane-cove-tunnel-udp-client | awk '{print $1 }')
OVERLAY_NETWORK_IP=10.10.0.1
echo "Using OVERLAY_NETWORK_IP = $OVERLAY_NETWORK_IP"
docker exec -it $CONTAINER_ID curl http://$OVERLAY_NETWORK_IP
