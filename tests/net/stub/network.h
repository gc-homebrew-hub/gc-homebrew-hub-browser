/* Host stand-in for libogc2's network.h: net_* map onto BSD sockets. */
#pragma once
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdint.h>

typedef int32_t s32;
typedef uint32_t u32;
typedef uint64_t u64;

/* lwIP's sockaddr_in has sin_len; glibc's does not */
#define sin_len sin_zero[0]

#define net_socket socket
#define net_connect connect
#define net_close close
#define net_select select
#define net_sendto sendto
#define net_recvfrom recvfrom
#define net_write(s, b, n) write((s), (b), (n))
#define net_read(s, b, n) read((s), (b), (n))

/* DHCP is the host's business; no gateway means public DNS servers */
static inline s32 if_config(char *ip, char *mask, char *gw, int use_dhcp, ...)
{
    (void) use_dhcp;
    ip[0] = mask[0] = gw[0] = 0;
    return 0;
}
