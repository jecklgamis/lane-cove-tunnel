default:
	cat ./Makefile
all:
	$(MAKE) -C udp all
clean:
	$(MAKE) -C udp clean
