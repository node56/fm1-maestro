/* SPDX-License-Identifier: GPL-3.0-only */
/* Unit tests for firmware/src/midi.c (Standard MIDI File events).
 * No hardware, no platform: the take's event format, the SMF parser
 * and the SMF writer, and the delta-time array ops.
 *
 *   cc -O2 -Wall -Wextra -Ifirmware/src -o build/host/midi_test tests/midi_test.c
 *   build/host/midi_test
 */
#include <stdio.h>
#include <string.h>
#include "midi.c"

static int failures;

#define CHECK(name, ok) do { \
    printf("%s  %s\n", (ok) ? "ok  " : "FAIL", name); \
    if (!(ok)) failures++; } while (0)

/* A format-1 file, PPQ 96, two tracks (two sheets):
 * track 0: C5 on at 0, off at 96; E5 on at 96 (chord with G5), G5 on
 *   at 96, both off at 192; a JUMP marker at 192; End of Track.
 * track 1: a Note On velocity 0 (a Note Off by convention) and EOT.
 * Deltas are VLQ; running status is used for the second chord note. */
static const uint8_t MID[] = {
    'M','T','h','d', 0,0,0,6, 0,1, 0,2, 0,96,
    'M','T','r','k', 0,0,0,35,
    0x00, 0x90, 60, 100,
    0x60, 0x80, 60, 0,
    0x00, 0x90, 64, 100,
    0x00, 67, 100,              /* running status: another Note On */
    0x60, 0x80, 64, 0,
    0x00, 0x80, 67, 0,
    0x00, 0xFF, 0x06, 0x04, 'J','U','M','P',
    0x00, 0xFF, 0x2F, 0x00,
    'M','T','r','k', 0,0,0,8,
    0x00, 0x90, 72, 0,          /* Note On velocity 0 == Note Off */
    0x00, 0xFF, 0x2F, 0x00,
};

int main(void)
{
    mev_t ev[64];
    uint16_t n = 0;
    uint8_t out[512];
    uint32_t out_len = 0;
    uint32_t v;
    int rc;

    /* VLQ round-trip, including a multi-byte value. */
    {
        uint8_t b[4];
        uint32_t w = midi_vlq_write(b, sizeof b, 0x1234);
        CHECK("vlq: 0x1234 writes 2 bytes", w == 2);
        CHECK("vlq: 0x1234 reads back", midi_vlq_read(b, w, &v) == 2 && v == 0x1234);
        CHECK("vlq: 0 writes 1 byte 0x00", midi_vlq_write(b, sizeof b, 0) == 1 && b[0] == 0);
        CHECK("vlq: truncated reads 0", midi_vlq_read(b, 0, &v) == 0);
    }

    rc = midi_parse(MID, sizeof MID, ev, 64, &n);
    CHECK("parse: ok", rc == 0);
    CHECK("parse: 10 events (7 track 0 + EOT, 1 track 1 + EOT)", n == 10);
    if (n == 10) {
        CHECK("parse: C5 on at 0, vel 100", mev_is_note_on(&ev[0]) && ev[0].d1 == 60 && ev[0].d2 == 100 && ev[0].delta == 0);
        CHECK("parse: C5 off at 96", mev_is_note_off(&ev[1]) && ev[1].d1 == 60 && mev_abs(ev, 0, 1) == 96);
        CHECK("parse: chord E5+G5 at the same time (delta 0)", mev_is_note_on(&ev[2]) && ev[2].d1 == 64 && ev[2].delta == 0 &&
              mev_is_note_on(&ev[3]) && ev[3].d1 == 67 && ev[3].delta == 0 && mev_abs(ev, 0, 3) == 96);
        CHECK("parse: running status honoured", mev_is_note_on(&ev[3]));
        CHECK("parse: JUMP marker", mev_is_jump(&ev[6]) && ev[6].delta == 0);
        CHECK("parse: End of Track ends track 0", mev_is_eot(&ev[7]));
        CHECK("parse: Note On vel 0 stored as Note Off", mev_is_note_off(&ev[8]) && ev[8].d1 == 72 && ev[8].status == MIDI_NOTE_OFF);
        CHECK("parse: End of Track ends track 1", mev_is_eot(&ev[9]));
    }

    /* Serialize the parsed events and parse them again: the round
     * trip must be exact (same events, same deltas). */
    rc = midi_write(ev, n, out, sizeof out, &out_len);
    CHECK("write: ok", rc == 0);
    if (rc == 0) {
        mev_t ev2[64];
        uint16_t n2 = 0;
        uint16_t i;
        uint8_t ok = 1;
        CHECK("write: header is format 1, 2 tracks, PPQ 96",
              out[0] == 'M' && out[8] == 0 && out[9] == 1 && out[10] == 0 && out[11] == 2 && out[12] == 0 && out[13] == 96);
        rc = midi_parse(out, out_len, ev2, 64, &n2);
        CHECK("round-trip: re-parse ok", rc == 0);
        if (rc == 0 && n2 == n) {
            for (i = 0; i < n; i++)
                if (ev2[i].delta != ev[i].delta || ev2[i].status != ev[i].status ||
                    ev2[i].d1 != ev[i].d1 || ev2[i].d2 != ev[i].d2) ok = 0;
            CHECK("round-trip: events identical (note/chord/jump/EOT)", ok);
        } else {
            CHECK("round-trip: same event count", 0);
        }
    }

    /* Delta-time array ops: insert keeps absolute times, remove
     * restores them. Start from [on C5 @0][off C5 @96][EOT @96]. */
    {
        mev_t a[8];
        uint16_t m = 0;
        a[m++] = (mev_t){0, MIDI_NOTE_ON, 60, 100};
        a[m++] = (mev_t){96, MIDI_NOTE_OFF, 60, 0};
        a[m++] = (mev_t){0, MIDI_META, MIDI_META_EOT, 0};
        /* insert a D5 on at 48: the C5 off keeps absolute 96. */
        rc = mev_insert(a, &m, 8, 1, 48, MIDI_NOTE_ON, 62, 100);
        CHECK("insert: ok", rc == 0);
        CHECK("insert: C5 off still at absolute 96", mev_abs(a, 0, 2) == 96);
        CHECK("insert: new note at absolute 48", mev_abs(a, 0, 1) == 48);
        CHECK("insert: EOT still at absolute 96", mev_abs(a, 0, 3) == 96);
        /* insert past the end (at 192): the track extends. */
        rc = mev_insert(a, &m, 8, 3, 96, MIDI_NOTE_ON, 64, 100);
        CHECK("insert past end: ok", rc == 0);
        CHECK("insert past end: new note at absolute 192", mev_abs(a, 0, 3) == 192);
        CHECK("insert past end: EOT moved to 192", mev_abs(a, 0, 4) == 192);
        /* remove the D5: absolute times after it are unchanged. */
        mev_remove(a, &m, 1);
        CHECK("remove: C5 off back at absolute 96", mev_abs(a, 0, 1) == 96);
        CHECK("remove: count back to 4", m == 4);
        /* find: first event at or after 96 is the C5 off. */
        CHECK("find_abs: >=96 finds the off", mev_find_abs(a, 0, m, 96, 1) == 1);
        CHECK("find_abs: >48 finds the off", mev_find_abs(a, 0, m, 48, 0) == 1);
        CHECK("find_abs: past the end returns e", mev_find_abs(a, 0, m, 9999, 1) == m);
        /* full array refuses an insert. */
        {
            mev_t full[2];
            uint16_t fm = 2;
            full[0] = (mev_t){0, MIDI_NOTE_ON, 60, 100};
            full[1] = (mev_t){0, MIDI_META, MIDI_META_EOT, 0};
            CHECK("insert: full array refuses", mev_insert(full, &fm, 2, 1, 0, MIDI_NOTE_ON, 62, 100) == -1);
        }
    }

    /* A file with a foreign PPQ (480) rescales to 96: a quarter
     * (480 ticks in the file) becomes 96 here. */
    {
        static const uint8_t mid480[] = {
            'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0x01,0xE0,
            'M','T','r','k', 0,0,0,13,
            0x00, 0x90, 60, 100,
            0x83, 0x60, 0x80, 60, 0,   /* delta 480 (VLQ 0x83 0x60) */
            0x00, 0xFF, 0x2F, 0x00,
        };
        mev_t e3[8];
        uint16_t n3 = 0;
        rc = midi_parse(mid480, sizeof mid480, e3, 8, &n3);
        CHECK("parse PPQ 480: ok, rescaled to 96", rc == 0 && n3 == 3 && mev_abs(e3, 0, 1) == 96);
    }

    /* Malformed input is rejected, not crashed on. */
    CHECK("parse: bad magic rejected", midi_parse((const uint8_t *)"NOPE", 4, ev, 64, &n) == -1);
    CHECK("parse: track without EOT rejected", midi_parse(MID, sizeof MID - 9, ev, 64, &n) == -1);

    printf("\n%s\n", failures ? "MIDI TEST FAILURES" : "ALL MIDI TESTS PASSED");
    return failures ? 1 : 0;
}
