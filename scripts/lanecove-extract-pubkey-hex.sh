#!/usr/bin/env bash
# Print the X25519 public key from a .crt (public key PEM) file as hex.
# Usage: lanecove-extract-pubkey-hex.sh <file.crt>
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "Usage: $0 <file.crt>" >&2
    exit 1
fi

OPENSSL=openssl
if [[ "$(uname)" == "Darwin" ]]; then
    BREW_OPENSSL="$(brew --prefix openssl 2>/dev/null)/bin/openssl"
    [[ -x "$BREW_OPENSSL" ]] && OPENSSL="$BREW_OPENSSL"
fi

"$OPENSSL" pkey -in "$1" -pubin -outform DER | tail -c 32 | od -An -tx1 | tr -d ' \n'
echo
