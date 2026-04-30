## lane-cove-tunnel

A simple Linux **hub-and-spoke layer 3 overlay network** using a TUN virtual interface over UDP. Implements a basic VPN for learning purposes.
Warning: not for production use.

Inspired by [WireGuard](https://www.wireguard.com/), this project explores similar concepts — X25519 key exchange, identity hiding, AllowedIPs routing, and session rekeying — reimplemented from scratch in C as a learning exercise.

It creates a virtual IP network (`10.9.0.0/24`) layered on top of an existing underlay network, with traffic encapsulated inside UDP datagrams. Layer 3 means the tunnel operates at the IP (network) layer — it forwards raw IP packets between peers, not Ethernet frames. Each peer has a TUN interface with an IP address, and routing rules direct traffic through it. Broadcast, multicast, and non-IP traffic are not supported.

The topology is **hub-and-spoke**: peers behind NAT connect outbound to a relay with a public IP, and all traffic between peers transits through the relay. The relay forwards packets between peers in user space — no kernel IP forwarding required. Peers do not connect directly to each other (no NAT hole-punching).

## Features

### Security
- **X25519 Diffie-Hellman key exchange** — ephemeral + static key pairs for forward secrecy and mutual authentication
- **AES-256-GCM encryption** — all tunnel traffic is authenticated and encrypted
- **Identity hiding** — static public keys are encrypted inside the handshake; passive observers cannot identify peers
- **PSK authentication** — optional HMAC-SHA256 over the handshake using a pre-shared key
- **Replay protection** — 2048-bit sliding window per session rejects replayed or reordered packets
- **AllowedIPs routing** — enforces a per-peer IP allowlist; packets with unexpected source IPs are dropped
- **Session rekeying** — peers re-handshake every 3 minutes, rotating the session key automatically
- **DoS mitigations** — 5-second handshake cooldown per address and per public key

### Limitations
- **Linux only** — uses `linux/if_tun.h` and `/dev/net/tun`; does not compile on macOS (use Docker)
- **IPv4 only** — TUN packets are validated as IPv4; IPv6 and non-IP traffic are dropped
- **UDP transport** — no packet ordering guarantees; packet loss is not retransmitted
- **Single-threaded** — one epoll loop handles all I/O; not designed for high throughput
- **Learning project** — not audited, not hardened for production use

## Requirements
Linux (tested on Ubuntu 22.04 LTS), gcc, make, iproute2, libssl-dev, libyaml-dev

```
sudo apt install gcc make iproute2 libssl-dev libyaml-dev
```

## Building
```
make all        # build peer binary
make image      # build Docker image (lane-cove-tunnel-peer:latest)
make clean      # remove peer binary
```

## Configuration

The `peer` binary is configured via a YAML file. Sample configs are in `config/`.

**Relay** (`config/relay.yaml`) — inbound-only, no `endpoint`:
```yaml
interface: lanecove0
port: 5040
private_key_file: /lanecove/relay.key
pre_shared_key: some-psk
verbose: false

peers:
  - public_key: <peer-1-pubkey-hex>
    allowed_ips:
      - 10.9.0.2/32
  - public_key: <peer-2-pubkey-hex>
    allowed_ips:
      - 10.9.0.3/32
```

**Peer 1** (`config/peer-1.yaml`) — connects outbound to relay:
```yaml
interface: lanecove0
port: 5040
private_key_file: /lanecove/peer-1.key
pre_shared_key: some-psk
verbose: false

peers:
  - public_key: <relay-pubkey-hex>
    endpoint: <relay-host-or-ip>:5040
    allowed_ips:
      - 10.9.0.0/24
```

**Peer 2** (`config/peer-2.yaml`) — same as peer-1 with its own key:
```yaml
interface: lanecove0
port: 5040
private_key_file: /lanecove/peer-2.key
pre_shared_key: some-psk
verbose: false

peers:
  - public_key: <relay-pubkey-hex>
    endpoint: <relay-host-or-ip>:5040
    allowed_ips:
      - 10.9.0.0/24
```

**Top-level keys:**

| Key | Required | Description |
|-----|----------|-------------|
| `interface` | Yes | TUN interface name (e.g. `lanecove0`) |
| `port` | Yes | UDP port to bind |
| `private_key_file` | Yes | Path to this peer's X25519 private key PEM file |
| `pre_shared_key` | No | Optional PSK for HMAC authentication of handshakes |
| `verbose` | No | Set to `true` to enable debug logging (default: `false`) |
| `peers` | No | List of peer entries (omit or leave empty for inbound-only) |

**Per-peer keys (under `peers:`):**

| Key | Required | Description |
|-----|----------|-------------|
| `public_key` | Yes | Peer's X25519 public key (hex-encoded) |
| `endpoint` | No | `host:port` to connect to; omit for inbound-only peers (e.g. relay) |
| `allowed_ips` | Yes | List of CIDR prefixes allowed from this peer (used for routing and source validation) |

The relay uses `/32` per peer because it only accepts packets sourced from each peer's specific overlay IP. Peers use `/24` (the full subnet) because all overlay traffic — including traffic destined for other peers — is routed through the relay as the single gateway.

## Key Generation

```bash
./scripts/generate-peer-keys.sh relay peer-1 peer-2
# produces: config/relay.key/.crt, config/peer-1.key/.crt, config/peer-2.key/.crt

# Inspect key hex values (private + public) from a .key file
./scripts/extract-keys-hex.sh config/peer-1.key

# Extract just the public key hex from a .crt file
./scripts/extract-pubkey-hex.sh config/peer-1.crt
```

Distribute public keys (`.crt` files only — never share `.key` files):

| Machine | Needs |
|---------|-------|
| relay   | `relay.key`, `peer-1.crt`, `peer-2.crt` |
| peer-1  | `peer-1.key`, `relay.crt` |
| peer-2  | `peer-2.key`, `relay.crt` |

## Running With Docker

**Setup (once):**
```bash
make image
```

**Local testing (all on one machine — 3 terminals):**

> Before running, update the `endpoint` in `config/peer-1.yaml` and `config/peer-2.yaml` to point to your relay's host or IP address.

```bash
./scripts/run-relay-in-docker.sh
./scripts/run-peer-1-in-docker.sh
./scripts/run-peer-2-in-docker.sh
```

**Testing the tunnel:**
```bash
./scripts/test-tunnel-using-peer-1.sh       # ping + curl peer-2 from peer-1
./scripts/test-tunnel-using-peer-2.sh       # ping + curl peer-1 from peer-2
./scripts/test-tunnel-relay.sh              # ping + curl both peers from relay
```

**Shell access:**
```bash
./scripts/exec-shell-to-peer-1-container.sh
./scripts/exec-shell-to-peer-2-container.sh
```

### Port Mapping

| Service | Container port | relay host | peer-1 host | peer-2 host |
|---------|---------------|------------|-------------|-------------|
| UDP tunnel | 5040 | 5040 | 5042 | 5043 |
| nginx | 80 | — | — | — |
| Envoy TCP proxy | 15040 | — | 15042 | 15043 |
| Envoy HTTP proxy | 15050 | — | 15052 | 15053 |
| Envoy admin | 9901 | 9901 | 9902 | 9903 |

### Envoy Proxy

Each peer container includes an [Envoy](https://www.envoyproxy.io/) proxy. When `ENVOY_UPSTREAM_HOST` is set, Envoy starts and forwards connections to the configured upstream.

Two listeners:

| Listener | Port | Mode |
|----------|------|------|
| TCP proxy | 15040 | L4 pass-through |
| HTTP proxy | 15050 | L7 with connection pooling (~130 req/connection) |
| Admin | 9901 | HTTP stats |

```bash
curl http://localhost:15052   # HTTP proxy through peer-1 → peer-2's nginx
curl http://localhost:15042   # TCP proxy
curl http://localhost:9902/stats  # Envoy admin
```

### Docker Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `TUNNEL_NAME` | `lanecove0` | TUN interface name |
| `PEER_IP` | `10.9.0.1/24` | This peer's overlay IP/CIDR |
| `PEER_ROUTES` | _(none)_ | Space-separated extra CIDRs to route via TUN |
| `PEER_CONFIG` | `peer.yaml` | Path to YAML config file inside container |
| `ENVOY_UPSTREAM_HOST` | — | Upstream host for Envoy; if unset, Envoy is not started |
| `ENVOY_UPSTREAM_PORT` | `80` | Upstream port for Envoy |

## Running Natively (Linux)

The `peer` binary can run as a non-root user if the TUN interface is pre-created with the correct owner:

```bash
# Generate keys (once)
./scripts/generate-peer-keys.sh relay peer-1 peer-2

# Update the `endpoint` in config/peer-1.yaml and config/peer-2.yaml
# to point to your relay's host or IP address before starting peers.

# Relay
./scripts/run-relay.sh

# peer-1
./scripts/run-peer-1.sh

# peer-2
./scripts/run-peer-2.sh
```

`create-peer-tunnel.sh` creates the TUN interface owned by the calling user (`$SUDO_USER`), so `peer` can open it without `CAP_NET_ADMIN`.

---

## Security Details

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

### Data Packets

```
[12-byte IV][AES-256-GCM ciphertext of (8-byte magic + 8-byte seq + payload)][16-byte GCM tag]
```

Magic is `0xdeadbeefcafebabe`. Packets with a bad magic header, invalid GCM tag, or replayed sequence number are silently dropped.

---

## References
* https://www.kernel.org/doc/Documentation/networking/tuntap.txt
* https://www.wireguard.com/papers/wireguard.pdf
