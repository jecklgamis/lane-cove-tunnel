#include "udp_common.h"

int log_level = 0;

const unsigned char pkt_header[HEADER_SIZE] = {0xde, 0xad, 0xbe, 0xef, 0xca, 0xfe, 0xba, 0xbe};

void derive_key(const char *psk, unsigned char *key) {
    SHA256((const unsigned char *) psk, strlen(psk), key);
}

int encrypt_packet(const unsigned char *key, const unsigned char *plain, int plain_len,
                   unsigned char *out, int *out_len) {
    unsigned char iv[CRYPTO_IV_LEN];
    if (RAND_bytes(iv, CRYPTO_IV_LEN) != 1)
        return -1;

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return -1;

    int len, ciphertext_len;
    unsigned char *ciphertext = out + CRYPTO_IV_LEN;

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, key, iv) != 1) goto err;
    if (EVP_EncryptUpdate(ctx, ciphertext, &len, plain, plain_len) != 1) goto err;
    ciphertext_len = len;
    if (EVP_EncryptFinal_ex(ctx, ciphertext + len, &len) != 1) goto err;
    ciphertext_len += len;
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, CRYPTO_TAG_LEN,
                             out + CRYPTO_IV_LEN + ciphertext_len) != 1) goto err;

    memcpy(out, iv, CRYPTO_IV_LEN);
    *out_len = CRYPTO_IV_LEN + ciphertext_len + CRYPTO_TAG_LEN;
    EVP_CIPHER_CTX_free(ctx);
    return 0;
err:
    EVP_CIPHER_CTX_free(ctx);
    return -1;
}

int decrypt_packet(const unsigned char *key, const unsigned char *in, int in_len,
                   unsigned char *out, int *out_len) {
    if (in_len < CRYPTO_OVERHEAD)
        return -1;

    const unsigned char *iv = in;
    const unsigned char *ciphertext = in + CRYPTO_IV_LEN;
    int ciphertext_len = in_len - CRYPTO_OVERHEAD;
    unsigned char tag[CRYPTO_TAG_LEN];
    memcpy(tag, in + in_len - CRYPTO_TAG_LEN, CRYPTO_TAG_LEN);

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return -1;

    int len, plain_len;
    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, key, iv) != 1) goto err;
    if (EVP_DecryptUpdate(ctx, out, &len, ciphertext, ciphertext_len) != 1) goto err;
    plain_len = len;
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, CRYPTO_TAG_LEN, tag) != 1) goto err;
    if (EVP_DecryptFinal_ex(ctx, out + len, &len) != 1) goto err;
    plain_len += len;

    *out_len = plain_len;
    EVP_CIPHER_CTX_free(ctx);
    return 0;
err:
    EVP_CIPHER_CTX_free(ctx);
    return -1;
}

static int alloc_tunnel(char *dev, int flags) {
    struct ifreq ifr;
    int tun_fd, ret_val;

    if ((tun_fd = open("/dev/net/tun", O_RDWR)) < 0) {
        LOG_ERROR("Unable to open tunnel : %s", strerror(errno));
        return tun_fd;
    }
    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = flags;
    if (*dev)
        strncpy(ifr.ifr_name, dev, IFNAMSIZ);
    if ((ret_val = ioctl(tun_fd, TUNSETIFF, (void *) &ifr)) < 0) {
        LOG_ERROR("Unable to issue ioctl on %d : %s", tun_fd, strerror(errno));
        close(tun_fd);
        return ret_val;
    }
    strcpy(dev, ifr.ifr_name);
    return tun_fd;
}

int open_tunnel(char *tunnel) {
    int tun_fd;
    if ((tun_fd = alloc_tunnel(tunnel, IFF_TUN | IFF_NO_PI)) < 0) {
        LOG_ERROR("Unable to connect to tunnel %s", tunnel);
        return tun_fd;
    }
    LOG_INFO("Opened tunnel %s", tunnel);
    return tun_fd;
}

static int generate_x25519_keypair(EVP_PKEY **pkey_out, unsigned char *pub_out) {
    EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, NULL);
    if (!pctx) return -1;
    EVP_PKEY *pkey = NULL;
    if (EVP_PKEY_keygen_init(pctx) != 1 || EVP_PKEY_keygen(pctx, &pkey) != 1) {
        EVP_PKEY_CTX_free(pctx);
        return -1;
    }
    EVP_PKEY_CTX_free(pctx);
    size_t pub_len = DH_PUBKEY_LEN;
    if (EVP_PKEY_get_raw_public_key(pkey, pub_out, &pub_len) != 1) {
        EVP_PKEY_free(pkey);
        return -1;
    }
    *pkey_out = pkey;
    return 0;
}

static int x25519_shared_secret(EVP_PKEY *my_key, const unsigned char *peer_pub,
                                unsigned char *secret_out) {
    EVP_PKEY *peer_key = EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, NULL, peer_pub, DH_PUBKEY_LEN);
    if (!peer_key) return -1;
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new(my_key, NULL);
    if (!ctx) { EVP_PKEY_free(peer_key); return -1; }
    size_t secret_len = DH_PUBKEY_LEN;
    int rc = -1;
    if (EVP_PKEY_derive_init(ctx) == 1 &&
        EVP_PKEY_derive_set_peer(ctx, peer_key) == 1 &&
        EVP_PKEY_derive(ctx, secret_out, &secret_len) == 1)
        rc = 0;
    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(peer_key);
    return rc;
}

static void derive_session_key(const unsigned char *shared_secret,
                               const unsigned char *client_pub,
                               const unsigned char *server_pub,
                               unsigned char *session_key) {
    unsigned char input[DH_PUBKEY_LEN * 3];
    memcpy(input,                   shared_secret, DH_PUBKEY_LEN);
    memcpy(input + DH_PUBKEY_LEN,   client_pub,    DH_PUBKEY_LEN);
    memcpy(input + DH_PUBKEY_LEN*2, server_pub,    DH_PUBKEY_LEN);
    SHA256(input, sizeof(input), session_key);
}

static int compute_hmac(const unsigned char *psk_key, const unsigned char *pub,
                        unsigned char *hmac_out) {
    unsigned int hlen = HMAC_LEN;
    return HMAC(EVP_sha256(), psk_key, CRYPTO_KEY_LEN, pub, DH_PUBKEY_LEN,
                hmac_out, &hlen) ? 0 : -1;
}

int handshake_client(int sock_fd, struct sockaddr_in *server_addr,
                     const unsigned char *psk_key, unsigned char *session_key) {
    EVP_PKEY *my_key = NULL;
    unsigned char my_pub[DH_PUBKEY_LEN];
    if (generate_x25519_keypair(&my_key, my_pub) < 0) {
        LOG_ERROR("Failed to generate X25519 key pair");
        return -1;
    }

    unsigned char pkt[HEADER_SIZE + DH_PUBKEY_LEN + HMAC_LEN];
    int pkt_len = HEADER_SIZE + DH_PUBKEY_LEN;
    memcpy(pkt, pkt_header, HEADER_SIZE);
    memcpy(pkt + HEADER_SIZE, my_pub, DH_PUBKEY_LEN);
    if (psk_key) {
        if (compute_hmac(psk_key, my_pub, pkt + HEADER_SIZE + DH_PUBKEY_LEN) < 0) {
            LOG_ERROR("HMAC computation failed");
            EVP_PKEY_free(my_key);
            return -1;
        }
        pkt_len += HMAC_LEN;
    }
    if (sendto(sock_fd, pkt, pkt_len, 0,
               (struct sockaddr *) server_addr, sizeof(*server_addr)) < 0) {
        LOG_ERROR("sendto() handshake failed : %s", strerror(errno));
        EVP_PKEY_free(my_key);
        return -1;
    }

    unsigned char resp[HEADER_SIZE + DH_PUBKEY_LEN + HMAC_LEN];
    ssize_t nr = recvfrom(sock_fd, resp, sizeof(resp), 0, NULL, NULL);
    int expected = HEADER_SIZE + DH_PUBKEY_LEN + (psk_key ? HMAC_LEN : 0);
    if (nr != expected) {
        LOG_ERROR("Handshake response size mismatch: got %zd, expected %d", nr, expected);
        EVP_PKEY_free(my_key);
        return -1;
    }
    if (memcmp(resp, pkt_header, HEADER_SIZE) != 0) {
        LOG_ERROR("Handshake response has bad magic");
        EVP_PKEY_free(my_key);
        return -1;
    }
    unsigned char *server_pub = resp + HEADER_SIZE;
    if (psk_key) {
        unsigned char expected_hmac[HMAC_LEN];
        if (compute_hmac(psk_key, server_pub, expected_hmac) < 0 ||
            memcmp(resp + HEADER_SIZE + DH_PUBKEY_LEN, expected_hmac, HMAC_LEN) != 0) {
            LOG_ERROR("Handshake HMAC verification failed — possible MITM");
            EVP_PKEY_free(my_key);
            return -1;
        }
    }

    unsigned char shared_secret[DH_PUBKEY_LEN];
    if (x25519_shared_secret(my_key, server_pub, shared_secret) < 0) {
        LOG_ERROR("X25519 shared secret derivation failed");
        EVP_PKEY_free(my_key);
        return -1;
    }
    derive_session_key(shared_secret, my_pub, server_pub, session_key);
    EVP_PKEY_free(my_key);
    LOG_INFO("Handshake complete — session key established");
    return 0;
}

int handshake_server_respond(int sock_fd, const unsigned char *pkt, int pkt_len,
                             struct sockaddr_in *peer_addr,
                             const unsigned char *psk_key, unsigned char *session_key) {
    int expected = HEADER_SIZE + DH_PUBKEY_LEN + (psk_key ? HMAC_LEN : 0);
    if (pkt_len != expected) {
        LOG_ERROR("Client handshake size mismatch: got %d, expected %d", pkt_len, expected);
        return -1;
    }
    if (memcmp(pkt, pkt_header, HEADER_SIZE) != 0) {
        LOG_ERROR("Client handshake has bad magic");
        return -1;
    }
    const unsigned char *client_pub = pkt + HEADER_SIZE;
    if (psk_key) {
        unsigned char expected_hmac[HMAC_LEN];
        if (compute_hmac(psk_key, client_pub, expected_hmac) < 0 ||
            memcmp(pkt + HEADER_SIZE + DH_PUBKEY_LEN, expected_hmac, HMAC_LEN) != 0) {
            LOG_ERROR("Client handshake HMAC verification failed — rejecting");
            return -1;
        }
    }

    EVP_PKEY *my_key = NULL;
    unsigned char my_pub[DH_PUBKEY_LEN];
    if (generate_x25519_keypair(&my_key, my_pub) < 0) {
        LOG_ERROR("Failed to generate X25519 key pair");
        return -1;
    }
    unsigned char resp[HEADER_SIZE + DH_PUBKEY_LEN + HMAC_LEN];
    int resp_len = HEADER_SIZE + DH_PUBKEY_LEN;
    memcpy(resp, pkt_header, HEADER_SIZE);
    memcpy(resp + HEADER_SIZE, my_pub, DH_PUBKEY_LEN);
    if (psk_key) {
        if (compute_hmac(psk_key, my_pub, resp + HEADER_SIZE + DH_PUBKEY_LEN) < 0) {
            LOG_ERROR("HMAC computation failed");
            EVP_PKEY_free(my_key);
            return -1;
        }
        resp_len += HMAC_LEN;
    }
    if (sendto(sock_fd, resp, resp_len, 0,
               (struct sockaddr *) peer_addr, sizeof(*peer_addr)) < 0) {
        LOG_ERROR("sendto() handshake response failed : %s", strerror(errno));
        EVP_PKEY_free(my_key);
        return -1;
    }

    unsigned char shared_secret[DH_PUBKEY_LEN];
    if (x25519_shared_secret(my_key, client_pub, shared_secret) < 0) {
        LOG_ERROR("X25519 shared secret derivation failed");
        EVP_PKEY_free(my_key);
        return -1;
    }
    derive_session_key(shared_secret, client_pub, my_pub, session_key);
    EVP_PKEY_free(my_key);
    LOG_INFO("Handshake complete — session key established");
    return 0;
}

int handshake_server(int sock_fd, struct sockaddr_in *peer_addr,
                     const unsigned char *psk_key, unsigned char *session_key) {
    unsigned char pkt[HEADER_SIZE + DH_PUBKEY_LEN + HMAC_LEN];
    socklen_t peer_len = sizeof(*peer_addr);
    ssize_t nr = recvfrom(sock_fd, pkt, sizeof(pkt), 0,
                          (struct sockaddr *) peer_addr, &peer_len);
    if (nr < 0) {
        LOG_ERROR("recvfrom() failed : %s", strerror(errno));
        return -1;
    }
    LOG_INFO("Handshake from %s:%d", inet_ntoa(peer_addr->sin_addr), ntohs(peer_addr->sin_port));
    return handshake_server_respond(sock_fd, pkt, (int) nr, peer_addr, psk_key, session_key);
}
