/* Host unit tests for the platform-independent core:
 * lesson parser + practice engine, drill generator, grading, CRC32.
 * gcc -Wall -Wextra -std=c99 -I ../firmware/src test_core.c \
 *     ../firmware/src/lesson.c ../firmware/src/drills.c \
 *     ../firmware/src/grade.c ../firmware/src/store.c ../firmware/src/ui.c \
 *     host_stub.c -o test_core && ./test_core
 * (ui.c is included so the whole app still compiles; it draws nothing here.)
 */
#include <stdio.h>
#include <string.h>

#include "lesson.h"
#include "drills.h"
#include "grade.h"
#include "store.h"

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL line %d: %s\n", __LINE__, #cond); \
        failures++; \
    } \
} while (0)

static const char *L01_TEXT =
    "title: First Five Notes\n"
    "kind: melody\n"
    "source: Original exercise\n"
    "level: 0\n"
    "hand: right\n"
    "position: C5\n"
    "tempo: 72\n"
    "time: 4/4\n"
    "notes:\n"
    "C5 q | D5 q | E5 q | F5 q |\n"
    "G5 q | F5 q | E5 q | D5 q |\n"
    "C5 h | R h |\n";

static void test_parse(void) {
    lesson_t l;
    int rc = lesson_parse(L01_TEXT, &l);
    CHECK(rc == 0);
    CHECK(l.event_count == 10);
    CHECK(l.kind == KIND_MELODY);
    CHECK(l.level == 0);
    CHECK(l.tempo == 72);
    CHECK(l.beats_per_bar == 4);
    CHECK(l.position_midi == 60);          /* C5 == middle C */
    CHECK(strcmp(l.title, "First Five Notes") == 0);
    CHECK(l.events[0].midi == 60 && l.events[0].ticks == 96);
    CHECK(l.events[4].midi == 67);        /* G5 */
    CHECK(l.events[8].midi == 60 && l.events[8].ticks == 192);
    CHECK(l.events[9].is_rest && l.events[9].ticks == 192);
}

static void test_parse_errors(void) {
    lesson_t l;
    CHECK(lesson_parse("title: x\nkind: bogus\ntempo: 72\ntime: 4/4\n"
                       "position: C5\nnotes:\nC5 q\n", &l) == -5);
    CHECK(lesson_parse("title: x\nkind: melody\ntempo: 72\ntime: 4/4\n"
                       "position: C5\nnotes:\nH5 q\n", &l) == -3);
    CHECK(lesson_parse("title: x\nkind: melody\ntempo: 72\ntime: 4/4\n"
                       "position: C5\nnotes:\nC5 z\n", &l) == -4);
}

static void test_practice(void) {
    lesson_t l;
    practice_t p;
    const lesson_event_t *ev;
    CHECK(lesson_parse(L01_TEXT, &l) == 0);
    practice_start(&p, &l);
    ev = practice_current(&p);
    CHECK(ev && ev->midi == 60);
    CHECK(practice_play(&p, 61) == 0);    /* wrong pitch: stays */
    CHECK(practice_play(&p, 60) == 1);    /* right: advances */
    ev = practice_current(&p);
    CHECK(ev && ev->midi == 62);
    /* play through the remaining notes */
    {
        static const uint8_t seq[] = { 62, 64, 65, 67, 65, 64, 62, 60 };
        uint8_t i;
        for (i = 0; i < 8; i++)
            CHECK(practice_play(&p, seq[i]) == 1);
    }
    /* only the trailing rest remains; jump the clock past it.
     * (rests are skipped lazily inside practice_current, as the
     * app's per-frame call does) */
    CHECK(!practice_done(&p));
    practice_set_now(&p, 100000);
    CHECK(practice_current(&p) == NULL);
    CHECK(practice_done(&p));
    CHECK(practice_play(&p, 60) == -1);
}

static void test_drills(void) {
    drill_params_t dp = { 60, 4, 96, 4, 4, RHY_Q | RHY_E, 1, 12345 };
    lesson_event_t a[128], b[128];
    uint16_t na, nb, i;
    na = drill_generate(&dp, a, 128);
    nb = drill_generate(&dp, b, 128);
    CHECK(na > 0 && na == nb);
    CHECK(memcmp(a, b, na * sizeof(a[0])) == 0);   /* deterministic */
    for (i = 0; i < na; i++) {
        if (!a[i].is_rest)
            CHECK(a[i].midi >= 60 && a[i].midi <= 64);
        CHECK(a[i].ticks == 96 || a[i].ticks == 48);
    }
    /* final note resolves to the root */
    i = na;
    while (i > 0 && a[i - 1].is_rest) i--;
    CHECK(i > 0 && a[i - 1].midi == 60);
}

static void test_grade(void) {
    grade_t g;
    int i;
    CHECK(timing_window_ms(0) == 160);
    CHECK(timing_window_ms(5) == 70);
    grade_init(&g);
    for (i = 0; i < 10; i++) grade_note(&g, 1, 0, 2);
    CHECK(g.score == 10000);
    CHECK(g.notes_hit == 10);
    CHECK(g.best_streak == 10);
    CHECK(grade_stars(&g) == 3);
    grade_init(&g);
    for (i = 0; i < 10; i++) grade_note(&g, 0, 0, 0);
    CHECK(g.score == 6000);
    CHECK(grade_stars(&g) == 0);
    grade_init(&g);
    grade_note(&g, 1, 500, 0);   /* way outside the 160ms window */
    CHECK(g.notes_hit == 0 && g.streak == 0);
}

static void test_crc(void) {
    CHECK(crc32((const uint8_t *)"123456789", 9) == 0xCBF43926u);
    CHECK(ticks_to_ms(96, 120) == 500);
    CHECK(ticks_to_ms(48, 96) == 312);
}

int main(void) {
    test_parse();
    test_parse_errors();
    test_practice();
    test_drills();
    test_grade();
    test_crc();
    if (failures == 0) printf("all tests passed\n");
    return failures != 0;
}
