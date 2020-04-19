#include "tcp_common.h"

lmk_logger *logger;

int init_logger(int log_level) {
    logger = lmk_get_logger("logger");
    lmk_set_log_level(logger, log_level);
    lmk_log_handler *console = lmk_get_console_log_handler();
    lmk_log_handler *file = lmk_get_file_log_handler("file", "tcp_server.log");
    lmk_attach_log_handler(logger, console);
    lmk_attach_log_handler(logger, file);
}

void start_server(char *tunnel, char *host, int port) {
    struct sockaddr_in server_addr, remote_addr;
    int server_sock_fd, client_sock_fd, tun_fd;
    int socket_opts = 1;

    if ((tun_fd = open_tunnel(tunnel)) < 0) {
        exit(EXIT_FAILURE);
    }

    if ((server_sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        LMK_LOG_ERROR(logger, "Unable to open socket : %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_sock_fd, SOL_SOCKET, SO_REUSEADDR, (char *) &socket_opts, sizeof(socket_opts)) < 0) {
        LMK_LOG_ERROR(logger, "Unable to set socket option : %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(port);

    if (bind(server_sock_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        LMK_LOG_ERROR(logger, "Unable to bind address : %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    int backlog = 10;
    LMK_LOG_INFO(logger, "Started TCP server on %s:%d", host, port);
    if (listen(server_sock_fd, backlog) < 0) {
        LMK_LOG_ERROR(logger, "Unable to listen from socket : %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    int remote_addr_len = sizeof(remote_addr);
    memset(&remote_addr, 0, remote_addr_len);

    if ((client_sock_fd = accept(server_sock_fd, (struct sockaddr *) &remote_addr, &remote_addr_len)) < 0) {
        LMK_LOG_ERROR(logger, "accept() failed : %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    LMK_LOG_INFO(logger, "Received connection from %s:%d, fd = %d", inet_ntoa(remote_addr.sin_addr),
                 ntohs(remote_addr.sin_port), client_sock_fd);

    event_loop(logger, tun_fd, client_sock_fd);
}

const char *program_name;

void usage() {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "%s -i <tunnel> -p <port> [-d] [-h]\n", program_name);
    fprintf(stderr, "%s -h\n", program_name);
    fprintf(stderr, "\n");
    exit(1);
}

int main(int argc, char *argv[]) {
    int option;
    int debug = 0;
    int port = 5050;
    program_name = argv[0];
    char tunnel_name[IF_NAMESIZE];
    memset(tunnel_name, 0, IF_NAMESIZE);

    while ((option = getopt(argc, argv, "i:p:hd")) > 0) {
        switch (option) {
            case 'h':
                usage();
                break;
            case 'd':
                debug = 1;
                break;
            case 'i':
                strncpy(tunnel_name, optarg, IFNAMSIZ - 1);
                break;
            case 'p':
                port = atoi(optarg);
                break;
            default:
                fprintf(stderr, "Unknown option %c\n", option);
                usage();
        }
    }

    argv += optind;
    argc -= optind;

    if (argc > 0) {
        usage(program_name);
    }
    if (*tunnel_name == '\0') {
        usage();
    }
    init_logger(debug ? LMK_LOG_LEVEL_DEBUG : LMK_LOG_LEVEL_INFO);
    start_server(tunnel_name, "0.0.0.0", port);
}


