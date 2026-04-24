#!/usr/bin/env bash
set -e
source "$(dirname "$0")/common.sh"
RELAY_IP=${RELAY_IP:-$(detect_local_ip)}
RELAY_IP=${RELAY_IP:?RELAY_IP could not be determined (set it explicitly)}

exec ./run-peer-in-docker.sh \
  -i lanecove0 \
  -k peer-1.key \
  -c peer-1.crt \
  --peer-ip 10.9.0.2/24 \
  --host-port 5042 \
  --tcp-proxy-port 15042 \
  --http-proxy-port 15052 \
  --admin-port 9901 \
  --envoy-upstream 10.9.0.3 \
  -p relay.crt:"${RELAY_IP}":5040:10.9.0.0/24 \
  "$@"
