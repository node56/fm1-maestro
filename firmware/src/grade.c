#include "grade.h"

void grade_init(grade_t *g) {
    g->notes_total = 0;
    g->notes_hit = 0;
    g->streak = 0;
    g->best_streak = 0;
    g->score = 10000;
}

uint8_t timing_window_ms(uint8_t level) {
    if (level > 5) level = 5;
    return (uint8_t)(160 - 18 * level);   /* 160,142,124,106,88,70 */
}

void grade_note(grade_t *g, int pitch_ok, int32_t err_ms, uint8_t level) {
    uint32_t penalty = 0;
    uint32_t w, a;

    g->notes_total++;
    if (!pitch_ok) {
        penalty = 400;          /* 4.00 points per wrong pitch */
        g->streak = 0;
    } else {
        w = timing_window_ms(level);
        a = err_ms < 0 ? (uint32_t)(-err_ms) : (uint32_t)err_ms;
        if (a > w) {
            penalty = 200;
            g->streak = 0;
        } else {
            penalty = (a * 200u) / w;   /* 0..200 scaled by lateness */
            g->streak++;
            if (g->streak > g->best_streak) g->best_streak = g->streak;
            g->notes_hit++;
        }
    }
    if (penalty >= g->score) g->score = 0;
    else g->score -= penalty;
}

uint8_t grade_stars(const grade_t *g) {
    if (g->score >= 9500) return 3;
    if (g->score >= 8500) return 2;
    if (g->score >= 7000) return 1;
    return 0;
}
