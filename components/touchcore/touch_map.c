#include "touch_map.h"
#include "hid_touch.h"

void touch_map_default(touch_map_t *m)
{
    *m = (touch_map_t) {.x_max = HT_COORD_MAX, .y_max = HT_COORD_MAX};
}

static uint16_t scale(uint16_t v, uint16_t lo, uint16_t hi)
{
    if (hi <= lo) {
        return v;
    }
    if (v <= lo) {
        return 0;
    }
    if (v >= hi) {
        return HT_COORD_MAX;
    }
    return (uint16_t)((uint32_t)(v - lo) * HT_COORD_MAX / (hi - lo));
}

void touch_map_apply(const touch_map_t *m, uint16_t x, uint16_t y, uint16_t *ox, uint16_t *oy)
{
    uint16_t rx = x, ry = y;
    switch (m->rotation & 3) {
    case 1:
        rx = HT_COORD_MAX - y;
        ry = x;
        break;
    case 2:
        rx = HT_COORD_MAX - x;
        ry = HT_COORD_MAX - y;
        break;
    case 3:
        rx = y;
        ry = HT_COORD_MAX - x;
        break;
    default:
        break;
    }
    if (m->mirror_x) {
        rx = HT_COORD_MAX - rx;
    }
    if (m->mirror_y) {
        ry = HT_COORD_MAX - ry;
    }
    *ox = scale(rx, m->x_min, m->x_max);
    *oy = scale(ry, m->y_min, m->y_max);
}
