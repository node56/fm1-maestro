/* SPDX-License-Identifier: GPL-3.0-only */
#include "mstore.h"

#define SET_MAGIC 0x4D414553u /* "MAES" */

static uint32_t mset_crc32(const uint8_t *p, uint32_t n) {
    uint32_t c = 0xFFFFFFFFu, i, j;
    for (i = 0; i < n; i++) {
        c ^= p[i];
        for (j = 0; j < 8u; j++)
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

void mset_load(settings_t *s) {
    uint8_t i;
    s->magic = 0;
    s->cursor_x = 60;
    s->speed_idx = 2;
    for (i = 0; i < NSTARS; i++) { s->stars[i] = 0; s->best_pct[i] = 0; }
    {
        uint8_t buf[sizeof(settings_t) + 4];
        if (blob_load(0u, buf, sizeof buf) < 0) return;
        if (mset_crc32(buf, sizeof(settings_t)) !=
            ((uint32_t)buf[sizeof(settings_t)] | ((uint32_t)buf[sizeof(settings_t) + 1] << 8) |
             ((uint32_t)buf[sizeof(settings_t) + 2] << 16) | ((uint32_t)buf[sizeof(settings_t) + 3] << 24)))
            return;
        {
            settings_t tmp;
            uint8_t *d = (uint8_t *)&tmp, k;
            for (k = 0; k < sizeof tmp; k++) d[k] = buf[k];
            if (tmp.magic != SET_MAGIC) return;
            *s = tmp;
            if (s->cursor_x < 30u || s->cursor_x > 180u) s->cursor_x = 60;
            if (s->speed_idx > 4u) s->speed_idx = 2;
        }
    }
}

void mset_save(settings_t *s) {
    uint8_t buf[sizeof(settings_t) + 4];
    uint8_t *d = (uint8_t *)s, k;
    uint32_t c;
    s->magic = SET_MAGIC;
    for (k = 0; k < sizeof *s; k++) buf[k] = d[k];
    c = mset_crc32(buf, sizeof(settings_t));
    buf[sizeof(settings_t)] = (uint8_t)c;
    buf[sizeof(settings_t) + 1] = (uint8_t)(c >> 8);
    buf[sizeof(settings_t) + 2] = (uint8_t)(c >> 16);
    buf[sizeof(settings_t) + 3] = (uint8_t)(c >> 24);
    blob_save(0u, buf, sizeof buf);
}
