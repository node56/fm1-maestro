#ifndef MAESTRO_LESSON_H
#define MAESTRO_LESSON_H

/* Lesson format parser + practice engine.
 * Pure C, no platform dependencies — unit-testable on the host.
 * Integer math only: the AC791N has no FPU.
 *
 * Note-name convention: C5 == MIDI 60 (middle C).
 */

#include <stdint.h>

#define TICKS_PER_QUARTER 96u
#define LESSON_MAX_EVENTS 256
#define LESSON_TITLE_LEN  48
#define LESSON_SOURCE_LEN 96

typedef enum {
    KIND_NOTE_ID = 0,
    KIND_RHYTHM,
    KIND_MELODY,
    KIND_DRILL,
    KIND_CHALLENGE,
    KIND_UNKNOWN
} lesson_kind_t;

typedef struct {
    uint8_t  midi;    /* MIDI note number; undefined when is_rest */
    uint16_t ticks;   /* duration, in ticks (quarter = 96) */
    uint8_t  is_rest;
} lesson_event_t;

typedef struct {
    char          title[LESSON_TITLE_LEN];
    char          source[LESSON_SOURCE_LEN];
    lesson_kind_t kind;
    uint8_t       level;          /* 0..5 */
    uint8_t       position_midi;  /* five-finger root, e.g. 60 */
    uint16_t      tempo;          /* quarter-note BPM */
    uint8_t       beats_per_bar;
    uint16_t      event_count;
    lesson_event_t events[LESSON_MAX_EVENTS];
} lesson_t;

/* Parse lesson text (see lessons/format.md).
 * Returns 0 on success, <0 on error:
 *  -1 bad header line, -2 too many events, -3 bad note token,
 *  -4 bad duration token, -5 missing required field. */
int lesson_parse(const char *text, lesson_t *out);

/* ticks -> milliseconds at a tempo. No 64-bit math:
 * max realistic ticks (~98304) * 625 < 2^32. */
static inline uint32_t ticks_to_ms(uint32_t ticks, uint16_t tempo) {
    if (tempo == 0) return 0;
    return (ticks * 625u) / tempo;
}

/* ---- practice engine: tick-driven, caller owns the clock ---- */

typedef struct {
    const lesson_t *lesson;
    uint16_t        idx;      /* current event */
    uint32_t        elapsed;  /* ticks of events before idx */
    uint32_t        now;      /* caller's tick clock */
} practice_t;

void practice_start(practice_t *p, const lesson_t *lesson);
void practice_set_now(practice_t *p, uint32_t now);
/* Current expected event (NULL when done). Rests whose time has
 * passed are skipped automatically. */
const lesson_event_t *practice_current(practice_t *p);
/* Start tick of the current event (for timing-error measurement). */
uint32_t practice_event_start(const practice_t *p);
/* Player played `midi`. Returns 1 = correct pitch (advances),
 * 0 = wrong pitch (stays), -1 = lesson complete. For KIND_RHYTHM
 * any pitch counts. */
int practice_play(practice_t *p, uint8_t midi);
int practice_done(const practice_t *p);

#endif /* MAESTRO_LESSON_H */
