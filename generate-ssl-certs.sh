#!/usr/bin/env bash
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

openssl genpkey -algorithm x25519 -out ${SERVER_KEY}
openssl pkey -in ${SERVER_KEY} -pubout -out ${SERVER_CERT}
echo "Wrote ${SERVER_KEY}"
echo "Wrote ${SERVER_CERT}"

openssl genpkey -algorithm x25519 -out ${CLIENT_KEY}
openssl pkey -in ${CLIENT_KEY} -pubout -out ${CLIENT_CERT}
echo "Wrote ${CLIENT_KEY}"
echo "Wrote ${CLIENT_CERT}"
