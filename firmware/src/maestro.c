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
 * start goes back to the book. (On a book piece with a take, HOME
 * is the manual retry instead -- see v0.25 below.)
 *
 * The PRESETS knob selects the FM piano patch (E.PIANO 4, PIANO 1,
 * E.PIANO 3, PIANO 5, E.PIANO 1); the patch name shows briefly at
 * the bottom, in the style of the BPM readout, and the preset is
 * session-only (it resets to E.PIANO 4 on boot). The keyboard keys
 * sit at a dim backlight so they are visible in the dark. If REC
 * is on and you play a note while stopped, PLAY auto-starts
 * (the velocity becomes the tempo rate) -- the user can veto this.
 * (Free Play; in a book piece the v0.24 kick-off below replaces it.)
 *
 * KNOB1 is tempo (40-200 BPM, 2 per click): record slow, crank it up
 * for playback. While the sheet is moving the velocity follows the
 * tempo at once. The BPM shows at the bottom right, always. KNOB2
 * does nothing -- except on a multi-part song, where it steps
 * between the song's parts (see v0.26 below). OCT+ / OCT- transpose the keyboard by an octave
 * per press (-2..+2): what you play and record shifts, the blue score
 * does not. The OCT button lights in the direction you shifted, and
 * the shift shows at the top right. A shifted note still sounds; only
 * notes in the visible staff range (about E4-B5) draw on the sheet.
 * The clef follows the octave: shift down and the staff becomes a
 * bass staff (F clef -- the notes you play are in the bass range);
 * shift back up and it returns to treble.
 *
 * The panel lights mean one thing each (v0.27): PLAY is a colour,
 * not a pattern -- green while the tape is stopped, red while it
 * moves (velocity > 0), steady in both, in the book too (stopped
 * is stopped). REC is solid while a take is rolling and pulses
 * slowly while merely armed; SEQ bright while the take contains a
 * jump, EDIT bright while the delete head is on, SAVE bright once
 * a free-play end is marked, HOME bright always; the rest sit at
 * a dim glow. The key LEDs point at the note to be played: the
 * next guide note group's keys (the kick-off group at the park)
 * are bright and every other key sits at the dim glow, so the
 * keyboard itself answers "what now". During the end-of-take
 * replay the keys light the take's next note group instead.
 * Free Play has no guide and no target: all keys glow.
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
 *
 * v0.24: a practice loop, added on top of the tape recorder (the
 * transport, the knobs, jumps and the delete head all keep working
 * as before). A book piece parks at its first note: entering it,
 * switching to it, or loading it after a take puts the playhead on
 * the first blue note instead of at position 0 (Free Play still
 * restores its saved offset). Parked and stopped, playing that
 * first note (or every pitch of its chord) is an alternative way
 * to kick off REC: the write bit turns on, the tape starts
 * rolling, and the chord is recorded. In a book piece that
 * replaces auto-start: a wrong first note starts nothing, and a
 * stopped press records nothing (while rolling, REC records as
 * always). When a rolling book take reaches the end of the piece,
 * another piece loads, parked: the same piece again after a
 * terrible take, otherwise the next piece in the book (wrapping
 * to the first; the drill is re-seeded when the sequence loads
 * it). PLAY-only play-along still just stops at the end.
 * "Terrible" is invisible -- nothing is scored on screen. During
 * a rolling take, presses are matched against the blue notes (a
 * press whose pitch is right and whose playhead is within
 * MATCH_W_PX of the note's x matches it); a take is terrible when
 * fewer than half the notes matched, or when wrong presses
 * outnumber the notes. All of it is session state: nothing
 * persists.
 *
 * v0.25: retries and inspection, from hardware feedback on v0.24.
 * The device powers on in the state PLAY+HOME used to be the
 * ritual to reach: on the tape, at the persisted piece, parked on
 * its first note (it booted into the book with the timestamp at 0,
 * and the first key press shifted the sheet sideways). When a
 * book take reaches the end it first plays back once -- the
 * end-of-take replay -- so the player hears the take and sees
 * what they missed: blue notes the take did not match draw in
 * amber during the replay (a third UI plane). Any key press skips
 * the replay. Only then does the finished take's load happen, and
 * a terrible take's reload now clears that sheet's take first, so
 * the retry is not accompanied by the failed attempt. HOME on a
 * book piece with a take is the same retry by hand: stop, clear,
 * park. Good takes are never cleared by any of this.
 *
 * v0.26: guided assembly -- a multi-part song, learned one part
 * at a time, where your finished parts become the band. The book
 * gains an eighth entry, the song (Ode to Joy; its parts were
 * generated at build time from a chord chart -- lessons2c.py).
 * A song is ONE sheet whose take holds every part at once: a
 * note's part is its MIDI channel (melody 0, bass 1, harmony 2
 * -- reserved, no events yet -- percussion 9, the GM drum
 * channel). The parts are learned in order (percussion, bass,
 * melody): the song opens parked at the first part's first note,
 * the kick-off works per part from cold, and completing a part
 * cues the next, with every completed part's take playing back
 * as backing, each through its own instrument (the bass sounds
 * an octave below written, the percussion channel is a drum).
 * A part is silent -- no guide, no sound -- until it is yours to
 * learn or you have played it. The end-of-part flow is the
 * v0.25 flow per part (replay with amber misses; a terrible take
 * retries with only that part's events cleared), and when the
 * last part lands, the whole assembly plays once before the
 * song re-parks at the first part. KNOB2 steps between parts
 * (back to one you have played, forward as far as the first
 * unplayed part). Voices: the part in your hands is LIVE and
 * always wins; backing parts are dropped before they can steal
 * a live voice (fm_piano.c).
 *
 * v0.28: a refactoring round -- no behaviour change. The session
 * state flags (transport, sheet kind, song and part, replay) are
 * gathered in one place, and every "what state are we in" question
 * is asked through a named predicate (the "session state: the
 * predicates" section: moving, recording, in_book_view,
 * on_free_play, on_song, on_guided_sheet, replaying_now,
 * assembly_playing, deleting, free_end_marked, part_is_done,
 * cur_part_done, and at_kick_park below). Handlers, the renderer
 * and the LED update no longer open-code derivations from the raw
 * flags. Equivalence was proven, not assumed: the full host
 * regression passed unchanged, and an old-vs-new sweep ran every
 * script fixture through both binaries and diffed every artifact.
 *
 * v0.29: accidentals. A black-key pitch now wears a sharp sign
 * before its notehead, drawn in ui_note() so the guide (blue),
 * the take (black) and a missed note in the replay (amber) all
 * get it, each in its own plane. ui_dia() has always put a sharp
 * on its natural's diatonic step, and ui_accidental() existed but
 * was never called -- so a C# drew exactly as a C. That bit in
 * practice: the generated drill is a semitone walk, and its first
 * note is a black key about a third of the time; the learner
 * reads the sheet exactly as drawn, presses the natural, and the
 * kick-off silently refuses (a wrong first pitch starts nothing,
 * by design). Every sharp is signed individually -- no key
 * signatures, no naturals -- so an unsigned notehead reads as
 * the natural. All content voices sharps, never flats (the
 * drill and the .lesson/.mid pipeline produce sharps by
 * construction), so the sign is always the sharp glyph.
 *
 * v0.30: the song is the front door, and the assembly is four
 * real parts. The book is reordered -- the song is slot 0, the
 * pieces follow (PIECE_BASE), Free Play is last -- so a fresh
 * boot lands on the song's first part instead of burying the
 * song behind every lesson. NBOOK is still 8 and every
 * persisted selection an older build could write is still a
 * valid slot, so old saves boot safely (onto a different sheet
 * than they meant, never a hole). The song's title carries its
 * "(Song)" suffix in its data (lessons/ode-to-joy.song), so its
 * sheets no longer read exactly like the Ode to Joy lesson's.
 * The harmony part is generated now (lessons2c.py: chord tones
 * cycling root-3rd-5th-3rd, written around middle C) and sounds
 * through a fixed PIANO 1 (route_event), leaving the PRESETS
 * knob to the melody alone. The drum gains a pitch drop so its
 * attack can be heard through the small speaker (fm_piano.c).
 */

#include "fm_piano.c"   /* DX7 FM piano presets (E.PIANO 4 default) -- v0.19 */
#include "midi.c"   /* Standard MIDI File events: the take -- v0.22 */

/* ---- pieces: the book ---- */
/* The book's order (v0.30): the song first, then the pieces,
 * then Free Play last. The song is the front door -- a fresh
 * boot (sel 0) lands on its first part. NBOOK is unchanged at
 * 8, and every value an older build could have persisted
 * (0..7) is still a valid slot in this order, so a saved
 * selection boots safely -- onto a different sheet than the
 * old build meant, never onto a hole. */
#define NPIECES 6u
#define SONG_IDX 0u
#define PIECE_BASE 1u   /* the pieces occupy slots PIECE_BASE..PIECE_BASE+NPIECES-1 */
#define FREE_IDX (PIECE_BASE + NPIECES)   /* 7 */
#define NBOOK (NPIECES + 2u)   /* the pieces + Free Play + the song */
static const lesson_t *pieces[NPIECES];  /* 0..4 lessons, 5 = generated drill */
static lesson_t drill_lesson;
static const char *free_title = "Free Play";

/* the generated drill (pieces[5]) is regenerated from drill_seed:
 * the boot drill uses seed 7, and the end-of-take sequence gives
 * the drill a fresh seed each time it loads it, so a wrapped
 * sequence never repeats the same drill. */
static uint32_t drill_seed = 7u;
static void regen_drill(void) {
    drill_params_t dp;
    dp.position_midi = 60; dp.range = 7; dp.tempo = 96;
    dp.bars = 4; dp.beats_per_bar = 4;
    dp.rhythm_mask = RHY_Q | RHY_H | RHY_E;
    dp.allow_rests = 1; dp.seed = drill_seed;
    drill_lesson.event_count = drill_generate(&dp, drill_lesson.events, LESSON_MAX_EVENTS);
}

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
static int16_t psym_ev[MAXPSYM];   /* lesson event index per symbol (-1: none) */
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
    psym[psym_n].x = CLEF_X; psym[psym_n].type = SYM_CLEF; psym_ev[psym_n] = -1; psym_n++;
    for (b = 1; b * bar_ticks < total_ticks && psym_n < MAXPSYM; b++) {
        psym[psym_n].x = (uint16_t)bar_x(b); psym[psym_n].type = SYM_BAR; psym_ev[psym_n] = -1; psym_n++;
    }
    for (i = 0; i < evn && psym_n < MAXPSYM; i++) {
        psym[psym_n].x = (uint16_t)evx[i];
        psym[psym_n].type = L->events[i].is_rest ? SYM_REST : SYM_NOTE;
        psym[psym_n].midi = L->events[i].midi;
        psym[psym_n].ticks = L->events[i].ticks;
        psym_ev[psym_n] = (int16_t)i;
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
        psym[psym_n].x = (uint16_t)piece_end_x; psym[psym_n].type = SYM_END; psym_ev[psym_n] = -1; psym_n++;
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
static uint8_t is_song = 0;      /* the current sheet is the multi-part song */
static uint8_t song_part;        /* index into ode_song.parts (learning order) */
static uint8_t song_done[SONG_MAX_PARTS];   /* parts with a completed take */
static uint8_t assembly;         /* the finished assembly is playing back */
static uint8_t replaying;        /* a finished take is playing back (v0.25) */
static uint8_t replay_terrible;  /* the verdict the replay ends into */
static uint8_t free_end_set = 0; /* SAVE: a free-play take has a marked end */
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

/* ---- session state: the predicates ----
 * Every "what state are we in" question the firmware asks is
 * answered here, by a named predicate over the state above.
 * Handlers, the renderer and the LED update call these, never
 * the raw flags, so each state derivation lives in exactly one
 * place and cannot drift into a subtly different copy at another
 * site (the v0.26 kick-off bug was an open-coded park test that
 * no other site shared). at_kick_park(), with the practice loop
 * below, is part of this family. The mutations are not here:
 * set_velocity/toggle_play flip the velocity, set_rec_write/
 * toggle_rec flip the write bit, setup_piece derives the sheet
 * kind from the selection, and the load/restore paths write the
 * flags (and occasionally the velocity, to avoid set_velocity's
 * side effects) directly -- those are the state being set, not
 * state being asked about. */
static uint8_t moving(void) { return velocity != 0; }              /* the tape rolls */
static uint8_t recording(void) { return rec_write; }               /* notes placed are written to the take */
static uint8_t in_book_view(void) { return in_book; }              /* selecting a piece, not on the tape */
static uint8_t on_free_play(void) { return is_free; }              /* the current sheet is Free Play */
static uint8_t on_song(void) { return is_song; }                   /* the current sheet is the multi-part song */
static uint8_t on_guided_sheet(void) { return !is_free; }          /* a sheet with a blue guide: a book piece or a song part */
static uint8_t replaying_now(void) { return replaying; }           /* a finished take (or the assembly) is playing back */
static uint8_t assembly_playing(void) { return assembly; }         /* that playback is the finished assembly, not an inspection */
static uint8_t deleting(void) { return delete_head; }              /* the playhead erases instead of sounding */
static uint8_t free_end_marked(void) { return is_free && free_end_set; }   /* a free-play end is marked on this sheet */
static uint8_t part_is_done(uint8_t p) { return song_done[p]; }    /* song part p has a completed take */
static uint8_t cur_part_done(void) { return is_song && song_done[song_part]; }   /* the part in your hands is already learned */
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
    if (in_book_view()) return "SELECT flip PLAY start";
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

/* ---- the multi-part song (v0.26) ----
 * The song (book entry SONG_IDX) is one sheet whose take holds
 * every part at once, told apart by MIDI channel: melody 0,
 * bass 1, harmony 2 (a real generated part since v0.30),
 * percussion 9 (the GM drum channel). song_lesson is the current part's
 * guide, filled in by load_song_part() so the layout, the judge
 * and the kick-off see an ordinary lesson. All song state is
 * session-only, like the rest of the practice loop. */
#define CH_MELODY 0u
#define CH_BASS 1u
#define CH_HARMONY 2u
#define CH_PERC 9u
#define DRUM_PITCH 38u   /* the percussion channel sounds at this pitch, whichever pad key was hit */
#define CLS_LIVE 0u      /* the part in your hands: its notes always win voices */
#define CLS_BACK 1u      /* a completed part, playing as backing */
/* the song state flags (is_song, song_part, song_done, assembly)
 * are declared with the transport state above, where the session
 * predicates can see them. */
static lesson_t song_lesson;

/* the channel notes are written to / judged against right now:
 * the current part's on the song, channel 0 anywhere else. */
static uint8_t cur_channel(void) { return is_song ? ode_song.parts[song_part].channel : 0u; }

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

/* is sheet s's take empty (nothing between its block start and its
 * End of Track)? */
static uint8_t sheet_take_empty(uint8_t s) {
    return sheet_notes_end(s) <= sheet_notes_begin(s);
}

/* does sheet s's take hold any note? (a jump alone is not one) */
static uint8_t sheet_take_has_notes(uint8_t s) {
    uint16_t i;
    for (i = sheet_notes_begin(s); i < sheet_notes_end(s); i++)
        if (mev_is_note_on(&rec[i])) return 1;
    return 0;
}

/* clear sheet s's take: every event in its block goes, leaving just
 * its End of Track with delta 0 -- exactly the fresh rec_init state
 * for that sheet. Other sheets are untouched (absolute times after
 * the block do not move: removal passes deltas forward, and the
 * block's own total ends up reset on its End of Track). */
static void clear_sheet_take(uint8_t s) {
    uint16_t b = sheet_notes_begin(s);
    while (sheet_notes_end(s) > b) remove_rec(b);
    rec[sheet_end_idx(s)].delta = 0;
}

/* does the current sheet's take hold a note on channel ch?
 * (On the song: has this part been played?) */
static uint8_t part_take_has_notes(uint8_t ch) {
    uint16_t i;
    for (i = sheet_notes_begin(sel); i < sheet_notes_end(sel); i++)
        if (mev_is_note_on(&rec[i]) && mev_channel(&rec[i]) == ch) return 1;
    return 0;
}

/* clear one part's take from the current sheet: every note event
 * on channel ch goes; the other parts' events keep their absolute
 * times (removal passes deltas forward through the interleaved
 * list). The song's retry clears one part, never the band. */
static void clear_part_take(uint8_t ch) {
    uint16_t i = sheet_notes_begin(sel);
    while (i < sheet_notes_end(sel)) {
        if (mev_channel(&rec[i]) == ch && (mev_is_note_on(&rec[i]) || mev_is_note_off(&rec[i])))
            remove_rec(i);
        else i++;
    }
}

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
 * (the first Note Off for that pitch on that channel after it);
 * the default quarter note when there is none (a take note always
 * has one). */
static uint32_t note_off_px(uint16_t b, uint16_t e, uint16_t on_idx, uint8_t pitch, uint32_t on_abs_ticks) {
    uint16_t j;
    uint8_t ch = mev_channel(&rec[on_idx]);
    for (j = (uint16_t)(on_idx + 1u); j < e; j++)
        if (mev_is_note_off(&rec[j]) && rec[j].d1 == pitch && mev_channel(&rec[j]) == ch)
            return ticks_to_px(mev_abs(rec, b, j));
    return ticks_to_px(on_abs_ticks + 96u);
}

/* remove a note: its Note On and its paired Note Off (the off first,
 * so the on's index stays valid). */
static void remove_note_pair(uint16_t on_idx) {
    uint16_t e = sheet_notes_end(sel);
    uint16_t j;
    uint8_t pitch = rec[on_idx].d1;
    uint8_t ch = mev_channel(&rec[on_idx]);
    for (j = (uint16_t)(on_idx + 1u); j < e; j++)
        if (mev_is_note_off(&rec[j]) && rec[j].d1 == pitch && mev_channel(&rec[j]) == ch) { remove_rec(j); break; }
    remove_rec(on_idx);
}

/* ghost notes: a key held while NOT recording shows its note at the
 * playhead until released (so you can see which note to start with).
 * One per key, indexed by the key. `sounded` marks that the playhead
 * has already triggered it, so it sounds exactly once. */
static struct { uint8_t midi; uint16_t x; uint8_t active; uint8_t sounded; } ghost[27];

/* scrub notes: notes the playhead sounds while scrubbing are turned
 * off after SCRUB_MS, so no voice is left ringing. A scrubbed note
 * is tracked by the pitch it SOUNDED at (routed, on the song) plus
 * its written pitch, so the held-key check still works. */
#define MAXSCRUB 8
#define SCRUB_MS 180u
static struct { uint8_t wmidi; uint8_t tpitch; uint8_t cls; uint32_t off_ms; } scrub_notes[MAXSCRUB];
static uint8_t scrub_n;

/* playback: next note to start, and notes currently sounding
 * (tracked by sounded pitch and voice class, so a note-off ends
 * the voice its note-on started). */
static uint16_t pb_idx;
#define MAXSOUND 12
static struct { uint8_t pitch; uint32_t end_x; uint8_t cls; } sounding[MAXSOUND];
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
    /* the note starts at the playhead, in ticks, on the current
     * part's channel (channel 0 off the song). */
    uint32_t t = px_to_ticks(timestamp > REC_X_MAX ? REC_X_MAX : timestamp);
    uint8_t ch = cur_channel();
    uint16_t b = sheet_notes_begin(sel), e = sheet_notes_end(sel), i;
    for (i = b; i < e; i++) {
        if (mev_is_note_on(&rec[i]) && rec[i].d1 == midi && mev_channel(&rec[i]) == ch) {
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
    insert_ev_abs(b, sheet_notes_end(sel), t + 96u, (uint8_t)(MIDI_NOTE_OFF | ch), midi, 0, 1);
    insert_ev_abs(b, sheet_notes_end(sel), t, (uint8_t)(MIDI_NOTE_ON | ch), midi, 100, 0);
    /* the playhead has passed the note just placed (it sounds via the
     * ghost), so the tick must not re-trigger it. */
    pb_idx = mev_find_abs(rec, sheet_notes_begin(sel), sheet_notes_end(sel), t, 0);
}
static void rec_note_off(uint8_t midi) {
    /* close the note: move its Note Off to the playhead (clamped to
     * a 24..384 tick duration, as before). The most recent Note On
     * for this pitch on this channel is the one the held key placed. */
    uint32_t t = px_to_ticks(timestamp > REC_X_MAX ? REC_X_MAX : timestamp);
    uint8_t ch = cur_channel();
    uint16_t b = sheet_notes_begin(sel), e = sheet_notes_end(sel), i, j;
    for (i = e; i-- > b; ) {
        if (mev_is_note_on(&rec[i]) && rec[i].d1 == midi && mev_channel(&rec[i]) == ch) {
            uint32_t on_abs = mev_abs(rec, b, i);
            uint32_t dur = t > on_abs ? t - on_abs : 0;
            if (dur < 24u) dur = 24u;
            if (dur > 384u) dur = 384u;
            for (j = (uint16_t)(i + 1u); j < e; j++)
                if (mev_is_note_off(&rec[j]) && rec[j].d1 == midi && mev_channel(&rec[j]) == ch) { remove_rec(j); break; }
            insert_ev_abs(b, sheet_notes_end(sel), on_abs + dur, (uint8_t)(MIDI_NOTE_OFF | ch), midi, 0, 1);
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

/* Routing (v0.26): the instrument an event sounds with, and the
 * pitch it sounds at. On an ordinary sheet everything is the live
 * piano at its written pitch. On the song the channel decides:
 * the bass is a transposing instrument (written at a readable
 * octave, sounding an octave below, like a double bass), the
 * percussion channel sounds at one fixed low pitch (a drum hit
 * is a drum hit, whichever pad key wrote it), the harmony has a
 * fixed patch of its own from the piano family (v0.30: PIANO 1,
 * so it reads as a second piano, not as the melody's voice),
 * and the melody is the PRESETS knob's patch. An event's voice
 * class is LIVE when its channel is the part in your hands,
 * BACKING when it is a completed part playing along. */
static void route_event(uint8_t ch, uint8_t pitch, uint8_t *patch, uint8_t *tpitch) {
    if (on_song()) {
        if (ch == CH_BASS) { *patch = PATCH_BASS; *tpitch = pitch > 12u ? (uint8_t)(pitch - 12u) : pitch; return; }
        if (ch == CH_PERC) { *patch = PATCH_DRUM; *tpitch = DRUM_PITCH; return; }
        if (ch == CH_HARMONY) { *patch = PATCH_HARMONY; *tpitch = pitch; return; }
    }
    *patch = piano_preset_get();
    *tpitch = pitch;
}
static uint8_t event_cls(uint8_t ch) {
    return (on_song() && ch != cur_channel()) ? CLS_BACK : CLS_LIVE;
}

/* Sound a note once. This is the only place piano_on_p is called.
 * `midi` is the written pitch; the note sounds at `tpitch` with
 * `patch` in voice class `cls` (route_event decides). `end_x` is
 * the piece-x at which a playback note turns off (ignored for
 * MODE_SCRUB/MODE_LIVE). A scrubbed note already sounding is
 * skipped (its 180 ms not yet up), and a note held live is skipped
 * when scrubbing (it is already sounding from the key press). A
 * backing note the engine drops (no voice it may take) is not
 * tracked: nothing needs turning off. */
static void trigger_note(uint8_t midi, uint32_t end_x, uint8_t mode, uint8_t patch, uint8_t tpitch, uint8_t cls) {
    uint8_t i;
    if (mode == MODE_SCRUB) {
        if (midi_held(midi)) return;  /* already sounding live */
        for (i = 0; i < scrub_n; i++)
            if (scrub_notes[i].tpitch == tpitch && scrub_notes[i].cls == cls) { scrub_notes[i].off_ms = fm1_ms + SCRUB_MS; return; }
        if (scrub_n >= MAXSCRUB) return;
    }
    if (!piano_on_p(tpitch, 100, patch, cls)) return;   /* the only piano_on call in the firmware */
    if (mode == MODE_SCRUB) {
        scrub_notes[scrub_n].wmidi = midi;
        scrub_notes[scrub_n].tpitch = tpitch;
        scrub_notes[scrub_n].cls = cls;
        scrub_notes[scrub_n].off_ms = fm1_ms + SCRUB_MS;
        scrub_n++;
    } else if (mode == MODE_PLAY) {
        /* a playback note, off at its recorded end */
        if (n_sounding < MAXSOUND) {
            sounding[n_sounding].pitch = tpitch;
            sounding[n_sounding].end_x = end_x;
            sounding[n_sounding].cls = cls;
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
    /* Ghost notes at the playhead that have not sounded yet: they
     * sound with the current part's instrument (route_event on the
     * current channel), in the live voice class. */
    for (i = 0; i < 27u; i++) {
        if (ghost[i].active && !ghost[i].sounded && ghost[i].x == to) {
            uint8_t patch, tpitch;
            route_event(cur_channel(), ghost[i].midi, &patch, &tpitch);
            ghost[i].sounded = 1;
            trigger_note(ghost[i].midi, 0, MODE_LIVE, patch, tpitch, CLS_LIVE);
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
                if (deleting()) { remove_rec(i); end--; continue; }
                t_prev = t; i++;
            } else if (mev_is_note_on(&rec[i])) {
                if (deleting()) { remove_note_pair(i); end = sheet_notes_end(sel); continue; }
                {
                    uint8_t patch, tpitch, ch = mev_channel(&rec[i]);
                    route_event(ch, rec[i].d1, &patch, &tpitch);
                    trigger_note(rec[i].d1, 0, MODE_SCRUB, patch, tpitch, event_cls(ch));
                }
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
                if (deleting()) {
                    remove_rec(pb_idx); end--;   /* next event shifts in; re-examine it */
                } else if (mode == MODE_PLAY) {
                    return 1;  /* the jump loops; the caller wraps the tape */
                } else {
                    pb_idx++;  /* a key press does not loop on a jump */
                    if (pb_idx < end) t += rec[pb_idx].delta;
                }
            } else if (mev_is_note_on(&rec[pb_idx])) {
                if (deleting()) {
                    remove_note_pair(pb_idx); end = sheet_notes_end(sel);
                } else {
                    uint8_t m = rec[pb_idx].d1;
                    uint8_t patch, tpitch, ch = mev_channel(&rec[pb_idx]);
                    route_event(ch, m, &patch, &tpitch);
                    trigger_note(m, note_off_px(b, end, pb_idx, m, t), mode, patch, tpitch, event_cls(ch));
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

/* the end-of-take replay (with the practice loop below) draws the
 * blue notes a take missed in amber; the renderer asks here. */
static uint8_t replay_missed_note(uint16_t ev);

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
                /* during the end-of-take replay, a blue note the
                 * take missed draws in amber; matched notes (and
                 * every note outside a replay) stay blue. */
                if (psym_ev[i] >= 0 && replay_missed_note((uint16_t)psym_ev[i])) ui_plane_amber();
                else ui_plane_blue();
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
    if (on_free_play()) {
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
    if (free_end_marked()) {
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
    const char *s = (sel == FREE_IDX) ? free_title : pl->title;
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
    if (in_book_view()) { render_book(); return; }
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
    if (in_book_view()) render_book();
    else render_window(scroll_now());
}

/* fill the song's lesson with the current part's guide, so the
 * layout, the judge and the kick-off see an ordinary lesson. The
 * title names the part ("Ode to Joy: Bass") -- on a score, the
 * part name is part of the music. */
static void load_song_part(void) {
    const song_part_t *p = &ode_song.parts[song_part];
    uint16_t i;
    {
        const char *a = ode_song.title, *b = p->name;
        char *d = song_lesson.title;
        char *end = song_lesson.title + LESSON_TITLE_LEN - 1;
        while (*a && d < end) *d++ = *a++;
        if (d + 2 < end) { *d++ = ':'; *d++ = ' '; }
        while (*b && d < end) *d++ = *b++;
        *d = 0;
    }
    {
        const char *s = "Beethoven (1824), public domain; parts generated at build time";
        char *d = song_lesson.source;
        char *end = song_lesson.source + LESSON_SOURCE_LEN - 1;
        while (*s && d < end) *d++ = *s++;
        *d = 0;
    }
    song_lesson.kind = KIND_MELODY;
    song_lesson.level = 2;
    song_lesson.position_midi = 60;
    song_lesson.tempo = ode_song.tempo;
    song_lesson.beats_per_bar = ode_song.beats_per_bar;
    song_lesson.event_count = p->event_count;
    for (i = 0; i < p->event_count; i++) song_lesson.events[i] = p->events[i];
}

static void setup_piece(void) {
    is_free = (sel == FREE_IDX);
    is_song = (sel == SONG_IDX);
    if (on_free_play()) {
        evn = 0;
        total_ticks = 0;
        bar_ticks = 384u;   /* 4/4: a bar is 128px, for the built-in bar lines */
        /* free play has no lesson, but the staff still gets a clef --
         * it is the symbol that shows which clef the staff is in. */
        psym_n = 0;
        psym[psym_n].x = CLEF_X; psym[psym_n].type = SYM_CLEF; psym_ev[psym_n] = -1; psym_n++;
    } else if (on_song()) {
        load_song_part();
        pl = &song_lesson;
        layout_piece(pl);
    } else {
        pl = pieces[sel - PIECE_BASE];
        layout_piece(pl);
    }
}
/* picking a piece (fresh): lay it out and reset the tempo to the piece's.
 * PLAY/REC/HOME use setup_piece() alone, so a tempo you dialed in with
 * KNOB1 survives starting the tape. */
static void select_piece(void) {
    setup_piece();
    tempo = on_free_play() ? FREE_TEMPO : pl->tempo;
}

/* ---- the practice loop (v0.24) ----
 * start_x is where a sheet's tape rests: Free Play at the cursor,
 * a book piece parked on its first blue note. Entering a book
 * piece, switching to it, or loading it after a take all park
 * there; Free Play restores its saved offset instead. */
static uint32_t start_x(void) {
    uint16_t i;
    if (on_free_play()) return CURSOR_X;
    for (i = 0; i < evn; i++)
        if (!pl->events[i].is_rest) return evx[i];
    return CURSOR_X;   /* a piece with no notes parks at the start */
}

/* the take's invisible score: how the rolling take is doing,
 * counted only to pick same-piece vs next-piece at the end. A
 * press matches a blue note when its pitch is right and the
 * playhead is within MATCH_W_PX of the note's x (half a quarter
 * note of score space: forgiving, not free). Wrong presses while
 * rolling a take count against it too. Nothing is displayed. */
#define MATCH_W_PX 16u   /* a press this close (px) to a blue note can match it */
#define TERRIBLE_MULT 2u /* terrible: matched * TERRIBLE_MULT < total */

/* the kick-off's park: the playhead stopped where a first-note
 * press can only mean "start the take" -- anywhere in the lead-in
 * before the first note (no music exists there yet), and up to
 * half a beat past it: a PLAY tap stopped a hair late, a scrub
 * detent, a bumped knob. The designed entries (boot, SELECT,
 * HOME, cue) all land on start_x() exactly, but a human reaches
 * the park approximately; an exact-equality test made every
 * near-park press a silent ghost -- it sounded, and started
 * nothing. kickoff_press snaps to the exact park before it
 * records, so a forgiven press still writes an aligned take. */
static uint8_t at_kick_park(void) {
    return timestamp <= start_x() + MATCH_W_PX;
}
static uint8_t ev_hit[MAXEV];   /* blue notes matched in the current take */
static uint32_t take_matched, take_wrong, take_total;
/* the first note's chord, for the REC kick-off: the pitches that
 * must be pressed at the park to start a take without REC/PLAY. */
static uint8_t kick_need[8];
static uint8_t kick_need_n;
static uint8_t kick_got_mask;   /* which kick_need pitches are already pressed */

/* a book piece becomes current: fresh counters, and the first
 * note's chord (every non-rest note at the first note's x). */
static void reset_take_stats(void) {
    uint16_t i;
    uint32_t x0 = 0;
    take_matched = 0; take_wrong = 0; take_total = 0;
    for (i = 0; i < MAXEV; i++) ev_hit[i] = 0;
    kick_need_n = 0; kick_got_mask = 0;
    if (on_free_play() || !pl) return;
    for (i = 0; i < evn; i++) {
        if (pl->events[i].is_rest) continue;
        take_total++;
        if (!kick_need_n) x0 = evx[i];
        else if (evx[i] != x0) continue;   /* the chord is only at the first x */
        if (kick_need_n < 8u) kick_need[kick_need_n++] = pl->events[i].midi;
    }
}

/* judge one press during a rolling book take: the nearest
 * unmatched blue note of this pitch within the match window is a
 * match; anything else is a wrong press. */
static void judge_press(uint8_t midi) {
    uint16_t i;
    int32_t best = -1;
    uint32_t bestd = MATCH_W_PX + 1u;
    for (i = 0; i < evn; i++) {
        uint32_t d;
        if (pl->events[i].is_rest || ev_hit[i] || pl->events[i].midi != midi) continue;
        d = evx[i] > timestamp ? evx[i] - timestamp : timestamp - evx[i];
        if (d < bestd) { bestd = d; best = (int32_t)i; }
    }
    if (best >= 0) { ev_hit[best] = 1; take_matched++; }
    else take_wrong++;
}

/* terrible: fewer than half the piece's blue notes matched, or
 * more wrong presses than the piece has notes. */
static uint8_t take_terrible(void) {
    if (!take_total) return 0;
    return take_matched * TERRIBLE_MULT < take_total || take_wrong > take_total;
}

/* ---- transport ---- */
static void silence_playback(void) {
    uint8_t i;
    for (i = 0; i < n_sounding; i++) piano_off_p(sounding[i].pitch, sounding[i].cls);
    n_sounding = 0;
}

static uint32_t target_keys(void);   /* below, with the replay state it reads */

/* the panel lights, one meaning each (v0.27): PLAY is green while
 * the tape is stopped and red while it moves (velocity > 0) --
 * steady, never a pattern, in the book too; REC solid while a take
 * is rolling, pulsing slowly while merely armed, dark while off;
 * SEQ bright while the take contains a jump, EDIT bright while
 * the delete head is on, SAVE bright once a free-play end is
 * marked, HOME bright always; the rest sit at a dim glow. The key
 * LEDs light the note to be played (target_keys) bright over the
 * dim glow every other key sits at. */
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
        if (b == (uint32_t)BTN_PLAY) continue;   /* its two colours, below */
        if (b == (uint32_t)BTN_HOME) bright = 1;
        else if (b == (uint32_t)BTN_REC) {
            if (recording() && moving()) bright = 1;              /* a take is rolling */
            else if (recording()) bright = (uint8_t)((fm1_ms / 400u) & 1u);   /* armed: a slow pulse */
            else bright = 0;
        }
        else if (b == (uint32_t)BTN_SEQ) bright = jump;
        else if (b == (uint32_t)BTN_EDIT) bright = deleting();
        else if (b == (uint32_t)BTN_OCT_UP) bright = (oct_shift > 0);
        else if (b == (uint32_t)BTN_OCT_DOWN) bright = (oct_shift < 0);
        else if (b == (uint32_t)BTN_SAVE) bright = free_end_marked();
        if (bright) { fm1_led_key(b, 1); fm1_led_key_glow(b, 0); }
        else { fm1_led_key(b, 0); fm1_led_key_glow(b, 1); }
    }
    /* PLAY: green stopped, red moving -- the transport state as
     * two points of the play driver's space: full brightness,
     * balance hard green or hard red (fm1_led_play_set). */
    fm1_led_play_set(255u, moving() ? 255u : 0u);
#if !BOARD_SMK37
    /* the note to be played, bright over the backlight glow
     * (FM-1 only: the SMK-37 keys have no LEDs). */
    {
        uint32_t target = target_keys();
        for (b = 0; b < 27u; b++) {
            if ((target >> b) & 1u) { fm1_led_key(KEY_LED(b), 1); fm1_led_key_glow(KEY_LED(b), 0); }
            else { fm1_led_key(KEY_LED(b), 0); fm1_led_key_glow(KEY_LED(b), 1); }
        }
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
        kick_got_mask = 0;   /* a half-pressed kick-off chord is forgotten */
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
    if (moving()) set_velocity(0);
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

/* ---- the end-of-take replay (v0.25) ----
 * When a book take reaches the end of the piece, it is played back
 * once before the next piece loads, so the player can hear what
 * they played and see what they missed. The take's match record
 * (ev_hit, from judge_press) is kept for the whole replay: blue
 * notes the take did not match draw in amber as they pass, matched
 * ones stay blue. The replay is ordinary playback through the one
 * sound path (MODE_PLAY) -- knobs scrub it, PLAY pauses it, a jump
 * in the take is stepped over instead of looping (the take plays
 * once). Any key press skips the replay. When it ends, played out
 * or skipped, the finished take's load happens (load_after_take):
 * a terrible take reloads the same piece with its take cleared --
 * the retry starts clean, nothing plays along with it -- and a
 * decent one advances. The clearing happens only there, after the
 * replay has inspected the take. All of it is session state.
 * (The replaying/replay_terrible flags are declared with the
 * transport state above, where the session predicates live.) */
static uint8_t replay_missed_note(uint16_t ev) {
    return replaying_now() && !assembly_playing() && ev < MAXEV && !ev_hit[ev];
}

/* ---- the note to be played (v0.27): the keys the panel lights
 * bright. A pitch lights the key that sounds it under the current
 * octave shift; a pitch no key currently sounds lights nothing. */
static uint32_t target_key_bit(uint8_t midi) {
    int32_t k = (int32_t)midi - 53 - (int32_t)oct_shift * 12;
    return k >= 0 && k < 27 ? 1u << (uint32_t)k : 0u;
}

/* the guide's next note group: the first group (the non-rest
 * events at one x) not yet fully matched and not more than the
 * match window behind the playhead. At the park that is the
 * kick-off group; rolling, it is the group to play next, and it
 * advances as each group is matched or passes out of reach. */
static uint32_t target_guide_keys(void) {
    uint16_t i;
    for (i = 0; i < evn; i++) {
        uint32_t x, mask = 0;
        uint16_t j;
        uint8_t all_hit = 1;
        if (pl->events[i].is_rest) continue;
        x = evx[i];
        for (j = i; j < evn && evx[j] == x; j++) {
            if (pl->events[j].is_rest || ev_hit[j]) continue;
            all_hit = 0;
            mask |= target_key_bit(pl->events[j].midi);
        }
        if (!all_hit && (int32_t)x + (int32_t)MATCH_W_PX >= (int32_t)timestamp)
            return mask;
        while (i + 1u < evn && evx[i + 1u] == x) i++;   /* skip the rest of this group */
    }
    return 0;
}

/* the replay's target: the take's next Note On group (any channel:
 * on the song the parts sound together), by the same window rule. */
static uint32_t target_take_keys(void) {
    uint16_t i, b = sheet_notes_begin(sel), e = sheet_notes_end(sel);
    uint32_t now = px_to_ticks(timestamp), gt = 0xFFFFFFFFu, mask = 0;
    for (i = b; i < e; i++) {
        uint32_t at;
        if (!mev_is_note_on(&rec[i])) continue;
        at = mev_abs(rec, b, i);
        if (at + MATCH_W_PX * 96u / PXQ >= now) { gt = at; break; }
    }
    if (gt == 0xFFFFFFFFu) return 0;
    for (i = b; i < e; i++)
        if (mev_is_note_on(&rec[i]) && mev_abs(rec, b, i) == gt)
            mask |= target_key_bit(rec[i].d1);
    return mask;
}

/* the keys to light bright, or 0 for none: the book and Free Play
 * have no "next note"; a replay lights the take, anything else on
 * a guided sheet lights the guide. */
static uint32_t target_keys(void) {
    if (in_book_view() || on_free_play()) return 0;
    if (replaying_now()) return target_take_keys();
    if (!pl || !evn) return 0;
    return target_guide_keys();
}

/* load the piece a finished take hands over to, parked at its
 * first note, tape stopped, ready for the next take. A terrible
 * take repeats the same piece (with its take cleared first: the
 * retry starts clean); a decent one advances through the book
 * (wrapping to the first piece, never to Free Play; the drill is
 * re-seeded when the sequence loads it). The write bit is left as
 * it is -- the kick-off works either way. */
static void load_after_take(uint8_t terrible) {
    uint8_t next = sel;
    if (!terrible) {
        do { next = (uint8_t)(next + 1u); if (next >= FREE_IDX) next = PIECE_BASE; } while (!pieces[next - PIECE_BASE]);
    }
    if (next != sel) {
        sheet_tempo[sel] = tempo;   /* the old sheet keeps its tempo */
        if (next == (uint8_t)(FREE_IDX - 1u)) { drill_seed++; regen_drill(); }
        sel = next;
        set.sel = sel;   /* the selection persists, as with SELECT */
        set_dirty = 1;
        setup_piece();
        tempo = pl->tempo;   /* a fresh piece starts at its own tempo */
        sheet_tempo[sel] = tempo;
    } else {
        clear_sheet_take(sel);   /* the terrible retry starts clean */
    }
    timestamp = start_x(); timestamp_frac = 0;
    sheet_ts[sel] = timestamp;
    pb_idx = sheet_notes_begin(sel);
    reset_take_stats();
    render_window(scroll_now());
    show_hint();
}

/* ---- the song's part flow (v0.26) ----
 * The assembly advances one part at a time. cue_part loads a
 * part's guide and parks at its first note with a fresh judge;
 * completed parts' takes stay in the sheet and play as backing,
 * and the cued part's own take (if any) stays too -- re-kicking
 * it off replaces it (kickoff_press), and its retry clears it
 * (song_finish_replay). A part with no guide events (harmony,
 * in v0.26) is skipped by the order: it exists, but there is
 * nothing to learn yet. */
static int8_t first_part(void) {
    int8_t p;
    for (p = 0; p < (int8_t)ode_song.part_count; p++)
        if (ode_song.parts[p].event_count) return p;
    return 0;
}
static int8_t next_part(int8_t from) {
    int8_t p;
    for (p = (int8_t)(from + 1); p < (int8_t)ode_song.part_count; p++)
        if (ode_song.parts[p].event_count) return p;
    return -1;
}
static void cue_part(uint8_t p) {
    song_part = p;
    setup_piece();
    timestamp = start_x(); timestamp_frac = 0;
    sheet_ts[sel] = timestamp;
    pb_idx = sheet_notes_begin(sel);
    reset_take_stats();
    render_window(scroll_now());
    show_hint();
}

/* every part has a take: play the whole assembly once -- all
 * parts together, through the one sound path -- then park back
 * at the first part, ready to re-play it or re-learn any part.
 * It is a replay in mechanics (a key press skips it), but
 * nothing is inspected: no amber, and at the end the song
 * re-parks instead of loading. */
static void begin_assembly(void) {
    assembly = 1;
    replaying = 1;
    timestamp = CURSOR_X; timestamp_frac = 0;   /* rewind to the song's start */
    set_velocity((uint32_t)tempo * PXQ);
    render_window(scroll_now());
    show_hint();
}
static void end_assembly(void) {
    assembly = 0;
    replaying = 0;
    set_velocity(0);
    cue_part((uint8_t)first_part());
}

/* a song part's replay ended: a terrible take reloads the same
 * part with only that part's events cleared (the band keeps
 * theirs); a good one marks the part done and cues the next --
 * or, after the last part, plays the whole assembly once. */
static void song_finish_replay(void) {
    uint8_t terrible = replay_terrible;
    int8_t next;
    replaying = 0;
    set_velocity(0);
    if (terrible) {
        clear_part_take(cur_channel());
        cue_part(song_part);
        return;
    }
    song_done[song_part] = 1;
    next = next_part(song_part);
    if (next >= 0) { cue_part((uint8_t)next); return; }
    begin_assembly();
}

static void replay_done(void);   /* below, after finish_replay */

/* a book take reached the end of the piece: play it back once for
 * inspection, then load. A take with no notes in it has nothing to
 * inspect (and nothing to hear), so it loads at once. On the song
 * the check is per part, and the load is the part flow's. */
static void begin_replay(void) {
    uint8_t terrible = take_terrible();
    set_velocity(0);
    replay_terrible = terrible;
    if (on_song() ? !part_take_has_notes(cur_channel()) : !sheet_take_has_notes(sel)) { replay_done(); return; }
    replaying = 1;
    timestamp = CURSOR_X; timestamp_frac = 0;   /* rewind to the piece's start */
    set_velocity((uint32_t)tempo * PXQ);   /* pb_idx restarts at the start */
    render_window(scroll_now());
    show_hint();
}

/* the replay ended -- played out to the end of the piece, or
 * skipped by a key press: load what the verdict says. */
static void finish_replay(void) {
    replaying = 0;
    set_velocity(0);
    load_after_take(replay_terrible);
}

/* a replay (or the assembly) ended, played out or skipped:
 * dispatch to the flow it belongs to. */
static void replay_done(void) {
    if (assembly_playing()) { end_assembly(); return; }
    if (on_song()) { song_finish_replay(); return; }
    finish_replay();
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
        if (!replaying_now()) {
            loop_wrap();          /* the jump loops to the beginning */
            render_window(scroll_now());
            show_tempo();
            return;
        }
        pb_idx++;   /* the replay plays the take once: step over the jump */
    }
    for (i = 0; i < n_sounding; ) {
        if (sounding[i].end_x <= px) {
            piano_off_p(sounding[i].pitch, sounding[i].cls);
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
        if (on_guided_sheet() && evn) { end = piece_end_x; has_end = 1; }
        else if (free_end_marked()) { end = free_end_x; has_end = 1; }
        if (has_end && px > end && !n_sounding) {
            /* a book TAKE reaching the end plays back once for
             * inspection, then loads another piece, parked
             * (v0.25); the replay reaching the end (or a key
             * press) loads it. On the song, a part's take does
             * the same per part (v0.26). Play-along (no REC) and
             * free play just stop, as before. */
            if (replaying_now()) replay_done();
            else if (on_guided_sheet() && recording()) begin_replay();
            else {
                set_velocity(0);
                render_window(scroll_now());
            }
            return;
        }
        if (!has_end && on_free_play() && !keys_down &&
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
            if (!midi_held(scrub_notes[i].wmidi)) piano_off_p(scrub_notes[i].tpitch, scrub_notes[i].cls);
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

/* enter the piece stopped (from the book), so a knob can scrub it.
 * A book piece is entered parked at its first note. */
static void enter_piece_stopped(void) {
    setup_piece();
    timestamp = start_x(); timestamp_frac = 0;
    sheet_ts[sel] = timestamp;   /* entering a piece starts at its rest position */
    velocity = 0;
    in_book = 0;
    if (on_guided_sheet()) reset_take_stats();
    render_window(scroll_now());
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

/* the REC kick-off: a book piece parked at its first note, tape
 * stopped -- pressing the first note (or every pitch of its chord,
 * in any order) starts a take: the write bit turns on, the tape
 * rolls, and the chord is recorded and counts as matched. Returns
 * 1 when this press completed the chord and kicked the take off;
 * the caller then sounds the press as a normal key press. */
static uint8_t kickoff_press(uint8_t midi) {
    uint8_t i, n;
    uint8_t pitches[8];
    for (i = 0; i < kick_need_n; i++)
        if (kick_need[i] == midi && !(kick_got_mask & (uint8_t)(1u << i))) {
            kick_got_mask |= (uint8_t)(1u << i);
            break;
        }
    if (i == kick_need_n) return 0;   /* not a new pitch of the chord */
    if (kick_got_mask != (uint8_t)((1u << kick_need_n) - 1u)) return 0;   /* chord incomplete */
    /* the take starts at the park itself, not at the near-park
     * spot the playhead happened to stop at (at_kick_park). */
    timestamp = start_x(); timestamp_frac = 0;
    if (cur_part_done()) {
        /* re-learning a finished part: its old take goes first --
         * a fresh take, like the terrible retry -- so the new
         * attempt is not accompanied by the previous one. The
         * other parts' takes are untouched and keep backing. */
        clear_part_take(cur_channel());
        song_done[song_part] = 0;
    }
    n = kick_need_n;
    for (i = 0; i < n; i++) pitches[i] = kick_need[i];
    kick_got_mask = 0;
    set_rec_write(1);
    set_velocity((uint32_t)tempo * PXQ);
    /* like the auto-start path: sound any take notes at the
     * playhead first, then write -- the recompute in rec_note_on
     * moves pb_idx past the playhead. */
    playhead_sound(timestamp, timestamp, MODE_PLAY);
    for (i = 0; i < n; i++) rec_note_on(pitches[i]);
    /* the first-note chord counts as matched: mark its events. */
    {
        uint16_t j;
        uint32_t x0 = start_x();
        for (j = 0; j < evn; j++) {
            if (pl->events[j].is_rest || evx[j] != x0 || ev_hit[j]) continue;
            ev_hit[j] = 1;
            take_matched++;
        }
    }
    return 1;
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
    /* the kick-off: a book piece parked at its first note, tape
     * stopped, and this press completed the first-note chord. */
    if (on_guided_sheet() && !in_book_view() && !moving() && kick_need_n && at_kick_park() &&
        kickoff_press(midi)) {
        ghost[k].midi = midi;
        ghost[k].x = (uint16_t)timestamp;
        ghost[k].active = 1;
        ghost[k].sounded = 0;
        playhead_sound(timestamp, timestamp, MODE_LIVE);
        render_window(scroll_now());
        return;
    }
    if (recording() && !assembly_playing()) {   /* during the assembly playback a press is a ghost only: it is a listening pass, not a take */
        if (!moving() && on_guided_sheet()) {
            /* a book piece, stopped, and no kick-off: the press is
             * a ghost only. In a book piece auto-start belongs to
             * the kick-off, so a stopped press never starts the
             * tape and never records -- not even at the park with
             * REC already armed. */
        } else {
            if (!moving()) toggle_play();   /* auto-start */
            playhead_sound(timestamp, timestamp, MODE_PLAY);
            rec_note_on(midi);
            if (on_guided_sheet() && moving()) judge_press(midi);
        }
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
    /* in a book piece, a stopped release closes nothing: stopped
     * presses recorded nothing, and a note the kick-off left open
     * keeps its default length. (Free play, and any release while
     * the tape rolls, close the note as always.) Nor does a release
     * during a replay: the take is being inspected, not edited. */
    if (recording() && !replaying_now() && (on_free_play() || moving())) rec_note_off(midi);
    ghost[k].active = 0;
    ghost[k].sounded = 0;
    if (had_ghost) render_window(scroll_now());
}

/* turn off the sound a live key made: the written pitch, routed
 * the way the key's note was routed when it sounded (on the song,
 * a bass key's sound is an octave down and a pad key's is the
 * drum). Off the song this is exactly piano_off(written). */
static void live_off(uint8_t midi) {
    uint8_t patch, tpitch;
    if (!on_song()) { piano_off(midi); return; }
    route_event(cur_channel(), midi, &patch, &tpitch);
    (void)patch;
    piano_off_p(tpitch, CLS_LIVE);
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
    if (!in_book_view()) {
        /* a book piece is restored parked at its first note, not
         * at its saved offset (v0.24); free play restores its
         * offset as always. A restored book piece starts a fresh
         * take count. */
        timestamp = on_free_play() ? sheet_ts[sel] : start_x();
        if (on_guided_sheet()) reset_take_stats();
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
        { const char *t = "Drill";
          for (i = 0; t[i]; i++) drill_lesson.title[i] = t[i];
          drill_lesson.title[i] = 0; }
        drill_lesson.level = 2; drill_lesson.tempo = 96;
        regen_drill();
        pieces[5] = &drill_lesson;
    }
    rec_init();   /* one unified list: NBOOK empty End-of-Track blocks */
    for (i = 0; i < NBOOK; i++) {
        sheet_ts[i] = CURSOR_X;
        /* a slot with no lesson (a short book) falls back to the
         * free-play tempo; the book never has a hole in practice. */
        if (i == FREE_IDX) sheet_tempo[i] = FREE_TEMPO;
        else if (i == SONG_IDX) sheet_tempo[i] = ode_song.tempo;
        else sheet_tempo[i] = pieces[i - PIECE_BASE] ? pieces[i - PIECE_BASE]->tempo : FREE_TEMPO;
        sheet_end_set[i] = 0;
        sheet_end_x[i] = 0;
    }
    mset_load(&set);
    sel = set.sel;   /* the saved book selection (clamped by the load) */
    if (sel >= NBOOK || (sel >= PIECE_BASE && sel < FREE_IDX && !pieces[sel - PIECE_BASE])) sel = 0;
    select_piece();

    /* Power on in the state the PLAY+HOME "reset" ritual used to
     * be needed to reach -- on the tape, at the persisted piece,
     * parked on its first note, stopped and ready for the kick-off
     * (Free Play at its rest position). Before v0.25 the device
     * booted into the book with the timestamp still 0 while the
     * book renders as if the timestamp were CURSOR_X, so the first
     * key press rendered at an underflowed scroll and the sheet
     * jumped 60 px to the right. */
    in_book = 0;
    timestamp = start_x(); timestamp_frac = 0;
    sheet_ts[sel] = timestamp;
    if (on_guided_sheet()) reset_take_stats();

    leds_update();  /* the panel lights follow the state */
    redraw_now();
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
                if (s == (int32_t)SONG_IDX || s == (int32_t)FREE_IDX || pieces[s - PIECE_BASE]) break;
                s += dir;
            }
            if ((uint8_t)s != sel) {
                replaying = 0;   /* switching sheets abandons a replay in progress */
                assembly = 0;
                if (!in_book_view()) save_sheet_state();
                else { sheet_tempo[sel] = tempo; sheet_end_set[sel] = free_end_set; sheet_end_x[sel] = free_end_x; }
                sel = (uint8_t)(s % NBOOK);
                setup_piece();
                restore_sheet_state();
                if (in_book_view()) render_book();
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
            if (in_book_view()) enter_piece_stopped();
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
            if (in_book_view()) enter_piece_stopped();
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
        e = fm1_enc_take(ENC_KNOB2);
        if (e) had_input = 1;
        if (e && on_song() && !in_book_view() && !moving() && !replaying_now()) {
            /* KNOB2 steps between the song's parts: back to a part
             * already played, or forward as far as the first part
             * not yet played -- the assembly order is the guide,
             * and a part joins the backing only once it has a
             * take. At the park only: a part is chosen while the
             * tape is stopped, never mid-take. (Off the song,
             * KNOB2 does nothing, as before.) */
            int8_t frontier = first_part();
            int8_t p;
            while (part_is_done((uint8_t)frontier) && next_part(frontier) >= 0) frontier = next_part(frontier);
            p = (int8_t)song_part + (e > 0 ? 1 : -1);
            while (p >= 0 && p < (int8_t)ode_song.part_count && !ode_song.parts[p].event_count)
                p = (int8_t)(p + (e > 0 ? 1 : -1));
            if (p >= first_part() && p <= frontier && p != (int8_t)song_part)
                cue_part((uint8_t)p);
        }
        /* (KNOB2 does nothing anywhere else.) */

        { uint32_t rel_; b = fm1_input_edges(&rel_); }
        if (b) had_input = 1;
        if (b & (1u << BTN_PLAY)) {
            /* PLAY toggles the velocity: stopped -> move at the tempo
             * rate, moving -> stop. It never touches recording. In
             * the book it enters the piece, then starts it. */
            if (in_book_view()) {
                setup_piece();
                timestamp = start_x(); timestamp_frac = 0;
                sheet_ts[sel] = timestamp;   /* a book piece enters parked at its first note */
                in_book = 0;
                if (on_guided_sheet()) reset_take_stats();
                render_window(scroll_now());
            }
            toggle_play();
        }
        if (b & (1u << BTN_REC)) {
            /* REC toggles the write bit: notes placed are written to
             * the take, or not. It never touches time. In the book
             * it enters the piece, then arms. */
            if (in_book_view()) {
                setup_piece();
                timestamp = start_x(); timestamp_frac = 0;
                sheet_ts[sel] = timestamp;   /* a book piece enters parked at its first note */
                in_book = 0;
                if (on_guided_sheet()) reset_take_stats();
                render_window(scroll_now());
            }
            toggle_rec();
        }
        if (b & (1u << BTN_SEQ)) {
            /* SEQ inserts a jump at the playhead (or removes it). */
            if (!in_book_view()) seq_jump();
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
            if (on_free_play() && !in_book_view()) {
                free_end_x = timestamp;
                free_end_set = 1;
                if (!moving()) render_window(scroll_now());
            }
        }
        leds_update();
        if (b & (1u << BTN_HOME)) {
            /* HOME on a book piece with a take is the manual retry
             * (v0.25): stop, clear this sheet's take, park at the
             * first note -- one press is "go again". (A replay in
             * progress is abandoned the same way.) On the song the
             * retry is per part (v0.26): only the current part's
             * events are cleared and its done mark lifted; the
             * band's takes are untouched. With an empty take, HOME is what it always was, below: on the tape,
             * go back to the book (stopping and disarming); stopped
             * at a scrolled position, rewind first; at the start,
             * go to the book. Free Play keeps those semantics
             * exactly -- its takes are never cleared by HOME. */
            if (!in_book_view() && on_guided_sheet() && (on_song() ? part_take_has_notes(cur_channel()) : !sheet_take_empty(sel))) {
                save_if_dirty();
                replaying = 0;
                assembly = 0;
                set_velocity(0);
                if (on_song()) { clear_part_take(cur_channel()); song_done[song_part] = 0; }
                else clear_sheet_take(sel);
                timestamp = start_x(); timestamp_frac = 0;
                sheet_ts[sel] = timestamp;
                pb_idx = sheet_notes_begin(sel);
                reset_take_stats();
                render_window(scroll_now());
                show_hint();
            } else if (!in_book_view() && (moving() || recording())) {
                save_if_dirty();
                setup_piece();
                timestamp = CURSOR_X; timestamp_frac = 0;
                sheet_ts[sel] = CURSOR_X;   /* HOME rewinds the sheet */
                velocity = 0; rec_write = 0;
                in_book = 1;
                render_book();
            } else if (!in_book_view() && !moving() && !recording()) {
                save_if_dirty();
                if (timestamp > start_x()) {
                    timestamp = start_x(); timestamp_frac = 0;
                    sheet_ts[sel] = timestamp;   /* HOME rewinds the sheet (to the park, in a book piece) */
                    render_window(scroll_now());
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
            if (ed && replaying_now()) replay_done();   /* a key press skips the replay: the load happens first, then the press acts in the new state */
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
                    live_off(midi);
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
