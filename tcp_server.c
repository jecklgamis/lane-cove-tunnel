#include "tcp_common.h"

void start_server(char *tunnel, int port) {
    struct sockaddr_in server_addr, remote_addr;
    int server_sock_fd, client_sock_fd, tun_fd;
    int socket_opts = 1;

    if ((tun_fd = open_tunnel(tunnel)) < 0) {
        exit(EXIT_FAILURE);
    }

    if ((server_sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        LOG_ERROR("Unable to open socket : %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_sock_fd, SOL_SOCKET, SO_REUSEADDR, (char *) &socket_opts, sizeof(socket_opts)) < 0) {
        LOG_ERROR("Unable to set socket option : %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(port);

    if (bind(server_sock_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        LOG_ERROR("Unable to bind address : %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    int backlog = 10;
    if (listen(server_sock_fd, backlog) < 0) {
        LOG_ERROR("Unable to listen from socket : %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    LOG_INFO("Started TCP server on 0.0.0.0:%d", port);

    while (1) {
        socklen_t remote_addr_len = sizeof(remote_addr);
        memset(&remote_addr, 0, remote_addr_len);

        if ((client_sock_fd = accept(server_sock_fd, (struct sockaddr *) &remote_addr, &remote_addr_len)) < 0) {
            LOG_ERROR("accept() failed : %s", strerror(errno));
            break;
        }

        LOG_INFO("Received connection from %s:%d, fd = %d", inet_ntoa(remote_addr.sin_addr),
                 ntohs(remote_addr.sin_port), client_sock_fd);

        event_loop(tun_fd, client_sock_fd);

        LOG_INFO("Connection closed, waiting for next client");
        close(client_sock_fd);
    }

    close(server_sock_fd);
}

const char *program_name;

void usage() {
    fprintf(stderr, "Usage : %s -i <tunnel-interface> -p [port] [-v] [-h]\n", program_name);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "   -i tunnel interface\n");
    fprintf(stderr, "   -p server port\n");
    fprintf(stderr, "   -v verbose\n");
    fprintf(stderr, "   -h print this help message\n");
    fprintf(stderr, "Example : %s -i lanecove -p 5050", program_name);
    fprintf(stderr, "\n");
    exit(1);
}

int main(int argc, char *argv[]) {
    program_name = argv[0];
    int option;
    int server_port = 5050;
    char tunnel_name[IF_NAMESIZE];

    memset(tunnel_name, 0, IF_NAMESIZE);

    while ((option = getopt(argc, argv, "i:p:vh")) > 0) {
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
            default:
                fprintf(stderr, "Unknown option %c\n", option);
                usage();
        }
    }

    argv += optind;
    argc -= optind;

    if (argc > 0) {
        usage();
    }
    if (*tunnel_name == '\0') {
        usage();
    }
    start_server(tunnel_name, server_port);
}
