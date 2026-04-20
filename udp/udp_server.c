#include "udp_common.h"

static void udp_event_loop(int tun_fd, int sock_fd, struct sockaddr_in *peer,
                           const unsigned char *key) {
    unsigned char buffer[BUFFER_SIZE];
    unsigned char crypto_buf[BUFFER_SIZE + CRYPTO_OVERHEAD];
    ssize_t nr_read, nr_written;
    struct sockaddr_in src_addr;
    socklen_t src_addr_len;
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
            if (events[i].events & (EPOLLERR | EPOLLHUP)) {
                LOG_ERROR("Got error/hangup event on fd = %d", events[i].data.fd);
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
                const void *send_buf = buffer;
                ssize_t send_len = nr_read;
                if (key) {
                    int enc_len;
                    if (encrypt_packet(key, buffer, (int) nr_read, crypto_buf, &enc_len) < 0) {
                        LOG_ERROR("encrypt_packet() failed");
                        terminate_loop = 1;
                        break;
                    }
                    send_buf = crypto_buf;
                    send_len = enc_len;
                }
                nr_written = sendto(sock_fd, send_buf, send_len, 0,
                                    (struct sockaddr *) peer, sizeof(*peer));
                if (nr_written < 0) {
                    LOG_ERROR("sendto() failed : %s", strerror(errno));
                    terminate_loop = 1;
                    break;
                }
                LOG_DEBUG("TUN -> UDP: Wrote %zd bytes", nr_written);
            } else {
                src_addr_len = sizeof(src_addr);
                nr_read = recvfrom(sock_fd, crypto_buf, sizeof(crypto_buf), 0,
                                   (struct sockaddr *) &src_addr, &src_addr_len);
                if (nr_read < 0) {
                    LOG_ERROR("recvfrom() failed : %s", strerror(errno));
                    terminate_loop = 1;
                    break;
                }
                if (src_addr.sin_addr.s_addr != peer->sin_addr.s_addr ||
                    src_addr.sin_port != peer->sin_port) {
                    LOG_INFO("New client from %s:%d, switching peer",
                             inet_ntoa(src_addr.sin_addr), ntohs(src_addr.sin_port));
                    *peer = src_addr;
                }
                const void *write_buf = crypto_buf;
                ssize_t write_len = nr_read;
                if (key) {
                    int plain_len;
                    if (decrypt_packet(key, crypto_buf, (int) nr_read, buffer, &plain_len) < 0) {
                        LOG_WARN("decrypt_packet() failed — dropping packet");
                        continue;
                    }
                    write_buf = buffer;
                    write_len = plain_len;
                }
                if (write_len == 0)
                    continue;
                nr_written = write(tun_fd, write_buf, write_len);
                if (nr_written < 0) {
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

void start_server(char *tunnel, int port, const unsigned char *key) {
    struct sockaddr_in server_addr, peer_addr;
    int sock_fd, tun_fd;
    int socket_opts = 1;

    if ((tun_fd = open_tunnel(tunnel)) < 0) {
        exit(EXIT_FAILURE);
    }
    if ((sock_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        LOG_ERROR("Unable to open socket : %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &socket_opts, sizeof(socket_opts)) < 0) {
        LOG_ERROR("Unable to set socket option : %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(port);
    if (bind(sock_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        LOG_ERROR("Unable to bind address : %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    LOG_INFO("Started UDP server on 0.0.0.0:%d", port);

    unsigned char buf[BUFFER_SIZE + CRYPTO_OVERHEAD];
    socklen_t peer_len = sizeof(peer_addr);
    ssize_t nr_read = recvfrom(sock_fd, buf, sizeof(buf), 0,
                               (struct sockaddr *) &peer_addr, &peer_len);
    if (nr_read < 0) {
        LOG_ERROR("recvfrom() failed : %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    LOG_INFO("First datagram from %s:%d", inet_ntoa(peer_addr.sin_addr), ntohs(peer_addr.sin_port));

    if (key) {
        unsigned char plain[BUFFER_SIZE];
        int plain_len;
        if (decrypt_packet(key, buf, (int) nr_read, plain, &plain_len) < 0) {
            LOG_ERROR("Failed to decrypt initial packet — wrong PSK?");
            exit(EXIT_FAILURE);
        }
        if (plain_len > 0 && write(tun_fd, plain, plain_len) < 0) {
            LOG_ERROR("Failed writing initial packet to tunnel : %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
    } else {
        if (nr_read > 0 && write(tun_fd, buf, nr_read) < 0) {
            LOG_ERROR("Failed writing initial packet to tunnel : %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

    udp_event_loop(tun_fd, sock_fd, &peer_addr, key);

    close(sock_fd);
    close(tun_fd);
}

const char *program_name;

void usage() {
    fprintf(stderr, "Usage : %s -i <tunnel-interface> -p [port] [-k <psk>] [-v] [-h]\n", program_name);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "   -i tunnel interface\n");
    fprintf(stderr, "   -p server port\n");
    fprintf(stderr, "   -k pre-shared key for AES-256-GCM encryption\n");
    fprintf(stderr, "   -v verbose\n");
    fprintf(stderr, "   -h print this help message\n");
    fprintf(stderr, "Example : %s -i lanecove-udp -p 5040 -k mysecret", program_name);
    fprintf(stderr, "\n");
    exit(1);
}

int main(int argc, char *argv[]) {
    program_name = argv[0];
    int option;
    int server_port = 5040;
    char tunnel_name[IF_NAMESIZE];
    char psk[256];
    int has_key = 0;

    memset(tunnel_name, 0, IF_NAMESIZE);
    memset(psk, 0, sizeof(psk));

    while ((option = getopt(argc, argv, "i:p:k:vh")) > 0) {
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
            case 'p':
                server_port = atoi(optarg);
                break;
            case 'k':
                strncpy(psk, optarg, sizeof(psk) - 1);
                has_key = 1;
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
    if (*tunnel_name == '\0')
        usage();

    unsigned char key[CRYPTO_KEY_LEN];
    if (has_key) {
        derive_key(psk, key);
        LOG_INFO("Encryption enabled (AES-256-GCM)");
    } else {
        LOG_WARN("No PSK provided — running without encryption");
    }

    start_server(tunnel_name, server_port, has_key ? key : NULL);
}
