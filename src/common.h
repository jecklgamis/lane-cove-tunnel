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
#include <openssl/crypto.h>
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

static inline const char *log_timestamp(void) {
    static char buf[32];
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm_info);
    return buf;
}

#define LOG_ERROR(fmt, ...) fprintf(stderr, "%s [ERROR] " fmt "\n", log_timestamp(), ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  fprintf(stderr, "%s [WARN]  " fmt "\n", log_timestamp(), ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  fprintf(stderr, "%s [INFO]  " fmt "\n", log_timestamp(), ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) do { if (log_level > 0) fprintf(stderr, "%s [DEBUG] " fmt "\n", log_timestamp(), ##__VA_ARGS__); } while(0)

#define REPLAY_WINDOW_WORDS 32  /* 32 * 64 = 2048-bit sliding window */

/* 1024-bit sliding window replay protection. Returns 0 if new, -1 if duplicate/too old.
 * window[0] is the least-significant word: bit 0 = highest, bit k = highest-k. */
static inline int check_replay(uint64_t seq, uint64_t *highest, uint64_t *window) {
    if (seq > *highest) {
        uint64_t diff = seq - *highest;
        if (diff >= REPLAY_WINDOW_WORDS * 64) {
            memset(window, 0, REPLAY_WINDOW_WORDS * sizeof(uint64_t));
        } else {
            int wn = (int)(diff / 64);
            int bn = (int)(diff % 64);
            for (int i = REPLAY_WINDOW_WORDS - 1; i >= 0; i--) {
                int lo_src = i - wn;
                int hi_src = lo_src - 1;
                uint64_t lo = lo_src >= 0 ? window[lo_src] : 0;
                uint64_t hi = (bn > 0 && hi_src >= 0) ? window[hi_src] : 0;
                window[i] = bn > 0 ? ((lo << bn) | (hi >> (64 - bn))) : lo;
            }
        }
        window[0] |= 1ULL;
        *highest = seq;
        return 0;
    }
    uint64_t diff = *highest - seq;
    if (diff >= REPLAY_WINDOW_WORDS * 64) return -1;
    int word = (int)(diff / 64);
    int bit  = (int)(diff % 64);
    if (window[word] & (1ULL << bit)) return -1;
    window[word] |= (1ULL << bit);
    return 0;
}

int open_tunnel(char *tunnel);
void derive_key(const char *psk, unsigned char *key);
void bytes_to_hex(const unsigned char *bytes, int len, char *hex_out);
int hex_to_bytes(const char *hex, unsigned char *bytes_out, int expected_len);
int generate_eph_keypair(EVP_PKEY **pkey_out, unsigned char *pub_out);
int load_or_generate_static_key(const char *path, EVP_PKEY **pkey_out, unsigned char *pub_out);
int load_static_key(const char *path, EVP_PKEY **pkey_out, unsigned char *pub_out);
int load_public_key(const char *path, unsigned char *pub_out);
int encrypt_packet(EVP_CIPHER_CTX *ctx, const unsigned char *key,
                   const unsigned char *plain, int plain_len,
                   unsigned char *out, int *out_len);
int decrypt_packet(EVP_CIPHER_CTX *ctx, const unsigned char *key,
                   const unsigned char *in, int in_len,
                   unsigned char *out, int *out_len);

/*
 * Handshake wire format (both directions):
 *   [8 magic][32 eph_pub][48 AES-256-GCM(identity_key, static_pub)][32 HMAC-SHA256(psk, ...)]?
 *
 * Identity encryption (hides static public keys from passive observers):
 *   Client encrypts its static_pub with key = SHA-256(DH(eph_c, server_static))
 *   Server encrypts its static_pub with key = SHA-256(DH(eph_s, eph_c))
 *   AES-256-GCM uses a zero IV — safe because the key is derived from a fresh ephemeral DH each time.
 *
 * Session key: SHA-256(ECDH(eph_c,eph_s) || ECDH(static_c,eph_s) || ECDH(eph_c,static_s)
 *                      || client_eph_pub || server_eph_pub || client_static_pub || server_static_pub)
 *
 * Client must know server_static_pub (via -C) to encrypt its identity.
 * Server returns client_static_pub for allowlist check by caller.
 */
#define HS_ENCRYPTED_PUB_LEN (DH_PUBKEY_LEN + CRYPTO_TAG_LEN)  /* 32 + 16 = 48 */

/* State carried between the send and receive phases of an outbound handshake. */
typedef struct {
    EVP_PKEY      *eph_key;
    unsigned char  eph_pub[DH_PUBKEY_LEN];
    unsigned char  ecdh_ec_ss[DH_PUBKEY_LEN];
} hs_client_state_t;

/* Send the handshake initiation packet; fill state_out for use by handshake_client_recv. */
int handshake_client_send(int sock_fd, struct sockaddr_in *server_addr,
                          const unsigned char *psk_key,
                          EVP_PKEY *static_key, const unsigned char *static_pub,
                          const unsigned char *server_static_pub,
                          hs_client_state_t *state_out);

/* Complete the handshake given the server's response; frees state->eph_key on success. */
int handshake_client_recv(const unsigned char *resp, int resp_len,
                          const unsigned char *psk_key,
                          EVP_PKEY *static_key, const unsigned char *static_pub,
                          const unsigned char *server_static_pub,
                          hs_client_state_t *state,
                          unsigned char *session_key_out);

int handshake_server_respond(int sock_fd, const unsigned char *pkt, int pkt_len,
                             struct sockaddr_in *peer_addr,
                             const unsigned char *psk_key,
                             EVP_PKEY *static_key, const unsigned char *static_pub,
                             EVP_PKEY *precomp_eph_key, const unsigned char *precomp_eph_pub,
                             unsigned char *client_static_pub_out,
                             unsigned char *session_key);

#endif //UDP_COMMON_H
