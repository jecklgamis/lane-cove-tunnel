## lane-cove-tunnel

A simple Linux **point-to-point layer 3 overlay network** using a TUN virtual interface over UDP. Implements a basic VPN for learning purposes.
Warning: not for production use.

It creates a virtual IP network (`10.9.0.0/24` ↔ `10.10.0.0/24`) layered on top of an existing underlay network, with traffic encapsulated inside UDP datagrams. Layer 3 means the tunnel operates at the IP (network) layer — it forwards raw IP packets between peers, not Ethernet frames. Each end of the tunnel has a TUN interface with an IP address, and routing rules direct traffic through it. Broadcast, multicast, and non-IP traffic are not supported.

## Features

### Security
- **X25519 Diffie-Hellman key exchange** — ephemeral + static key pairs for forward secrecy and mutual authentication
- **AES-256-GCM encryption** — all tunnel traffic is authenticated and encrypted
- **Identity hiding** — static public keys are encrypted inside the handshake; passive observers cannot identify peers
- **Cert pinning** — client verifies the server's static public key before completing the handshake (`-C`)
- **PSK authentication** — optional HMAC-SHA256 over the handshake using a pre-shared key (`-k`)
- **Replay protection** — 2048-bit sliding window per session rejects replayed or reordered packets
- **AllowedIPs routing** — server enforces a per-client IP allowlist; packets with unexpected source IPs are dropped
- **Session rekeying** — client re-handshakes every 5 minutes, rotating the session key automatically
- **DoS mitigations** — 5-second handshake cooldown per address and per public key; max-client cap for new peers only

### Limitations
- **Linux only** — uses `linux/if_tun.h` and `/dev/net/tun`; does not compile on macOS (use Docker)
- **IPv4 only** — TUN packets are validated as IPv4; IPv6 and non-IP traffic are dropped
- **UDP transport** — no packet ordering guarantees; packet loss is not retransmitted
- **Single-threaded** — one epoll loop handles all I/O; not designed for high throughput
- **No key rotation for long-lived servers** — server static key is loaded once at startup
- **No certificate revocation** — removing a client requires a server restart with the key removed from `-A`
- **Learning project** — not audited, not hardened for production use

## Requirements
* Linux (tested using Ubuntu 22.04 LTS), gcc, make, iproute2, libssl-dev

```
$ sudo apt install gcc make iproute2 libssl-dev
```

## Building
```
$ make all        # build server and client
$ make clean      # remove binaries
```

## Key Generation

Before building Docker images or running natively, generate X25519 key pairs for server and client:

```
$ ./generate-ssl-certs.sh
# produces: server.key, server.crt, client.key, client.crt
```

The script prompts before overwriting existing files.

## Example Setup
```
10.9.0.0/24 network                                        10.10.0.0/24 network
...                                                        ...
[transport: segment/packet]   <---- tunnel using UDP --->  [transport: segment/packet]
[network: ip packet]                                       [network: ip packet]
[datalink: ethernet frame ]                                [datalink: ethernet frame ]
[physical: bits]                                           [physical: bits]
```
* Local network : 10.9.0.0/24
* Remote network : 10.10.0.0/24
* Local machine :
  * Adds `lanecove-udp` virtual interface (`10.9.0.1/24`)
  * Adds route to `10.10.0.0/24` network via `10.9.0.1`
  * Runs the client and connects to the remote server on port 5040
* Remote machine :
  * Adds `lanecove-udp` virtual interface (`10.10.0.1/24`)
  * Adds route to `10.9.0.0/24` network via `10.10.0.1`
  * Runs the server on port 5040

## Running With Docker

```
$ ./generate-ssl-certs.sh                            # generate key pairs (once)
$ ./run-server-in-docker.sh                          # build server image and run
$ SERVER_IP=<server-ip> ./run-client-in-docker.sh   # build client image and run
```
The client auto-detects the host IP from `en0` (falling back to `en1`) on Mac.

## Running Natively (Linux)

### Running The Server

* Create the tunnel:
```
$ ./create-server-tunnel.sh   # creates lanecove-udp at 10.10.0.1/24
```

* Run the server binary directly:
```
$ ./server -i lanecove-udp -p 5040 -K server.key -A <client-pubkey-hex> -R 10.9.0.0/24 -k mysecret
```

### Running The Client

* Create the tunnel:
```
$ ./create-client-tunnel.sh   # creates lanecove-udp, 10.9.0.1/24
```

* Run the client binary directly:
```
$ ./client -i lanecove-udp -s <server-ip> -p 5040 -K client.key -C server.crt -k mysecret
```

The client public key hex can be extracted from `client.crt`:
```
$ openssl pkey -in client.crt -pubin -outform DER | tail -c 32 | od -An -tx1 | tr -d ' \n'
```

## Security

The tunnel uses X25519 Diffie-Hellman for key exchange and AES-256-GCM for encryption.

### Identity Hiding

Static public keys are encrypted inside the handshake so passive observers cannot identify peers:

- Client encrypts its static key with `SHA-256(DH(eph_c, server_static))` — requires knowing the server's public key upfront (enforced via `-C`)
- Server encrypts its static key with `SHA-256(DH(eph_s, eph_c))`

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

### Session Rekeying

The client automatically re-handshakes every 5 minutes. The server updates the session key in-place, preserving routes and client state.

### AllowedIPs Routing

The server enforces a per-client routing table (similar to WireGuard):

- **TUN→UDP**: destination IP is resolved by longest-prefix match across all client route tables
- **UDP→TUN**: source IP in incoming packets must fall within the client's AllowedIPs — mismatches are dropped
- At least one `-A <pubkey> -R <cidr>` pair is mandatory at server startup

### Replay Protection

Each session uses a per-direction 64-bit sequence counter. The receiver maintains a **2048-bit sliding window** (32 × 64-bit words) to detect and drop replayed or reordered packets.

### DoS Mitigations

- 5-second cooldown per address and per public key between handshakes
- Max-clients limit only applies to new public keys; rekeying an existing client does not consume a slot

### Data Packets

Each datagram on the wire is:
```
[12-byte IV][AES-256-GCM ciphertext of (8-byte magic + 8-byte seq + payload)][16-byte GCM tag]
```
Magic is `0xdeadbeefcafebabe`. Packets with a bad magic header, invalid GCM tag, or replayed sequence number are silently dropped.

## CLI Options

### Server
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
| `-K`   | Static private key PEM file (default: `client.key`) |
| `-E`   | Server public key hex — overrides `-C` |
| `-v`   | Verbose / debug logging |

## Configuring The Routing Table
The `create-xxx-tunnel.sh` scripts add routing table entries automatically.

Local machine:
```
$ ip route show
10.10.0.0/24 via 10.9.0.1 dev lanecove-udp
```

Remote machine:
```
$ ip route show
10.9.0.0/24 via 10.10.0.1 dev lanecove-udp
```

## Monitoring Tunnel Traffic
```
sudo apt install tshark
sudo tshark -i lanecove-udp
```

## Verifying
Ping a machine in the remote network, or `curl` the nginx server running on the server container:
```
curl http://10.10.0.1
```

## References
* https://www.kernel.org/doc/Documentation/networking/tuntap.txt
