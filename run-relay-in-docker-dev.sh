#!/usr/bin/env bash
set -e
exec "$(dirname "$0")/run-relay-in-docker.sh" \
  -i lanecove0 \
  -k relay.key \
  -c relay.crt \
  --peer-ip 10.9.0.1/24 \
  -p peer-1.crt:10.9.0.2/32 \
  -p peer-2.crt:10.9.0.3/32 \
  "$@"
