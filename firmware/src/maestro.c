/* Maestro — music lessons on the FM-1.
 *
 * App entry for the Dinghy platform. The platform owns boot, USB,
 * MIDI, the screen and storage, and calls into us here:
 *
 *   app_main()  — runs the UI + lesson state machine (never returns)
 *   app_audio() — fills an empty stereo frame buffer; called from the
 *                 audio ISR, so: short, integer math only, no waiting.
 *
 * INTEGRATION (see Dinghy docs/API.md):
 *   - bind plat_* (platform_api.h) to the platform's real calls
 *   - lessons come from tools/lessons2c.py -> lessons_data.c
 *     (generated at build time; see SETUP.md step 3)
 */

#include <stdint.h>

#include "lesson.h"
#include "drills.h"
#include "grade.h"
#include "ui.h"
#include "store.h"
#include "platform_api.h"

/* lessons_data.c (generated): */
/* extern const lesson_t *all_lessons[]; */
/* extern const uint8_t all_lessons_count; */

/* ---- tiny feedback synth: square blips for metronome/hit/miss ----
 * app_audio mixes these into the platform's buffer. */

typedef struct {
    uint16_t freq;        /* Hz */
    uint32_t frames_left;
    uint16_t amp;         /* 0..32767 */
} blip_t;

#define BLIP_SLOTS 3
static blip_t blips[BLIP_SLOTS];
static uint32_t blip_phase[BLIP_SLOTS];
static uint32_t sample_rate = 48000;   /* TODO: query platform */

static void blip_start(uint16_t freq, uint32_t ms, uint16_t amp) {
    uint8_t i;
    for (i = 0; i < BLIP_SLOTS; i++) {
        if (blips[i].frames_left == 0) {
            blips[i].freq = freq;
            blips[i].frames_left = (ms * sample_rate) / 1000u;
            blips[i].amp = amp;
            blip_phase[i] = 0;
            return;
        }
    }
}

/* platform: void app_audio(int32_t *buf, uint32_t frames) */
void app_audio(int32_t *buf, uint32_t frames) {
    uint32_t f;
    uint8_t i;
    for (f = 0; f < frames; f++) {
        int32_t s = 0;
        for (i = 0; i < BLIP_SLOTS; i++) {
            if (blips[i].frames_left == 0) continue;
            /* square wave, integer phase: one period = sr/freq frames */
            {
                uint32_t period = sample_rate / blips[i].freq;
                uint32_t ph = blip_phase[i] % (period ? period : 1);
                int32_t v = (ph < period / 2)
                    ? (int32_t)blips[i].amp : -(int32_t)blips[i].amp;
                /* short decay envelope: linear, integer */
                v = (v * (int32_t)blips[i].frames_left) /
                    (int32_t)((100u * sample_rate) / 1000u + 1u);
                s += v;
                blip_phase[i]++;
                blips[i].frames_left--;
            }
        }
        buf[2 * f] += s;       /* stereo: same on both */
        buf[2 * f + 1] += s;
    }
}

/* ---- app state machine ---- */

typedef enum {
    ST_MENU,
    ST_PRACTICE,
    ST_RESULTS
} app_state_t;

/* platform: void app_main(void) */
void app_main(void) {
    /* TODO(integration): the template calls app_main after platform
     * init. Wire the real lesson table here:
     *
     *   extern const lesson_t *all_lessons[];
     *   extern const uint8_t all_lessons_count;
     */
    progress_t progress;
    app_state_t state = ST_MENU;
    uint8_t selected = 0;

    progress_load(&progress);

    for (;;) {
        switch (state) {
        case ST_MENU:
            /* TODO: build title/star arrays from all_lessons[];
             * ui_lesson_list(titles, stars, count, selected);
             * ENC_KNOB1 / up-down to move, BTN_PLAY to start. */
            (void)selected;
            break;
        case ST_PRACTICE: {
            /* TODO: the practice loop —
             *   practice_t pr; grade_t gr;
             *   practice_start(&pr, all_lessons[selected]);
             *   grade_init(&gr);
             *   tick clock from the platform timer;
             *   on key press: practice_play() -> grade_note() ->
             *     blip_start() hit/miss, ui_practice() redraw;
             *   metronome: blip_start() on beats (levels 0-2);
             *   when practice_done(): progress_save(); state = ST_RESULTS.
             */
            break;
        }
        case ST_RESULTS:
            /* TODO: ui_results(&gr, title, is_best); PLAY retries,
             * encoder moves to next lesson. */
            state = ST_MENU;
            break;
        }
        /* TODO: platform idle/yield hook here (never busy-wait the
         * screen DMA; see Dinghy LCD_IDLE). */
    }
}
