default:
	cat ./Makefile
all:
	gcc -o server src/server.c src/common.c src/rcunit_list.c -lssl -lcrypto
	gcc -o client src/client.c src/common.c -lssl -lcrypto
	chmod +x server
	chmod +x client
clean:
	rm -f ./server
	rm -f ./client
