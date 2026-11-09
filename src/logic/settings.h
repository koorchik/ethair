#pragma once

#include <cstddef>
#include <cstdint>

// User-changeable adapter settings and their rules. Pure logic.

constexpr size_t SSID_MAX = 32;
constexpr size_t PASS_MIN = 8;
constexpr size_t PASS_MAX = 63;
constexpr size_t HOSTNAME_MAX = 63;
constexpr uint8_t CHANNEL_MIN = 1;
constexpr uint8_t CHANNEL_MAX = 13;

constexpr const char* DEFAULT_SSID_PREFIX = "BMCam";
constexpr const char* DEFAULT_PASSWORD = "blackmagic";
constexpr uint8_t DEFAULT_CHANNEL = 6;
constexpr const char* DEFAULT_CAMERA_HOSTNAME = "micro-studio-camera-4k-g2";

struct Settings {
    char ssid[SSID_MAX + 1];
    char password[PASS_MAX + 1];
    uint8_t channel;
    // mDNS name (without ".local") the adapter answers for when the camera
    // does not answer itself.
    char cameraHostname[HOSTNAME_MAX + 1];
};

// "BMCam-XXXX" from the last two MAC bytes.
void defaultSettings(const uint8_t mac[6], Settings& out);

// Each returns nullptr when valid, else a short English error message.
const char* validateSsid(const char* ssid);
const char* validatePassword(const char* password);
const char* validateChannel(int channel);
const char* validateHostname(const char* hostname);
const char* validateSettings(const Settings& s);
