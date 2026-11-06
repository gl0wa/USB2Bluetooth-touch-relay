// Maps normalised panel coordinates onto normalised display coordinates.
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t rotation;           // clockwise quarter turns, 0..3
    bool mirror_x;              // applied after rotation
    bool mirror_y;
    uint16_t x_min, x_max;      // calibrated panel edges, in rotated coordinates
    uint16_t y_min, y_max;
} touch_map_t;

void touch_map_default(touch_map_t *m);
void touch_map_apply(const touch_map_t *m, uint16_t x, uint16_t y, uint16_t *ox, uint16_t *oy);
