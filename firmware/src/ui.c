/* SPDX-License-Identifier: GPL-3.0-only */
/* Maestro v0.1 staff renderer: 1-bit scroll buffer -> RGB565 blit. */
#include "ui.h"

/* colors are RGB565; INK/PAPER chosen here, accents passed in */
#define PAPER UI_PAPER
#define INK UI_INK
#define CURSOR_C 0xFCE0u /* amber */
#define HIT_C 0x07E0u    /* green */
#define MISS_C 0xF800u   /* red */

static uint8_t pfb[SCROLL_H][30];         /* 1 = ink, scroll region only */
static uint16_t strip[240 * 12];            /* blit staging, 12 rows */

/* full screen to paper (the static areas are drawn once and persist) */
void ui_paper(void) {
    lcd_fill(0u, 0u, 240u, 240u, PAPER);
}

static void px(uint32_t x, uint32_t y) {
    if (x >= 240u || y < SCROLL_TOP || y >= SCROLL_TOP + SCROLL_H) return;
    y -= SCROLL_TOP;
    pfb[y][x >> 3] |= (uint8_t)(0x80u >> (x & 7u));
}
static void hline(uint32_t x0, uint32_t x1, uint32_t y) {
    uint32_t x;
    for (x = x0; x < x1; x++) px(x, y);
}
static void vline(uint32_t x, uint32_t y0, uint32_t y1) {
    uint32_t y;
    if (y1 < y0) { uint32_t t = y0; y0 = y1; y1 = t; }
    for (y = y0; y <= y1; y++) px(x, y);
}

void ui_clear(void) {
    uint32_t y, i;
    for (y = 0; y < SCROLL_H; y++)
        for (i = 0; i < 30u; i++) pfb[y][i] = 0;
}

/* shift the scroll region left by dx pixels (dx < 240) */
void ui_scroll_px(uint32_t dx) {
    uint32_t y, i;
    while (dx >= 8u) {                       /* whole bytes first */
        for (y = 0; y < SCROLL_H; y++) {
            uint8_t *r = pfb[y];
            for (i = 0; i < 29u; i++) r[i] = r[i + 1];
            r[29] = 0;
        }
        dx -= 8u;
    }
    if (dx) {                                /* remaining bits, back to front */
        for (y = 0; y < SCROLL_H; y++) {
            uint8_t *r = pfb[y];
            uint8_t carry = 0;
            for (i = 29u; ; i--) {
                uint8_t b = r[i];
                r[i] = (uint8_t)((b << dx) | carry);
                carry = (uint8_t)(b >> (8u - dx));
                if (!i) break;
            }
        }
    }
}

void ui_staff_cols(uint32_t x0, uint32_t x1) {
    uint32_t l, x;
    if (x1 > 240u) x1 = 240u;
    for (l = 0; l < 5u; l++) {
        uint32_t y = STAFF_TOP + l * STAFF_SPC;
        for (x = x0; x < x1; x++) px(x, y);
    }
}

/* diatonic steps; our convention C5 == MIDI 60, E5 (bottom line) == 37 */
int32_t ui_dia(int32_t midi) {
    static const int8_t L[12] = {0,-1,1,-1,2,3,-1,4,-1,5,-1,6}; /* C..B, -1 = sharp */
    int32_t letter = L[midi % 12];
    if (letter < 0) letter = L[(midi - 1) % 12];  /* sharps: snap down (never happens in our pieces) */
    return (midi / 12) * 7 + letter;
}
uint32_t ui_y_of(int32_t dia) {
    return STAFF_BOT - (uint32_t)((dia - 37) * (int32_t)(STAFF_SPC / 2));
}

static void ledgers(uint32_t x, int32_t dia) {
    int32_t dd;
    for (dd = 35; dd >= dia && dd >= 31; dd -= 2)
        hline(x - 11u, x + 12u, ui_y_of(dd));
    for (dd = 47; dd <= dia; dd += 2)
        hline(x - 11u, x + 12u, ui_y_of(dd));
}

static void head(uint32_t x, uint32_t y, uint32_t open) {
    /* filled ellipse rx=7 ry=5; open heads: clear the middle */
    int32_t dy, dx, rx = 7, ry = 5;
    for (dy = -ry; dy <= ry; dy++) {
        /* dx = rx*sqrt(1-(dy/ry)^2), integer: binary-search-free approx via squares */
        int32_t q = (int32_t)ry * ry - dy * dy;
        int32_t dxi = 0;
        while ((dxi + 1) * (dxi + 1) * ry * ry <= q * rx * rx) dxi++;
        for (dx = -dxi; dx <= dxi; dx++) {
            if (open && dxi - (dx < 0 ? -dx : dx) > 2 && dy > -ry + 2 && dy < ry - 2)
                continue;                    /* hollow middle */
            px(x + dx, y + dy);
        }
    }
}

static void stem(uint32_t x, uint32_t y, int32_t dia) {
    int32_t up = dia < 41;                   /* below the middle line (B5) */
    uint32_t sx = up ? x + 6u : x - 6u;
    uint32_t len = 35u;
    if (up) vline(sx, y - len, y);
    else vline(sx, y, y + len);
}

static void flag_up(uint32_t x, uint32_t y) {
    /* eighth-note flag: short diagonal from the stem top */
    uint32_t i;
    for (i = 0; i < 10u; i++) px(x + 6u + i, y - 35u + i);
}

void ui_note(uint32_t x, int32_t midi, uint32_t ticks) {
    int32_t dia = ui_dia(midi);
    uint32_t y = ui_y_of(dia);
    uint32_t open = ticks >= 192u;           /* half and whole */
    ledgers(x, dia);
    head(x, y, open);
    if (ticks < 384u) stem(x, y, dia);       /* whole notes: no stem */
    if (ticks <= 48u) flag_up(x, y);         /* eighth and shorter */
    if (ticks == 144u || ticks == 288u) {    /* augmentation dot */
        px(x + 11u, y - 3u); px(x + 12u, y - 3u);
        px(x + 11u, y - 2u); px(x + 12u, y - 2u);
    }
}

void ui_rest(uint32_t x, uint32_t ticks) {
    uint32_t ym = STAFF_TOP + 2u * STAFF_SPC; /* middle line */
    if (ticks >= 384u) {                     /* whole: rect under 2nd line from top */
        uint32_t y = STAFF_TOP + STAFF_SPC, i, j;
        for (j = 0; j < 5u; j++) for (i = 0; i < 10u; i++) px(x - 5u + i, y + 1u + j);
    } else if (ticks >= 192u) {              /* half: rect on middle line */
        uint32_t i, j;
        for (j = 0; j < 5u; j++) for (i = 0; i < 10u; i++) px(x - 5u + i, ym - 5u + j);
    } else {                                 /* quarter: zigzag */
        int32_t i;
        for (i = -10; i <= 10; i++) {
            uint32_t xx = (uint32_t)((int32_t)x + (i < 0 ? -i / 3 : i / 3));
            px(xx, (uint32_t)((int32_t)ym + i));
        }
        px(x, ym - 12u); px(x + 1u, ym - 12u);
    }
}

/* expand one region row to RGB565, byte-swapped for the LCD (like lcd_fill) */
static void expand_row(uint32_t y, uint16_t *dst) {
    uint32_t i, x;
    uint8_t *r = pfb[y];
    for (i = 0; i < 30u; i++) {
        uint8_t b = r[i];
        for (x = 0; x < 8u; x++) {
            uint16_t v = (b & (0x80u >> x)) ? INK : PAPER;
            dst[i * 8u + x] = (uint16_t)((v >> 8) | (v << 8));
        }
    }
}

void ui_blit(void) {
    uint32_t y, s;
    for (s = 0; s < SCROLL_H / 12u; s++) {    /* 12-row strips over the region */
        for (y = 0; y < 12u; y++)
            expand_row(s * 12u + y, strip + y * 240u);
        lcd_blit(0, SCROLL_TOP + s * 12u, 240u, 12u, strip);
    }
}

/* ---- overlays: drawn on the LCD after the blit (via the gfx canvas) ---- */
static void overlay_text(uint32_t x, uint32_t y, const char *s,
                         const felucca_font_t *f, uint32_t h) {
    int32_t w = text_w(f, s);
    if (w < 0) w = 0;
    if (w > 232) w = 232;
    cv_begin((uint32_t)w + 4u, h, PAPER);
    cv_text(2, 0, f, s, INK);
    cv_blit(x, y);
}
void ui_title(const char *s) {
    overlay_text(8u, TITLE_Y, s, &FONT_S, 16u);
}
void ui_tally(const char *s) {
    overlay_text(8u, TALLY_Y, s, &FONT_L, 32u);
}
void ui_indic(const char *s) {
    overlay_text(196u, 218u, s, &FONT_S, 16u);
}
void ui_hint(const char *s) {
    overlay_text(8u, 218u, s, &FONT_S, 16u);
}
void ui_cursor(uint32_t x) {
    lcd_fill(x, STAFF_TOP - 10u, 2u, 5u * STAFF_SPC + 20u, CURSOR_C);
}
/* played-note notehead in color at the cursor */
void ui_played(uint32_t x, int32_t midi, uint32_t color) {
    int32_t dy, rx = 8, ry = 6;
    uint32_t y = ui_y_of(ui_dia(midi));
    for (dy = -ry; dy <= ry; dy++) {
        int32_t q = (int32_t)ry * ry - dy * dy;
        int32_t dxi = 0;
        while ((dxi + 1) * (dxi + 1) * ry * ry <= q * rx * rx) dxi++;
        if (dxi > 0) lcd_fill(x - (uint32_t)dxi, y + (uint32_t)dy, (uint32_t)(2 * dxi), 1u, (uint16_t)color);
    }
}

