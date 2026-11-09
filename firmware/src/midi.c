/* SPDX-License-Identifier: GPL-3.0-only */
/* Standard MIDI File (SMF) parse and write for the take (midi.h).
 * Pure code, no platform dependencies: the host unit test
 * (tests/midi_test.c) compiles this file on its own.
 *
 * Parse: format 0 or 1, any PPQ (rescaled to 96). Each track's events
 * are appended in order, each track ending with its End of Track, so
 * a format-1 file lands in the take exactly as one block per sheet.
 * Running status is honoured; Note On velocity 0 becomes Note Off;
 * other channel messages are skipped; Meta events other than the
 * JUMP marker and End of Track are skipped (tempo, time signature
 * and track names carry no take data). SysEx is skipped.
 * Write: format 1, one track per End of Track, PPQ 96. The JUMP
 * marker is written with its text; End of Track closes each track.
 */
#include "midi.h"

static uint16_t midi_rd16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t midi_rd32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

/* the absolute time of event i, summing deltas from block start b. */
uint32_t mev_abs(const mev_t *ev, uint16_t b, uint16_t i)
{
    uint32_t t = 0;
    uint16_t k;
    for (k = b; k <= i; k++) t += ev[k].delta;
    return t;
}

/* first index in [b, e) whose absolute time is >= abs_ticks (ge=1)
 * or > abs_ticks (ge=0); e when there is none. */
uint16_t mev_find_abs(const mev_t *ev, uint16_t b, uint16_t e, uint32_t abs_ticks, uint8_t ge)
{
    uint32_t t = 0;
    uint16_t i;
    for (i = b; i < e; i++) {
        t += ev[i].delta;
        if ((ge && t >= abs_ticks) || (!ge && t > abs_ticks)) return i;
    }
    return e;
}

/* insert an event at pos (the tail shifts right by one). The event
 * that was at pos keeps its absolute time: its delta shrinks by the
 * new event's delta (clamped at 0, so inserting past a track's end
 * extends the track instead of underflowing). Returns 0, or -1 when
 * the array is full. */
int mev_insert(mev_t *ev, uint16_t *n, uint16_t cap, uint16_t pos, uint32_t delta, uint8_t st, uint8_t d1, uint8_t d2)
{
    uint16_t i;
    if (*n >= cap || pos > *n) return -1;
    for (i = *n; i > pos; i--) ev[i] = ev[i - 1u];
    ev[pos].delta = delta;
    ev[pos].status = st;
    ev[pos].d1 = d1;
    ev[pos].d2 = d2;
    if (pos < *n) {   /* the event that was at pos keeps its absolute time */
        if (ev[pos + 1u].delta >= delta) ev[pos + 1u].delta -= delta;
        else ev[pos + 1u].delta = 0;
    }
    (*n)++;
    return 0;
}

/* remove the event at i (the tail shifts left by one). The event
 * after it inherits its delta, so absolute times after i are
 * unchanged. */
void mev_remove(mev_t *ev, uint16_t *n, uint16_t i)
{
    uint16_t j;
    if (i >= *n) return;
    if (i + 1u < *n) ev[i + 1u].delta += ev[i].delta;
    for (j = i; j + 1u < *n; j++) ev[j] = ev[j + 1u];
    (*n)--;
}

/* parse one track's events into ev (appended at *n). Returns 0, or
 * -1 on a malformed track / a full array. ppq is the file's PPQ. */
static int midi_parse_track(const uint8_t *p, uint32_t len, uint16_t ppq, mev_t *ev, uint16_t cap, uint16_t *n)
{
    uint32_t pos = 0;
    uint8_t running = 0;
    while (pos < len) {
        uint32_t delta;
        uint32_t rd = midi_vlq_read(p + pos, len - pos, &delta);
        uint8_t st;
        if (!rd) return -1;
        pos += rd;
        /* a delta above ~3M ticks would overflow the PPQ rescale
         * (delta * 96 must fit in 32 bits); real files never come
         * close, so this is corruption, not music. */
        if (delta > 0x2FFFFFu) return -1;
        if (ppq != MIDI_PPQ) delta = (delta * MIDI_PPQ + ppq / 2u) / ppq;
        if (pos >= len) return -1;
        st = p[pos];
        if (st < 0x80u) {
            if (!running) return -1;
            st = running;   /* running status: no status byte */
        } else {
            pos++;
            if (st < 0xF0u) running = st;
        }
        if (st == 0xFFu) {   /* meta event */
            uint8_t type;
            uint32_t mlen, mr;
            if (pos >= len) return -1;
            type = p[pos++];
            mr = midi_vlq_read(p + pos, len - pos, &mlen);
            if (!mr || pos + mr + mlen > len) return -1;
            pos += mr;
            if (type == MIDI_META_EOT) {
                if (*n >= cap) return -1;
                ev[*n].delta = delta; ev[*n].status = MIDI_META;
                ev[*n].d1 = MIDI_META_EOT; ev[*n].d2 = 0;
                (*n)++;
                return 0;   /* a track ends at its End of Track */
            }
            if (type == MIDI_META_MARKER && mlen == MIDI_MARKER_JUMP_LEN &&
                p[pos] == 'J' && p[pos + 1u] == 'U' && p[pos + 2u] == 'M' && p[pos + 3u] == 'P') {
                if (*n >= cap) return -1;
                ev[*n].delta = delta; ev[*n].status = MIDI_META;
                ev[*n].d1 = MIDI_META_MARKER; ev[*n].d2 = MIDI_MARKER_JUMP_LEN;
                (*n)++;
            }
            pos += mlen;   /* any other meta: skipped */
            running = 0;   /* a meta event cancels running status */
        } else if (st == 0xF0u || st == 0xF7u) {   /* SysEx: skipped */
            uint32_t mlen, mr = midi_vlq_read(p + pos, len - pos, &mlen);
            if (!mr || pos + mr + mlen > len) return -1;
            pos += mr + mlen;
            running = 0;
        } else if (st >= 0xF0u) {   /* system common / realtime: skipped (no data) */
            running = 0;
        } else {
            uint8_t hi = (uint8_t)(st & 0xF0u), d1, d2 = 0;
            uint8_t ndata = (hi == 0xC0u || hi == 0xD0u) ? 1u : 2u;
            if (pos + ndata > len) return -1;
            d1 = p[pos++];
            if (ndata == 2u) d2 = p[pos++];
            if (hi == MIDI_NOTE_ON || hi == MIDI_NOTE_OFF) {
                if (*n >= cap) return -1;
                /* Note On velocity 0 is a Note Off. */
                ev[*n].delta = delta;
                ev[*n].status = (hi == MIDI_NOTE_ON && d2 != 0) ? MIDI_NOTE_ON : MIDI_NOTE_OFF;
                ev[*n].d1 = (uint8_t)(d1 & 0x7Fu);
                ev[*n].d2 = (ev[*n].status == MIDI_NOTE_ON) ? (uint8_t)(d2 & 0x7Fu) : 0;
                (*n)++;
            }
            /* any other channel message carries no take data. */
        }
    }
    return -1;   /* a track without an End of Track is malformed */
}

int midi_parse(const uint8_t *buf, uint32_t len, mev_t *ev, uint16_t cap, uint16_t *out_n)
{
    uint16_t format, ntrk, ppq, t;
    uint32_t pos;
    if (len < 14u || buf[0] != 'M' || buf[1] != 'T' || buf[2] != 'h' || buf[3] != 'd') return -1;
    if (midi_rd32(buf + 4) != 6u) return -1;
    format = midi_rd16(buf + 8);
    ntrk = midi_rd16(buf + 10);
    ppq = midi_rd16(buf + 12);
    if (format > 1u || !ntrk || (ppq & 0x8000u)) return -1;   /* no format 2, no SMPTE timebase */
    *out_n = 0;
    pos = 14;
    for (t = 0; t < ntrk; t++) {
        uint32_t tlen;
        if (pos + 8u > len || buf[pos] != 'M' || buf[pos + 1u] != 'T' || buf[pos + 2u] != 'r' || buf[pos + 3u] != 'k') return -1;
        tlen = midi_rd32(buf + pos + 4);
        pos += 8;
        if (pos + tlen > len) return -1;
        if (midi_parse_track(buf + pos, tlen, ppq, ev, cap, out_n)) return -1;
        pos += tlen;
    }
    return 0;
}

/* write the flat event array as a format-1 file: one track per End
 * of Track. Returns 0, or -1 when the buffer is too small. */
int midi_write(const mev_t *ev, uint16_t n, uint8_t *buf, uint32_t cap, uint32_t *out_len)
{
    uint32_t pos = 0, i = 0;
    uint16_t ntrk = 0;
    uint16_t k;
    for (k = 0; k < n; k++) if (mev_is_eot(&ev[k])) ntrk++;
    if (!ntrk) return -1;
    if (cap < 14u) return -1;
    buf[pos++] = 'M'; buf[pos++] = 'T'; buf[pos++] = 'h'; buf[pos++] = 'd';
    buf[pos++] = 0; buf[pos++] = 0; buf[pos++] = 0; buf[pos++] = 6;
    buf[pos++] = 0; buf[pos++] = 1;   /* format 1 */
    buf[pos++] = (uint8_t)(ntrk >> 8); buf[pos++] = (uint8_t)ntrk;
    buf[pos++] = (uint8_t)(MIDI_PPQ >> 8); buf[pos++] = (uint8_t)MIDI_PPQ;
    while (i < n) {
        uint32_t track_start, len_pos, track_len;
        uint16_t j = i;
        if (pos + 8u > cap) return -1;
        buf[pos++] = 'M'; buf[pos++] = 'T'; buf[pos++] = 'r'; buf[pos++] = 'k';
        len_pos = pos;
        pos += 4;   /* length, patched below */
        track_start = pos;
        while (j < n) {
            uint32_t w;
            const mev_t *e = &ev[j];
            w = midi_vlq_write(buf + pos, cap - pos, e->delta);
            if (!w) return -1;
            pos += w;
            if (e->status == MIDI_META && e->d1 == MIDI_META_MARKER) {
                static const char JM[] = MIDI_MARKER_JUMP;
                uint32_t c;
                if (pos + 3u + MIDI_MARKER_JUMP_LEN > cap) return -1;
                buf[pos++] = MIDI_META; buf[pos++] = MIDI_META_MARKER;
                buf[pos++] = (uint8_t)MIDI_MARKER_JUMP_LEN;
                for (c = 0; c < MIDI_MARKER_JUMP_LEN; c++) buf[pos++] = (uint8_t)JM[c];
            } else if (e->status == MIDI_META && e->d1 == MIDI_META_EOT) {
                if (pos + 3u > cap) return -1;
                buf[pos++] = MIDI_META; buf[pos++] = MIDI_META_EOT; buf[pos++] = 0;
            } else if (e->status == MIDI_NOTE_ON || e->status == MIDI_NOTE_OFF) {
                if (pos + 3u > cap) return -1;
                buf[pos++] = e->status; buf[pos++] = e->d1; buf[pos++] = e->d2;
            } else {
                return -1;   /* the take holds no other event kinds */
            }
            j++;
            if (mev_is_eot(e)) break;
        }
        track_len = pos - track_start;
        buf[len_pos] = (uint8_t)(track_len >> 24); buf[len_pos + 1u] = (uint8_t)(track_len >> 16);
        buf[len_pos + 2u] = (uint8_t)(track_len >> 8); buf[len_pos + 3u] = (uint8_t)track_len;
        i = j;
    }
    *out_len = pos;
    return 0;
}
