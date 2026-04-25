RCUNIT_DIR ?= /Users/jeck/workspace/rcunit

all:
	gcc -o peer src/peer.c src/common.c -lssl -lcrypto
	chmod +x peer
image:
	docker build -f Dockerfile.peer -t lane-cove-tunnel-peer:latest .
test:
	docker run --rm \
		-v "$(PWD)":/src \
		-v "$(RCUNIT_DIR)":/rcunit \
		-w /src \
		debian:bookworm-slim \
		sh -c "apt-get update -qq && apt-get install -y -qq gcc libssl-dev >/dev/null 2>&1 && \
		       gcc -o tests/test_common \
		           tests/test_common.c src/common.c /rcunit/src/*.c \
		           -Isrc -I/rcunit/src \
		           -lssl -lcrypto -lpthread && \
		       ./tests/test_common"
clean:
	rm -f ./peer tests/test_common *.key *.crt
