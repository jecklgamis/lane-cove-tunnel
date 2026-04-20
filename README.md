## lane-cove-tunnel

A simple Linux IP tunnel using tun/tap virtual interface over TCP or UDP. This implements a simple (insecure!) VPN network.
Warning: this is not secure and should only be used for learning purposes.

## Requirements
* Linux (tested using Ubuntu 18.04 LTS), gcc, make, iproute2

```
$ sudo apt install gcc make iproute2
```

## Building
```
$ make all          # build all four binaries
$ make -C tcp all   # build tcp_server and tcp_client only
$ make -C udp all   # build udp_server and udp_client only
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
  * Runs the client and connects to the remote server on port 5050
* Remote machine :
  * Adds `lanecove` virtual interface (`10.10.0.x/24`, derived from host IP)
  * Adds route to `10.9.0.0/24` network via `10.10.0.x`
  * Runs the server on port 5050

## Running With Docker

### TCP

```
cd tcp
./run-tcp-server-in-docker.sh                        # build server image and run
SERVER_IP=<server-ip> ./run-tcp-client-in-docker.sh  # build client image and run
```
The client auto-detects the host IP from `en0` (falling back to `en1`) on Mac.

### UDP

```
cd udp
./run-udp-server-in-docker.sh                        # build server image and run
SERVER_IP=<server-ip> ./run-udp-client-in-docker.sh  # build client image and run
```

## Running Natively (Linux)

### Running The Server
* Create the tunnel:
```
$ ./tcp/create-server-tunnel.sh   # TCP (lanecove, port 5050)
$ ./udp/create-server-tunnel.sh   # UDP (lanecove-udp, port 5040)
```
Each script creates a named TUN interface with an IP derived from the host's network interface.

* Run the server binary directly:
```
$ ./tcp/tcp_server -i lanecove     -p 5050   # TCP
$ ./udp/udp_server -i lanecove-udp -p 5040   # UDP
```

### Running The Client
* Create the tunnel:
```
$ ./tcp/create-client-tunnel.sh   # TCP (lanecove, 10.9.0.1/24)
$ ./udp/create-client-tunnel.sh   # UDP (lanecove-udp, 10.9.0.1/24)
```

* Run the client binary directly:
```
$ ./tcp/tcp_client -i lanecove     -s <server-ip> -p 5050   # TCP
$ ./udp/udp_client -i lanecove-udp -s <server-ip> -p 5040   # UDP
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
