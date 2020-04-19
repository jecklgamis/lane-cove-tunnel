## lane-cove-tunnel

A simple Linux IP tunnel using tun/tap virtual interface. This implements a simple (insecure!) VPN network. 

## Requirements
* Linux machine (tested using Ubuntu 18.04 LTS), gcc, make

## Building
```
make all
```
This generates two binaries, `tcp_server` and `tcp_client`.

## Running The Server
* Create the tunnel:
```
./create-server-tunnel.sh
```

* Run the server:
```
./run-tcp-server.sh
```

## Running The Client
* Create the tunnel:
```
./create-client-tunnel.sh
```

* Run the client:
```
./run-tcp-client.sh
```

## Monitoring Tunnel Traffic
```
apt install tshark
tshark -i tun2
``` 
## Testing The Tunnel
* Testing using `ping`

In the local machine:
```
ping 10.9.0.2
```
* Testing using `ssh`
```
ssh user@10.9.0.2
```


