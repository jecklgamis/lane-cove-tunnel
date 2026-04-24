#!/usr/bin/env bash
set -e
source "$(dirname "$0")/common.sh"
RELAY_IP=${RELAY_IP:-$(detect_local_ip)}
RELAY_IP=${RELAY_IP:?RELAY_IP could not be determined (set it explicitly)}

exec ./run-peer-in-docker.sh \
  -i lanecove0 \
  -k peer-2.key \
  -c peer-2.crt \
  --peer-ip 10.9.0.3/24 \
  --host-port 5043 \
  --tcp-proxy-port 15043 \
  --http-proxy-port 15053 \
  --admin-port 9902 \
  --envoy-upstream 10.9.0.2 \
  -p relay.crt:"${RELAY_IP}":5040:10.9.0.0/24 \
  "$@"
