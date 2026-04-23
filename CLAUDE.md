# lane-cove-tunnel

## Project Overview
A simple Linux hub-and-spoke layer 3 overlay network using a TUN virtual interface over UDP. Implements a basic VPN for learning purposes. Not for production use.

## Architecture

- `peer.c` — symmetric peer binary. Binds a UDP socket, accepts inbound handshakes, and initiates outbound connections to peers with a configured endpoint (`-E`). Maintains a session table with per-peer AllowedIPs, sequence counters, and replay windows. Periodically checks whether outbound peers need rekeying (every 5 minutes).
- `common.c/h` — TUN interface allocation, logging macros, AES-256-GCM encrypt/decrypt helpers, X25519 DH handshake functions with identity hiding, and a 2048-bit sliding-window replay-protection implementation.

### Topology
```
peer-a (10.9.0.2) ──┐
                     ├── UDP 5040 ── relay (10.9.0.1, public IP)
peer-b (10.9.0.3) ──┘
```

- **relay** — public IP, inbound-only, routes traffic between peers
- **peer-a / peer-b** — behind NAT, connect outbound to relay

### Key Provisioning
Keys are X25519 key pairs stored in PEM files. Use `generate-peer-keys.sh` to generate key pairs.

```
./generate-peer-keys.sh relay peer-a peer-b
# produces: relay.key/crt, peer-a.key/crt, peer-b.key/crt
```

| Machine | Needs |
|---------|-------|
| relay | `relay.key`, `relay.crt`, `peer-a.crt`, `peer-b.crt` |
| peer-a | `peer-a.key`, `peer-a.crt`, `relay.crt` |
| peer-b | `peer-b.key`, `peer-b.crt`, `relay.crt` |

### Session Management
- Sessions are stored in a flat array (`sessions[]`, max 64)
- Slot reuse: existing slot found by pub key first, then addr, then free slot
- Outbound peers checked every 10 seconds; reconnect attempted every 30 seconds on failure
- Handshake timeout: 5 seconds (blocking; other peers may miss packets during rekey)

### Handshake Wire Format
```
[8 magic][32 eph_pub][48 AES-256-GCM(static_pub)][32 HMAC-SHA256(psk,...)]?
```

- **`eph_pub`** — fresh X25519 ephemeral public key, generated per handshake
- **`AES-256-GCM(static_pub)`** — static public key encrypted to hide identity from passive observers:
  - Initiator encrypts its static_pub with `key = SHA-256(DH(eph_c, server_static))`
  - Responder encrypts its static_pub with `key = SHA-256(DH(eph_s, eph_c))`
  - Zero IV is safe because the encryption key is derived from a fresh ephemeral DH every time
- **`HMAC-SHA256`** — optional PSK authentication over `[magic][eph_pub][encrypted_static_pub]`; omitted if no PSK

### Session Key Derivation
```
SHA-256(
  DH(eph_c, eph_s)          ||
  DH(static_c, eph_s)       ||
  DH(eph_c, static_s)       ||
  client_eph_pub            ||
  server_eph_pub            ||
  client_static_pub         ||
  server_static_pub
)
```

### Data Packets
Each datagram on the wire is:
```
[12-byte IV][AES-256-GCM ciphertext of (8-byte magic + 8-byte seq + payload)][16-byte GCM tag]
```
Magic is `0xdeadbeefcafebabe`. Packets with a bad magic header, invalid GCM tag, or replayed/too-old sequence number are silently dropped. The receiver uses a **2048-bit sliding window** (`32 × uint64_t`) to detect replays.

### AllowedIPs Routing
On the TUN→UDP path, destination IP is looked up using longest-prefix match across all peer session route tables. On the UDP→TUN path, source IP is validated against the sending peer's AllowedIPs — packets with an unexpected source IP are dropped.

## Logging
Custom `fprintf`-based logging defined in `common.h`. Global `log_level` variable (0=INFO, 1=DEBUG). Pass `-v` flag to enable debug output. All log lines include a timestamp:
```
2026-04-23 14:05:32 [INFO]  Peer connected: 1.2.3.4:5040 (key=deadbeef...)
2026-04-23 14:05:32 [WARN]  Replay detected from 1.2.3.4:5040 (seq=42) — dropping
```

## Build
```
make all          # compile peer binary
make clean        # remove binary
```

## Running With Docker
```
./generate-peer-keys.sh relay peer-a peer-b
./run-relay-in-docker.sh
RELAY_IP=<ip> ./run-peer-a-in-docker.sh
RELAY_IP=<ip> ./run-peer-b-in-docker.sh
```

## Docker
- `Dockerfile.peer` — multi-stage build (`debian:bookworm-slim`); accepts `KEY_FILE` and `CRT_FILE` build args; includes iproute2, nginx, curl, ping, ifconfig
- `docker-entrypoint-peer.sh` — reads `PEER_PUB_n`/`PEER_ENDPOINT_n`/`PEER_ALLOWED_IPS_n` env vars, creates TUN, starts nginx, starts peer
- `create-peer-tunnel.sh` — creates TUN interface using `PEER_IP` and `PEER_ROUTES`
- `run-relay-in-docker.sh` — builds relay image (relay.key/crt), extracts peer-a/peer-b pubkeys, runs container
- `run-peer-a-in-docker.sh` — builds peer-a image, auto-detects relay IP from en0/en1, runs container
- `run-peer-b-in-docker.sh` — builds peer-b image, auto-detects relay IP from en0/en1, runs container

## CLI Options

### Peer
```
peer -i <iface> [-p <port>] [-K <keyfile>] -P <pubkey_hex> [-E <ip:port>] [-R <cidr>] [...] [-k <psk>] [-v]
```
| Option | Description |
|--------|-------------|
| `-i`   | TUN interface name (required) |
| `-p`   | Listen port (default: 5040) |
| `-K`   | Static private key PEM file (default: `peer.key`) |
| `-P`   | Known peer public key hex (repeatable) |
| `-E`   | Peer endpoint `ip:port` — initiates outbound connection |
| `-R`   | AllowedIPs CIDR for the preceding `-P` entry (repeatable) |
| `-k`   | Pre-shared key for handshake HMAC authentication |
| `-v`   | Verbose / debug logging |

## Tunnel Interface
- Interface name: `lanecove.0`
- Overlay network: `10.9.0.0/24` (relay=`10.9.0.1`, peer-a=`10.9.0.2`, peer-b=`10.9.0.3`)

## Key Environment Variables
| Variable | Default | Description |
|----------|---------|-------------|
| `TUNNEL_NAME` | `lanecove.0` | TUN interface name |
| `PEER_PORT` | `5040` | Listen port |
| `PEER_IP` | `10.9.0.1/24` | This peer's overlay IP |
| `PEER_ROUTES` | _(none)_ | Space-separated CIDRs to route via TUN |
| `PEER_PUB_n` | — | Known peer public key hex |
| `PEER_ENDPOINT_n` | — | Peer endpoint `ip:port` |
| `PEER_ALLOWED_IPS_n` | — | AllowedIPs CIDR(s) for peer n |

## Platform Notes
- Requires Linux kernel (uses `linux/if_tun.h` and `/dev/net/tun`)
- Does not compile on macOS — use Docker for building and running
- Docker containers require `--cap-add=NET_ADMIN` and `--device=/dev/net/tun`
- Requires libssl-dev (OpenSSL) for AES-256-GCM and X25519
