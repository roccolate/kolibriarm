#include "kernel/net/dhcp_options.h"

#include <stddef.h>

/*
 * Pure DHCP option parser.
 *
 * The network driver feeds this helper the fixed DHCP options area from a
 * packet. It extracts only the options KolibriARM currently consumes and
 * rejects truncated TLVs so packet handling never walks beyond the buffer.
 */

#define DHCP_OPTION_PAD         0U
#define DHCP_OPTION_SUBNET      1U
#define DHCP_OPTION_ROUTER      3U
#define DHCP_OPTION_DNS         6U
#define DHCP_OPTION_MSG_TYPE    53U
#define DHCP_OPTION_END         255U

static uint32_t dhcp_get_ipv4(const uint8_t *addr) {
    return ((uint32_t)addr[0]) |
           ((uint32_t)addr[1] << 8) |
           ((uint32_t)addr[2] << 16) |
           ((uint32_t)addr[3] << 24);
}

static void dhcp_options_clear(dhcp_options_t *out) {
    out->message_type = 0;
    out->flags = 0;
    out->subnet = 0;
    out->gateway = 0;
    out->dns = 0;
}

int dhcp_options_parse(const uint8_t *options, uint32_t len,
                       dhcp_options_t *out) {
    uint32_t pos = 0;

    if (options == 0 || out == 0) {
        return -1;
    }

    dhcp_options_clear(out);

    while (pos < len) {
        uint8_t code = options[pos++];
        uint8_t opt_len;
        const uint8_t *value;

        if (code == DHCP_OPTION_END) {
            return 0;
        }
        if (code == DHCP_OPTION_PAD) {
            continue;
        }
        if (pos >= len) {
            return -1;
        }

        opt_len = options[pos++];
        if ((uint32_t)opt_len > len - pos) {
            return -1;
        }

        value = &options[pos];
        if (code == DHCP_OPTION_MSG_TYPE && opt_len >= 1U) {
            out->message_type = value[0];
        } else if (code == DHCP_OPTION_SUBNET && opt_len >= 4U) {
            out->subnet = dhcp_get_ipv4(value);
            out->flags |= DHCP_OPTIONS_HAS_SUBNET;
        } else if (code == DHCP_OPTION_ROUTER && opt_len >= 4U) {
            out->gateway = dhcp_get_ipv4(value);
            out->flags |= DHCP_OPTIONS_HAS_GATEWAY;
        } else if (code == DHCP_OPTION_DNS && opt_len >= 4U) {
            out->dns = dhcp_get_ipv4(value);
            out->flags |= DHCP_OPTIONS_HAS_DNS;
        }

        pos += opt_len;
    }

    return 0;
}
