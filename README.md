## lane-cove-tunnel

A simple Linux IP tunnel using tun/tap virtual interface. This implements a simple (insecure!) VPN network.
Warning: this is not secure and should only be used for learning purposes.

## Requirements
* Linux (tested using Ubuntu 18.04 LTS), gcc, make, iproute2

```
$ sudo apt install gcc make iproute2
```

## Building
```
$ make all
```

## Example Setup
```
10.9.0.0/24 network                                        10.10.0.0/24 network
...                                                        ...
[transport: segment/packet]   <---- tunnel using TCP --->  [transport: segment/packet]
[network: ip packet]                                       [network: ip packet]
[datalink: ethernet frame ]                                [datalink: ethernet frame ]
[physical: bits]                                           [physical: bits]
```
* Local network : 10.9.0.0/24
* Remote network : 10.10.0.0/24
* Local machine :
  * Adds `lanecove` virtual interface (`10.9.0.1/24`)
  * Adds route to `10.10.0.0/24` network via `10.9.0.1`
  * Runs the TCP client and connects to remote TCP server on port 5050
* Remote machine :
  * Adds `lanecove` virtual interface (`10.10.0.x/24`, derived from host IP)
  * Adds route to `10.9.0.0/24` network via `10.10.0.x`
  * Runs the TCP server on port 5050

## Running With Docker

### Build Images
```
make server-image
make client-image
```

### Run Server
```
make run-server
```
Or directly:
```
./run-tcp-server-in-docker.sh
```

### Run Client
```
make run-client
```
Or directly:
```
SERVER_IP=<server-ip> ./run-tcp-client-in-docker.sh
```
The client auto-detects the host IP from `en0` (falling back to `en1`) on Mac.

## Running Natively (Linux)

### Running The Server
* Create the tunnel:
```
$ ./create-server-tunnel.sh
```
This creates a tunnel named `lanecove` with an IP derived from the host's current network interface.

* Run the server:
```
$ ./run-tcp-server.sh
```
This will start nginx and a TCP server on port 5050 bound to `0.0.0.0`.

### Running The Client
* Create the tunnel:
```
$ ./create-client-tunnel.sh
```
This creates a tunnel named `lanecove` and assigns it `10.9.0.1/24`.

* Run the client:
```
$ SERVER_IP=<server-ip> ./run-tcp-client.sh
```

## Configuring The Routing Table
In this setup, we created `10.9.0.0/24` (local) and `10.10.0.0/24` (remote) networks. The `create-xxx-tunnel.sh`
scripts also add routing table entries to the peer network.

Local machine:
```
$ ip route show
10.10.0.0/24 via 10.9.0.1 dev lanecove
```

Remote machine:
```
$ ip route show
10.9.0.0/24 via 10.10.0.x dev lanecove
```

## Monitoring Tunnel Traffic
You can use `tshark` or `tcpdump` to monitor traffic on the virtual interface.

```
sudo apt install tshark
sudo tshark -i lanecove
```

## Verifying
Ping a machine in the remote network, or `curl` the nginx server running on the server container:
```
curl http://10.10.0.1
```

## References
* https://www.kernel.org/doc/Documentation/networking/tuntap.txt
