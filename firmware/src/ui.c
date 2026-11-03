#include "ui.h"
#include "platform_api.h"

#include <stddef.h>

/* Layout (240x240):
 *   top bar:    y 0..23   (title, score, streak)
 *   roll area:  y 24..199  (notes scroll right -> left to play line)
 *   key strip:  y 200..239 (mini keyboard map)
 */

#define TOP_H      24
#define ROLL_TOP   24
#define ROLL_BOT   199
#define STRIP_TOP  200
#define PLAY_X     60          /* play line x */
#define PX_PER_TICK_DEN 2      /* horizontal scale: 1 tick = 1/2 px */

void ui_clear(void) {
    plat_clear(UI_BLACK);
}

void ui_lesson_list(const char **titles, const uint8_t *stars,
                    uint8_t count, uint8_t selected) {
    uint8_t i;
    char buf[56];
    ui_clear();
    plat_text(8, 4, 1, "MAESTRO - lessons", UI_YELLOW);
    for (i = 0; i < count && i < 8; i++) {
        int y = 28 + (int)i * 24;
        uint16_t c = (i == selected) ? UI_BLACK : UI_WHITE;
        if (i == selected)
            plat_rect(4, y - 2, 232, 22, UI_YELLOW);
        /* title (truncated) + stars, e.g. "Ode to Joy  **" */
        {
            uint8_t s = 0;
            const char *t = titles[i];
            while (s < 40 && t[s]) { buf[s] = t[s]; s++; }
            buf[s++] = ' ';
            {
                uint8_t k;
                for (k = 0; k < stars[i] && k < 3; k++) buf[s++] = '*';
            }
            buf[s] = '\0';
        }
        plat_text(10, y, 1, buf, c);
    }
    plat_present();
}

void ui_practice(practice_t *p, const grade_t *g,
                 uint8_t hit_flash, uint8_t miss_flash,
                 uint8_t key_base_midi, uint8_t key_count) {
    char buf[40];
    uint16_t i;
    uint32_t now = p->now;
    uint8_t nkeys = key_count ? key_count : 27;

    ui_clear();

    /* top bar */
    plat_rect(0, 0, 240, TOP_H, UI_DKGREY);
    {
        uint8_t s = 0;
        const char *t = p->lesson->title;
        while (s < 22 && t[s]) { buf[s] = t[s]; s++; }
        buf[s] = '\0';
        plat_text(6, 6, 1, buf, UI_WHITE);
        /* score right-aligned (integer percent) */
        buf[0] = (char)('0' + (g->score / 10000) % 10);
        buf[1] = (char)('0' + (g->score / 1000) % 10);
        buf[2] = (char)('0' + (g->score / 100) % 10);
        buf[3] = '%';
        buf[4] = '\0';
        plat_text(196, 6, 1, buf, UI_GREEN);
    }
    /* play line */
    plat_line(PLAY_X, ROLL_TOP, PLAY_X, ROLL_BOT, UI_GREY);

    /* note bars: map pitch -> y, time -> x */
    {
        uint32_t acc = 0;
        uint8_t lo = 255, hi = 0;
        /* find pitch range first (cheap, <=256 events) */
        for (i = 0; i < p->lesson->event_count; i++) {
            const lesson_event_t *e = &p->lesson->events[i];
            if (!e->is_rest) {
                if (e->midi < lo) lo = e->midi;
                if (e->midi > hi) hi = e->midi;
            }
        }
        if (hi < lo) { lo = 60; hi = 72; }
        for (i = 0; i < p->lesson->event_count; i++) {
            const lesson_event_t *e = &p->lesson->events[i];
            int32_t x0, x1, y, h;
            uint16_t color;
            if (e->is_rest) { acc += e->ticks; continue; }
            x0 = PLAY_X + (int32_t)((int32_t)(acc - now) / PX_PER_TICK_DEN);
            x1 = x0 + (int32_t)(e->ticks / PX_PER_TICK_DEN);
            acc += e->ticks;
            if (x1 < 0 || x0 > 240) continue;
            if (x0 < 0) x0 = 0;
            if (x1 > 240) x1 = 240;
            h = (ROLL_BOT - ROLL_TOP) / (int32_t)(hi - lo + 1);
            if (h < 3) h = 3;
            y = ROLL_BOT - (int32_t)(e->midi - lo + 1) * h;
            color = (i < p->idx) ? UI_DKGREY
                  : (i == p->idx) ? UI_YELLOW : UI_BLUE;
            plat_rect(x0, y, (uint16_t)(x1 - x0), (uint16_t)h, color);
        }
    }

    /* key strip: highlight expected key + pressed keys */
    {
        const lesson_event_t *cur = NULL;
        uint8_t k;
        int w = 240 / nkeys;
        if (!practice_done(p))
            cur = practice_current(p);
        for (k = 0; k < nkeys; k++) {
            int x = (int)k * w;
            uint8_t midi = (uint8_t)(key_base_midi + k);
            uint16_t c = UI_GREY;
            if (cur && !cur->is_rest && cur->midi == midi) c = UI_YELLOW;
            if (plat_key_down(k)) c = (c == UI_YELLOW) ? UI_GREEN : UI_WHITE;
            plat_rect(x, STRIP_TOP, (uint16_t)(w - 1), 240 - STRIP_TOP, c);
        }
    }

    /* hit/miss flash border */
    if (hit_flash)  plat_rect(0, 0, 240, 240, UI_GREEN);   /* TODO: border only */
    if (miss_flash) plat_rect(0, 0, 240, 240, UI_RED);
    plat_present();
}

void ui_results(const grade_t *g, const char *title, uint8_t is_best) {
    char buf[40];
    uint8_t s, k;
    ui_clear();
    plat_text(8, 8, 1, title, UI_YELLOW);
    /* score as percent with one decimal, integer math */
    buf[0] = (char)('0' + (g->score / 10000) % 10);
    buf[1] = (char)('0' + (g->score / 1000) % 10);
    buf[2] = (char)('0' + (g->score / 100) % 10);
    buf[3] = '.';
    buf[4] = (char)('0' + (g->score / 10) % 10);
    buf[5] = '%';
    buf[6] = '\0';
    plat_text(8, 40, 2, buf, UI_WHITE);
    /* stars */
    s = grade_stars(g);
    for (k = 0; k < 3; k++) {
        int x = 8 + (int)k * 40;
        if (k < s) plat_rect(x, 80, 32, 32, UI_YELLOW);
        else       plat_rect(x, 80, 32, 32, UI_DKGREY);
    }
    /* best streak */
    {
        uint8_t n = 0;
        uint16_t v = g->best_streak;
        char tmp[8];
        int t = 0;
        if (v == 0) tmp[t++] = '0';
        while (v > 0 && t < 7) { tmp[t++] = (char)('0' + v % 10); v /= 10; }
        buf[0] = 's'; buf[1] = 't'; buf[2] = 'r'; buf[3] = 'e';
        buf[4] = 'a'; buf[5] = 'k'; buf[6] = ' '; n = 7;
        while (t > 0 && n < 38) buf[n++] = tmp[--t];
        buf[n] = '\0';
        plat_text(8, 130, 1, buf, UI_GREEN);
    }
    if (is_best) plat_text(8, 160, 1, "new best!", UI_YELLOW);
    plat_text(8, 200, 1, "PLAY: retry  ENC: next", UI_GREY);
    plat_present();
}
