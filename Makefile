DOCKER_IMAGE:=lane-cove-tunnel-peer:latest

all:
	gcc -o peer src/peer.c src/common.c -lssl -lcrypto -lyaml
	chmod +x peer
image:
	docker build -f Dockerfile.peer -t $(DOCKER_IMAGE) .
run-shell:
	@docker run -it --entrypoint /bin/bash $(DOCKER_IMAGE)
run:
	@docker run --rm 	--name lane-cove-tunnel-peer -it $(DOCKER_IMAGE)
clean:
	rm -f ./peer
