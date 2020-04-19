#include "tcp_common.h"

lmk_logger *logger;

int alloc_tunnel(char *dev, int flags) {
    struct ifreq ifr;
    int tun_fd, ret_val;

    if ((tun_fd = open("/dev/net/tun", O_RDWR)) < 0) {
        LMK_LOG_ERROR(logger, "Unable to open tunnel : %s", strerror(errno));
        return tun_fd;
    }
    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = flags;
    if (*dev)
        strncpy(ifr.ifr_name, dev, IFNAMSIZ);
    if ((ret_val = ioctl(tun_fd, TUNSETIFF, (void *) &ifr)) < 0) {
        LMK_LOG_ERROR(logger, "Unable to issue ioctl on %d : %s", tun_fd, strerror(errno));
        close(tun_fd);
        return ret_val;
    }
    strcpy(dev, ifr.ifr_name);
    return tun_fd;
}

int open_tunnel(char *tunnel) {
    int tun_fd;
    if ((tun_fd = alloc_tunnel(tunnel, IFF_TUN | IFF_NO_PI)) < 0) {
        LMK_LOG_ERROR(logger, "Unable to connect to tunnel %s", tunnel);
        return tun_fd;
    }
    LMK_LOG_INFO(logger, "Opened tunnel %s", tunnel);
    return tun_fd;
}

int read_nr_bytes(int fd, char *buffer, int n) {
    int nr_read, left = n;
    while (left > 0) {
        if ((nr_read = read(fd, buffer, left)) < 0) {
            exit(-2);
            return 0;
        } else {
            left -= nr_read;
            buffer += nr_read;
        }
    }
    return n;
}


void event_loop(lmk_logger *logger, int tun_fd, int sock_fd) {
    char buffer[BUFFER_SIZE];
    int ret_val;
    ssize_t nr_read, nr_written, packet_len;

    struct pollfd fds[4];
    int nr_fds = 0;

    fds[nr_fds].fd = tun_fd;
    fds[nr_fds].events = POLLIN;
    nr_fds++;

    fds[nr_fds].fd = sock_fd;
    fds[nr_fds].events = POLLIN;
    int client_sock_fd_index = nr_fds;
    nr_fds++;

    int terminate_loop = 0;

    LMK_LOG_INFO(logger, "nr_fds = %d", nr_fds);
    while (!terminate_loop) {
        int timeout_msecs = 500;
        ret_val = poll(fds, nr_fds, timeout_msecs);
        if (ret_val < 0) {
            LMK_LOG_ERROR(logger, "poll() failed : %s", strerror(errno));
            break;
        }
        for (int i = 0; i < nr_fds; i++) {
            if (fds[i].revents == 0)
                continue;
            if (fds[i].revents != POLLIN) {
                LMK_LOG_ERROR(logger, "Got invalid event on fd = %d", fds[i].fd);
                terminate_loop = 1;
                break;
            }
            if (fds[i].fd == tun_fd) {
                nr_read = read(tun_fd, buffer, BUFFER_SIZE);
                if (nr_read <= 0) {
                    LMK_LOG_ERROR(logger, "Failed reading from tunnel %d : %s", fds[i].fd, strerror(errno));
                    terminate_loop = 1;
                    break;
                } else {
                    packet_len = htons(nr_read);
                    nr_written = write(fds[client_sock_fd_index].fd, (char *) &packet_len, sizeof(packet_len));
                    if (nr_written <= 0) {
                        LMK_LOG_ERROR(logger, "Failed writing to socket %d :%s ", fds[i].fd, strerror(errno));
                        terminate_loop = 1;
                        break;
                    }
                    nr_written = write(fds[client_sock_fd_index].fd, buffer, nr_read);
                    if (nr_written <= 0) {
                        LMK_LOG_ERROR(logger, "Failed writing to socket %d :%s ", fds[i].fd, strerror(errno));
                        terminate_loop = 1;
                        break;
                    }
                    LMK_LOG_DEBUG(logger, "TUN -> TCP: Wrote %d bytes", nr_written);
                }
            } else {
                int close_connection = 0;
                LMK_LOG_DEBUG(logger, "nr_fds = %d, fd = %d", nr_fds, fds[i].fd);
                nr_read = read_nr_bytes(fds[i].fd, (char *) &packet_len, sizeof(packet_len));
                if (nr_read <= 0) {
                    LMK_LOG_ERROR(logger, "Failed reading socket %d :%s", fds[i].fd, strerror(errno));
                    close_connection = 1;
                } else {
                    nr_read = read_nr_bytes(fds[i].fd, buffer, ntohs(packet_len));
                    if (nr_read <= 0) {
                        LMK_LOG_ERROR(logger, "Failed reading socket %d :%s", fds[i].fd, strerror(errno));
                        close_connection = 1;
                    } else {
                        nr_written = write(tun_fd, buffer, nr_read);
                        if (nr_written <= 0) {
                            LMK_LOG_ERROR(logger, "Failed writing to tunnel %d : %s", tun_fd, strerror(errno));
                            close_connection = 1;
                        } else if (nr_written == nr_read) {
                            LMK_LOG_DEBUG(logger, "TCP -> TUN: Wrote %d bytes", nr_written);
                        }
                    }
                }
                if (close_connection) {
                    LMK_LOG_WARN(logger, "Closing socket %d", fds[i].fd);
                    close(fds[i].fd);
                    fds[i].fd = -1;
                    break;
                }
            }
        }
    }
    LMK_LOG_INFO(logger, "poll() loop terminated");
}


