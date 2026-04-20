#include "udp_common.h"

int log_level = 0;

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
