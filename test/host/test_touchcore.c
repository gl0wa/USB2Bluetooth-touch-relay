#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hid_touch.h"
#include "touch_map.h"
#include "panel_descriptors.h"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

// The exact descriptor verified on the Shield through uhid; also the BLE report map.
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

int main(void)
{
    test_mt();
    test_two_fingers();
    test_hybrid();
    test_real_panel();
    test_mice();
    test_robustness();
    test_map();
    printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures != 0;
}
