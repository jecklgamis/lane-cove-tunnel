#!/usr/bin/env bash
# Shared helpers sourced by run scripts.

# Resolve openssl binary (use Homebrew openssl on macOS if available)
OPENSSL=openssl
if [[ "$(uname)" == "Darwin" ]]; then
    BREW_OPENSSL="$(brew --prefix openssl 2>/dev/null)/bin/openssl"
    [[ -x "$BREW_OPENSSL" ]] && OPENSSL="$BREW_OPENSSL"
fi

# Extract X25519 public key hex from a PEM public key file
extract_pub() { ${OPENSSL} pkey -in "$1" -pubin -outform DER | tail -c 32 | od -An -tx1 | tr -d ' \n'; }

# Detect the local outbound IP address (macOS and Linux)
detect_local_ip() {
    local ip=""
    if [[ "$(uname)" == "Darwin" ]]; then
        ip=$(ipconfig getifaddr en0 2>/dev/null || true)
        [[ -z "$ip" ]] && ip=$(ipconfig getifaddr en1 2>/dev/null || true)
    else
        ip=$(ip route get 1.1.1.1 2>/dev/null | grep -oP 'src \K\S+' || true)
    fi
    echo "$ip"
}
