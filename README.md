## lane-cove-tunnel

A simple Linux **hub-and-spoke layer 3 overlay network** using a TUN virtual interface over UDP. Implements a basic VPN for learning purposes.
Warning: not for production use.

Inspired by [WireGuard](https://www.wireguard.com/), this project explores similar concepts — X25519 key exchange, identity hiding, AllowedIPs routing, and session rekeying — reimplemented from scratch in C as a learning exercise.

It creates a virtual IP network (`10.9.0.0/24`) layered on top of an existing underlay network, with traffic encapsulated inside UDP datagrams. Layer 3 means the tunnel operates at the IP (network) layer — it forwards raw IP packets between peers, not Ethernet frames. Each peer has a TUN interface with an IP address, and routing rules direct traffic through it. Broadcast, multicast, and non-IP traffic are not supported.

The topology is **hub-and-spoke**: peers behind NAT connect outbound to a relay with a public IP, and all traffic between peers transits through the relay. Peers do not connect directly to each other (no NAT hole-punching).

## Features

### Security
- **X25519 Diffie-Hellman key exchange** — ephemeral + static key pairs for forward secrecy and mutual authentication
- **AES-256-GCM encryption** — all tunnel traffic is authenticated and encrypted
- **Identity hiding** — static public keys are encrypted inside the handshake; passive observers cannot identify peers
- **PSK authentication** — optional HMAC-SHA256 over the handshake using a pre-shared key (`-k`)
- **Replay protection** — 2048-bit sliding window per session rejects replayed or reordered packets
- **AllowedIPs routing** — enforces a per-peer IP allowlist; packets with unexpected source IPs are dropped
- **Session rekeying** — peers re-handshake every 5 minutes, rotating the session key automatically
- **DoS mitigations** — 5-second handshake cooldown per address and per public key

### Limitations
- **Linux only** — uses `linux/if_tun.h` and `/dev/net/tun`; does not compile on macOS (use Docker)
- **IPv4 only** — TUN packets are validated as IPv4; IPv6 and non-IP traffic are dropped
- **UDP transport** — no packet ordering guarantees; packet loss is not retransmitted
- **Single-threaded** — one epoll loop handles all I/O; not designed for high throughput
- **Learning project** — not audited, not hardened for production use

## Requirements
* Linux (tested using Ubuntu 22.04 LTS), gcc, make, iproute2, libssl-dev

```
$ sudo apt install gcc make iproute2 libssl-dev
```

## Building
```
$ make all        # build server, client, and peer binaries
$ make clean      # remove binaries
```

## Peer Mode Setup

### Architecture

The same `peer` binary supports two topologies:

**Hub-and-spoke (with relay)** — peers behind NAT connect outbound to a relay with a public IP. The relay forwards traffic between them.

```
peer-1 (10.9.0.2) ──┐
                     ├── UDP 5040 ── relay (10.9.0.1, public IP)
peer-2 (10.9.0.3) ──┘
```

**Direct peer-to-peer (no relay)** — if at least one peer has a public IP (or both are on the same network), they can connect directly without a relay. The peer with the public IP omits `-E`; the other peer points `-E` at it.

```
peer-1 (10.9.0.1, public IP) ──── UDP 5040 ──── peer-2 (10.9.0.2)
```

```bash
# peer-1 — public IP, inbound only (no -E)
$ PEER_IP=10.9.0.1/24 ./create-peer-tunnel.sh
$ ./peer -i lanecove0 -K peer-1.key \
    -P <peer-2-pubkey-hex> -R 10.9.0.2/32

# peer-2 — connects outbound to peer-1
$ PEER_IP=10.9.0.2/24 ./create-peer-tunnel.sh
$ ./peer -i lanecove0 -K peer-2.key \
    -P <peer-1-pubkey-hex> -E <peer-1-ip>:5040 -R 10.9.0.1/32
```

If both peers have public IPs, both can set `-E` pointing at each other — they will race to initiate and converge on a shared session.

### Key Generation

```bash
$ ./generate-peer-keys.sh relay peer-1 peer-2
# produces: relay.key/crt, peer-1.key/crt, peer-2.key/crt
```

Distribute public keys (`.crt` files only — never share `.key` files):

| Machine | Needs |
|---------|-------|
| relay (VPS) | `relay.key`, `relay.crt`, `peer-1.crt`, `peer-2.crt` |
| peer-1 | `peer-1.key`, `peer-1.crt`, `relay.crt` |
| peer-2 | `peer-2.key`, `peer-2.crt`, `relay.crt` |

### Running With Docker

**Setup (once):**
```bash
$ ./generate-peer-keys.sh relay peer-1 peer-2
$ make image
```

**Local testing (all on one Mac — 3 terminals):**
```bash
# Terminal 1 — relay
$ ./run-relay-in-docker-dev.sh

# Terminal 2 — peer-1
$ RELAY_IP=<your-mac-ip> ./run-peer-1-in-docker.sh

# Terminal 3 — peer-2
$ RELAY_IP=<your-mac-ip> ./run-peer-2-in-docker.sh
```

Get your Mac IP: `ipconfig getifaddr en0`

**Distributed (relay on VPS, peers on separate machines):**
```bash
# On the relay (VPS)
$ ./run-relay-in-docker.sh -i lanecove0 -k relay.key -c relay.crt \
    --peer-ip 10.9.0.1/24 \
    -p peer-1.crt:10.9.0.2/32 \
    -p peer-2.crt:10.9.0.3/32

# On peer-1
$ RELAY_IP=<relay-public-ip> ./run-peer-1-in-docker.sh

# On peer-2
$ RELAY_IP=<relay-public-ip> ./run-peer-2-in-docker.sh
```

Override host ports with `PEER_1_HOST_PORT` / `PEER_2_HOST_PORT` if the defaults conflict with other services.

### Envoy Proxy

Each peer container includes an [Envoy](https://www.envoyproxy.io/) proxy. When `ENVOY_UPSTREAM_HOST` is set, Envoy starts and forwards connections to the configured upstream — useful for proxying traffic across the tunnel to a service running on a remote peer.

Two listeners are available:

| Listener | Port | Mode | Notes |
|----------|------|------|-------|
| TCP proxy | 15040 | L4 pass-through | One upstream connection per downstream connection |
| HTTP proxy | 15050 | L7 with connection pooling | Recommended for HTTP — amortizes tunnel connect cost across ~130–170 requests per upstream connection |
| Admin | 9901 | HTTP stats/config | `/stats`, `/clusters`, `/listeners` |

By default:
- peer-1 proxies to `10.9.0.3:80` (peer-2's nginx), exposed on host ports `15042` (TCP), `15052` (HTTP), `9901` (admin)
- peer-2 proxies to `10.9.0.2:80` (peer-1's nginx), exposed on host ports `15043` (TCP), `15053` (HTTP), `9902` (admin)

| Variable | Default | Description |
|----------|---------|-------------|
| `ENVOY_UPSTREAM_HOST` | — | Upstream host; if unset, Envoy is not started |
| `ENVOY_UPSTREAM_PORT` | `80` | Upstream port |

```bash
# HTTP proxy through peer-1's Envoy to peer-2's nginx (recommended)
$ curl http://localhost:15052

# TCP proxy (transparent L4)
$ curl http://localhost:15042

# Envoy admin stats
$ curl http://localhost:9901/stats
```

> **Note (Docker Desktop on Mac):** Without pinned host ports, Docker Desktop's userspace NAT can remap the UDP source port between the handshake and subsequent data packets, causing the relay to drop traffic as "unknown peer".

### Performance

Performance was measured using [gatling-scala-example](https://github.com/jecklgamis/gatling-scala-example) sending HTTP GET requests through peer-1's Envoy HTTP proxy (port 15052) to peer-2's nginx.

**Test environment:**
- Relay: DigitalOcean droplet (1 vCPU, 512 MB RAM) running the relay binary
- peer-1: Mac Mini M4 (Docker container)
- peer-2: MacBook Air M4 (Docker container)

The ~200ms response time floor is the inherent round-trip latency of the two-hop encrypted overlay (peer-1 → relay → peer-2).

| Load | Requests | OK | p50 | p95 | p99 | Max | Notes |
|------|----------|----|-----|-----|-----|-----|-------|
| 10 rps | 4,800 | **100%** | 204ms | 222ms | 249ms | 653ms | |
| 40 rps | 9,600 | **100%** | 203ms | 209ms | 228ms | 849ms | |
| 100 rps | 24,000 | **100%** | 202ms | 207ms | 216ms | 660ms | |
| 250 rps | 60,000 | **82%** | — | — | — | — | Relay CPU saturated — 18% 503/504 errors |

Latency tightens as load increases — connection pool utilization improves at higher concurrency, reducing variance. The ~200ms floor holds stable up to 100 rps. At 250 rps the single-threaded relay event loop on a 1 vCPU droplet saturates: connect timeouts spike and Envoy begins shedding requests. The ceiling for this hardware is somewhere between 100 and 250 rps.

### Testing

```bash
# From peer-1, ping/curl peer-2 (10.9.0.3)
$ ./test-tunnel-using-peer-1.sh

# From peer-2, ping/curl peer-1 (10.9.0.2)
$ ./test-tunnel-using-peer-2.sh

# From peer-1, ping/curl the relay (10.9.0.1)
$ ./test-tunnel-relay.sh

# Shell into containers
$ ./exec-shell-to-peer-1-container.sh
$ ./exec-shell-to-peer-2-container.sh

# Generic — test any container/target or open a shell
$ ./test-tunnel.sh lane-cove-tunnel-peer-1 10.9.0.3
$ ./exec-shell.sh lane-cove-tunnel-peer-1

# Shell into relay
$ ./exec-shell.sh lane-cove-tunnel-relay
```

### Running Natively (Linux)

```bash
# Generate keys
$ ./generate-peer-keys.sh relay peer-1 peer-2

# Relay (public VPS) — inbound only
$ ./create-peer-tunnel.sh lanecove0 10.9.0.1/24
$ ./run-as-relay.sh -i lanecove0 -k relay.key \
    -p peer-1.crt:10.9.0.2/32 \
    -p peer-2.crt:10.9.0.3/32

# peer-1 — connects outbound to relay
$ ./create-peer-tunnel.sh lanecove0 10.9.0.2/24
$ ./run-as-peer.sh -i lanecove0 -k peer-1.key \
    -p relay.crt:<relay-ip>:5040:10.9.0.0/24

# peer-2 — connects outbound to relay
$ ./create-peer-tunnel.sh lanecove0 10.9.0.3/24
$ ./run-as-peer.sh -i lanecove0 -k peer-2.key \
    -p relay.crt:<relay-ip>:5040:10.9.0.0/24
```

`create-peer-tunnel.sh` creates the TUN interface, assigns the overlay IP, disables ICMP redirects, and marks the interface unmanaged in NetworkManager.

### Peer CLI Options

```
peer -i <iface> [-p <port>] [-K <keyfile>] -P <pubkey_hex> [-E <ip:port>] [-R <cidr>] [...] [-k <psk>] [-v]
```

| Option | Description |
|--------|-------------|
| `-i`   | TUN interface name (required) |
| `-p`   | Listen port (default: 5040) |
| `-K`   | Static private key PEM file (default: `peer.key`) |
| `-P`   | Known peer public key hex (repeatable) |
| `-E`   | Peer endpoint `ip:port` — if set, initiate outbound connection |
| `-R`   | AllowedIPs CIDR for the preceding `-P` entry (repeatable) |
| `-k`   | Pre-shared key for handshake HMAC authentication |
| `-v`   | Verbose / debug logging |

### Peer Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `TUNNEL_NAME` | `lanecove0` | TUN interface name |
| `PEER_PORT` | `5040` | Listen port |
| `PEER_IP` | `10.9.0.1/24` | This peer's overlay IP |
| `PEER_ROUTES` | _(none)_ | Space-separated CIDRs to route via TUN |
| `PEER_PUB_n` | — | Known peer public key hex |
| `PEER_ENDPOINT_n` | — | Peer endpoint `ip:port` (triggers outbound) |
| `PEER_ALLOWED_IPS_n` | — | AllowedIPs for peer n |

---

## Security

### Handshake Wire Format

```
[8 magic][32 eph_pub][48 AES-256-GCM(static_pub)][32 HMAC-SHA256(psk,...)]?
```

### Session Key Derivation

```
SHA-256(
  DH(eph_c, eph_s)        ||
  DH(static_c, eph_s)     ||
  DH(eph_c, static_s)     ||
  client_eph_pub          ||
  server_eph_pub          ||
  client_static_pub       ||
  server_static_pub
)
```

Three DH contributions provide forward secrecy (ephemeral-ephemeral) and mutual authentication (static components).

### Data Packets

```
[12-byte IV][AES-256-GCM ciphertext of (8-byte magic + 8-byte seq + payload)][16-byte GCM tag]
```

Magic is `0xdeadbeefcafebabe`. Packets with a bad magic header, invalid GCM tag, or replayed sequence number are silently dropped.

### Replay Protection

Each session uses a per-direction 64-bit sequence counter. The receiver maintains a **2048-bit sliding window** (32 × 64-bit words) to detect and drop replayed or reordered packets.

---

## Monitoring Tunnel Traffic
```
sudo apt install tshark
sudo tshark -i lanecove0
```

## References
* https://www.kernel.org/doc/Documentation/networking/tuntap.txt
* https://www.wireguard.com/papers/wireguard.pdf
