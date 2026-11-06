// The event path shared by the USB input and the network test endpoint.
#pragma once
#include <stdbool.h>
#include <stdint.h>

// Coordinates are normalised to 0..32767. Safe to call from any task.
void relay_submit(bool down, uint16_t x, uint16_t y);
// Persistent edge-gesture switch (default NVS, namespace "cfg"). Applies at
// the next press so the current contact cannot stick. In standard HID mode
// the engine stays disabled and this reports off; the saved setting is kept.
void relay_set_gestures(bool enabled);
bool relay_get_gestures(void);
// D-pad mode flag: volatile copy maintained by relay_task, safe from any task.
bool relay_get_dpad(void);
// Persistent HID mode (default NVS, namespace "cfg", key "std_hid").
// Latched at boot before BLE init; saving needs a reboot and re-pair.
bool relay_get_standard_hid(void);
void relay_save_standard_hid(bool standard);
// Short button press asking for Back. Consumed by relay_task, obeys the switch.
void relay_request_back(void);
