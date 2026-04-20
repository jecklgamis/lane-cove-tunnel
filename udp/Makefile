default:
	cat ./Makefile
all:
	gcc -o udp_server udp_server.c udp_common.c -lssl -lcrypto
	gcc -o udp_client udp_client.c udp_common.c -lssl -lcrypto
	chmod +x udp_server
	chmod +x udp_client
clean:
	rm -f ./udp_server
	rm -f ./udp_client
