/* Host unit tests for the platform-independent core: the drill
 * generator and the persistent settings (flash blob 0).
 *
 * The lesson format is parsed at build time by tools/lessons2c.py --
 * there is no runtime lesson parser, practice engine, or grading
 * in the firmware any more, so there is nothing to unit-test there.
 * The screen, sound, transport, knobs, and presets are covered by
 * the host-mode scripted tests (tests/scripts in the Dinghy build
 * tree) run from the build tree.
 *
 * gcc -Wall -Wextra -std=c99 -I ../firmware/src test_core.c -o test_core
 * ./test_core
 *
 * store.c and drills.c are #included, so this is one translation
 * unit. store.c calls blob_load/blob_save; the stubs below back
 * blob 0 with a RAM buffer. (mset_crc32 comes along with store.c,
 * which lets the bad-magic test build a blob with a valid CRC.)
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "lesson.h"
#include "drills.h"
#include "mstore.h"

/* ---- stub flash blob 0 (RAM-backed) ---- */
static uint8_t blob_buf[64];
static int blob_present = 0;

static int blob_load(uint32_t i, void *p, uint32_t n) {
    if (i != 0u || !blob_present) return -1;
    if (n > sizeof blob_buf) n = sizeof blob_buf;
    memcpy(p, blob_buf, n);
    return 0;
}
static int blob_save(uint32_t i, const void *p, uint32_t n) {
    if (i != 0u) return -1;
    if (n > sizeof blob_buf) n = sizeof blob_buf;
    memcpy(blob_buf, p, n);
    blob_present = 1;
    return 0;
}

#include "store.c"
#include "drills.c"

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL line %d: %s\n", __LINE__, #cond); \
        failures++; \
    } \
} while (0)

/* ---- drills: deterministic, in range, resolves to the root ---- */
static void test_drills(void) {
    drill_params_t dp = { 60, 4, 96, 4, 4, RHY_Q | RHY_E, 1, 12345 };
    lesson_event_t a[128], b[128];
    uint16_t na, nb, i;
    na = drill_generate(&dp, a, 128);
    nb = drill_generate(&dp, b, 128);
    CHECK(na > 0 && na == nb);
    /* deterministic: same seed -> same events. Compared field by
     * field -- memcmp would also compare the struct's uninitialised
     * padding bytes, which drill_generate never writes. */
    for (i = 0; i < na; i++) {
        CHECK(a[i].midi == b[i].midi);
        CHECK(a[i].ticks == b[i].ticks);
        CHECK(a[i].is_rest == b[i].is_rest);
    }
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

/* ---- settings: defaults on an empty blob ---- */
static void test_settings_defaults(void) {
    settings_t s;
    blob_present = 0;
    mset_load(&s);
    CHECK(s.speed_idx == 2);
    CHECK(s.level == 2);
    CHECK(s.sel == 0);
}

/* ---- settings: save then load round-trips, including sel ----
 * (sel used to be written to the blob never and restored never --
 * the book selection reset to 0 on every reboot despite the blob
 * having a sel field. This test pins the fix.) */
static void test_settings_roundtrip(void) {
    settings_t s, t;
    blob_present = 0;
    memset(&s, 0, sizeof s);
    s.speed_idx = 1;
    s.level = 4;
    s.sel = 6;              /* free play */
    mset_save(&s);
    memset(&t, 0, sizeof t);
    mset_load(&t);
    CHECK(t.speed_idx == 1);
    CHECK(t.level == 4);
    CHECK(t.sel == 6);
    /* and again for a book piece */
    s.sel = 3;
    mset_save(&s);
    memset(&t, 0, sizeof t);
    mset_load(&t);
    CHECK(t.sel == 3);
}

/* ---- settings: out-of-range values clamp to defaults on load ---- */
static void test_settings_clamp(void) {
    settings_t s, t;
    blob_present = 0;
    memset(&s, 0, sizeof s);
    s.speed_idx = 9;        /* > 4 */
    s.level = 9;            /* > 5 */
    s.sel = 9;              /* > 6 */
    mset_save(&s);          /* saved as-is, with a valid CRC */
    memset(&t, 0, sizeof t);
    mset_load(&t);
    CHECK(t.speed_idx == 2);
    CHECK(t.level == 2);
    CHECK(t.sel == 0);
}

/* ---- settings: a corrupted blob falls back to defaults ---- */
static void test_settings_crc(void) {
    settings_t s, t;
    blob_present = 0;
    memset(&s, 0, sizeof s);
    s.sel = 5;
    mset_save(&s);
    blob_buf[5] ^= 0xFFu;   /* corrupt one settings byte, CRC now wrong */
    memset(&t, 0, sizeof t);
    mset_load(&t);
    CHECK(t.speed_idx == 2);
    CHECK(t.level == 2);
    CHECK(t.sel == 0);
}

/* ---- settings: valid CRC but bad magic falls back to defaults ---- */
static void test_settings_magic(void) {
    settings_t s, t;
    uint32_t c;
    uint8_t buf[sizeof(settings_t) + 4];
    blob_present = 0;
    memset(&s, 0, sizeof s);
    s.sel = 5;
    mset_save(&s);
    /* rebuild the blob by hand: bad magic, correct CRC over it */
    memcpy(buf, blob_buf, sizeof buf);
    buf[0] = 0x00u; buf[1] = 0x00u; buf[2] = 0x00u; buf[3] = 0x00u;
    c = mset_crc32(buf, sizeof(settings_t));
    buf[sizeof(settings_t)] = (uint8_t)c;
    buf[sizeof(settings_t) + 1] = (uint8_t)(c >> 8);
    buf[sizeof(settings_t) + 2] = (uint8_t)(c >> 16);
    buf[sizeof(settings_t) + 3] = (uint8_t)(c >> 24);
    memcpy(blob_buf, buf, sizeof buf);
    memset(&t, 0, sizeof t);
    mset_load(&t);
    CHECK(t.speed_idx == 2);
    CHECK(t.level == 2);
    CHECK(t.sel == 0);
}

int main(void) {
    test_drills();
    test_settings_defaults();
    test_settings_roundtrip();
    test_settings_clamp();
    test_settings_crc();
    test_settings_magic();
    if (failures == 0) printf("all tests passed\n");
    return failures != 0;
}
