#pragma once

#include <cstdint>

// Fallback mDNS responder: answers "<cameraHostname>.local" with the camera's
// IP only while the camera does not answer for itself.
enum class MdnsState { NoCamera, CameraAnswers, AdapterAnswers };

void mdnsTaskStart(const char* cameraHostname);
MdnsState mdnsState();

// The camera address the adapter knows: DHCP lease, else the address seen on
// USB if it is inside the adapter's subnet. 0 if unknown.
uint32_t cameraIpForClients();
