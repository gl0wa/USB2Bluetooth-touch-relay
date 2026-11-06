// Wi-Fi station plus the HTTP service used for OTA, logs and diagnostics.
#pragma once
#include <stdbool.h>

// Wi-Fi settings and the API token come from the `devcfg` NVS partition, written
// by tools/provision.py. Without them the device runs without networking.
void netsvc_start(void);
bool netsvc_connected(void);
