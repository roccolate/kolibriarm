#include "unity.h"

#include "kernel/net/dhcp_options.h"

#define TEST_IPV4(a, b, c, d) \
    ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | \
     ((uint32_t)(d) << 24))

void test_dhcp_options_parse_extracts_known_options(void) {
    const uint8_t options[] = {
        53, 1, DHCP_ACK,
        1, 4, 255, 255, 255, 0,
        3, 4, 10, 0, 2, 1,
        6, 4, 1, 1, 1, 1,
        255,
    };
    dhcp_options_t parsed;

    TEST_ASSERT_EQUAL_UINT64(0, dhcp_options_parse(options, sizeof(options),
                                                   &parsed));
    TEST_ASSERT_EQUAL_UINT64(DHCP_ACK, parsed.message_type);
    TEST_ASSERT_TRUE((parsed.flags & DHCP_OPTIONS_HAS_SUBNET) != 0);
    TEST_ASSERT_TRUE((parsed.flags & DHCP_OPTIONS_HAS_GATEWAY) != 0);
    TEST_ASSERT_TRUE((parsed.flags & DHCP_OPTIONS_HAS_DNS) != 0);
    TEST_ASSERT_EQUAL_UINT64(TEST_IPV4(255, 255, 255, 0), parsed.subnet);
    TEST_ASSERT_EQUAL_UINT64(TEST_IPV4(10, 0, 2, 1), parsed.gateway);
    TEST_ASSERT_EQUAL_UINT64(TEST_IPV4(1, 1, 1, 1), parsed.dns);
}

void test_dhcp_options_parse_skips_padding_and_stops_at_end(void) {
    const uint8_t options[] = {
        0,
        53, 1, DHCP_OFFER,
        255,
        53, 1, DHCP_ACK,
    };
    dhcp_options_t parsed;

    TEST_ASSERT_EQUAL_UINT64(0, dhcp_options_parse(options, sizeof(options),
                                                   &parsed));
    TEST_ASSERT_EQUAL_UINT64(DHCP_OFFER, parsed.message_type);
}

void test_dhcp_options_parse_rejects_truncated_option(void) {
    const uint8_t options[] = {
        3, 4, 10, 0,
    };
    dhcp_options_t parsed;

    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)dhcp_options_parse(options,
                                                          sizeof(options),
                                                          &parsed));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)dhcp_options_parse(0, sizeof(options),
                                                          &parsed));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)dhcp_options_parse(options,
                                                          sizeof(options), 0));
}

void test_dhcp_options_parse_clears_output_on_error(void) {
    const uint8_t options[] = {
        3, 4, 10, 0,
    };
    dhcp_options_t parsed = {
        .message_type = DHCP_ACK,
        .flags = DHCP_OPTIONS_HAS_SUBNET | DHCP_OPTIONS_HAS_GATEWAY |
                 DHCP_OPTIONS_HAS_DNS,
        .subnet = TEST_IPV4(255, 255, 255, 0),
        .gateway = TEST_IPV4(10, 0, 2, 1),
        .dns = TEST_IPV4(1, 1, 1, 1),
    };

    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)dhcp_options_parse(options,
                                                          sizeof(options),
                                                          &parsed));
    TEST_ASSERT_EQUAL_UINT64(0, parsed.message_type);
    TEST_ASSERT_EQUAL_UINT64(0, parsed.flags);
    TEST_ASSERT_EQUAL_UINT64(0, parsed.subnet);
    TEST_ASSERT_EQUAL_UINT64(0, parsed.gateway);
    TEST_ASSERT_EQUAL_UINT64(0, parsed.dns);
}
