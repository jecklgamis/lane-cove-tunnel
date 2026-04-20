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

#ifndef UDP_COMMON_H
#define UDP_COMMON_H

#define BUFFER_SIZE 2048

extern int log_level;

#define LOG_ERROR(fmt, ...) fprintf(stderr, "[ERROR] " fmt "\n", ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  fprintf(stderr, "[WARN]  " fmt "\n", ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  fprintf(stderr, "[INFO]  " fmt "\n", ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) do { if (log_level > 0) fprintf(stderr, "[DEBUG] " fmt "\n", ##__VA_ARGS__); } while(0)

int open_tunnel(char *tunnel);

#endif //UDP_COMMON_H
