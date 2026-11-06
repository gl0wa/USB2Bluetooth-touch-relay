// HID over GATT peripheral presenting one absolute touch contact.
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool connected;
    bool encrypted;
    bool subscribed;
    int bonds;
    uint16_t conn_itvl; // units of 1.25 ms
    uint32_t sent;
    uint32_t send_failed;
} ble_hogp_status_t;

void ble_hogp_init(void);

// Returns false if the report could not be queued; the caller may retry.
bool ble_hogp_send_touch(bool down, uint16_t x, uint16_t y);
bool ble_hogp_ready(void);
void ble_hogp_get_status(ble_hogp_status_t *out);

// Forgets every bonded host and restarts advertising.
void ble_hogp_clear_bonds(void);
