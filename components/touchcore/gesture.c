#include "gesture.h"

#define COORD_MAX 32767

gesture_cfg_t gesture_default_cfg(void)
{
    return (gesture_cfg_t) {
        .enabled = true,
        .edge = 983, // 3%
        .trigger = 3277, // 10%
        .max_off_axis = 2500,
        .hold_ms = 250,
    };
}

void gesture_init(gesture_t *g, gesture_cfg_t cfg)
{
    g->cfg = cfg;
    g->st = GESTURE_IDLE;
    g->effective = cfg.enabled;
    g->left_cand = g->bottom_cand = false;
    g->sx = g->sy = 0;
    g->t0 = 0;
}

void gesture_set_enabled(gesture_t *g, bool enabled)
{
    // Applied at the next press so the current contact cannot stick.
    g->cfg.enabled = enabled;
}

bool gesture_is_passthrough(const gesture_t *g)
{
    return g->st == GESTURE_PASS || g->st == GESTURE_IDLE;
}

static uint16_t abs_diff_u16(uint16_t a, uint16_t b)
{
    return a >= b ? (uint16_t)(a - b) : (uint16_t)(b - a);
}

static bool expired(uint32_t now, uint32_t start, uint32_t hold_ms)
{
    return (uint32_t)(now - start) >= hold_ms;
}

static void emit_touch(gesture_out_t *out, bool down, uint16_t x, uint16_t y)
{
    out->n = 1;
    out->ev[0] = (gesture_ev_t) {.is_key = false, .down = down, .x = x, .y = y};
}

static void emit_key(gesture_out_t *out, uint8_t key)
{
    out->n = 2;
    out->ev[0] = (gesture_ev_t) {.is_key = true, .key = key, .down = true};
    out->ev[1] = (gesture_ev_t) {.is_key = true, .key = key, .down = false};
}

// Replay a held press as a touch: down at the start, then the current point
// when it differs. The contact then passes through.
static void replay(gesture_t *g, uint16_t cx, uint16_t cy, bool has_current, gesture_out_t *out)
{
    out->n = 0;
    out->ev[out->n++] = (gesture_ev_t) {.is_key = false, .down = true, .x = g->sx, .y = g->sy};
    if (has_current && (cx != g->sx || cy != g->sy)) {
        out->ev[out->n++] = (gesture_ev_t) {.is_key = false, .down = true, .x = cx, .y = cy};
    }
    g->st = GESTURE_PASS;
    g->left_cand = g->bottom_cand = false;
}

void gesture_feed(gesture_t *g, bool down, uint16_t x, uint16_t y, uint32_t now_ms, gesture_out_t *out)
{
    out->n = 0;
    if (!down) {
        switch (g->st) {
        case GESTURE_IDLE:
            emit_touch(out, false, x, y); // never lose a release
            break;
        case GESTURE_HELD:
            // Quick edge tap: down then up at the start point.
            out->n = 2;
            out->ev[0] = (gesture_ev_t) {.is_key = false, .down = true, .x = g->sx, .y = g->sy};
            out->ev[1] = (gesture_ev_t) {.is_key = false, .down = false, .x = g->sx, .y = g->sy};
            g->st = GESTURE_IDLE;
            g->left_cand = g->bottom_cand = false;
            break;
        case GESTURE_PASS:
            emit_touch(out, false, x, y);
            g->st = GESTURE_IDLE;
            g->left_cand = g->bottom_cand = false;
            break;
        case GESTURE_FIRED:
            g->st = GESTURE_IDLE; // swallow the release
            g->left_cand = g->bottom_cand = false;
            break;
        }
        return;
    }

    // down == true: press or move.
    if (g->st == GESTURE_IDLE) {
        g->effective = g->cfg.enabled;
        if (!g->effective) {
            g->st = GESTURE_PASS;
            emit_touch(out, true, x, y);
            return;
        }
        bool left = x < g->cfg.edge;
        bool bottom = y > (uint16_t)(COORD_MAX - g->cfg.edge);
        if (!left && !bottom) {
            g->st = GESTURE_PASS;
            emit_touch(out, true, x, y);
            return;
        }
        g->st = GESTURE_HELD;
        g->left_cand = left;
        g->bottom_cand = bottom;
        g->sx = x;
        g->sy = y;
        g->t0 = now_ms;
        return; // held: emit nothing yet
    }

    if (g->st == GESTURE_PASS) {
        emit_touch(out, true, x, y);
        return;
    }

    if (g->st == GESTURE_FIRED) {
        return; // swallow the rest of the contact
    }

    // GESTURE_HELD with the finger still down.
    int32_t dx = (int32_t)x - (int32_t)g->sx;
    int32_t dy_up = (int32_t)g->sy - (int32_t)y;
    uint16_t off_y = abs_diff_u16(y, g->sy);
    uint16_t off_x = abs_diff_u16(x, g->sx);

    bool left_fire = g->left_cand && dx >= (int32_t)g->cfg.trigger && off_y <= g->cfg.max_off_axis;
    bool bottom_fire = g->bottom_cand && dy_up >= (int32_t)g->cfg.trigger && off_x <= g->cfg.max_off_axis;
    if (left_fire || bottom_fire) {
        // Both cannot fire at once with the default config (dx cannot be
        // both >= trigger and <= max_off_axis), but either order still fires
        // exactly one key for a corner start.
        emit_key(out, left_fire ? GESTURE_KEY_BACK : GESTURE_KEY_HOME);
        g->st = GESTURE_FIRED;
        g->left_cand = g->bottom_cand = false;
        return;
    }

    if (g->left_cand && off_y > g->cfg.max_off_axis) {
        g->left_cand = false;
    }
    if (g->bottom_cand && off_x > g->cfg.max_off_axis) {
        g->bottom_cand = false;
    }
    if (!g->left_cand && !g->bottom_cand) {
        replay(g, x, y, true, out);
        return;
    }
    if (expired(now_ms, g->t0, g->cfg.hold_ms)) {
        replay(g, x, y, true, out);
        return;
    }
}

void gesture_tick(gesture_t *g, uint32_t now_ms, gesture_out_t *out)
{
    out->n = 0;
    if (g->st == GESTURE_HELD && g->effective && expired(now_ms, g->t0, g->cfg.hold_ms)) {
        replay(g, 0, 0, false, out);
    }
}
