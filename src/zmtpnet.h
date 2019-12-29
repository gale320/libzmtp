#ifndef __ZMTPNET_H__
#define __ZMTPNET_H__

#include <unistd.h>

#include <stddef.h>
#include <stdint.h> 
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>

#include <sys/types.h>
#include <sys/socket.h>

int s_tcp_send (int fd, const void *data, size_t len);

int s_tcp_recv (int fd, void *buffer, size_t len);

#endif
