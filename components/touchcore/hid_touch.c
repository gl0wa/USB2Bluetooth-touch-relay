#include "hid_touch.h"
#include <stdlib.h>
#include <string.h>

#define PAGE_DESKTOP 0x01u
#define PAGE_BUTTON 0x09u
#define PAGE_DIGITIZER 0x0Du
#define U(page, id) (((uint32_t)(page) << 16) | (id))

#define MAX_CANDIDATES 8
#define MAX_USAGES 16
#define MAX_REPORT_IDS 16
#define MAX_GLOBAL_STACK 4

typedef struct {
    uint32_t page;
    int32_t lmin, lmax;
    uint32_t size, count;
    uint8_t report_id;
} globals_t;

typedef struct {
    ht_layout_t l;
    uint8_t app;
    int8_t slot;      // current contact index, -1 before the first one
    uint8_t open_gen; // finger collection generation seen by this candidate
    bool relative;
} cand_t;

typedef struct {
    uint8_t id;
    uint32_t bits;
} offset_t;

static ht_kind_t kind_for_app(uint32_t usage)
{
    switch (usage) {
    case U(PAGE_DIGITIZER, 0x04):
        return HT_KIND_TOUCHSCREEN;
    case U(PAGE_DIGITIZER, 0x01):
    case U(PAGE_DIGITIZER, 0x02):
    case U(PAGE_DIGITIZER, 0x05):
        return HT_KIND_DIGITIZER;
    case U(PAGE_DESKTOP, 0x01):
    case U(PAGE_DESKTOP, 0x02):
        return HT_KIND_ABS_MOUSE;
    default:
        return HT_KIND_NONE;
    }
}

static uint32_t *offset_for(offset_t *offs, size_t *n, uint8_t id)
{
    for (size_t i = 0; i < *n; i++) {
        if (offs[i].id == id) {
            return &offs[i].bits;
        }
    }
    if (*n == MAX_REPORT_IDS) {
        return NULL;
    }
    offs[*n].id = id;
    offs[*n].bits = 0;
    return &offs[(*n)++].bits;
}

static cand_t *cand_for(cand_t *c, size_t *n, uint8_t app, ht_kind_t kind, uint8_t id)
{
    for (size_t i = 0; i < *n; i++) {
        if (c[i].app == app && c[i].l.report_id == id) {
            return &c[i];
        }
    }
    if (*n == MAX_CANDIDATES) {
        return NULL;
    }
    cand_t *e = &c[(*n)++];
    memset(e, 0, sizeof(*e));
    e->app = app;
    e->slot = -1;
    e->l.kind = kind;
    e->l.report_id = id;
    return e;
}

static void set_field(ht_field_t *f, const globals_t *g, uint32_t off)
{
    f->present = true;
    f->bit_off = (uint16_t)off;
    f->bits = (uint8_t)g->size;
    f->lmin = g->lmin;
    f->lmax = g->lmax;
    f->is_signed = g->lmin < 0;
}

static void map_field(cand_t *c, uint8_t finger_gen, uint32_t usage, const globals_t *g, uint32_t off)
{
    if (c->slot < 0 || c->open_gen != finger_gen) {
        if (c->slot + 1 >= HT_MAX_CONTACTS) {
            return;
        }
        c->slot++;
        c->open_gen = finger_gen;
    }
    ht_contact_t *k = &c->l.c[c->slot];
    switch (usage) {
    case U(PAGE_DESKTOP, 0x30):
        set_field(&k->x, g, off);
        break;
    case U(PAGE_DESKTOP, 0x31):
        set_field(&k->y, g, off);
        break;
    case U(PAGE_DIGITIZER, 0x42):
    case U(PAGE_BUTTON, 0x01):
        if (!k->tip.present) {
            set_field(&k->tip, g, off);
        }
        break;
    case U(PAGE_DIGITIZER, 0x51):
        set_field(&k->id, g, off);
        break;
    default:
        break;
    }
}

static int32_t item_value(const uint8_t *p, size_t n, bool sign)
{
    uint32_t v = 0;
    for (size_t i = 0; i < n; i++) {
        v |= (uint32_t)p[i] << (8 * i);
    }
    if (sign && n > 0 && n < 4 && (p[n - 1] & 0x80)) {
        v |= ~0u << (8 * n);
    }
    return (int32_t)v;
}

bool ht_parse_descriptor(const uint8_t *d, size_t len, ht_layout_t *out)
{
    globals_t g = {0}, stack[MAX_GLOBAL_STACK];
    size_t sp = 0;
    uint32_t usages[MAX_USAGES];
    size_t n_usages = 0;
    uint32_t umin = 0, umax = 0;
    bool have_range = false;

    // Heap, not stack: this workspace is several KB and USB tasks have small stacks.
    cand_t *cands = malloc(sizeof(cand_t) * MAX_CANDIDATES);
    size_t n_cands = 0;
    offset_t offs[MAX_REPORT_IDS];
    size_t n_offs = 0;

    size_t depth = 0;
    uint8_t finger_gen = 0, app = 0;
    uint32_t app_usage = 0;

    memset(out, 0, sizeof(*out));
    if (!cands) {
        return false;
    }

    for (size_t i = 0; i < len;) {
        uint8_t b = d[i++];
        if (b == 0xFE) { // long item: skip
            if (i + 2 > len) {
                break;
            }
            i += 2u + d[i];
            continue;
        }
        size_t sz = b & 3u;
        sz = sz == 3 ? 4 : sz;
        if (i + sz > len) {
            break;
        }
        const uint8_t *p = &d[i];
        i += sz;
        uint8_t tag = b & 0xFCu;
        uint32_t uval = (uint32_t)item_value(p, sz, false);

        switch (tag) {
        case 0x04:
            g.page = uval;
            break;
        case 0x14:
            g.lmin = item_value(p, sz, true);
            break;
        case 0x24:
            // Unsigned when lmin is not negative, so 0xFFFF in 2 bytes is 65535.
            g.lmax = item_value(p, sz, g.lmin < 0);
            break;
        case 0x74:
            g.size = uval;
            break;
        case 0x84:
            g.report_id = (uint8_t)uval;
            break;
        case 0x94:
            g.count = uval;
            break;
        case 0xA4:
            if (sp < MAX_GLOBAL_STACK) {
                stack[sp++] = g;
            }
            break;
        case 0xB4:
            if (sp > 0) {
                g = stack[--sp];
            }
            break;
        case 0x08:
            if (n_usages < MAX_USAGES) {
                usages[n_usages++] = sz == 4 ? uval : U(g.page, uval);
            }
            break;
        case 0x18:
            umin = sz == 4 ? uval : U(g.page, uval);
            have_range = true;
            break;
        case 0x28:
            umax = sz == 4 ? uval : U(g.page, uval);
            break;
        case 0xA0: { // Collection
            uint32_t usage = n_usages ? usages[0] : 0;
            if (depth == 0) {
                app_usage = usage;
                app++;
            }
            depth++;
            if (usage == U(PAGE_DIGITIZER, 0x22) || usage == U(PAGE_DIGITIZER, 0x20)) {
                finger_gen++;
            }
            n_usages = 0;
            have_range = false;
            break;
        }
        case 0xC0:
            if (depth > 0) {
                depth--;
            }
            break;
        case 0x80: { // Input
            uint32_t *off = offset_for(offs, &n_offs, g.report_id);
            ht_kind_t kind = kind_for_app(app_usage);
            bool constant = uval & 0x01u, variable = uval & 0x02u, relative = uval & 0x04u;
            if (off && !constant && variable && kind != HT_KIND_NONE) {
                cand_t *c = cand_for(cands, &n_cands, app, kind, g.report_id);
                for (uint32_t k = 0; c && k < g.count; k++) {
                    uint32_t usage = 0;
                    if (have_range) {
                        usage = umin + k <= umax ? umin + k : umax;
                    } else if (n_usages) {
                        usage = usages[k < n_usages ? k : n_usages - 1];
                    }
                    uint32_t at = *off + k * g.size;
                    if (usage == U(PAGE_DIGITIZER, 0x54)) {
                        set_field(&c->l.count, &g, at);
                    } else if (usage == U(PAGE_DESKTOP, 0x30) || usage == U(PAGE_DESKTOP, 0x31) ||
                               usage == U(PAGE_DIGITIZER, 0x42) || usage == U(PAGE_DIGITIZER, 0x51) ||
                               usage == U(PAGE_BUTTON, 0x01)) {
                        if (relative && (usage >> 16) == PAGE_DESKTOP) {
                            c->relative = true;
                        }
                        map_field(c, finger_gen, usage, &g, at);
                    }
                }
            }
            if (off) {
                *off += g.size * g.count;
            }
            n_usages = 0;
            have_range = false;
            break;
        }
        case 0x90:
        case 0xB0:
            n_usages = 0;
            have_range = false;
            break;
        default:
            break;
        }
    }

    const cand_t *best = NULL;
    uint8_t best_contacts = 0;
    for (size_t i = 0; i < n_cands; i++) {
        cand_t *c = &cands[i];
        uint8_t usable = 0;
        for (int s = 0; s <= c->slot; s++) {
            const ht_contact_t *k = &c->l.c[s];
            if (k->x.present && k->y.present && k->tip.present) {
                usable++;
            }
        }
        if (usable == 0 || c->relative) {
            continue;
        }
        c->l.contacts = usable;
        if (!best || c->l.kind > best->l.kind || (c->l.kind == best->l.kind && usable > best_contacts)) {
            best = c;
            best_contacts = usable;
        }
    }
    if (!best) {
        free(cands);
        return false;
    }
    *out = best->l;
    // Compact so that c[0..contacts) are all usable.
    uint8_t w = 0;
    for (int s = 0; s <= best->slot; s++) {
        const ht_contact_t *k = &best->l.c[s];
        if (k->x.present && k->y.present && k->tip.present) {
            out->c[w++] = *k;
        }
    }
    free(cands);
    return true;
}

static bool read_field(const ht_field_t *f, const uint8_t *r, size_t len, int32_t *out)
{
    if (!f->present || f->bits == 0 || f->bits > 32 || (size_t)f->bit_off + f->bits > len * 8) {
        return false;
    }
    uint32_t v = 0;
    for (uint8_t i = 0; i < f->bits; i++) {
        uint32_t bit = (uint32_t)f->bit_off + i;
        v |= (uint32_t)((r[bit >> 3] >> (bit & 7)) & 1u) << i;
    }
    if (f->is_signed && f->bits < 32 && (v & (1u << (f->bits - 1)))) {
        v |= ~0u << f->bits;
    }
    *out = (int32_t)v;
    return true;
}

static uint16_t normalise(const ht_field_t *f, int32_t v)
{
    int64_t lo = f->lmin, hi = f->lmax;
    if (hi <= lo) { // descriptor gave no usable range: use the field width
        lo = 0;
        hi = f->bits >= 32 ? INT32_MAX : (1ll << f->bits) - 1;
    }
    if (v < lo) {
        v = (int32_t)lo;
    }
    if (v > hi) {
        v = (int32_t)hi;
    }
    return (uint16_t)(((int64_t)v - lo) * HT_COORD_MAX / (hi - lo));
}

bool ht_decode(const ht_layout_t *l, const uint8_t *report, size_t len, ht_state_t *st, ht_touch_t *out)
{
    if (l->kind == HT_KIND_NONE) {
        return false;
    }
    if (l->report_id) {
        if (len < 1 || report[0] != l->report_id) {
            return false;
        }
        report++;
        len--;
    }

    // Hybrid mode splits one frame over several reports: the first carries the
    // contact count, and continuation reports carry zero.
    int32_t count = 0;
    bool has_count = read_field(&l->count, report, len, &count);
    int slots = l->contacts;
    bool frame_done = true;
    if (has_count) {
        if (count > 0 || st->frame_left <= 0) {
            st->frame_left = count;
            st->seen = false;
        } else if (st->frame_left < slots) {
            slots = st->frame_left; // the rest of this report is unused padding
        }
        st->frame_left -= st->frame_left < l->contacts ? st->frame_left : l->contacts;
        frame_done = st->frame_left == 0;
    }

    int found = -1;
    int32_t found_id = 0, tip = 0;
    for (int s = 0; s < slots; s++) {
        const ht_contact_t *k = &l->c[s];
        int32_t id = s, t = 0;
        if (k->id.present) {
            read_field(&k->id, report, len, &id);
        }
        if (!read_field(&k->tip, report, len, &t)) {
            return false;
        }
        if (st->tracking ? id == st->active_id : t != 0) {
            found = s;
            found_id = id;
            tip = t;
            break;
        }
    }
    if (found < 0) {
        // The tracked contact only ended if a whole frame passed without it.
        if (st->tracking && has_count && frame_done && !st->seen) {
            st->tracking = false;
            out->down = false;
            out->x = out->y = 0;
            return true;
        }
        return false;
    }
    st->seen = true;

    int32_t x, y;
    if (!read_field(&l->c[found].x, report, len, &x) || !read_field(&l->c[found].y, report, len, &y)) {
        return false;
    }
    out->x = normalise(&l->c[found].x, x);
    out->y = normalise(&l->c[found].y, y);
    out->down = tip != 0;
    st->tracking = out->down;
    st->active_id = found_id;
    return true;
}
