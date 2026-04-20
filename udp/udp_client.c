#include "udp_common.h"

static void udp_event_loop(int tun_fd, int sock_fd, struct sockaddr_in *server_addr) {
    char buffer[BUFFER_SIZE];
    ssize_t nr_read, nr_written;
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
                    LOG_ERROR("Failed reading from tunnel : %s", strerror(errno));
                    terminate_loop = 1;
                    break;
                }
                nr_written = sendto(sock_fd, buffer, nr_read, 0,
                                    (struct sockaddr *) server_addr, sizeof(*server_addr));
                if (nr_written <= 0) {
                    LOG_ERROR("sendto() failed : %s", strerror(errno));
                    terminate_loop = 1;
                    break;
                }
                LOG_DEBUG("TUN -> UDP: Wrote %zd bytes", nr_written);
            } else {
                nr_read = recvfrom(sock_fd, buffer, BUFFER_SIZE, 0, NULL, NULL);
                if (nr_read <= 0) {
                    LOG_ERROR("recvfrom() failed : %s", strerror(errno));
                    terminate_loop = 1;
                    break;
                }
                nr_written = write(tun_fd, buffer, nr_read);
                if (nr_written <= 0) {
                    LOG_ERROR("Failed writing to tunnel : %s", strerror(errno));
                    terminate_loop = 1;
                    break;
                }
                LOG_DEBUG("UDP -> TUN: Wrote %zd bytes", nr_written);
            }
        }
    }
    close(epoll_fd);
    LOG_INFO("epoll() loop terminated");
}

void start_client(char *tunnel, char *ip_addr, int port) {
    struct sockaddr_in server_addr;
    int sock_fd, tun_fd;

    if ((tun_fd = open_tunnel(tunnel)) < 0) {
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    if (inet_pton(AF_INET, ip_addr, &server_addr.sin_addr) <= 0) {
        LOG_ERROR("Invalid server IP address : %s", ip_addr);
        exit(EXIT_FAILURE);
    }
    server_addr.sin_port = htons(port);

    while (1) {
        if ((sock_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
            LOG_ERROR("Unable to open socket : %s", strerror(errno));
            exit(EXIT_FAILURE);
        }

        /* Send a zero-length probe so the server learns our address */
        if (sendto(sock_fd, "", 0, 0, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
            LOG_ERROR("sendto() probe failed : %s", strerror(errno));
            close(sock_fd);
            exit(EXIT_FAILURE);
        }
        LOG_INFO("Connected to %s:%d", ip_addr, port);

        udp_event_loop(tun_fd, sock_fd, &server_addr);

        LOG_INFO("Disconnected from %s:%d, reconnecting", ip_addr, port);
        close(sock_fd);
    }
}

const char *program_name;

void usage() {
    fprintf(stderr, "Usage : %s -i <tunnel-interface> -s <server-ip> -p [port] [-v] [-h]\n", program_name);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "   -i tunnel interface\n");
    fprintf(stderr, "   -s server ip\n");
    fprintf(stderr, "   -p server port\n");
    fprintf(stderr, "   -v verbose\n");
    fprintf(stderr, "   -h print this help message\n");
    fprintf(stderr, "Example : %s -i lanecove-udp -s 10.9.0.2 -p 5040", program_name);
    fprintf(stderr, "\n");
    exit(1);
}

int main(int argc, char *argv[]) {
    int option;
    int server_port = 5040;
    program_name = argv[0];
    char tunnel_name[IF_NAMESIZE];
    char server_ip[INET_ADDRSTRLEN];

    memset(tunnel_name, 0, IF_NAMESIZE);
    memset(server_ip, 0, INET_ADDRSTRLEN);

    while ((option = getopt(argc, argv, "i:s:p:hv")) > 0) {
        switch (option) {
            case 'h':
                usage();
                break;
            case 'v':
                log_level = 1;
                break;
            case 'i':
                strncpy(tunnel_name, optarg, IFNAMSIZ - 1);
                break;
            case 's':
                strncpy(server_ip, optarg, INET_ADDRSTRLEN - 1);
                break;
            case 'p':
                server_port = atoi(optarg);
                break;
            default:
                fprintf(stderr, "Unknown option %c\n", option);
                usage();
        }
    }

    argv += optind;
    argc -= optind;

    if (argc > 0)
        usage();
    if (*tunnel_name == '\0' || *server_ip == '\0')
        usage();
    start_client(tunnel_name, server_ip, server_port);
}
