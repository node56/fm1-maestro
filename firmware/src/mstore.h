/* SPDX-License-Identifier: GPL-3.0-only */
/* Maestro persistent settings, flash blob 0. No scoring. */
#ifndef MAESTRO_STORE_H
#define MAESTRO_STORE_H
#include <stdint.h>

typedef struct {
    uint32_t magic;
    uint8_t reserved;     /* was cursor_x; the playhead is fixed now */
    uint8_t speed_idx;    /* unused now (the tempo is KNOB1's job); kept so the blob stays 8 bytes */
    uint8_t level;        /* drill difficulty 0..5; not read at runtime yet, kept for the blob */
    uint8_t sel;          /* book selection (0..6); restored on boot, saved on change */
} settings_t;

void mset_load(settings_t *s);  /* defaults when the blob is empty/bad */
void mset_save(settings_t *s);

#endif
