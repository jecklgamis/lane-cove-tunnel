Name:           lanecove-tunnel
Version:        %{pkg_version}
Release:        %{pkg_release}%{?dist}
Summary:        Hub-and-spoke layer 3 overlay network over UDP
License:        MIT
URL:            https://github.com/jecklgamis/lanecove-tunnel

Requires:       iproute, openssl-libs

%description
A simple Linux VPN using a TUN virtual interface and UDP transport.
Implements X25519 key exchange, AES-256-GCM encryption, identity hiding,
AllowedIPs routing, replay protection, and session rekeying.

%install
mkdir -p %{buildroot}/usr/bin
mkdir -p %{buildroot}/usr/share/lanecove-tunnel
mkdir -p %{buildroot}/usr/lib/systemd/system
mkdir -p %{buildroot}/etc/lanecove

install -m 755 %{_sourcedir}/lanecove                      %{buildroot}/usr/bin/lanecove-peer
install -m 755 %{_sourcedir}/scripts/lanecove-create-tunnel.sh        %{buildroot}/usr/share/lanecove-tunnel/
install -m 755 %{_sourcedir}/scripts/lanecove-extract-keys-hex.sh    %{buildroot}/usr/bin/
install -m 755 %{_sourcedir}/scripts/lanecove-extract-pubkey-hex.sh  %{buildroot}/usr/bin/
install -m 755 %{_sourcedir}/scripts/lanecove-generate-peer-keys.sh  %{buildroot}/usr/bin/
install -m 644 %{_sourcedir}/rpm/systemd/lanecove-relay.service    %{buildroot}/usr/lib/systemd/system/
install -m 644 %{_sourcedir}/rpm/systemd/lanecove-peer.service      %{buildroot}/usr/lib/systemd/system/
install -m 644 %{_sourcedir}/rpm/systemd/lanecove-peer-1.service   %{buildroot}/usr/lib/systemd/system/
install -m 644 %{_sourcedir}/rpm/systemd/lanecove-peer-2.service   %{buildroot}/usr/lib/systemd/system/
install -m 640 %{_sourcedir}/config/relay.yaml   %{buildroot}/etc/lanecove/relay.yaml
install -m 640 %{_sourcedir}/config/peer.yaml    %{buildroot}/etc/lanecove/peer.yaml
install -m 640 %{_sourcedir}/config/peer-1.yaml  %{buildroot}/etc/lanecove/peer-1.yaml
install -m 640 %{_sourcedir}/config/peer-2.yaml  %{buildroot}/etc/lanecove/peer-2.yaml

%files
/usr/bin/lanecove-peer
/usr/share/lanecove-tunnel/lanecove-create-tunnel.sh
/usr/bin/lanecove-extract-keys-hex.sh
/usr/bin/lanecove-extract-pubkey-hex.sh
/usr/bin/lanecove-generate-peer-keys.sh
/usr/lib/systemd/system/lanecove-relay.service
/usr/lib/systemd/system/lanecove-peer.service
/usr/lib/systemd/system/lanecove-peer-1.service
/usr/lib/systemd/system/lanecove-peer-2.service
%config(noreplace) /etc/lanecove/relay.yaml
%config(noreplace) /etc/lanecove/peer.yaml
%config(noreplace) /etc/lanecove/peer-1.yaml
%config(noreplace) /etc/lanecove/peer-2.yaml

%post
systemctl daemon-reload
echo "Lane Cove Tunnel installed."
echo ""
echo "Edit configs in /etc/lanecove/, then enable the appropriate service:"
echo "  systemctl enable --now lanecove-relay    # on the relay machine"
echo "  systemctl enable --now lanecove-peer     # on a generic peer machine"
echo "  systemctl enable --now lanecove-peer-1   # on peer-1 machine"
echo "  systemctl enable --now lanecove-peer-2   # on peer-2 machine"

%preun
for svc in lanecove-relay lanecove-peer lanecove-peer-1 lanecove-peer-2; do
    if systemctl is-active --quiet "$svc" 2>/dev/null; then
        systemctl stop "$svc"
    fi
    if systemctl is-enabled --quiet "$svc" 2>/dev/null; then
        systemctl disable "$svc"
    fi
done

%postun
systemctl daemon-reload
