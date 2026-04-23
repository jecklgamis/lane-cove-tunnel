## lane-cove-tunnel

A simple Linux **point-to-point layer 3 overlay network** using a TUN virtual interface over UDP. Implements a basic VPN for learning purposes.
Warning: not for production use.

Inspired by [WireGuard](https://www.wireguard.com/), this project explores similar concepts — X25519 key exchange, identity hiding, AllowedIPs routing, and session rekeying — reimplemented from scratch in C as a learning exercise.

It creates a virtual IP network layered on top of an existing underlay network, with traffic encapsulated inside UDP datagrams. Layer 3 means the tunnel operates at the IP (network) layer — it forwards raw IP packets between peers, not Ethernet frames. Each end of the tunnel has a TUN interface with an IP address, and routing rules direct traffic through it. Broadcast, multicast, and non-IP traffic are not supported.

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

## Modes

This project has two modes:

### 1. Peer Mode (recommended)

A symmetric peer model where every node runs the same `peer` binary. Any peer can initiate or accept connections. A relay peer with a public IP acts as a transit node for peers behind NAT.

### 2. Server/Client Mode (classic)

The original asymmetric model — a dedicated server accepts connections from clients. See [Server/Client Setup](#serverlient-setup) below.

---

## Peer Mode Setup

### Architecture

```
peer-a (10.9.0.2) ──┐
                     ├── UDP 5040 ── relay (10.9.0.1, public IP)
peer-b (10.9.0.3) ──┘
```

Peers behind NAT connect outbound to the relay. The relay forwards traffic between them. All three run the same `peer` binary.

### Key Generation

```bash
$ ./generate-peer-keys.sh relay peer-a peer-b
# produces: relay.key/crt, peer-a.key/crt, peer-b.key/crt
```

Distribute public keys (`.crt` files only — never share `.key` files):

| Machine | Needs |
|---------|-------|
| relay (VPS) | `relay.key`, `relay.crt`, `peer-a.crt`, `peer-b.crt` |
| peer-a | `peer-a.key`, `peer-a.crt`, `relay.crt` |
| peer-b | `peer-b.key`, `peer-b.crt`, `relay.crt` |

### Running With Docker

```bash
# On the relay (VPS)
$ ./run-relay-in-docker.sh

# On peer-a (auto-detects host IP from en0/en1)
$ RELAY_IP=<relay-public-ip> ./run-peer-a-in-docker.sh

# On peer-b
$ RELAY_IP=<relay-public-ip> ./run-peer-b-in-docker.sh
```

### Testing

```bash
# From peer-a, ping/curl peer-b (10.9.0.3)
$ ./test-tunnel-using-peer-a.sh

# From peer-b, ping/curl peer-a (10.9.0.2)
$ ./test-tunnel-using-peer-b.sh

# Shell into containers
$ ./exec-shell-to-peer-a-container.sh
$ ./exec-shell-to-peer-b-container.sh
```

### Running Natively (Linux)

```bash
# Generate keys
$ ./generate-peer-keys.sh relay peer-a peer-b

# Relay (public VPS) — inbound only, no -E flag
$ ./create-peer-tunnel.sh   # PEER_IP=10.9.0.1/24
$ ./peer -i lanecove.0 -K relay.key \
    -P <peer-a-pubkey-hex> -R 10.9.0.2/32 \
    -P <peer-b-pubkey-hex> -R 10.9.0.3/32

# peer-a — connects outbound to relay
$ ./create-peer-tunnel.sh   # PEER_IP=10.9.0.2/24
$ ./peer -i lanecove.0 -K peer-a.key \
    -P <relay-pubkey-hex> -E <relay-ip>:5040 -R 10.9.0.0/24

# peer-b — connects outbound to relay
$ ./create-peer-tunnel.sh   # PEER_IP=10.9.0.3/24
$ ./peer -i lanecove.0 -K peer-b.key \
    -P <relay-pubkey-hex> -E <relay-ip>:5040 -R 10.9.0.0/24
```

Extract a peer's public key hex from its `.crt` file:
```bash
$ openssl pkey -in relay.crt -pubin -outform DER | tail -c 32 | od -An -tx1 | tr -d ' \n'
```

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
| `TUNNEL_NAME` | `lanecove.0` | TUN interface name |
| `PEER_PORT` | `5040` | Listen port |
| `PEER_IP` | `10.9.0.1/24` | This peer's overlay IP |
| `PEER_ROUTES` | _(none)_ | Space-separated CIDRs to route via TUN |
| `PEER_PUB_n` | — | Known peer public key hex |
| `PEER_ENDPOINT_n` | — | Peer endpoint `ip:port` (triggers outbound) |
| `PEER_ALLOWED_IPS_n` | — | AllowedIPs for peer n |

---

## Server/Client Setup

The original asymmetric model. The server accepts connections from one or more clients.

### Key Generation

```bash
$ ./generate-ssl-certs.sh
# produces: server.key, server.crt, client.key, client.crt
```

### Running With Docker

```bash
$ ./generate-ssl-certs.sh
$ ./run-server-in-docker.sh
$ SERVER_IP=<server-ip> ./run-client-in-docker.sh
```

The client auto-detects the host IP from `en0` (falling back to `en1`) on Mac.

### Running Natively (Linux)

```bash
# Server
$ ./create-server-tunnel.sh   # creates lanecove.0 at 10.10.0.1/24
$ ./server -i lanecove.0 -p 5040 -K server.key -A <client-pubkey-hex> -R 10.9.0.0/24

# Client
$ ./create-client-tunnel.sh   # creates lanecove.0 at 10.9.0.1/24
$ ./client -i lanecove.0 -s <server-ip> -p 5040 -K client.key -C server.crt
```

### Multiple Clients

```bash
$ CLIENT_CERT_1=client1.crt ALLOWED_IPS_1=10.9.1.0/24 \
  CLIENT_CERT_2=client2.crt ALLOWED_IPS_2=10.9.2.0/24 \
  ./run-server-in-docker.sh

$ SERVER_IP=<ip> CLIENT_IP=10.9.1.1/24 ./run-client-in-docker.sh
$ SERVER_IP=<ip> CLIENT_IP=10.9.2.1/24 ./run-client-in-docker.sh
```

### Server CLI Options

```
server -i <iface> [-p <port>] [-K <keyfile>] -A <pubkey_hex> -R <cidr> [...] [-m <max>] [-k <psk>] [-v]
```

| Option | Description |
|--------|-------------|
| `-i`   | TUN interface name (required) |
| `-p`   | UDP port (default: 5040) |
| `-K`   | Static private key PEM file (default: `server.key`) |
| `-A`   | Allowlisted client public key hex (repeatable, required) |
| `-R`   | AllowedIPs CIDR for the preceding `-A` entry (repeatable, required per `-A`) |
| `-m`   | Max connected clients (default: 16) |
| `-k`   | Pre-shared key for handshake HMAC authentication |
| `-v`   | Verbose / debug logging |

### Client CLI Options

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
| `-K`   | Static private key PEM file (default: `client.key`) |
| `-E`   | Server public key hex — overrides `-C` |
| `-v`   | Verbose / debug logging |

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
sudo tshark -i lanecove.0
```

## References
* https://www.kernel.org/doc/Documentation/networking/tuntap.txt
* https://www.wireguard.com/papers/wireguard.pdf
