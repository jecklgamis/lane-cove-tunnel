#!/usr/bin/env bash
# macOS ships LibreSSL which lacks x25519; prefer Homebrew OpenSSL if available
OPENSSL=openssl
if [[ "$(uname)" == "Darwin" ]]; then
    BREW_OPENSSL="$(brew --prefix openssl 2>/dev/null)/bin/openssl"
    [[ -x "$BREW_OPENSSL" ]] && OPENSSL="$BREW_OPENSSL"
fi

SERVER_KEY=server.key
SERVER_CERT=server.crt
CLIENT_KEY=client.key
CLIENT_CERT=client.crt

existing=()
for f in ${SERVER_KEY} ${SERVER_CERT} ${CLIENT_KEY} ${CLIENT_CERT}; do
    [[ -f $f ]] && existing+=("$f")
done

if [[ ${#existing[@]} -gt 0 ]]; then
    echo "The following file(s) already exist: ${existing[*]}"
    read -r -p "Overwrite? [y/N] " answer
    if [[ ! ${answer} =~ ^[Yy]$ ]]; then
        echo "Aborted."
        exit 1
    fi
    rm -f ${SERVER_KEY} ${SERVER_CERT} ${CLIENT_KEY} ${CLIENT_CERT}
fi

${OPENSSL} genpkey -algorithm x25519 -out ${SERVER_KEY}
${OPENSSL} pkey -in ${SERVER_KEY} -pubout -out ${SERVER_CERT}
echo "Wrote ${SERVER_KEY}"
echo "Wrote ${SERVER_CERT}"

${OPENSSL} genpkey -algorithm x25519 -out ${CLIENT_KEY}
${OPENSSL} pkey -in ${CLIENT_KEY} -pubout -out ${CLIENT_CERT}
echo "Wrote ${CLIENT_KEY}"
echo "Wrote ${CLIENT_CERT}"
