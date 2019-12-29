#include "zmtpnet.h"

int
zmtp_tcp_send (int fd, const void *data, size_t len)
{
    size_t bytes_sent = 0;
    while (bytes_sent < len) {
        const ssize_t rc = send (
            fd, (char *) data + bytes_sent, len - bytes_sent, 0);
        if (rc == -1 && errno == EINTR)
            continue;
        if (rc == -1)
            return -1;
        bytes_sent += rc;
    }
    return 0;
}

int
zmtp_tcp_recv (int fd, void *buffer, size_t len)
{
    size_t bytes_read = 0;
    while (bytes_read < len) {
        const ssize_t n = recv (
            fd, (char *) buffer + bytes_read, len - bytes_read, 0);
        if (n == -1 && errno == EINTR)
            continue;
        if (n == -1 || n == 0)
            return -1;
        bytes_read += n;
    }
    return 0;
}


int zmtp_udp_send (int fd, const void *data, size_t len)
{
      size_t bytes_sent = 0;
    while (bytes_sent < len) {
        const ssize_t rc = send (
            fd, (char *) data + bytes_sent, len - bytes_sent, 0);
        if (rc == -1 && errno == EINTR)
            continue;
        if (rc == -1)
            return -1;
        bytes_sent += rc;
    }
    return 0;  
}

int zmtp_udp_recv (int fd, void *buffer, size_t len)
{

        size_t bytes_read = 0;
    while (bytes_read < len) {
        const ssize_t n = recv (
            fd, (char *) buffer + bytes_read, len - bytes_read, 0);
        if (n == -1 && errno == EINTR)
            continue;
        if (n == -1 || n == 0)
            return -1;
        bytes_read += n;
    }
    return 0;
}
