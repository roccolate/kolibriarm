#ifndef KOLIBRIARM_KERNEL_NET_DHCP_OPTIONS_H
#define KOLIBRIARM_KERNEL_NET_DHCP_OPTIONS_H

#include <stdint.h>

#include "kernel/net/dhcp.h"

#define DHCP_OPTIONS_HAS_SUBNET    (1U << 0)
#define DHCP_OPTIONS_HAS_GATEWAY   (1U << 1)
#define DHCP_OPTIONS_HAS_DNS       (1U << 2)

/*
 * Parsed DHCP options used by the minimal net stack.
 *
 * IPv4 values are stored in the same little-endian host representation used by
 * net_info_t. Missing options leave the value at zero and clear the matching
 * DHCP_OPTIONS_HAS_* flag.
 */

typedef struct {
    uint8_t message_type;
    uint8_t flags;
    uint32_t subnet;
    uint32_t gateway;
    uint32_t dns;
} dhcp_options_t;

int dhcp_options_parse(const uint8_t *options, uint32_t len,
                       dhcp_options_t *out);

#endif
