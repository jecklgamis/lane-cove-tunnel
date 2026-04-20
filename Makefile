default:
	cat ./Makefile
all:
	$(MAKE) -C tcp all
	$(MAKE) -C udp all
clean:
	$(MAKE) -C tcp clean
	$(MAKE) -C udp clean
