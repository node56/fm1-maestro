/* SPDX-License-Identifier: GPL-3.0-only */
/* Maestro staff renderer: 1-bit planes -> composited RGB565 blit. */
#include "ui.h"
#include "glyphs.h"

/* colors are RGB565 */
#define PAPER UI_PAPER
#define INK UI_INK
#define CURSOR_C 0xFCE0u /* amber */
#define BLUE UI_BLUE      /* suggested notes */

#define PLANE_W 30u      /* bytes per row */
static uint8_t blk_pfb[SCROLL_H * PLANE_W];  /* black: staff, clef, bars, your notes */
static uint8_t blu_pfb[SCROLL_H * PLANE_W];  /* blue: suggested notes */
static uint8_t *cur_fb = blk_pfb;            /* drawing target plane */

void ui_plane_black(void) { cur_fb = blk_pfb; }
void ui_plane_blue(void) { cur_fb = blu_pfb; }

static uint16_t strip[240 * 12];             /* blit staging, 12 rows */

/* full screen to paper (static areas are drawn once and persist) */
void ui_paper(void) {
    lcd_fill(0u, 0u, 240u, 240u, PAPER);
}

static void px(uint32_t x, uint32_t y) {
    if (x >= 240u || y < SCROLL_TOP || y >= SCROLL_TOP + SCROLL_H) return;
    y -= SCROLL_TOP;
    cur_fb[y * PLANE_W + (x >> 3)] |= (uint8_t)(0x80u >> (x & 7u));
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
    uint32_t i;
    for (i = 0; i < (uint32_t)SCROLL_H * PLANE_W; i++)
        blk_pfb[i] = blu_pfb[i] = 0;
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
    static const int8_t L[12] = {0,-1,1,-1,2,3,-1,4,-1,5,-1,6};
    int32_t letter = L[midi % 12];
    if (letter < 0) letter = L[(midi - 1) % 12];
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

/* ellipse outline, for the clef */
static void ellipse_o(uint32_t cx, uint32_t cy, uint32_t rx, uint32_t ry) {
    int32_t dy;
    for (dy = -(int32_t)ry; dy <= (int32_t)ry; dy++) {
        int32_t q = (int32_t)ry * ry - dy * dy;
        int32_t dxi = 0;
        while ((dxi + 1) * (dxi + 1) * (int32_t)ry * ry <= q * (int32_t)rx * rx) dxi++;
        px(cx + (uint32_t)dxi, cy + (uint32_t)dy);
        px(cx - (uint32_t)dxi, cy + (uint32_t)dy);
    }
}

static void head(uint32_t x, uint32_t y, uint32_t open) {
    int32_t dy, dx, rx = 7, ry = 5;
    for (dy = -ry; dy <= ry; dy++) {
        int32_t q = (int32_t)ry * ry - dy * dy;
        int32_t dxi = 0;
        while ((dxi + 1) * (dxi + 1) * ry * ry <= q * rx * rx) dxi++;
        for (dx = -dxi; dx <= dxi; dx++) {
            if (open && dxi - (dx < 0 ? -dx : dx) > 2 && dy > -ry + 2 && dy < ry - 2)
                continue;
            px(x + dx, y + dy);
        }
    }
}

static void stem(uint32_t x, uint32_t y, int32_t dia) {
    int32_t up = dia < 41;
    uint32_t sx = up ? x + 6u : x - 6u;
    uint32_t len = 35u;
    if (up) vline(sx, y - len, y);
    else vline(sx, y, y + len);
}

static void flag_up(uint32_t x, uint32_t y) {
    uint32_t i;
    for (i = 0; i < 10u; i++) px(x + 6u + i, y - 35u + i);
}

void ui_note(uint32_t x, int32_t midi, uint32_t ticks) {
    int32_t dia = ui_dia(midi);
    uint32_t y = ui_y_of(dia);
    uint32_t open = ticks >= 192u;
    ledgers(x, dia);
    head(x, y, open);
    if (ticks < 384u) stem(x, y, dia);
    if (ticks <= 48u) flag_up(x, y);
    if (ticks == 144u || ticks == 288u) {
        px(x + 11u, y - 3u); px(x + 12u, y - 3u);
        px(x + 11u, y - 2u); px(x + 12u, y - 2u);
    }
}

void ui_rest(uint32_t x, uint32_t ticks) {
    uint32_t ym = STAFF_TOP + 2u * STAFF_SPC;
    if (ticks >= 384u) {
        uint32_t y = STAFF_TOP + STAFF_SPC, i, j;
        for (j = 0; j < 5u; j++) for (i = 0; i < 10u; i++) px(x - 5u + i, y + 1u + j);
    } else if (ticks >= 192u) {
        uint32_t i, j;
        for (j = 0; j < 5u; j++) for (i = 0; i < 10u; i++) px(x - 5u + i, ym - 5u + j);
    } else {
        int32_t i;
        for (i = -10; i <= 10; i++) {
            uint32_t xx = (uint32_t)((int32_t)x + (i < 0 ? -i / 3 : i / 3));
            px(xx, (uint32_t)((int32_t)ym + i));
        }
        px(x, ym - 12u); px(x + 1u, ym - 12u);
    }
}

void ui_bar(uint32_t x) {
    vline(x, STAFF_TOP, STAFF_BOT);
}

/* final double bar: thin line, then a thick line 3px right */
void ui_endbar(uint32_t x) {
    vline(x, STAFF_TOP, STAFF_BOT);
    vline(x + 3u, STAFF_TOP, STAFF_BOT);
    vline(x + 4u, STAFF_TOP, STAFF_BOT);
}

/* draw a 1-bit glyph bitmap into the current plane.
 * x0 may be negative (glyph sliding off the left edge); left columns clip. */
static void glyph_blit(int32_t x0, uint32_t y0,
                       const uint8_t *g, uint32_t w, uint32_t h, uint32_t bpr) {
    uint32_t y, x;
    uint32_t skip = x0 < 0 ? (uint32_t)(-x0) : 0;
    if (skip >= w) return;
    for (y = 0; y < h; y++)
        for (x = skip; x < w; x++)
            if (g[y * bpr + (x >> 3)] & (0x80u >> (x & 7u)))
                px((uint32_t)(x0 + (int32_t)x), y0 + y);
}

/* treble clef from Bravura (see glyphs.h). Spiral centers on the G line.
 * x is the screen position of the glyph's left edge (may be negative). */
void ui_clef(int32_t x) {
    glyph_blit(x, STAFF_BOT - STAFF_SPC - GCLEF_G_OFF,
               glyph_gclef, GCLEF_W, GCLEF_H, GCLEF_BPR);
}

/* accidental before a notehead: 0=sharp, 1=flat, 2=natural.
 * Centers on the note's staff y. */
void ui_accidental(int32_t x, uint32_t y, uint32_t kind) {
    const uint8_t *g;
    uint32_t w, h, bpr, off;
    if (kind == 0u) { g = glyph_sharp; w = SHARP_W; h = SHARP_H; bpr = SHARP_BPR; off = SHARP_H / 2u; }
    else if (kind == 1u) { g = glyph_flat; w = FLAT_W; h = FLAT_H; bpr = FLAT_BPR; off = FLAT_H - 10u; }
    else { g = glyph_natural; w = NATURAL_W; h = NATURAL_H; bpr = NATURAL_BPR; off = NATURAL_H / 2u; }
    glyph_blit(x, y >= off ? y - off : 0u, g, w, h, bpr);
}

/* composite one region row: blue (suggested) > black (staff, your notes) > paper */
static void expand_row(uint32_t y, uint16_t *dst) {
    uint32_t i, x;
    uint8_t *rb = blk_pfb + y * PLANE_W;
    uint8_t *ru = blu_pfb + y * PLANE_W;
    for (i = 0; i < PLANE_W; i++) {
        uint8_t bb = rb[i], bu = ru[i];
        for (x = 0; x < 8u; x++) {
            uint8_t m = (uint8_t)(0x80u >> x);
            uint16_t v = (bu & m) ? BLUE : (bb & m) ? INK : PAPER;
            dst[i * 8u + x] = (uint16_t)((v >> 8) | (v << 8));
        }
    }
}

void ui_blit(void) {
    uint32_t y, s;
    for (s = 0; s < SCROLL_H / 12u; s++) {
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
void ui_indic(const char *s) {
    overlay_text(196u, 224u, s, &FONT_S, 16u);
}
void ui_hint(const char *s) {
    overlay_text(8u, 224u, s, &FONT_S, 16u);
}
/* amber playhead triangle below the staff, pointing up at the cursor */
void ui_cursor_mark(uint32_t x) {
    int32_t i;
    for (i = 0; i < 12; i++) {
        uint32_t w = (uint32_t)(12 - i);
        uint32_t x0 = x >= w / 2u ? x - w / 2u : 0u;
        lcd_fill(x0, 210u + (uint32_t)i, w, 1u, CURSOR_C);
    }
}
