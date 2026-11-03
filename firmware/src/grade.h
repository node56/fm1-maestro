#ifndef MAESTRO_GRADE_H
#define MAESTRO_GRADE_H

/* Pitch + timing grading. Pure C, integer math only.
 * Score is 0..10000 (i.e. percent * 100).
 */

#include <stdint.h>

typedef struct {
    uint16_t notes_total;
    uint16_t notes_hit;    /* pitch ok AND inside the timing window */
    uint16_t streak;
    uint16_t best_streak;
    uint32_t score;        /* 0..10000 */
} grade_t;

void grade_init(grade_t *g);

/* Timing window in ms by level: L0 160ms -> L5 70ms. */
uint8_t timing_window_ms(uint8_t level);

/* Record one played note. pitch_ok: exact MIDI match.
 * err_ms: signed timing error (negative = early). */
void grade_note(grade_t *g, int pitch_ok, int32_t err_ms, uint8_t level);

/* 3 stars >= 95.00, 2 >= 85.00, 1 >= 70.00, else 0. */
uint8_t grade_stars(const grade_t *g);

#endif /* MAESTRO_GRADE_H */
