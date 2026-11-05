/* SPDX-License-Identifier: GPL-3.0-only */
/* Maestro: the screen is sheet music, the buttons are a tape recorder.
 *
 * PLAY starts the tape moving. REC starts it moving and stores what you
 * play. PLAY during REC punches out (keeps moving, stops storing); REC
 * during PLAY punches in. PLAY or REC while moving stops the tape.
 * HOME rewinds to the start; HOME at the start goes back to the book.
 * KNOB2 is speed while moving, a scrub knob while stopped.
 *
 * Rendering is positional: render_window(sc) draws whatever is at scroll
 * position sc, forward or backward. Nothing is ever "reset" on PLAY/REC;
 * they just set the tape moving and whether notes are stored.
 *
 * Blue notes are the sheet music (silent, for reading). Black notes are
 * your take. REC overwrites whatever is at the playhead (same pitch,
 * nearby position); there is no blanket erase.
 */

#include "piano_data.c"   /* sampled piano (see file header for license) */

#define NV 6u
static struct {
    volatile uint8_t sample;
    volatile uint32_t pos;
    volatile uint32_t rate;
    volatile int32_t env;
    volatile int32_t target;
    volatile uint8_t rel;
    uint8_t note;
    uint32_t age;
} voice[NV];
static volatile uint32_t ages;

static const uint32_t RATE_T[3] = { 30929u, 32768u, 34716u };

static void piano_on(uint8_t note, uint8_t vel) {
    uint32_t v, pick = 0, best = 0xFFFFFFFFu;
    int32_t si;
    if (note > 127u || !vel) return;
    for (v = 0; v < NV; v++) {
        uint32_t score = (voice[v].note == note && voice[v].env > 1000)
            ? 0u : (uint32_t)voice[v].env + 1u;
        if (score < best) { best = score; pick = v; }
    }
    si = ((int32_t)note - 54 + 1) / 3;
    if (si < 0) si = 0;
    if (si > 8) si = 8;
    voice[pick].note = note;
    voice[pick].age = ++ages;
    voice[pick].sample = (uint8_t)si;
    voice[pick].pos = 0;
    voice[pick].rate = RATE_T[(int32_t)note - (int32_t)piano_midi[si] + 1];
    voice[pick].env = 0;
    voice[pick].target = (int32_t)vel * 200;
    voice[pick].rel = 0;
}
static void piano_off(uint8_t note) {
    uint32_t v;
    for (v = 0; v < NV; v++)
        if (voice[v].note == note) voice[v].rel = 1;
}
static void app_audio(int32_t *out, uint32_t n) {
    uint32_t v, i;
    for (v = 0; v < NV; v++) {
        const int16_t *smp;
        int32_t env, target;
        uint32_t pos, rate;
        uint8_t rel;
        if (!voice[v].target && !voice[v].env) continue;
        smp = piano_samples[voice[v].sample];
        env = voice[v].env; target = voice[v].target;
        pos = voice[v].pos; rate = voice[v].rate; rel = voice[v].rel;
        for (i = 0; i < n; i++) {
            uint32_t idx;
            int32_t s0, s1, s;
            if (rel) {
                env -= (env >> 8) + 1;
                if (env <= 0) { env = 0; target = 0; break; }
            } else if (env < target) {
                env += ((target - env) >> 6) + 1;
                if (env > target) env = target;
            }
            idx = pos >> 16;
            if (idx >= PIANO_N) {
                pos -= (uint32_t)PIANO_LOOP_LEN << 16;
                idx -= PIANO_LOOP_LEN;
            }
            s0 = smp[idx];
            s1 = (idx + 1u < PIANO_N) ? smp[idx + 1u] : smp[PIANO_LOOP_START];
            s = s0 + (((s1 - s0) * (int32_t)(pos & 0xFFFFu)) >> 16);
            s = (s * env) >> 15;
            out[2u * i] += s;
            out[2u * i + 1u] += s;
            pos += rate;
        }
        voice[v].pos = pos;
        voice[v].env = env;
        voice[v].target = target;
    }
}

/* ---- pieces: the book ---- */
#define NPIECES 6u
#define FREE_IDX 6u
#define NBOOK (NPIECES + 1u)
static const lesson_t *pieces[NPIECES];  /* 0..4 lessons, 5 = generated drill */
static lesson_t drill_lesson;
static const char *free_title = "Free Play";

#define PXQ 32u      /* px per quarter note */
#define CLEF_W 80u   /* tick 0 sits here; Bravura clef (38px at x=30) ends at 68 */
#define BAR_PAD 16u  /* barline sits this far left of its tick */
#define FREE_TEMPO 72u
#define MAXEV 256
static uint32_t evx[MAXEV];   /* piece-x of each blue event */
static uint16_t evn;
static uint32_t total_ticks;
static uint32_t bar_ticks;

static void layout_piece(const lesson_t *L) {
    uint32_t t = 0;
    uint16_t i;
    for (i = 0; i < L->event_count && i < MAXEV; i++) {
        evx[i] = CLEF_W + (t * PXQ) / 96u;
        t += L->events[i].ticks;
    }
    evn = i;
    total_ticks = t;
    bar_ticks = (uint32_t)L->beats_per_bar * 96u;
    if (!bar_ticks) bar_ticks = 384u;
}
static uint32_t bar_x(uint32_t b) {
    return CLEF_W - BAR_PAD + (b * bar_ticks * PXQ) / 96u;
}

/* ---- transport: a tape recorder ---- */
#define ST_BOOK 0u
#define ST_STOP 1u   /* tape stopped, at the current position */
#define ST_PLAY 2u   /* tape moving; black notes sound at the playhead */
#define ST_REC 3u    /* tape moving; your notes overwrite at the playhead */
static uint8_t state = ST_BOOK;
static uint8_t sel = 0;
static uint8_t is_free = 0;
static uint32_t tempo = 72u;
static const lesson_t *pl;

static uint32_t scroll_px;     /* tape position */
static uint32_t scroll_frac;   /* fractional scroll accumulator */
static uint32_t last_frame_ms;
static uint32_t indic_until;

static const int32_t SPD_N[5] = {1, 3, 4, 6, 8};
static const int32_t SPD_D[5] = {2, 4, 4, 4, 4};
static const char *SPD_S[5] = {"0.5x", "0.75x", "1x", "1.5x", "2x"};

static settings_t set;
static uint8_t set_dirty = 0;
static uint32_t cursor_x(void) { return set.cursor_x; }
static uint32_t play_x(void) { return scroll_px + cursor_x(); }  /* playhead piece-x */

/* ---- the take: notes, sorted by x ---- */
#define MAXREC 512
#define PUNCH_W 8u   /* overwrite same-pitch notes within this many px */
static struct { uint16_t x; uint16_t ticks; uint8_t midi; } rec[MAXREC];
static uint16_t rec_n;

/* playback: next note to start, and notes currently sounding */
static uint16_t pb_idx;
#define MAXSOUND 8
static struct { uint8_t midi; uint32_t end_x; } sounding[MAXSOUND];
static uint8_t n_sounding;

/* punch-in: playing a note replaces same-pitch notes near the playhead */
static void rec_note_on(uint8_t midi) {
    uint16_t x = (uint16_t)play_x();
    uint16_t i, j;
    for (i = 0; i < rec_n; ) {
        if (rec[i].midi == midi && rec[i].x + PUNCH_W >= x && rec[i].x <= x + PUNCH_W) {
            for (j = i; j + 1u < rec_n; j++) rec[j] = rec[j + 1u];
            rec_n--;
        } else i++;
    }
    if (rec_n >= MAXREC) return;
    for (i = rec_n; i > 0 && rec[i - 1u].x > x; i--) rec[i] = rec[i - 1u];
    rec[i].x = x;
    rec[i].ticks = 0;  /* open; closed by the note-off */
    rec[i].midi = midi;
    rec_n++;
}
static void rec_note_off(uint8_t midi) {
    uint16_t x = (uint16_t)play_x();
    uint16_t i;
    for (i = rec_n; i-- > 0; ) {
        if (rec[i].midi == midi && !rec[i].ticks) {
            uint32_t t = (x > rec[i].x ? x - rec[i].x : 0) * 96u / PXQ;
            rec[i].ticks = t < 24u ? 24u : t > 384u ? 384u : (uint16_t)t;
            return;
        }
    }
}

/* ---- rendering: draw the window at scroll position sc ---- */
static void render_window(uint32_t sc) {
    uint32_t i, b;
    ui_clear();
    ui_plane_black();
    ui_staff_cols(0u, 240u);
    ui_clef(30u);
    if (!is_free) {
        for (b = 1; b * bar_ticks < total_ticks + bar_ticks; b++) {  /* no barline at 0; the clef marks the start */
            uint32_t bx = bar_x(b);
            if (bx < sc || bx >= sc + 240u) continue;
            ui_bar(bx - sc);
        }
        ui_plane_blue();
        for (i = 0; i < evn; i++) {
            uint32_t sx;
            if (evx[i] < sc || evx[i] >= sc + 240u) continue;
            sx = evx[i] - sc;
            if (pl->events[i].is_rest) ui_rest(sx, pl->events[i].ticks);
            else ui_note(sx, pl->events[i].midi, pl->events[i].ticks);
        }
        ui_plane_black();
    }
    for (i = 0; i < rec_n; i++) {
        uint32_t sx;
        if (rec[i].x < sc || rec[i].x >= sc + 240u) continue;
        sx = rec[i].x - sc;
        ui_note(sx, rec[i].midi, rec[i].ticks ? rec[i].ticks : 96u);
    }
    ui_blit();
    ui_cursor_mark(cursor_x());
}

static void book_title(char *dst) {
    const char *s = (sel == FREE_IDX) ? free_title : pieces[sel]->title;
    uint32_t i = 0;
    while (*s && i < 43u) dst[i++] = *s++;
    dst[i] = 0;
}

static void render_book(void) {
    char title[44];
    book_title(title);
    ui_paper();
    render_window(0u);
    ui_title(title);
    ui_hint("SELECT flip - PLAY start");
}

static void setup_piece(void) {
    is_free = (sel == FREE_IDX);
    if (is_free) {
        tempo = FREE_TEMPO;
        evn = 0;
        total_ticks = 0;
    } else {
        pl = pieces[sel];
        tempo = pl->tempo;
        layout_piece(pl);
    }
}

/* ---- transport ---- */
static void silence_playback(void) {
    uint8_t i;
    for (i = 0; i < n_sounding; i++) piano_off(sounding[i].midi);
    n_sounding = 0;
}

/* stop the tape, stay where you are */
static void tape_stop(void) {
    silence_playback();
    state = ST_STOP;
    ui_hint("PLAY resume - REC punch in");
}

/* start moving; black notes sound as the playhead reaches them */
static void tape_play(void) {
    pb_idx = 0;
    while (pb_idx < rec_n && rec[pb_idx].x < play_x()) pb_idx++;
    n_sounding = 0;
    state = ST_PLAY;
    last_frame_ms = fm1_ms;
    ui_hint("PLAY stop - REC punch in");
}

/* start moving and storing; overwrites at the playhead, never erases all */
static void tape_rec(void) {
    state = ST_REC;
    last_frame_ms = fm1_ms;
    ui_hint("REC stop - PLAY punch out");
}

/* one frame while the tape moves */
static void play_frame(void) {
    uint32_t now = fm1_ms;
    uint32_t dt, num, den, px, i, j;
    dt = now - last_frame_ms;
    last_frame_ms = now;
    /* advance scroll: 32-bit safe (dt<1000, tempo<256) */
    num = dt * tempo * PXQ * (uint32_t)SPD_N[set.speed_idx];
    den = 60000u * (uint32_t)SPD_D[set.speed_idx];
    scroll_px += num / den;
    scroll_frac += num % den;
    if (scroll_frac >= den) { scroll_px += scroll_frac / den; scroll_frac %= den; }
    px = play_x();
    if (state == ST_PLAY) {
        /* sound black notes as the playhead reaches them */
        while (pb_idx < rec_n && rec[pb_idx].x <= px) {
            uint8_t m = rec[pb_idx].midi;
            uint32_t dur = (rec[pb_idx].ticks ? rec[pb_idx].ticks : 96u) * PXQ / 96u;
            piano_on(m, 100);
            if (n_sounding < MAXSOUND) {
                sounding[n_sounding].midi = m;
                sounding[n_sounding].end_x = rec[pb_idx].x + dur;
                n_sounding++;
            }
            pb_idx++;
        }
        for (i = 0; i < n_sounding; ) {
            if (sounding[i].end_x <= px) {
                piano_off(sounding[i].midi);
                for (j = i; j + 1u < n_sounding; j++) sounding[j] = sounding[j + 1u];
                n_sounding--;
            } else i++;
        }
        /* end of the piece: stop the tape */
        if (!is_free && evn && px > evx[evn - 1u] + 240u) {
            tape_stop();
            render_window(scroll_px);
            return;
        }
        if (is_free && rec_n > 0 && pb_idx >= rec_n && !n_sounding &&
            px > rec[rec_n - 1u].x + 240u) {
            tape_stop();
            render_window(scroll_px);
            return;
        }
    }
    render_window(scroll_px);
    if ((int32_t)(now - indic_until) < 0)
        ui_indic(SPD_S[set.speed_idx]);
}

/* live key press. REC: punch in. Otherwise: sounds only. */
static void on_key(uint8_t midi) {
    if (state != ST_REC) return;
    rec_note_on(midi);
    render_window(scroll_px);
}
static void on_key_up(uint8_t midi) {
    if (state == ST_REC) rec_note_off(midi);
}

/* ---- main ---- */
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
    mset_load(&set);
    if (sel >= NBOOK) sel = 0;
    setup_piece();

    render_book();

    for (;;) {
        uint32_t keys, b;
        int32_t e;
        platform_poll();

        e = fm1_enc_take(ENC_SELECT);
        if (e && state == ST_BOOK) {
            int32_t s = (int32_t)sel + e;
            while (s < 0) s += NBOOK;
            sel = (uint8_t)(s % NBOOK);
            rec_n = 0;  /* new piece, new tape */
            setup_piece();
            render_book();
            set_dirty = 1;
        }
        e = fm1_enc_take(ENC_KNOB1);
        if (e) {
            int32_t c = (int32_t)set.cursor_x + e * 3;
            if (c < 30) c = 30; if (c > 180) c = 180;
            if (c != (int32_t)set.cursor_x) {
                set.cursor_x = (uint8_t)c; set_dirty = 1;
                if (state == ST_BOOK || state == ST_STOP) {
                    render_window(state == ST_BOOK ? 0u : scroll_px);
                }
            }
        }
        e = fm1_enc_take(ENC_KNOB2);
        if (e) {
            if (state == ST_STOP) {
                /* scrub the tape */
                int32_t s = (int32_t)scroll_px + e * 4;
                if (s < 0) s = 0;
                scroll_px = (uint32_t)s;
                render_window(scroll_px);
            } else {
                int32_t s = (int32_t)set.speed_idx + e;
                if (s < 0) s = 0; if (s > 4) s = 4;
                if (s != (int32_t)set.speed_idx) {
                    set.speed_idx = (uint8_t)s; set_dirty = 1;
                    indic_until = fm1_ms + 1200u;
                }
            }
        }

        { uint32_t rel_; b = fm1_input_edges(&rel_); }
        if (b & (1u << BTN_PLAY)) {
            if (state == ST_BOOK) {
                setup_piece();
                scroll_px = 0; scroll_frac = 0;
                render_window(0u);
                tape_play();
            } else if (state == ST_STOP) {
                tape_play();
            } else if (state == ST_PLAY) {
                tape_stop();
            } else if (state == ST_REC) {
                tape_play();  /* punch out, keep moving */
            }
        }
        if (b & (1u << BTN_REC)) {
            if (state == ST_BOOK) {
                setup_piece();
                scroll_px = 0; scroll_frac = 0;
                render_window(0u);
                tape_rec();
            } else if (state == ST_STOP) {
                tape_rec();
            } else if (state == ST_PLAY) {
                tape_rec();  /* punch in, keep moving */
            } else if (state == ST_REC) {
                tape_stop();
            }
        }
        if (b & (1u << BTN_HOME)) {
            if (state == ST_PLAY || state == ST_REC) {
                save_if_dirty();
                setup_piece();
                state = ST_BOOK;
                render_book();
            } else if (state == ST_STOP) {
                save_if_dirty();
                if (scroll_px > 0) {
                    scroll_px = 0; scroll_frac = 0;
                    render_window(0u);
                } else {
                    setup_piece();
                    state = ST_BOOK;
                    render_book();
                }
            }
        }

        keys = fm1_in.notes;
        {
            uint32_t ed = keys & ~prev_keys;
            prev_keys = keys;
            for (k = 0; k < 27u; k++)
                if (ed >> k & 1u) {
                    uint8_t midi = (uint8_t)(53u + k);
                    piano_on(midi, 100);
                    midi_send(0x90u, midi, 100);
                    on_key(midi);
                }
        }
        {
            static uint32_t sounding_keys = 0;
            uint32_t off = sounding_keys & ~keys;
            sounding_keys = keys;
            for (k = 0; k < 27u; k++)
                if (off >> k & 1u) {
                    uint8_t midi = (uint8_t)(53u + k);
                    piano_off(midi);
                    midi_send(0x80u, midi, 0);
                    on_key_up(midi);
                }
        }

        if ((state == ST_PLAY || state == ST_REC) &&
            fm1_ms - last_frame_ms >= 33u) {
            play_frame();
        }

        if (set_dirty && fm1_ms - last_save_ms > 10000u) {
            save_if_dirty(); last_save_ms = fm1_ms;
        }
    }
}
