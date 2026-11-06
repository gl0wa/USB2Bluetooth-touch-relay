// USB host: finds the touch interface on the attached device and forwards touches.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    bool started;
    bool attached;
    bool has_touch;
    uint16_t vid, pid;
    uint8_t kind;      // ht_kind_t of the chosen report
    uint8_t iface;
    uint8_t contacts;
    uint32_t reports;
    uint32_t touches;
    uint32_t errors;
} usb_touch_status_t;

// Takes over the USB PHY, which ends the USB Serial/JTAG console until reset.
void usb_touch_start(void);
void usb_touch_get_status(usb_touch_status_t *out);

// Copies the report descriptor of interface slot `index` for diagnostics.
// Returns its length, or 0 when the slot is empty.
size_t usb_touch_get_descriptor(int index, uint8_t *out, size_t max, uint8_t *iface);
