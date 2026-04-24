#!/usr/bin/env bash
exec "$(dirname "$0")/test-tunnel.sh" lane-cove-tunnel-peer-2 10.9.0.2 "$@"
