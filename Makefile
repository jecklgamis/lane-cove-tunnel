default:
	cat ./Makefile
all:
	gcc -o tcp_server tcp_server.c tcp_common.c
	gcc -o tcp_client tcp_client.c tcp_common.c
	chmod +x tcp_server
	chmod +x tcp_client
clean:
	rm -f ./tcp_server
	rm -f ./tcp_client
server-image:
	docker build -f Dockerfile.server -t lane-cove-tunnel-server:latest .
client-image:
	docker build -f Dockerfile.client -t lane-cove-tunnel-client:latest .
image: server-image client-image
run-server:
	./run-tcp-server-in-docker.sh
run-client:
	./run-tcp-client-in-docker.sh
run-shell-server:
	docker run --privileged -it lane-cove-tunnel-server:latest /bin/bash
run-shell-client:
	docker run --privileged -it lane-cove-tunnel-client:latest /bin/bash
