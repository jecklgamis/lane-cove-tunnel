all:
	gcc -o peer src/peer.c src/common.c -lssl -lcrypto
	chmod +x peer
image:
	docker build -f Dockerfile.peer -t lane-cove-tunnel-peer:latest .
clean:
	rm -f ./peer *.key *.crt
