DOCKER_IMAGE:=lane-cove-tunnel-peer:latest
DEB_VERSION:=1.0.0
DEB_PKG:=lanecove-tunnel_$(DEB_VERSION)_amd64
DEB_ROOT:=build/$(DEB_PKG)

all:
	gcc -o peer src/peer.c src/common.c -lssl -lcrypto -lyaml
	chmod +x peer
image:
	docker build -f Dockerfile.peer -t $(DOCKER_IMAGE) .
run-shell:
	@docker run -it --entrypoint /bin/bash $(DOCKER_IMAGE)
run:
	@docker run --rm 	--name lane-cove-tunnel-peer -it $(DOCKER_IMAGE)
deb: all
	rm -rf $(DEB_ROOT)
	mkdir -p $(DEB_ROOT)/DEBIAN
	mkdir -p $(DEB_ROOT)/usr/bin
	mkdir -p $(DEB_ROOT)/usr/share/lanecove-tunnel
	mkdir -p $(DEB_ROOT)/lib/systemd/system
	mkdir -p $(DEB_ROOT)/etc/lanecove
	install -m 755 peer $(DEB_ROOT)/usr/bin/lanecove-peer
	install -m 755 scripts/create-peer-tunnel.sh $(DEB_ROOT)/usr/share/lanecove-tunnel/
	install -m 644 debian/systemd/lanecove-relay.service $(DEB_ROOT)/lib/systemd/system/
	install -m 644 debian/systemd/lanecove-peer-1.service $(DEB_ROOT)/lib/systemd/system/
	install -m 644 debian/systemd/lanecove-peer-2.service $(DEB_ROOT)/lib/systemd/system/
	install -m 640 config/relay.yaml $(DEB_ROOT)/etc/lanecove/relay.yaml
	install -m 640 config/peer-1.yaml $(DEB_ROOT)/etc/lanecove/peer-1.yaml
	install -m 640 config/peer-2.yaml $(DEB_ROOT)/etc/lanecove/peer-2.yaml
	install -m 644 debian/control $(DEB_ROOT)/DEBIAN/control
	install -m 755 debian/postinst $(DEB_ROOT)/DEBIAN/postinst
	install -m 755 debian/prerm $(DEB_ROOT)/DEBIAN/prerm
	dpkg-deb --build $(DEB_ROOT) build/$(DEB_PKG).deb
	@echo "Package built: build/$(DEB_PKG).deb"
clean:
	rm -f ./peer
	rm -rf build/
