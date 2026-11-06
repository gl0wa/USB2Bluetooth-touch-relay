// Parses a USB HID report descriptor and extracts a single touch from input
// reports. Pure C with no ESP-IDF dependency, so it is unit tested on the host.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HT_MAX_CONTACTS 10
#define HT_COORD_MAX 32767

typedef struct {
    bool present;
    bool is_signed;
    uint16_t bit_off; // offset from the first byte after the report ID
    uint8_t bits;
    int32_t lmin;
    int32_t lmax;
} ht_field_t;

typedef struct {
    ht_field_t tip; // Tip Switch, or Button 1 for an absolute mouse
    ht_field_t id;  // Contact ID (optional)
    ht_field_t x;
    ht_field_t y;
} ht_contact_t;

typedef enum {
    HT_KIND_NONE = 0,
    HT_KIND_ABS_MOUSE = 1,
    HT_KIND_DIGITIZER = 2,
    HT_KIND_TOUCHSCREEN = 3,
} ht_kind_t;

typedef struct {
    ht_kind_t kind;
    uint8_t report_id; // 0 when the device does not use report IDs
    uint8_t contacts;
    ht_contact_t c[HT_MAX_CONTACTS];
    ht_field_t count; // Contact Count (optional)
} ht_layout_t;

typedef struct {
    bool tracking;
    bool seen;          // tracked contact appeared in the current frame
    int32_t active_id;
    int32_t frame_left; // contacts still expected in a hybrid-mode frame
} ht_state_t;

typedef struct {
    bool down;
    uint16_t x; // normalised to 0..HT_COORD_MAX
    uint16_t y;
} ht_touch_t;

// Picks the best touch-capable report in the descriptor. Returns false if none.
bool ht_parse_descriptor(const uint8_t *desc, size_t len, ht_layout_t *out);

// Decodes one input report, including its report ID byte if the layout has one.
// Returns true when `out` holds a new state for the tracked contact.
bool ht_decode(const ht_layout_t *l, const uint8_t *report, size_t len, ht_state_t *st, ht_touch_t *out);
