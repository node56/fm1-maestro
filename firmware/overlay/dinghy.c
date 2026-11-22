/* SPDX-License-Identifier: GPL-3.0-only */
/* Maestro on the Dinghy platform. The screen is sheet music. */
static void app_idle(void) {
}
#define LCD_IDLE app_idle
#include "platform.c"
#include "drills.c"
#include "store.c"
#include "lessons_data.c"
#include "ui.c"
#include "maestro.c"
