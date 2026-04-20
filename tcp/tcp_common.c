#include "tcp_common.h"

int log_level = 0;

int alloc_tunnel(char *dev, int flags) {
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

int read_nr_bytes(int fd, char *buffer, int n) {
    int nr_read, left = n;
    while (left > 0) {
        if ((nr_read = read(fd, buffer, left)) < 0) {
            exit(-2);
        } else {
            left -= nr_read;
            buffer += nr_read;
        }
    }
    return n;
}

void event_loop(int tun_fd, int sock_fd) {
    char buffer[BUFFER_SIZE];
    ssize_t nr_read, nr_written, packet_len;
    int terminate_loop = 0;

    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        LOG_ERROR("epoll_create1() failed : %s", strerror(errno));
        return;
    }

    struct epoll_event ev, events[2];
    ev.events = EPOLLIN;
    ev.data.fd = tun_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, tun_fd, &ev) < 0) {
        LOG_ERROR("epoll_ctl() failed for tun_fd : %s", strerror(errno));
        close(epoll_fd);
        return;
    }
    ev.data.fd = sock_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sock_fd, &ev) < 0) {
        LOG_ERROR("epoll_ctl() failed for sock_fd : %s", strerror(errno));
        close(epoll_fd);
        return;
    }

    while (!terminate_loop) {
        int nfds = epoll_wait(epoll_fd, events, 2, 500);
        if (nfds < 0) {
            LOG_ERROR("epoll_wait() failed : %s", strerror(errno));
            break;
        }
        for (int i = 0; i < nfds; i++) {
            if (events[i].events & EPOLLERR) {
                LOG_ERROR("Got error event on fd = %d", events[i].data.fd);
                terminate_loop = 1;
                break;
            }
            if (events[i].data.fd == tun_fd) {
                nr_read = read(tun_fd, buffer, BUFFER_SIZE);
                if (nr_read <= 0) {
                    LOG_ERROR("Failed reading from tunnel %d : %s", tun_fd, strerror(errno));
                    terminate_loop = 1;
                    break;
                }
                packet_len = htons(nr_read);
                nr_written = write(sock_fd, (char *) &packet_len, sizeof(packet_len));
                if (nr_written <= 0) {
                    LOG_ERROR("Failed writing to socket %d : %s", sock_fd, strerror(errno));
                    terminate_loop = 1;
                    break;
                }
                nr_written = write(sock_fd, buffer, nr_read);
                if (nr_written <= 0) {
                    LOG_ERROR("Failed writing to socket %d : %s", sock_fd, strerror(errno));
                    terminate_loop = 1;
                    break;
                }
                LOG_DEBUG("TUN -> TCP: Wrote %zd bytes", nr_written);
            } else {
                int close_connection = 0;
                LOG_DEBUG("fd = %d", events[i].data.fd);
                nr_read = read_nr_bytes(sock_fd, (char *) &packet_len, sizeof(packet_len));
                if (nr_read <= 0) {
                    LOG_ERROR("Failed reading socket %d : %s", sock_fd, strerror(errno));
                    close_connection = 1;
                } else {
                    nr_read = read_nr_bytes(sock_fd, buffer, ntohs(packet_len));
                    if (nr_read <= 0) {
                        LOG_ERROR("Failed reading socket %d : %s", sock_fd, strerror(errno));
                        close_connection = 1;
                    } else {
                        nr_written = write(tun_fd, buffer, nr_read);
                        if (nr_written <= 0) {
                            LOG_ERROR("Failed writing to tunnel %d : %s", tun_fd, strerror(errno));
                            close_connection = 1;
                        } else if (nr_written == nr_read) {
                            LOG_DEBUG("TCP -> TUN: Wrote %zd bytes", nr_written);
                        }
                    }
                }
                if (close_connection) {
                    LOG_WARN("Closing socket %d", sock_fd);
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, sock_fd, NULL);
                    close(sock_fd);
                    terminate_loop = 1;
                }
            }
        }
    }
    close(epoll_fd);
    LOG_INFO("epoll() loop terminated");
}
