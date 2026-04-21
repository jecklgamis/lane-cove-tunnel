## lane-cove-tunnel

A simple Linux IP tunnel using a TUN virtual interface over UDP. Implements a basic VPN for learning purposes.
Warning: not for production use.

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
  * Adds `lanecove-udp` virtual interface (`10.10.0.x/24`, derived from host IP)
  * Adds route to `10.9.0.0/24` network via `10.10.0.x`
  * Runs the server on port 5040

## Running With Docker

```
./run-server-in-docker.sh                        # build server image and run
SERVER_IP=<server-ip> ./run-client-in-docker.sh  # build client image and run
```
The client auto-detects the host IP from `en0` (falling back to `en1`) on Mac.

## Running Natively (Linux)

### Running The Server
* Create the tunnel:
```
$ ./create-server-tunnel.sh   # creates lanecove-udp interface, port 5040
```

* Run the server binary directly:
```
$ ./server -i lanecove-udp -p 5040 -k mysecret
```

### Running The Client
* Create the tunnel:
```
$ ./create-client-tunnel.sh   # creates lanecove-udp, 10.9.0.1/24
```

* Run the client binary directly:
```
$ ./client -i lanecove-udp -s <server-ip> -p 5040 -k mysecret   # authenticated + encrypted
$ ./client -i lanecove-udp -s <server-ip> -p 5040                # unauthenticated (MITM-vulnerable)
```

## Security

The tunnel uses X25519 Diffie-Hellman for key exchange and AES-256-GCM for encryption.

### Handshake
On connect (and reconnect), client and server perform an ephemeral X25519 DH exchange:
1. Client sends `[8-byte magic][32-byte X25519 pubkey][32-byte HMAC-SHA256(psk, pubkey)]`
2. Server verifies the HMAC, generates its own key pair, responds in the same format
3. Both sides derive the session key: `SHA-256(shared_secret || client_pub || server_pub)`

The PSK (`-k`) authenticates the handshake to prevent MITM. Without `-k`, the exchange still
encrypts traffic but is vulnerable to MITM attacks.

### Data packets
Each datagram on the wire is `[12-byte IV][ciphertext of (8-byte magic + payload)][16-byte GCM tag]`.
The magic header is authenticated by the GCM tag. Packets that fail tag verification are silently dropped.
Each reconnect produces a new session key (forward secrecy).

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
10.9.0.0/24 via 10.10.0.x dev lanecove-udp
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
