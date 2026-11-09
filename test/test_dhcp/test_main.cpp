#include <unity.h>
#include <cstring>

#include "logic/dhcp_server.h"

void setUp(void) {}
void tearDown(void) {}

static const DhcpConfig CFG = {
    ipv4(192, 168, 73, 1), ipv4(255, 255, 255, 0), ipv4(192, 168, 73, 2),
    ipv4(192, 168, 73, 10), ipv4(192, 168, 73, 50), 3600,
};

static const uint8_t CAMERA[6] = {0x7C, 0x2E, 0x0D, 0x11, 0x22, 0x33};
static const uint8_t PHONE[6] = {0x3A, 0x10, 0x20, 0x30, 0x40, 0x50};
static const uint8_t PHONE2[6] = {0x3A, 0x10, 0x20, 0x30, 0x40, 0x51};

static uint8_t req[400];
static uint8_t out[DHCP_REPLY_CAP];

static size_t mkReq(uint8_t type, const uint8_t mac[6], uint32_t requested = 0,
                    uint32_t serverId = 0, const char* hostname = nullptr) {
    memset(req, 0, sizeof(req));
    req[0] = 1;
    req[1] = 1;
    req[2] = 6;
    req[4] = 0xDE;
    req[5] = 0xAD;
    req[6] = 0xBE;
    req[7] = 0xEF;
    req[10] = 0x80;  // broadcast flag
    memcpy(req + 28, mac, 6);
    req[236] = 0x63;
    req[237] = 0x82;
    req[238] = 0x53;
    req[239] = 0x63;
    size_t i = 240;
    req[i++] = 53;
    req[i++] = 1;
    req[i++] = type;
    if (requested) {
        req[i++] = 50;
        req[i++] = 4;
        req[i++] = (uint8_t)(requested >> 24);
        req[i++] = (uint8_t)(requested >> 16);
        req[i++] = (uint8_t)(requested >> 8);
        req[i++] = (uint8_t)requested;
    }
    if (serverId) {
        req[i++] = 54;
        req[i++] = 4;
        req[i++] = (uint8_t)(serverId >> 24);
        req[i++] = (uint8_t)(serverId >> 16);
        req[i++] = (uint8_t)(serverId >> 8);
        req[i++] = (uint8_t)serverId;
    }
    if (hostname) {
        req[i++] = 12;
        req[i++] = (uint8_t)strlen(hostname);
        memcpy(req + i, hostname, strlen(hostname));
        i += strlen(hostname);
    }
    req[i++] = 255;
    return 300;
}

static uint32_t yiaddr() {
    return ((uint32_t)out[16] << 24) | ((uint32_t)out[17] << 16) | ((uint32_t)out[18] << 8) | out[19];
}

static uint8_t replyType(size_t len) {
    const uint8_t* v = nullptr;
    TEST_ASSERT_EQUAL_INT(1, dhcpFindOption(out, len, 53, &v));
    return v[0];
}

void test_camera_discover_gets_fixed_ip(void) {
    DhcpServer s(CFG);
    size_t n = s.handle(req, mkReq(DHCP_DISCOVER, CAMERA), true, 100, out, sizeof(out));
    TEST_ASSERT_TRUE(n >= DHCP_MIN_PACKET);
    TEST_ASSERT_EQUAL_UINT8(DHCP_OFFER, replyType(n));
    TEST_ASSERT_EQUAL_HEX32(ipv4(192, 168, 73, 2), yiaddr());
    TEST_ASSERT_EQUAL_UINT8(2, out[0]);
    TEST_ASSERT_EQUAL_MEMORY(req + 4, out + 4, 4);    // xid
    TEST_ASSERT_EQUAL_MEMORY(CAMERA, out + 28, 6);    // chaddr
    TEST_ASSERT_EQUAL_UINT8(0x80, out[10]);           // flags echoed
}

void test_reply_has_mask_lease_server_but_no_router_or_dns(void) {
    DhcpServer s(CFG);
    size_t n = s.handle(req, mkReq(DHCP_DISCOVER, PHONE), false, 100, out, sizeof(out));
    const uint8_t* v = nullptr;
    TEST_ASSERT_EQUAL_INT(4, dhcpFindOption(out, n, 1, &v));
    TEST_ASSERT_EQUAL_UINT8(255, v[0]);
    TEST_ASSERT_EQUAL_UINT8(0, v[3]);
    TEST_ASSERT_EQUAL_INT(4, dhcpFindOption(out, n, 54, &v));
    TEST_ASSERT_EQUAL_UINT8(1, v[3]);
    TEST_ASSERT_EQUAL_INT(4, dhcpFindOption(out, n, 51, &v));
    TEST_ASSERT_EQUAL_INT(-1, dhcpFindOption(out, n, 3, &v));
    TEST_ASSERT_EQUAL_INT(-1, dhcpFindOption(out, n, 6, &v));
}

void test_phone_gets_pool_ip_and_ack(void) {
    DhcpServer s(CFG);
    s.handle(req, mkReq(DHCP_DISCOVER, PHONE), false, 100, out, sizeof(out));
    const uint32_t offered = yiaddr();
    TEST_ASSERT_EQUAL_HEX32(ipv4(192, 168, 73, 10), offered);
    size_t n = s.handle(req, mkReq(DHCP_REQUEST, PHONE, offered, CFG.serverIp, "iPhone"), false,
                        101, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT8(DHCP_ACK, replyType(n));
    TEST_ASSERT_EQUAL_HEX32(offered, yiaddr());
    DhcpLease l[4];
    TEST_ASSERT_EQUAL_size_t(1, s.activeLeases(102, l, 4));
    TEST_ASSERT_EQUAL_STRING("iPhone", l[0].hostname);
}

void test_two_phones_get_different_ips(void) {
    DhcpServer s(CFG);
    s.handle(req, mkReq(DHCP_REQUEST, PHONE, ipv4(192, 168, 73, 10)), false, 1, out, sizeof(out));
    s.handle(req, mkReq(DHCP_DISCOVER, PHONE2), false, 2, out, sizeof(out));
    TEST_ASSERT_EQUAL_HEX32(ipv4(192, 168, 73, 11), yiaddr());
}

void test_request_for_foreign_ip_is_naked(void) {
    DhcpServer s(CFG);
    size_t n = s.handle(req, mkReq(DHCP_REQUEST, CAMERA, ipv4(10, 0, 0, 5)), true, 1, out,
                        sizeof(out));
    TEST_ASSERT_EQUAL_UINT8(DHCP_NAK, replyType(n));
    TEST_ASSERT_EQUAL_HEX32(0, yiaddr());
}

void test_camera_requesting_pool_ip_is_naked(void) {
    DhcpServer s(CFG);
    size_t n = s.handle(req, mkReq(DHCP_REQUEST, CAMERA, ipv4(192, 168, 73, 10)), true, 1, out,
                        sizeof(out));
    TEST_ASSERT_EQUAL_UINT8(DHCP_NAK, replyType(n));
}

void test_camera_init_reboot_with_fixed_ip_is_acked(void) {
    DhcpServer s(CFG);
    size_t n = s.handle(req, mkReq(DHCP_REQUEST, CAMERA, ipv4(192, 168, 73, 2)), true, 1, out,
                        sizeof(out));
    TEST_ASSERT_EQUAL_UINT8(DHCP_ACK, replyType(n));
    DhcpLease cam;
    TEST_ASSERT_TRUE(s.cameraLease(2, cam));
    TEST_ASSERT_EQUAL_HEX32(ipv4(192, 168, 73, 2), cam.ip);
}

void test_request_for_other_server_is_ignored(void) {
    DhcpServer s(CFG);
    TEST_ASSERT_EQUAL_size_t(0, s.handle(req, mkReq(DHCP_REQUEST, PHONE, ipv4(192, 168, 73, 10),
                                                    ipv4(192, 168, 73, 254)),
                                         false, 1, out, sizeof(out)));
}

void test_release_frees_lease(void) {
    DhcpServer s(CFG);
    s.handle(req, mkReq(DHCP_REQUEST, PHONE, ipv4(192, 168, 73, 10)), false, 1, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(0, s.handle(req, mkReq(DHCP_RELEASE, PHONE), false, 2, out, sizeof(out)));
    DhcpLease l[4];
    TEST_ASSERT_EQUAL_size_t(0, s.activeLeases(3, l, 4));
}

void test_lease_expires(void) {
    DhcpServer s(CFG);
    s.handle(req, mkReq(DHCP_REQUEST, CAMERA, ipv4(192, 168, 73, 2)), true, 1, out, sizeof(out));
    DhcpLease cam;
    TEST_ASSERT_FALSE(s.cameraLease(1 + 3600, cam));
}

void test_pool_exhaustion_returns_nothing(void) {
    DhcpConfig tiny = CFG;
    tiny.poolEnd = tiny.poolStart;
    DhcpServer s(tiny);
    s.handle(req, mkReq(DHCP_REQUEST, PHONE, tiny.poolStart), false, 1, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(0, s.handle(req, mkReq(DHCP_DISCOVER, PHONE2), false, 2, out, sizeof(out)));
}

void test_garbage_is_ignored(void) {
    DhcpServer s(CFG);
    uint8_t junk[100] = {1, 1, 6};
    TEST_ASSERT_EQUAL_size_t(0, s.handle(junk, sizeof(junk), false, 1, out, sizeof(out)));
    mkReq(DHCP_DISCOVER, PHONE);
    req[0] = 2;  // a server reply, not a request
    TEST_ASSERT_EQUAL_size_t(0, s.handle(req, 300, false, 1, out, sizeof(out)));
}

void test_truncated_option_does_not_overrun(void) {
    DhcpServer s(CFG);
    size_t n = mkReq(DHCP_DISCOVER, PHONE);
    (void)n;
    req[243] = 12;   // hostname option claiming 200 bytes in a 250-byte packet
    req[244] = 200;
    TEST_ASSERT_TRUE(s.handle(req, 250, false, 1, out, sizeof(out)) > 0);
}

void test_new_camera_mac_replaces_old_camera_lease(void) {
    DhcpServer s(CFG);
    s.handle(req, mkReq(DHCP_REQUEST, CAMERA, ipv4(192, 168, 73, 2)), true, 1, out, sizeof(out));
    s.handle(req, mkReq(DHCP_REQUEST, PHONE2, ipv4(192, 168, 73, 2)), true, 2, out, sizeof(out));
    DhcpLease l[4];
    TEST_ASSERT_EQUAL_size_t(1, s.activeLeases(3, l, 4));
    TEST_ASSERT_EQUAL_MEMORY(PHONE2, l[0].mac, 6);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_camera_discover_gets_fixed_ip);
    RUN_TEST(test_reply_has_mask_lease_server_but_no_router_or_dns);
    RUN_TEST(test_phone_gets_pool_ip_and_ack);
    RUN_TEST(test_two_phones_get_different_ips);
    RUN_TEST(test_request_for_foreign_ip_is_naked);
    RUN_TEST(test_camera_requesting_pool_ip_is_naked);
    RUN_TEST(test_camera_init_reboot_with_fixed_ip_is_acked);
    RUN_TEST(test_request_for_other_server_is_ignored);
    RUN_TEST(test_release_frees_lease);
    RUN_TEST(test_lease_expires);
    RUN_TEST(test_pool_exhaustion_returns_nothing);
    RUN_TEST(test_garbage_is_ignored);
    RUN_TEST(test_truncated_option_does_not_overrun);
    RUN_TEST(test_new_camera_mac_replaces_old_camera_lease);
    return UNITY_END();
}
