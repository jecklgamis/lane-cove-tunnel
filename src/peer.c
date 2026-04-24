#include <time.h>
#include "common.h"

#define REKEY_AFTER_SECS          180
#define REKEY_INITIATE_SECS       144   /* start rekeying at 80% of interval */
#define PREV_KEY_GRACE_SECS         5
#define HANDSHAKE_COOLDOWN_SECS     5
#define HANDSHAKE_TIMEOUT_SECS      5
#define RECONNECT_INTERVAL_SECS    30
#define MAX_PEERS                  64
#define MAX_ROUTES_PER_PEER        16

typedef struct {
    uint32_t network;
    uint32_t mask;
    int      prefix_len;
} ip_prefix_t;

/* Statically configured peer (from CLI -P/-E/-R flags) */
typedef struct {
    unsigned char      pub[DH_PUBKEY_LEN];
    ip_prefix_t        routes[MAX_ROUTES_PER_PEER];
    int                route_count;
    int                has_endpoint;   /* if set, we initiate to this peer */
    struct sockaddr_in endpoint;
    time_t             last_attempt;
} peer_config_t;

/* Runtime session with a connected peer */
typedef struct {
    int                active;
    struct sockaddr_in addr;
    unsigned char      static_pub[DH_PUBKEY_LEN];
    unsigned char      session_key[CRYPTO_KEY_LEN];
    unsigned char      prev_session_key[CRYPTO_KEY_LEN];
    int                prev_key_active;
    time_t             prev_key_expires;
    time_t             last_seen;
    time_t             last_handshake;
    time_t             rekey_deadline;
    int                rekeying;
    uint64_t           send_seq;
    uint64_t           recv_seq_highest;
    uint64_t           recv_seq_window[REPLAY_WINDOW_WORDS];
    ip_prefix_t        routes[MAX_ROUTES_PER_PEER];
    int                route_count;
    int                is_outbound;
} peer_session_t;

/* Pending outbound handshake — initiated but awaiting response in the epoll loop */
typedef struct {
    int                active;
    int                cfg_idx;
    time_t             sent_at;
    hs_client_state_t  hs_state;
    struct sockaddr_in server_addr;
} pending_hs_t;

static peer_config_t  peer_configs[MAX_PEERS];
static int            peer_config_count = 0;
static peer_session_t sessions[MAX_PEERS];
static int            session_slots = 0;
static pending_hs_t   pending_hs[MAX_PEERS];

/* Pre-generated ephemeral keypair for the inbound handshake responder.
 * Generated at startup and refreshed immediately after each use so the
 * expensive X25519 keygen never runs on the event-loop hot path. */
static EVP_PKEY      *precomp_eph_key = NULL;
static unsigned char  precomp_eph_pub[DH_PUBKEY_LEN];

static void refresh_precomp_eph(void) {
    EVP_PKEY *old = precomp_eph_key;
    EVP_PKEY *nk  = NULL;
    if (generate_eph_keypair(&nk, precomp_eph_pub) < 0) {
        LOG_WARN("Failed to pre-generate ephemeral keypair");
        precomp_eph_key = old;
        return;
    }
    precomp_eph_key = nk;
    if (old) EVP_PKEY_free(old);
}

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

static peer_config_t *find_peer_config(const unsigned char *pub) {
    for (int i = 0; i < peer_config_count; i++)
        if (memcmp(peer_configs[i].pub, pub, DH_PUBKEY_LEN) == 0)
            return &peer_configs[i];
    return NULL;
}

static peer_session_t *find_session_by_addr(struct sockaddr_in *addr) {
    for (int i = 0; i < session_slots; i++)
        if (sessions[i].active &&
            sessions[i].addr.sin_addr.s_addr == addr->sin_addr.s_addr &&
            sessions[i].addr.sin_port == addr->sin_port)
            return &sessions[i];
    return NULL;
}

static peer_session_t *find_session_by_pub(const unsigned char *pub) {
    for (int i = 0; i < session_slots; i++)
        if (sessions[i].active && memcmp(sessions[i].static_pub, pub, DH_PUBKEY_LEN) == 0)
            return &sessions[i];
    return NULL;
}

/* Returns an existing slot (by pub first, then addr), falls back to a free slot */
static peer_session_t *alloc_session(const unsigned char *pub, struct sockaddr_in *addr) {
    for (int i = 0; i < session_slots; i++)
        if (memcmp(sessions[i].static_pub, pub, DH_PUBKEY_LEN) == 0)
            return &sessions[i];
    peer_session_t *s = find_session_by_addr(addr);
    if (s) return s;
    for (int i = 0; i < session_slots; i++)
        if (!sessions[i].active) return &sessions[i];
    if (session_slots >= MAX_PEERS) return NULL;
    return &sessions[session_slots++];
}

static peer_session_t *route_lookup(uint32_t dst_ip) {
    peer_session_t *best = NULL;
    int best_prefix = -1;
    for (int i = 0; i < session_slots; i++) {
        if (!sessions[i].active) continue;
        for (int j = 0; j < sessions[i].route_count; j++) {
            if ((dst_ip & sessions[i].routes[j].mask) == sessions[i].routes[j].network &&
                sessions[i].routes[j].prefix_len > best_prefix) {
                best_prefix = sessions[i].routes[j].prefix_len;
                best = &sessions[i];
            }
        }
    }
    return best;
}

static int check_allowed_src(peer_session_t *s, uint32_t src_ip) {
    if (s->route_count == 0) return 1;
    for (int i = 0; i < s->route_count; i++)
        if ((src_ip & s->routes[i].mask) == s->routes[i].network) return 1;
    return 0;
}

static void session_init(peer_session_t *s, struct sockaddr_in *addr,
                         const unsigned char *pub, unsigned char *key,
                         peer_config_t *cfg, int is_outbound) {
    int was_active = s->active;
    struct sockaddr_in old_addr = s->addr;

    s->active = 1;
    s->addr = *addr;
    memcpy(s->static_pub, pub, DH_PUBKEY_LEN);
    if (was_active) {
        memcpy(s->prev_session_key, s->session_key, CRYPTO_KEY_LEN);
        s->prev_key_active = 1;
        s->prev_key_expires = time(NULL) + PREV_KEY_GRACE_SECS;
    } else {
        s->prev_key_active = 0;
    }
    memcpy(s->session_key, key, CRYPTO_KEY_LEN);
    s->last_seen = 0;
    s->last_handshake = time(NULL);
    s->rekey_deadline = time(NULL) + REKEY_AFTER_SECS;
    s->rekeying = 0;
    s->send_seq = 0;
    s->recv_seq_highest = 0;
    memset(s->recv_seq_window, 0, sizeof(s->recv_seq_window));
    s->is_outbound = is_outbound;
    if (cfg) {
        memcpy(s->routes, cfg->routes, cfg->route_count * sizeof(ip_prefix_t));
        s->route_count = cfg->route_count;
    } else {
        s->route_count = 0;
    }

    char key_hex[17];
    bytes_to_hex(pub, 8, key_hex);
    if (was_active) {
        if (old_addr.sin_addr.s_addr != addr->sin_addr.s_addr ||
            old_addr.sin_port != addr->sin_port)
            LOG_INFO("Peer address changed: %s:%d -> %s:%d (key=%s...)",
                     inet_ntoa(old_addr.sin_addr), ntohs(old_addr.sin_port),
                     inet_ntoa(addr->sin_addr), ntohs(addr->sin_port), key_hex);
        else
            LOG_INFO("Peer re-keyed: %s:%d (key=%s...)",
                     inet_ntoa(addr->sin_addr), ntohs(addr->sin_port), key_hex);
    } else {
        LOG_INFO("Peer connected: %s:%d (key=%s...)",
                 inet_ntoa(addr->sin_addr), ntohs(addr->sin_port), key_hex);
    }
}

static void print_sessions(void) {
    int active = 0;
    for (int i = 0; i < session_slots; i++) active += sessions[i].active ? 1 : 0;
    LOG_INFO("Active peers (%d):", active);
    for (int i = 0; i < session_slots; i++) {
        if (!sessions[i].active) continue;
        peer_session_t *s = &sessions[i];
        char key_hex[17];
        bytes_to_hex(s->static_pub, 8, key_hex);
        char ts[32] = "never";
        if (s->last_seen) {
            struct tm *tm_info = localtime(&s->last_seen);
            strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm_info);
        }
        LOG_INFO("  [%s] %s:%d key=%s... routes=%d last_seen=%s",
                 s->is_outbound ? "out" : "in",
                 inet_ntoa(s->addr.sin_addr), ntohs(s->addr.sin_port),
                 key_hex, s->route_count, ts);
    }
}

static void forward_to_peer(int sock_fd, peer_session_t *s,
                            unsigned char *plain_buf, const unsigned char *payload,
                            int payload_len, unsigned char *wire_buf) {
    int enc_len;
    uint64_t seq_be = htobe64(s->send_seq++);
    memcpy(plain_buf, pkt_header, HEADER_SIZE);
    memcpy(plain_buf + HEADER_SIZE, &seq_be, SEQ_SIZE);
    memcpy(plain_buf + HEADER_SIZE + SEQ_SIZE, payload, payload_len);
    if (encrypt_packet(s->session_key, plain_buf, HEADER_SIZE + SEQ_SIZE + payload_len,
                       wire_buf, &enc_len) < 0) {
        LOG_ERROR("Encrypt failed for %s:%d",
                  inet_ntoa(s->addr.sin_addr), ntohs(s->addr.sin_port));
        return;
    }
    sendto(sock_fd, wire_buf, enc_len, 0, (struct sockaddr *)&s->addr, sizeof(s->addr));
}

/* Send a handshake initiation to an outbound peer and register a pending entry.
 * Returns immediately — the response is handled asynchronously in the epoll loop.
 * The old session (if any) stays active and keeps forwarding until the response
 * arrives and session_init atomically replaces the key. */
static int initiate_outbound_handshake(int sock_fd, int cfg_idx,
                                       EVP_PKEY *static_key, const unsigned char *static_pub,
                                       const unsigned char *psk_key) {
    peer_config_t *cfg = &peer_configs[cfg_idx];
    cfg->last_attempt = time(NULL);

    int slot = -1;
    for (int i = 0; i < MAX_PEERS; i++) {
        if (!pending_hs[i].active) { slot = i; break; }
    }
    if (slot < 0) {
        LOG_WARN("No pending handshake slots available");
        return -1;
    }

    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &cfg->endpoint.sin_addr, ip_str, sizeof(ip_str));
    LOG_INFO("Initiating handshake to %s:%d", ip_str, ntohs(cfg->endpoint.sin_port));

    pending_hs_t *p = &pending_hs[slot];
    if (handshake_client_send(sock_fd, &cfg->endpoint, psk_key,
                               static_key, static_pub, cfg->pub, &p->hs_state) < 0) {
        LOG_WARN("Handshake initiation to %s:%d failed", ip_str, ntohs(cfg->endpoint.sin_port));
        return -1;
    }
    p->active     = 1;
    p->cfg_idx    = cfg_idx;
    p->sent_at    = time(NULL);
    p->server_addr = cfg->endpoint;
    return 0;
}

static void event_loop(int tun_fd, int sock_fd, EVP_PKEY *static_key,
                       const unsigned char *static_pub, const unsigned char *psk_key) {
    unsigned char buffer[BUFFER_SIZE];
    unsigned char plain_buf[HEADER_SIZE + SEQ_SIZE + BUFFER_SIZE];
    unsigned char wire_buf[BUFFER_SIZE + WIRE_OVERHEAD];
    ssize_t nr;
    struct sockaddr_in src_addr;
    socklen_t src_addr_len;
    time_t last_rekey_check = 0;
    int hs_size = HEADER_SIZE + DH_PUBKEY_LEN + HS_ENCRYPTED_PUB_LEN + (psk_key ? HMAC_LEN : 0);

    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) { LOG_ERROR("epoll_create1: %s", strerror(errno)); return; }

    struct epoll_event ev, events[2];
    ev.events = EPOLLIN;
    ev.data.fd = tun_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, tun_fd, &ev);
    ev.data.fd = sock_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sock_fd, &ev);

    while (1) {
        /* Periodically check whether outbound peers need (re)connecting */
        time_t now = time(NULL);
        if (now - last_rekey_check >= 10) {
            last_rekey_check = now;
            for (int i = 0; i < peer_config_count; i++) {
                peer_config_t *cfg = &peer_configs[i];
                if (!cfg->has_endpoint) continue;
                peer_session_t *s = find_session_by_pub(cfg->pub);
                int needs = !s || (now >= s->last_handshake + REKEY_INITIATE_SECS && !s->rekeying);
                int ready  = now - cfg->last_attempt >= RECONNECT_INTERVAL_SECS;
                if (needs && ready) {
                    if (s) s->rekeying = 1;
                    if (initiate_outbound_handshake(sock_fd, i, static_key, static_pub, psk_key) < 0)
                        if (s) s->rekeying = 0;
                }
            }
            /* Expire timed-out pending handshakes */
            for (int j = 0; j < MAX_PEERS; j++) {
                pending_hs_t *p = &pending_hs[j];
                if (!p->active || now - p->sent_at < HANDSHAKE_TIMEOUT_SECS) continue;
                char ip_str[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &peer_configs[p->cfg_idx].endpoint.sin_addr, ip_str, sizeof(ip_str));
                LOG_WARN("Handshake to %s:%d timed out — will retry in %ds",
                         ip_str, ntohs(peer_configs[p->cfg_idx].endpoint.sin_port),
                         RECONNECT_INTERVAL_SECS);
                EVP_PKEY_free(p->hs_state.eph_key);
                p->hs_state.eph_key = NULL;
                p->active = 0;
                peer_session_t *ts = find_session_by_pub(peer_configs[p->cfg_idx].pub);
                if (ts) ts->rekeying = 0;
            }
        }

        int nfds = epoll_wait(epoll_fd, events, 2, 5000);
        if (nfds < 0) { LOG_ERROR("epoll_wait: %s", strerror(errno)); break; }

        for (int i = 0; i < nfds; i++) {
            if (events[i].events & (EPOLLERR | EPOLLHUP)) {
                LOG_ERROR("Error/hangup on fd=%d", events[i].data.fd);
                goto done;
            }

            /* TUN -> UDP */
            if (events[i].data.fd == tun_fd) {
                nr = read(tun_fd, buffer, BUFFER_SIZE);
                if (nr <= 0) { LOG_ERROR("TUN read: %s", strerror(errno)); goto done; }
                if (nr >= 20 && (buffer[0] >> 4) == 4) {
                    uint32_t dst_ip = ntohl(*(uint32_t *)(buffer + 16));
                    peer_session_t *s = route_lookup(dst_ip);
                    if (s)
                        forward_to_peer(sock_fd, s, plain_buf, buffer, (int)nr, wire_buf);
                    else {
                        struct in_addr a = { htonl(dst_ip) };
                        LOG_DEBUG("No route for %s — dropping", inet_ntoa(a));
                    }
                }
                continue;
            }

            /* UDP -> TUN or handshake */
            src_addr_len = sizeof(src_addr);
            nr = recvfrom(sock_fd, wire_buf, sizeof(wire_buf), 0,
                          (struct sockaddr *)&src_addr, &src_addr_len);
            if (nr < 0) { LOG_ERROR("recvfrom: %s", strerror(errno)); goto done; }

            now = time(NULL);

            /* Response to a pending outbound handshake — check before inbound path */
            if (nr == hs_size && memcmp(wire_buf, pkt_header, HEADER_SIZE) == 0) {
                int handled = 0;
                for (int j = 0; j < MAX_PEERS; j++) {
                    pending_hs_t *p = &pending_hs[j];
                    if (!p->active) continue;
                    if (p->server_addr.sin_addr.s_addr != src_addr.sin_addr.s_addr ||
                        p->server_addr.sin_port        != src_addr.sin_port) continue;
                    peer_config_t *cfg = &peer_configs[p->cfg_idx];
                    unsigned char session_key[CRYPTO_KEY_LEN];
                    p->active = 0;
                    if (handshake_client_recv(wire_buf, (int)nr, psk_key,
                                              static_key, static_pub, cfg->pub,
                                              &p->hs_state, session_key) == 0) {
                        peer_session_t *s = alloc_session(cfg->pub, &src_addr);
                        if (s) {
                            session_init(s, &src_addr, cfg->pub, session_key, cfg, 1);
                            print_sessions();
                        }
                    } else {
                        peer_session_t *s = find_session_by_pub(cfg->pub);
                        if (s) s->rekeying = 0;
                    }
                    handled = 1;
                    break;
                }
                if (handled) continue;
            }

            /* Inbound handshake */
            if (nr == hs_size && memcmp(wire_buf, pkt_header, HEADER_SIZE) == 0) {
                /* Tie-breaking: if we have a pending outbound to this peer, the side
                 * with the higher pub key wins and ignores the inbound initiation. */
                int skip = 0;
                for (int j = 0; j < MAX_PEERS; j++) {
                    pending_hs_t *p = &pending_hs[j];
                    if (!p->active) continue;
                    if (p->server_addr.sin_addr.s_addr != src_addr.sin_addr.s_addr ||
                        p->server_addr.sin_port        != src_addr.sin_port) continue;
                    if (memcmp(static_pub, peer_configs[p->cfg_idx].pub, DH_PUBKEY_LEN) > 0) {
                        LOG_DEBUG("Rekey collision from %s:%d — ignoring (we have higher pub key)",
                                  inet_ntoa(src_addr.sin_addr), ntohs(src_addr.sin_port));
                        skip = 1;
                    } else {
                        LOG_INFO("Rekey collision from %s:%d — yielding (we have lower pub key)",
                                 inet_ntoa(src_addr.sin_addr), ntohs(src_addr.sin_port));
                        EVP_PKEY_free(p->hs_state.eph_key);
                        p->hs_state.eph_key = NULL;
                        p->active = 0;
                        peer_session_t *s = find_session_by_pub(peer_configs[p->cfg_idx].pub);
                        if (s) s->rekeying = 0;
                    }
                    break;
                }
                if (skip) continue;

                peer_session_t *existing = find_session_by_addr(&src_addr);
                if (existing && now - existing->last_handshake < HANDSHAKE_COOLDOWN_SECS) {
                    LOG_WARN("Handshake cooldown for %s:%d — ignoring",
                             inet_ntoa(src_addr.sin_addr), ntohs(src_addr.sin_port));
                    continue;
                }
                LOG_INFO("Inbound handshake from %s:%d",
                         inet_ntoa(src_addr.sin_addr), ntohs(src_addr.sin_port));
                unsigned char session_key[CRYPTO_KEY_LEN];
                unsigned char peer_pub[DH_PUBKEY_LEN];
                if (handshake_server_respond(sock_fd, wire_buf, (int)nr, &src_addr,
                                             psk_key, static_key, static_pub,
                                             precomp_eph_key, precomp_eph_pub,
                                             peer_pub, session_key) < 0)
                    continue;
                refresh_precomp_eph();
                peer_config_t *cfg = find_peer_config(peer_pub);
                if (!cfg) {
                    char hex[DH_PUBKEY_LEN * 2 + 1];
                    bytes_to_hex(peer_pub, DH_PUBKEY_LEN, hex);
                    LOG_WARN("Unknown peer public key: %s — rejecting", hex);
                    continue;
                }
                peer_session_t *by_pub = find_session_by_pub(peer_pub);
                if (by_pub && now - by_pub->last_handshake < HANDSHAKE_COOLDOWN_SECS) {
                    LOG_WARN("Handshake cooldown for peer key — ignoring");
                    continue;
                }
                peer_session_t *s = alloc_session(peer_pub, &src_addr);
                if (!s) {
                    LOG_WARN("Session table full — rejecting %s:%d",
                             inet_ntoa(src_addr.sin_addr), ntohs(src_addr.sin_port));
                    continue;
                }
                session_init(s, &src_addr, peer_pub, session_key, cfg, 0);
                print_sessions();
                continue;
            }

            /* Data packet */
            peer_session_t *s = find_session_by_addr(&src_addr);
            if (!s) {
                LOG_WARN("Packet from unknown peer %s:%d — dropping",
                         inet_ntoa(src_addr.sin_addr), ntohs(src_addr.sin_port));
                continue;
            }
            s->last_seen = now;
            int plain_len;
            int dec_ok = 0;
            if (decrypt_packet(s->session_key, wire_buf, (int)nr, plain_buf, &plain_len) == 0) {
                dec_ok = 1;
                s->prev_key_active = 0;
            } else if (s->prev_key_active && now <= s->prev_key_expires) {
                if (decrypt_packet(s->prev_session_key, wire_buf, (int)nr, plain_buf, &plain_len) == 0)
                    dec_ok = 1;
            }
            if (!dec_ok) {
                LOG_WARN("Decrypt failed from %s:%d — dropping",
                         inet_ntoa(s->addr.sin_addr), ntohs(s->addr.sin_port));
                continue;
            }
            if (plain_len < HEADER_SIZE + SEQ_SIZE ||
                memcmp(plain_buf, pkt_header, HEADER_SIZE) != 0) {
                LOG_WARN("Bad header from %s:%d — dropping",
                         inet_ntoa(s->addr.sin_addr), ntohs(s->addr.sin_port));
                continue;
            }
            uint64_t seq_be;
            memcpy(&seq_be, plain_buf + HEADER_SIZE, SEQ_SIZE);
            uint64_t seq = be64toh(seq_be);
            if (check_replay(seq, &s->recv_seq_highest, s->recv_seq_window) < 0) {
                LOG_WARN("Replay from %s:%d (seq=%lu) — dropping",
                         inet_ntoa(s->addr.sin_addr), ntohs(s->addr.sin_port), (unsigned long)seq);
                continue;
            }
            int payload_len = plain_len - HEADER_SIZE - SEQ_SIZE;
            if (payload_len == 0) continue;
            const unsigned char *payload = plain_buf + HEADER_SIZE + SEQ_SIZE;
            if (s->route_count > 0 && payload_len >= 20 && (payload[0] >> 4) == 4) {
                uint32_t src_ip = ntohl(*(uint32_t *)(payload + 12));
                if (!check_allowed_src(s, src_ip)) {
                    struct in_addr a = { htonl(src_ip) };
                    LOG_WARN("Source IP %s not in AllowedIPs for %s:%d — dropping",
                             inet_ntoa(a),
                             inet_ntoa(s->addr.sin_addr), ntohs(s->addr.sin_port));
                    continue;
                }
            }
            ssize_t nw = write(tun_fd, payload, payload_len);
            if (nw < 0) { LOG_WARN("TUN write: %s", strerror(errno)); continue; }
        }
    }
done:
    close(epoll_fd);
    LOG_INFO("Event loop terminated");
}

static void start_peer(char *tunnel, int port, const unsigned char *psk_key,
                       EVP_PKEY *static_key, const unsigned char *static_pub) {
    int sock_fd, tun_fd;

    if ((tun_fd = open_tunnel(tunnel)) < 0)
        exit(EXIT_FAILURE);

    if ((sock_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        LOG_ERROR("socket: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    int opt = 1;
    setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in bind_addr = {0};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind_addr.sin_port = htons(port);
    if (bind(sock_fd, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        LOG_ERROR("bind: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    LOG_INFO("Listening on UDP 0.0.0.0:%d", port);

    refresh_precomp_eph();
    event_loop(tun_fd, sock_fd, static_key, static_pub, psk_key);

    close(sock_fd);
    close(tun_fd);
}

const char *program_name;

static void usage(void) {
    fprintf(stderr,
            "Usage: %s -i <iface> [-p <port>] [-K <keyfile>] "
            "-P <pubkey_hex> [-E <ip:port>] [-R <cidr>] [...] "
            "[-k <psk>] [-v] [-h]\n", program_name);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "   -i  tunnel interface name (required)\n");
    fprintf(stderr, "   -p  listen port (default: 5040)\n");
    fprintf(stderr, "   -K  static private key PEM file (default: peer.key)\n");
    fprintf(stderr, "   -P  peer public key hex (repeatable)\n");
    fprintf(stderr, "   -E  peer endpoint ip:port — initiates outbound connection\n");
    fprintf(stderr, "   -R  AllowedIPs CIDR for the preceding -P (repeatable)\n");
    fprintf(stderr, "   -k  pre-shared key\n");
    fprintf(stderr, "   -v  verbose\n");
    fprintf(stderr, "   -h  help\n");
    fprintf(stderr,
            "Example: %s -i lanecove0 -K peer.key "
            "-P <pubB> -E 1.2.3.4:5040 -R 10.9.0.2/32 "
            "-P <pubC> -R 10.9.0.3/32\n", program_name);
    exit(1);
}

int main(int argc, char *argv[]) {
    program_name = argv[0];
    int option;
    int port = 5040;
    char tunnel[IF_NAMESIZE] = {0};
    char keyfile[256] = "peer.key";
    char psk[256] = {0};
    int has_psk = 0;

    while ((option = getopt(argc, argv, "i:p:K:P:E:R:k:vh")) > 0) {
        switch (option) {
            case 'h': usage(); break;
            case 'v': log_level = 1; break;
            case 'i': strncpy(tunnel, optarg, IF_NAMESIZE - 1); break;
            case 'p': port = atoi(optarg); break;
            case 'K': strncpy(keyfile, optarg, sizeof(keyfile) - 1); break;
            case 'P':
                if (peer_config_count >= MAX_PEERS) {
                    fprintf(stderr, "Too many -P entries (max %d)\n", MAX_PEERS);
                    usage();
                }
                if (hex_to_bytes(optarg, peer_configs[peer_config_count].pub, DH_PUBKEY_LEN) < 0) {
                    fprintf(stderr, "Invalid public key hex: %s\n", optarg);
                    usage();
                }
                peer_configs[peer_config_count].route_count = 0;
                peer_configs[peer_config_count].has_endpoint = 0;
                peer_configs[peer_config_count].last_attempt = 0;
                peer_config_count++;
                break;
            case 'E': {
                if (peer_config_count == 0) { fprintf(stderr, "-E must follow -P\n"); usage(); }
                peer_config_t *pc = &peer_configs[peer_config_count - 1];
                char buf[256];
                strncpy(buf, optarg, sizeof(buf) - 1);
                char *colon = strrchr(buf, ':');
                if (!colon) {
                    fprintf(stderr, "Invalid endpoint (expected ip:port): %s\n", optarg);
                    usage();
                }
                *colon = '\0';
                memset(&pc->endpoint, 0, sizeof(pc->endpoint));
                pc->endpoint.sin_family = AF_INET;
                pc->endpoint.sin_port = htons(atoi(colon + 1));
                if (inet_pton(AF_INET, buf, &pc->endpoint.sin_addr) != 1) {
                    fprintf(stderr, "Invalid endpoint IP: %s\n", buf);
                    usage();
                }
                pc->has_endpoint = 1;
                break;
            }
            case 'R': {
                if (peer_config_count == 0) { fprintf(stderr, "-R must follow -P\n"); usage(); }
                peer_config_t *pc = &peer_configs[peer_config_count - 1];
                if (pc->route_count >= MAX_ROUTES_PER_PEER) {
                    fprintf(stderr, "Too many -R entries (max %d)\n", MAX_ROUTES_PER_PEER);
                    usage();
                }
                if (parse_cidr(optarg, &pc->routes[pc->route_count]) < 0) {
                    fprintf(stderr, "Invalid CIDR: %s\n", optarg);
                    usage();
                }
                pc->route_count++;
                break;
            }
            case 'k':
                strncpy(psk, optarg, sizeof(psk) - 1);
                has_psk = 1;
                break;
            default:
                fprintf(stderr, "Unknown option: %c\n", option);
                usage();
        }
    }

    if (*tunnel == '\0') { fprintf(stderr, "Missing -i\n"); usage(); }
    if (peer_config_count == 0) { fprintf(stderr, "At least one -P entry is required\n"); usage(); }

    EVP_PKEY *static_key = NULL;
    unsigned char static_pub[DH_PUBKEY_LEN];
    if (load_static_key(keyfile, &static_key, static_pub) < 0)
        exit(EXIT_FAILURE);

    char pub_hex[DH_PUBKEY_LEN * 2 + 1];
    bytes_to_hex(static_pub, DH_PUBKEY_LEN, pub_hex);
    LOG_INFO("Peer public key: %s", pub_hex);

    for (int i = 0; i < peer_config_count; i++) {
        char hex[DH_PUBKEY_LEN * 2 + 1];
        bytes_to_hex(peer_configs[i].pub, DH_PUBKEY_LEN, hex);
        LOG_INFO("Known peer: %s (%d route(s))%s", hex, peer_configs[i].route_count,
                 peer_configs[i].has_endpoint ? " [outbound]" : " [inbound-only]");
    }

    unsigned char psk_key[CRYPTO_KEY_LEN];
    if (has_psk) {
        derive_key(psk, psk_key);
        LOG_INFO("PSK set — handshake will be authenticated");
    } else {
        LOG_WARN("No PSK — handshake unauthenticated (MITM-vulnerable)");
    }

    start_peer(tunnel, port, has_psk ? psk_key : NULL, static_key, static_pub);
    EVP_PKEY_free(static_key);
    return 0;
}
