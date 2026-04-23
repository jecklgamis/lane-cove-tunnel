# lane-cove-tunnel

## Project Overview
A simple Linux TUN/TAP-based IP tunnel over UDP. Implements a basic VPN for learning purposes. Not for production use.

## Architecture

### UDP
- `server.c` — binds a UDP socket, maintains a connected-client list, performs X25519 DH handshakes, enforces an AllowedIPs routing table, and forwards packets between the TUN interface and UDP socket. Detects re-handshake packets from reconnecting or rekeying clients and updates the session in-place.
- `client.c` — performs X25519 DH handshake with the server, forwards packets between the TUN interface and the UDP socket. Reconnects and re-handshakes on error or after 5 minutes (session rekeying).
- `common.c/h` — TUN interface allocation, logging macros, AES-256-GCM encrypt/decrypt helpers, X25519 DH handshake functions with identity hiding, and a 2048-bit sliding-window replay-protection implementation.

#### Key Provisioning
Keys are X25519 (Curve25519) key pairs stored in PEM files. Use `generate-ssl-certs.sh` to generate server and client key pairs before building images. The script prompts before overwriting existing files.

```
./generate-ssl-certs.sh
# produces: server.key, server.crt, client.key, client.crt
```

Both server and client **require** a static private key file at startup (`-K`). The server additionally requires at least one allowlisted client (`-A` + `-R`). The client requires the server's public key file for cert pinning (`-C`).

#### Handshake Wire Format
```
[8 magic][32 eph_pub][48 AES-256-GCM(static_pub)][32 HMAC-SHA256(psk,...)]?
```

- **`eph_pub`** — fresh X25519 ephemeral public key, generated per handshake
- **`AES-256-GCM(static_pub)`** — static public key encrypted to hide identity from passive observers:
  - Client encrypts its static_pub with `key = SHA-256(DH(eph_c, server_static))`
  - Server encrypts its static_pub with `key = SHA-256(DH(eph_s, eph_c))`
  - Zero IV is safe because the encryption key is derived from a fresh ephemeral DH every time
- **`HMAC-SHA256`** — optional PSK authentication over `[magic][eph_pub][encrypted_static_pub]`; omitted if no PSK

#### Session Key Derivation
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

Three DH values contribute to the session key: ephemeral-ephemeral, client-static × server-ephemeral, and client-ephemeral × server-static. This provides forward secrecy (from the ephemeral component) and mutual authentication (from the static components).

#### Data Packets
Each datagram on the wire is:
```
[12-byte IV][AES-256-GCM ciphertext of (8-byte magic + 8-byte seq + payload)][16-byte GCM tag]
```
Magic is `0xdeadbeefcafebabe`. The sequence number is a per-session monotonically increasing 64-bit counter (big-endian). Packets with a bad magic header, invalid GCM tag, or replayed/too-old sequence number are silently dropped. The receiver uses a **2048-bit sliding window** (`32 × uint64_t`) to detect replays.

#### Session Rekeying
The client automatically triggers a rekey every **5 minutes** (`REKEY_AFTER_SECS = 300`). When the deadline is reached, the client closes its socket and reconnects, performing a fresh DH handshake. The server recognises the incoming handshake by the packet size and updates the client entry in-place (resetting sequence counters and session key while preserving the AllowedIPs routes).

#### AllowedIPs Routing
The server maintains a per-client routing table. On the TUN→UDP path, destination IP is looked up using longest-prefix match across all client route tables. On the UDP→TUN path, source IP is validated against the sending client's AllowedIPs — packets with an unexpected source IP are dropped. At least one `-A` + `-R` entry is mandatory at server startup.

#### Client Roaming
When a client reconnects on a new source port (e.g. after a container restart or rekey), the server looks up the client by static public key rather than IP:port. If found, the address is updated in-place and the session is re-keyed without consuming a new client slot.

#### Handshake Cooldown and DoS Mitigation
The server enforces a 5-second cooldown between handshakes per address and per public key to prevent handshake flooding. New client slots are only consumed for truly new public keys; rekeying existing clients is not counted against `max_clients`.

## Logging
Custom `fprintf`-based logging defined in `common.h`. Global `log_level` variable (0=INFO, 1=DEBUG). Pass `-v` flag to enable debug output.

## Build
```
make all          # compile server and client
make clean        # remove binaries
```

## Running With Docker
```
./generate-ssl-certs.sh           # generate key pairs (once)
./run-server-in-docker.sh
SERVER_IP=<ip> ./run-client-in-docker.sh
docker run --privileged -it lane-cove-tunnel-udp-server:latest /bin/bash
docker run --privileged -it lane-cove-tunnel-udp-client:latest /bin/bash
```

## Docker
- `Dockerfile.server` — server image; copies `server.key`, `server.crt`, `client.crt` into the image
- `Dockerfile.client` — client image; copies `client.key`, `client.crt`, `server.crt` into the image
- `docker-entrypoint-server.sh` — extracts client public key from `client.crt`, creates tunnel, starts nginx, starts server
- `docker-entrypoint-client.sh` — creates tunnel, starts client with server cert pinning
- `run-server-in-docker.sh` — builds server image and runs container, exposes UDP port
- `run-client-in-docker.sh` — builds client image and runs container, auto-detects host IP from en0/en1

## CLI Options

### Server
```
server -i <iface> [-p <port>] [-K <keyfile>] -A <pubkey_hex> -R <cidr> [...] [-m <max>] [-k <psk>] [-v]
```
| Option | Description |
|--------|-------------|
| `-i`   | TUN interface name (required) |
| `-p`   | UDP port (default: 5040) |
| `-K`   | Static private key PEM file (default: `server.key`, required) |
| `-A`   | Allowlisted client public key (hex, repeatable) |
| `-R`   | AllowedIPs CIDR for the preceding `-A` entry (repeatable, required per `-A`) |
| `-m`   | Max connected clients (default: 16) |
| `-k`   | Pre-shared key for handshake HMAC authentication |
| `-v`   | Verbose / debug logging |

At least one `-A <pubkey> -R <cidr>` pair is mandatory. Multiple `-R` entries may follow a single `-A`.

### Client
```
client -i <iface> -s <server-ip> -C <server-cert> [-p <port>] [-k <psk>] [-K <keyfile>] [-E <pubkey-hex>] [-v]
```
| Option | Description |
|--------|-------------|
| `-i`   | TUN interface name (required) |
| `-s`   | Server IP address (required) |
| `-C`   | Server public key PEM file for cert pinning (required) |
| `-p`   | Server UDP port (default: 5040) |
| `-k`   | Pre-shared key for handshake HMAC authentication |
| `-K`   | Static private key PEM file (default: `client.key`, required) |
| `-E`   | Server public key hex — overrides `-C` |
| `-v`   | Verbose / debug logging |

## Tunnel Interface
- Interface name: `lanecove-udp`
- Server overlay network: `10.10.0.0/24` (fixed at `10.10.0.1/24`)
- Client overlay network: `10.9.0.0/24` (fixed at `10.9.0.1/24`)

## Key Environment Variables
| Variable       | Default        | Description                  |
|----------------|----------------|------------------------------|
| `TUNNEL_NAME`  | `lanecove-udp` | TUN interface name           |
| `SERVER_PORT`  | `5040`         | Tunnel port                  |
| `SERVER_IP`    | (auto)         | Server IP (client only)      |
| `ALLOWED_IPS`  | `10.9.0.0/24`  | Client AllowedIPs (server entrypoint) |

## Platform Notes
- Requires Linux kernel (uses `linux/if_tun.h` and `/dev/net/tun`)
- Does not compile on macOS — use Docker for building and running
- Docker containers require `--cap-add=NET_ADMIN` and `--device=/dev/net/tun`
- Requires libssl-dev (OpenSSL) for AES-256-GCM and X25519
