/* SPDX-License-Identifier: GPL-3.0-only */
/* Maestro persistent settings, flash blob 0. No scoring. */
#ifndef MAESTRO_STORE_H
#define MAESTRO_STORE_H
#include <stdint.h>

typedef struct {
    uint32_t magic;
    uint8_t cursor_x;     /* 30..180 */
    uint8_t speed_idx;    /* 0..4 -> 0.5x..2x */
    uint8_t level;        /* drill difficulty 0..5 */
    uint8_t sel;          /* book selection */
} settings_t;

void mset_load(settings_t *s);  /* defaults when the blob is empty/bad */
void mset_save(settings_t *s);

#endif
