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
