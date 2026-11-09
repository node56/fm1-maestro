/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MAESTRO_MIDI_H
#define MAESTRO_MIDI_H
/* Standard MIDI File (SMF) events are the take's storage.
 *
 * The firmware's tick unit is 96 ticks per quarter note, which is
 * MIDI PPQ=96 exactly, so a .mid file's timing maps directly: no
 * conversion, no re-encoding. An ordinary .mid file parses straight
 * into the take, and the take serializes straight to a .mid file.
 *
 * One event is one MIDI event: a delta time (ticks since the previous
 * event in the same track), a status byte, and up to two data bytes.
 *   Note On:  status 0x90, d1 = pitch (0..127), d2 = velocity.
 *   Note Off: status 0x80, d1 = pitch, d2 = 0. A Note On with
 *             velocity 0 is a Note Off (standard MIDI convention)
 *             and is stored as one on parse.
 *   Marker:   status 0xFF, d1 = 0x06, d2 = 4, text "JUMP": the SEQ
 *             jump, the loop point of the take.
 *   End of Track: status 0xFF, d1 = 0x2F, d2 = 0. One per sheet: a
 *             Format-1 file has one track per sheet, and in memory
 *             the single take array is partitioned the same way --
 *             each sheet's block ends with its End of Track.
 * A chord is several Note Ons at the same delta time (one per pitch).
 * The playhead triggers them together, up to the 6-voice engine.
 *
 * Integer math only (AC791N: no FPU, no 64-bit division). VLQ
 * deltas are at most a few thousand ticks in real files; the parser
 * rejects absurd ones before scaling.
 */
#include <stdint.h>

#define MIDI_PPQ 96u
#define MIDI_NOTE_OFF 0x80u
#define MIDI_NOTE_ON 0x90u
#define MIDI_META 0xFFu
#define MIDI_META_MARKER 0x06u
#define MIDI_META_EOT 0x2Fu
#define MIDI_MARKER_JUMP "JUMP"
#define MIDI_MARKER_JUMP_LEN 4u

typedef struct {
    uint32_t delta;   /* ticks since the previous event in the track */
    uint8_t status;   /* 0x90 Note On, 0x80 Note Off, 0xFF Meta */
    uint8_t d1;       /* pitch, or the meta type */
    uint8_t d2;       /* velocity, or the meta length */
} mev_t;

static inline uint8_t mev_is_note_on(const mev_t *e) { return e->status == MIDI_NOTE_ON && e->d2 != 0; }
static inline uint8_t mev_is_note_off(const mev_t *e) { return e->status == MIDI_NOTE_OFF || (e->status == MIDI_NOTE_ON && e->d2 == 0); }
static inline uint8_t mev_is_jump(const mev_t *e) { return e->status == MIDI_META && e->d1 == MIDI_META_MARKER && e->d2 == MIDI_MARKER_JUMP_LEN; }
static inline uint8_t mev_is_eot(const mev_t *e) { return e->status == MIDI_META && e->d1 == MIDI_META_EOT; }

/* VLQ: returns bytes consumed (0 = truncated/bad), value in *v. */
static inline uint32_t midi_vlq_read(const uint8_t *p, uint32_t n, uint32_t *v)
{
    uint32_t i = 0;
    *v = 0;
    while (i < n && i < 4u) {
        *v = (*v << 7) | (p[i] & 0x7Fu);
        if (!(p[i] & 0x80u)) return i + 1u;
        i++;
    }
    return 0;
}
/* VLQ: writes the value, returns bytes written (0 = no room). */
static inline uint32_t midi_vlq_write(uint8_t *p, uint32_t cap, uint32_t v)
{
    uint8_t tmp[4];
    uint32_t n = 0, i;
    tmp[n++] = (uint8_t)(v & 0x7Fu);
    v >>= 7;
    while (v) { tmp[n++] = (uint8_t)(v & 0x7Fu); v >>= 7; }
    if (n > cap) return 0;
    for (i = 0; i < n; i++) p[i] = tmp[n - 1u - i] | (i + 1u < n ? 0x80u : 0u);
    return n;
}

int midi_parse(const uint8_t *buf, uint32_t len, mev_t *ev, uint16_t cap, uint16_t *out_n);
int midi_write(const mev_t *ev, uint16_t n, uint8_t *buf, uint32_t cap, uint32_t *out_len);
uint32_t mev_abs(const mev_t *ev, uint16_t b, uint16_t i);
uint16_t mev_find_abs(const mev_t *ev, uint16_t b, uint16_t e, uint32_t abs_ticks, uint8_t ge);
int mev_insert(mev_t *ev, uint16_t *n, uint16_t cap, uint16_t pos, uint32_t delta, uint8_t st, uint8_t d1, uint8_t d2);
void mev_remove(mev_t *ev, uint16_t *n, uint16_t i);

#endif /* MAESTRO_MIDI_H */
