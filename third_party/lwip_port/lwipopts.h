#ifndef LWIPOPTS_H
#define LWIPOPTS_H

#include <stdint.h>

#define NO_SYS                  1
#define LWIP_SOCKET             0
#define LWIP_NETCONN            0
#define LWIP_RAW                1

#define MEM_ALIGNMENT           8
#define MEM_LIBC_MALLOC         0
#define MEM_USE_POOLS           0
#define MEM_CUSTOM_ALLOCATOR    1

#define MEM_SIZE                (32 * 1024)
#define MEMP_NUM_PBUF           16
#define MEMP_NUM_RAW_PCB        4
#define MEMP_NUM_UDP_PCB        8
#define MEMP_NUM_TCP_PCB        8
#define MEMP_NUM_TCP_PCB_LISTEN 4
#define MEMP_NUM_TCP_SEG        32
#define MEMP_NUM_NETBUF         8
#define MEMP_NUM_NETCONN        8

#define PBUF_POOL_SIZE          16
#define PBUF_POOL_BUFSIZE       1280

#define LWIP_IPV4               1
#define LWIP_IPV6               0

#define TCP_MSS                 1280
#define TCP_WND                 (4 * TCP_MSS)
#define TCP_SND_BUF             (4 * TCP_MSS)
#define TCP_SND_QUEUELEN        (4 * TCP_SND_BUF / TCP_MSS)

#define LWIP_TCP_KEEPALIVE      1

#define DEFAULT_NETIF_MTU       1280

#define LWIP_IGMP               0
#define LWIP_ICMP               1
#define LWIP_DHCP               1
#define LWIP_DNS                1

#define LWIP_STATS              0
#define LWIP_STATS_DISPLAY      0

#define LWIP_DEBUG              0

#define TCPIP_THREAD_NAME       "tcpip"
#define DEFAULT_THREAD_NAME     "lwip"

#define BYTE_ORDER              LITTLE_ENDIAN

#endif