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
#include <openssl/rand.h>
#include <openssl/sha.h>

#ifndef UDP_COMMON_H
#define UDP_COMMON_H

#define BUFFER_SIZE     2048
#define CRYPTO_KEY_LEN  32
#define CRYPTO_IV_LEN   12
#define CRYPTO_TAG_LEN  16
#define CRYPTO_OVERHEAD (CRYPTO_IV_LEN + CRYPTO_TAG_LEN)

extern int log_level;

#define LOG_ERROR(fmt, ...) fprintf(stderr, "[ERROR] " fmt "\n", ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  fprintf(stderr, "[WARN]  " fmt "\n", ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  fprintf(stderr, "[INFO]  " fmt "\n", ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) do { if (log_level > 0) fprintf(stderr, "[DEBUG] " fmt "\n", ##__VA_ARGS__); } while(0)

int open_tunnel(char *tunnel);
void derive_key(const char *psk, unsigned char *key);
int encrypt_packet(const unsigned char *key, const unsigned char *plain, int plain_len,
                   unsigned char *out, int *out_len);
int decrypt_packet(const unsigned char *key, const unsigned char *in, int in_len,
                   unsigned char *out, int *out_len);

#endif //UDP_COMMON_H
