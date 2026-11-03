/* SPDX-License-Identifier: GPL-3.0-only */
/* Maestro v0.1 persistent state: settings + per-piece bests, flash blob 0. */
#ifndef MAESTRO_STORE_H
#define MAESTRO_STORE_H
#include <stdint.h>

#define NSTARS 8u

typedef struct {
    uint32_t magic;
    uint8_t cursor_x;     /* 30..180 */
    uint8_t speed_idx;    /* 0..4 -> 0.5x..2x */
    uint8_t stars[NSTARS];
    uint16_t best_pct[NSTARS]; /* 0..10000 */
} settings_t;

void mset_load(settings_t *s);  /* defaults when the blob is empty/bad */
void mset_save(settings_t *s);

#endif
