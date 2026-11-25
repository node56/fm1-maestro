#ifndef MAESTRO_LESSON_H
#define MAESTRO_LESSON_H

/* Lesson data types (the book). Pure C, no platform dependencies.
 * Integer math only: the AC791N has no FPU.
 *
 * Note-name convention: C5 == MIDI 60 (middle C).
 *
 * The lesson text format (lessons dir, .lesson files) is parsed at
 * build time by tools/lessons2c.py into firmware/src/lessons_data.c;
 * the format is documented in lessons/format.md. There is no runtime
 * lesson parser in the firmware.
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

/* ---- scores and pieces (v0.31) ----
 * A SCORE is a self-contained unit: a sequence of notes AND the
 * patch they sound with. The patch is score data, not a routing
 * decision elsewhere: a score that says DRUM sounds as a drum
 * wherever it is played, and a score that says PRESET sounds
 * with the PRESETS knob's current patch, resolved at trigger
 * time. A PIECE is a mechanism for chaining scores: an ordered
 * list, any number of scores, each any patch. A lesson is a
 * one-score piece (its patch is PRESET); Free Play is a piece
 * whose score is blank; the song is a piece of four scores, in
 * learning order. A TAKE is per score: within a sheet's event
 * list, a note's score index is its status byte's low nibble
 * (the field MIDI calls the channel -- the take still
 * serializes as ordinary SMF, one track per sheet, and a note's
 * score is told apart the way a part used to be told apart by
 * channel). A score with event_count 0 exists -- there is
 * nothing to learn yet -- but cannot be cued.
 * Generated at build time by tools/lessons2c.py; the firmware
 * only plays what it is given. */
#define PIECE_MAX_SCORES 4
#define SCORE_PATCH_PRESET 0xFFu  /* the PRESETS knob's patch, at trigger time */
#define SCORE_PATCH_PIANO1 1u     /* PIANO 1: a preset-family voice, not the knob's */
#define SCORE_PATCH_BASS 5u
#define SCORE_PATCH_DRUM 6u
typedef struct {
    uint8_t patch;              /* SCORE_PATCH_*: the patch this score sounds with */
    int8_t transpose;           /* semitones added when sounding (bass: -12) */
    uint8_t fixed_pitch;        /* 0 = as written; else the pitch it always sounds at (drum: 38) */
    const char *name;           /* "Percussion", "Bass", "Harmony", "Melody" */
    uint16_t event_count;
    const lesson_event_t *events; /* NULL when event_count is 0 */
} score_t;

typedef struct {
    char title[LESSON_TITLE_LEN];
    uint16_t tempo;               /* quarter-note BPM, shared by all scores */
    uint8_t beats_per_bar;
    uint8_t score_count;
    score_t scores[PIECE_MAX_SCORES];   /* in learning order */
} piece_t;

extern const piece_t ode_piece;   /* defined in lessons_data.c (generated) */

#endif /* MAESTRO_LESSON_H */
