/* SPDX-License-Identifier: GPL-3.0-only */
/* Maestro: a groovebox that teaches you to read sheet music.
 * The screen is sheet music, the buttons are a tape recorder.
 *
 * There is ONE way to make a note sound: the playhead. There are not
 * two sound paths (live keys vs playback). The sheet has a velocity;
 * the playhead has a timestamp. Every tick, timestamp += velocity.
 * PLAY just toggles the velocity (0 <-> the tempo rate). REC just
 * toggles the write bit (notes placed are written to the take, or
 * not). There is no variable named `play`. PLAY does not change the
 * sound path or the recording; REC does not change the transport or
 * the sound path.
 *
 * A key press places a note at the playhead's timestamp (a ghost
 * note). The playhead sounds it, once. Releasing the key ends the
 * note (sets its duration). If REC is on, the note is written to the
 * take; if REC is off, it is a ghost (temporary, sounds but is not
 * recorded, disappears on release). The sound engine triggers a note
 * ONCE when the playhead timestamp crosses (or lands on) its start,
 * then the note decays on its own envelope. If the playhead sits on
 * a note with velocity 0, the note does NOT keep playing -- it already
 * triggered and decayed; velocity matters only for triggering, not
 * for sustaining. No droning, no re-triggering. The renderer is a
 * pure function of the playhead timestamp.
 *
 * KNOB3/KNOB4 work while the sheet is moving and update the playhead
 * TIMESTAMP (position), not the velocity. When moving and you turn a
 * knob, the playhead jumps to the new timestamp and keeps advancing
 * from there at the same velocity. Scrubbing sounds the notes the
 * playhead passes (forward and backward), or erases them if the
 * delete head is on.
 *
 * Recording always adds (overdub). There is no erase head: the loop
 * never wipes what you played, it just comes around again and you add
 * more on top. A note you replay replaces the same pitch nearby, so
 * you can fix a note without stacking duplicates. The take is a set of
 * layers (one per recording pass); for now they are always all added
 * and all shown, like a drawing with every layer visible. While
 * recording, the black notes you already made SOUND as the playhead
 * passes them, so you hear the take while you add a layer.
 *
 * The playhead is fixed (CURSOR_X); the sheet scrolls under it, and
 * the playhead is the sounding point: whenever it crosses a note,
 * that note sounds -- while time moves (PLAY) or while you scrub
 * the sheet with KNOB4 (coarse, one beat per click, snapping to beat
 * boundaries) or KNOB3 (fine, 4px per click). Scrubbing works from
 * the moment the device starts: in the book it enters the piece
 * stopped, then moves the sheet.
 *
 * EDIT toggles the playhead between a play head (notes sound) and a
 * delete head (notes at the playhead are erased instead, while time
 * moves or while scrubbing; the EDIT LED is bright while deleting).
 * SEQ inserts a jump at the playhead: a symbol in the take, just
 * like a note, that loops the sheet back to the beginning when the
 * playhead hits it during playback (and can be erased by the delete
 * head). With no jump, playback stops at the end of the piece.
 * ARP does nothing (looping is the jump's job now). SAVE marks the
 * end of a free-play take. HOME rewinds to the start; HOME at the
 * start goes back to the book.
 *
 * The PRESETS knob selects the FM piano patch (E.PIANO 4, PIANO 1,
 * E.PIANO 3, PIANO 5, E.PIANO 1); the patch name shows briefly at
 * the bottom, in the style of the BPM readout, and the preset is
 * session-only (it resets to E.PIANO 4 on boot). The keyboard keys
 * sit at a dim backlight so they are visible in the dark. If REC
 * is on and you play a note while stopped, PLAY auto-starts
 * (the velocity becomes the tempo rate) -- the user can veto this.
 *
 * KNOB1 is tempo (40-200 BPM, 2 per click): record slow, crank it up
 * for playback. While the sheet is moving the velocity follows the
 * tempo at once. The BPM shows at the bottom right, always. KNOB2
 * does nothing yet. OCT+ / OCT- transpose the keyboard by an octave
 * per press (-2..+2): what you play and record shifts, the blue score
 * does not. The OCT button lights in the direction you shifted, and
 * the shift shows at the top right. A shifted note still sounds; only
 * notes in the visible staff range (about E4-B5) draw on the sheet.
 * The clef follows the octave: shift down and the staff becomes a
 * bass staff (F clef -- the notes you play are in the bass range);
 * shift back up and it returns to treble.
 *
 * The panel lights follow the transport: PLAY bright while the sheet
 * moves (velocity != 0), REC bright while the write bit is on, SEQ
 * bright while the take contains a jump, EDIT bright while the delete
 * head is on, SAVE bright once a free-play end is marked, HOME bright
 * always; the rest sit at a dim glow. The 27 key LEDs sit at a dim
 * glow (the backlight).
 *
 * Idle: after IDLE_BLACK_MS (2 minutes) with no key, button or
 * encoder input, the screen fills black and nothing draws. Any
 * input wakes it and repaints the current frame in full, and the
 * waking input acts normally too. Only the display blanks -- a
 * playing tape keeps playing; nothing stops, pauses or resets.
 *
 * Rendering is positional: render_window(sc) draws whatever is at scroll
 * position sc, forward or backward. Nothing is ever "reset" on PLAY/REC;
 * they just set the velocity and whether notes are stored.
 *
 * Blue notes are the sheet music (silent, for reading). Black notes are
 * your take. The take is spatial (positions, not times), so it plays back
 * at whatever tempo you set. The piece ends at a final double bar. A
 * score is a program: the end bar is Fine, the jump is a loop to the
 * start, and the playhead is the program counter.
 *
 * v0.22: the take is Standard MIDI File data (midi.h/midi.c). One
 * unified event array holds every sheet's take, as MIDI events with
 * delta times (PPQ 96, which is the firmware's tick unit exactly, so
 * timing maps directly: a note at piece-x px sits at px*96/PXQ
 * ticks). A note is a Note On / Note Off pair; a chord is several
 * Note Ons at the same delta time. A jump is a Marker meta event
 * (0xFF 0x06 "JUMP"); each sheet's block ends with exactly one End
 * of Track (0xFF 0x2F) -- a format-1 file has one track per sheet,
 * and the in-memory array is partitioned the same way. SELECT
 * switches sheets from anywhere -- on the tape as well as in the
 * book -- and never erases: the take, the playhead offset and the
 * tempo are saved per sheet and restored on switch. A sheet's block
 * is found by a linear scan counting End of Track events (the array
 * caps at 2048 events, a switch is a human action). Blue notes stay
 * separate lesson data, now parsed from .mid files at build time
 * (tools/lessons2c.py), never in the take. The take serializes
 * straight to a .mid file (midi_write) and parses straight back
 * (midi_parse): no re-encoding, no lossy translation layer.
 */

#include "fm_piano.c"   /* DX7 FM piano presets (E.PIANO 4 default) -- v0.19 */
#include "midi.c"   /* Standard MIDI File events: the take -- v0.22 */

/* ---- pieces: the book ---- */
#define NPIECES 6u
#define FREE_IDX 6u
#define NBOOK (NPIECES + 1u)
static const lesson_t *pieces[NPIECES];  /* 0..4 lessons, 5 = generated drill */
static lesson_t drill_lesson;
static const char *free_title = "Free Play";

#define PXQ 32u      /* px per quarter note */
#define CURSOR_X 60u   /* the playhead is fixed; the sheet scrolls under it */
#define CLEF_W 80u   /* tick 0 sits here; Bravura clef (38px at x=30) ends at 68 */
#define BAR_PAD 16u  /* barline sits this far left of its tick */
#define FREE_TEMPO 72u
#define MAXEV 256
static uint32_t evx[MAXEV];   /* piece-x of each blue event */
static uint16_t evn;
static uint32_t total_ticks;
static uint32_t bar_ticks;

/* ---- the piece as symbols: a list of (offset, what) the renderer draws ----
 * The clef is a symbol like any other: it has an offset and scrolls with
 * the sheet, instead of being a fixed UI element. */
#define CLEF_X 30u   /* clef offset on the staff */
#define SYM_CLEF 0u
#define SYM_BAR 1u
#define SYM_NOTE 2u
#define SYM_REST 3u
#define SYM_END 4u   /* final double bar: the piece's Fine */
typedef struct { uint16_t x; uint8_t type; uint8_t midi; uint16_t ticks; } psym_t;
#define MAXPSYM (1u + 64u + MAXEV + 1u)
static psym_t psym[MAXPSYM];
static uint16_t psym_n;
static uint32_t piece_end_x;   /* piece-x of the final bar (book pieces) */

static uint32_t bar_x(uint32_t b) {
    return CLEF_W - BAR_PAD + (b * bar_ticks * PXQ) / 96u;
}

static void layout_piece(const lesson_t *L) {
    uint32_t t = 0, b;
    uint16_t i;
    for (i = 0; i < L->event_count && i < MAXEV; i++) {
        evx[i] = CLEF_W + (t * PXQ) / 96u;
        t += L->events[i].ticks;
    }
    evn = i;
    total_ticks = t;
    bar_ticks = (uint32_t)L->beats_per_bar * 96u;
    if (!bar_ticks) bar_ticks = 384u;
    /* symbols: clef, bars, notes/rests, final bar (order doesn't matter;
     * planes decide). The end is the barline at the end of the last bar,
     * so the loop is always a whole number of bars -- never mid-bar.
     * The final barline is the end bar itself, not a separate barline. */
    psym_n = 0;
    psym[psym_n].x = CLEF_X; psym[psym_n].type = SYM_CLEF; psym_n++;
    for (b = 1; b * bar_ticks < total_ticks && psym_n < MAXPSYM; b++) {
        psym[psym_n].x = (uint16_t)bar_x(b); psym[psym_n].type = SYM_BAR; psym_n++;
    }
    for (i = 0; i < evn && psym_n < MAXPSYM; i++) {
        psym[psym_n].x = (uint16_t)evx[i];
        psym[psym_n].type = L->events[i].is_rest ? SYM_REST : SYM_NOTE;
        psym[psym_n].midi = L->events[i].midi;
        psym[psym_n].ticks = L->events[i].ticks;
        psym_n++;
    }
    if (evn) {
        uint32_t end_bar = total_ticks / bar_ticks;
        if (total_ticks % bar_ticks) end_bar++;   /* round up to a barline */
        if (!end_bar) end_bar = 1;
        piece_end_x = bar_x(end_bar);
    } else {
        piece_end_x = CLEF_W;
    }
    if (psym_n < MAXPSYM) {
        psym[psym_n].x = (uint16_t)piece_end_x; psym[psym_n].type = SYM_END; psym_n++;
    }
}
/* ---- transport: a velocity and a timestamp (the unified model) ----
 * The sheet has a velocity; the playhead has a timestamp. Every tick,
 * timestamp += velocity (scaled by the elapsed time). PLAY toggles the
 * velocity (0 <-> the tempo rate). REC toggles the write bit. There is
 * no variable named `play`: whether the tape moves is the velocity,
 * and whether notes are stored is the write bit. The book is a
 * separate mode (in_book): selecting a piece, not on the tape.
 * velocity is tempo*PXQ when moving, 0 when stopped; each tick the
 * timestamp advances by velocity*dt/60000 px (dt in ms), so the
 * playhead moves at tempo*PXQ/60 px/s. */
static uint32_t timestamp;       /* playhead position (piece-x) */
static uint32_t timestamp_frac;  /* fractional accumulator (denominator 60000) */
static uint32_t velocity;        /* playhead rate: 0 = stopped, else tempo*PXQ */
static uint8_t rec_write;        /* REC: notes placed are written to the take */
static uint8_t in_book = 1;      /* selecting a piece in the book */
static uint8_t sel = 0;
static uint8_t is_free = 0;
static uint8_t delete_head = 0;  /* EDIT: the playhead erases instead of playing */
#define PRESET_NOTICE_MS 2000u  /* how long the patch name shows */
static uint32_t preset_notice_until;  /* fm1_ms until the patch name clears */
static uint32_t keys_down;   /* bit k = key k currently down (main loop) */

/* ---- idle: blank the screen when nobody is touching the device ----
 * Any key press, button press or encoder detent is input and resets
 * the timer. After IDLE_BLACK_MS with no input the screen fills
 * black and nothing draws until the next input, which repaints the
 * current frame in full (redraw_now). Only the display blanks: the
 * transport and the audio are untouched, so a playing tape keeps
 * playing behind the black screen. */
#define IDLE_BLACK_MS 120000u  /* idle this long (2 min): the screen goes black */
static uint32_t last_input_ms;   /* fm1_ms of the last input */
static uint8_t screen_blank;     /* 1 = blanked: the draw entry points all no-op */

/* moving = the timestamp advances; recording = notes are stored. */
static uint8_t moving(void) { return velocity != 0; }
static uint8_t recording(void) { return rec_write; }
/* the scroll offset for rendering: the playhead is fixed at CURSOR_X,
 * so the sheet scrolls under it. The renderer is a pure function of
 * the playhead timestamp. */
static uint32_t scroll_now(void) { return timestamp - CURSOR_X; }
#define OCT_MIN (-2)
#define OCT_MAX 2
static int8_t oct_shift = 0;   /* keyboard octave shift, session only */
static uint8_t held_midi[27];  /* MIDI each held key was pressed at (octave may move) */

/* the MIDI a physical key plays, with the octave shift (clamped 0..127) */
static uint8_t key_midi(uint32_t k) {
    int32_t m = (int32_t)(53u + k) + (int32_t)oct_shift * 12;
    if (m < 0) m = 0;
    if (m > 127) m = 127;
    return (uint8_t)m;
}

static int8_t shown_oct = 99;  /* sentinel: force the first octave draw */
static void show_oct(void) {
    if (screen_blank) return;   /* shown_oct stays stale, so it redraws on wake */
    if (oct_shift != shown_oct) {
        shown_oct = oct_shift;
        ui_oct(oct_shift);
    }
}


static uint8_t free_end_set = 0; /* SAVE: a free-play take has a marked end */
static uint32_t free_end_x = 0;  /* piece-x of the marked end (free play) */
static uint32_t tempo = 72u;
static const lesson_t *pl;

#define TEMPO_MIN 40u
#define TEMPO_MAX 200u
#define TEMPO_STEP 2u    /* BPM per KNOB1 click */

static uint32_t last_frame_ms;
static uint32_t shown_bpm = 0; /* last BPM drawn (0 = none yet) */

static settings_t set;  /* speed_idx kept in the blob, unused now */
static uint8_t set_dirty = 0;
/* the playhead position (piece-x) is the timestamp itself. */

/* the tempo on screen, always. Redraws only when it changes
 * (the LCD persists between frames). */
static void show_tempo(void) {
    if (screen_blank) return;
    if (tempo != shown_bpm) {
        shown_bpm = tempo;
        ui_tempo(tempo);
    }
}

/* the hint strip, by (book, moving, recording) (short: the tempo sits
 * at x=180, so a hint must stay under ~21 characters). While a preset
 * name is showing, the hint waits -- the name is the more important
 * thing to read. */
static const char *hint_for_transport(void) {
    if (in_book) return "SELECT flip PLAY start";
    if (!moving() && !recording()) return "PLAY go REC arm SEQ";
    if (moving() && !recording()) return "PLAY stop REC in";
    if (moving() && recording()) return "REC out PLAY stop";
    return "PLAY go REC off";  /* stopped, recording (armed) */
}
static void show_hint(void) {
    if (screen_blank) return;
    if (preset_notice_until && fm1_ms < preset_notice_until) return;
    ui_hint(hint_for_transport());
}
/* the preset knob turned: show the patch name briefly (about 2 s),
 * in the style of the BPM readout, then the hint comes back.
 * The name only ever covers the hint strip (x < 180); the tempo
 * readout at x=180 is never erased, so there is nothing to redraw. */
static void show_preset(void) {
    preset_notice_until = fm1_ms + PRESET_NOTICE_MS;
    if (screen_blank) return;   /* the notice still arms; the name draws on wake */
    ui_preset(piano_preset_name());
}

/* the take: MIDI events, in time order within each sheet's block.
 * One unified array (midi.h): a note is a Note On / Note Off pair, a
 * chord is several Note Ons at the same delta time, a jump is a
 * Marker meta event ("JUMP"), and each sheet's block ends with
 * exactly one End of Track meta event -- the list always holds all
 * NBOOK blocks, even when a sheet's take is empty (rec_init). The
 * playhead's pb_idx stays inside the current sheet's block. */
#define MAXREC 2048u   /* MIDI events (a note is two: ~1000 notes, 16 KiB) */
#define PUNCH_W 8u   /* replaying a pitch replaces it nearby (fix, don't stack) */
#define PUNCH_TICKS (PUNCH_W * 96u / PXQ)   /* the punch window, in ticks */
#define END_TAIL_PX 240u  /* open take: stop a screen-width past the last note */
#define REC_X_MAX 4095u  /* the take clamps at this piece-x (12285 ticks) */
static mev_t rec[MAXREC];
static uint16_t rec_n;
/* per-sheet state, saved on switch and restored on switch back:
 * the playhead offset, the tempo, and (free play only) the marked
 * end. The book selection itself persists in the settings blob. */
static uint32_t sheet_ts[NBOOK];
static uint32_t sheet_tempo[NBOOK];
static uint8_t sheet_end_set[NBOOK];
static uint32_t sheet_end_x[NBOOK];

/* piece-x <-> MIDI ticks. The tick unit is 96 per quarter and a
 * quarter is PXQ px, so the factor is exact (96/32 = 3). */
static uint32_t px_to_ticks(uint32_t px) { return px * 96u / PXQ; }
static uint32_t ticks_to_px(uint32_t t) { return t * PXQ / 96u; }

/* the End of Track index of sheet s (a linear scan counting them). */
static uint16_t sheet_end_idx(uint8_t s) {
    uint16_t i;
    uint8_t seen = 0;
    for (i = 0; i < rec_n; i++)
        if (mev_is_eot(&rec[i])) {
            if (seen == s) return i;
            seen++;
        }
    return rec_n;
}
/* the first event of sheet s's block (the event after the previous
 * sheet's End of Track, or 0 for the first sheet). */
static uint16_t sheet_begin_idx(uint8_t s) {
    return s == 0 ? 0 : (uint16_t)(sheet_end_idx((uint8_t)(s - 1u)) + 1u);
}
/* the take events of sheet s live in [begin, end): the End of Track
 * itself is at end and is not a note. */
static uint16_t sheet_notes_begin(uint8_t s) { return sheet_begin_idx(s); }
static uint16_t sheet_notes_end(uint8_t s) { return sheet_end_idx(s); }

/* the take starts as NBOOK empty blocks: one End of Track per sheet. */
static void rec_init(void) {
    uint8_t s;
    rec_n = 0;
    for (s = 0; s < NBOOK; s++) {
        rec[rec_n].delta = 0; rec[rec_n].status = MIDI_META;
        rec[rec_n].d1 = MIDI_META_EOT; rec[rec_n].d2 = 0;
        rec_n++;
    }
}

/* remove the take event at i, closing the gap (the event after it
 * inherits its delta, so absolute times after i are unchanged). */
static void remove_rec(uint16_t i) { mev_remove(rec, &rec_n, i); }

/* insert one MIDI event at absolute time abs_ticks inside sheet
 * [b, e). ge=1 puts it before events already at that time, ge=0
 * after them: a Note On goes after (an off ending at the same tick
 * must come first, or the new note would end at once), anything
 * else goes before. */
static void insert_ev_abs(uint16_t b, uint16_t e, uint32_t abs_ticks, uint8_t st, uint8_t d1, uint8_t d2, uint8_t ge) {
    uint16_t pos = mev_find_abs(rec, b, e, abs_ticks, ge);
    uint32_t prev = pos > b ? mev_abs(rec, b, (uint16_t)(pos - 1u)) : 0;
    uint32_t delta = abs_ticks > prev ? abs_ticks - prev : 0;
    mev_insert(rec, &rec_n, MAXREC, pos, delta, st, d1, d2);
}

/* the piece-x of the Note Off paired with the Note On at on_idx
 * (the first Note Off for that pitch after it); the default quarter
 * note when there is none (a take note always has one). */
static uint32_t note_off_px(uint16_t b, uint16_t e, uint16_t on_idx, uint8_t pitch, uint32_t on_abs_ticks) {
    uint16_t j;
    for (j = (uint16_t)(on_idx + 1u); j < e; j++)
        if (mev_is_note_off(&rec[j]) && rec[j].d1 == pitch)
            return ticks_to_px(mev_abs(rec, b, j));
    return ticks_to_px(on_abs_ticks + 96u);
}

/* remove a note: its Note On and its paired Note Off (the off first,
 * so the on's index stays valid). */
static void remove_note_pair(uint16_t on_idx) {
    uint16_t e = sheet_notes_end(sel);
    uint16_t j;
    uint8_t pitch = rec[on_idx].d1;
    for (j = (uint16_t)(on_idx + 1u); j < e; j++)
        if (mev_is_note_off(&rec[j]) && rec[j].d1 == pitch) { remove_rec(j); break; }
    remove_rec(on_idx);
}

/* ghost notes: a key held while NOT recording shows its note at the
 * playhead until released (so you can see which note to start with).
 * One per key, indexed by the key. `sounded` marks that the playhead
 * has already triggered it, so it sounds exactly once. */
static struct { uint8_t midi; uint16_t x; uint8_t active; uint8_t sounded; } ghost[27];

/* scrub notes: notes the playhead sounds while scrubbing are turned
 * off after SCRUB_MS, so no voice is left ringing. */
#define MAXSCRUB 8
#define SCRUB_MS 180u
static struct { uint8_t midi; uint32_t off_ms; } scrub_notes[MAXSCRUB];
static uint8_t scrub_n;

/* playback: next note to start, and notes currently sounding */
static uint16_t pb_idx;
#define MAXSOUND 8
static struct { uint8_t midi; uint32_t end_x; } sounding[MAXSOUND];
static uint8_t n_sounding;

/* record a note at the playhead: insert into the take, in x order.
 * A note already recorded for this pitch near the playhead is
 * removed, so re-playing a note does not stack two on top of each
 * other. The note is written (recorded), but the playhead sounds it
 * via the ghost the key press placed, not via the take: pb_idx is
 * recomputed to the first note past the playhead, so the tick does
 * not re-trigger the note just placed. That is the double-play fix,
 * by design -- one sound path, one trigger. (The caller flushes any
 * pending notes at the playhead first, so none are skipped by the
 * recompute.) */
static void rec_note_on(uint8_t midi) {
    /* the note starts at the playhead, in ticks. */
    uint32_t t = px_to_ticks(timestamp > REC_X_MAX ? REC_X_MAX : timestamp);
    uint16_t b = sheet_notes_begin(sel), e = sheet_notes_end(sel), i;
    for (i = b; i < e; i++) {
        if (mev_is_note_on(&rec[i]) && rec[i].d1 == midi) {
            uint32_t ax = mev_abs(rec, b, i);
            if (ax + PUNCH_TICKS >= t && ax <= t + PUNCH_TICKS) {
                remove_note_pair(i);   /* replay replaces, never stacks */
                break;
            }
        }
    }
    if (rec_n + 2u > MAXREC) return;
    /* a note is a pair: the off goes in first (at a default quarter
     * note, moved by the note-off), then the on at the playhead. */
    insert_ev_abs(b, sheet_notes_end(sel), t + 96u, MIDI_NOTE_OFF, midi, 0, 1);
    insert_ev_abs(b, sheet_notes_end(sel), t, MIDI_NOTE_ON, midi, 100, 0);
    /* the playhead has passed the note just placed (it sounds via the
     * ghost), so the tick must not re-trigger it. */
    pb_idx = mev_find_abs(rec, sheet_notes_begin(sel), sheet_notes_end(sel), t, 0);
}
static void rec_note_off(uint8_t midi) {
    /* close the note: move its Note Off to the playhead (clamped to
     * a 24..384 tick duration, as before). The most recent Note On
     * for this pitch is the one the held key placed. */
    uint32_t t = px_to_ticks(timestamp > REC_X_MAX ? REC_X_MAX : timestamp);
    uint16_t b = sheet_notes_begin(sel), e = sheet_notes_end(sel), i, j;
    for (i = e; i-- > b; ) {
        if (mev_is_note_on(&rec[i]) && rec[i].d1 == midi) {
            uint32_t on_abs = mev_abs(rec, b, i);
            uint32_t dur = t > on_abs ? t - on_abs : 0;
            if (dur < 24u) dur = 24u;
            if (dur > 384u) dur = 384u;
            for (j = (uint16_t)(i + 1u); j < e; j++)
                if (mev_is_note_off(&rec[j]) && rec[j].d1 == midi) { remove_rec(j); break; }
            insert_ev_abs(b, sheet_notes_end(sel), on_abs + dur, MIDI_NOTE_OFF, midi, 0, 1);
            return;
        }
    }
}

/* does the current sheet's take contain a jump? (the SEQ LED follows this) */
static uint8_t has_jump(void) {
    uint16_t i;
    for (i = sheet_notes_begin(sel); i < sheet_notes_end(sel); i++)
        if (mev_is_jump(&rec[i])) return 1;
    return 0;
}

/* ---- the playhead sounds notes: the single sound path ----
 * There is ONE way to make a note sound: the playhead. Keys do not
 * call piano_on; they place a note at the playhead's timestamp, and
 * the playhead sounds it. The tick sounds notes as the timestamp
 * advances; a knob sounds notes as the timestamp jumps; a key press
 * sounds the note just placed at the timestamp. All three go through
 * playhead_sound, and the only piano_on call is in trigger_note. */

/* is this MIDI pitch currently held live by the user? */
static uint8_t midi_held(uint8_t midi) {
    uint32_t k;
    for (k = 0; k < 27u; k++)
        if ((keys_down >> k & 1u) && held_midi[k] == midi) return 1;
    return 0;
}

/* Sound modes: how a note triggered by the playhead is tracked so it
 * turns off. MODE_PLAY: a playback note, off at its recorded end
 * (sounding[]). MODE_SCRUB: a scrubbed note, off after 180 ms
 * (scrub_notes[]). MODE_LIVE: a note a key just placed, off when the
 * key is released (not tracked here). */
#define MODE_PLAY 0u
#define MODE_SCRUB 1u
#define MODE_LIVE 2u

/* Sound a note once. This is the only place piano_on is called.
 * `end_x` is the piece-x at which a playback note turns off (ignored
 * for MODE_SCRUB/MODE_LIVE). A scrubbed note already sounding is
 * skipped (its 180 ms not yet up), and a note held live is skipped
 * when scrubbing (it is already sounding from the key press). */
static void trigger_note(uint8_t midi, uint32_t end_x, uint8_t mode) {
    uint8_t i;
    if (mode == MODE_SCRUB) {
        if (midi_held(midi)) return;  /* already sounding live */
        for (i = 0; i < scrub_n; i++)
            if (scrub_notes[i].midi == midi) { scrub_notes[i].off_ms = fm1_ms + SCRUB_MS; return; }
        if (scrub_n >= MAXSCRUB) return;
    }
    piano_on(midi, 100);   /* the only piano_on call in the firmware */
    if (mode == MODE_SCRUB) {
        scrub_notes[scrub_n].midi = midi;
        scrub_notes[scrub_n].off_ms = fm1_ms + SCRUB_MS;
        scrub_n++;
    } else if (mode == MODE_PLAY) {
        /* a playback note, off at its recorded end */
        if (n_sounding < MAXSOUND) {
            sounding[n_sounding].midi = midi;
            sounding[n_sounding].end_x = end_x;
            n_sounding++;
        }
    }
    /* MODE_LIVE: not tracked here; the key release turns it off. Its
     * duration is not known yet (the key is still held), and adding
     * it with a default duration would cut it off early. */
}

/* The playhead sounds notes. `from` and `to` are playhead timestamps.
 * Forward (to > from): sound notes with start in (from, to].
 * Backward (to < from): sound notes with start in [to, from).
 * Still (to == from): sound notes at exactly `to` (the playhead
 * landed on one, e.g. a key just placed it there). Each note sounds
 * once per crossing: pb_idx tracks the take (it only moves forward,
 * and a jump resets it), and a ghost's `sounded` flag tracks it.
 * Returns 1 if the playhead hit a jump (MODE_PLAY only) and the tape
 * should loop; 0 otherwise. A jump is erased by the delete head
 * (any mode) and never sounds. */
static uint8_t playhead_sound(uint32_t from, uint32_t to, uint8_t mode) {
    uint32_t i;
    /* Ghost notes at the playhead that have not sounded yet. */
    for (i = 0; i < 27u; i++) {
        if (ghost[i].active && !ghost[i].sounded && ghost[i].x == to) {
            ghost[i].sounded = 1;
            trigger_note(ghost[i].midi, 0, MODE_LIVE);
        }
    }
    if (mode == MODE_SCRUB || to < from) {
        /* A jump (a knob moved the timestamp): scan the take for
         * notes in the jumped interval. Forward sounds (from, to],
         * backward sounds [to, from), so a note exactly on the
         * boundary sounds once per crossing. */
        uint32_t lo = (to > from) ? px_to_ticks(from) : px_to_ticks(to);
        uint32_t hi = (to > from) ? px_to_ticks(to) : px_to_ticks(from);
        uint8_t forward = (to > from);
        uint16_t end = sheet_notes_end(sel);
        uint32_t t_prev = 0;   /* absolute time of the event before i */
        for (i = sheet_notes_begin(sel); i < end; ) {
            uint32_t t = t_prev + rec[i].delta;   /* absolute time of rec[i] */
            uint8_t in_range;
            if (forward) in_range = (t > lo && t <= hi);
            else in_range = (t >= lo && t < hi);
            if (!in_range) { t_prev = t; i++; continue; }
            if (mev_is_jump(&rec[i])) {
                if (delete_head) { remove_rec(i); end--; continue; }
                t_prev = t; i++;
            } else if (mev_is_note_on(&rec[i])) {
                if (delete_head) { remove_note_pair(i); end = sheet_notes_end(sel); continue; }
                trigger_note(rec[i].d1, 0, MODE_SCRUB);
                t_prev = t; i++;
            } else { t_prev = t; i++; }   /* a Note Off: nothing to sound */
        }
        /* After a jump, pb_idx restarts past the notes just sounded,
         * so the tick does not re-trigger them. */
        pb_idx = mev_find_abs(rec, sheet_notes_begin(sel), sheet_notes_end(sel), px_to_ticks(to), 0);
        return 0;
    }
    /* Forward or still: advance pb_idx through notes at or before `to`.
     * (Not for MODE_LIVE: a key press sounds only its ghost. The
     * tick sounds recorded notes, with their duration tracked; a key
     * press must not steal one and sound it untracked.) */
    if (mode == MODE_LIVE) return 0;
    /* pb_idx stays inside the current sheet's block: notes of other
     * sheets never sound here. `t` is the absolute time of the event
     * at pb_idx; it advances by each event's delta, and is unchanged
     * when the delete head removes an event (the next event inherits
     * its delta, so its absolute time is the same). */
    {
        uint16_t b = sheet_notes_begin(sel), end = sheet_notes_end(sel);
        uint32_t to_t = px_to_ticks(to);
        uint32_t t = (pb_idx >= b && pb_idx < end) ? mev_abs(rec, b, pb_idx) : 0;
        while (pb_idx < end && t <= to_t) {
            if (mev_is_jump(&rec[pb_idx])) {
                if (delete_head) {
                    remove_rec(pb_idx); end--;   /* next event shifts in; re-examine it */
                } else if (mode == MODE_PLAY) {
                    return 1;  /* the jump loops; the caller wraps the tape */
                } else {
                    pb_idx++;  /* a key press does not loop on a jump */
                    if (pb_idx < end) t += rec[pb_idx].delta;
                }
            } else if (mev_is_note_on(&rec[pb_idx])) {
                if (delete_head) {
                    remove_note_pair(pb_idx); end = sheet_notes_end(sel);
                } else {
                    uint8_t m = rec[pb_idx].d1;
                    trigger_note(m, note_off_px(b, end, pb_idx, m, t), mode);
                    pb_idx++;
                    if (pb_idx < end) t += rec[pb_idx].delta;
                }
            } else {
                /* a Note Off: its note turns off by end_x in tick();
                 * a lone off here (its on is behind the playhead) is
                 * just skipped, never deleted on its own. */
                pb_idx++;
                if (pb_idx < end) t += rec[pb_idx].delta;
            }
        }
    }
    return 0;
}

/* ---- rendering: draw the window at scroll position sc ----
 * Every symbol on the staff has an offset; the renderer draws the ones
 * in [sc, sc+240). The clef is a symbol, so it scrolls with the sheet
 * (and slides off the left edge via glyph clipping) instead of staying put. */
static void render_window(uint32_t sc) {
    uint32_t i;
    if (screen_blank) return;
    ui_clear();
    ui_plane_black();
    ui_staff_cols(0u, 240u);
    /* the symbol list is the source of truth: in free play it holds only
     * the clef, in a book piece the clef, bars, notes and final bar. */
    {
        for (i = 0; i < psym_n; i++) {
            int32_t sx = (int32_t)psym[i].x - (int32_t)sc;
            if (sx >= 240) continue;
            switch (psym[i].type) {
            case SYM_CLEF:
                ui_plane_black();
                if (sx + (int32_t)FCLEF_W > 0) ui_clef(sx);  /* FCLEF is the wider clef */
                break;
            case SYM_BAR:
                if (sx < 0) break;
                ui_plane_black();
                ui_bar((uint32_t)sx);
                break;
            case SYM_NOTE:
                if (sx < 0) break;
                ui_plane_blue();
                ui_note((uint32_t)sx, psym[i].midi, psym[i].ticks);
                break;
            case SYM_REST:
                if (sx < 0) break;
                ui_plane_blue();
                ui_rest((uint32_t)sx, psym[i].ticks);
                break;
            case SYM_END:
                if (sx < 0) break;
                ui_plane_black();
                ui_endbar((uint32_t)sx);
                break;
            }
        }
    }
    ui_plane_black();
    if (is_free) {
        /* free play has bar lines built in: they scroll by like in a
         * book piece, a bar every 128px (4/4, bar_ticks = 384).
         * Start at the first bar at or after sc, not at bar 1: the
         * tape can be far along, and re-scanning from the start
         * every frame costs O(scroll). */
        uint32_t step = (bar_ticks * PXQ) / 96u;   /* px per bar */
        uint32_t b;
        if (sc <= CLEF_W - BAR_PAD) b = 1;
        else b = (sc - (CLEF_W - BAR_PAD) + step - 1u) / step;
        for (; ; b++) {
            uint32_t bx = bar_x(b);
            if (bx >= sc + 240u) break;
            if (bx >= sc) ui_bar(bx - sc);
        }
    }
    if (is_free && free_end_set) {
        /* the marked end of a free-play take */
        int32_t ex = (int32_t)free_end_x - (int32_t)sc;
        if (ex >= 0 && ex < 240) ui_endbar((uint32_t)ex);
    }
    /* the current sheet's take only: other sheets' notes never draw. */
    {
        uint16_t b = sheet_notes_begin(sel), e = sheet_notes_end(sel);
        uint32_t t_prev = 0;   /* absolute time of the event before i */
        for (i = b; i < e; i++) {
            uint32_t t = t_prev + rec[i].delta;   /* absolute time, ticks */
            uint32_t px, sx;
            t_prev = t;
            px = ticks_to_px(t);
            if (px < sc || px >= sc + 240u) continue;
            sx = px - sc;
            if (mev_is_jump(&rec[i])) {
                /* the jump: a double bar, the loop point of the take */
                ui_bar(sx);
                if (sx + 5u < 240u) ui_bar(sx + 5u);
            } else if (mev_is_note_on(&rec[i])) {
                uint32_t off_px = note_off_px(b, e, (uint16_t)i, rec[i].d1, t);
                uint32_t dur_ticks = (off_px > px ? off_px - px : 0) * 96u / PXQ;
                ui_note(sx, rec[i].d1, dur_ticks ? dur_ticks : 96u);
            }
        }
    }
    for (i = 0; i < 27u; i++) {
        /* ghost notes: keys held while not recording, at the playhead */
        uint32_t sx;
        if (!ghost[i].active) continue;
        if (ghost[i].x < sc || ghost[i].x >= sc + 240u) continue;
        sx = ghost[i].x - sc;
        ui_note(sx, ghost[i].midi, 96u);
    }
    ui_blit();
    ui_cursor_mark(CURSOR_X);
}

static void book_title(char *dst) {
    const char *s = (sel == FREE_IDX) ? free_title : pieces[sel]->title;
    uint32_t i = 0;
    while (*s && i < 43u) dst[i++] = *s++;
    dst[i] = 0;
}

static void render_book(void) {
    char title[44];
    if (screen_blank) return;
    book_title(title);
    ui_paper();
    render_window(0u);
    ui_title(title);
    show_hint();
    shown_bpm = 0;
    show_tempo();
    shown_oct = 99;
    show_oct();
}

/* repaint the whole current screen: the paper, the window, and every
 * overlay (title, tempo, octave, hint). The wake from an idle blank
 * needs it -- the blank erased the whole panel, and the overlays
 * otherwise only redraw when they change. In the book, render_book
 * already repaints everything. */
static void redraw_now(void) {
    char title[44];
    if (in_book) { render_book(); return; }
    ui_paper();
    render_window(scroll_now());
    book_title(title);
    ui_title(title);
    shown_oct = 99; show_oct();   /* force the overlays back on */
    shown_bpm = 0; show_tempo();
    if (preset_notice_until && fm1_ms < preset_notice_until) show_preset();
    else show_hint();
}

/* blank the screen: fill it black. A fill, not the panel's
 * display-off command -- the host harness emulates the fill, so the
 * blank is verifiable on host. On hardware the backlight stays on;
 * only the pixels go black. */
static void blank_screen(void) {
    screen_blank = 1;
    lcd_fill(0, 0, 240, 240, 0);
}

/* The clef follows the octave: shift down and the staff becomes a bass
 * staff (the notes you play are in the bass range), shift back up and
 * it returns to treble. Re-renders so the staff and notes move now. */
static void apply_octave(void) {
    ui_set_clef(oct_shift < 0);
    show_oct();
    if (in_book) render_book();
    else render_window(scroll_now());
}

static void setup_piece(void) {
    is_free = (sel == FREE_IDX);
    if (is_free) {
        evn = 0;
        total_ticks = 0;
        bar_ticks = 384u;   /* 4/4: a bar is 128px, for the built-in bar lines */
        /* free play has no lesson, but the staff still gets a clef --
         * it is the symbol that shows which clef the staff is in. */
        psym_n = 0;
        psym[psym_n].x = CLEF_X; psym[psym_n].type = SYM_CLEF; psym_n++;
    } else {
        pl = pieces[sel];
        layout_piece(pl);
    }
}
/* picking a piece (fresh): lay it out and reset the tempo to the piece's.
 * PLAY/REC/HOME use setup_piece() alone, so a tempo you dialed in with
 * KNOB1 survives starting the tape. */
static void select_piece(void) {
    setup_piece();
    tempo = is_free ? FREE_TEMPO : pl->tempo;
}

/* ---- transport ---- */
static void silence_playback(void) {
    uint8_t i;
    for (i = 0; i < n_sounding; i++) piano_off(sounding[i].midi);
    n_sounding = 0;
}

/* the panel lights follow the state: PLAY bright while the tape moves,
 * REC bright while armed or recording, SEQ bright while the take
 * contains a jump, EDIT bright while the delete head is on, SAVE
 * bright once a free-play end is marked, HOME bright always; the
 * rest sit at a dim glow. The 27 key LEDs sit at a dim glow --
 * the backlight, so the keys are visible in the dark. */
static void leds_update(void) {
#if BOARD_SMK37
    uint32_t nbtn = 18u;
#else
    uint32_t nbtn = 14u;
#endif
    uint32_t b;
    uint8_t jump = has_jump();   /* once: the take doesn't change mid-loop */
    for (b = 0; b < nbtn; b++) {
        uint8_t bright = 0;
        if (b == (uint32_t)BTN_HOME) bright = 1;
        else if (b == (uint32_t)BTN_PLAY) bright = moving();
        else if (b == (uint32_t)BTN_REC) bright = recording();
        else if (b == (uint32_t)BTN_SEQ) bright = jump;
        else if (b == (uint32_t)BTN_EDIT) bright = delete_head;
        else if (b == (uint32_t)BTN_OCT_UP) bright = (oct_shift > 0);
        else if (b == (uint32_t)BTN_OCT_DOWN) bright = (oct_shift < 0);
        else if (b == (uint32_t)BTN_SAVE) bright = (is_free && free_end_set);
        if (bright) { fm1_led_key(b, 1); fm1_led_key_glow(b, 0); }
        else { fm1_led_key(b, 0); fm1_led_key_glow(b, 1); }
    }
#if !BOARD_SMK37
    /* the key backlight (FM-1 only: the SMK-37 keys have no LEDs). */
    for (b = 0; b < 27u; b++) {
        fm1_led_key(KEY_LED(b), 0);
        fm1_led_key_glow(KEY_LED(b), 1);
    }
#endif
}

/* ---- transport: set the velocity and the write bit ----
 * PLAY toggles the velocity (0 <-> the tempo rate); REC toggles the
 * write bit. Neither touches the sound path: the playhead sounds
 * notes whatever the velocity and the write bit are. When the
 * velocity becomes non-zero the timestamp starts advancing from
 * where it is; when it becomes zero the timestamp stays put. */
static void set_velocity(uint32_t v) {
    if (v && !velocity) {
        /* starting to move: pb_idx restarts at the playhead, and
         * notes still sounding are silenced (a fresh start). */
        pb_idx = mev_find_abs(rec, sheet_notes_begin(sel), sheet_notes_end(sel), px_to_ticks(timestamp), 1);
        n_sounding = 0;
        last_frame_ms = fm1_ms;
    } else if (!v && velocity) {
        silence_playback();
    }
    velocity = v;
    show_hint();
}
static void set_rec_write(uint8_t w) {
    rec_write = w;
    show_hint();
}
/* PLAY: if stopped, move at the tempo rate; if moving, stop. */
static void toggle_play(void) {
    if (velocity) set_velocity(0);
    else set_velocity((uint32_t)tempo * PXQ);
}
/* REC: toggle whether notes placed are written to the take. */
static void toggle_rec(void) {
    set_rec_write(!rec_write);
}

/* loop back to the start (a jump is a loop, not a stop).
 * Nothing is erased: the take survives the wrap and you keep adding.
 * pb_idx skips what is behind the playhead -- and a jump sitting
 * exactly at the playhead, so it cannot re-trigger at once. */
static void loop_wrap(void) {
    silence_playback();
    timestamp = CURSOR_X; timestamp_frac = 0;
    pb_idx = mev_find_abs(rec, sheet_notes_begin(sel), sheet_notes_end(sel), px_to_ticks(timestamp), 1);
    while (pb_idx < sheet_notes_end(sel) && mev_is_jump(&rec[pb_idx]) &&
           mev_abs(rec, sheet_notes_begin(sel), pb_idx) == px_to_ticks(timestamp)) pb_idx++;
}

/* the piece-x of the current sheet's last note (0 when the take is
 * empty): the open take stops a screen-width past it. */
static uint32_t last_take_px(uint8_t s) {
    uint16_t b = sheet_notes_begin(s), i = sheet_notes_end(s);
    while (i > b && !mev_is_note_on(&rec[i - 1u])) i--;
    if (i == b) return 0;
    return ticks_to_px(mev_abs(rec, b, (uint16_t)(i - 1u)));
}

/* one tick while the sheet moves: timestamp += velocity, then the
 * playhead sounds what it crossed. In REC the black notes sound too
 * (overdub monitoring), so you hear the take while you add a layer.
 * A delete head erases instead of sounding, a jump loops to the
 * beginning. The tick also turns off playback notes whose end the
 * playhead passed, and stops the tape at the end (no jump) once the
 * playhead passes the end bar and no note is still sounding. */
static void tick(void) {
    uint32_t now = fm1_ms;
    uint32_t dt, num, den, px, i, j;
    uint32_t old_ts = timestamp;
    dt = now - last_frame_ms;
    last_frame_ms = now;
    /* advance the timestamp by velocity*dt/60000 px (32-bit safe:
     * dt<1000, tempo<256, PXQ=32). */
    num = dt * velocity;
    den = 60000u;
    timestamp += num / den;
    timestamp_frac += num % den;
    if (timestamp_frac >= den) { timestamp += timestamp_frac / den; timestamp_frac %= den; }
    px = timestamp;
    /* the playhead is the sounding point. */
    if (playhead_sound(old_ts, px, MODE_PLAY)) {
        loop_wrap();          /* the jump loops to the beginning */
        render_window(scroll_now());
        show_tempo();
        return;
    }
    for (i = 0; i < n_sounding; ) {
        if (sounding[i].end_x <= px) {
            piano_off(sounding[i].midi);
            for (j = i; j + 1u < n_sounding; j++) sounding[j] = sounding[j + 1u];
            n_sounding--;
        } else i++;
    }
    /* the end: the final bar (book) or the marked end (free). A jump
     * loops before this is reached; with no jump, playback stops at
     * the end. It stops as soon as the playhead passes the end bar
     * and no note is still sounding. A note's length is bounded
     * (<= 128px: at most 384 ticks), so a sounding note always
     * finishes on its own -- there is no stuck note to wait past,
     * and no backstop is needed. (The old +240 backstop cut off
     * notes recorded past the end bar; without it they play out.) */
    {
        uint32_t end = 0;
        uint8_t has_end = 0;
        if (!is_free && evn) { end = piece_end_x; has_end = 1; }
        else if (is_free && free_end_set) { end = free_end_x; has_end = 1; }
        if (has_end && px > end && !n_sounding) {
            set_velocity(0);
            render_window(scroll_now());
            return;
        }
        if (!has_end && is_free && !keys_down &&
            last_take_px(sel) > 0 &&   /* an empty sheet has no take to stop past */
            pb_idx >= sheet_notes_end(sel) && !n_sounding &&
            px > last_take_px(sel) + END_TAIL_PX) {
            /* open take, no marked end: stop past the last note.
             * This fires while recording too (not just PLAY): the old
             * ST_PLAY gate meant a free-play take never stopped. It
             * waits until no key is held, so it cannot cut off a
             * note you are still playing. (The old gate required
             * the tape to be in PLAY; a free-play take recorded
             * stopped, or in REC, never stopped.) */
            set_velocity(0);
            render_window(scroll_now());
            return;
        }
    }
    render_window(scroll_now());
    show_tempo();
}

/* ---- scrubbing: a knob moves the timestamp, the playhead sounds it ---- */

/* turn off scrub notes whose time is up (called from the main loop).
 * A note held live by then is left alone -- the key release turns
 * it off. */
static void scrub_notes_off(void) {
    uint8_t i, j;
    for (i = 0; i < scrub_n; ) {
        if (fm1_ms >= scrub_notes[i].off_ms) {
            if (!midi_held(scrub_notes[i].midi)) piano_off(scrub_notes[i].midi);
            for (j = i; j + 1u < scrub_n; j++) scrub_notes[j] = scrub_notes[j + 1u];
            scrub_n--;
        } else i++;
    }
}

/* move the playhead to new_ts (scrubbing), sounding or erasing the
 * notes it passes, then re-render. The timestamp is set directly
 * (the velocity is untouched), so a knob works while the sheet is
 * moving: the playhead jumps to the new timestamp and keeps
 * advancing from there at the same velocity. */
static void scrub_to(uint32_t new_ts) {
    uint32_t old_ts = timestamp;
    if (new_ts < CURSOR_X) new_ts = CURSOR_X;  /* the playhead never goes before the start */
    timestamp = new_ts;
    timestamp_frac = 0;
    playhead_sound(old_ts, new_ts, MODE_SCRUB);
    render_window(scroll_now());
}

/* KNOB4: coarse scroll, one beat per click, snapping the playhead to
 * a beat boundary. Beats sit at CLEF_W + n*PXQ (n = 0, 1, 2, ...).
 * dir is the number of clicks (the encoder may deliver several at
 * once if the knob is turned fast), so it moves |dir| beats. */
static void knob_coarse_scroll(int32_t dir) {
    int32_t px = (int32_t)timestamp;
    int32_t base, target_beat, target_px;
    if (!dir) return;
    if (dir > 0) {
        /* the beat at or below the playhead, then forward dir beats.
         * Before beat 0 the base is -1, so +1 lands on beat 0. */
        base = (px < (int32_t)CLEF_W) ? -1 : (px - (int32_t)CLEF_W) / (int32_t)PXQ;
        target_beat = base + dir;
    } else {
        /* the beat strictly below the playhead, then back |dir| beats
         * from there (a click back from between beats snaps to the
         * beat below; a click back from on a beat goes to the one
         * before it). At or before beat 0 the base is -1: the start. */
        base = (px <= (int32_t)CLEF_W) ? -1 : (px - (int32_t)CLEF_W - 1) / (int32_t)PXQ;
        target_beat = base + dir + 1;
    }
    if (target_beat < 0) {
        scrub_to(CURSOR_X);   /* back to the start */
        return;
    }
    target_px = (int32_t)CLEF_W + target_beat * (int32_t)PXQ;
    scrub_to((uint32_t)target_px);
}

/* enter the piece stopped (from the book), so a knob can scrub it. */
static void enter_piece_stopped(void) {
    setup_piece();
    timestamp = CURSOR_X; timestamp_frac = 0;
    sheet_ts[sel] = CURSOR_X;   /* entering a piece starts at the beginning */
    velocity = 0;
    in_book = 0;
    render_window(0u);
    show_hint();
}

/* SEQ: insert a jump at the playhead (or remove it, if one is already
 * there). The jump is a symbol in the take, like a note. */
static void seq_jump(void) {
    uint32_t t = px_to_ticks(timestamp > REC_X_MAX ? REC_X_MAX : timestamp);
    uint16_t b = sheet_notes_begin(sel), e = sheet_notes_end(sel), i;
    for (i = b; i < e; i++) {
        if (mev_is_jump(&rec[i])) {
            uint32_t ax = mev_abs(rec, b, i);
            if (ax + PUNCH_TICKS >= t && ax <= t + PUNCH_TICKS) {
                remove_rec(i);
                render_window(scroll_now());
                return;
            }
        }
    }
    if (rec_n >= MAXREC) return;
    insert_ev_abs(b, e, t, MIDI_META, MIDI_META_MARKER, MIDI_MARKER_JUMP_LEN, 1);
    render_window(scroll_now());
}

/* A key press places a note at the playhead's timestamp (a ghost
 * note). The playhead sounds it, once. If REC is on, the note is
 * written to the take (recorded); if REC is off, it is a ghost
 * (temporary, sounds but is not recorded, disappears on release).
 * If the tape is stopped and REC is on, playing a note auto-starts
 * it (REC + note = PLAY) -- the user can veto this. Before the note
 * is placed, any notes already at the playhead that the tick has not
 * reached yet are sounded (so the recompute in rec_note_on, which
 * moves pb_idx past the playhead, does not skip them). */
static void on_key(uint8_t midi, uint32_t k) {
    if (recording()) {
        if (!moving()) toggle_play();   /* auto-start */
        playhead_sound(timestamp, timestamp, MODE_PLAY);
        rec_note_on(midi);
    }
    ghost[k].midi = midi;
    ghost[k].x = (uint16_t)timestamp;
    ghost[k].active = 1;
    ghost[k].sounded = 0;
    playhead_sound(timestamp, timestamp, MODE_LIVE);
    render_window(scroll_now());
}
static void on_key_up(uint8_t midi, uint32_t k) {
    uint8_t had_ghost = ghost[k].active;
    if (recording()) rec_note_off(midi);
    ghost[k].active = 0;
    ghost[k].sounded = 0;
    if (had_ghost) render_window(scroll_now());
}

/* ---- per-sheet switching (SELECT from anywhere) ----
 * Switching sheets never erases: the current sheet's playhead
 * offset, tempo and marked end are saved, the new sheet's are
 * restored. On the tape the timestamp jumps to the new sheet's saved
 * offset and the velocity follows its tempo; in the book the tape is
 * not running, so only the layout and tempo change (the tape still
 * starts at the beginning when PLAY enters it). */
static void save_sheet_state(void) {
    sheet_ts[sel] = timestamp;
    sheet_tempo[sel] = tempo;
    sheet_end_set[sel] = free_end_set;
    sheet_end_x[sel] = free_end_x;
}
static void restore_sheet_state(void) {
    tempo = sheet_tempo[sel];
    free_end_set = sheet_end_set[sel];
    free_end_x = sheet_end_x[sel];
    if (!in_book) {
        timestamp = sheet_ts[sel];
        timestamp_frac = 0;
        if (moving()) velocity = tempo * PXQ;
        silence_playback();
        pb_idx = mev_find_abs(rec, sheet_notes_begin(sel), sheet_notes_end(sel), px_to_ticks(timestamp), 1);
    }
}

/* ---- main ---- */
#define SAVE_THROTTLE_MS 10000u  /* at most one settings save per 10 s */
#define FRAME_MS 33u             /* the tape advances ~30 times a second */
static void save_if_dirty(void) {
    if (set_dirty) { mset_save(&set); set_dirty = 0; }
}

static void app_main(void) {
    uint32_t prev_keys = 0, last_save_ms = 0, k;
    uint32_t i;
    for (i = 0; i < 5u && i < (uint32_t)all_lessons_count; i++) pieces[i] = all_lessons[i];
    {
        drill_params_t dp;
        dp.position_midi = 60; dp.range = 7; dp.tempo = 96;
        dp.bars = 4; dp.beats_per_bar = 4;
        dp.rhythm_mask = RHY_Q | RHY_H | RHY_E;
        dp.allow_rests = 1; dp.seed = 7;
        { const char *t = "Drill";
          for (i = 0; t[i]; i++) drill_lesson.title[i] = t[i];
          drill_lesson.title[i] = 0; }
        drill_lesson.level = 2; drill_lesson.tempo = 96;
        drill_lesson.event_count = drill_generate(&dp, drill_lesson.events, LESSON_MAX_EVENTS);
        pieces[5] = &drill_lesson;
    }
    rec_init();   /* one unified list: NBOOK empty End-of-Track blocks */
    for (i = 0; i < NBOOK; i++) {
        sheet_ts[i] = CURSOR_X;
        /* a slot with no lesson (a short book) falls back to the
         * free-play tempo; the book never has a hole in practice. */
        sheet_tempo[i] = (i == FREE_IDX || !pieces[i]) ? FREE_TEMPO : pieces[i]->tempo;
        sheet_end_set[i] = 0;
        sheet_end_x[i] = 0;
    }
    mset_load(&set);
    sel = set.sel;   /* the saved book selection (clamped by the load) */
    if (sel >= NBOOK || (sel < NPIECES && !pieces[sel])) sel = 0;
    select_piece();

    leds_update();  /* the panel lights follow the state */
    render_book();
    last_input_ms = fm1_ms;   /* the idle timer runs from power-on */

    for (;;) {
        uint32_t keys, b;
        int32_t e;
        uint8_t had_input = 0;   /* any input this pass (idle timer) */
        platform_poll();

        e = fm1_enc_take(ENC_SELECT);
        if (e) had_input = 1;
        if (e) {
            /* SELECT switches sheets from anywhere -- in the book or
             * on the tape -- and never erases: the take, the playhead
             * offset and the tempo are per sheet. */
            int32_t s = (int32_t)sel + e;
            int32_t dir = (e > 0) ? 1 : -1;
            /* a slot with no lesson is skipped (a short book); the
             * drill always fills a slot, so this terminates. */
            for (;;) {
                while (s < 0) s += NBOOK;
                s %= NBOOK;
                if (s >= (int32_t)NPIECES || pieces[s]) break;
                s += dir;
            }
            if ((uint8_t)s != sel) {
                if (!in_book) save_sheet_state();
                else { sheet_tempo[sel] = tempo; sheet_end_set[sel] = free_end_set; sheet_end_x[sel] = free_end_x; }
                sel = (uint8_t)(s % NBOOK);
                setup_piece();
                restore_sheet_state();
                if (in_book) render_book();
                else render_window(scroll_now());
                set.sel = sel;   /* the selection persists across reboots */
                set_dirty = 1;
            }
        }
        e = fm1_enc_take(ENC_KNOB1);
        if (e) had_input = 1;
        if (e) {
            /* tempo: record slow, crank it up for playback. While the
             * sheet is moving the velocity follows the tempo at once. */
            int32_t t = (int32_t)tempo + e * (int32_t)TEMPO_STEP;
            if (t < (int32_t)TEMPO_MIN) t = (int32_t)TEMPO_MIN;
            if (t > (int32_t)TEMPO_MAX) t = (int32_t)TEMPO_MAX;
            if (t != (int32_t)tempo) {
                tempo = (uint32_t)t;
                sheet_tempo[sel] = tempo;   /* the tempo is per sheet */
                if (moving()) velocity = (uint32_t)tempo * PXQ;
                show_tempo();
            }
        }
        e = fm1_enc_take(ENC_KNOB4);
        if (e) had_input = 1;
        if (e) {
            /* KNOB4 scrolls coarsely, one beat per click, snapping
             * the playhead to a beat boundary. In the book it enters
             * the piece stopped, then moves the sheet. It works
             * while the sheet is moving: the playhead jumps to the
             * new timestamp and keeps advancing from there at the
             * same velocity. Notes sound as the playhead passes them
             * (or are erased, delete head on). */
            if (in_book) enter_piece_stopped();
            knob_coarse_scroll(e);
        }
        e = fm1_enc_take(ENC_KNOB3);
        if (e) had_input = 1;
        if (e) {
            /* KNOB3 moves the sheet finely (scrub the tape), 4px per
             * click, from the moment the device starts: in the book
             * it enters the piece stopped, then moves the sheet. It
             * works while the sheet is moving, like KNOB4. */
            int32_t s;
            if (in_book) enter_piece_stopped();
            s = (int32_t)timestamp + e * 4;
            if (s < (int32_t)CURSOR_X) s = (int32_t)CURSOR_X;
            scrub_to((uint32_t)s);
        }
        e = fm1_enc_take(ENC_PRESETS);
        if (e) had_input = 1;
        if (e) {
            /* the PRESETS knob selects the FM piano patch. It wraps:
             * past the last patch it comes back to the first. The
             * patch name shows briefly at the bottom. Session-only:
             * it resets to E.PIANO 4 on boot. */
            int32_t p = (int32_t)piano_preset_get() + e;
            int32_t n = (int32_t)piano_preset_count();
            while (p < 0) p += n;
            piano_preset_set((uint8_t)(p % n));
            show_preset();
        }
        /* KNOB2 does nothing yet. */

        { uint32_t rel_; b = fm1_input_edges(&rel_); }
        if (b) had_input = 1;
        if (b & (1u << BTN_PLAY)) {
            /* PLAY toggles the velocity: stopped -> move at the tempo
             * rate, moving -> stop. It never touches recording. In
             * the book it enters the piece, then starts it. */
            if (in_book) {
                setup_piece();
                timestamp = CURSOR_X; timestamp_frac = 0;
                sheet_ts[sel] = CURSOR_X;   /* entering a piece starts at the beginning */
                in_book = 0;
                render_window(0u);
            }
            toggle_play();
        }
        if (b & (1u << BTN_REC)) {
            /* REC toggles the write bit: notes placed are written to
             * the take, or not. It never touches time. In the book
             * it enters the piece, then arms. */
            if (in_book) {
                setup_piece();
                timestamp = CURSOR_X; timestamp_frac = 0;
                sheet_ts[sel] = CURSOR_X;   /* entering a piece starts at the beginning */
                in_book = 0;
                render_window(0u);
            }
            toggle_rec();
        }
        if (b & (1u << BTN_SEQ)) {
            /* SEQ inserts a jump at the playhead (or removes it). */
            if (!in_book) seq_jump();
        }
        if (b & (1u << BTN_EDIT)) {
            /* EDIT toggles the delete head: the playhead erases
             * instead of playing, while time moves or while scrubbing. */
            delete_head = !delete_head;
            if (!moving()) render_window(scroll_now());
        }
        /* ARP does nothing now: looping is the jump's job (SEQ). */
        if (b & (1u << BTN_OCT_UP)) {
            if (oct_shift < OCT_MAX) { oct_shift++; apply_octave(); }
        }
        if (b & (1u << BTN_OCT_DOWN)) {
            if (oct_shift > OCT_MIN) { oct_shift--; apply_octave(); }
        }
        if (b & (1u << BTN_SAVE)) {
            /* mark the end of a free-play take at the playhead */
            if (is_free && !in_book) {
                free_end_x = timestamp;
                free_end_set = 1;
                if (!moving()) render_window(scroll_now());
            }
        }
        leds_update();
        if (b & (1u << BTN_HOME)) {
            /* HOME: on the tape, go back to the book (stopping and
             * disarming); stopped at a scrolled position, rewind
             * first; at the start, go to the book. */
            if (!in_book && (moving() || recording())) {
                save_if_dirty();
                setup_piece();
                timestamp = CURSOR_X; timestamp_frac = 0;
                sheet_ts[sel] = CURSOR_X;   /* HOME rewinds the sheet */
                velocity = 0; rec_write = 0;
                in_book = 1;
                render_book();
            } else if (!in_book && !moving() && !recording()) {
                save_if_dirty();
                if (timestamp > CURSOR_X) {
                    timestamp = CURSOR_X; timestamp_frac = 0;
                    sheet_ts[sel] = CURSOR_X;   /* HOME rewinds the sheet */
                    render_window(0u);
                } else {
                    setup_piece();
                    timestamp = CURSOR_X; timestamp_frac = 0;
                    sheet_ts[sel] = CURSOR_X;   /* HOME rewinds the sheet */
                    velocity = 0; rec_write = 0;
                    in_book = 1;
                    render_book();
                }
            }
        }

        keys = fm1_in.notes;
        keys_down = keys;
        if (keys) had_input = 1;   /* a held key is not idle */
        {
            /* one previous-keys state drives both edges: a press is
             * a bit set now and clear before, a release the reverse.
             * (A key cannot be both in one poll.) */
            uint32_t ed = keys & ~prev_keys;
            uint32_t off = prev_keys & ~keys;
            prev_keys = keys;
            if (ed || off) had_input = 1;
            for (k = 0; k < 27u; k++)
                if (ed >> k & 1u) {
                    uint8_t midi = key_midi(k);
                    held_midi[k] = midi;
                    /* no piano_on here: the key places a note at the
                     * playhead, and the playhead sounds it (on_key
                     * -> playhead_sound). One sound path. */
                    midi_send(0x90u, midi, 100);
                    on_key(midi, k);
                }
            for (k = 0; k < 27u; k++)
                if (off >> k & 1u) {
                    uint8_t midi = held_midi[k];
                    piano_off(midi);
                    midi_send(0x80u, midi, 0);
                    on_key_up(midi, k);
                }
        }

        /* scrub notes sound briefly; turn them off when their time is up. */
        scrub_notes_off();

        /* the preset name clears after ~2 s and the hint comes back. */
        if (preset_notice_until && fm1_ms >= preset_notice_until) {
            preset_notice_until = 0;
            show_hint();
        }

        if (moving() && fm1_ms - last_frame_ms >= FRAME_MS) {
            tick();
        }

        if (set_dirty && fm1_ms - last_save_ms > SAVE_THROTTLE_MS) {
            save_if_dirty(); last_save_ms = fm1_ms;
        }

        /* idle: any input resets the timer, and wakes a blanked
         * screen (repainted in full -- the input itself has already
         * acted normally above, so its own drawing was suppressed
         * and the repaint covers it). No input for IDLE_BLACK_MS
         * blanks the screen; the tape and the audio keep going. */
        if (had_input) {
            last_input_ms = fm1_ms;
            if (screen_blank) {
                screen_blank = 0;
                redraw_now();
            }
        } else if (!screen_blank && fm1_ms - last_input_ms >= IDLE_BLACK_MS) {
            blank_screen();
        }
    }
}
