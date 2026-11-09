#include "web.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "boot_mode.h"
#include "bridge.h"
#include "dhcp_task.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "log_buffer.h"
#include "logic/web_text.h"
#include "mdns_task.h"
#include "net_config.h"
#include "settings_store.h"

namespace {
const char* TAG = "web";
Settings s_settings;

struct Out {
    char* buf;
    size_t cap;
    size_t len;
    void add(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
        if (len >= cap) return;
        va_list ap;
        va_start(ap, fmt);
        const int n = vsnprintf(buf + len, cap - len, fmt, ap);
        va_end(ap);
        if (n > 0) len = (len + (size_t)n < cap) ? len + (size_t)n : cap - 1;
    }
};

const char* mdnsText(MdnsState s) {
    switch (s) {
        case MdnsState::CameraAnswers: return "камера відповідає сама";
        case MdnsState::AdapterAnswers: return "адаптер відповідає за камеру";
        default: return "камери ще немає";
    }
}

enum class RestartKind : intptr_t { Normal = 0, Safe = 1, Download = 2 };

void delayedRestart(RestartKind kind) {
    static esp_timer_handle_t t = nullptr;
    esp_timer_create_args_t args = {};
    args.callback = [](void* arg) {
        switch ((RestartKind)(intptr_t)arg) {
            case RestartKind::Safe: bootModeRestartSafe();
            case RestartKind::Download: bootModeRestartDownload();
            default: esp_restart();
        }
    };
    args.arg = (void*)(intptr_t)kind;
    args.name = "restart";
    if (!t && esp_timer_create(&args, &t) == ESP_OK) esp_timer_start_once(t, 1500 * 1000);
}

void renderPage(Out& o, const Settings& form, const char* message, bool error) {
    BridgeStatus st;
    bridgeStatus(st);
    const uint32_t camIp = cameraIpForClients();
    char camStr[16], seenStr[16];
    ipToStr(camIp, camStr);
    ipToStr(st.cameraSeenIp, seenStr);
    char ssid[SSID_MAX * 6 + 1], pass[PASS_MAX * 6 + 1], host[HOSTNAME_MAX * 6 + 1];
    htmlEscape(form.ssid, ssid, sizeof(ssid));
    htmlEscape(form.password, pass, sizeof(pass));
    htmlEscape(form.cameraHostname, host, sizeof(host));

    o.add("<!doctype html><html lang=uk><head><meta charset=utf-8>"
          "<meta name=viewport content='width=device-width,initial-scale=1'>"
          "<title>BMCam адаптер</title><style>"
          "body{font:16px system-ui,sans-serif;margin:0;padding:16px;max-width:520px;"
          "background:#111;color:#eee}h1{font-size:20px}h2{font-size:17px;margin-top:24px}"
          "table{width:100%%;border-collapse:collapse}td{padding:6px 4px;border-bottom:1px solid #333}"
          "td:first-child{color:#999;width:45%%}a{color:#6cf}label{display:block;margin-top:12px;color:#bbb}"
          "input{width:100%%;box-sizing:border-box;padding:10px;font-size:16px;background:#222;"
          "color:#eee;border:1px solid #444;border-radius:6px}"
          "button{margin-top:16px;padding:12px 18px;font-size:16px;border:0;border-radius:6px;"
          "background:#2a7;color:#fff}.msg{padding:10px;border-radius:6px;margin:12px 0}"
          ".ok{background:#143}.err{background:#511}.warn{color:#fb4}small{color:#888}"
          "</style></head><body><h1>BMCam USB&nbsp;Ethernet &rarr; Wi&#8209;Fi</h1>");
    if (message) {
        char esc[256];
        htmlEscape(message, esc, sizeof(esc));
        o.add("<div class='msg %s'>%s</div>", error ? "err" : "ok", esc);
    }
    o.add("<h2>Стан</h2><table>");
    o.add("<tr><td>Камера по USB</td><td>%s</td></tr>",
          st.usbMounted ? "підключена" : "<span class=warn>не підключена</span>");
    if (camIp) {
        o.add("<tr><td>IP камери</td><td><a href='http://%s/control/api/v1/system'>%s</a></td></tr>",
              camStr, camStr);
    } else if (st.usbMounted && st.cameraSeenIp) {
        o.add("<tr><td>IP камери</td><td class=warn>%s &mdash; з іншої мережі. "
              "Увімкніть DHCP у мережевих налаштуваннях камери.</td></tr>",
              seenStr);
    } else {
        o.add("<tr><td>IP камери</td><td>ще немає</td></tr>");
    }
    o.add("<tr><td>Пошук камери (mDNS)</td><td>%s</td></tr>", mdnsText(mdnsState()));
    o.add("<tr><td>Клієнтів Wi&#8209;Fi</td><td>%d</td></tr>", st.wifiClients);
    o.add("<tr><td>Пакетів USB&rarr;Wi&#8209;Fi / Wi&#8209;Fi&rarr;USB</td><td>%lu / %lu</td></tr>",
          (unsigned long)st.usbToWifi, (unsigned long)st.wifiToUsb);
    o.add("<tr><td>Працює</td><td>%lu хв</td></tr></table>",
          (unsigned long)(esp_timer_get_time() / 60000000));

    o.add("<h2>Налаштування</h2><form method=post action='/save'>"
          "<label>Назва Wi&#8209;Fi мережі</label><input name=ssid maxlength=32 value=\"%s\">"
          "<label>Пароль Wi&#8209;Fi (8&ndash;63 символи)</label>"
          "<input name=password maxlength=63 value=\"%s\">"
          "<label>Канал Wi&#8209;Fi (1&ndash;13)</label>"
          "<input name=channel type=number min=1 max=13 value=%u>"
          "<label>mDNS ім'я камери <small>(без .local)</small></label>"
          "<input name=hostname maxlength=63 value=\"%s\">"
          "<button type=submit>Зберегти і перезапустити</button></form>"
          "<p><small>Скидання до заводських: утримувати кнопку G0 5 секунд. "
          "Заводська мережа &mdash; BMCam&#8209;XXXX, пароль blackmagic.</small></p>"
          "</body></html>",
          ssid, pass, (unsigned)form.channel, host);
}

// Blocks cross-site requests from web pages the phone opens over mobile data:
// API calls must carry a custom header (browsers cannot add one without a
// CORS preflight, which this server never approves) and form posts must not
// come from another origin.
constexpr const char* API_HEADER = "X-BMCam";

// DNS rebinding guard: a foreign hostname resolving to us is not same-origin.
bool hostOk(httpd_req_t* req) {
    char h[32];
    if (httpd_req_get_hdr_value_str(req, "Host", h, sizeof(h)) != ESP_OK) return false;
    return strcmp(h, "192.168.73.1") == 0 || strcmp(h, "192.168.73.1:80") == 0;
}

bool hasApiHeader(httpd_req_t* req) {
    return hostOk(req) && httpd_req_get_hdr_value_len(req, API_HEADER) > 0;
}

bool sameOrigin(httpd_req_t* req) {
    if (!hostOk(req)) return false;
    char value[96];
    const char* hdrs[] = {"Origin", "Referer"};
    for (const char* h : hdrs) {
        const esp_err_t e = httpd_req_get_hdr_value_str(req, h, value, sizeof(value));
        if (e == ESP_ERR_NOT_FOUND) continue;  // curl and other non-browser clients
        if (e != ESP_OK) return false;         // e.g. truncated: fail closed
        if (strncmp(value, "http://192.168.73.1", 19) != 0 ||
            (value[19] != '\0' && value[19] != '/')) {
            return false;
        }
    }
    return true;
}

esp_err_t rejectCrossSite(httpd_req_t* req) {
    ESP_LOGW(TAG, "rejected cross-site request to %s", req->uri);
    httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "missing X-BMCam header or foreign origin");
    return ESP_FAIL;
}

esp_err_t sendPage(httpd_req_t* req, const Settings& form, const char* message, bool error) {
    Out o{(char*)malloc(8192), 8192, 0};
    if (!o.buf) return httpd_resp_send_500(req);
    o.buf[0] = '\0';
    renderPage(o, form, message, error);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    const esp_err_t e = httpd_resp_send(req, o.buf, (ssize_t)o.len);
    free(o.buf);
    return e;
}

esp_err_t handleRoot(httpd_req_t* req) {
    return sendPage(req, s_settings, nullptr, false);
}

esp_err_t handleSave(httpd_req_t* req) {
    if (!sameOrigin(req)) return rejectCrossSite(req);
    char body[600];
    if (req->content_len >= sizeof(body)) {
        return sendPage(req, s_settings, "Забагато даних у формі", true);
    }
    size_t got = 0;
    while (got < req->content_len) {
        const int n = httpd_req_recv(req, body + got, req->content_len - got);
        if (n <= 0) return ESP_FAIL;
        got += (size_t)n;
    }
    body[got] = '\0';

    Settings next = s_settings;
    char channel[8] = "";
    const bool parsed = formField(body, "ssid", next.ssid, sizeof(next.ssid)) &&
                        formField(body, "password", next.password, sizeof(next.password)) &&
                        formField(body, "channel", channel, sizeof(channel)) &&
                        formField(body, "hostname", next.cameraHostname,
                                  sizeof(next.cameraHostname));
    if (!parsed) return sendPage(req, s_settings, "Некоректні дані форми (задовгі значення?)", true);
    char* end = nullptr;
    const long ch = strtol(channel, &end, 10);
    if (!end || *end != '\0' || ch < 0 || ch > 255) {
        return sendPage(req, next, "Канал: від 1 до 13", true);
    }
    next.channel = (uint8_t)ch;
    if (const char* err = settingsSave(next)) return sendPage(req, next, err, true);
    ESP_LOGI(TAG, "settings saved, restarting");
    s_settings = next;
    delayedRestart(RestartKind::Normal);
    return sendPage(req, next,
                    "Збережено. Адаптер перезапускається — підключіться до нової мережі.",
                    false);
}

esp_err_t handleStatus(httpd_req_t* req) {
    BridgeStatus st;
    bridgeStatus(st);
    char cam[16], seen[16], ssid[SSID_MAX * 6 + 1], host[HOSTNAME_MAX * 6 + 1];
    ipToStr(cameraIpForClients(), cam);
    ipToStr(st.cameraSeenIp, seen);
    jsonEscape(s_settings.ssid, ssid, sizeof(ssid));
    jsonEscape(s_settings.cameraHostname, host, sizeof(host));
    const char* mdns = mdnsState() == MdnsState::CameraAnswers    ? "camera"
                       : mdnsState() == MdnsState::AdapterAnswers ? "adapter"
                                                                  : "no_camera";
    char json[1024];
    snprintf(json, sizeof(json),
             "{\"usb_connected\":%s,\"camera_ip\":\"%s\",\"camera_seen_ip\":\"%s\","
             "\"mdns\":\"%s\",\"camera_hostname\":\"%s\",\"wifi_clients\":%d,\"ssid\":\"%s\","
             "\"channel\":%u,\"uptime_s\":%llu,\"frames\":{\"usb_to_wifi\":%lu,"
             "\"wifi_to_usb\":%lu,\"to_adapter\":%lu,\"from_adapter\":%lu,\"dropped\":%lu},"
             "\"free_heap\":%lu}",
             st.usbMounted ? "true" : "false", cameraIpForClients() ? cam : "",
             st.cameraSeenIp ? seen : "", mdns, host, st.wifiClients, ssid,
             (unsigned)s_settings.channel, (unsigned long long)(esp_timer_get_time() / 1000000),
             (unsigned long)st.usbToWifi, (unsigned long)st.wifiToUsb, (unsigned long)st.toLocal,
             (unsigned long)st.fromLocal, (unsigned long)st.drops,
             (unsigned long)esp_get_free_heap_size());
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

esp_err_t handleLog(httpd_req_t* req) {
    char* buf = (char*)malloc(8200);
    if (!buf) return httpd_resp_send_500(req);
    const size_t n = logBufferRead(buf, 8200);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    const esp_err_t e = httpd_resp_send(req, buf, (ssize_t)n);
    free(buf);
    return e;
}

esp_err_t handleReboot(httpd_req_t* req) {
    if (!hasApiHeader(req) || !sameOrigin(req)) return rejectCrossSite(req);
    char query[32] = "", mode[8] = "";
    httpd_req_get_url_query_str(req, query, sizeof(query));
    httpd_query_key_value(query, "mode", mode, sizeof(mode));
    RestartKind kind = RestartKind::Normal;
    if (strcmp(mode, "safe") == 0) kind = RestartKind::Safe;
    if (strcmp(mode, "download") == 0) kind = RestartKind::Download;
    ESP_LOGW(TAG, "reboot requested (%s)", mode[0] ? mode : "normal");
    httpd_resp_sendstr(req, kind == RestartKind::Safe       ? "restarting into safe mode (USB = serial port)\n"
                            : kind == RestartKind::Download ? "restarting into ROM download mode\n"
                                                            : "restarting\n");
    delayedRestart(kind);
    return ESP_OK;
}

// POST /api/ota with the raw firmware.bin as the body. The new image boots
// once; if it does not mark itself valid (main.cpp) the bootloader rolls back.
esp_err_t handleOta(httpd_req_t* req) {
    if (!hasApiHeader(req) || !sameOrigin(req)) return rejectCrossSite(req);
    const esp_partition_t* part = esp_ota_get_next_update_partition(nullptr);
    if (!part || req->content_len == 0 || req->content_len > part->size) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad image size");
        return ESP_FAIL;
    }
    esp_ota_handle_t ota = 0;
    if (esp_ota_begin(part, req->content_len, &ota) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota begin failed");
        return ESP_FAIL;
    }
    char* buf = (char*)malloc(4096);
    size_t left = req->content_len;
    esp_err_t err = buf ? ESP_OK : ESP_ERR_NO_MEM;
    while (err == ESP_OK && left > 0) {
        const int n = httpd_req_recv(req, buf, left < 4096 ? left : 4096);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (n <= 0) {
            err = ESP_FAIL;
            break;
        }
        err = esp_ota_write(ota, buf, (size_t)n);
        left -= (size_t)n;
    }
    free(buf);
    if (err == ESP_OK) err = esp_ota_end(ota);
    else esp_ota_abort(ota);
    if (err == ESP_OK) err = esp_ota_set_boot_partition(part);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
        return ESP_FAIL;
    }
    ESP_LOGW(TAG, "OTA written to %s (%u bytes), restarting", part->label, (unsigned)req->content_len);
    httpd_resp_sendstr(req, "ok, restarting into the new firmware\n");
    delayedRestart(RestartKind::Normal);
    return ESP_OK;
}
}  // namespace

void webStart(const Settings& current) {
    s_settings = current;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 8192;
    cfg.lru_purge_enable = true;
    httpd_handle_t server = nullptr;
    if (httpd_start(&server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed");
        return;
    }
    const httpd_uri_t routes[] = {
        {"/", HTTP_GET, handleRoot, nullptr},
        {"/save", HTTP_POST, handleSave, nullptr},
        {"/api/status", HTTP_GET, handleStatus, nullptr},
        {"/api/log", HTTP_GET, handleLog, nullptr},
        {"/api/reboot", HTTP_POST, handleReboot, nullptr},
        {"/api/ota", HTTP_POST, handleOta, nullptr},
    };
    for (const httpd_uri_t& r : routes) httpd_register_uri_handler(server, &r);
    ESP_LOGI(TAG, "settings page on http://192.168.73.1/");
}
