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
static const int16_t WT[256] = {
0,1263,2523,3777,5021,6251,7466,8662,9836,10985,12106,13197,
14255,15277,16262,17207,18110,18969,19783,20550,21269,21939,22558,23126,
23643,24108,24520,24880,25189,25445,25651,25806,25912,25969,25979,25943,
25863,25739,25575,25372,25131,24855,24546,24207,23838,23444,23026,22586,
22127,21652,21162,20661,20149,19631,19108,18582,18055,17529,17007,16491,
15981,15480,14989,14509,14043,13591,13153,12732,12328,11941,11572,11221,
10889,10576,10281,10005,9747,9507,9284,9078,8887,8713,8552,8405,
8270,8147,8033,7929,7832,7742,7657,7577,7498,7422,7345,7268,
7188,7105,7017,6924,6825,6718,6602,6478,6344,6200,6045,5879,
5701,5511,5309,5096,4870,4633,4384,4124,3853,3571,3280,2980,
2671,2354,2031,1702,1368,1029,688,344,0,-344,-688,-1029,
-1368,-1702,-2031,-2354,-2671,-2980,-3280,-3571,-3853,-4124,-4384,-4633,
-4870,-5096,-5309,-5511,-5701,-5879,-6045,-6200,-6344,-6478,-6602,-6718,
-6825,-6924,-7017,-7105,-7188,-7268,-7345,-7422,-7498,-7577,-7657,-7742,
-7832,-7929,-8033,-8147,-8270,-8405,-8552,-8713,-8887,-9078,-9284,-9507,
-9747,-10005,-10281,-10576,-10889,-11221,-11572,-11941,-12328,-12732,-13153,-13591,
-14043,-14509,-14989,-15480,-15981,-16491,-17007,-17529,-18055,-18582,-19108,-19631,
-20149,-20661,-21162,-21652,-22127,-22586,-23026,-23444,-23838,-24207,-24546,-24855,
-25131,-25372,-25575,-25739,-25863,-25943,-25979,-25969,-25912,-25806,-25651,-25445,
-25189,-24880,-24520,-24108,-23643,-23126,-22558,-21939,-21269,-20550,-19783,-18969,
-18110,-17207,-16262,-15277,-14255,-13197,-12106,-10985,-9836,-8662,-7466,-6251,
-5021,-3777,-2523,-1263
};

#define NV 6u
static struct {
    volatile uint32_t inc;
    uint32_t ph;
    volatile int32_t env;      /* Q15-ish amplitude, decays exponentially */
    uint8_t note;
    uint32_t age;
} voice[NV];
static volatile uint32_t ages;

static const uint32_t INC_C9[12] = {
    815363807u, 863847862u, 915214929u, 969636441u, 1027294024u, 1088380105u,
    1153098554u, 1221665363u, 1294309365u, 1371273005u, 1452813141u, 1539201906u};

static void piano_on(uint8_t note, uint8_t vel) {
    uint32_t v, pick = 0, best = 0xFFFFFFFFu;
    if (note > 127u || !vel) return;
    for (v = 0; v < NV; v++) {
        /* same ringing note wins; otherwise the quietest voice */
        uint32_t score = (voice[v].note == note && voice[v].env > 1000)
            ? 0u : (uint32_t)voice[v].env + 1u;
        if (score < best) { best = score; pick = v; }
    }
    voice[pick].note = note;
    voice[pick].age = ++ages;
    voice[pick].inc = INC_C9[note % 12u] >> (10u - note / 12u);
    voice[pick].ph = 0;
    voice[pick].env = (int32_t)vel * 200;   /* 127 -> 25400 */
}
static void piano_off(uint8_t note) {
    uint32_t v;
    for (v = 0; v < NV; v++)
        if (voice[v].note == note) voice[v].env >>= 3;  /* damper */
}
/* audio ISR: keep it short, no flash, no screen */
static void app_audio(int32_t *out, uint32_t n) {
    uint32_t v, i;
    for (v = 0; v < NV; v++) {
        int32_t env = voice[v].env;
        uint32_t ph = voice[v].ph, step = voice[v].inc;
        if (env <= 0) continue;
        for (i = 0; i < n; i++) {
            int32_t s;
            env -= (env >> 14) + 1;         /* exponential decay */
            if (env < 0) env = 0;
            ph += step;
            s = WT[(ph >> 24) & 0xFFu];
            s = (s * env) >> 15;
            out[2u * i] += s;
            out[2u * i + 1u] += s;
            if (!env) break;
        }
        voice[v].ph = ph;
        voice[v].env = env;
    }
}

/* ---- pieces: the book ---- */
#define NPIECES 6u
static const lesson_t *pieces[NPIECES];
static lesson_t drill_lesson;
static const char *drill_title = "Drill";

#define PXQ 20u   /* px per quarter note */
/* piece-x of event i (240 = right edge at scroll 0) */
#define MAXEV 256
static uint32_t evx[MAXEV];
static uint16_t evn;

static void layout_piece(const lesson_t *L) {
    uint32_t t = 0;
    uint16_t i;
    for (i = 0; i < L->event_count && i < MAXEV; i++) {
        evx[i] = 240u + (t * PXQ) / 96u;
        t += L->events[i].ticks;
    }
    evn = i;
}
static int32_t tick_of(uint16_t i) {
    return ((int32_t)evx[i] - 240) * 96 / (int32_t)PXQ;
}

/* ---- app state ---- */
#define ST_BOOK 0u
#define ST_PLAY 1u
static uint8_t state = ST_BOOK;
static uint8_t sel = 0;
static uint8_t piece_idx = 0;      /* piece being played (for stars) */
static const lesson_t *pl;
static settings_t set;
static uint8_t set_dirty = 0;

static uint32_t scroll_f;          /* 1/16 px */
static uint32_t scroll_rem;          /* fractional scroll accumulator */
static uint32_t last_scroll;
static uint32_t last_frame_ms;
static uint16_t next_ev;
static grade_t gr;
static uint8_t tally = 0;          /* piece finished, tally shown */

static struct { int32_t midi; uint32_t color; uint32_t until; } ov[8];
static uint32_t indic_until;       /* speed indicator timeout */

static const int32_t SPD_N[5] = {1, 3, 4, 6, 8};
static const int32_t SPD_D[5] = {2, 4, 4, 4, 4};
static const char *SPD_S[5] = {"0.5x", "0.75x", "1x", "1.5x", "2x"};

static uint32_t cursor_x(void) { return set.cursor_x; }

static void overlay(int32_t midi, uint32_t color) {
    uint32_t i;
    for (i = 0; i < 8u; i++)
        if (!ov[i].until || (int32_t)(fm1_ms - ov[i].until) >= 0) {
            ov[i].midi = midi; ov[i].color = color;
            ov[i].until = fm1_ms + 700u;
            return;
        }
}

/* ---- rendering ---- */
static void draw_new_cols(uint32_t sc, uint32_t dx) {
    uint32_t x0 = dx >= 240u ? 0u : 240u - dx, i;
    ui_staff_cols(x0, 240u);
    for (i = 0; i < evn; i++) {
        uint32_t sx;
        if (evx[i] < sc) continue;
        sx = evx[i] - sc;
        if (sx >= 240u || sx + 48u < x0) continue;
        if (pl->events[i].is_rest) ui_rest(sx, pl->events[i].ticks);
        else ui_note(sx, pl->events[i].midi, pl->events[i].ticks);
    }
}

static void book_title(char *dst) {
    const char *t = sel < 5u ? pieces[sel]->title : drill_title;
    uint32_t i = 0;
    while (t[i] && i < 40u) { dst[i] = t[i]; i++; }
    dst[i] = 0;
}

static void render_book(void) {
    char title[44];
    uint32_t t = 0, x;
    uint16_t i;
    const lesson_t *L = pieces[sel];
    book_title(title);
    ui_clear();
    ui_staff_cols(0u, 240u);
    for (i = 0; i < L->event_count; i++) {
        x = 64u + (t * PXQ) / 96u;
        if (x > 232u) break;
        if (L->events[i].is_rest) ui_rest(x, L->events[i].ticks);
        else ui_note(x, L->events[i].midi, L->events[i].ticks);
        t += L->events[i].ticks;
    }
    ui_blit();
    ui_title(title);
    ui_hint("SELECT flip - PLAY start");
}

static void start_play(void) {
    uint32_t i;
    piece_idx = sel;
    pl = pieces[sel];
    layout_piece(pl);
    scroll_f = 0; scroll_rem = 0; last_scroll = 0;
    next_ev = 0;
    grade_init(&gr);
    tally = 0;
    for (i = 0; i < 8u; i++) ov[i].until = 0;
    ui_clear();
    ui_staff_cols(0u, 240u);
    state = ST_PLAY;
    last_frame_ms = fm1_ms;
}

static void finish_play(void) {
    char buf[24];
    uint8_t st = grade_stars(&gr);
    uint32_t pct = gr.score / 100u;
    uint32_t i = 0;
    tally = 1;
    if (piece_idx < NSTARS) {
        if (gr.score > set.best_pct[piece_idx]) {
            set.best_pct[piece_idx] = (uint16_t)gr.score;
            set.stars[piece_idx] = st;
            set_dirty = 1;
        }
    }
    buf[i++] = (char)('0' + pct / 100u);
    buf[i++] = (char)('0' + (pct / 10u) % 10u);
    buf[i++] = (char)('0' + pct % 10u);
    buf[i++] = '%';
    buf[i++] = 0;
    ui_tally(buf);
}

/* one scroll frame at ~30 fps */
static void play_frame(void) {
    uint32_t now = fm1_ms;
    int32_t tick_now, win;
    uint16_t i;
    /* advance the scroll (exact fractional accumulation) */
    {
        int32_t px_s = (int32_t)pl->tempo * SPD_N[set.speed_idx] / SPD_D[set.speed_idx] * (int32_t)PXQ / 60;
        scroll_rem += (uint32_t)(px_s * 16);
        scroll_f += scroll_rem / 30u;
        scroll_rem %= 30u;
    }
    {
        uint32_t sc = scroll_f >> 4;
        uint32_t dx = sc - last_scroll;
        if (dx) {
            if (dx > 240u) dx = 240u;
            ui_scroll_px(dx);
            draw_new_cols(sc, dx);
            last_scroll = sc;
        }
    }
    tick_now = ((int32_t)(cursor_x() + last_scroll) - 240) * 96 / (int32_t)PXQ;
    win = timing_window_ms(pl->level);
    /* rests resolve themselves; overdue notes are misses */
    while (next_ev < evn) {
        const lesson_event_t *e = &pl->events[next_ev];
        int32_t err_ms;
        if (e->is_rest) { next_ev++; continue; }
        err_ms = (tick_now - tick_of(next_ev)) * 60000 / ((int32_t)pl->tempo * 96);
        if (err_ms > win) { grade_note(&gr, 0, 0, pl->level); next_ev++; }
        else break;
    }
    /* end of piece: last note scrolled off */
    if (next_ev >= evn && evn && last_scroll > evx[evn - 1u] + 48u) {
        finish_play();
        return;
    }
    ui_blit();
    ui_title(pl->title);
    ui_cursor(cursor_x());
    for (i = 0; i < 8u; i++)
        if (ov[i].until && (int32_t)(now - ov[i].until) < 0)
            ui_played(cursor_x(), ov[i].midi, ov[i].color);
    if ((int32_t)(now - indic_until) < 0)
        ui_indic(SPD_S[set.speed_idx]);
}

/* key press during play: judge the nearest expected note */
static void on_key(uint8_t midi) {
    int32_t tick_now, estart, err_ms, win;
    if (state != ST_PLAY || tally) { overlay(midi, UI_HIT_C); return; }
    while (next_ev < evn && pl->events[next_ev].is_rest) next_ev++;
    if (next_ev >= evn) { overlay(midi, UI_MISS_C); return; }
    tick_now = ((int32_t)(cursor_x() + last_scroll) - 240) * 96 / (int32_t)PXQ;
    estart = tick_of(next_ev);
    err_ms = (tick_now - estart) * 60000 / ((int32_t)pl->tempo * 96);
    win = timing_window_ms(pl->level);
    if (err_ms < -win || err_ms > win) { overlay(midi, UI_MISS_C); return; }
    {
        uint8_t ok = (midi == pl->events[next_ev].midi);
        grade_note(&gr, ok, err_ms, pl->level);
        overlay(midi, ok ? UI_HIT_C : UI_MISS_C);
        next_ev++;
    }
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

    render_book();

    for (;;) {
        uint32_t keys, b, rel;
        int32_t e;
        platform_poll();

        e = fm1_enc_take(ENC_SELECT);
        if (e && state == ST_BOOK) {
            int32_t s = (int32_t)sel + e;
            while (s < 0) s += NPIECES;
            sel = (uint8_t)(s % NPIECES);
            render_book();
        }
        e = fm1_enc_take(ENC_KNOB1);
        if (e) {
            int32_t c = (int32_t)set.cursor_x + e * 3;
            if (c < 30) c = 30; if (c > 180) c = 180;
            if (c != set.cursor_x) { set.cursor_x = (uint8_t)c; set_dirty = 1; }
        }
        e = fm1_enc_take(ENC_KNOB2);
        if (e) {
            int32_t s = (int32_t)set.speed_idx + e;
            if (s < 0) s = 0; if (s > 4) s = 4;
            if (s != set.speed_idx) {
                set.speed_idx = (uint8_t)s; set_dirty = 1;
                indic_until = fm1_ms + 1200u;
            }
        }

        b = fm1_input_edges(&rel);
        if (b & (1u << BTN_PLAY)) {
            if (state == ST_BOOK || tally) start_play();
        }
        if (b & (1u << BTN_HOME)) {
            if (state != ST_BOOK) { save_if_dirty(); state = ST_BOOK; render_book(); }
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
        /* note-offs via a sounding mask */
        {
            static uint32_t sounding = 0;
            uint32_t off = sounding & ~keys;
            sounding = keys;
            for (k = 0; k < 27u; k++)
                if (off >> k & 1u) {
                    uint8_t midi = (uint8_t)(53u + k);
                    piano_off(midi);
                    midi_send(0x80u, midi, 0);
                }
        }

        if (state == ST_PLAY && !tally && fm1_ms - last_frame_ms >= 33u) {
            last_frame_ms = fm1_ms;
            play_frame();
        }

        /* expected-key LED */
        {
            uint8_t exp = 255;
            uint16_t j = next_ev;
            if (state == ST_PLAY && !tally) {
                while (j < evn && pl->events[j].is_rest) j++;
                if (j < evn && pl->events[j].midi >= 53u && pl->events[j].midi < 80u)
                    exp = (uint8_t)(pl->events[j].midi - 53u);
            }
            for (k = 0; k < 27u; k++) fm1_led_key(KEY_LED(k), k == exp);
        }

        if (set_dirty && fm1_ms - last_save_ms > 10000u) {
            save_if_dirty(); last_save_ms = fm1_ms;
        }
    }
}
