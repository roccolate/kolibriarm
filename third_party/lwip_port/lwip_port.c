#include "kolibri_net.h"

#include "drivers/board.h"
#include "drivers/net/virtio_net.h"
#include "kernel/mm/kheap.h"
#include "drivers/uart/pl011.h"

#include <stddef.h>
#include <stdint.h>

#define ETH_TYPE_IP   0x0800
#define ETH_TYPE_ARP  0x0806

#define IP_PROTO_UDP  17

static kolibri_net_info_t g_net_info;
static virtio_net_device_t g_net_dev;
static uint8_t g_net_inited = 0;
static uint32_t g_dhcp_xid = 0x12345678;

static void memcpy32(void *dst, const void *src, uint32_t len) {
    uint32_t *d = (uint32_t *)dst;
    const uint32_t *s = (const uint32_t *)src;
    for (uint32_t i = 0; i < (len + 3) / 4; i++) {
        d[i] = s[i];
    }
}

static int memcmp(const void *a, const void *b, uint32_t len) {
    const uint8_t *p = (const uint8_t *)a;
    const uint8_t *q = (const uint8_t *)b;
    for (uint32_t i = 0; i < len; i++) {
        if (p[i] != q[i]) {
            return 1;
        }
    }
    return 0;
}

static void memset(void *dst, uint8_t val, uint32_t len) {
    uint8_t *d = (uint8_t *)dst;
    for (uint32_t i = 0; i < len; i++) {
        d[i] = val;
    }
}

static void set_ip_addr(uint8_t *addr, uint32_t ip) {
    addr[0] = ip & 0xFF;
    addr[1] = (ip >> 8) & 0xFF;
    addr[2] = (ip >> 16) & 0xFF;
    addr[3] = (ip >> 24) & 0xFF;
}

static uint32_t get_ip_addr(const uint8_t *addr) {
    return addr[0] | (addr[1] << 8) | (addr[2] << 16) | (addr[3] << 24);
}

static uint16_t checksum(void *data, uint32_t len) {
    uint16_t *p = (uint16_t *)data;
    uint32_t sum = 0;

    while (len > 1) {
        sum += *p++;
        len -= 2;
    }

    if (len > 0) {
        sum += *(uint8_t *)p;
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return (uint16_t)~sum;
}

static void print_ip(uint32_t ip) {
    uart_puts("IP=");
    uart_putc('0' + ((ip >> 0) & 0xFF));
    uart_putc('.');
    uart_putc('0' + ((ip >> 8) & 0xFF));
    uart_putc('.');
    uart_putc('0' + ((ip >> 16) & 0xFF));
    uart_putc('.');
    uart_putc('0' + ((ip >> 24) & 0xFF));
}

static int send_eth_frame(const void *data, uint32_t len, uint16_t eth_type) {
    uint8_t frame[1536];
    uint8_t *src_mac = g_net_dev.mac;
    uint8_t broadcast[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    if (len + 14 > sizeof(frame)) {
        return -1;
    }

    memcpy32(frame, broadcast, 6);
    memcpy32(frame + 6, src_mac, 6);

    frame[12] = (eth_type >> 8) & 0xFF;
    frame[13] = eth_type & 0xFF;

    memcpy32(frame + 14, data, len);

    return virtio_net_send(&g_net_dev, frame, len + 14);
}

static int send_ip_packet(const void *data, uint32_t len,
                          uint32_t dest_ip, uint8_t proto) {
    uint8_t packet[1536];
    uint8_t src_ip[4];

    if (g_net_info.ip == 0) {
        src_ip[0] = 0; src_ip[1] = 0; src_ip[2] = 0; src_ip[3] = 0;
    } else {
        set_ip_addr(src_ip, g_net_info.ip);
    }

    uint8_t broadcast[] = {255, 255, 255, 255};

    if (len + 20 > sizeof(packet)) {
        return -1;
    }

    memset(packet, 0, 20);
    packet[0] = 0x45;
    packet[1] = 0;
    *(uint16_t *)&packet[2] = (uint16_t)((len + 20) << 8) | ((len + 20) >> 8);
    *(uint32_t *)&packet[4] = 0;
    packet[8] = 64;
    packet[9] = proto;
    *(uint16_t *)&packet[10] = 0;
    memcpy32(&packet[12], src_ip, 4);
    if (dest_ip == 0) {
        memcpy32(&packet[16], broadcast, 4);
    } else {
        set_ip_addr(&packet[16], dest_ip);
    }
    *(uint16_t *)&packet[10] = checksum(packet, 20);

    memcpy32(packet + 20, data, len);

    return send_eth_frame(packet, len + 20, ETH_TYPE_IP);
}

static int send_udp_packet(const void *data, uint32_t len,
                           uint32_t dest_ip, uint16_t dest_port,
                           uint16_t src_port) {
    uint8_t packet[1536];
    uint8_t udp_header[8];

    if (len + 8 > sizeof(packet)) {
        return -1;
    }

    udp_header[0] = (src_port >> 8) & 0xFF;
    udp_header[1] = src_port & 0xFF;
    udp_header[2] = (dest_port >> 8) & 0xFF;
    udp_header[3] = dest_port & 0xFF;
    udp_header[4] = ((len + 8) >> 8) & 0xFF;
    udp_header[5] = (len + 8) & 0xFF;
    udp_header[6] = 0;
    udp_header[7] = 0;

    memcpy32(packet, udp_header, 8);
    memcpy32(packet + 8, data, len);

    return send_ip_packet(packet, len + 8, dest_ip, IP_PROTO_UDP);
}

static void send_dhcp_discover(void) {
    dhcp_packet_t pkt;
    uint8_t options[4] = {0x63, 0x82, 0x53, 0x63};

    memset(&pkt, 0, sizeof(pkt));
    pkt.op = 1;
    pkt.htype = 1;
    pkt.hlen = 6;
    pkt.xid = g_dhcp_xid;
    pkt.flags = 0x8000;

    memcpy32(pkt.chaddr, g_net_dev.mac, 6);

    memcpy32(pkt.options, options, 4);
    pkt.options[4] = 53;
    pkt.options[5] = 1;
    pkt.options[6] = DHCP_DISCOVER;
    pkt.options[7] = 0xFF;

    send_udp_packet(&pkt, sizeof(dhcp_packet_t), 0xFFFFFFFF, DHCP_PORT_SERVER, DHCP_PORT_CLIENT);
}

static void send_dhcp_request(uint32_t server_ip, uint32_t requested_ip) {
    dhcp_packet_t pkt;
    uint8_t options[4] = {0x63, 0x82, 0x53, 0x63};
    uint8_t opt_idx = 4;

    memset(&pkt, 0, sizeof(pkt));
    pkt.op = 1;
    pkt.htype = 1;
    pkt.hlen = 6;
    pkt.xid = g_dhcp_xid;
    pkt.flags = 0x8000;

    set_ip_addr(pkt.ciaddr, requested_ip);
    memcpy32(pkt.chaddr, g_net_dev.mac, 6);

    memcpy32(pkt.options, options, 4);

    pkt.options[opt_idx++] = 53;
    pkt.options[opt_idx++] = 1;
    pkt.options[opt_idx++] = DHCP_REQUEST;

    pkt.options[opt_idx++] = 54;
    pkt.options[opt_idx++] = 4;
    set_ip_addr(&pkt.options[opt_idx], server_ip);
    opt_idx += 4;

    pkt.options[opt_idx++] = 50;
    pkt.options[opt_idx++] = 4;
    set_ip_addr(&pkt.options[opt_idx], requested_ip);
    opt_idx += 4;

    pkt.options[opt_idx++] = 0xFF;

    send_udp_packet(&pkt, sizeof(dhcp_packet_t), 0xFFFFFFFF, DHCP_PORT_SERVER, DHCP_PORT_CLIENT);
}

static void parse_dhcp_offer(dhcp_packet_t *pkt, uint32_t len) {
    (void)len;

    if (pkt->op != 2 || pkt->xid != g_dhcp_xid) {
        return;
    }

    g_net_info.ip = get_ip_addr(pkt->yiaddr);

    uint8_t *opt = pkt->options;
    while (opt < pkt->options + DHCP_OPTIONS_LEN - 1) {
        if (opt[0] == 0xFF) {
            break;
        }
        if (opt[0] == 0) {
            opt++;
            continue;
        }
        if (opt[0] == 53 && opt[2] == DHCP_OFFER) {
            g_net_info.dhcp_state = DHCP_OFFER;
        }
        if (opt[0] == 54) {
            g_net_info.dhcp_state = DHCP_REQUEST;
        }
        opt += 2 + opt[1];
    }

    uart_puts("[net] DHCP offer: ");
    print_ip(g_net_info.ip);
    uart_puts("\n");
}

static void parse_dhcp_ack(dhcp_packet_t *pkt, uint32_t len) {
    (void)len;

    if (pkt->op != 2 || pkt->xid != g_dhcp_xid) {
        return;
    }

    g_net_info.ip = get_ip_addr(pkt->yiaddr);

    uint8_t *opt = pkt->options;
    while (opt < pkt->options + DHCP_OPTIONS_LEN - 1) {
        if (opt[0] == 0xFF) {
            break;
        }
        if (opt[0] == 0) {
            opt++;
            continue;
        }
        if (opt[0] == 53 && opt[2] == DHCP_ACK) {
            g_net_info.dhcp_state = DHCP_ACK;
            g_net_info.discovered = 1;
        }
        if (opt[0] == 1) {
            g_net_info.subnet = get_ip_addr(&opt[2]);
        }
        if (opt[0] == 3) {
            g_net_info.gateway = get_ip_addr(&opt[2]);
        }
        if (opt[0] == 6) {
            g_net_info.dns = get_ip_addr(&opt[2]);
        }
        opt += 2 + opt[1];
    }

    uart_puts("[net] DHCP ack: ");
    print_ip(g_net_info.ip);
    uart_puts(" gw=");
    print_ip(g_net_info.gateway);
    uart_puts("\n");
}

static void parse_udp_packet(uint8_t *data, uint32_t len) {
    uint16_t dest_port;
    dhcp_packet_t *pkt;

    if (len < 8) {
        return;
    }

    dest_port = (data[2] << 8) | data[3];

    if (dest_port != DHCP_PORT_CLIENT) {
        return;
    }

    pkt = (dhcp_packet_t *)(data + 8);
    len -= 8;

    if (len < sizeof(dhcp_packet_t)) {
        return;
    }

    if (pkt->op == 2) {
        uint8_t *opt = pkt->options;
        while (opt < pkt->options + DHCP_OPTIONS_LEN - 1) {
            if (opt[0] == 0xFF) {
                break;
            }
            if (opt[0] == 0) {
                opt++;
                continue;
            }
            if (opt[0] == 53) {
                if (opt[2] == DHCP_OFFER) {
                    parse_dhcp_offer(pkt, len);
                } else if (opt[2] == DHCP_ACK) {
                    parse_dhcp_ack(pkt, len);
                }
                break;
            }
            opt += 2 + opt[1];
        }
    }
}

static void parse_ip_packet(uint8_t *data, uint32_t len) {
    uint8_t proto;

    if (len < 20) {
        return;
    }

    if ((data[0] & 0xF0) != 0x40) {
        return;
    }

    proto = data[9];

    if (proto == IP_PROTO_UDP) {
        parse_udp_packet(data + 20, len - 20);
    }
}

static void parse_eth_frame(uint8_t *data, uint32_t len) {
    uint16_t eth_type;

    if (len < 14) {
        return;
    }

    if (data[0] == 0xFF && data[1] == 0xFF && data[2] == 0xFF &&
        data[3] == 0xFF && data[4] == 0xFF && data[5] == 0xFF) {
    }

    if (memcmp(data, g_net_dev.mac, 6) != 0 && memcmp(data, "\xFF\xFF\xFF\xFF\xFF\xFF", 6) != 0) {
        return;
    }

    eth_type = (data[12] << 8) | data[13];

    if (eth_type == ETH_TYPE_IP) {
        parse_ip_packet(data + 14, len - 14);
    }
}

int kolibri_net_init(void) {
    uint64_t net_base;
    virtio_net_info_t net_info;

    uart_puts("[net] probing for virtio-net...\n");

    if (virtio_net_probe_range(board_virtio_mmio_base(),
                               board_virtio_mmio_size(),
                               board_virtio_mmio_stride(),
                               &net_base, &net_info) != 0) {
        uart_puts("[net] virtio-net not found\n");
        return -1;
    }

    uart_puts("[net] found at ");
    uart_put_hex(net_base);
    uart_puts(" MAC=");
    for (int i = 0; i < 6; i++) {
        if (i > 0) uart_putc(':');
        uart_put_hex_byte(net_info.mac[i]);
    }
    uart_puts("\n");

    if (virtio_net_init(&g_net_dev, net_base) != 0) {
        uart_puts("[net] failed to initialize virtio-net\n");
        return -1;
    }

    g_net_info.discovered = 0;
    g_net_info.dhcp_state = 0;
    g_net_info.ip = 0;
    g_net_info.subnet = 0;
    g_net_info.gateway = 0;
    g_net_info.dns = 0;
    memcpy32(g_net_info.mac, net_info.mac, 6);

    g_net_inited = 1;

    uart_puts("[net] network initialized, starting DHCP...\n");

    g_dhcp_xid = 0x12345678 + (uint32_t)((uintptr_t)&g_net_dev & 0xFFFF);
    send_dhcp_discover();

    return 0;
}

void kolibri_net_poll(void) {
    uint8_t buf[1536];
    int len;

    if (!g_net_inited || !g_net_dev.ready) {
        return;
    }

    while ((len = virtio_net_recv(&g_net_dev, buf, sizeof(buf))) > 0) {
        parse_eth_frame(buf, (uint32_t)len);
    }

    if (!g_net_info.discovered) {
        if (g_net_info.dhcp_state == DHCP_OFFER) {
            send_dhcp_request(0, g_net_info.ip);
        }
    }
}

uint8_t *kolibri_net_get_mac(void) {
    if (!g_net_inited) {
        return NULL;
    }
    return g_net_info.mac;
}

uint32_t kolibri_net_get_ip(void) {
    return g_net_info.ip;
}

int kolibri_net_is_link_up(void) {
    return g_net_inited;
}

kolibri_net_info_t *kolibri_net_get_info(void) {
    if (!g_net_inited) {
        return NULL;
    }
    return &g_net_info;
}

int kolibri_net_send_udp(const void *data, uint32_t len,
                         uint32_t dest_ip, uint16_t dest_port) {
    if (!g_net_inited) {
        return -1;
    }
    return send_udp_packet(data, len, dest_ip, dest_port, DHCP_PORT_CLIENT);
}

int kolibri_net_recv_udp(void *data, uint32_t max_len,
                         uint32_t *src_ip, uint16_t *src_port) {
    (void)data;
    (void)max_len;
    (void)src_ip;
    (void)src_port;
    return -1;
}