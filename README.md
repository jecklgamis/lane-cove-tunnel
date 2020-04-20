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

Example Setup
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
  * Adds `tun2` virtual interface (`10.9.0.1/24`)
  * Adds route to `10.10.0.0/24` network via `10.9.0.1`
  * Runs the TCP client and connects to remote TCP server on port 5050 
* Remote machine : 
  * Adds `tun2` virtual interface (`10.10.0.1/24`)
  * Adds route to `10.9.0.0/24` network via `10.10.0.1`
  * Runs the TCP server on port 5050  (use Internet routable ip if you can)

## Running The Server
* Create the tunnel:
```
$ ./create-server-tunnel.sh
```
This creates a tunnel named `tun2` and assign it an ip `10.10.0.1/24`.

Example output:
```
$ ./create-server-tunnel.sh 
+ TUNNEL_NAME=tun2
+ IP_ADDRESS=10.10.0.1/24
+ sudo ip tuntap del tun2 mode tun
+ sudo ip tuntap add tun2 mode tun
+ sudo ip link set tun2 up
+ sudo ip addr add 10.10.0.1/24 dev tun2
+ echo 'Creating tunnel tun2 with ip address 10.10.0.1/24'
Creating tunnel tun2 with ip address 10.10.0.1/24
+ sudo ip route add 10.9.0.0/24 via 10.10.0.1
+ echo 'Added route to 10.9.0.0/24 network via tun2'
Added route to 10.9.0.0/24 network via tun2
```

* Run the server:
```
./run-tcp-server.sh
```
This will start a TCP server on port 5050 bound to `0.0.0.0` address.

## Running The Client
* Create the tunnel:
```
./create-client-tunnel.sh
```
This creates a tunnel named `tun2` and assign it an ip `10.9.0.1/24`.

Example output:
```
$ ./create-client-tunnel.sh 
+ TUNNEL_NAME=tun2
+ IP_ADDRESS=10.9.0.1/24
+ sudo ip tuntap del tun2 mode tun
+ sudo ip tuntap add tun2 mode tun
+ sudo ip link set tun2 up
+ sudo ip addr add 10.9.0.1/24 dev tun2
+ echo 'Created tunnel tun2 with ip address 10.9.0.1/24'
Created tunnel tun2 with ip address 10.9.0.1/24
+ sudo ip route add 10.10.0.0/24 via 10.9.0.1
+ echo 'Created route to 10.10.0.0/24 network via tun2'
Created route to 10.10.0.0/24 network via tun2
```
* Run the client:
```
export SERVER_IP=<some-server-ip>
$ ./run-tcp-client.sh
```
Example output:
```
$ ./run-tcp-client.sh 
+ TUNNEL_NAME=tun2
+ SERVER_IP=some-server-ip
+ SERVER_PORT=5050
+ ./tcp_client -i tun2 -s some-server-ip -p 5050 -v
[INFO  Mon Apr 20 19:59:05 2020 (tcp_common.c:32)] : Opened tunnel tun2
[INFO  Mon Apr 20 19:59:05 2020 (tcp_client.c:37)] : Connected to some-server-ip:5050
```

## Configuring The Routing Table
In this setup, we created `10.9.0.0/24`  (local) and `10.10.0.0/24` (remote) networks. The `create-xxx-tunnel.sh`
scripts also added routing table entries to the peer network.

Local machine:
```
$ ip route show
10.10.0.0/24 via 10.9.0.1 dev tun2
```

Remote:
```
$ ip route show
10.9.0.0/24 via 10.10.0.1 dev tun2
````

That's it!, if all goes well, tunnel is ready to take some traffic!

## Monitoring Tunnel Traffic
You can use `tshark` or `tcpdump` to monitor the traffic in the virtual interface.

Example using `tshark`:
```
sudo apt install tshark
sudo tshark -i tun2
``` 

## Verifying
You can ping a machine in the remote network (`10.10.0.0/24`), establish SSH connection or `curl` a running `nginx` (if you have one!)
```
curl http://10.10.0.1
```

Look ma!
```
<!DOCTYPE html>
<html>
<head>
<title>Welcome to nginx!</title>
<style>
    body {
        width: 35em;
        margin: 0 auto;
        font-family: Tahoma, Verdana, Arial, sans-serif;
    }
</style>
</head>
<body>
<h1>Welcome to nginx!</h1>
<p>If you see this page, the nginx web server is successfully installed and
working. Further configuration is required.</p>

<p>For online documentation and support please refer to
<a href="http://nginx.org/">nginx.org</a>.<br/>
Commercial support is available at
<a href="http://nginx.com/">nginx.com</a>.</p>

<p><em>Thank you for using nginx.</em></p>
</body>
</html>
```

## References
* https://www.kernel.org/doc/Documentation/networking/tuntap.txt
