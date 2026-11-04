/* SPDX-License-Identifier: GPL-3.0-only */
/* Maestro staff renderer: 1-bit planes -> composited RGB565 blit. */
#include "ui.h"

/* colors are RGB565 */
#define PAPER UI_PAPER
#define INK UI_INK
#define CURSOR_C 0xFCE0u /* amber */
#define HIT_C UI_HIT_C   /* green */
#define MISS_C UI_MISS_C /* red */

#define PLANE_W 30u      /* bytes per row */
static uint8_t pfb[SCROLL_H * PLANE_W];      /* ink: the notation */
static uint8_t ok_pfb[SCROLL_H * PLANE_W];   /* your correct notes (green) */
static uint8_t bad_pfb[SCROLL_H * PLANE_W];  /* your wrong notes (red) */
static uint8_t *cur_fb = pfb;                /* drawing target plane */

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
        pfb[i] = ok_pfb[i] = bad_pfb[i] = 0;
}

/* shift all planes left by dx pixels (dx < 240) */
void ui_scroll_px(uint32_t dx) {
    uint32_t y, i, p;
    uint8_t *planes[3] = { pfb, ok_pfb, bad_pfb };
    for (p = 0; p < 3u; p++) {
        uint8_t *fb = planes[p];
        uint32_t d = dx;
        while (d >= 8u) {
            for (y = 0; y < SCROLL_H; y++) {
                uint8_t *r = fb + y * PLANE_W;
                for (i = 0; i < PLANE_W - 1u; i++) r[i] = r[i + 1];
                r[PLANE_W - 1u] = 0;
            }
            d -= 8u;
        }
        if (d) {
            for (y = 0; y < SCROLL_H; y++) {
                uint8_t *r = fb + y * PLANE_W;
                uint8_t carry = 0;
                for (i = PLANE_W; i-- > 0; ) {
                    uint8_t b = r[i];
                    r[i] = (uint8_t)((b << d) | carry);
                    carry = (uint8_t)(b >> (8u - d));
                }
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
    uint8_t *save = cur_fb;
    cur_fb = pfb;
    ledgers(x, dia);
    head(x, y, open);
    if (ticks < 384u) stem(x, y, dia);
    if (ticks <= 48u) flag_up(x, y);
    if (ticks == 144u || ticks == 288u) {
        px(x + 11u, y - 3u); px(x + 12u, y - 3u);
        px(x + 11u, y - 2u); px(x + 12u, y - 2u);
    }
    cur_fb = save;
}

void ui_rest(uint32_t x, uint32_t ticks) {
    uint32_t ym = STAFF_TOP + 2u * STAFF_SPC;
    uint8_t *save = cur_fb;
    cur_fb = pfb;
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
    cur_fb = save;
}

void ui_bar(uint32_t x) {
    uint8_t *save = cur_fb;
    cur_fb = pfb;
    vline(x, STAFF_TOP, STAFF_BOT);
    cur_fb = save;
}

/* treble clef, stylized for 1-bit. G line (2nd from bottom) is y=136. */
void ui_clef(uint32_t x) {
    uint8_t *save = cur_fb;
    cur_fb = pfb;
    vline(x, 84u, 200u);                 /* main stroke */
    ellipse_o(x, 142u, 11u, 14u);        /* belly around the G line */
    ellipse_o(x, 112u, 6u, 8u);          /* upper loop */
    px(x - 1u, 83u); px(x - 2u, 82u); px(x - 3u, 82u);
    px(x - 4u, 83u); px(x - 5u, 84u);    /* top hook */
    px(x - 1u, 201u); px(x - 2u, 202u); px(x - 3u, 203u);
    px(x - 4u, 203u); px(x - 5u, 202u); px(x - 6u, 200u); /* tail */
    cur_fb = save;
}

/* your played note, into the ok/bad plane so it scrolls with the music */
void ui_played_scroll(uint32_t x, int32_t midi, uint8_t ok) {
    uint8_t *save = cur_fb;
    uint32_t y = ui_y_of(ui_dia(midi));
    cur_fb = ok ? ok_pfb : bad_pfb;
    head(x, y, 0);
    cur_fb = save;
}

/* composite one region row: bad > ok > ink > paper, byte-swapped for the LCD */
static void expand_row(uint32_t y, uint16_t *dst) {
    uint32_t i, x;
    uint8_t *r0 = pfb + y * PLANE_W;
    uint8_t *r1 = ok_pfb + y * PLANE_W;
    uint8_t *r2 = bad_pfb + y * PLANE_W;
    for (i = 0; i < PLANE_W; i++) {
        uint8_t b0 = r0[i], b1 = r1[i], b2 = r2[i];
        for (x = 0; x < 8u; x++) {
            uint8_t m = (uint8_t)(0x80u >> x);
            uint16_t v = (b2 & m) ? MISS_C : (b1 & m) ? HIT_C : (b0 & m) ? INK : PAPER;
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
void ui_tally(const char *s) {
    overlay_text(8u, TALLY_Y, s, &FONT_L, 32u);
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
