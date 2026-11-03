/* SPDX-License-Identifier: GPL-3.0-only */
/* Maestro v0.1: smooth side-scrolling treble staff.
 *
 * The screen is sheet music. Rendering uses a 1-bit offscreen buffer
 * (240x240x1 = 7200 B): each frame the buffer shifts left by the scroll
 * delta, only the newly exposed right columns are drawn from the piece
 * data, then the buffer is expanded to RGB565 and blitted. No full-screen
 * clear per frame, no flicker.
 */
#ifndef MAESTRO_UI_H
#define MAESTRO_UI_H
#include <stdint.h>

/* staff geometry (240x240) */
#define STAFF_TOP 94u        /* F6, top line */
#define STAFF_SPC 14u        /* line spacing */
#define STAFF_BOT 150u       /* E5, bottom line */
#define TITLE_Y 10u
#define TALLY_Y 208u

/* 1-bit buffer */
void ui_clear(void);                       /* wipe the buffer */
void ui_scroll_px(uint32_t dx);            /* shift the buffer left by dx px */
void ui_staff_cols(uint32_t x0, uint32_t x1); /* draw staff lines for columns [x0,x1) */

/* notation, drawn at absolute buffer x. y follows the staff. */
int32_t ui_dia(int32_t midi);              /* diatonic steps, E5 == 37 */
uint32_t ui_y_of(int32_t dia);             /* buffer y for a diatonic step */
void ui_note(uint32_t x, int32_t midi, uint32_t ticks);
void ui_rest(uint32_t x, uint32_t ticks);

/* push the buffer to the LCD */
void ui_blit(void);

/* text / cursor / played-note overlay, drawn on the LCD after the blit */
void ui_title(const char *s);
void ui_tally(const char *s);
void ui_indic(const char *s);
void ui_hint(const char *s);
void ui_cursor(uint32_t x);
void ui_played(uint32_t x, int32_t midi, uint32_t color); /* notehead overlay */
#define UI_HIT_C 0x07E0u   /* green */
#define UI_MISS_C 0xF800u  /* red */
#define UI_PAPER 0xF7BCu   /* warm paper */
#define UI_INK 0x0000u

#endif
