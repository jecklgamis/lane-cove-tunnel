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
#include <sys/select.h>
#include <sys/time.h>
#include <errno.h>
#include <stdarg.h>
#include <logmoko.h>
#include <poll.h>

#ifndef TCP_COMMON_H
#define TCP_COMMON_H

#define BUFFER_SIZE 2048

int open_tunnel(char *tunnel);

void event_loop(lmk_logger *logger, int tun_fd, int sock_fd);

#endif //TCP_COMMON_H
