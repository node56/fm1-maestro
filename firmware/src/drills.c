#include "drills.h"

uint32_t xs32(uint32_t *state) {
    uint32_t x = *state;
    if (x == 0) x = 0x9E3779B9u;   /* xorshift32 sticks at 0; reseed */
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

/* rhythm bit -> ticks, in a fixed order for deterministic selection */
static const uint16_t rhy_ticks[5] = { 96, 192, 48, 24, 144 };
static const uint8_t  rhy_bits[5]  = { RHY_Q, RHY_H, RHY_E, RHY_S, RHY_QD };

uint16_t drill_generate(const drill_params_t *p,
                        lesson_event_t *out, uint16_t cap) {
    uint32_t rng = p->seed;
    uint16_t n = 0;
    uint8_t bar, degree = 0;
    uint16_t bar_ticks = (uint16_t)p->beats_per_bar * TICKS_PER_QUARTER;

    if (cap == 0 || p->bars == 0) return 0;

    for (bar = 0; bar < p->bars; bar++) {
        uint16_t used = 0;
        while (used < bar_ticks) {
            /* pick a duration from the mask that fits the bar */
            uint16_t choices[5];
            uint8_t nchoices = 0, ci;
            uint8_t i;
            for (i = 0; i < 5; i++) {
                if ((p->rhythm_mask & rhy_bits[i]) &&
                    rhy_ticks[i] <= bar_ticks - used)
                    choices[nchoices++] = rhy_ticks[i];
            }
            if (nchoices == 0) break;   /* shouldn't happen; bail safely */
            ci = (uint8_t)(xs32(&rng) % nchoices);

            if (n >= cap) return n;
            if (p->allow_rests && (xs32(&rng) % 100) < 12) {
                out[n].is_rest = 1;
                out[n].midi = 0;
            } else {
                /* random walk inside [0, range]: mostly steps, some skips */
                uint32_t r = xs32(&rng) % 100;
                int step;
                if (r < 70) {
                    step = (xs32(&rng) & 1) ? 1 : -1;
                } else if (r < 95) {
                    int mag = 2 + (int)(xs32(&rng) % 2);   /* 2 or 3 */
                    step = (xs32(&rng) & 1) ? mag : -mag;
                } else {
                    step = 0;
                }
                {
                    int d = (int)degree + step;
                    if (d < 0) d = 0;
                    if (d > p->range) d = p->range;
                    degree = (uint8_t)d;
                }
                out[n].is_rest = 0;
                out[n].midi = (uint8_t)(p->position_midi + degree);
            }
            out[n].ticks = choices[ci];
            n++;
            used = (uint16_t)(used + choices[ci]);
        }
    }

    /* resolve the final note to the root */
    {
        int i = (int)n - 1;
        while (i >= 0 && out[i].is_rest) i--;
        if (i >= 0) out[i].midi = p->position_midi;
    }
    return n;
}
