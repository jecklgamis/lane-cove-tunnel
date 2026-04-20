#include "udp_common.h"

int log_level = 0;

static int alloc_tunnel(char *dev, int flags) {
    struct ifreq ifr;
    int tun_fd, ret_val;

    if ((tun_fd = open("/dev/net/tun", O_RDWR)) < 0) {
        LOG_ERROR("Unable to open tunnel : %s", strerror(errno));
        return tun_fd;
    }
    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = flags;
    if (*dev)
        strncpy(ifr.ifr_name, dev, IFNAMSIZ);
    if ((ret_val = ioctl(tun_fd, TUNSETIFF, (void *) &ifr)) < 0) {
        LOG_ERROR("Unable to issue ioctl on %d : %s", tun_fd, strerror(errno));
        close(tun_fd);
        return ret_val;
    }
    strcpy(dev, ifr.ifr_name);
    return tun_fd;
}

int open_tunnel(char *tunnel) {
    int tun_fd;
    if ((tun_fd = alloc_tunnel(tunnel, IFF_TUN | IFF_NO_PI)) < 0) {
        LOG_ERROR("Unable to connect to tunnel %s", tunnel);
        return tun_fd;
    }
    LOG_INFO("Opened tunnel %s", tunnel);
    return tun_fd;
}
