#pragma once

#include <cstddef>

#include "logic/dhcp_server.h"

void dhcpTaskStart();
bool dhcpCameraLease(DhcpLease& out);
size_t dhcpLeases(DhcpLease* out, size_t cap);
