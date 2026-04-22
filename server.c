#include <time.h>
#include "common.h"
#include "rcunit_list.h"

#define HANDSHAKE_COOLDOWN_SECS  5
#define MAX_CLIENTS_DEFAULT      16
#define MAX_ALLOWED_CLIENTS      64
#define MAX_ROUTES_PER_CLIENT    16

typedef struct {
    uint32_t network;    /* host byte order */
    uint32_t mask;       /* host byte order */
    int      prefix_len; /* for longest-prefix match */
} ip_prefix_t;

typedef struct {
    unsigned char pub[DH_PUBKEY_LEN];
    ip_prefix_t   routes[MAX_ROUTES_PER_CLIENT];
    int           route_count;
} allowed_client_t;

typedef struct {
    rcu_list      node;
    struct sockaddr_in addr;
    unsigned char session_key[CRYPTO_KEY_LEN];
    unsigned char static_pub[DH_PUBKEY_LEN];
    time_t        last_seen;
    time_t        last_handshake;
    uint64_t      send_seq;
    uint64_t      recv_seq_highest;
    uint64_t      recv_seq_window[REPLAY_WINDOW_WORDS];
    ip_prefix_t   routes[MAX_ROUTES_PER_CLIENT];
    int           route_count;
} udp_client_t;

static rcu_list        client_list;
static int             max_clients = MAX_CLIENTS_DEFAULT;
static allowed_client_t allowed_clients[MAX_ALLOWED_CLIENTS];
static int             allowed_client_count = 0;

static int parse_cidr(const char *cidr, ip_prefix_t *out) {
    char buf[32];
    strncpy(buf, cidr, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char *slash = strchr(buf, '/');
    int prefix_len = 32;
    if (slash) {
        *slash = '\0';
        prefix_len = atoi(slash + 1);
        if (prefix_len < 0 || prefix_len > 32) return -1;
    }
    struct in_addr addr;
    if (inet_pton(AF_INET, buf, &addr) != 1) return -1;
    uint32_t mask = prefix_len == 0 ? 0u : (~0u << (32 - prefix_len));
    out->network    = ntohl(addr.s_addr) & mask;
    out->mask       = mask;
    out->prefix_len = prefix_len;
    return 0;
}

static allowed_client_t *find_allowed_client(const unsigned char *static_pub) {
    if (allowed_client_count == 0) return NULL;
    for (int i = 0; i < allowed_client_count; i++)
        if (memcmp(allowed_clients[i].pub, static_pub, DH_PUBKEY_LEN) == 0)
            return &allowed_clients[i];
    return NULL;
}

static udp_client_t *find_client(struct sockaddr_in *addr) {
    RCU_FOR_EACH_ENTRY_WITH_CURSOR(&client_list, cursor) {
        udp_client_t *c = (udp_client_t *) cursor;
        if (c->addr.sin_addr.s_addr == addr->sin_addr.s_addr &&
            c->addr.sin_port == addr->sin_port)
            return c;
    }
    return NULL;
}

static udp_client_t *find_client_by_pub(const unsigned char *static_pub) {
    RCU_FOR_EACH_ENTRY_WITH_CURSOR(&client_list, cursor) {
        udp_client_t *c = (udp_client_t *) cursor;
        if (memcmp(c->static_pub, static_pub, DH_PUBKEY_LEN) == 0)
            return c;
    }
    return NULL;
}

static udp_client_t *route_lookup(uint32_t dst_ip) {
    udp_client_t *best = NULL;
    int best_prefix = -1;
    RCU_FOR_EACH_ENTRY_WITH_CURSOR(&client_list, cursor) {
        udp_client_t *c = (udp_client_t *) cursor;
        for (int i = 0; i < c->route_count; i++) {
            if ((dst_ip & c->routes[i].mask) == c->routes[i].network &&
                c->routes[i].prefix_len > best_prefix) {
                best_prefix = c->routes[i].prefix_len;
                best = c;
            }
        }
    }
    return best;
}

static int check_allowed_src(udp_client_t *c, uint32_t src_ip) {
    if (c->route_count == 0) return 1;
    for (int i = 0; i < c->route_count; i++)
        if ((src_ip & c->routes[i].mask) == c->routes[i].network) return 1;
    return 0;
}

static void print_client_list() {
    int i = 1;
    LOG_INFO("Connected clients (%d):", rcu_get_list_size(&client_list));
    RCU_FOR_EACH_ENTRY_WITH_CURSOR(&client_list, cursor) {
        udp_client_t *c = (udp_client_t *) cursor;
        char ts[32] = "never";
        if (c->last_seen) {
            struct tm *tm_info = localtime(&c->last_seen);
            strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm_info);
        }
        char key_hex[17];
        bytes_to_hex(c->static_pub, 8, key_hex);
        LOG_INFO("  [%d] %s:%d key=%s... last_seen=%s routes=%d",
                 i++, inet_ntoa(c->addr.sin_addr), ntohs(c->addr.sin_port),
                 key_hex, ts, c->route_count);
    }
}

static udp_client_t *add_or_update_client(struct sockaddr_in *addr,
                                           const unsigned char *static_pub,
                                           unsigned char *session_key,
                                           allowed_client_t *ac) {
    udp_client_t *c = find_client_by_pub(static_pub);
    if (!c) c = find_client(addr);
    if (c) {
        if (c->addr.sin_addr.s_addr != addr->sin_addr.s_addr ||
            c->addr.sin_port != addr->sin_port) {
            LOG_INFO("Client address changed: %s:%d -> %s:%d",
                     inet_ntoa(c->addr.sin_addr), ntohs(c->addr.sin_port),
                     inet_ntoa(addr->sin_addr), ntohs(addr->sin_port));
            c->addr = *addr;
        }
        memcpy(c->session_key, session_key, CRYPTO_KEY_LEN);
        memcpy(c->static_pub, static_pub, DH_PUBKEY_LEN);
        c->send_seq = 0;
        c->recv_seq_highest = 0;
        memset(c->recv_seq_window, 0, sizeof(c->recv_seq_window));
        c->last_handshake = time(NULL);
        LOG_INFO("Client re-keyed: %s:%d", inet_ntoa(addr->sin_addr), ntohs(addr->sin_port));
    } else {
        c = malloc(sizeof(udp_client_t));
        if (!c) return NULL;
        c->addr = *addr;
        memcpy(c->static_pub, static_pub, DH_PUBKEY_LEN);
        c->last_seen = 0;
        c->last_handshake = time(NULL);
        c->send_seq = 0;
        c->recv_seq_highest = 0;
        memset(c->recv_seq_window, 0, sizeof(c->recv_seq_window));
        memcpy(c->session_key, session_key, CRYPTO_KEY_LEN);
        rcu_insert_list(&client_list, &c->node);
        char key_hex[17];
        bytes_to_hex(static_pub, 8, key_hex);
        LOG_INFO("Client connected: %s:%d key=%s...",
                 inet_ntoa(addr->sin_addr), ntohs(addr->sin_port), key_hex);
    }
    if (ac) {
        memcpy(c->routes, ac->routes, ac->route_count * sizeof(ip_prefix_t));
        c->route_count = ac->route_count;
    } else {
        c->route_count = 0;
    }
    print_client_list();
    return c;
}

static void forward_to_client(int sock_fd, udp_client_t *c,
                               unsigned char *plain_buf, const unsigned char *payload,
                               int payload_len, unsigned char *wire_buf) {
    int enc_len;
    uint64_t seq_be = htobe64(c->send_seq++);
    memcpy(plain_buf, pkt_header, HEADER_SIZE);
    memcpy(plain_buf + HEADER_SIZE, &seq_be, SEQ_SIZE);
    memcpy(plain_buf + HEADER_SIZE + SEQ_SIZE, payload, payload_len);
    if (encrypt_packet(c->session_key, plain_buf, HEADER_SIZE + SEQ_SIZE + payload_len,
                       wire_buf, &enc_len) < 0) {
        LOG_ERROR("encrypt_packet() failed for %s:%d",
                  inet_ntoa(c->addr.sin_addr), ntohs(c->addr.sin_port));
        return;
    }
    ssize_t sent = sendto(sock_fd, wire_buf, enc_len, 0,
                          (struct sockaddr *) &c->addr, sizeof(c->addr));
    if (sent < 0)
        LOG_ERROR("sendto() failed for %s:%d : %s",
                  inet_ntoa(c->addr.sin_addr), ntohs(c->addr.sin_port), strerror(errno));
    else
        LOG_DEBUG("TUN -> UDP [%s:%d]: %zd bytes",
                  inet_ntoa(c->addr.sin_addr), ntohs(c->addr.sin_port), sent);
}

static void udp_event_loop(int tun_fd, int sock_fd, const unsigned char *psk_key,
                           EVP_PKEY *static_key, const unsigned char *static_pub) {
    unsigned char buffer[BUFFER_SIZE];
    unsigned char plain_buf[HEADER_SIZE + SEQ_SIZE + BUFFER_SIZE];
    unsigned char wire_buf[BUFFER_SIZE + WIRE_OVERHEAD];
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
                if (nr_read >= 20 && (buffer[0] >> 4) == 4) {
                    uint32_t dst_ip = ntohl(*(uint32_t *)(buffer + 16));
                    udp_client_t *c = route_lookup(dst_ip);
                    if (!c) {
                        struct in_addr a = { htonl(dst_ip) };
                        LOG_DEBUG("No route for dst %s — dropping", inet_ntoa(a));
                    } else {
                        forward_to_client(sock_fd, c, plain_buf, buffer, (int)nr_read, wire_buf);
                    }
                } else {
                    LOG_DEBUG("Non-IPv4 or short packet from TUN — dropping");
                }
            } else {
                src_addr_len = sizeof(src_addr);
                nr_read = recvfrom(sock_fd, wire_buf, sizeof(wire_buf), 0,
                                   (struct sockaddr *) &src_addr, &src_addr_len);
                if (nr_read < 0) {
                    LOG_ERROR("recvfrom() failed : %s", strerror(errno));
                    terminate_loop = 1;
                    break;
                }
                int hs_size = HEADER_SIZE + DH_PUBKEY_LEN * 2 + (psk_key ? HMAC_LEN : 0);
                if (nr_read == hs_size && memcmp(wire_buf, pkt_header, HEADER_SIZE) == 0) {
                    udp_client_t *existing = find_client(&src_addr);
                    if (existing) {
                        time_t elapsed = time(NULL) - existing->last_handshake;
                        if (elapsed < HANDSHAKE_COOLDOWN_SECS) {
                            LOG_WARN("Handshake cooldown active for %s:%d (%lds remaining) — ignoring",
                                     inet_ntoa(src_addr.sin_addr), ntohs(src_addr.sin_port),
                                     (long)(HANDSHAKE_COOLDOWN_SECS - elapsed));
                            continue;
                        }
                    } else if (rcu_get_list_size(&client_list) >= max_clients) {
                        LOG_WARN("Max clients (%d) reached — rejecting handshake from %s:%d",
                                 max_clients, inet_ntoa(src_addr.sin_addr), ntohs(src_addr.sin_port));
                        continue;
                    }
                    LOG_INFO("Handshake from %s:%d",
                             inet_ntoa(src_addr.sin_addr), ntohs(src_addr.sin_port));
                    unsigned char session_key[CRYPTO_KEY_LEN];
                    unsigned char client_static_pub[DH_PUBKEY_LEN];
                    if (handshake_server_respond(sock_fd, wire_buf, (int) nr_read,
                                                 &src_addr, psk_key,
                                                 static_key, static_pub,
                                                 client_static_pub, session_key) < 0)
                        continue;
                    allowed_client_t *ac = find_allowed_client(client_static_pub);
                    if (allowed_client_count > 0 && !ac) {
                        char key_hex[DH_PUBKEY_LEN * 2 + 1];
                        bytes_to_hex(client_static_pub, DH_PUBKEY_LEN, key_hex);
                        LOG_WARN("Rejecting client with unknown public key: %s", key_hex);
                        continue;
                    }
                    add_or_update_client(&src_addr, client_static_pub, session_key, ac);
                    continue;
                }
                udp_client_t *c = find_client(&src_addr);
                if (!c) {
                    LOG_WARN("Dropping packet from unknown peer %s:%d",
                             inet_ntoa(src_addr.sin_addr), ntohs(src_addr.sin_port));
                    continue;
                }
                c->last_seen = time(NULL);
                int plain_len;
                if (decrypt_packet(c->session_key, wire_buf, (int) nr_read, plain_buf, &plain_len) < 0) {
                    LOG_WARN("decrypt_packet() failed — dropping packet from %s:%d",
                             inet_ntoa(src_addr.sin_addr), ntohs(src_addr.sin_port));
                    continue;
                }
                if (plain_len < HEADER_SIZE + SEQ_SIZE || memcmp(plain_buf, pkt_header, HEADER_SIZE) != 0) {
                    LOG_WARN("Dropping packet: bad header from %s:%d",
                             inet_ntoa(src_addr.sin_addr), ntohs(src_addr.sin_port));
                    continue;
                }
                uint64_t seq_be;
                memcpy(&seq_be, plain_buf + HEADER_SIZE, SEQ_SIZE);
                uint64_t seq = be64toh(seq_be);
                if (check_replay(seq, &c->recv_seq_highest, c->recv_seq_window) < 0) {
                    LOG_WARN("Replay detected from %s:%d (seq=%lu) — dropping",
                             inet_ntoa(src_addr.sin_addr), ntohs(src_addr.sin_port), (unsigned long) seq);
                    continue;
                }
                int payload_len = plain_len - HEADER_SIZE - SEQ_SIZE;
                if (payload_len == 0)
                    continue;
                const unsigned char *payload = plain_buf + HEADER_SIZE + SEQ_SIZE;
                if (c->route_count > 0 && payload_len >= 20 && (payload[0] >> 4) == 4) {
                    uint32_t src_ip = ntohl(*(uint32_t *)(payload + 12));
                    if (!check_allowed_src(c, src_ip)) {
                        struct in_addr a = { htonl(src_ip) };
                        LOG_WARN("Source IP %s not in AllowedIPs for %s:%d — dropping",
                                 inet_ntoa(a),
                                 inet_ntoa(c->addr.sin_addr), ntohs(c->addr.sin_port));
                        continue;
                    }
                }
                nr_written = write(tun_fd, payload, payload_len);
                if (nr_written < 0) {
                    LOG_ERROR("Failed writing to tunnel : %s", strerror(errno));
                    terminate_loop = 1;
                    break;
                }
                LOG_DEBUG("UDP -> TUN [%s:%d]: %zd bytes",
                          inet_ntoa(src_addr.sin_addr), ntohs(src_addr.sin_port), nr_written);
            }
        }
    }
    close(epoll_fd);
    LOG_INFO("epoll() loop terminated");
}

void start_server(char *tunnel, int port, const unsigned char *psk_key, int max,
                  EVP_PKEY *static_key, const unsigned char *static_pub) {
    max_clients = max;
    int sock_fd, tun_fd;
    int socket_opts = 1;
    struct sockaddr_in server_addr;

    rcu_init_list(&client_list);

    if ((tun_fd = open_tunnel(tunnel)) < 0)
        exit(EXIT_FAILURE);
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

    udp_event_loop(tun_fd, sock_fd, psk_key, static_key, static_pub);

    close(sock_fd);
    close(tun_fd);
}

const char *program_name;

void usage() {
    fprintf(stderr, "Usage : %s -i <iface> -p [port] [-K <keyfile>] [-A <pubkey_hex> [-R <cidr>] ...] [-m <max>] [-k <psk>] [-v] [-h]\n",
            program_name);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "   -i  tunnel interface\n");
    fprintf(stderr, "   -p  server port\n");
    fprintf(stderr, "   -K  static private key file in PEM format (default: server.key, required)\n");
    fprintf(stderr, "   -A  allowed client public key (hex, repeatable)\n");
    fprintf(stderr, "   -R  AllowedIPs CIDR for the preceding -A entry (repeatable, required per -A)\n");
    fprintf(stderr, "   -m  max connected clients (default: %d)\n", MAX_CLIENTS_DEFAULT);
    fprintf(stderr, "   -k  pre-shared key for handshake authentication\n");
    fprintf(stderr, "   -v  verbose\n");
    fprintf(stderr, "   -h  print this help message\n");
    fprintf(stderr, "Example : %s -i lanecove-udp -p 5040 -K server.key -A <pubkey> -R 10.9.0.0/24 -k mysecret\n",
            program_name);
    exit(1);
}

int main(int argc, char *argv[]) {
    program_name = argv[0];
    int option;
    int server_port = 5040;
    int max = MAX_CLIENTS_DEFAULT;
    char tunnel_name[IF_NAMESIZE];
    char psk[256];
    char keyfile[256];
    int has_psk = 0;

    memset(tunnel_name, 0, IF_NAMESIZE);
    memset(psk, 0, sizeof(psk));
    strncpy(keyfile, "server.key", sizeof(keyfile) - 1);

    while ((option = getopt(argc, argv, "i:p:K:A:R:m:k:vh")) > 0) {
        switch (option) {
            case 'h': usage(); break;
            case 'v': log_level = 1; break;
            case 'i': strncpy(tunnel_name, optarg, IFNAMSIZ - 1); break;
            case 'p': server_port = atoi(optarg); break;
            case 'K': strncpy(keyfile, optarg, sizeof(keyfile) - 1); break;
            case 'A':
                if (allowed_client_count >= MAX_ALLOWED_CLIENTS) {
                    fprintf(stderr, "Too many -A entries (max %d)\n", MAX_ALLOWED_CLIENTS);
                    usage();
                }
                if (hex_to_bytes(optarg, allowed_clients[allowed_client_count].pub, DH_PUBKEY_LEN) < 0) {
                    fprintf(stderr, "Invalid public key hex: %s\n", optarg);
                    usage();
                }
                allowed_clients[allowed_client_count].route_count = 0;
                allowed_client_count++;
                break;
            case 'R': {
                if (allowed_client_count == 0) {
                    fprintf(stderr, "-R must follow -A\n");
                    usage();
                }
                allowed_client_t *ac = &allowed_clients[allowed_client_count - 1];
                if (ac->route_count >= MAX_ROUTES_PER_CLIENT) {
                    fprintf(stderr, "Too many -R entries for this client (max %d)\n", MAX_ROUTES_PER_CLIENT);
                    usage();
                }
                if (parse_cidr(optarg, &ac->routes[ac->route_count]) < 0) {
                    fprintf(stderr, "Invalid CIDR: %s\n", optarg);
                    usage();
                }
                ac->route_count++;
                break;
            }
            case 'm':
                max = atoi(optarg);
                if (max <= 0) { fprintf(stderr, "max clients must be > 0\n"); usage(); }
                break;
            case 'k':
                strncpy(psk, optarg, sizeof(psk) - 1);
                has_psk = 1;
                break;
            default:
                fprintf(stderr, "Unknown option %c\n", option);
                usage();
        }
    }

    argv += optind;
    argc -= optind;
    if (argc > 0) usage();
    if (*tunnel_name == '\0') usage();
    if (allowed_client_count == 0) {
        fprintf(stderr, "At least one -A <pubkey> -R <cidr> entry is required\n");
        usage();
    }
    for (int i = 0; i < allowed_client_count; i++) {
        if (allowed_clients[i].route_count == 0) {
            char key_hex[DH_PUBKEY_LEN * 2 + 1];
            bytes_to_hex(allowed_clients[i].pub, DH_PUBKEY_LEN, key_hex);
            fprintf(stderr, "Client %s has no -R routes — each -A must have at least one -R\n", key_hex);
            usage();
        }
    }

    EVP_PKEY *static_key = NULL;
    unsigned char static_pub[DH_PUBKEY_LEN];
    if (load_static_key(keyfile, &static_key, static_pub) < 0)
        exit(EXIT_FAILURE);

    char pub_hex[DH_PUBKEY_LEN * 2 + 1];
    bytes_to_hex(static_pub, DH_PUBKEY_LEN, pub_hex);
    LOG_INFO("Server public key: %s", pub_hex);

    for (int i = 0; i < allowed_client_count; i++) {
        char key_hex[DH_PUBKEY_LEN * 2 + 1];
        bytes_to_hex(allowed_clients[i].pub, DH_PUBKEY_LEN, key_hex);
        LOG_INFO("Allowed client: %s (%d route(s))", key_hex, allowed_clients[i].route_count);
    }

    unsigned char psk_key[CRYPTO_KEY_LEN];
    if (has_psk) {
        derive_key(psk, psk_key);
        LOG_INFO("PSK set — handshake will be authenticated");
    } else {
        LOG_WARN("No PSK — handshake unauthenticated (MITM-vulnerable)");
    }

    start_server(tunnel_name, server_port, has_psk ? psk_key : NULL, max,
                 static_key, static_pub);
    EVP_PKEY_free(static_key);
}
