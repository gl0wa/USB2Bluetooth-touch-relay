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
        .dpad_step = 2500,
        .dpad_tap = 1200,
        .dpad_idle_ms = 10000,
    };
}

void gesture_init(gesture_t *g, gesture_cfg_t cfg)
{
    g->cfg = cfg;
    g->st = GESTURE_IDLE;
    g->effective = cfg.enabled;
    g->left_cand = g->bottom_cand = g->top_cand = false;
    g->sx = g->sy = 0;
    g->t0 = 0;
    g->dpad = false;
    g->dpad_last = 0;
    g->ax = g->ay = 0;
    g->dpad_moved = g->dpad_far = false;
}

void gesture_set_enabled(gesture_t *g, bool enabled)
{
    // Applied at the next press so the current contact cannot stick.
    g->cfg.enabled = enabled;
    if (!enabled) {
        // Exit D-pad mode without a stuck touch. A D-pad contact in
        // progress is swallowed (no touch was ever emitted, no key after
        // this point); a held edge press falls back to the non-D-pad path
        // because g->dpad is now false.
        g->dpad = false;
        if (g->st == GESTURE_DPAD) {
            g->st = GESTURE_FIRED;
            g->left_cand = g->bottom_cand = g->top_cand = false;
        }
    }
}

bool gesture_is_passthrough(const gesture_t *g)
{
    return g->st == GESTURE_PASS || g->st == GESTURE_IDLE;
}

bool gesture_dpad_active(const gesture_t *g)
{
    return g->dpad;
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
    g->left_cand = g->bottom_cand = g->top_cand = false;
}

// Start a D-pad contact anchored at the press start point.
static void enter_dpad_contact(gesture_t *g, gesture_out_t *out)
{
    out->n = 0;
    g->st = GESTURE_DPAD;
    g->ax = g->sx;
    g->ay = g->sy;
    g->dpad_moved = false;
    // dpad_far keeps its HELD value: a hold given up on off-axis travel has
    // already moved beyond a tap.
    g->left_cand = g->bottom_cand = g->top_cand = false;
}

void gesture_feed(gesture_t *g, bool down, uint16_t x, uint16_t y, uint32_t now_ms, gesture_out_t *out)
{
    out->n = 0;
    if (!down) {
        switch (g->st) {
        case GESTURE_IDLE:
            if (g->dpad) {
                g->dpad_last = now_ms; // any contact refreshes the idle timer
                break; // swallow: no touch was ever emitted for D-pad
            }
            emit_touch(out, false, x, y); // never lose a release
            break;
        case GESTURE_HELD:
            if (g->dpad) {
                g->dpad_last = now_ms;
                // Quick tap in a strip during D-pad mode is Select.
                // Movement between dpad_tap and dpad_step gives nothing.
                if (!g->dpad_far && abs_diff_u16(x, g->sx) <= g->cfg.dpad_tap &&
                    abs_diff_u16(y, g->sy) <= g->cfg.dpad_tap) {
                    emit_key(out, GESTURE_KEY_SELECT);
                }
                g->st = GESTURE_IDLE;
                g->left_cand = g->bottom_cand = g->top_cand = false;
                break;
            }
            // Quick edge tap: down then up at the start point.
            out->n = 2;
            out->ev[0] = (gesture_ev_t) {.is_key = false, .down = true, .x = g->sx, .y = g->sy};
            out->ev[1] = (gesture_ev_t) {.is_key = false, .down = false, .x = g->sx, .y = g->sy};
            g->st = GESTURE_IDLE;
            g->left_cand = g->bottom_cand = g->top_cand = false;
            break;
        case GESTURE_PASS:
            emit_touch(out, false, x, y);
            g->st = GESTURE_IDLE;
            g->left_cand = g->bottom_cand = g->top_cand = false;
            break;
        case GESTURE_FIRED:
            if (g->dpad) {
                g->dpad_last = now_ms;
            }
            g->st = GESTURE_IDLE; // swallow the release
            g->left_cand = g->bottom_cand = g->top_cand = false;
            break;
        case GESTURE_DPAD: {
            g->dpad_last = now_ms;
            bool tap_ok = !g->dpad_moved && !g->dpad_far &&
                abs_diff_u16(x, g->sx) <= g->cfg.dpad_tap &&
                abs_diff_u16(y, g->sy) <= g->cfg.dpad_tap;
            g->st = GESTURE_IDLE;
            if (tap_ok) {
                emit_key(out, GESTURE_KEY_SELECT);
                g->dpad_last = now_ms;
            }
            break;
        }
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
        bool top = y < g->cfg.edge;
        if (!left && !bottom && !top) {
            if (g->dpad) {
                // Centre press in D-pad mode becomes a D-pad contact.
                g->st = GESTURE_DPAD;
                g->sx = x;
                g->sy = y;
                g->ax = x;
                g->ay = y;
                g->t0 = now_ms;
                g->dpad_moved = false;
                g->dpad_far = false;
                g->dpad_last = now_ms;
                return; // emit nothing yet
            }
            g->st = GESTURE_PASS;
            emit_touch(out, true, x, y);
            return;
        }
        g->st = GESTURE_HELD;
        g->left_cand = left;
        g->bottom_cand = bottom;
        g->top_cand = top;
        g->sx = x;
        g->sy = y;
        g->t0 = now_ms;
        g->dpad_far = false;
        if (g->dpad) {
            g->dpad_last = now_ms;
        }
        return; // held: emit nothing yet
    }

    if (g->st == GESTURE_PASS) {
        emit_touch(out, true, x, y);
        return;
    }

    if (g->st == GESTURE_FIRED) {
        if (g->dpad) {
            g->dpad_last = now_ms;
        }
        return; // swallow the rest of the contact
    }

    if (g->st == GESTURE_DPAD) {
        g->dpad_last = now_ms;
        int32_t dx = (int32_t)x - (int32_t)g->ax;
        int32_t dy = (int32_t)y - (int32_t)g->ay;
        int32_t adx = dx >= 0 ? dx : -dx;
        int32_t ady = dy >= 0 ? dy : -dy;
        // Track excursion beyond a tap from the contact start.
        if (abs_diff_u16(x, g->sx) > g->cfg.dpad_tap || abs_diff_u16(y, g->sy) > g->cfg.dpad_tap) {
            g->dpad_far = true;
        }
        uint8_t key = 0;
        if (adx >= ady) {
            if (adx >= (int32_t)g->cfg.dpad_step) {
                key = dx >= 0 ? GESTURE_KEY_RIGHT : GESTURE_KEY_LEFT;
            }
        } else {
            if (ady >= (int32_t)g->cfg.dpad_step) {
                // Up means y decreasing.
                key = dy <= 0 ? GESTURE_KEY_UP : GESTURE_KEY_DOWN;
            }
        }
        if (key) {
            emit_key(out, key);
            g->ax = x;
            g->ay = y;
            g->dpad_moved = true;
            g->dpad_last = now_ms;
        }
        return;
    }

    // GESTURE_HELD with the finger still down.
    if (g->dpad) {
        g->dpad_last = now_ms;
        if (abs_diff_u16(x, g->sx) > g->cfg.dpad_tap || abs_diff_u16(y, g->sy) > g->cfg.dpad_tap) {
            g->dpad_far = true;
        }
    }
    int32_t dx = (int32_t)x - (int32_t)g->sx;
    int32_t dy_up = (int32_t)g->sy - (int32_t)y;
    int32_t dy_down = (int32_t)y - (int32_t)g->sy;
    uint16_t off_y = abs_diff_u16(y, g->sy);
    uint16_t off_x = abs_diff_u16(x, g->sx);

    bool left_fire = g->left_cand && dx >= (int32_t)g->cfg.trigger && off_y <= g->cfg.max_off_axis;
    bool bottom_fire = g->bottom_cand && dy_up >= (int32_t)g->cfg.trigger && off_x <= g->cfg.max_off_axis;
    bool top_fire = g->top_cand && dy_down >= (int32_t)g->cfg.trigger && off_x <= g->cfg.max_off_axis;
    // Top toggles only while gestures are on, so disabling mid-hold cannot
    // re-enter D-pad mode. Left/bottom keep the historical behaviour of
    // firing for the current contact.
    if (g->top_cand && !g->cfg.enabled) {
        top_fire = false;
    }
    if (left_fire || bottom_fire || top_fire) {
        // At most one fires with the default config; priority is
        // left, bottom, top for a corner start.
        if (left_fire) {
            emit_key(out, GESTURE_KEY_BACK);
        } else if (bottom_fire) {
            emit_key(out, GESTURE_KEY_HOME);
        } else {
            g->dpad = !g->dpad;
            if (g->dpad) {
                g->dpad_last = now_ms;
            }
        }
        g->st = GESTURE_FIRED;
        g->left_cand = g->bottom_cand = g->top_cand = false;
        return;
    }

    if (g->left_cand && off_y > g->cfg.max_off_axis) {
        g->left_cand = false;
    }
    if (g->bottom_cand && off_x > g->cfg.max_off_axis) {
        g->bottom_cand = false;
    }
    if (g->top_cand && off_x > g->cfg.max_off_axis) {
        g->top_cand = false;
    }
    if (!g->left_cand && !g->bottom_cand && !g->top_cand) {
        if (g->dpad) {
            enter_dpad_contact(g, out);
            return;
        }
        replay(g, x, y, true, out);
        return;
    }
    if (expired(now_ms, g->t0, g->cfg.hold_ms)) {
        if (g->dpad) {
            enter_dpad_contact(g, out);
            return;
        }
        replay(g, x, y, true, out);
        return;
    }
}

void gesture_tick(gesture_t *g, uint32_t now_ms, gesture_out_t *out)
{
    out->n = 0;
    if (g->st == GESTURE_HELD && g->effective && expired(now_ms, g->t0, g->cfg.hold_ms)) {
        if (g->dpad) {
            enter_dpad_contact(g, out);
            return;
        }
        replay(g, 0, 0, false, out);
        return;
    }
    // Idle exit: only with no contact in progress. Any contact or key
    // refreshes dpad_last in gesture_feed.
    if (g->dpad && g->st == GESTURE_IDLE && expired(now_ms, g->dpad_last, g->cfg.dpad_idle_ms)) {
        g->dpad = false;
    }
}
