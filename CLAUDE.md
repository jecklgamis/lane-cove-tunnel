# lane-cove-tunnel

## Project Overview
A simple Linux TUN/TAP-based IP tunnel over TCP and UDP. Implements a basic (insecure) VPN for learning purposes. Not for production use.

## Architecture

### TCP (`tcp/`)
- `tcp_server.c` — listens for a TCP client, forwards packets between the TUN interface and the TCP socket. Accepts multiple sequential clients via a loop.
- `tcp_client.c` — connects to the server, forwards packets between the TUN interface and the TCP socket. Reconnects on disconnect.
- `tcp_common.c/h` — TUN interface allocation, poll-based event loop (with 2-byte length-prefixed framing), and logging macros.

### UDP (`udp/`)
- `udp_server.c` — binds a UDP socket, learns the client address from the first datagram, forwards packets between the TUN interface and the UDP socket. Switches peer on new source address.
- `udp_client.c` — sends a probe datagram to register with the server, then forwards packets between the TUN interface and the UDP socket. Reconnects on error.
- `udp_common.c/h` — TUN interface allocation and logging macros (no framing — UDP datagrams preserve boundaries).

## Logging
Custom `fprintf`-based logging defined in `tcp_common.h` and `udp_common.h`. Global `log_level` variable (0=INFO, 1=DEBUG). Pass `-v` flag to enable debug output.

## Build
```
make all          # compile all four binaries (delegates to tcp/ and udp/)
make clean        # remove all binaries
make -C tcp all   # compile tcp_server and tcp_client only
make -C udp all   # compile udp_server and udp_client only
```

## Running With Docker
```
# TCP
cd tcp
./run-tcp-server-in-docker.sh
SERVER_IP=<ip> ./run-tcp-client-in-docker.sh
docker run --privileged -it lane-cove-tunnel-server:latest /bin/bash
docker run --privileged -it lane-cove-tunnel-client:latest /bin/bash

# UDP
cd udp
./run-udp-server-in-docker.sh
SERVER_IP=<ip> ./run-udp-client-in-docker.sh
docker run --privileged -it lane-cove-tunnel-udp-server:latest /bin/bash
docker run --privileged -it lane-cove-tunnel-udp-client:latest /bin/bash
```

## Docker

### TCP (`tcp/`)
- `Dockerfile.server` — server image with nginx, iproute2, net-tools, ping, traceroute, htop, kmod
- `Dockerfile.client` — client image (same tools, no nginx)
- `docker-entrypoint-server.sh` — creates tunnel, starts nginx, starts tcp_server
- `docker-entrypoint-client.sh` — creates tunnel, starts tcp_client
- `run-tcp-server-in-docker.sh` — builds server image and runs container with `--cap-add=NET_ADMIN --device=/dev/net/tun`
- `run-tcp-client-in-docker.sh` — builds client image and runs container, auto-detects host IP from en0/en1

### UDP (`udp/`)
- `Dockerfile.server` — server image with nginx, iproute2, net-tools, ping, traceroute, htop, kmod
- `Dockerfile.client` — client image (same tools, no nginx)
- `docker-entrypoint-server.sh` — creates tunnel, starts nginx, starts udp_server
- `docker-entrypoint-client.sh` — creates tunnel, starts udp_client
- `run-udp-server-in-docker.sh` — builds server image and runs container, exposes UDP port
- `run-udp-client-in-docker.sh` — builds client image and runs container, auto-detects host IP from en0/en1

## Tunnel Interface
- Interface name: `lanecove`
- Server overlay network: `10.10.0.0/24` (IP derived from last octet of host eth0)
- Client overlay network: `10.9.0.0/24` (fixed at `10.9.0.1/24`)

## Key Environment Variables
| Variable      | TCP default  | UDP default    | Description                  |
|---------------|--------------|----------------|------------------------------|
| `TUNNEL_NAME` | `lanecove`   | `lanecove-udp` | TUN interface name           |
| `SERVER_PORT` | `5050`       | `5040`         | Tunnel port                  |
| `SERVER_IP`   | (auto)       | (auto)         | Server IP (client only)      |

## Platform Notes
- Requires Linux kernel (uses `linux/if_tun.h` and `/dev/net/tun`)
- Does not compile on macOS — use Docker for building and running
- Docker containers require `--cap-add=NET_ADMIN` and `--device=/dev/net/tun`
