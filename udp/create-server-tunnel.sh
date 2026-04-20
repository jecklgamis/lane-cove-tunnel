#!/usr/bin/env bash
TUNNEL_NAME=${TUNNEL_NAME:-lanecove-udp}
HOST_IP=$(ip route get 1.1.1.1 | grep -oP 'src \K[\d.]+')
LAST_OCTET=$(echo "$HOST_IP" | cut -d. -f4)
IP_ADDRESS="10.10.0.${LAST_OCTET}"
IP_ADDRESS_CIDR="${IP_ADDRESS}/24"
REMOTE_SUBNET="10.9.0.0/24"

echo "Host IP is $HOST_IP"
echo "Overlay network ip is $IP_ADDRESS"
echo "Remote subnet is $REMOTE_SUBNET"

ip tuntap del "$TUNNEL_NAME" mode tun
ip tuntap add "$TUNNEL_NAME" mode tun && echo "Created tunnel $TUNNEL_NAME"
ip link set "$TUNNEL_NAME" up && echo "$TUNNEL_NAME tunnel up"
ip addr add ${IP_ADDRESS} dev "$TUNNEL_NAME" && echo "Assigned $IP_ADDRESS_CIDR to $TUNNEL_NAME device"
ip route add $REMOTE_SUBNET via $IP_ADDRESS && echo "Added route to $REMOTE_SUBNET network via $IP_ADDRESS"
ifconfig $TUNNEL_NAME
ip route show

echo "Done. Test connectivity using curl http://$IP_ADDRESS to reach nginx"

echo "TUNNEL_NAME = $TUNNEL_NAME"  > runtime-server-info.txt
echo "OVERLAY_NETWORK_IP = $IP_ADDRESS"  >> runtime-server-info.txt
echo "REMOTE_SUBNET = $IP_ADDRESS"  >> runtime-server-info.txt