/* SPDX-License-Identifier: GPL-3.0-only */
/* Maestro v0.1: the screen is sheet music.
 *
 * Diegetic UI: the device boots into a book of pieces. Turn SELECT to flip
 * through them, PLAY to play. The staff scrolls right-to-left under a fixed
 * cursor; your played notes are drawn on the staff in green (right) or red
 * (wrong) so accuracy is visible where it happened. KNOB1 moves the cursor
 * (left = more anticipation), KNOB2 sets the scroll speed. No menus.
 */

/* ---- piano voice: one wavetable cycle (sine + 2nd + 3rd harmonic) ---- */
#include "piano_data.c"   /* sampled piano (see file header for license) */

#define NV 6u
static struct {
    volatile uint8_t sample;   /* which piano sample */
    volatile uint32_t pos;     /* 16.16 fixed-point sample position */
    volatile uint32_t rate;    /* 16.16 fixed-point step per output sample */
    volatile int32_t env;      /* current amplitude */
    volatile int32_t target;   /* attack target amplitude */
    volatile uint8_t rel;      /* releasing (damper) */
    uint8_t note;
    uint32_t age;
} voice[NV];
static volatile uint32_t ages;

/* playback rate for 22050 Hz samples at 44100 Hz output, offset -1/0/+1 semitone */
static const uint32_t RATE_T[3] = { 30929u, 32768u, 34716u };

static void piano_on(uint8_t note, uint8_t vel) {
    uint32_t v, pick = 0, best = 0xFFFFFFFFu;
    int32_t si;
    if (note > 127u || !vel) return;
    for (v = 0; v < NV; v++) {
        /* same ringing note wins; otherwise the quietest voice */
        uint32_t score = (voice[v].note == note && voice[v].env > 1000)
            ? 0u : (uint32_t)voice[v].env + 1u;
        if (score < best) { best = score; pick = v; }
    }
    /* nearest sample in minor thirds (samples at 54,57,...,78) */
    si = ((int32_t)note - 54 + 1) / 3;
    if (si < 0) si = 0;
    if (si > 8) si = 8;
    voice[pick].note = note;
    voice[pick].age = ++ages;
    voice[pick].sample = (uint8_t)si;
    voice[pick].pos = 0;
    voice[pick].rate = RATE_T[(int32_t)note - (int32_t)piano_midi[si] + 1];
    voice[pick].env = 0;
    voice[pick].target = (int32_t)vel * 200;   /* 127 -> 25400 */
    voice[pick].rel = 0;
}
static void piano_off(uint8_t note) {
    uint32_t v;
    for (v = 0; v < NV; v++)
        if (voice[v].note == note) voice[v].rel = 1;  /* damper: fast fade */
}
/* audio ISR: keep it short, no flash, no screen */
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
            if (rel) {                       /* damper release */
                env -= (env >> 8) + 1;
                if (env <= 0) { env = 0; target = 0; break; }
            } else if (env < target) {       /* click-free attack */
                env += ((target - env) >> 6) + 1;
                if (env > target) env = target;
            }
            idx = pos >> 16;
            if (idx >= PIANO_N) {            /* sustain loop */
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
/* ---- pieces: the book ---- */
#define NPIECES 6u
#define FREE_IDX 6u        /* book item: free play (no suggested notes) */
#define NBOOK (NPIECES + 1u)
static const lesson_t *pieces[NPIECES];
static lesson_t drill_lesson;
static const char *drill_title = "Drill";
static const char *free_title = "Free Play";

#define PXQ 32u   /* px per quarter note: airy, classy */
#define CLEF_W 64u   /* px reserved for the clef before tick 0 */
#define BAR_PAD 16u  /* barline sits this far left of its tick */
#define FREE_TEMPO 72u
/* piece-x of event i (240 = right edge at scroll 0) */
#define MAXEV 256
static uint32_t evx[MAXEV];
static uint16_t evn;
static uint32_t total_ticks;
static uint32_t bar_ticks;   /* ticks per bar */

static void layout_piece(const lesson_t *L) {
    uint32_t t = 0;
    uint16_t i;
    for (i = 0; i < L->event_count && i < MAXEV; i++) {
        evx[i] = 240u + CLEF_W + (t * PXQ) / 96u;
        t += L->events[i].ticks;
    }
    evn = i;
    total_ticks = t;
    bar_ticks = (uint32_t)L->beats_per_bar * 96u;
    if (!bar_ticks) bar_ticks = 384u;
}
static uint32_t bar_x(uint32_t b) {
    return 240u + CLEF_W - BAR_PAD + (b * bar_ticks * PXQ) / 96u;
}

/* ---- state ----
 * Blue notes are sheet music: silent, for reading. Only you make sound.
 * Black notes are your writing: they sound on playback.
 *   PLAY (practice): blue scrolls silent; you play along (not recorded).
 *   REC:             blue scrolls silent (guide); you play, black recorded.
 *   READY:           your take as black notation on the staff.
 *   PLAYBACK:        black notation performs.
 */
#define ST_BOOK 0u
#define ST_PLAY 1u      /* practice: blue silent, you play (not recorded) */
#define ST_REC 2u
#define ST_READY 3u
#define ST_PLAYBACK 4u
#define ST_PAUSE 5u
static uint8_t state = ST_BOOK;
static uint8_t pause_resume;
static uint8_t sel = 0;
static uint8_t is_free = 0;      /* free play: no suggested notes */
static uint32_t tempo = 72u;     /* bpm: piece tempo or FREE_TEMPO */
static const lesson_t *pl;

static uint32_t play_ms;         /* playhead, ms since start */
static uint32_t last_frame_ms;

/* recording: what you played (ms, midi, on/off) */
#define MAXREC 512
static struct { uint32_t ms; uint16_t x; uint8_t midi; uint8_t on; } rec[MAXREC];
static uint16_t rec_n;
static uint16_t pb_idx;          /* next rec event during playback */
static uint8_t rec_on;           /* record live key events */

static uint32_t indic_until;     /* speed indicator timeout */

static const int32_t SPD_N[5] = {1, 3, 4, 6, 8};
static const int32_t SPD_D[5] = {2, 4, 4, 4, 4};
static const char *SPD_S[5] = {"0.5x", "0.75x", "1x", "1.5x", "2x"};

static settings_t set;
static uint8_t set_dirty = 0;
static uint32_t cursor_x(void) { return set.cursor_x; }

static uint32_t scroll_f;      /* integer scroll px */
static uint32_t scroll_rem;      /* fractional accumulator */
static uint32_t last_scroll;

static void rec_event(uint8_t midi, uint8_t on) {
    if (!rec_on || rec_n >= MAXREC) return;
    rec[rec_n].ms = play_ms;
    rec[rec_n].x = (uint16_t)(last_scroll + cursor_x());  /* absolute piece-x */
    rec[rec_n].midi = midi;
    rec[rec_n].on = on;
    rec_n++;
}

/* live feedback: your note in black at the cursor */
static void draw_played(uint8_t midi) {
    ui_plane_black();
    ui_note(cursor_x(), midi, 96u);
}

/* recorded black notes, drawn as notation scrolling in (not popping in).
   x was stored at record time (absolute piece-x), so alignment is exact. */
static void draw_rec_cols(uint32_t sc, uint32_t dx) {
    uint32_t x0 = dx >= 240u ? 0u : 240u - dx;
    uint16_t i, j;
    ui_plane_black();
    for (i = 0; i < rec_n; i++) {
        uint32_t sx, ticks = 96u;
        if (!rec[i].on) continue;
        if (rec[i].x < sc) continue;
        sx = rec[i].x - sc;
        if (sx >= 240u || sx + 48u < x0) continue;
        /* duration from the matching note-off */
        for (j = i + 1u; j < rec_n; j++) {
            if (!rec[j].on && rec[j].midi == rec[i].midi) {
                uint32_t dms = rec[j].ms > rec[i].ms ? rec[j].ms - rec[i].ms : 0;
                ticks = (dms * tempo * 96u) / 60000u;
                if (ticks < 24u) ticks = 24u;
                if (ticks > 384u) ticks = 384u;
                break;
            }
        }
        ui_note(sx, rec[i].midi, ticks);
    }
}

/* ---- rendering ---- */
static void draw_new_cols(uint32_t sc, uint32_t dx) {
    uint32_t x0 = dx >= 240u ? 0u : 240u - dx, i, b;
    ui_plane_black();
    ui_staff_cols(x0, 240u);
    if (sc <= 240u) {
        uint32_t sx = 240u - sc;
        if (sx < 240u && sx + 48u >= x0) ui_clef(sx);
    }
    if (!is_free) {
        for (b = 0; b * bar_ticks < total_ticks + bar_ticks; b++) {
            uint32_t bx = bar_x(b), sx;
            if (bx < sc) continue;
            sx = bx - sc;
            if (sx >= 240u || sx + 8u < x0) continue;
            ui_bar(sx);
        }
    }
    if (!is_free) {
        ui_plane_blue();
        for (i = 0; i < evn; i++) {
            uint32_t sx;
            if (evx[i] < sc) continue;
            sx = evx[i] - sc;
            if (sx >= 240u || sx + 48u < x0) continue;
            if (pl->events[i].is_rest) ui_rest(sx, pl->events[i].ticks);
            else ui_note(sx, pl->events[i].midi, pl->events[i].ticks);
        }
    }
    /* your recorded take as black notation (if any) */
    if (rec_n > 0) draw_rec_cols(sc, dx);
    ui_plane_black();
}

static void book_title(char *dst) {
    const char *s;
    uint32_t i = 0;
    if (sel == FREE_IDX) s = free_title;
    else if (sel == 5u) s = drill_title;
    else s = pieces[sel]->title;
    while (*s && i < 43u) dst[i++] = *s++;
    dst[i] = 0;
}

static void render_book(void) {
    char title[44];
    book_title(title);
    ui_paper();
    ui_clear();
    ui_plane_black();
    ui_staff_cols(0u, 240u);
    ui_clef(30u);
    if (sel != FREE_IDX && sel != 5u) {
        /* show the first notes in blue */
        const lesson_t *L = pieces[sel];
        uint32_t t = 0, x;
        uint16_t i;
        ui_plane_blue();
        for (i = 0; i < L->event_count; i++) {
            x = 80u + (t * PXQ) / 96u;
            if (x > 232u) break;
            if (L->events[i].is_rest) ui_rest(x, L->events[i].ticks);
            else ui_note(x, L->events[i].midi, L->events[i].ticks);
            t += L->events[i].ticks;
        }
        ui_plane_black();
    }
    ui_blit();
    ui_title(title);
    ui_hint("SELECT flip - PLAY start");
    ui_cursor_mark(cursor_x());
}

/* staff is already set up (HOME/BOOK). Just wipe the planes and redraw the
   bare staff+clef (no flash, no scrolling clef). Notes scroll in via play_frame. */
static void show_start(const char *title) {
    ui_title(title);
    ui_clear();
    ui_plane_black();
    ui_staff_cols(0u, 240u);
    ui_clef(30u);
    if (rec_n > 0) draw_rec_cols(0u, 240u);  /* black notes already on-screen */
    ui_blit();
    ui_cursor_mark(cursor_x());
}

/* rewind to the beginning (keep the recording) */
static void rewind_start(void) {
    play_ms = 0;
    scroll_f = 0; scroll_rem = 0; last_scroll = 0;
    pb_idx = 0;
    rec_on = 0;
    show_start(is_free ? free_title : pl->title);
    ui_hint("PLAY: hear take - REC: new take");
    state = ST_READY;
}

static void setup_piece(void) {
    is_free = (sel == FREE_IDX);
    if (is_free) {
        tempo = FREE_TEMPO;
        evn = 0;
        total_ticks = 0;
    } else {
        pl = (sel == 5u) ? &drill_lesson : pieces[sel];
        tempo = pl->tempo;
        layout_piece(pl);
    }
}


/* PLAY from book: practice (blue scrolls silent, you play along) */
static void start_play(void) {
    setup_piece();
    play_ms = 0;
    scroll_f = 0; scroll_rem = 0; last_scroll = 0;
    rec_on = 0;
    show_start(is_free ? free_title : pl->title);
    state = ST_PLAY;
    last_frame_ms = fm1_ms;
}

/* REC: tape rolls from the start (erases previous take) */
static void start_rec(void) {
    setup_piece();
    play_ms = 0;
    scroll_f = 0; scroll_rem = 0; last_scroll = 0;
    rec_n = 0;
    rec_on = 1;
    show_start(is_free ? free_title : pl->title);
    state = ST_REC;
    last_frame_ms = fm1_ms;
}

/* one frame while the playhead advances */
static void play_frame(void) {
    uint32_t now = fm1_ms;
    uint32_t dt, num, den, sc, dx;
    dt = now - last_frame_ms;
    last_frame_ms = now;
    play_ms += dt;
    /* advance scroll: 32-bit safe (dt<1000, tempo<256) */
    num = dt * tempo * PXQ * (uint32_t)SPD_N[set.speed_idx];
    den = 60000u * (uint32_t)SPD_D[set.speed_idx];
    scroll_f += num / den;
    scroll_rem += num % den;
    if (scroll_rem >= den) { scroll_f += scroll_rem / den; scroll_rem %= den; }
    sc = scroll_f;
    dx = sc - last_scroll;
    if (dx) {
        if (dx > 240u) dx = 240u;
        ui_scroll_px(dx);
        draw_new_cols(sc, dx);
        last_scroll = sc;
    }
    if (state == ST_PLAYBACK) {
        /* black notes perform: sound them as the playhead passes.
           They're already on the staff (drawn by draw_rec_cols), no popping in. */
        while (pb_idx < rec_n && rec[pb_idx].ms <= play_ms) {
            uint8_t midi = rec[pb_idx].midi;
            if (rec[pb_idx].on) piano_on(midi, 100);
            else piano_off(midi);
            pb_idx++;
        }
        if (pb_idx >= rec_n && rec_n && play_ms > rec[rec_n - 1u].ms + 2000u) {
            pause_resume = ST_PLAYBACK;
            state = ST_PAUSE;
            return;
        }
    } else if (!is_free && evn && sc > evx[evn - 1u] + 240u) {
        /* piece scrolled through (practice or rec) */
        if (state == ST_PLAY) { pause_resume = ST_PLAY; state = ST_PAUSE; return; }
    }
    /* ST_PLAY: practice (blue silent, you play). ST_REC: tape rolls. */
    ui_blit();
    ui_cursor_mark(cursor_x());
    if ((int32_t)(now - indic_until) < 0)
        ui_indic(SPD_S[set.speed_idx]);
}

/* live key press. PLAY/REC: drawn black at cursor. REC: also recorded. */
static void on_key(uint8_t midi) {
    if (state != ST_PLAY && state != ST_REC) return;
    draw_played(midi);
    if (state == ST_REC) rec_event(midi, 1);
}
static void on_key_up(uint8_t midi) {
    if (state == ST_REC) rec_event(midi, 0);
}

/* ---- main ---- */
static void save_if_dirty(void) {
    if (set_dirty) { mset_save(&set); set_dirty = 0; }
}

static void app_main(void) {
    uint32_t prev_keys = 0, last_save_ms = 0, k;
    uint32_t i;
    /* pieces */
    for (i = 0; i < 5u && i < (uint32_t)all_lessons_count; i++) pieces[i] = all_lessons[i];
    {
        drill_params_t dp;
        dp.position_midi = 60; dp.range = 7; dp.tempo = 96;
        dp.bars = 4; dp.beats_per_bar = 4;
        dp.rhythm_mask = RHY_Q | RHY_H | RHY_E;
        dp.allow_rests = 1; dp.seed = 7;
        for (i = 0; i < LESSON_TITLE_LEN; i++) drill_lesson.title[i] = 0;
        drill_lesson.title[0] = 'D'; drill_lesson.title[1] = 'r';
        drill_lesson.title[2] = 'i'; drill_lesson.title[3] = 'l';
        drill_lesson.title[4] = 'l'; drill_lesson.title[5] = 0;
        drill_lesson.level = 2; drill_lesson.tempo = 96;
        drill_lesson.event_count = drill_generate(&dp, drill_lesson.events, LESSON_MAX_EVENTS);
        pieces[5] = &drill_lesson;
    }
    mset_load(&set);
    if (sel >= NBOOK) sel = 0;

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
            set_dirty = 1;
            render_book();
        }
        e = fm1_enc_take(ENC_KNOB1);
        if (e) {
            int32_t c = (int32_t)set.cursor_x + e * 3;
            if (c < 30) c = 30; if (c > 180) c = 180;
            if (c != (int32_t)set.cursor_x) {
                set.cursor_x = (uint8_t)c; set_dirty = 1;
                if (state == ST_BOOK || state == ST_READY) {
                    ui_blit();
                    ui_cursor_mark(cursor_x());
                }
            }
        }
        e = fm1_enc_take(ENC_KNOB2);
        if (e) {
            int32_t s = (int32_t)set.speed_idx + e;
            if (s < 0) s = 0; if (s > 4) s = 4;
            if (s != (int32_t)set.speed_idx) {
                set.speed_idx = (uint8_t)s; set_dirty = 1;
                indic_until = fm1_ms + 1200u;
            }
        }

        { uint32_t rel_; b = fm1_input_edges(&rel_); }
        if (b & (1u << BTN_PLAY)) {
            if (state == ST_BOOK) {
                start_play();  /* practice: blue silent, you play */
            } else if (state == ST_PLAY || state == ST_PLAYBACK) {
                pause_resume = state;
                state = ST_PAUSE;
            } else if (state == ST_PAUSE) {
                state = pause_resume;
                last_frame_ms = fm1_ms;
            } else if (state == ST_READY) {
                if (rec_n > 0) {  /* PLAY: hear the take */
                    pb_idx = 0;
                    rec_on = 0;
                    state = ST_PLAYBACK;
                    last_frame_ms = fm1_ms;
                }
            } else if (state == ST_REC) {
                rewind_start();  /* PLAY stops the take */
            }
        }
        if (b & (1u << BTN_REC)) {
            if (state == ST_BOOK || state == ST_READY || state == ST_PLAYBACK) {
                start_rec();  /* REC erases the tape and rolls */
            } else if (state == ST_REC) {
                rewind_start();  /* REC stops the take */
            }
        }
        if (b & (1u << BTN_HOME)) {
            if (state == ST_PLAY || state == ST_REC) {
                save_if_dirty();
                state = ST_BOOK;
                render_book();
            } else if (state == ST_PLAYBACK || state == ST_PAUSE || state == ST_READY) {
                save_if_dirty();
                rewind_start();
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
            static uint32_t sounding = 0;
            uint32_t off = sounding & ~keys;
            sounding = keys;
            for (k = 0; k < 27u; k++)
                if (off >> k & 1u) {
                    uint8_t midi = (uint8_t)(53u + k);
                    piano_off(midi);
                    midi_send(0x80u, midi, 0);
                    on_key_up(midi);
                }
        }

        if ((state == ST_PLAY || state == ST_REC || state == ST_PLAYBACK) &&
            fm1_ms - last_frame_ms >= 33u) {
            play_frame();
        }

        if (set_dirty && fm1_ms - last_save_ms > 10000u) {
            save_if_dirty(); last_save_ms = fm1_ms;
        }
    }
}
