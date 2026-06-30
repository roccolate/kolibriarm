/*
 * Minimal from-scratch DHCP/ARP client for ArmoniOS.
 *
 * Speaks just enough UDP/IP/Ethernet to obtain an IPv4 lease from a
 * QEMU -netdev user backend. Implemented directly on top of
 * drivers/net/virtio_net.c with no external protocol stack.
 */
#include "kernel/net/dhcp.h"

#include "drivers/board.h"
#include "drivers/net/virtio_net.h"
#include "drivers/uart/pl011.h"
#include "kernel/kstring.h"
#include "kernel/net/dhcp_options.h"
#include "kernel/print.h"

#include <stddef.h>
#include <stdint.h>

#define ETH_TYPE_IP   0x0800
#define ETH_TYPE_ARP  0x0806
#define IP_PROTO_UDP  17
#define NET_FRAME_MAX 1536U
#define ETH_HEADER_LEN 14U
#define IPV4_HEADER_LEN 20U
#define UDP_HEADER_LEN 8U

static net_info_t g_net_info;
static virtio_net_device_t g_net_dev;
static uint8_t g_net_inited = 0;
static uint8_t g_net_polling = 0;
static uint32_t g_dhcp_xid = 0x12345678;
static uint8_t g_net_rx_frame[NET_FRAME_MAX] KERNEL_ALIGNED(8);
static uint8_t g_net_tx_frame[NET_FRAME_MAX] KERNEL_ALIGNED(8);


static void set_ip_addr(uint8_t *addr, uint32_t ip) {
    addr[0] = ip & 0xFF;
    addr[1] = (ip >> 8) & 0xFF;
    addr[2] = (ip >> 16) & 0xFF;
    addr[3] = (ip >> 24) & 0xFF;
}

static uint32_t get_ip_addr(const uint8_t *addr) {
    return addr[0] | (addr[1] << 8) | (addr[2] << 16) | (addr[3] << 24);
}

static void put_be16(uint8_t *dst, uint16_t value) {
    dst[0] = (uint8_t)((value >> 8) & 0xffU);
    dst[1] = (uint8_t)(value & 0xffU);
}

static uint16_t checksum(const void *data, uint32_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t sum = 0;

    while (len > 1) {
        sum += ((uint32_t)p[0] << 8) | p[1];
        p += 2;
        len -= 2;
    }

    if (len > 0) {
        sum += ((uint32_t)p[0] << 8);
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return (uint16_t)~sum;
}

static void set_dhcp_broadcast_flag(dhcp_packet_t *pkt) {
    uint8_t *flags = (uint8_t *)(void *)&pkt->flags;

    flags[0] = 0x80;
    flags[1] = 0x00;
}

static void print_ip(uint32_t ip) {
    uart_puts("IP=");
    print_dec64((ip >> 0) & 0xFF);
    uart_putc('.');
    print_dec64((ip >> 8) & 0xFF);
    uart_putc('.');
    print_dec64((ip >> 16) & 0xFF);
    uart_putc('.');
    print_dec64((ip >> 24) & 0xFF);
}

static dhcp_packet_t *tx_dhcp_packet(void) {
    return (dhcp_packet_t *)(void *)(g_net_tx_frame + ETH_HEADER_LEN +
                                     IPV4_HEADER_LEN + UDP_HEADER_LEN);
}

static void write_eth_header(uint8_t *frame, uint16_t eth_type) {
    static const uint8_t broadcast[6] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
    };

    kmemcpy(frame, broadcast, sizeof(broadcast));
    kmemcpy(frame + 6, g_net_dev.mac, 6);

    frame[12] = (eth_type >> 8) & 0xFF;
    frame[13] = eth_type & 0xFF;
}

static void write_ipv4_header(uint8_t *packet, uint32_t payload_len,
                              uint32_t dest_ip, uint8_t proto) {
    uint32_t total_len = IPV4_HEADER_LEN + payload_len;

    kmemset(packet, 0, IPV4_HEADER_LEN);
    packet[0] = 0x45;
    packet[1] = 0;
    put_be16(&packet[2], (uint16_t)total_len);
    packet[8] = 64;
    packet[9] = proto;
    if (g_net_info.ip == 0) {
        packet[12] = 0;
        packet[13] = 0;
        packet[14] = 0;
        packet[15] = 0;
    } else {
        set_ip_addr(&packet[12], g_net_info.ip);
    }
    if (dest_ip == 0) {
        packet[16] = 255;
        packet[17] = 255;
        packet[18] = 255;
        packet[19] = 255;
    } else {
        set_ip_addr(&packet[16], dest_ip);
    }

    put_be16(&packet[10], checksum(packet, IPV4_HEADER_LEN));
}

static int send_udp_frame(uint32_t payload_len, uint32_t dest_ip,
                          uint16_t dest_port, uint16_t src_port) {
    uint8_t *ip;
    uint8_t *udp;
    uint32_t udp_len = UDP_HEADER_LEN + payload_len;

    if (payload_len > NET_FRAME_MAX - ETH_HEADER_LEN -
                          IPV4_HEADER_LEN - UDP_HEADER_LEN) {
        return -1;
    }

    write_eth_header(g_net_tx_frame, ETH_TYPE_IP);

    ip = g_net_tx_frame + ETH_HEADER_LEN;
    write_ipv4_header(ip, udp_len, dest_ip, IP_PROTO_UDP);

    udp = ip + IPV4_HEADER_LEN;
    put_be16(&udp[0], src_port);
    put_be16(&udp[2], dest_port);
    put_be16(&udp[4], (uint16_t)udp_len);
    udp[6] = 0;
    udp[7] = 0;

    return virtio_net_send(&g_net_dev, g_net_tx_frame,
                           ETH_HEADER_LEN + IPV4_HEADER_LEN + udp_len);
}

static void send_dhcp_discover(void) {
    dhcp_packet_t *pkt = tx_dhcp_packet();
    static const uint8_t options[4] = {0x63, 0x82, 0x53, 0x63};
    int status;

    kmemset(pkt, 0, sizeof(*pkt));
    pkt->op = 1;
    pkt->htype = 1;
    pkt->hlen = 6;
    pkt->xid = g_dhcp_xid;
    set_dhcp_broadcast_flag(pkt);

    kmemcpy(pkt->chaddr, g_net_dev.mac, 6);

    kmemcpy(pkt->options, options, sizeof(options));
    pkt->options[4] = 53;
    pkt->options[5] = 1;
    pkt->options[6] = DHCP_DISCOVER;
    pkt->options[7] = 0xFF;

    status = send_udp_frame(sizeof(*pkt), 0xFFFFFFFF, DHCP_PORT_SERVER,
                            DHCP_PORT_CLIENT);
    if (status != 0) {
        uart_puts("[net] DHCP discover send failed: ");
        print_hex64((uint64_t)(uint32_t)status);
        uart_puts("\n");
    }
}

static void send_dhcp_request(uint32_t server_ip, uint32_t requested_ip) {
    dhcp_packet_t *pkt = tx_dhcp_packet();
    static const uint8_t options[4] = {0x63, 0x82, 0x53, 0x63};
    uint8_t opt_idx = 4;
    int status;

    kmemset(pkt, 0, sizeof(*pkt));
    pkt->op = 1;
    pkt->htype = 1;
    pkt->hlen = 6;
    pkt->xid = g_dhcp_xid;
    set_dhcp_broadcast_flag(pkt);

    set_ip_addr(pkt->ciaddr, requested_ip);
    kmemcpy(pkt->chaddr, g_net_dev.mac, 6);

    kmemcpy(pkt->options, options, sizeof(options));

    pkt->options[opt_idx++] = 53;
    pkt->options[opt_idx++] = 1;
    pkt->options[opt_idx++] = DHCP_REQUEST;

    if (server_ip != 0) {
        pkt->options[opt_idx++] = 54;
        pkt->options[opt_idx++] = 4;
        set_ip_addr(&pkt->options[opt_idx], server_ip);
        opt_idx += 4;
    }

    pkt->options[opt_idx++] = 50;
    pkt->options[opt_idx++] = 4;
    set_ip_addr(&pkt->options[opt_idx], requested_ip);
    opt_idx += 4;

    pkt->options[opt_idx++] = 0xFF;

    status = send_udp_frame(sizeof(*pkt), 0xFFFFFFFF, DHCP_PORT_SERVER,
                            DHCP_PORT_CLIENT);
    if (status != 0) {
        uart_puts("[net] DHCP request send failed: ");
        print_hex64((uint64_t)(uint32_t)status);
        uart_puts("\n");
    }
}

static void parse_dhcp_offer(dhcp_packet_t *pkt,
                             const dhcp_options_t *options) {
    g_net_info.ip = get_ip_addr(pkt->yiaddr);

    if (options->message_type == DHCP_OFFER) {
        g_net_info.dhcp_state = DHCP_OFFER;
    }
    if ((options->flags & DHCP_OPTIONS_HAS_SERVER_ID) != 0) {
        g_net_info.dhcp_server = options->server_id;
    }
    uart_puts("[net] DHCP offer: ");
    print_ip(g_net_info.ip);
    uart_puts("\n");
}

static void parse_dhcp_ack(dhcp_packet_t *pkt,
                           const dhcp_options_t *options) {
    g_net_info.ip = get_ip_addr(pkt->yiaddr);

    if (options->message_type == DHCP_ACK) {
        g_net_info.dhcp_state = DHCP_ACK;
        g_net_info.discovered = 1;
    }
    if ((options->flags & DHCP_OPTIONS_HAS_SUBNET) != 0) {
        g_net_info.subnet = options->subnet;
    }
    if ((options->flags & DHCP_OPTIONS_HAS_GATEWAY) != 0) {
        g_net_info.gateway = options->gateway;
    }
    if ((options->flags & DHCP_OPTIONS_HAS_DNS) != 0) {
        g_net_info.dns = options->dns;
    }
    if ((options->flags & DHCP_OPTIONS_HAS_SERVER_ID) != 0) {
        g_net_info.dhcp_server = options->server_id;
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
    dhcp_options_t options;

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

    if (pkt->op != 2 || pkt->xid != g_dhcp_xid ||
        dhcp_options_parse(pkt->options, DHCP_OPTIONS_LEN, &options) != 0) {
        return;
    }

    if (options.message_type == DHCP_OFFER) {
        parse_dhcp_offer(pkt, &options);
    } else if (options.message_type == DHCP_ACK) {
        parse_dhcp_ack(pkt, &options);
    }
}

static void parse_ip_packet(uint8_t *data, uint32_t len) {
    uint8_t proto;
    uint32_t header_len;

    if (len < 20) {
        return;
    }

    if ((data[0] & 0xF0) != 0x40) {
        return;
    }

    header_len = (uint32_t)(data[0] & 0x0fU) * 4U;
    if (header_len < 20U || header_len > len) {
        return;
    }

    proto = data[9];

    if (proto == IP_PROTO_UDP) {
        parse_udp_packet(data + header_len, len - header_len);
    }
}

static void parse_eth_frame(uint8_t *data, uint32_t len) {
    uint16_t eth_type;

    if (len < 14) {
        return;
    }

    if (kmemcmp(data, g_net_dev.mac, 6) != 0 &&
        kmemcmp(data, "\xFF\xFF\xFF\xFF\xFF\xFF", 6) != 0) {
        return;
    }

    eth_type = (data[12] << 8) | data[13];

    if (eth_type == ETH_TYPE_IP) {
        parse_ip_packet(data + 14, len - 14);
    }
}

int net_init(void) {
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
    print_hex64(net_base);
    uart_puts(" MAC=");
    for (int i = 0; i < 6; i++) {
        if (i > 0) uart_putc(':');
        print_hex8(net_info.mac[i]);
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
    g_net_info.dhcp_server = 0;
    kmemcpy(g_net_info.mac, net_info.mac, 6);

    g_net_inited = 1;

    uart_puts("[net] network initialized, starting DHCP...\n");

    g_dhcp_xid = 0x12345678 + (uint32_t)((uintptr_t)&g_net_dev & 0xFFFF);
    send_dhcp_discover();

    return 0;
}

void net_poll(void) {
    int len;

    if (!g_net_inited || !g_net_dev.ready) {
        return;
    }
    if (g_net_polling != 0) {
        return;
    }
    g_net_polling = 1;

    while ((len = virtio_net_recv(&g_net_dev, g_net_rx_frame,
                                  sizeof(g_net_rx_frame))) > 0) {
        parse_eth_frame(g_net_rx_frame, (uint32_t)len);
    }

    if (!g_net_info.discovered) {
        if (g_net_info.dhcp_state == DHCP_OFFER) {
            send_dhcp_request(g_net_info.dhcp_server, g_net_info.ip);
        }
    }
    g_net_polling = 0;
}

int net_is_link_up(void) {
    return g_net_inited;
}
