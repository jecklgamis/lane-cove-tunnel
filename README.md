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
peer-a (10.9.0.2) ──┐
                     ├── UDP 5040 ── relay (10.9.0.1, public IP)
peer-b (10.9.0.3) ──┘
```

**Direct peer-to-peer (no relay)** — if at least one peer has a public IP (or both are on the same network), they can connect directly without a relay. The peer with the public IP omits `-E`; the other peer points `-E` at it.

```
peer-a (10.9.0.1, public IP) ──── UDP 5040 ──── peer-b (10.9.0.2)
```

```bash
# peer-a — public IP, inbound only (no -E)
$ PEER_IP=10.9.0.1/24 ./create-peer-tunnel.sh
$ ./peer -i lanecove0 -K peer-a.key \
    -P <peer-b-pubkey-hex> -R 10.9.0.2/32

# peer-b — connects outbound to peer-a
$ PEER_IP=10.9.0.2/24 ./create-peer-tunnel.sh
$ ./peer -i lanecove0 -K peer-b.key \
    -P <peer-a-pubkey-hex> -E <peer-a-ip>:5040 -R 10.9.0.1/32
```

If both peers have public IPs, both can set `-E` pointing at each other — they will race to initiate and converge on a shared session.

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

# On peer-a — binds host UDP port 5041 to pin the NAT mapping
$ RELAY_IP=<relay-public-ip> ./run-peer-a-in-docker.sh

# On peer-b — binds host UDP port 5042
$ RELAY_IP=<relay-public-ip> ./run-peer-b-in-docker.sh
```

Override host ports with `PEER_A_HOST_PORT` / `PEER_B_HOST_PORT` if the defaults conflict with other services.

> **Note (Docker Desktop on Mac):** Without pinned host ports, Docker Desktop's userspace NAT can remap the UDP source port between the handshake and subsequent data packets, causing the relay to drop traffic as "unknown peer".

### Testing

```bash
# From peer-a, ping/curl peer-b (10.9.0.3)
$ ./test-tunnel-using-peer-a.sh

# From peer-b, ping/curl peer-a (10.9.0.2)
$ ./test-tunnel-using-peer-b.sh

# From peer-a, ping/curl the relay (10.9.0.1)
$ ./test-tunnel-relay.sh

# Shell into containers
$ ./exec-shell-to-peer-a-container.sh
$ ./exec-shell-to-peer-b-container.sh
```

### Running Natively (Linux)

```bash
# Generate keys
$ ./generate-peer-keys.sh relay peer-a peer-b

# Relay (public VPS) — inbound only, no -E flag
$ PEER_IP=10.9.0.1/24 ./create-peer-tunnel.sh
$ ./run-relay.sh   # reads relay.key/crt + peer-a.crt + peer-b.crt

# peer-a — connects outbound to relay
$ PEER_IP=10.9.0.2/24 ./create-peer-tunnel.sh
$ PEER_KEY=peer-a.key PEER_IP=10.9.0.2/24 \
  PEER_PUB_1=<relay-pubkey-hex> PEER_ENDPOINT_1=<relay-ip>:5040 PEER_ALLOWED_IPS_1=10.9.0.0/24 \
  ./run-peer.sh

# peer-b — connects outbound to relay
$ PEER_IP=10.9.0.3/24 ./create-peer-tunnel.sh
$ PEER_KEY=peer-b.key PEER_IP=10.9.0.3/24 \
  PEER_PUB_1=<relay-pubkey-hex> PEER_ENDPOINT_1=<relay-ip>:5040 PEER_ALLOWED_IPS_1=10.9.0.0/24 \
  ./run-peer.sh
```

`create-peer-tunnel.sh` creates the TUN interface, assigns the overlay IP, disables ICMP redirects, and marks the interface unmanaged in NetworkManager.

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
