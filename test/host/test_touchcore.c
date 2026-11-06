#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gesture.h"
#include "hid_touch.h"
#include "touch_map.h"
#include "panel_descriptors.h"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

// The touch part of the descriptor verified on the Shield through uhid; also
// the touch part of the BLE report map. The full BLE map appends the Consumer
// Back/Home collection (see keys_suffix below).
static const uint8_t mt_desc[] = {
    0x05,0x0D,0x09,0x04,0xA1,0x01,0x85,0x01,0x09,0x22,0xA1,0x02,0x09,0x42,0x15,0x00,0x25,0x01,0x75,0x01,
    0x95,0x01,0x81,0x02,0x09,0x32,0x81,0x02,0x75,0x06,0x95,0x01,0x81,0x03,0x09,0x51,0x25,0x0F,0x75,0x08,
    0x95,0x01,0x81,0x02,0x05,0x01,0x09,0x30,0x09,0x31,0x15,0x00,0x26,0xFF,0x7F,0x35,0x00,0x46,0xFF,0x7F,
    0x75,0x10,0x95,0x02,0x81,0x02,0xC0,0x05,0x0D,0x09,0x54,0x15,0x00,0x25,0x0A,0x75,0x08,0x95,0x01,0x81,
    0x02,0xC0};

// Two fingers per report, 12-bit coordinates packed across byte boundaries.
static const uint8_t two_finger_desc[] = {
    0x05,0x0D,0x09,0x04,0xA1,0x01,0x85,0x05,
    0x09,0x22,0xA1,0x02,0x09,0x42,0x15,0x00,0x25,0x01,0x75,0x01,0x95,0x01,0x81,0x02,
    0x75,0x03,0x09,0x51,0x25,0x07,0x81,0x02,0x75,0x04,0x81,0x03,
    0x05,0x01,0x09,0x30,0x26,0xFF,0x0F,0x75,0x0C,0x95,0x01,0x81,0x02,0x09,0x31,0x81,0x02,0xC0,
    0x05,0x0D,0x09,0x22,0xA1,0x02,0x09,0x42,0x15,0x00,0x25,0x01,0x75,0x01,0x95,0x01,0x81,0x02,
    0x75,0x03,0x09,0x51,0x25,0x07,0x81,0x02,0x75,0x04,0x81,0x03,
    0x05,0x01,0x09,0x30,0x26,0xFF,0x0F,0x75,0x0C,0x95,0x01,0x81,0x02,0x09,0x31,0x81,0x02,0xC0,
    0x05,0x0D,0x09,0x54,0x25,0x02,0x75,0x08,0x95,0x01,0x81,0x02,0xC0};

// Absolute mouse without report IDs: buttons then 16-bit X/Y.
static const uint8_t abs_mouse_desc[] = {
    0x05,0x01,0x09,0x02,0xA1,0x01,0x09,0x01,0xA1,0x00,0x05,0x09,0x19,0x01,0x29,0x03,0x15,0x00,0x25,0x01,
    0x75,0x01,0x95,0x03,0x81,0x02,0x75,0x05,0x95,0x01,0x81,0x03,0x05,0x01,0x09,0x30,0x09,0x31,0x15,0x00,
    0x26,0xFF,0x7F,0x75,0x10,0x95,0x02,0x81,0x02,0xC0,0xC0};

// Relative mouse: must be rejected.
static const uint8_t rel_mouse_desc[] = {
    0x05,0x01,0x09,0x02,0xA1,0x01,0x09,0x01,0xA1,0x00,0x05,0x09,0x19,0x01,0x29,0x03,0x15,0x00,0x25,0x01,
    0x75,0x01,0x95,0x03,0x81,0x02,0x75,0x05,0x95,0x01,0x81,0x03,0x05,0x01,0x09,0x30,0x09,0x31,0x15,0x81,
    0x25,0x7F,0x75,0x08,0x95,0x02,0x81,0x06,0xC0,0xC0};

static void test_mt(void)
{
    ht_layout_t l;
    CHECK(ht_parse_descriptor(mt_desc, sizeof mt_desc, &l));
    CHECK(l.kind == HT_KIND_TOUCHSCREEN && l.report_id == 1 && l.contacts == 1);
    CHECK(l.count.present && l.c[0].id.present);

    ht_state_t st = {0};
    ht_touch_t t;
    uint8_t down[] = {1, 0x03, 0x00, 0xFF, 0x7F, 0x00, 0x40, 1};
    CHECK(ht_decode(&l, down, sizeof down, &st, &t));
    CHECK(t.down && t.x == 32767 && t.y == 0x4000);

    uint8_t up[] = {1, 0x00, 0x00, 0xFF, 0x7F, 0x00, 0x40, 0};
    CHECK(ht_decode(&l, up, sizeof up, &st, &t));
    CHECK(!t.down && !st.tracking);

    uint8_t other_id[] = {2, 0x03, 0x00, 0, 0, 0, 0, 1};
    CHECK(!ht_decode(&l, other_id, sizeof other_id, &st, &t));
    CHECK(!ht_decode(&l, down, 3, &st, &t)); // truncated report
}

static void pack12(uint8_t *r, uint8_t tip, uint8_t id, uint16_t x, uint16_t y)
{
    r[0] = (uint8_t)(tip | (id << 1));
    r[1] = x & 0xFF;
    r[2] = (uint8_t)((x >> 8) | ((y & 0x0F) << 4));
    r[3] = y >> 4;
}

static void test_two_fingers(void)
{
    ht_layout_t l;
    CHECK(ht_parse_descriptor(two_finger_desc, sizeof two_finger_desc, &l));
    CHECK(l.contacts == 2 && l.report_id == 5);
    CHECK(l.c[1].x.bit_off == 40 && l.c[1].y.bits == 12);

    ht_state_t st = {0};
    ht_touch_t t;
    uint8_t r[10] = {5};
    pack12(&r[1], 0, 0, 0, 0);
    pack12(&r[5], 1, 3, 4095, 2048); // only the second slot is touching
    r[9] = 1;
    CHECK(ht_decode(&l, r, sizeof r, &st, &t));
    CHECK(t.down && t.x == 32767 && st.active_id == 3);

    // Tracked contact stays pinned when a new finger arrives in the first slot.
    pack12(&r[1], 1, 1, 100, 100);
    pack12(&r[5], 1, 3, 0, 0);
    r[9] = 2;
    CHECK(ht_decode(&l, r, sizeof r, &st, &t));
    CHECK(t.down && t.x == 0 && st.active_id == 3);

    // All contacts gone with count 0 ends the touch.
    memset(&r[1], 0, 9);
    CHECK(ht_decode(&l, r, sizeof r, &st, &t));
    CHECK(!t.down && !st.tracking);
}

static void test_hybrid(void)
{
    ht_layout_t l;
    CHECK(ht_parse_descriptor(mt_desc, sizeof mt_desc, &l));
    ht_state_t st = {0};
    ht_touch_t t;

    // One contact per report: tracked contact 1, then the frame's second contact.
    uint8_t a[] = {1, 0x03, 1, 0x10, 0, 0x20, 0, 2};
    uint8_t b[] = {1, 0x03, 2, 0x30, 0, 0x40, 0, 0};
    CHECK(ht_decode(&l, a, sizeof a, &st, &t) && t.down);
    CHECK(!ht_decode(&l, b, sizeof b, &st, &t)); // continuation is not a release
    CHECK(st.tracking && st.active_id == 1);
    CHECK(ht_decode(&l, a, sizeof a, &st, &t) && t.down);

    // A complete frame without the tracked contact ends it.
    uint8_t other[] = {1, 0x03, 2, 0x30, 0, 0x40, 0, 1};
    CHECK(!ht_decode(&l, b, sizeof b, &st, &t));
    CHECK(ht_decode(&l, other, sizeof other, &st, &t) && !t.down);
    CHECK(!st.tracking);
}

// Layout of the real panel: report 0x0D, ten 5-byte contacts, scan time, count.
static void test_real_panel(void)
{
    ht_layout_t l;
    CHECK(ht_parse_descriptor(panel_touch_desc, sizeof panel_touch_desc, &l));
    CHECK(l.kind == HT_KIND_TOUCHSCREEN && l.report_id == 0x0D && l.contacts == 10);
    CHECK(l.c[0].tip.bit_off == 0 && l.c[0].id.bit_off == 4 && l.c[0].id.bits == 4);
    CHECK(l.c[0].x.bit_off == 8 && l.c[0].y.bit_off == 24 && l.c[1].tip.bit_off == 40);
    CHECK(l.c[0].x.lmax == 0x3FFF && l.c[0].y.lmax == 0x257F);
    CHECK(l.count.present && l.count.bit_off == 416 && l.count.bits == 8);

    uint8_t r[54] = {0x0D};
    r[1] = 0x01 | (2 << 4); // tip, contact 2
    r[2] = 0xFF; r[3] = 0x3F;
    r[4] = 0xBF; r[5] = 0x12;
    r[53] = 1;
    ht_state_t st = {0};
    ht_touch_t t;
    CHECK(ht_decode(&l, r, sizeof r, &st, &t));
    CHECK(t.down && t.x == 32767 && t.y == 0x12BF * 32767 / 0x257F && st.active_id == 2);
    r[1] = 2 << 4; // lift
    CHECK(ht_decode(&l, r, sizeof r, &st, &t));
    CHECK(!t.down && !st.tracking);

    CHECK(!ht_parse_descriptor(panel_vendor_desc, sizeof panel_vendor_desc, &l));
    CHECK(ht_parse_descriptor(panel_mouse_desc, sizeof panel_mouse_desc, &l));
    CHECK(l.kind == HT_KIND_ABS_MOUSE && l.report_id == 7);
}

// Layout of the real panel's absolute-mouse interface: report 0x07, buttons,
// 16-bit X/Y, wheel. The panel sends all touches here, nothing on 0x0D.
static void test_real_panel_mouse(void)
{
    ht_layout_t l;
    CHECK(ht_parse_descriptor(panel_mouse_desc, sizeof panel_mouse_desc, &l));
    CHECK(l.kind == HT_KIND_ABS_MOUSE && l.report_id == 7 && l.contacts == 1);

    ht_state_t st = {0};
    ht_touch_t t;
    uint8_t press[] = {0x07, 0x01, 0x5D, 0x2C, 0xD7, 0x15, 0x00};
    CHECK(ht_decode(&l, press, sizeof press, &st, &t));
    CHECK(t.down && t.x == 0x2C5D * 32767 / 0x3FFF && t.y == 0x15D7 * 32767 / 0x257F);
    CHECK(st.tracking);

    uint8_t release[] = {0x07, 0x00, 0x5D, 0x2C, 0xD7, 0x15, 0x00};
    CHECK(ht_decode(&l, release, sizeof release, &st, &t));
    CHECK(!t.down && !st.tracking);

    // Hover with the button up while idle is not a touch: ht_decode reports
    // nothing (returns false) and starts no contact. That is the correct
    // behaviour for a mouse hover, so assert it.
    CHECK(!ht_decode(&l, release, sizeof release, &st, &t));
    CHECK(!st.tracking);

    uint8_t press2[] = {0x07, 0x01, 0xCC, 0x20, 0x88, 0x0C, 0x00};
    CHECK(ht_decode(&l, press2, sizeof press2, &st, &t));
    CHECK(t.down && t.x == 0x20CC * 32767 / 0x3FFF && t.y == 0x0C88 * 32767 / 0x257F);
    CHECK(st.tracking);
}

static void test_mice(void)
{
    ht_layout_t l;
    CHECK(ht_parse_descriptor(abs_mouse_desc, sizeof abs_mouse_desc, &l));
    CHECK(l.kind == HT_KIND_ABS_MOUSE && l.report_id == 0);
    ht_state_t st = {0};
    ht_touch_t t;
    uint8_t r[] = {0x01, 0xFF, 0x3F, 0xFF, 0x7F};
    CHECK(ht_decode(&l, r, sizeof r, &st, &t));
    CHECK(t.down && t.x == 0x3FFF && t.y == 32767);
    CHECK(!ht_parse_descriptor(rel_mouse_desc, sizeof rel_mouse_desc, &l));
}

static void test_robustness(void)
{
    ht_layout_t l;
    for (size_t n = 0; n < sizeof mt_desc; n++) { // ASan catches any overrun
        uint8_t *copy = malloc(n ? n : 1);
        memcpy(copy, mt_desc, n);
        ht_parse_descriptor(copy, n, &l);
        free(copy);
    }
    srand(1);
    for (int i = 0; i < 20000; i++) {
        uint8_t junk[96];
        for (size_t k = 0; k < sizeof junk; k++) {
            junk[k] = (uint8_t)rand();
        }
        if (ht_parse_descriptor(junk, sizeof junk, &l)) {
            ht_state_t st = {0};
            ht_touch_t t;
            ht_decode(&l, junk, sizeof junk, &st, &t);
        }
    }
}

static void test_map(void)
{
    touch_map_t m;
    touch_map_default(&m);
    uint16_t x, y;
    touch_map_apply(&m, 100, 200, &x, &y);
    CHECK(x == 100 && y == 200);
    m.rotation = 2;
    touch_map_apply(&m, 0, 32767, &x, &y);
    CHECK(x == 32767 && y == 0);
    m.rotation = 1;
    touch_map_apply(&m, 0, 0, &x, &y);
    CHECK(x == 32767 && y == 0);
    m.rotation = 3;
    touch_map_apply(&m, 0, 0, &x, &y);
    CHECK(x == 0 && y == 32767);
    touch_map_default(&m);
    m.mirror_x = true;
    touch_map_apply(&m, 0, 0, &x, &y);
    CHECK(x == 32767 && y == 0);
    touch_map_default(&m);
    m.x_min = 1000;
    m.x_max = 31000;
    touch_map_apply(&m, 500, 0, &x, &y);
    CHECK(x == 0);
    touch_map_apply(&m, 32000, 0, &x, &y);
    CHECK(x == 32767);
    touch_map_apply(&m, 16000, 0, &x, &y);
    CHECK(x == 16383);
}

// The Consumer key suffix appended to the BLE report map (report 2:
// bit0 AC Back, bit1 AC Home, bit2 Up, bit3 Down, bit4 Left, bit5 Right,
// bit6 Select). Byte-exact per the uhid-tested layout.
static const uint8_t keys_suffix[] = {
    0x05,0x0C,0x09,0x01,0xA1,0x01,0x85,0x02,0x0A,0x24,0x02,0x0A,0x23,0x02,0x09,0x42,0x09,0x43,0x09,0x44,
    0x09,0x45,0x09,0x41,0x15,0x00,0x25,0x01,0x75,0x01,0x95,0x07,0x81,0x02,0x95,0x01,0x81,0x03,0xC0};

static void test_report_map_keys(void)
{
    static const uint8_t expect[] = {
        0x05,0x0C,0x09,0x01,0xA1,0x01,0x85,0x02,0x0A,0x24,0x02,0x0A,0x23,0x02,0x09,0x42,0x09,0x43,0x09,0x44,
        0x09,0x45,0x09,0x41,0x15,0x00,0x25,0x01,0x75,0x01,0x95,0x07,0x81,0x02,0x95,0x01,0x81,0x03,0xC0};
    CHECK(sizeof keys_suffix == sizeof expect && memcmp(keys_suffix, expect, sizeof expect) == 0);
    CHECK(sizeof keys_suffix == 39);
    // The full BLE map (touch part + keys) still parses as the same touchscreen.
    uint8_t full[sizeof mt_desc + sizeof keys_suffix];
    memcpy(full, mt_desc, sizeof mt_desc);
    memcpy(full + sizeof mt_desc, keys_suffix, sizeof keys_suffix);
    ht_layout_t l;
    CHECK(ht_parse_descriptor(full, sizeof full, &l));
    CHECK(l.kind == HT_KIND_TOUCHSCREEN && l.report_id == 1 && l.contacts == 1);
}

static void g_init(gesture_t *g)
{
    gesture_init(g, gesture_default_cfg());
}

static void test_gesture_disabled(void)
{
    gesture_t g;
    g_init(&g);
    gesture_set_enabled(&g, false);
    gesture_out_t o;
    gesture_feed(&g, true, 100, 16383, 0, &o);
    CHECK(o.n == 1 && !o.ev[0].is_key && o.ev[0].down && o.ev[0].x == 100);
    gesture_feed(&g, true, 5000, 16383, 50, &o);
    CHECK(o.n == 1 && !o.ev[0].is_key && o.ev[0].down);
    gesture_feed(&g, false, 5000, 16383, 100, &o);
    CHECK(o.n == 1 && !o.ev[0].is_key && !o.ev[0].down);
}

static void test_gesture_centre(void)
{
    gesture_t g;
    g_init(&g);
    gesture_out_t o;
    gesture_feed(&g, true, 16383, 16383, 0, &o);
    CHECK(o.n == 1 && !o.ev[0].is_key && o.ev[0].down);
    // A press starting outside the strips passes through even at the edge.
    gesture_feed(&g, true, 100, 16383, 50, &o);
    CHECK(o.n == 1 && !o.ev[0].is_key && o.ev[0].down && o.ev[0].x == 100);
    gesture_feed(&g, false, 100, 16383, 100, &o);
    CHECK(o.n == 1 && !o.ev[0].is_key && !o.ev[0].down);
}

static void test_gesture_left_back(void)
{
    gesture_t g;
    g_init(&g);
    gesture_out_t o;
    gesture_feed(&g, true, 100, 16383, 0, &o);
    CHECK(o.n == 0);
    gesture_feed(&g, true, 2000, 16383, 50, &o);
    CHECK(o.n == 0);
    gesture_feed(&g, true, 100 + 3277, 16383, 100, &o);
    CHECK(o.n == 2 && o.ev[0].is_key && o.ev[0].key == GESTURE_KEY_BACK && o.ev[0].down);
    CHECK(o.ev[1].is_key && o.ev[1].key == GESTURE_KEY_BACK && !o.ev[1].down);
    gesture_feed(&g, true, 8000, 16383, 150, &o);
    CHECK(o.n == 0);
    gesture_feed(&g, false, 8000, 16383, 200, &o);
    CHECK(o.n == 0);
}

static void test_gesture_bottom_home(void)
{
    gesture_t g;
    g_init(&g);
    gesture_out_t o;
    gesture_feed(&g, true, 16383, 32000, 0, &o);
    CHECK(o.n == 0);
    gesture_feed(&g, true, 16383, 32000 - 3277, 100, &o);
    CHECK(o.n == 2 && o.ev[0].is_key && o.ev[0].key == GESTURE_KEY_HOME && o.ev[0].down);
    CHECK(o.ev[1].is_key && !o.ev[1].down);
    gesture_feed(&g, false, 16383, 28000, 150, &o);
    CHECK(o.n == 0);
}

static void test_gesture_tap(void)
{
    gesture_t g;
    g_init(&g);
    gesture_out_t o;
    gesture_feed(&g, true, 100, 16383, 0, &o);
    CHECK(o.n == 0);
    gesture_feed(&g, false, 150, 16400, 50, &o);
    CHECK(o.n == 2 && !o.ev[0].is_key && o.ev[0].down && o.ev[0].x == 100 && o.ev[0].y == 16383);
    CHECK(!o.ev[1].is_key && !o.ev[1].down && o.ev[1].x == 100 && o.ev[1].y == 16383);
}

static void test_gesture_tick_expiry(void)
{
    gesture_t g;
    g_init(&g);
    gesture_out_t o;
    gesture_feed(&g, true, 100, 16383, 1000, &o);
    CHECK(o.n == 0);
    gesture_tick(&g, 1000 + 249, &o);
    CHECK(o.n == 0);
    gesture_tick(&g, 1000 + 250, &o);
    CHECK(o.n == 1 && !o.ev[0].is_key && o.ev[0].down && o.ev[0].x == 100);
    gesture_tick(&g, 1000 + 500, &o);
    CHECK(o.n == 0);
    gesture_feed(&g, true, 500, 16383, 1000 + 500, &o);
    CHECK(o.n == 1 && !o.ev[0].is_key && o.ev[0].down && o.ev[0].x == 500);
    gesture_feed(&g, false, 500, 16383, 1000 + 550, &o);
    CHECK(o.n == 1 && !o.ev[0].down);
}

static void test_gesture_feed_expiry(void)
{
    gesture_t g;
    g_init(&g);
    gesture_out_t o;
    gesture_feed(&g, true, 100, 16383, 0, &o);
    CHECK(o.n == 0);
    gesture_feed(&g, true, 200, 16390, 300, &o);
    CHECK(o.n == 2 && !o.ev[0].is_key && o.ev[0].down && o.ev[0].x == 100);
    CHECK(!o.ev[1].is_key && o.ev[1].down && o.ev[1].x == 200 && o.ev[1].y == 16390);
    gesture_feed(&g, true, 300, 16390, 350, &o);
    CHECK(o.n == 1 && !o.ev[0].is_key);
    gesture_feed(&g, false, 300, 16390, 400, &o);
    CHECK(o.n == 1 && !o.ev[0].down);
}

static void test_gesture_off_axis(void)
{
    gesture_t g;
    g_init(&g);
    gesture_out_t o;
    gesture_feed(&g, true, 100, 16383, 0, &o);
    CHECK(o.n == 0);
    gesture_feed(&g, true, 150, 16383 + 2501, 50, &o);
    CHECK(o.n == 2 && !o.ev[0].is_key && o.ev[0].down && o.ev[0].x == 100);
    CHECK(o.n == 2 && o.ev[1].x == 150);
}

static void test_gesture_corner(void)
{
    gesture_t g;
    gesture_out_t o;
    g_init(&g);
    gesture_feed(&g, true, 100, 32000, 0, &o);
    CHECK(o.n == 0);
    gesture_feed(&g, true, 100 + 3277, 32000, 50, &o);
    CHECK(o.n == 2 && o.ev[0].key == GESTURE_KEY_BACK);
    gesture_feed(&g, false, 5000, 32000, 100, &o);
    CHECK(o.n == 0);

    g_init(&g);
    gesture_feed(&g, true, 100, 32000, 0, &o);
    CHECK(o.n == 0);
    gesture_feed(&g, true, 100, 32000 - 3277, 50, &o);
    CHECK(o.n == 2 && o.ev[0].key == GESTURE_KEY_HOME);
    gesture_feed(&g, false, 100, 28000, 100, &o);
    CHECK(o.n == 0);
}

static void test_gesture_second(void)
{
    gesture_t g;
    g_init(&g);
    gesture_out_t o;
    gesture_feed(&g, true, 100, 16383, 0, &o);
    gesture_feed(&g, true, 100 + 4000, 16383, 50, &o);
    CHECK(o.n == 2 && o.ev[0].is_key);
    gesture_feed(&g, false, 8000, 16383, 100, &o);
    CHECK(o.n == 0);
    gesture_feed(&g, true, 16383, 32000, 200, &o);
    CHECK(o.n == 0);
    gesture_feed(&g, true, 16383, 32000 - 4000, 250, &o);
    CHECK(o.n == 2 && o.ev[0].key == GESTURE_KEY_HOME);
    gesture_feed(&g, false, 16383, 28000, 300, &o);
    CHECK(o.n == 0);
}

static void test_gesture_idle_release(void)
{
    gesture_t g;
    g_init(&g);
    gesture_out_t o;
    gesture_feed(&g, false, 1000, 2000, 0, &o);
    CHECK(o.n == 1 && !o.ev[0].is_key && !o.ev[0].down);
}

static void test_gesture_wrap(void)
{
    gesture_t g;
    g_init(&g);
    gesture_out_t o;
    uint32_t t0 = 0xFFFFFF00u;
    gesture_feed(&g, true, 100, 16383, t0, &o);
    CHECK(o.n == 0);
    gesture_tick(&g, t0 + 100, &o);
    CHECK(o.n == 0);
    gesture_tick(&g, t0 + 300, &o);
    CHECK(o.n == 1 && !o.ev[0].is_key && o.ev[0].down);
}

static void test_gesture_toggle_mid(void)
{
    gesture_t g;
    g_init(&g);
    gesture_out_t o;
    gesture_feed(&g, true, 100, 16383, 0, &o);
    CHECK(o.n == 0);
    gesture_set_enabled(&g, false);
    gesture_tick(&g, 300, &o);
    CHECK(o.n == 1 && !o.ev[0].is_key && o.ev[0].down);
    gesture_feed(&g, false, 200, 16383, 350, &o);
    CHECK(o.n == 1 && !o.ev[0].down);
    gesture_feed(&g, true, 100, 16383, 400, &o);
    CHECK(o.n == 1 && !o.ev[0].is_key && o.ev[0].down);
    gesture_feed(&g, false, 100, 16383, 450, &o);
    CHECK(o.n == 1 && !o.ev[0].down);

    g_init(&g);
    gesture_feed(&g, true, 16383, 16383, 0, &o);
    CHECK(o.n == 1);
    gesture_set_enabled(&g, false);
    gesture_feed(&g, false, 16383, 16383, 50, &o);
    CHECK(o.n == 1 && !o.ev[0].down);
    gesture_feed(&g, true, 100, 16383, 100, &o);
    CHECK(o.n == 1 && !o.ev[0].is_key);
    gesture_feed(&g, false, 100, 16383, 150, &o);
    CHECK(o.n == 1 && !o.ev[0].down);
}

static void dpad_enter(gesture_t *g, gesture_out_t *o, uint32_t *now)
{
    // Top strip downward swipe toggles D-pad on: no key, no touch.
    gesture_feed(g, true, 16383, 100, *now, o);
    CHECK(o->n == 0);
    *now += 50;
    gesture_feed(g, true, 16383, 100 + 4000, *now, o);
    CHECK(o->n == 0);
    CHECK(gesture_dpad_active(g));
    *now += 50;
    gesture_feed(g, false, 16383, 4100, *now, o);
    CHECK(o->n == 0);
    CHECK(gesture_dpad_active(g));
}

static void test_dpad_enter_toggle(void)
{
    gesture_t g;
    g_init(&g);
    gesture_out_t o;
    uint32_t now = 0;
    CHECK(!gesture_dpad_active(&g));
    dpad_enter(&g, &o, &now);
    // Second top swipe toggles off, still no key and no touch.
    gesture_feed(&g, true, 16383, 100, now, &o);
    CHECK(o.n == 0);
    now += 50;
    gesture_feed(&g, true, 16383, 100 + 4000, now, &o);
    CHECK(o.n == 0);
    CHECK(!gesture_dpad_active(&g));
    now += 50;
    gesture_feed(&g, false, 16383, 4100, now, &o);
    CHECK(o.n == 0);
    CHECK(!gesture_dpad_active(&g));
}

static void test_dpad_tap_select(void)
{
    gesture_t g;
    g_init(&g);
    gesture_out_t o;
    uint32_t now = 0;
    dpad_enter(&g, &o, &now);
    // Centre tap gives one Select and no touch.
    gesture_feed(&g, true, 16383, 16383, now, &o);
    CHECK(o.n == 0);
    now += 50;
    gesture_feed(&g, false, 16390, 16390, now, &o);
    CHECK(o.n == 2 && o.ev[0].is_key && o.ev[0].key == GESTURE_KEY_SELECT && o.ev[0].down);
    CHECK(o.ev[1].is_key && o.ev[1].key == GESTURE_KEY_SELECT && !o.ev[1].down);
    CHECK(gesture_dpad_active(&g));
}

static void test_dpad_arrows(void)
{
    gesture_t g;
    gesture_out_t o;
    g_init(&g);
    uint32_t now = 0;
    dpad_enter(&g, &o, &now);
    // Right.
    gesture_feed(&g, true, 10000, 10000, now, &o);
    CHECK(o.n == 0);
    now += 20;
    gesture_feed(&g, true, 10000 + 2500, 10000, now, &o);
    CHECK(o.n == 2 && o.ev[0].is_key && o.ev[0].key == GESTURE_KEY_RIGHT);
    now += 20;
    gesture_feed(&g, false, 12500, 10000, now, &o);
    CHECK(o.n == 0); // arrow produced, so no Select on release
    now += 20;
    // Left.
    gesture_feed(&g, true, 10000, 10000, now, &o);
    now += 20;
    gesture_feed(&g, true, 10000 - 2500, 10000, now, &o);
    CHECK(o.n == 2 && o.ev[0].is_key && o.ev[0].key == GESTURE_KEY_LEFT);
    now += 20;
    gesture_feed(&g, false, 7500, 10000, now, &o);
    CHECK(o.n == 0);
    now += 20;
    // Up means y decreasing.
    gesture_feed(&g, true, 10000, 10000, now, &o);
    now += 20;
    gesture_feed(&g, true, 10000, 10000 - 2500, now, &o);
    CHECK(o.n == 2 && o.ev[0].is_key && o.ev[0].key == GESTURE_KEY_UP);
    now += 20;
    gesture_feed(&g, false, 10000, 7500, now, &o);
    CHECK(o.n == 0);
    now += 20;
    // Down.
    gesture_feed(&g, true, 10000, 10000, now, &o);
    now += 20;
    gesture_feed(&g, true, 10000, 10000 + 2500, now, &o);
    CHECK(o.n == 2 && o.ev[0].is_key && o.ev[0].key == GESTURE_KEY_DOWN);
    now += 20;
    gesture_feed(&g, false, 10000, 12500, now, &o);
    CHECK(o.n == 0);
}

static void test_dpad_long_drag(void)
{
    gesture_t g;
    g_init(&g);
    gesture_out_t o;
    uint32_t now = 0;
    dpad_enter(&g, &o, &now);
    gesture_feed(&g, true, 5000, 15000, now, &o);
    CHECK(o.n == 0);
    int arrows = 0;
    uint16_t x = 5000;
    for (int i = 0; i < 3; i++) {
        now += 20;
        x += 2500;
        gesture_feed(&g, true, x, 15000, now, &o);
        CHECK(o.n == 2 && o.ev[0].is_key && o.ev[0].key == GESTURE_KEY_RIGHT);
        arrows++;
    }
    CHECK(arrows == 3);
    now += 20;
    gesture_feed(&g, false, x, 15000, now, &o);
    CHECK(o.n == 0);
}

static void test_dpad_dominant_axis(void)
{
    gesture_t g;
    g_init(&g);
    gesture_out_t o;
    uint32_t now = 0;
    dpad_enter(&g, &o, &now);
    // Diagonal with dominant x gives Right.
    gesture_feed(&g, true, 10000, 10000, now, &o);
    now += 20;
    gesture_feed(&g, true, 10000 + 3000, 10000 + 1000, now, &o);
    CHECK(o.n == 2 && o.ev[0].key == GESTURE_KEY_RIGHT);
    now += 20;
    gesture_feed(&g, false, 13000, 11000, now, &o);
    CHECK(o.n == 0);
    now += 20;
    // Diagonal with dominant y (up) gives Up.
    gesture_feed(&g, true, 10000, 10000, now, &o);
    now += 20;
    gesture_feed(&g, true, 10000 + 1000, 10000 - 3000, now, &o);
    CHECK(o.n == 2 && o.ev[0].key == GESTURE_KEY_UP);
    now += 20;
    gesture_feed(&g, false, 11000, 7000, now, &o);
    CHECK(o.n == 0);
}

static void test_dpad_slow_drag(void)
{
    gesture_t g;
    g_init(&g);
    gesture_out_t o;
    uint32_t now = 0;
    dpad_enter(&g, &o, &now);
    gesture_feed(&g, true, 8000, 8000, now, &o);
    // Slow: large time gaps must still give arrows, one per input.
    now += 2000;
    gesture_feed(&g, true, 8000 + 2500, 8000, now, &o);
    CHECK(o.n == 2 && o.ev[0].key == GESTURE_KEY_RIGHT);
    now += 2000;
    gesture_feed(&g, true, 8000 + 5000, 8000, now, &o);
    CHECK(o.n == 2 && o.ev[0].key == GESTURE_KEY_RIGHT);
    now += 20;
    gesture_feed(&g, false, 13000, 8000, now, &o);
    CHECK(o.n == 0);
}

static void test_dpad_wobble_select(void)
{
    gesture_t g;
    g_init(&g);
    gesture_out_t o;
    uint32_t now = 0;
    dpad_enter(&g, &o, &now);
    // Small wobble below dpad_tap still gives Select.
    gesture_feed(&g, true, 15000, 15000, now, &o);
    now += 20;
    gesture_feed(&g, true, 15000 + 500, 15000 + 500, now, &o);
    CHECK(o.n == 0);
    now += 20;
    gesture_feed(&g, true, 15000 - 500, 15000, now, &o);
    CHECK(o.n == 0);
    now += 20;
    gesture_feed(&g, false, 15000, 15000, now, &o);
    CHECK(o.n == 2 && o.ev[0].is_key && o.ev[0].key == GESTURE_KEY_SELECT);
}

static void test_dpad_dead_zone(void)
{
    gesture_t g;
    g_init(&g);
    gesture_out_t o;
    uint32_t now = 0;
    dpad_enter(&g, &o, &now);
    // Movement between dpad_tap (1200) and dpad_step (2500) gives nothing.
    gesture_feed(&g, true, 15000, 15000, now, &o);
    now += 20;
    gesture_feed(&g, true, 15000 + 1500, 15000, now, &o);
    CHECK(o.n == 0);
    now += 20;
    gesture_feed(&g, false, 16500, 15000, now, &o);
    CHECK(o.n == 0);
    CHECK(gesture_dpad_active(&g));
}

static void test_dpad_edge_priority(void)
{
    gesture_t g;
    g_init(&g);
    gesture_out_t o;
    uint32_t now = 0;
    dpad_enter(&g, &o, &now);
    // Left swipe still gives Back and the mode stays on.
    gesture_feed(&g, true, 100, 16383, now, &o);
    CHECK(o.n == 0);
    now += 50;
    gesture_feed(&g, true, 100 + 4000, 16383, now, &o);
    CHECK(o.n == 2 && o.ev[0].is_key && o.ev[0].key == GESTURE_KEY_BACK);
    CHECK(gesture_dpad_active(&g));
    now += 50;
    gesture_feed(&g, false, 5000, 16383, now, &o);
    CHECK(o.n == 0);
    CHECK(gesture_dpad_active(&g));
    now += 50;
    // Bottom swipe still gives Home and the mode stays on.
    gesture_feed(&g, true, 16383, 32000, now, &o);
    CHECK(o.n == 0);
    now += 50;
    gesture_feed(&g, true, 16383, 32000 - 4000, now, &o);
    CHECK(o.n == 2 && o.ev[0].is_key && o.ev[0].key == GESTURE_KEY_HOME);
    CHECK(gesture_dpad_active(&g));
    now += 50;
    gesture_feed(&g, false, 16383, 28000, now, &o);
    CHECK(o.n == 0);
    CHECK(gesture_dpad_active(&g));
}

static void test_dpad_strip_hold(void)
{
    gesture_t g;
    g_init(&g);
    gesture_out_t o;
    uint32_t now = 0;
    dpad_enter(&g, &o, &now);
    // Strip hold expiry in D-pad mode emits no touch; the contact becomes
    // a D-pad contact anchored at the start. The hold is longer in D-pad
    // mode (dpad_hold_ms).
    gesture_feed(&g, true, 100, 16383, now, &o);
    CHECK(o.n == 0);
    gesture_tick(&g, now + 799, &o);
    CHECK(o.n == 0);
    CHECK(!gesture_is_passthrough(&g));
    gesture_tick(&g, now + 800, &o);
    CHECK(o.n == 0);
    CHECK(!gesture_is_passthrough(&g));
    // No arrow and no excursion: release gives Select, still no touch.
    now += 900;
    gesture_feed(&g, true, 100 + 2500, 16383, now, &o);
    CHECK(o.n == 2 && o.ev[0].key == GESTURE_KEY_RIGHT);
    now += 20;
    gesture_feed(&g, false, 2600, 16383, now, &o);
    CHECK(o.n == 0);
    // Quick strip tap in D-pad mode is a Select with no touch.
    now += 50;
    gesture_feed(&g, true, 100, 16383, now, &o);
    CHECK(o.n == 0);
    now += 50;
    gesture_feed(&g, false, 120, 16390, now, &o);
    CHECK(o.n == 2 && o.ev[0].is_key && o.ev[0].key == GESTURE_KEY_SELECT);
    for (uint8_t i = 0; i < o.n; i++) {
        CHECK(o.ev[i].is_key);
    }
}

static void test_dpad_exit_wide_zone(void)
{
    gesture_t g;
    g_init(&g);
    gesture_out_t o;
    uint32_t now = 0;
    dpad_enter(&g, &o, &now);
    CHECK(gesture_dpad_active(&g));
    // 6% of height is below the wide top zone but above the normal edge.
    gesture_feed(&g, true, 16383, 1966, now, &o);
    CHECK(o.n == 0);
    now += 50;
    gesture_feed(&g, true, 16383, 1966 + 4000, now, &o);
    CHECK(o.n == 0);
    CHECK(!gesture_dpad_active(&g));
    now += 50;
    gesture_feed(&g, false, 16383, 5966, now, &o);
    CHECK(o.n == 0);
    CHECK(!gesture_dpad_active(&g));
}

static void test_dpad_exit_slow(void)
{
    gesture_t g;
    g_init(&g);
    gesture_out_t o;
    uint32_t now = 0;
    dpad_enter(&g, &o, &now);
    gesture_feed(&g, true, 16383, 100, now, &o);
    CHECK(o.n == 0);
    // A slower swipe up to 0.8 s still exits.
    now += 600;
    gesture_feed(&g, true, 16383, 100 + 4000, now, &o);
    CHECK(o.n == 0);
    CHECK(!gesture_dpad_active(&g));
    now += 50;
    gesture_feed(&g, false, 16383, 4100, now, &o);
    CHECK(o.n == 0);
    CHECK(!gesture_dpad_active(&g));
}

static void test_normal_wide_zone_passthrough(void)
{
    gesture_t g;
    g_init(&g);
    gesture_out_t o;
    // 6% height is not an edge in normal mode: passes through as touch.
    gesture_feed(&g, true, 16383, 1966, 0, &o);
    CHECK(o.n == 1 && !o.ev[0].is_key && o.ev[0].down && o.ev[0].x == 16383 && o.ev[0].y == 1966);
    gesture_feed(&g, true, 16383, 3000, 50, &o);
    CHECK(o.n == 1 && !o.ev[0].is_key && o.ev[0].down);
    gesture_feed(&g, false, 16383, 3000, 100, &o);
    CHECK(o.n == 1 && !o.ev[0].is_key && !o.ev[0].down);
    CHECK(!gesture_dpad_active(&g));
}

static void test_normal_top_hold_replay(void)
{
    gesture_t g;
    g_init(&g);
    gesture_out_t o;
    // Top-strip hold in normal mode still replays at hold_ms (250 ms).
    gesture_feed(&g, true, 16383, 100, 0, &o);
    CHECK(o.n == 0);
    gesture_tick(&g, 249, &o);
    CHECK(o.n == 0);
    gesture_tick(&g, 250, &o);
    CHECK(o.n == 1 && !o.ev[0].is_key && o.ev[0].down && o.ev[0].x == 16383 && o.ev[0].y == 100);

    g_init(&g);
    gesture_feed(&g, true, 16383, 100, 0, &o);
    CHECK(o.n == 0);
    // Small move after 600 ms: hold long expired, so replay as touch.
    gesture_feed(&g, true, 16383, 500, 600, &o);
    CHECK(o.n == 2 && !o.ev[0].is_key && o.ev[0].down && o.ev[0].x == 16383 && o.ev[0].y == 100);
    CHECK(!o.ev[1].is_key && o.ev[1].down && o.ev[1].x == 16383 && o.ev[1].y == 500);
}

static void test_dpad_up_from_top(void)
{
    gesture_t g;
    g_init(&g);
    gesture_out_t o;
    uint32_t now = 0;
    dpad_enter(&g, &o, &now);
    gesture_feed(&g, true, 16383, 2600, now, &o);
    CHECK(o.n == 0);
    // Move up past dpad_tap: the top candidate drops and the contact
    // becomes a D-pad contact promptly, without waiting for the hold.
    now += 50;
    gesture_feed(&g, true, 16383, 1300, now, &o);
    CHECK(o.n == 0);
    CHECK(gesture_dpad_active(&g));
    CHECK(!gesture_is_passthrough(&g));
    now += 50;
    gesture_feed(&g, true, 16383, 0, now, &o);
    CHECK(o.n == 2 && o.ev[0].is_key && o.ev[0].key == GESTURE_KEY_UP);
    CHECK(gesture_dpad_active(&g));
    now += 20;
    gesture_feed(&g, false, 16383, 0, now, &o);
    CHECK(o.n == 0);
    CHECK(gesture_dpad_active(&g));
}

static void test_dpad_hold_expires_to_dpad(void)
{
    gesture_t g;
    g_init(&g);
    gesture_out_t o;
    uint32_t now = 0;
    dpad_enter(&g, &o, &now);
    gesture_feed(&g, true, 16383, 100, now, &o);
    CHECK(o.n == 0);
    uint32_t t0 = now;
    gesture_tick(&g, t0 + 799, &o);
    CHECK(o.n == 0);
    CHECK(!gesture_is_passthrough(&g));
    gesture_tick(&g, t0 + 800, &o);
    CHECK(o.n == 0);
    CHECK(!gesture_is_passthrough(&g));
    // A swipe taking over 800 ms gives Down arrows, not exit.
    now = t0 + 850;
    gesture_feed(&g, true, 16383, 100 + 2500, now, &o);
    CHECK(o.n == 2 && o.ev[0].is_key && o.ev[0].key == GESTURE_KEY_DOWN);
    CHECK(gesture_dpad_active(&g));
    now += 20;
    gesture_feed(&g, false, 16383, 2600, now, &o);
    CHECK(o.n == 0);
    CHECK(gesture_dpad_active(&g));
}

static void test_dpad_top_tap_select(void)
{
    gesture_t g;
    g_init(&g);
    gesture_out_t o;
    uint32_t now = 0;
    dpad_enter(&g, &o, &now);
    // Quick tap inside the wide top zone stays Select.
    gesture_feed(&g, true, 16383, 1500, now, &o);
    CHECK(o.n == 0);
    now += 50;
    gesture_feed(&g, false, 16390, 1510, now, &o);
    CHECK(o.n == 2 && o.ev[0].is_key && o.ev[0].key == GESTURE_KEY_SELECT && o.ev[0].down);
    CHECK(o.ev[1].is_key && o.ev[1].key == GESTURE_KEY_SELECT && !o.ev[1].down);
    for (uint8_t i = 0; i < o.n; i++) {
        CHECK(o.ev[i].is_key);
    }
    CHECK(gesture_dpad_active(&g));
}

static void test_dpad_below_zone_down(void)
{
    gesture_t g;
    g_init(&g);
    gesture_out_t o;
    uint32_t now = 0;
    dpad_enter(&g, &o, &now);
    // A Down swipe starting below the wide zone gives Down, not exit.
    gesture_feed(&g, true, 16383, 5000, now, &o);
    CHECK(o.n == 0);
    now += 20;
    gesture_feed(&g, true, 16383, 5000 + 2500, now, &o);
    CHECK(o.n == 2 && o.ev[0].is_key && o.ev[0].key == GESTURE_KEY_DOWN);
    CHECK(gesture_dpad_active(&g));
    now += 20;
    gesture_feed(&g, false, 16383, 7500, now, &o);
    CHECK(o.n == 0);
    CHECK(gesture_dpad_active(&g));
}

static void test_dpad_idle(void)
{
    gesture_t g;
    g_init(&g);
    gesture_out_t o;
    uint32_t now = 0;
    dpad_enter(&g, &o, &now);
    uint32_t last = now;
    gesture_tick(&g, last + 9999, &o);
    CHECK(o.n == 0);
    CHECK(gesture_dpad_active(&g));
    gesture_tick(&g, last + 10000, &o);
    CHECK(o.n == 0);
    CHECK(!gesture_dpad_active(&g));
}

static void test_dpad_idle_blocked(void)
{
    gesture_t g;
    g_init(&g);
    gesture_out_t o;
    uint32_t now = 0;
    dpad_enter(&g, &o, &now);
    // A contact in progress blocks the idle timeout.
    gesture_feed(&g, true, 15000, 15000, now, &o);
    CHECK(o.n == 0);
    gesture_tick(&g, now + 20000, &o);
    CHECK(o.n == 0);
    CHECK(gesture_dpad_active(&g));
    now += 20000;
    gesture_feed(&g, false, 15000, 15000, now, &o);
    // Tap release gives Select and refreshes the timer.
    CHECK(o.n == 2 && o.ev[0].key == GESTURE_KEY_SELECT);
    gesture_tick(&g, now + 9999, &o);
    CHECK(gesture_dpad_active(&g));
    gesture_tick(&g, now + 10000, &o);
    CHECK(!gesture_dpad_active(&g));
}

static void test_dpad_wrap(void)
{
    gesture_t g;
    g_init(&g);
    gesture_out_t o;
    uint32_t now = 0xFFFFFF00u;
    dpad_enter(&g, &o, &now);
    uint32_t last = now;
    gesture_tick(&g, last + 9999, &o);
    CHECK(gesture_dpad_active(&g));
    gesture_tick(&g, last + 10001, &o);
    CHECK(!gesture_dpad_active(&g));
}

static void test_dpad_disabled(void)
{
    gesture_t g;
    g_init(&g);
    gesture_out_t o;
    uint32_t now = 0;
    dpad_enter(&g, &o, &now);
    CHECK(gesture_dpad_active(&g));
    // Disabling exits the mode immediately, and the next press passes through.
    gesture_set_enabled(&g, false);
    CHECK(!gesture_dpad_active(&g));
    gesture_feed(&g, true, 16383, 16383, now, &o);
    CHECK(o.n == 1 && !o.ev[0].is_key && o.ev[0].down);
    gesture_feed(&g, false, 16383, 16383, now + 50, &o);
    CHECK(o.n == 1 && !o.ev[0].down);
    // No stuck touch: balance holds.
    gesture_set_enabled(&g, true);
    gesture_feed(&g, true, 16383, 16383, now + 100, &o);
    CHECK(o.n == 1 && !o.ev[0].is_key);
    gesture_feed(&g, false, 16383, 16383, now + 150, &o);
    CHECK(o.n == 1 && !o.ev[0].down);
    CHECK(!gesture_dpad_active(&g));
}

static void test_dpad_random(void)
{
    // Random contacts in D-pad mode: touch down/up stay balanced and
    // D-pad contacts emit no touch at all.
    srand(999);
    gesture_t g;
    g_init(&g);
    gesture_out_t o;
    uint32_t now = 0;
    dpad_enter(&g, &o, &now);
    bool active = false;
    int presses = 0, releases = 0;
    for (int c = 0; c < 300; c++) {
        uint16_t sx = (uint16_t)(rand() % 32768);
        uint16_t sy = (uint16_t)(rand() % 32768);
        now += (uint32_t)(rand() % 400);
        gesture_feed(&g, true, sx, sy, now, &o);
        for (uint8_t i = 0; i < o.n; i++) {
            if (o.ev[i].is_key) {
                continue;
            }
            if (o.ev[i].down && !active) {
                active = true;
                presses++;
            } else if (!o.ev[i].down && active) {
                active = false;
                releases++;
            }
            CHECK(presses == releases || presses == releases + 1);
        }
        int moves = rand() % 5;
        for (int m = 0; m < moves; m++) {
            uint16_t mx = (uint16_t)(rand() % 32768);
            uint16_t my = (uint16_t)(rand() % 32768);
            now += (uint32_t)(rand() % 120);
            gesture_feed(&g, true, mx, my, now, &o);
            for (uint8_t i = 0; i < o.n; i++) {
                if (o.ev[i].is_key) {
                    continue;
                }
                if (o.ev[i].down && !active) {
                    active = true;
                    presses++;
                } else if (!o.ev[i].down && active) {
                    active = false;
                    releases++;
                }
                CHECK(presses == releases || presses == releases + 1);
            }
            CHECK(gesture_is_passthrough(&g) == false || o.n <= 2);
            if (rand() % 3 == 0) {
                now += (uint32_t)(rand() % 400);
                gesture_tick(&g, now, &o);
                CHECK(o.n == 0); // tick never emits touch in D-pad mode
                for (uint8_t i = 0; i < o.n; i++) {
                    CHECK(o.ev[i].is_key);
                }
            }
        }
        uint16_t ex = (uint16_t)(rand() % 32768);
        uint16_t ey = (uint16_t)(rand() % 32768);
        now += (uint32_t)(rand() % 400);
        gesture_feed(&g, false, ex, ey, now, &o);
        for (uint8_t i = 0; i < o.n; i++) {
            if (o.ev[i].is_key) {
                continue;
            }
            if (o.ev[i].down && !active) {
                active = true;
                presses++;
            } else if (!o.ev[i].down && active) {
                active = false;
                releases++;
            }
            CHECK(presses == releases || presses == releases + 1);
        }
        CHECK(!active);
        // Keep the mode alive: any contact refreshes the timer, but a long
        // random gap could idle out, so re-enter if needed.
        if (!gesture_dpad_active(&g)) {
            dpad_enter(&g, &o, &now);
            CHECK(gesture_dpad_active(&g));
        }
    }
    CHECK(presses == releases);
    CHECK(presses == 0); // D-pad contacts emit no touch at all
}

static void test_gesture_random(void)
{
    srand(12345);
    gesture_t g;
    g_init(&g);
    bool active = false;
    int presses = 0, releases = 0;
    gesture_out_t o;
    uint32_t now = 0;
    for (int c = 0; c < 300; c++) {
        uint16_t sx = (uint16_t)(rand() % 32768);
        uint16_t sy = (uint16_t)(rand() % 32768);
        now += (uint32_t)(rand() % 400);
        gesture_feed(&g, true, sx, sy, now, &o);
        for (uint8_t i = 0; i < o.n; i++) {
            if (o.ev[i].is_key) {
                continue;
            }
            if (o.ev[i].down && !active) {
                active = true;
                presses++;
            } else if (!o.ev[i].down && active) {
                active = false;
                releases++;
            }
            CHECK(presses == releases || presses == releases + 1);
        }
        int moves = rand() % 5;
        for (int m = 0; m < moves; m++) {
            uint16_t mx = (uint16_t)(rand() % 32768);
            uint16_t my = (uint16_t)(rand() % 32768);
            now += (uint32_t)(rand() % 120);
            gesture_feed(&g, true, mx, my, now, &o);
            for (uint8_t i = 0; i < o.n; i++) {
                if (o.ev[i].is_key) {
                    continue;
                }
                if (o.ev[i].down && !active) {
                    active = true;
                    presses++;
                } else if (!o.ev[i].down && active) {
                    active = false;
                    releases++;
                }
                CHECK(presses == releases || presses == releases + 1);
            }
            if (rand() % 3 == 0) {
                now += (uint32_t)(rand() % 400);
                gesture_tick(&g, now, &o);
                for (uint8_t i = 0; i < o.n; i++) {
                    if (o.ev[i].is_key) {
                        continue;
                    }
                    if (o.ev[i].down && !active) {
                        active = true;
                        presses++;
                    } else if (!o.ev[i].down && active) {
                        active = false;
                        releases++;
                    }
                    CHECK(presses == releases || presses == releases + 1);
                }
            }
        }
        uint16_t ex = (uint16_t)(rand() % 32768);
        uint16_t ey = (uint16_t)(rand() % 32768);
        now += (uint32_t)(rand() % 400);
        gesture_feed(&g, false, ex, ey, now, &o);
        for (uint8_t i = 0; i < o.n; i++) {
            if (o.ev[i].is_key) {
                continue;
            }
            if (o.ev[i].down && !active) {
                active = true;
                presses++;
            } else if (!o.ev[i].down && active) {
                active = false;
                releases++;
            }
            CHECK(presses == releases || presses == releases + 1);
        }
        CHECK(!active);
    }
    CHECK(presses == releases && presses > 0);
}

int main(void)
{
    test_mt();
    test_two_fingers();
    test_hybrid();
    test_real_panel();
    test_real_panel_mouse();
    test_mice();
    test_robustness();
    test_map();
    test_report_map_keys();
    test_gesture_disabled();
    test_gesture_centre();
    test_gesture_left_back();
    test_gesture_bottom_home();
    test_gesture_tap();
    test_gesture_tick_expiry();
    test_gesture_feed_expiry();
    test_gesture_off_axis();
    test_gesture_corner();
    test_gesture_second();
    test_gesture_idle_release();
    test_gesture_wrap();
    test_gesture_toggle_mid();
    test_dpad_enter_toggle();
    test_dpad_tap_select();
    test_dpad_arrows();
    test_dpad_long_drag();
    test_dpad_dominant_axis();
    test_dpad_slow_drag();
    test_dpad_wobble_select();
    test_dpad_dead_zone();
    test_dpad_edge_priority();
    test_dpad_strip_hold();
    test_dpad_exit_wide_zone();
    test_dpad_exit_slow();
    test_normal_wide_zone_passthrough();
    test_normal_top_hold_replay();
    test_dpad_up_from_top();
    test_dpad_hold_expires_to_dpad();
    test_dpad_top_tap_select();
    test_dpad_below_zone_down();
    test_dpad_idle();
    test_dpad_idle_blocked();
    test_dpad_wrap();
    test_dpad_disabled();
    test_dpad_random();
    test_gesture_random();
    printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures != 0;
}
