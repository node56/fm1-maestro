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
    uint8_t buf[sizeof(settings_t) + 4];
    uint32_t stored_crc;
    settings_t tmp;
    /* defaults (an empty or bad blob leaves these) */
    s->magic = 0;
    s->reserved = 0;
    s->speed_idx = 2;
    s->level = 2;
    s->sel = 0;
    if (blob_load(0u, buf, sizeof buf) < 0) return;
    stored_crc = (uint32_t)buf[sizeof(settings_t)]
        | ((uint32_t)buf[sizeof(settings_t) + 1] << 8)
        | ((uint32_t)buf[sizeof(settings_t) + 2] << 16)
        | ((uint32_t)buf[sizeof(settings_t) + 3] << 24);
    if (mset_crc32(buf, sizeof(settings_t)) != stored_crc) return;
    /* load into a scratch copy and validate the magic before
     * overwriting the defaults, so a bad blob cannot clobber them. */
    memcpy(&tmp, buf, sizeof tmp);
    if (tmp.magic != SET_MAGIC) return;
    *s = tmp;
    if (s->speed_idx > 4u) s->speed_idx = 2;
    if (s->level > 5u) s->level = 2;
    if (s->sel > 6u) s->sel = 0;
}

void mset_save(settings_t *s) {
    uint8_t buf[sizeof(settings_t) + 4];
    uint32_t c;
    s->magic = SET_MAGIC;
    memcpy(buf, s, sizeof *s);
    c = mset_crc32(buf, sizeof(settings_t));
    buf[sizeof(settings_t)] = (uint8_t)c;
    buf[sizeof(settings_t) + 1] = (uint8_t)(c >> 8);
    buf[sizeof(settings_t) + 2] = (uint8_t)(c >> 16);
    buf[sizeof(settings_t) + 3] = (uint8_t)(c >> 24);
    blob_save(0u, buf, sizeof buf);
}
