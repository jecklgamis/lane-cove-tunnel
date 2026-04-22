#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>
#include <linux/if_tun.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/epoll.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <endian.h>
#include <stdint.h>

#ifndef UDP_COMMON_H
#define UDP_COMMON_H

#define BUFFER_SIZE     2048
#define CRYPTO_KEY_LEN  32
#define CRYPTO_IV_LEN   12
#define CRYPTO_TAG_LEN  16
#define CRYPTO_OVERHEAD (CRYPTO_IV_LEN + CRYPTO_TAG_LEN)
#define HEADER_SIZE     8
#define SEQ_SIZE        8
#define WIRE_OVERHEAD   (HEADER_SIZE + SEQ_SIZE + CRYPTO_OVERHEAD)
#define DH_PUBKEY_LEN   32
#define HMAC_LEN        32

extern const unsigned char pkt_header[HEADER_SIZE];

extern int log_level;

#define LOG_ERROR(fmt, ...) fprintf(stderr, "[ERROR] " fmt "\n", ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  fprintf(stderr, "[WARN]  " fmt "\n", ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  fprintf(stderr, "[INFO]  " fmt "\n", ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) do { if (log_level > 0) fprintf(stderr, "[DEBUG] " fmt "\n", ##__VA_ARGS__); } while(0)

/* 64-bit sliding window replay protection. Returns 0 if new, -1 if duplicate/too old. */
static inline int check_replay(uint64_t seq, uint64_t *highest, uint64_t *window) {
    if (seq > *highest) {
        uint64_t diff = seq - *highest;
        *window = diff < 64 ? (*window << diff) | 1ULL : 1ULL;
        *highest = seq;
        return 0;
    }
    uint64_t diff = *highest - seq;
    if (diff >= 64) return -1;
    if (*window & (1ULL << diff)) return -1;
    *window |= (1ULL << diff);
    return 0;
}

int open_tunnel(char *tunnel);
void derive_key(const char *psk, unsigned char *key);
void bytes_to_hex(const unsigned char *bytes, int len, char *hex_out);
int hex_to_bytes(const char *hex, unsigned char *bytes_out, int expected_len);
int load_or_generate_static_key(const char *path, EVP_PKEY **pkey_out, unsigned char *pub_out);
int load_static_key(const char *path, EVP_PKEY **pkey_out, unsigned char *pub_out);
int load_public_key(const char *path, unsigned char *pub_out);
int encrypt_packet(const unsigned char *key, const unsigned char *plain, int plain_len,
                   unsigned char *out, int *out_len);
int decrypt_packet(const unsigned char *key, const unsigned char *in, int in_len,
                   unsigned char *out, int *out_len);

/*
 * Handshake wire format (both directions):
 *   [8 magic][32 eph_pub][32 static_pub][32 HMAC-SHA256(psk, eph_pub||static_pub)]?
 *
 * Session key: SHA-256(ECDH(eph_c,eph_s) || ECDH(static_c,eph_s) || ECDH(eph_c,static_s)
 *                      || client_eph_pub || server_eph_pub || client_static_pub || server_static_pub)
 *
 * Client verifies server_static_pub matches expected (if provided).
 * Server returns client_static_pub for allowlist check by caller.
 */
int handshake_client(int sock_fd, struct sockaddr_in *server_addr,
                     const unsigned char *psk_key,
                     EVP_PKEY *static_key, const unsigned char *static_pub,
                     const unsigned char *server_static_pub,
                     unsigned char *session_key);

int handshake_server_respond(int sock_fd, const unsigned char *pkt, int pkt_len,
                             struct sockaddr_in *peer_addr,
                             const unsigned char *psk_key,
                             EVP_PKEY *static_key, const unsigned char *static_pub,
                             unsigned char *client_static_pub_out,
                             unsigned char *session_key);

#endif //UDP_COMMON_H
