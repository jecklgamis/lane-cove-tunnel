#include "tcp_common.h"

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
        if ((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            LOG_ERROR("Unable to open socket : %s", strerror(errno));
            exit(EXIT_FAILURE);
        }

        if (connect(sock_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
            LOG_ERROR("Unable to connect : %s", strerror(errno));
            close(sock_fd);
            exit(EXIT_FAILURE);
        }

        LOG_INFO("Connected to %s:%d", ip_addr, port);
        event_loop(tun_fd, sock_fd);

        LOG_INFO("Disconnected from %s:%d", ip_addr, port);
        close(sock_fd);
    }

    close(tun_fd);
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
    fprintf(stderr, "Example : %s -i tun2 -s 10.9.0.2 -p 5050", program_name);
    fprintf(stderr, "\n");
    exit(1);
}

int main(int argc, char *argv[]) {
    int option;
    int server_port = 5050;
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

    if (argc > 0) {
        usage();
    }
    if (*tunnel_name == '\0' || *server_ip == '\0') {
        usage();
    }
    start_client(tunnel_name, server_ip, server_port);
}
