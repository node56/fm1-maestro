/* SPDX-License-Identifier: GPL-3.0-only */
/* FM piano: a preset engine -- five DX7 factory patches, all in
 * integer math (no FPU, no 64-bit division). The PRESETS knob
 * (ENC_PRESETS) selects the patch; E.PIANO 4 is the default.
 *
 * Patches (decoded from the ROM SysEx dumps):
 *  0 E.PIANO 4 (ROM1B #5, Alg 5, fb 4) -- the dark one, default.
 *  1 PIANO 1   (ROM1A #8, Alg 19, fb 6) -- bright 1983 grand.
 *  2 E.PIANO 3 (ROM3B #6, Alg 6, fb 4) -- mellow, 5th near the grand.
 *  3 PIANO 5   (ROM1B #2, Alg 3, fb 6) -- mellowest acoustic-named.
 *  4 E.PIANO 1 (ROM1A #11, Alg 5, fb 6) -- the famous Rhodes.
 * The preset is session-only: it resets to E.PIANO 4 on boot.
 *
 * Routing is the Dexed bus model (FmCore::algorithms, verbatim):
 * each operator reads a modulation bus (0=none, 1, 2) and writes
 * to a bus (0=audio out, 1, 2), adding or replacing. Operators
 * are evaluated OP6 down to OP1, so every modulator is computed
 * before its target. Feedback is on the operator whose flags
 * have both top bits set (OP6 in all five patches here).
 *
 * Integer math only (no FPU, no 64-bit division):
 * - Phase is uint32, 2^32 = one cycle; increments come from NOTE_INC.
 * - Sine is a 256-entry Q15 table with linear interpolation.
 * - A modulator output y (Q15) bends its carrier's phase by y<<18.
 *   (Dexed: phase deviation in cycles = gain * sin, and here Q15
 *   full scale is a Dexed gain of 2.0, so cycles = 2*y/32768 and
 *   the uint32 offset is y * 2^32 * 2 / 32768 = y << 18. A bus
 *   holding the sum of modulator outputs bends by the sum << 18;
 *   the shift is done on the unsigned value, so it wraps modulo
 *   2^32, which is the correct phase behaviour.)
 * - Feedback: Dexed fb_phase = 2*pi*g*(s1+s2) / 2^(9-feedback).
 *   With the Q15 scaling this reduces to (gain_q15 * (s1+s2)) >>
 *   (6-feedback), where s1,s2 are the feedback operator's previous
 *   two sine values (Q15). FB_SHIFT = 6 - feedback, so feedback 4
 *   shifts by 2 and feedback 6 shifts by 0 -- the divisor is
 *   parameterised per patch, not hardcoded. The product is int32
 *   (it fits: 32767*65534 < 2^31) and the shift is arithmetic,
 *   which GCC/Clang guarantee for signed values.
 * - Ratios: x1 is the base increment; the rest are shifts-and-adds
 *   (x3 = 2+1, x5 = 4+1, x11 = 8+2+1, x14 = 8+4+2, x1.58 =
 *   1 + 1/2 + 1/16 + 1/64 + 1/512 = 1.58008, 0.09 cents sharp).
 *   If base > (2^32-1)/mult the product would overflow, so the
 *   increment is clamped to 0xFFFFFFFF (Nyquist, effectively
 *   silent) -- that only happens for notes above MIDI 91 (x14),
 *   84 (x11), 96 (x5) or 108 (x3), past the top of the keyboard
 *   with OCT+2 for the high ones.
 * - Detune is a small cents offset: inc += (inc>>11) * detune,
 *   about 0.9 cents per detune unit at C5 (Dexed's osc_freq).
 *
 * Envelopes (DX7 EG, simplified to firmware):
 * Each operator has a 4-segment envelope (R1->L1, R2->L2, R3->L3,
 * and R4->L4 on release). The level is Q15.16 so a slow segment
 * can move by a fraction of a Q15 unit per sample. The segment
 * target gain comes from GAIN_T[output_level + EG_level]: a DX7
 * output level and an EG level are each ~0.75 dB per unit, so
 * gain = 2^((OL+L)/8 - 23.625) in Dexed units, scaled so Dexed
 * gain 2.0 is Q15 full scale (32767). L3 = 0 is effectively
 * silent (gain 7), which is how a piano note dies while held.
 * A DX7 rate R maps to an exponential step: the level moves
 * toward its target by (target-level)>>shift per sample, with
 * shift = 4 + (99-R)*17/99. That matches Dexed's per-doubling
 * times to within ~1.5x across the range (R=99: ~16 samples,
 * R=81: ~128, R=58: ~2k, R=25: ~65k, R=0: ~2M -- effectively
 * a hold). Segments 0-2 are attack/decay/sustain; segment 3
 * (release) is entered only by piano_off, and segment 4 is done.
 * Note OP2's L4 = 99 in E.PIANO 4 (the only operator with L4 != 0
 * there): on release its envelope RISES toward GAIN_T[89+99] --
 * that is what the patch says (Dexed does the same), a brief
 * brightening of the release. OP2 is a modulator, so it cannot
 * keep the voice alive.
 *
 * Rate scaling (Dexed ScaleRate): higher notes decay faster.
 * scale_rate = (rs * x) >> 3, x = min(31, max(0, note/3 - 7)).
 * Dexed adds this to qrate (0-63); the firmware's shift is derived
 * from the rate R (0-99), so the adjustment is converted to R
 * units with (rs_adj * 25) >> 4 (~x1.56) and added to R, clamped
 * to 99, before computing the shift. It is precomputed per
 * operator at note-on (the note is fixed then) and stored as rsadj.
 *
 * Velocity sensitivity (Dexed ScaleVelocity): harder strikes raise
 * an operator's level. scale_velocity = ((kvs * (VEL_DATA[vel>>1]
 * - 239) + 7) >> 3) << 4, in Dexed outlevel units (256 = one
 * doubling). GAIN_T index units are 1/8 doubling, so the offset
 * is scale_velocity >> 5, added to the output level and clamped
 * to 0..99 at note-on (stored as the operator's effective OL).
 * At the firmware's playing velocity (100) the offset is 0 for
 * every KVS in E.PIANO 4 (VEL_DATA[50] = 241, near 239), so the
 * sound at vel 100 is the nominal patch; the sensitivity only
 * matters at other velocities.
 *
 * A voice keeps the patch it started with: if the preset changes
 * while a note sounds, that note finishes with its own patch
 * (its operators' envelopes and routing come from the voice's
 * stored patch index, not the current preset).
 *
 * The piano-first sound rule still holds for the part you play:
 * the only sound this engine makes for it is piano notes. There
 * are no buzzes, beeps, clicks or error sounds -- a note starts
 * from silence (all phases and envelopes at zero) and decays to
 * silence on release.
 *
 * v0.26: two more patches live past the PRESETS knob's five --
 * a BASS and a DRUM, the instruments of a song's backing parts
 * (the knob never lands on them; piano_preset_count stays 5).
 * They are FM like everything else here: the bass is a two-
 * operator plucked stack (a fast-decaying x3 modulator over a
 * sustained fundamental), and the drum is a kick approximation --
 * a low sine burst whose high-ratio modulator collapses within
 * tens of milliseconds, leaving the thump. There is no noise
 * source in this engine, so there is no snare or hat.
 * Voices also carry a class: LIVE (the part being played or
 * learned -- its notes always win) or BACKING (a completed part
 * playing along). A backing note may only take a free voice or
 * another backing voice; if none is available the note is
 * dropped, never stolen from the player's hands.
 *
 * v0.30: the DRUM gains a pitch envelope (the patch_t penv
 * fields): a hit starts at four times its written pitch and
 * drops exponentially to the thump (~2048-sample time
 * constant), so the attack's first tens of milliseconds sweep
 * through the 300 Hz-4 kHz band the FM-1's small speaker
 * actually reproduces -- on the v0.29 patch the hit lived at
 * ~147 Hz, under the speaker's roll-off, and was barely
 * heard. OP2's ratio drops from x11 to x3 so the crunch sweeps
 * inside that band instead of above it. The tail is the
 * unchanged thump; no other patch declares an envelope.
 */
#include "fm_tables.h"

/* operator parameters.
 * mult: 0 = x1.00, 1 = x3.00, 2 = x5.00, 3 = x11.00, 4 = x14.00, 5 = x1.58. */
typedef struct { uint8_t ol; uint8_t r[4]; uint8_t l[4]; int8_t det; uint8_t mult; uint8_t rs; uint8_t kvs; } fmop_t;
/* penv_from / penv_sh (v0.30): an optional pitch envelope. A
 * voice started on a patch with penv_from != 0 begins at
 * penv_from/256 times its written pitch (all operator ratios
 * preserved) and glides exponentially back to unity with time
 * constant ~2^penv_sh samples. Only the DRUM declares one; every
 * other patch leaves the fields zero and renders exactly as
 * before. */
typedef struct { const char *name; uint8_t alg; uint8_t fb; fmop_t op[6]; uint16_t penv_from; uint8_t penv_sh; } patch_t;

static const patch_t PATCHES[] = {
    { "E.PIANO 4", 5, 4, {
        /* ol,  R1  R2  R3  R4,   L1  L2  L3  L4, det, mult, rs, kvs */
        { 99, {96, 43, 31, 85}, {99, 75,  0,  0}, 0, 0, 3, 0 },  /* OP1 carrier <- OP2 */
        { 89, {95, 99, 25, 68}, {99, 75,  0, 99}, 0, 0, 5, 2 },  /* OP2 mod -> OP1 */
        { 99, {95, 40, 29, 75}, {99, 90,  0,  0}, 0, 0, 3, 1 },  /* OP3 carrier <- OP4 */
        { 68, {97, 92, 42, 52}, {99, 60, 40,  0}, 0, 4, 3, 4 },  /* OP4 mod -> OP3, x14.00 tine */
        { 87, {95, 78, 29, 57}, {99, 95,  0,  0}, 0, 0, 4, 2 },  /* OP5 carrier <- OP6 */
        { 71, {95, 78, 20, 75}, {99, 95,  0,  0}, 0, 0, 3, 0 },  /* OP6 mod -> OP5, feedback */
    } },
    { "PIANO 1", 19, 6, {
        { 99, {81, 25, 20, 48}, {99, 82,  0,  0}, -2, 0, 4, 2 },  /* OP1 carrier <- OP2 */
        { 87, {99,  0, 25,  0}, {99, 75,  0,  0},  2, 0, 5, 0 },  /* OP2 mod -> OP1, <- OP3 */
        { 57, {81, 25, 25, 14}, {99, 99, 99,  0},  0, 1, 5, 0 },  /* OP3 mod -> OP2, x3.00 */
        { 99, {81, 23, 22, 45}, {99, 78,  0,  0},  1, 0, 5, 2 },  /* OP4 carrier <- OP6 */
        { 93, {81, 58, 36, 39}, {99, 14,  0,  0}, -1, 5, 5, 1 },  /* OP5 carrier <- OP6, x1.58 */
        { 82, {99,  0, 25,  0}, {99, 75,  0,  0}, -1, 0, 5, 0 },  /* OP6 mod -> OP4+OP5, feedback */
    } },
    { "E.PIANO 3", 6, 4, {
        { 99, {96, 25, 25, 54}, {99, 75,  0,  0}, 0, 0, 3, 0 },  /* OP1 carrier <- OP2 */
        { 74, {95, 99,  0, 54}, {99, 99, 75,  0}, 0, 0, 3, 2 },  /* OP2 mod -> OP1 */
        { 99, {95, 31, 20, 54}, {99, 80,  0,  0}, 0, 0, 3, 1 },  /* OP3 carrier <- OP4 */
        { 32, {97, 68, 13, 54}, {99, 81,  0,  0}, 0, 4, 3, 4 },  /* OP4 mod -> OP3, x14.00 tine */
        { 99, {95, 78, 20, 57}, {99, 95,  0,  0}, 0, 0, 3, 2 },  /* OP5 carrier <- OP6 */
        { 60, {90, 52, 25, 54}, {99, 60,  0,  0}, 0, 3, 2, 7 },  /* OP6 mod -> OP5, x11.00, feedback */
    } },
    { "PIANO 5", 3, 6, {
        { 99, {70, 30, 23, 50}, {99, 85,  0,  0}, 0, 0, 4, 3 },  /* OP1 carrier <- OP2 */
        { 72, {98, 36,  6, 32}, {91, 90,  0,  0}, 0, 0, 2, 1 },  /* OP2 mod -> OP1, <- OP3 */
        { 92, {94, 21, 33, 12}, {74, 19,  0,  0}, 0, 1, 3, 7 },  /* OP3 mod -> OP2, x3.00 */
        { 99, {70, 30, 23, 50}, {99, 85,  0,  0}, 0, 0, 4, 3 },  /* OP4 carrier <- OP5 */
        { 75, {98, 20,  6,  2}, {91, 90,  0,  0}, 0, 0, 2, 1 },  /* OP5 mod -> OP4, <- OP6 */
        { 67, {94,  1, 33, 12}, { 4, 19,  0,  0}, 0, 2, 2, 1 },  /* OP6 mod -> OP5, x5.00, feedback */
    } },
    { "E.PIANO 1", 5, 6, {
        { 99, {96, 25, 25, 67}, {99, 75,  0,  0},  3, 0, 3, 2 },  /* OP1 carrier <- OP2 */
        { 58, {95, 50, 35, 78}, {99, 75,  0,  0},  0, 4, 3, 7 },  /* OP2 mod -> OP1, x14.00 tine */
        { 99, {95, 20, 20, 50}, {99, 95,  0,  0},  0, 0, 3, 2 },  /* OP3 carrier <- OP4 */
        { 89, {95, 29, 20, 50}, {99, 95,  0,  0},  0, 0, 3, 6 },  /* OP4 mod -> OP3 */
        { 99, {95, 20, 20, 50}, {99, 95,  0,  0}, -7, 0, 3, 0 },  /* OP5 carrier <- OP6 */
        { 79, {95, 29, 20, 50}, {99, 95,  0,  0},  7, 0, 3, 6 },  /* OP6 mod -> OP5, feedback */
    } },
    /* The backing instruments (v0.26): not on the PRESETS knob.
     * Authored in the same decoded-parameter format; Alg 5 is
     * three independent modulator->carrier pairs, and each of
     * these uses only the OP2->OP1 pair (the other pairs are
     * silent: every level 0). */
    { "BASS", 5, 0, {
        { 99, {90, 50, 30, 60}, {99, 88, 80,  0}, 0, 0, 2, 2 },  /* OP1 carrier: sustained fundamental */
        { 88, {99, 88, 70, 60}, {99, 30,  0,  0}, 0, 1, 2, 4 },  /* OP2 mod -> OP1, x3.00: the pluck, gone fast */
        { 40, {50, 50, 50, 50}, { 0,  0,  0,  0}, 0, 0, 0, 0 },  /* OP3 silent */
        { 40, {50, 50, 50, 50}, { 0,  0,  0,  0}, 0, 0, 0, 0 },  /* OP4 silent */
        { 40, {50, 50, 50, 50}, { 0,  0,  0,  0}, 0, 0, 0, 0 },  /* OP5 silent */
        { 40, {50, 50, 50, 50}, { 0,  0,  0,  0}, 0, 0, 0, 0 },  /* OP6 silent */
    } },
    { "DRUM", 5, 0, {
        { 99, {99, 55, 50, 80}, {99,  0,  0,  0}, 0, 0, 0, 0 },  /* OP1 carrier: the thump (sine burst, ~200 ms) */
        { 95, {99, 75, 60, 80}, {99,  0,  0,  0}, 0, 1, 0, 0 },  /* OP2 mod -> OP1, x3.00: attack crunch, collapses fast (x11 until v0.30 -- see the pitch-envelope note below) */
        { 40, {50, 50, 50, 50}, { 0,  0,  0,  0}, 0, 0, 0, 0 },  /* OP3 silent */
        { 40, {50, 50, 50, 50}, { 0,  0,  0,  0}, 0, 0, 0, 0 },  /* OP4 silent */
        { 40, {50, 50, 50, 50}, { 0,  0,  0,  0}, 0, 0, 0, 0 },  /* OP5 silent */
        { 40, {50, 50, 50, 50}, { 0,  0,  0,  0}, 0, 0, 0, 0 },  /* OP6 silent */
    }, 1024, 11 },   /* pitch envelope (v0.30): starts x4.00 and drops to
                     * the thump with a ~2048-sample time constant, so
                     * the attack sweeps down through the band a small
                     * speaker reproduces instead of starting below it.
                     * OP2's ratio came down from x11 to x3 for the
                     * same reason: at x11 the crunch sits at ~1.6 kHz
                     * only at the written pitch, and any pitch drop
                     * sweeps it up out of the band before it can be
                     * heard; at x3 the whole sweep stays in the band.
                     * Measured on host renders of a lone hit (attack
                     * window 30 ms): 300 Hz-4 kHz RMS 2728 -> 3356,
                     * attack total unchanged, the 100-300 ms tail
                     * (the thump) unchanged. */
};
#define N_PATCHES (sizeof PATCHES / sizeof PATCHES[0])
#define N_PIANO_PRESETS 5u   /* the PRESETS knob's patches (indices 0..4) */
#define PATCH_BASS 5u
#define PATCH_DRUM 6u
#define PATCH_HARMONY 1u   /* PIANO 1: the song harmony part's fixed patch
                            * (v0.30) -- a preset-family voice that is not
                            * the knob's business; the knob still chooses
                            * the melody's patch alone */
static uint8_t cur_preset;   /* session-only; resets to 0 (E.PIANO 4) on boot */

/* Dexed FmCore::algorithms[32], verbatim (ops[0]=OP6 ... ops[5]=OP1).
 * Per operator: inbus = (flags>>4)&3, outbus = flags&3,
 * add = flags&0x04, feedback = (flags&0xc0)==0xc0. */
static const uint8_t ALG_FLAGS[32][6] = {
    { 0xc1, 0x11, 0x11, 0x14, 0x01, 0x14 },  /* 1 */
    { 0x01, 0x11, 0x11, 0x14, 0xc1, 0x14 },  /* 2 */
    { 0xc1, 0x11, 0x14, 0x01, 0x11, 0x14 },  /* 3 */
    { 0xc1, 0x11, 0x94, 0x01, 0x11, 0x14 },  /* 4 */
    { 0xc1, 0x14, 0x01, 0x14, 0x01, 0x14 },  /* 5 */
    { 0xc1, 0x94, 0x01, 0x14, 0x01, 0x14 },  /* 6 */
    { 0xc1, 0x11, 0x05, 0x14, 0x01, 0x14 },  /* 7 */
    { 0x01, 0x11, 0xc5, 0x14, 0x01, 0x14 },  /* 8 */
    { 0x01, 0x11, 0x05, 0x14, 0xc1, 0x14 },  /* 9 */
    { 0x01, 0x05, 0x14, 0xc1, 0x11, 0x14 },  /* 10 */
    { 0xc1, 0x05, 0x14, 0x01, 0x11, 0x14 },  /* 11 */
    { 0x01, 0x05, 0x05, 0x14, 0xc1, 0x14 },  /* 12 */
    { 0xc1, 0x05, 0x05, 0x14, 0x01, 0x14 },  /* 13 */
    { 0xc1, 0x05, 0x11, 0x14, 0x01, 0x14 },  /* 14 */
    { 0x01, 0x05, 0x11, 0x14, 0xc1, 0x14 },  /* 15 */
    { 0xc1, 0x11, 0x02, 0x25, 0x05, 0x14 },  /* 16 */
    { 0x01, 0x11, 0x02, 0x25, 0xc5, 0x14 },  /* 17 */
    { 0x01, 0x11, 0x11, 0xc5, 0x05, 0x14 },  /* 18 */
    { 0xc1, 0x14, 0x14, 0x01, 0x11, 0x14 },  /* 19 */
    { 0x01, 0x05, 0x14, 0xc1, 0x14, 0x14 },  /* 20 */
    { 0x01, 0x14, 0x14, 0xc1, 0x14, 0x14 },  /* 21 */
    { 0xc1, 0x14, 0x14, 0x14, 0x01, 0x14 },  /* 22 */
    { 0xc1, 0x14, 0x14, 0x01, 0x14, 0x04 },  /* 23 */
    { 0xc1, 0x14, 0x14, 0x14, 0x04, 0x04 },  /* 24 */
    { 0xc1, 0x14, 0x14, 0x04, 0x04, 0x04 },  /* 25 */
    { 0xc1, 0x05, 0x14, 0x01, 0x14, 0x04 },  /* 26 */
    { 0x01, 0x05, 0x14, 0xc1, 0x14, 0x04 },  /* 27 */
    { 0x04, 0xc1, 0x11, 0x14, 0x01, 0x14 },  /* 28 */
    { 0xc1, 0x14, 0x01, 0x14, 0x04, 0x04 },  /* 29 */
    { 0x04, 0xc1, 0x11, 0x14, 0x04, 0x04 },  /* 30 */
    { 0xc1, 0x14, 0x04, 0x04, 0x04, 0x04 },  /* 31 */
    { 0xc4, 0x04, 0x04, 0x04, 0x04, 0x04 },  /* 32 */
};

/* a patch's decoded routing (operators indexed 0=OP1 .. 5=OP6). */
typedef struct {
    uint8_t inbus[6];     /* 0=none, 1=bus1, 2=bus2 */
    uint8_t outbus[6];    /* 0=audio out, 1=bus1, 2=bus2 */
    uint8_t add[6];       /* 1=add to the bus, 0=replace it */
    uint8_t fb_op;        /* operator with feedback, or 0xFF */
    uint8_t fb_shift;     /* 6 - feedback level */
    uint8_t carrier_mask; /* bit o set if operator o outputs to audio */
} fmroute_t;
static fmroute_t routes[N_PATCHES];
static uint8_t routes_ready;

static void routes_init(void) {
    uint8_t p, j;
    if (routes_ready) return;
    for (p = 0; p < N_PATCHES; p++) {
        const uint8_t *fl = ALG_FLAGS[PATCHES[p].alg - 1];
        fmroute_t *R = &routes[p];
        R->fb_op = 0xFF;
        R->carrier_mask = 0;
        for (j = 0; j < 6; j++) {
            uint8_t o = 5 - j;   /* flags[j] is OP(6-j) */
            uint8_t f = fl[j];
            R->inbus[o] = (uint8_t)((f >> 4) & 3);
            R->outbus[o] = (uint8_t)(f & 3);
            R->add[o] = (f & 0x04) ? 1u : 0u;
            if ((f & 0xc0) == 0xc0 && PATCHES[p].fb) R->fb_op = o;
            if (R->outbus[o] == 0) R->carrier_mask |= (uint8_t)(1u << o);
        }
        R->fb_shift = (uint8_t)(6 - PATCHES[p].fb);
    }
    routes_ready = 1;
}

static void piano_preset_set(uint8_t idx) {
    if (idx < N_PIANO_PRESETS) cur_preset = idx;
}
static uint8_t piano_preset_get(void) { return cur_preset; }
static uint8_t piano_preset_count(void) { return (uint8_t)N_PIANO_PRESETS; }
static const char *piano_preset_name(void) { return PATCHES[cur_preset].name; }

/* Dexed velocity table (VEL_DATA, 64 entries, indexed by vel>>1). */
static const uint8_t VEL_DATA[64] = {
    0, 70, 86, 97, 106, 114, 121, 126, 132, 138, 142, 148, 152, 156,
    160, 163, 166, 170, 173, 174, 178, 181, 184, 186, 189, 190, 194,
    196, 198, 200, 202, 205, 206, 209, 211, 214, 216, 218, 220, 222,
    224, 225, 227, 229, 230, 232, 233, 235, 237, 238, 240, 241, 242,
    243, 244, 246, 246, 248, 249, 250, 251, 252, 253, 254
};

/* one operator: phase, envelope (Q15.16), and its EG segment. */
typedef struct {
    uint32_t ph;    /* phase accumulator, 2^32 = one cycle */
    uint32_t inc;   /* phase increment per sample */
    int32_t eg;     /* envelope level, Q15.16 (gain << 16) */
    int32_t tgt;    /* envelope segment target, Q15.16 */
    uint8_t seg;    /* 0,1,2 = attack/decay/sustain, 3 = release, 4 = done */
    uint8_t sh;     /* EG step shift for the current segment */
    uint8_t ol;     /* effective output level (velocity-adjusted) */
    uint8_t rsadj;  /* rate-scaling adjustment, R units, for this note */
} fm_op_t;

/* a voice already playing this note louder than this (carrier-gain
 * sum, Q15 units) is retriggered instead of stealing another voice. */
#define VOICE_RETRIG_ENV 1000
#define NV 6u
/* voice classes (v0.26): LIVE notes (the part being played or
 * learned) always win a voice; BACKING notes (a completed part
 * playing along) may only take a free voice or another backing
 * voice, and are dropped when neither is available. */
#define VOICE_LIVE 0u
#define VOICE_BACK 1u
static struct {
    fm_op_t op[6];
    int32_t fb1, fb2;   /* feedback operator's previous two sine values, Q15 */
    volatile int32_t env;     /* carrier gain sum, for voice allocation */
    volatile int32_t target;  /* 1 while sounding, 0 when done */
    volatile uint8_t rel;
    uint8_t note;
    uint8_t vel;
    uint8_t patch;      /* the preset this voice started with */
    uint8_t cls;        /* VOICE_LIVE or VOICE_BACK */
    uint32_t age;
    uint32_t penv;      /* pitch-envelope factor, Q16 (65536 = written
                         * pitch); 65536 whenever the patch has none */
    uint8_t penv_sh;    /* the envelope's decay shift (patch data) */
} voice[NV];
static volatile uint32_t ages;

#ifdef DINGHY_HOST
/* host-only instrumentation (the v029 suite reads these via the
 * harness): the most voices ever sounding at once, backing notes
 * dropped for want of a voice, and backing steals of a sounding
 * live voice (the policy forbids them; the counter proves it). */
uint32_t fm_host_max_active, fm_host_drops, fm_host_steal_live;
#endif

/* sine, Q15, with linear interpolation (256-entry table). */
static int32_t fsin(uint32_t ph) {
    uint32_t i = ph >> 24;
    uint32_t frac = (ph >> 16) & 0xFFu;
    int32_t s0 = SINE_T[i];
    int32_t s1 = SINE_T[(i + 1u) & 0xFFu];
    return s0 + (((s1 - s0) * (int32_t)frac) >> 8);
}

/* phase increment for a ratio code, with an overflow clamp
 * (Nyquist, effectively silent) past the top of the keyboard.
 * The clamp is (2^32-1)/mult, so the shift-and-add product fits. */
static uint32_t ratio_inc(uint32_t base, uint8_t mult) {
    switch (mult) {
    case 1:  /* x3.00 = 2+1 */
        if (base > 0xFFFFFFFFu / 3u) return 0xFFFFFFFFu;
        return (base << 1) + base;
    case 2:  /* x5.00 = 4+1 */
        if (base > 0xFFFFFFFFu / 5u) return 0xFFFFFFFFu;
        return (base << 2) + base;
    case 3:  /* x11.00 = 8+2+1 */
        if (base > 0xFFFFFFFFu / 11u) return 0xFFFFFFFFu;
        return (base << 3) + (base << 1) + base;
    case 4:  /* x14.00 = 8+4+2 */
        if (base > 0xFFFFFFFFu / 14u) return 0xFFFFFFFFu;
        return (base << 3) + (base << 2) + (base << 1);
    case 5:  /* x1.58 = 1 + 1/2 + 1/16 + 1/64 + 1/512 = 1.58008 */
        return base + (base >> 1) + (base >> 4) + (base >> 6) + (base >> 9);
    default: /* x1.00 */
        return base;
    }
}

/* DX7 rate -> EG step shift (see the header comment). */
static uint8_t eg_shift(uint8_t r) {
    return (uint8_t)(4 + ((99 - (int32_t)r) * 17) / 99);
}

/* Dexed ScaleRate, converted to R units (~x1.56): the amount to add
 * to a rate R for this note. */
static uint8_t rate_adj(uint8_t note, uint8_t rs) {
    int32_t x = (int32_t)(note / 3) - 7;
    uint32_t adj;
    if (x < 0) x = 0;
    if (x > 31) x = 31;
    adj = ((uint32_t)rs * (uint32_t)x) >> 3;   /* qrate units */
    return (uint8_t)((adj * 25u) >> 4);        /* to R units */
}

/* rate R with this operator's rate-scaling adjustment, clamped to 99. */
static uint8_t rate_eff(uint8_t r, uint8_t rsadj) {
    int32_t v = (int32_t)r + rsadj;
    if (v > 99) v = 99;
    return (uint8_t)v;
}

/* Dexed ScaleVelocity -> GAIN_T index offset (see the header comment). */
static int8_t vel_ol_offset(uint8_t vel, uint8_t kvs) {
    int32_t vv = (int32_t)VEL_DATA[vel >> 1] - 239;
    int32_t sv = ((kvs * vv + 7) >> 3) << 4;
    return (int8_t)(sv >> 5);
}

/* GAIN_T index, clamped to the table. */
#define GAIN_T_MAX (sizeof GAIN_T / sizeof GAIN_T[0] - 1u)
static uint32_t gain_idx(uint32_t idx) {
    if (idx > GAIN_T_MAX) idx = GAIN_T_MAX;
    return idx;
}

/* one envelope step for one operator. The level moves toward its
 * target by (target-level)>>shift per sample; within EG_SNAP of
 * the target it snaps to it, so the exponential approach finishes
 * instead of creeping forever by fractions of a Q15.16 unit. */
#define EG_SNAP 512   /* Q15.16 units: 1/128 of a Q15 gain unit */
static void eg_step(fm_op_t *o, const fmop_t *p) {
    int32_t diff;
    if (o->seg >= 4) return;
    diff = o->tgt - o->eg;
    if (diff > EG_SNAP) {
        int32_t step = diff >> o->sh;
        if (step < 1) step = 1;
        o->eg += step;
        if (o->eg > o->tgt) o->eg = o->tgt;
    } else if (diff < -EG_SNAP) {
        int32_t step = (-diff) >> o->sh;
        if (step < 1) step = 1;
        o->eg -= step;
        if (o->eg < o->tgt) o->eg = o->tgt;
    } else {
        o->eg = o->tgt;
        if (o->seg < 2) {
            o->seg++;
            o->tgt = (int32_t)GAIN_T[gain_idx((uint32_t)o->ol + p->l[o->seg])] << 16;
            o->sh = eg_shift(rate_eff(p->r[o->seg], o->rsadj));
        } else if (o->seg == 3) {
            o->seg = 4;
        }
        /* seg == 2 holds at L3 (the sustain). */
    }
}

/* Start a note with a chosen patch and voice class. Returns 1
 * when the note sounds, 0 when it was dropped -- which only a
 * BACKING note can be: a backing note takes a free voice or the
 * quietest backing voice, and when every voice is a sounding
 * LIVE voice it is dropped rather than stealing from the
 * player's hands. A LIVE note keeps the original policy: a voice
 * already playing this note loudly is retriggered, otherwise
 * the quietest voice overall goes. */
static int piano_on_p(uint8_t note, uint8_t vel, uint8_t patch, uint8_t cls) {
    uint32_t v, pick = 0, best = 0xFFFFFFFFu;
    uint32_t base;
    uint8_t o;
    const patch_t *P;
    if (note > 127u || !vel || patch >= N_PATCHES) return 0;
    routes_init();
    P = &PATCHES[patch];
    for (v = 0; v < NV; v++) {
        /* Voice stealing: a voice already playing this note loudly
         * is retriggered (so a repeated note does not stack two
         * voices on one pitch); otherwise the quietest voice goes.
         * env is the carrier-gain sum, Q15 units. A BACKING note's
         * candidates are only free voices (target 0) and backing
         * voices; with no candidate its best stays 0xFFFFFFFF and
         * the note is dropped below. */
        uint32_t score;
        if (cls == VOICE_BACK && voice[v].target && voice[v].cls != VOICE_BACK) continue;
        score = (voice[v].note == note && voice[v].cls == cls && voice[v].env > VOICE_RETRIG_ENV)
            ? 0u : (uint32_t)voice[v].env + 1u;
        if (score < best) { best = score; pick = v; }
    }
    if (best == 0xFFFFFFFFu) {
#ifdef DINGHY_HOST
        fm_host_drops++;
#endif
        return 0;   /* no voice a backing note may take: dropped */
    }
#ifdef DINGHY_HOST
    if (cls == VOICE_BACK && voice[pick].cls == VOICE_LIVE && voice[pick].target && voice[pick].env > 100)
        fm_host_steal_live++;   /* the policy forbids this; count it if it ever happens */
    {
        uint32_t act = 1;
        for (v = 0; v < NV; v++) if (v != pick && voice[v].target) act++;
        if (act > fm_host_max_active) fm_host_max_active = act;
    }
#endif
    base = NOTE_INC[note];
    for (o = 0; o < 6; o++) {
        const fmop_t *p = &P->op[o];
        fm_op_t *op = &voice[pick].op[o];
        int32_t ole;
        uint32_t inc;
        /* velocity-adjusted output level, clamped to 0..99. */
        ole = (int32_t)p->ol + vel_ol_offset(vel, p->kvs);
        if (ole < 0) ole = 0;
        if (ole > 99) ole = 99;
        op->ol = (uint8_t)ole;
        op->rsadj = rate_adj(note, p->rs);
        inc = ratio_inc(base, p->mult);
        /* detune, ~0.9 cents per unit at C5. */
        inc += (uint32_t)((int32_t)(inc >> 11) * (int32_t)p->det);
        op->inc = inc;
        op->ph = 0;
        op->eg = 0;
        op->seg = 0;
        op->sh = eg_shift(rate_eff(p->r[0], op->rsadj));
        op->tgt = (int32_t)GAIN_T[gain_idx((uint32_t)op->ol + p->l[0])] << 16;
    }
    voice[pick].fb1 = 0;
    voice[pick].fb2 = 0;
    voice[pick].penv = P->penv_from ? ((uint32_t)P->penv_from << 8) : 65536u;
    voice[pick].penv_sh = P->penv_sh;
    voice[pick].note = note;
    voice[pick].vel = vel;
    voice[pick].patch = patch;
    voice[pick].cls = cls;
    voice[pick].age = ++ages;
    /* env is the allocator's loudness measure, recomputed by the
     * render loop from the carrier gains. Until the first render
     * measures it, a just-struck note must not read as quiet:
     * with a token small value the next allocation in the same
     * tick (a song's parts striking together) would steal this
     * voice before it ever sounded, and the note would vanish.
     * Read as loudest until measured. */
    voice[pick].env = 1 << 30;
    voice[pick].target = 1;
    voice[pick].rel = 0;
    return 1;
}

static void piano_on(uint8_t note, uint8_t vel) {
    (void)piano_on_p(note, vel, cur_preset, VOICE_LIVE);
}

/* release one voice's note: the voice of this pitch in this class. */
static void voice_release(uint32_t v) {
    const patch_t *P = &PATCHES[voice[v].patch];
    uint8_t o;
    voice[v].rel = 1;
    for (o = 0; o < 6; o++) {
        const fmop_t *p = &P->op[o];
        fm_op_t *op = &voice[v].op[o];
        if (op->seg < 3) {
            op->seg = 3;
            op->tgt = (int32_t)GAIN_T[gain_idx((uint32_t)op->ol + p->l[3])] << 16;
            op->sh = eg_shift(rate_eff(p->r[3], op->rsadj));
        }
    }
}

static void piano_off(uint8_t note) {
    uint32_t v;
    for (v = 0; v < NV; v++)
        if (voice[v].note == note) voice_release(v);
}

/* release the note of one class only (a backing part's note-off
 * must not end a live note of the same pitch, or vice versa). */
static void piano_off_p(uint8_t note, uint8_t cls) {
    uint32_t v;
    for (v = 0; v < NV; v++)
        if (voice[v].note == note && voice[v].cls == cls) voice_release(v);
}

static void app_audio(int32_t *out, uint32_t n) {
    uint32_t v, i;
    routes_init();
    for (v = 0; v < NV; v++) {
        const patch_t *P;
        const fmroute_t *R;
        fm_op_t *op;
        int32_t car_sum;
        uint8_t o, car_done;
        if (!voice[v].target && !voice[v].env) continue;
        P = &PATCHES[voice[v].patch];
        R = &routes[voice[v].patch];
        op = voice[v].op;
        for (i = 0; i < n; i++) {
            int32_t bus1 = 0, bus2 = 0, vout = 0;
            int8_t o8;
            uint32_t pf = voice[v].penv;   /* this sample's pitch factor */
            /* OP6 down to OP1: every modulator is computed before
             * its target, and each bus is written before it is read. */
            for (o8 = 5; o8 >= 0; o8--) {
                uint32_t ph;
                int32_t s, y;
                o = (uint8_t)o8;
                eg_step(&op[o], &P->op[o]);
                ph = op[o].ph;
                if (R->inbus[o] == 1) ph += ((uint32_t)bus1 << 18);
                else if (R->inbus[o] == 2) ph += ((uint32_t)bus2 << 18);
                if (o == R->fb_op) {
                    int32_t fb = ((op[o].eg >> 16) * (voice[v].fb1 + voice[v].fb2)) >> R->fb_shift;
                    ph += (uint32_t)fb;
                }
                s = fsin(ph);
                y = ((op[o].eg >> 16) * s) >> 15;
                if (o == R->fb_op) {
                    voice[v].fb2 = voice[v].fb1;
                    voice[v].fb1 = s;
                }
                if (R->outbus[o] == 0) {
                    if (R->add[o]) vout += y; else vout = y;
                } else if (R->outbus[o] == 1) {
                    if (R->add[o]) bus1 += y; else bus1 = y;
                } else {
                    if (R->add[o]) bus2 += y; else bus2 = y;
                }
                op[o].ph += (pf == 65536u) ? op[o].inc
                    : (uint32_t)(((uint64_t)op[o].inc * pf) >> 16);
            }
            if (pf != 65536u) {
                /* the pitch envelope decays toward unity; within
                 * 1/256 of it, it lands exactly and the voice is
                 * back to its written increments. */
                uint32_t d = pf - 65536u;
                d -= d >> voice[v].penv_sh;
                voice[v].penv = (d < 256u) ? 65536u : 65536u + d;
            }
            /* the carriers, mixed and velocity-scaled. >> 2 is
             * headroom for the carrier sum (up to 3 carriers, each
             * up to Q15 full scale) so it cannot clip before the
             * velocity scale brings it back down. */
            {
                int32_t s = vout >> 2;
                s = (s * (int32_t)voice[v].vel) >> 7;
                out[2u * i] += s;
                out[2u * i + 1u] += s;
            }
        }
        /* voice level for allocation; done when released and all
         * carriers have finished their release segment. (v0.17
         * tested car_sum == 0, but L4 = 0 still leaves gain 7, so
         * a released voice never reached 0 and was never freed.) */
        car_sum = 0;
        car_done = 1;
        for (o = 0; o < 6; o++) {
            if (R->carrier_mask & (1u << o)) {
                car_sum += (op[o].eg >> 16);
                if (op[o].seg != 4) car_done = 0;
            }
        }
        voice[v].env = car_sum;
        if (voice[v].rel && car_done)
            voice[v].target = 0;
    }
}
