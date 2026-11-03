#ifndef MAESTRO_DRILLS_H
#define MAESTRO_DRILLS_H

/* Seeded generative drills: infinite, deterministic, adaptive.
 * Pure C, no platform dependencies. Integer math only.
 *
 * Same seed -> same drill (shareable, repeatable, host-testable).
 */

#include <stdint.h>
#include "lesson.h"

/* rhythm vocabulary bits */
#define RHY_Q   0x01u   /* quarter        */
#define RHY_H   0x02u   /* half           */
#define RHY_E   0x04u   /* eighth         */
#define RHY_S   0x08u   /* sixteenth      */
#define RHY_QD  0x10u   /* dotted quarter */

typedef struct {
    uint8_t  position_midi;  /* five-finger root, e.g. 60 */
    uint8_t  range;          /* semitones above root the melody may use */
    uint8_t  tempo;          /* quarter BPM (not used by generator, for UI) */
    uint8_t  bars;           /* phrase length */
    uint8_t  beats_per_bar;  /* 3 or 4 */
    uint8_t  rhythm_mask;    /* RHY_* bits */
    uint8_t  allow_rests;    /* 0/1 */
    uint32_t seed;
} drill_params_t;

/* xorshift32 PRNG step. */
uint32_t xs32(uint32_t *state);

/* Generate a drill into out[] (capacity cap events).
 * Returns the event count. Final note always resolves to the root. */
uint16_t drill_generate(const drill_params_t *p,
                        lesson_event_t *out, uint16_t cap);

#endif /* MAESTRO_DRILLS_H */
