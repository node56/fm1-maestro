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

/* ---- multi-part songs (v0.26) ----
 * A song is a set of parts sharing one tempo and one bar grid.
 * Each part has a guide (a lesson_event_t list, like a lesson's)
 * and a MIDI channel: in the take's single event list, a note's
 * part IS its channel, so the parts live interleaved in one
 * sequence and playback is one walk over one sorted list. Parts
 * are listed in learning order. A part with event_count 0 exists
 * -- its channel is reserved -- but cannot be learned yet.
 * Generated at build time by tools/lessons2c.py; the firmware
 * only plays what it is given. */
#define SONG_MAX_PARTS 4
typedef struct {
    uint8_t channel;              /* the part's MIDI channel in the take */
    const char *name;             /* "Percussion", "Bass", "Harmony", "Melody" */
    uint16_t event_count;
    const lesson_event_t *events; /* NULL when event_count is 0 */
} song_part_t;

typedef struct {
    char title[LESSON_TITLE_LEN];
    uint16_t tempo;               /* quarter-note BPM, shared by all parts */
    uint8_t beats_per_bar;
    uint8_t part_count;
    song_part_t parts[SONG_MAX_PARTS];   /* in learning order */
} song_t;

extern const song_t ode_song;   /* defined in lessons_data.c (generated) */

#endif /* MAESTRO_LESSON_H */
