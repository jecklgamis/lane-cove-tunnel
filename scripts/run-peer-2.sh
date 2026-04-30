#!/usr/bin/env bash
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
sudo "${SCRIPT_DIR}/create-peer-tunnel.sh" lanecove0 10.9.0.3/24
"${SCRIPT_DIR}/../peer" -c "${SCRIPT_DIR}/../config/peer-2.yaml"
