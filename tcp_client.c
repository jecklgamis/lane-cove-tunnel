#include "tcp_common.h"

lmk_logger *logger;

int init_logger() {
    logger = lmk_get_logger("logger");
    lmk_set_log_level(logger, LMK_LOG_LEVEL_INFO);
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

int main(int argc, char *argv[]) {
    init_logger();
    char tunnel_name[IF_NAMESIZE];
    memset(tunnel_name, 0, IF_NAMESIZE);
    strcpy(tunnel_name, "tun2");
    start_client(tunnel_name, "68.183.181.146", 5050);
}


