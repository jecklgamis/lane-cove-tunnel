# lane-cove-tunnel

## Project Overview
A simple Linux TUN/TAP-based IP tunnel over UDP. Implements a basic VPN for learning purposes. Not for production use.

## Architecture

### UDP (`udp/`)
- `udp_server.c` — binds a UDP socket, performs DH handshake with client, forwards packets between the TUN interface and the UDP socket. Detects re-handshake packets from reconnecting clients and re-keys automatically.
- `udp_client.c` — performs DH handshake with the server, forwards packets between the TUN interface and the UDP socket. Reconnects and re-handshakes on error.
- `udp_common.c/h` — TUN interface allocation, logging macros, AES-256-GCM encrypt/decrypt helpers, and X25519 DH handshake functions.

#### Handshake
On connect (and reconnect), client and server perform an ephemeral X25519 DH exchange:
1. Client sends `[8-byte magic][32-byte X25519 pubkey][32-byte HMAC-SHA256(psk_key, pubkey)]`
2. Server verifies HMAC, generates its own key pair, responds in the same format
3. Both derive session key: `SHA-256(shared_secret || client_pub || server_pub)`

The PSK authenticates the handshake (prevents MITM). Without PSK the exchange still encrypts but is unauthenticated.

#### Data Packets
Each datagram on the wire is `[12-byte IV][ciphertext of (8-byte magic + payload)][16-byte GCM tag]`. Without encryption: `[8-byte magic][plaintext]`. Magic is `0xdeadbeefcafebabe`. Packets with a bad header or invalid GCM tag are silently dropped.

## Logging
Custom `fprintf`-based logging defined in `udp_common.h`. Global `log_level` variable (0=INFO, 1=DEBUG). Pass `-v` flag to enable debug output.

## Build
```
make all          # compile udp_server and udp_client
make clean        # remove binaries
make -C udp all   # same, explicit
```

## Running With Docker
```
cd udp
./run-udp-server-in-docker.sh
SERVER_IP=<ip> ./run-udp-client-in-docker.sh
docker run --privileged -it lane-cove-tunnel-udp-server:latest /bin/bash
docker run --privileged -it lane-cove-tunnel-udp-client:latest /bin/bash
```

## Docker (`udp/`)
- `Dockerfile.server` — server image with nginx, iproute2, net-tools, ping, traceroute, htop, kmod
- `Dockerfile.client` — client image (same tools, no nginx)
- `docker-entrypoint-server.sh` — creates tunnel, starts nginx, starts udp_server
- `docker-entrypoint-client.sh` — creates tunnel, starts udp_client
- `run-udp-server-in-docker.sh` — builds server image and runs container, exposes UDP port
- `run-udp-client-in-docker.sh` — builds client image and runs container, auto-detects host IP from en0/en1

## Tunnel Interface
- Interface name: `lanecove-udp`
- Server overlay network: `10.10.0.0/24` (IP derived from last octet of host eth0)
- Client overlay network: `10.9.0.0/24` (fixed at `10.9.0.1/24`)

## Key Environment Variables
| Variable      | Default        | Description                  |
|---------------|----------------|------------------------------|
| `TUNNEL_NAME` | `lanecove-udp` | TUN interface name           |
| `SERVER_PORT` | `5040`         | Tunnel port                  |
| `SERVER_IP`   | (auto)         | Server IP (client only)      |

## Platform Notes
- Requires Linux kernel (uses `linux/if_tun.h` and `/dev/net/tun`)
- Does not compile on macOS — use Docker for building and running
- Docker containers require `--cap-add=NET_ADMIN` and `--device=/dev/net/tun`
- Requires libssl-dev (OpenSSL) for AES-256-GCM and X25519
