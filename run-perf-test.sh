#!/usr/bin/env bash
set -e

# Usage: ./run-perf-test.sh [base_url]
# Example: ./run-perf-test.sh http://192.168.1.10:15052
# Defaults to peer-1 Envoy HTTP proxy port on the detected host IP

source "$(dirname "$0")/common.sh"
HOST_IP=$(detect_local_ip)
HOST_IP=${HOST_IP:?Host IP could not be determined (set BASE_URL explicitly)}
BASE_URL="${1:-http://${HOST_IP}:15052}"

docker run --rm \
  -e SIMULATION_NAME=gatling.test.example.simulation.ExampleGetSimulation \
  -e "JAVA_OPTS=-DbaseUrl=${BASE_URL} -DrequestPerSecond=10 -DdurationMin=4" \
  jecklgamis/gatling-scala-example:main
