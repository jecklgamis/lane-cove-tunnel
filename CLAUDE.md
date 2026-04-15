# lane-cove-tunnel

## Project Overview
A simple Linux TUN/TAP-based IP tunnel over TCP. Implements a basic (insecure) VPN for learning purposes. Not for production use.

## Architecture
- `tcp_server.c` — listens for a TCP client, forwards packets between the TUN interface and the TCP socket. Accepts multiple sequential clients via a loop.
- `tcp_client.c` — connects to the server, forwards packets between the TUN interface and the TCP socket. Reconnects on disconnect.
- `tcp_common.c/h` — shared event loop (poll-based), TUN interface allocation, and logging macros.

## Logging
Custom `fprintf`-based logging defined in `tcp_common.h`. Global `log_level` variable (0=INFO, 1=DEBUG). Pass `-v` flag to enable debug output.

## Build
```
make all          # compile tcp_server and tcp_client
make server-image # build Docker server image (lane-cove-tunnel-server:latest)
make client-image # build Docker client image (lane-cove-tunnel-client:latest)
make clean        # remove binaries
```

## Docker
- `Dockerfile.server` — server image with nginx, iproute2, net-tools, ping, traceroute, htop, kmod
- `Dockerfile.client` — client image (same tools, no nginx)
- `docker-entrypoint-server.sh` — creates tunnel, starts nginx, starts tcp_server
- `docker-entrypoint-client.sh` — creates tunnel, starts tcp_client
- `run-tcp-server-in-docker.sh` — runs server container with `--cap-add=NET_ADMIN --device=/dev/net/tun`
- `run-tcp-client-in-docker.sh` — runs client container, auto-detects host IP from en0/en1

## Tunnel Interface
- Interface name: `lanecove`
- Server overlay network: `10.10.0.0/24` (IP derived from last octet of host eth0)
- Client overlay network: `10.9.0.0/24` (fixed at `10.9.0.1/24`)

## Key Environment Variables
| Variable      | Default    | Description                  |
|---------------|------------|------------------------------|
| `TUNNEL_NAME` | `lanecove` | TUN interface name           |
| `SERVER_PORT` | `5050`     | TCP tunnel port              |
| `SERVER_IP`   | (auto)     | Server IP (client only)      |

## Platform Notes
- Requires Linux kernel (uses `linux/if_tun.h` and `/dev/net/tun`)
- Does not compile on macOS — use Docker for building and running
- Docker containers require `--cap-add=NET_ADMIN` and `--device=/dev/net/tun`
