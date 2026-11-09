#include "logic/settings.h"

#include <cstdio>
#include <cstring>

namespace {
bool printable(const char* s) {
    for (; *s; ++s) {
        if ((unsigned char)*s < 0x20 || (unsigned char)*s > 0x7E) return false;
    }
    return true;
}
}  // namespace

void defaultSettings(const uint8_t mac[6], Settings& out) {
    memset(&out, 0, sizeof(out));
    snprintf(out.ssid, sizeof(out.ssid), "%s-%02X%02X", DEFAULT_SSID_PREFIX, mac[4], mac[5]);
    snprintf(out.password, sizeof(out.password), "%s", DEFAULT_PASSWORD);
    out.channel = DEFAULT_CHANNEL;
    snprintf(out.cameraHostname, sizeof(out.cameraHostname), "%s", DEFAULT_CAMERA_HOSTNAME);
}

const char* validateSsid(const char* ssid) {
    if (!ssid) return "Не вказано назву мережі";
    const size_t n = strlen(ssid);
    if (n == 0) return "Назва мережі не може бути порожньою";
    if (n > SSID_MAX) return "Назва мережі: не більше 32 символів";
    if (!printable(ssid)) return "Назва мережі: лише латиниця, цифри та звичайні знаки";
    return nullptr;
}

const char* validatePassword(const char* password) {
    if (!password) return "Не вказано пароль";
    const size_t n = strlen(password);
    if (n < PASS_MIN || n > PASS_MAX) return "Пароль: від 8 до 63 символів";
    if (!printable(password)) return "Пароль: лише латиниця, цифри та звичайні знаки";
    return nullptr;
}

const char* validateChannel(int channel) {
    if (channel < CHANNEL_MIN || channel > CHANNEL_MAX) return "Канал: від 1 до 13";
    return nullptr;
}

const char* validateHostname(const char* hostname) {
    if (!hostname) return "Не вказано ім'я камери";
    const size_t n = strlen(hostname);
    if (n == 0 || n > HOSTNAME_MAX) return "Ім'я камери: від 1 до 63 символів";
    if (hostname[0] == '-' || hostname[n - 1] == '-') {
        return "Ім'я камери не може починатися чи закінчуватися на '-'";
    }
    for (size_t i = 0; i < n; ++i) {
        const char c = hostname[i];
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '-';
        if (!ok) return "Ім'я камери: лише латинські літери, цифри та '-'";
    }
    return nullptr;
}

const char* validateSettings(const Settings& s) {
    if (const char* e = validateSsid(s.ssid)) return e;
    if (const char* e = validatePassword(s.password)) return e;
    if (const char* e = validateChannel(s.channel)) return e;
    return validateHostname(s.cameraHostname);
}
