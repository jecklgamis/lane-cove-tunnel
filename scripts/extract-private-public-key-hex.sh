#!/usr/bin/env bash
# Print the private and public key hex from a .key (X25519 private key PEM) file.
# Usage: show-key-hex.sh <file.key>
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "Usage: $0 <file.key>" >&2
    exit 1
fi

OPENSSL=openssl
if [[ "$(uname)" == "Darwin" ]]; then
    BREW_OPENSSL="$(brew --prefix openssl 2>/dev/null)/bin/openssl"
    [[ -x "$BREW_OPENSSL" ]] && OPENSSL="$BREW_OPENSSL"
fi

priv=$("$OPENSSL" pkey -in "$1" -outform DER | tail -c 32 | od -An -tx1 | tr -d ' \n')
pub=$("$OPENSSL" pkey -in "$1" -pubout -outform DER | tail -c 32 | od -An -tx1 | tr -d ' \n')

echo "private: $priv"
echo "public:  $pub"
