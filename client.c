#include "common.h"

#define REKEY_AFTER_SECS 300

static void udp_event_loop(int tun_fd, int sock_fd, struct sockaddr_in *server_addr,
                           const unsigned char *key) {
    unsigned char buffer[BUFFER_SIZE];
    unsigned char plain_buf[HEADER_SIZE + SEQ_SIZE + BUFFER_SIZE];
    unsigned char wire_buf[BUFFER_SIZE + WIRE_OVERHEAD];
    ssize_t nr_read, nr_written;
    int terminate_loop = 0;
    uint64_t send_seq = 0;
    uint64_t recv_seq_highest = 0;
    uint64_t recv_seq_window[REPLAY_WINDOW_WORDS] = {0};
    time_t rekey_deadline = time(NULL) + REKEY_AFTER_SECS;

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
        if (time(NULL) >= rekey_deadline) {
            LOG_INFO("Rekey interval reached — initiating rekey");
            break;
        }
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
                ssize_t send_len;
                if (key) {
                    int enc_len;
                    uint64_t seq_be = htobe64(send_seq++);
                    memcpy(plain_buf, pkt_header, HEADER_SIZE);
                    memcpy(plain_buf + HEADER_SIZE, &seq_be, SEQ_SIZE);
                    memcpy(plain_buf + HEADER_SIZE + SEQ_SIZE, buffer, nr_read);
                    if (encrypt_packet(key, plain_buf, HEADER_SIZE + SEQ_SIZE + (int) nr_read, wire_buf, &enc_len) < 0) {
                        LOG_ERROR("encrypt_packet() failed");
                        terminate_loop = 1;
                        break;
                    }
                    send_len = enc_len;
                } else {
                    memcpy(wire_buf, pkt_header, HEADER_SIZE);
                    memcpy(wire_buf + HEADER_SIZE, buffer, nr_read);
                    send_len = HEADER_SIZE + nr_read;
                }
                nr_written = sendto(sock_fd, wire_buf, send_len, 0,
                                    (struct sockaddr *) server_addr, sizeof(*server_addr));
                if (nr_written < 0) {
                    LOG_ERROR("sendto() failed : %s", strerror(errno));
                    terminate_loop = 1;
                    break;
                }
                LOG_DEBUG("TUN -> UDP: Wrote %zd bytes", nr_written);
            } else {
                nr_read = recvfrom(sock_fd, wire_buf, sizeof(wire_buf), 0, NULL, NULL);
                if (nr_read < 0) {
                    LOG_ERROR("recvfrom() failed : %s", strerror(errno));
                    terminate_loop = 1;
                    break;
                }
                if (key) {
                    int plain_len;
                    if (decrypt_packet(key, wire_buf, (int) nr_read, plain_buf, &plain_len) < 0) {
                        LOG_WARN("decrypt_packet() failed — dropping packet");
                        continue;
                    }
                    if (plain_len < HEADER_SIZE + SEQ_SIZE || memcmp(plain_buf, pkt_header, HEADER_SIZE) != 0) {
                        LOG_WARN("dropping packet: bad header");
                        continue;
                    }
                    uint64_t seq_be;
                    memcpy(&seq_be, plain_buf + HEADER_SIZE, SEQ_SIZE);
                    uint64_t seq = be64toh(seq_be);
                    if (check_replay(seq, &recv_seq_highest, recv_seq_window) < 0) {
                        LOG_WARN("Replay detected (seq=%lu) — dropping", (unsigned long) seq);
                        continue;
                    }
                    int payload_len = plain_len - HEADER_SIZE - SEQ_SIZE;
                    if (payload_len == 0)
                        continue;
                    nr_written = write(tun_fd, plain_buf + HEADER_SIZE + SEQ_SIZE, payload_len);
                } else {
                    if (nr_read < HEADER_SIZE || memcmp(wire_buf, pkt_header, HEADER_SIZE) != 0) {
                        LOG_WARN("dropping packet: bad header");
                        continue;
                    }
                    ssize_t write_len = nr_read - HEADER_SIZE;
                    if (write_len == 0)
                        continue;
                    nr_written = write(tun_fd, wire_buf + HEADER_SIZE, write_len);
                }
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

void start_client(char *tunnel, char *ip_addr, int port, const unsigned char *psk_key,
                  EVP_PKEY *static_key, const unsigned char *static_pub,
                  const unsigned char *server_static_pub) {
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

        unsigned char session_key[CRYPTO_KEY_LEN];
        if (handshake_client(sock_fd, &server_addr, psk_key,
                             static_key, static_pub, server_static_pub,
                             session_key) < 0) {
            LOG_ERROR("Handshake failed, reconnecting");
            close(sock_fd);
            continue;
        }
        LOG_INFO("Connected to %s:%d", ip_addr, port);

        udp_event_loop(tun_fd, sock_fd, &server_addr, session_key);

        LOG_INFO("Session ended for %s:%d, reconnecting", ip_addr, port);
        close(sock_fd);
    }
}

const char *program_name;

void usage() {
    fprintf(stderr, "Usage : %s -i <tunnel-interface> -s <server-ip> -C <server-cert> [-p <port>] [-k <psk>] [-K <keyfile>] [-E <server-pubkey-hex>] [-v] [-h]\n",
            program_name);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "   -i  tunnel interface\n");
    fprintf(stderr, "   -s  server ip\n");
    fprintf(stderr, "   -C  server public key file in PEM format (required)\n");
    fprintf(stderr, "   -p  server port (default 5040)\n");
    fprintf(stderr, "   -k  pre-shared key for handshake authentication\n");
    fprintf(stderr, "   -K  static private key file in PEM format (default client.key, required)\n");
    fprintf(stderr, "   -E  server public key (hex) — overrides -C\n");
    fprintf(stderr, "   -v  verbose\n");
    fprintf(stderr, "   -h  print this help message\n");
    fprintf(stderr, "Example : %s -i lanecove-udp -s 10.10.0.1 -p 5040 -k mysecret -K client.key -C server.crt\n", program_name);
    exit(1);
}

int main(int argc, char *argv[]) {
    int option;
    int server_port = 5040;
    program_name = argv[0];
    char tunnel_name[IF_NAMESIZE];
    char server_ip[INET_ADDRSTRLEN];
    char psk[256];
    char keyfile[256];
    char server_cert[256];
    char server_pub_hex[DH_PUBKEY_LEN * 2 + 1];
    int has_psk = 0;
    int has_server_pub = 0;
    int has_server_cert = 0;

    memset(tunnel_name, 0, IF_NAMESIZE);
    memset(server_ip, 0, INET_ADDRSTRLEN);
    memset(psk, 0, sizeof(psk));
    strncpy(keyfile, "client.key", sizeof(keyfile) - 1);
    memset(server_cert, 0, sizeof(server_cert));
    memset(server_pub_hex, 0, sizeof(server_pub_hex));

    while ((option = getopt(argc, argv, "i:s:p:k:K:C:E:hv")) > 0) {
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
            case 'k':
                strncpy(psk, optarg, sizeof(psk) - 1);
                has_psk = 1;
                break;
            case 'K':
                strncpy(keyfile, optarg, sizeof(keyfile) - 1);
                break;
            case 'C':
                strncpy(server_cert, optarg, sizeof(server_cert) - 1);
                has_server_cert = 1;
                break;
            case 'E':
                strncpy(server_pub_hex, optarg, sizeof(server_pub_hex) - 1);
                has_server_pub = 1;
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
    if (*tunnel_name == '\0' || *server_ip == '\0' || !has_server_cert)
        usage();

    unsigned char psk_key[CRYPTO_KEY_LEN];
    if (has_psk) {
        derive_key(psk, psk_key);
        LOG_INFO("PSK set — DH handshake will be authenticated");
    } else {
        LOG_WARN("No PSK — DH handshake unauthenticated (MITM-vulnerable)");
    }

    EVP_PKEY *static_key = NULL;
    unsigned char static_pub[DH_PUBKEY_LEN];
    if (load_static_key(keyfile, &static_key, static_pub) < 0)
        exit(EXIT_FAILURE);
    char pub_hex[DH_PUBKEY_LEN * 2 + 1];
    bytes_to_hex(static_pub, DH_PUBKEY_LEN, pub_hex);
    LOG_INFO("Client public key: %s", pub_hex);

    unsigned char server_static_pub[DH_PUBKEY_LEN];
    if (has_server_pub) {
        if (hex_to_bytes(server_pub_hex, server_static_pub, DH_PUBKEY_LEN) < 0) {
            LOG_ERROR("Invalid server public key hex: %s", server_pub_hex);
            exit(EXIT_FAILURE);
        }
        LOG_INFO("Expected server public key: %s", server_pub_hex);
    } else {
        if (load_public_key(server_cert, server_static_pub) < 0)
            exit(EXIT_FAILURE);
        char pub_hex[DH_PUBKEY_LEN * 2 + 1];
        bytes_to_hex(server_static_pub, DH_PUBKEY_LEN, pub_hex);
        LOG_INFO("Expected server public key (from %s): %s", server_cert, pub_hex);
        has_server_pub = 1;
    }

    start_client(tunnel_name, server_ip, server_port,
                 has_psk ? psk_key : NULL,
                 static_key, static_pub,
                 has_server_pub ? server_static_pub : NULL);
}
