#include "mdns_task.h"

#include <atomic>
#include <cstring>

#include "bridge.h"
#include "dhcp_task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "logic/frame_info.h"
#include "logic/mdns.h"
#include "logic/settings.h"
#include "lwip/sockets.h"
#include "net_config.h"

namespace {
const char* TAG = "mdns";
constexpr uint32_t CAMERA_QUIET_MS = 180000;  // camera silent this long -> we answer
constexpr uint32_t PROBE_EVERY_MS = 30000;
constexpr uint32_t ANSWER_TTL = 120;

char s_host[HOSTNAME_MAX + 1];
std::atomic<MdnsState> s_state{MdnsState::NoCamera};

uint32_t nowMs() {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void task(void*) {
    const int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    int one = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(MDNS_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (sock < 0 || bind(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "socket/bind failed");
        vTaskDelete(nullptr);
        return;
    }
    ip_mreq mreq = {};
    mreq.imr_multiaddr.s_addr = inet_addr("224.0.0.251");
    mreq.imr_interface.s_addr = htonl(ADAPTER_IP);
    if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        ESP_LOGE(TAG, "join 224.0.0.251 failed");
    }
    in_addr ifaddr = {};
    ifaddr.s_addr = htonl(ADAPTER_IP);
    setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, &ifaddr, sizeof(ifaddr));
    uint8_t ttl = 255;
    setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
    timeval tv = {1, 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    sockaddr_in group = {};
    group.sin_family = AF_INET;
    group.sin_port = htons(MDNS_PORT);
    group.sin_addr.s_addr = inet_addr("224.0.0.251");

    static uint8_t buf[1500];
    static uint8_t reply[300];
    bool cameraEverAnswered = false;
    uint32_t cameraAnsweredMs = 0;
    uint32_t lastProbeMs = 0;
    ESP_LOGI(TAG, "fallback responder for %s.local", s_host);

    while (true) {
        const uint32_t camIp = cameraIpForClients();
        const uint32_t now = nowMs();
        const bool cameraTalks = cameraEverAnswered && now - cameraAnsweredMs < CAMERA_QUIET_MS;
        s_state = !camIp ? MdnsState::NoCamera
                         : (cameraTalks ? MdnsState::CameraAnswers : MdnsState::AdapterAnswers);

        if (camIp && now - lastProbeMs >= PROBE_EVERY_MS) {
            lastProbeMs = now;
            const size_t q = mdnsBuildQuery(s_host, reply, sizeof(reply));
            if (q) sendto(sock, reply, q, 0, (sockaddr*)&group, sizeof(group));
        }

        sockaddr_in from = {};
        socklen_t fromLen = sizeof(from);
        const int n = recvfrom(sock, buf, sizeof(buf), 0, (sockaddr*)&from, &fromLen);
        if (n <= 0) continue;
        const uint32_t src = ntohl(from.sin_addr.s_addr);
        if (src == ADAPTER_IP) continue;

        if (camIp && src == camIp && mdnsResponseAnswers(buf, (size_t)n, s_host)) {
            if (!cameraEverAnswered) ESP_LOGI(TAG, "camera answers for itself; staying quiet");
            cameraEverAnswered = true;
            cameraAnsweredMs = nowMs();
            continue;
        }
        if (!camIp || cameraTalks || ntohs(from.sin_port) != MDNS_PORT) continue;
        bool qu = false;
        if (!mdnsQueryAsksFor(buf, (size_t)n, s_host, &qu)) continue;
        const size_t len = mdnsBuildAnswer(s_host, camIp, ANSWER_TTL, reply, sizeof(reply));
        if (len) {
            sendto(sock, reply, len, 0, (sockaddr*)&group, sizeof(group));
            char a[16], b[16];
            ipToStr(src, a);
            ipToStr(camIp, b);
            ESP_LOGI(TAG, "answered %s for %s.local -> %s", a, s_host, b);
        }
    }
}
}  // namespace

uint32_t cameraIpForClients() {
    BridgeStatus st;
    bridgeStatus(st);
    if (!st.usbMounted) return 0;
    DhcpLease lease;
    if (dhcpCameraLease(lease)) return lease.ip;
    if (ipv4InSubnet(st.cameraSeenIp, ADAPTER_IP, NETMASK)) return st.cameraSeenIp;
    return 0;
}

void mdnsTaskStart(const char* cameraHostname) {
    strncpy(s_host, cameraHostname, HOSTNAME_MAX);
    s_host[HOSTNAME_MAX] = '\0';
    xTaskCreate(task, "mdns", 4096, nullptr, 4, nullptr);
}

MdnsState mdnsState() {
    return s_state;
}
