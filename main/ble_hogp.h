// HID over GATT peripheral presenting one absolute touch contact plus Back/Home keys.
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool connected;
    bool encrypted;
    bool subscribed;
    bool standard_hid;
    int bonds;
    uint16_t conn_itvl; // units of 1.25 ms
    uint32_t sent;
    uint32_t send_failed;
    uint32_t keys_sent;
} ble_hogp_status_t;

// Latches the HID mode for the whole run. Call before ble_hogp_init.
// false (default): Android mode, one Input Report characteristic.
// true: standard mode, normal HOGP report IDs, keys unavailable.
void ble_hogp_set_standard_hid(bool standard);
void ble_hogp_init(void);

// Returns false if the report could not be queued; the caller may retry.
bool ble_hogp_send_touch(bool down, uint16_t x, uint16_t y);
bool ble_hogp_send_keys(uint8_t bits);
bool ble_hogp_ready(void);
void ble_hogp_get_status(ble_hogp_status_t *out);

// Forgets every bonded host and restarts advertising.
void ble_hogp_clear_bonds(void);
