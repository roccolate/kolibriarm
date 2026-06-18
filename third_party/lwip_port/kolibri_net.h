#ifndef KOLIBRI_NET_H
#define KOLIBRI_NET_H

#include <stdint.h>

#define DHCP_DISCOVER 1
#define DHCP_OFFER     2
#define DHCP_REQUEST   3
#define DHCP_ACK       5

#define DHCP_PORT_SERVER 67
#define DHCP_PORT_CLIENT 68

#define DHCP_CHADDR_LEN 16
#define DHCP_SNAME_LEN  64
#define DHCP_FILE_LEN   128
#define DHCP_OPTIONS_LEN 312

typedef struct {
    uint8_t  op;
    uint8_t  htype;
    uint8_t  hlen;
    uint8_t  hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint8_t  ciaddr[4];
    uint8_t  yiaddr[4];
    uint8_t  siaddr[4];
    uint8_t  giaddr[4];
    uint8_t  chaddr[DHCP_CHADDR_LEN];
    uint8_t  sname[DHCP_SNAME_LEN];
    uint8_t  file[DHCP_FILE_LEN];
    uint8_t  options[DHCP_OPTIONS_LEN];
} __attribute__((packed)) dhcp_packet_t;

typedef struct {
    uint8_t      mac[6];
    uint32_t     ip;
    uint32_t     subnet;
    uint32_t     gateway;
    uint32_t     dns;
    uint8_t      dhcp_state;
    uint8_t      discovered;
} kolibri_net_info_t;

int kolibri_net_init(void);
void kolibri_net_poll(void);
uint8_t *kolibri_net_get_mac(void);
uint32_t kolibri_net_get_ip(void);
int kolibri_net_is_link_up(void);
kolibri_net_info_t *kolibri_net_get_info(void);

int kolibri_net_send_udp(const void *data, uint32_t len,
                         uint32_t dest_ip, uint16_t dest_port);
int kolibri_net_recv_udp(void *data, uint32_t max_len,
                         uint32_t *src_ip, uint16_t *src_port);

#endif