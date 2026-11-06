// Edge-swipe gestures to Back/Home keys plus a D-pad mode. Pure C, no
// ESP-IDF, no allocation. Coordinates are normalised to 0..32767.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define GESTURE_KEY_BACK 1   // same bits as the BLE HID keys report
#define GESTURE_KEY_HOME 2
#define GESTURE_KEY_UP 4
#define GESTURE_KEY_DOWN 8
#define GESTURE_KEY_LEFT 16
#define GESTURE_KEY_RIGHT 32
#define GESTURE_KEY_SELECT 64

typedef struct {
    bool enabled;
    uint16_t edge;         // strip width at the left/bottom/top edges
    uint16_t trigger;      // travel along the swipe direction that fires
    uint16_t max_off_axis; // travel across it tolerated before it is not a gesture
    uint32_t hold_ms;      // edge press held this long replays as a touch
    uint16_t dpad_step;    // travel on the dominant axis per D-pad arrow
    uint16_t dpad_tap;     // movement still counted as a tap (Select)
    uint32_t dpad_idle_ms; // idle time with no contact that exits D-pad mode
} gesture_cfg_t;

typedef enum {
    GESTURE_IDLE = 0,
    GESTURE_HELD,
    GESTURE_PASS,
    GESTURE_FIRED,
    GESTURE_DPAD,
} gesture_state_t;

typedef struct {
    gesture_cfg_t cfg;
    gesture_state_t st;
    bool effective; // enabled value captured at the current press
    bool left_cand;
    bool bottom_cand;
    bool top_cand;
    uint16_t sx, sy; // press start point
    uint32_t t0;
    bool dpad; // D-pad mode active
    uint32_t dpad_last; // last D-pad activity (wrap-safe ms)
    uint16_t ax, ay; // D-pad anchor for step detection
    bool dpad_moved; // a D-pad arrow was emitted this contact
    bool dpad_far; // movement beyond dpad_tap seen this contact
} gesture_t;

typedef struct {
    bool is_key;
    uint8_t key; // GESTURE_KEY_* when is_key
    bool down;   // touch down/up when !is_key, key down/up when is_key
    uint16_t x, y;
} gesture_ev_t;

typedef struct {
    uint8_t n; // 0..3
    gesture_ev_t ev[3];
} gesture_out_t;

gesture_cfg_t gesture_default_cfg(void);
void gesture_init(gesture_t *g, gesture_cfg_t cfg);
void gesture_set_enabled(gesture_t *g, bool enabled);
bool gesture_is_passthrough(const gesture_t *g);
bool gesture_dpad_active(const gesture_t *g);

// down=true is press/move, down=false is release. now_ms wraps at 2^32.
void gesture_feed(gesture_t *g, bool down, uint16_t x, uint16_t y, uint32_t now_ms, gesture_out_t *out);
// Fires the hold expiry when no new report arrives. Also exits D-pad mode
// after dpad_idle_ms with no contact.
void gesture_tick(gesture_t *g, uint32_t now_ms, gesture_out_t *out);
