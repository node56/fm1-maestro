#ifndef MAESTRO_UI_H
#define MAESTRO_UI_H

/* 240x240 screen layout for the lesson UI.
 * Thin wrappers over plat_* (platform_api.h); the layout math here
 * is real, the drawing calls bind at integration time.
 * (practice_t is declared in lesson.h.)
 */

#include <stdint.h>
#include "lesson.h"
#include "grade.h"

/* RGB565 colors */
#define UI_BLACK   0x0000u
#define UI_WHITE   0xFFFFu
#define UI_GREEN   0x07E0u
#define UI_RED     0xF800u
#define UI_YELLOW  0xFFE0u
#define UI_BLUE    0x001Fu
#define UI_GREY    0x8410u
#define UI_DKGREY  0x4208u

void ui_clear(void);

/* Lesson select list. `titles`/`stars`/`count` from the lesson index;
 * `selected` is the highlighted row. */
void ui_lesson_list(const char **titles, const uint8_t *stars,
                    uint8_t count, uint8_t selected);

/* Practice screen: piano roll of the lesson around the play line,
 * mini key-strip at the bottom, score/streak on top.
 * `hit_flash`/`miss_flash`: frames remaining for feedback flashes. */
void ui_practice(practice_t *p, const grade_t *g,
                 uint8_t hit_flash, uint8_t miss_flash,
                 uint8_t key_base_midi, uint8_t key_count);

/* Results screen. */
void ui_results(const grade_t *g, const char *title, uint8_t is_best);

#endif /* MAESTRO_UI_H */
