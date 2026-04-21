default:
	cat ./Makefile
all:
	gcc -o server server.c common.c -lssl -lcrypto
	gcc -o client client.c common.c -lssl -lcrypto
	chmod +x server
	chmod +x client
clean:
	rm -f ./server
	rm -f ./client
