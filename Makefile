default:
	cat ./Makefile
all:
	gcc -o peer src/peer.c src/common.c -lssl -lcrypto
	chmod +x peer
clean:
	rm -f ./peer
