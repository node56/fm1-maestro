/* SPDX-License-Identifier: GPL-3.0-only */
/* Maestro: smooth side-scrolling treble staff.
 *
 * The screen is sheet music. Rendering uses 1-bit offscreen planes
 * (240x132x1 = 4.0 KB each): each frame the planes shift left by the scroll
 * delta, only the newly exposed right columns are drawn from the piece
 * data, then the planes are composited to RGB565 and blitted. No full-screen
 * clear per frame, no flicker.
 *
 * Planes: black (staff, clef, bars, and the notes YOU play),
 * blue (the suggested notes, if a piece is loaded).
 * What you play is black; what the piece suggests is blue.
 */
#ifndef MAESTRO_UI_H
#define MAESTRO_UI_H
#include <stdint.h>

/* staff geometry (240x240) */
#define STAFF_TOP 94u        /* F6, top line */
#define STAFF_SPC 14u        /* line spacing */
#define STAFF_BOT 150u       /* E5, bottom line */
#define TITLE_Y 10u

/* the scroll region: only this band moves; title/cursor/text are static */
#define SCROLL_TOP 72u
#define SCROLL_H 132u        /* 72..204: staff, stems, ledger lines (12-row strips) */

/* 1-bit planes (scroll region only) */
void ui_paper(void);                       /* full screen to paper, once */
void ui_clear(void);                       /* wipe all planes */
void ui_scroll_px(uint32_t dx);            /* shift all planes left by dx px */
void ui_staff_cols(uint32_t x0, uint32_t x1); /* draw staff lines for columns [x0,x1) */
void ui_plane_black(void);                 /* draw in black (staff, your notes) */
void ui_plane_blue(void);                  /* draw in blue (suggested notes) */

/* notation, drawn at absolute buffer x into the selected plane. */
int32_t ui_dia(int32_t midi);              /* diatonic steps, E5 == 37 */
uint32_t ui_y_of(int32_t dia);             /* buffer y for a diatonic step */
void ui_note(uint32_t x, int32_t midi, uint32_t ticks);
void ui_rest(uint32_t x, uint32_t ticks);
void ui_bar(uint32_t x);                   /* bar line across the staff */
void ui_clef(uint32_t x);                  /* treble clef at the start */

/* push the planes to the LCD (composited) */
void ui_blit(void);

/* text / cursor, drawn on the LCD after the blit */
void ui_title(const char *s);
void ui_indic(const char *s);
void ui_hint(const char *s);
void ui_cursor_mark(uint32_t x);           /* amber playhead below the staff */

#define UI_PAPER 0xF7BCu   /* warm paper */
#define UI_INK 0x0000u     /* black: staff and your notes */
#define UI_BLUE 0x001Fu    /* blue: suggested notes */

#endif
