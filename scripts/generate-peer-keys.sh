#!/usr/bin/env bash
# Usage: ./generate-peer-keys.sh <name> [name2 ...]
# Generates <name>.key (private) and <name>.crt (public) for each name.

OPENSSL=openssl
if [[ "$(uname)" == "Darwin" ]]; then
    BREW_OPENSSL="$(brew --prefix openssl 2>/dev/null)/bin/openssl"
    [[ -x "$BREW_OPENSSL" ]] && OPENSSL="$BREW_OPENSSL"
fi

if [[ $# -eq 0 ]]; then
    echo "Usage: $0 <name> [name2 ...]"
    echo "Example: $0 peer-1 peer-2 relay"
    exit 1
fi

for name in "$@"; do
    KEY="${name}.key"
    CRT="${name}.crt"
    existing=()
    [[ -f "$KEY" ]] && existing+=("$KEY")
    [[ -f "$CRT" ]] && existing+=("$CRT")
    if [[ ${#existing[@]} -gt 0 ]]; then
        echo "Existing files for ${name}: ${existing[*]}"
        read -r -p "Overwrite? [y/N] " answer
        [[ "$answer" != "y" && "$answer" != "Y" ]] && echo "Skipping ${name}." && continue
        rm -f "$KEY" "$CRT"
    fi
    ${OPENSSL} genpkey -algorithm x25519 -out "$KEY"
    ${OPENSSL} pkey -in "$KEY" -pubout -out "$CRT"
    echo "Wrote ${KEY}"
    echo "Wrote ${CRT}"
done
