#include "tcp_common.h"

lmk_logger *logger;

int init_logger(int log_level) {
    logger = lmk_get_logger("logger");
    lmk_set_log_level(logger, log_level);
    lmk_log_handler *console = lmk_get_console_log_handler();
    lmk_log_handler *file = lmk_get_file_log_handler("file", "tcp_client.log");
    lmk_attach_log_handler(logger, console);
    lmk_attach_log_handler(logger, file);
}

void start_client(char *tunnel, char *ip_addr, int port) {
    struct sockaddr_in server_addr;
    int sock_fd, tun_fd;
    int socket_opts = 1;

    if ((tun_fd = open_tunnel(tunnel)) < 0) {
        exit(EXIT_FAILURE);
    }

    if ((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        LMK_LOG_ERROR(logger, "Unable to open socket");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(ip_addr);
    server_addr.sin_port = htons(port);

    if (connect(sock_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        LMK_LOG_ERROR(logger, "Unable to connect");
        exit(EXIT_FAILURE);
    }
    LMK_LOG_INFO(logger, "Connected to %s:%d", inet_ntoa(server_addr.sin_addr), port);
    event_loop(logger, tun_fd, sock_fd);
}

const char *program_name;

void usage() {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "%s -i <tunnel> -s <server-ip> -p <port> [-d] [-h]\n", program_name);
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
    char remote_ip[16] = "";
    memset(remote_ip, 0, 16);

    while ((option = getopt(argc, argv, "i:s:p:hd")) > 0) {
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
            case 's':
                strncpy(remote_ip, optarg, 15);
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
    if (*tunnel_name == '\0' || *remote_ip == '\0') {
        usage();
    }
    init_logger(debug ? LMK_LOG_LEVEL_DEBUG : LMK_LOG_LEVEL_INFO);
    start_client(tunnel_name, remote_ip, port);
}


