/* SPDX-License-Identifier: GPL-3.0-only */
/* FM piano: the DX7 factory patch E.PIANO 4 (ROM1B #5), Algorithm 5,
 * feedback 4, decoded from the ROM1B SysEx dump (see
 * dx7_pianos_ranked.json, full_parameters.rom1b_05). This replaces the
 * v0.17 PIANO 1 (Algorithm 19): the user chose E.PIANO 4 over PIANO 1
 * for being mellower -- its 2nd harmonic sits near the real grand's
 * and its 5th-8th partials are essentially absent, where PIANO 1 was
 * a bright 1983 FM piano with the 5th-7th far too loud.
 *
 * Topology (Alg 5): three parallel 2-operator pairs -- OP2 -> OP1,
 * OP4 -> OP3, OP6 -> OP5 -- with feedback on OP6. Carriers OP1, OP3,
 * OP5. All ratios are 1.00 except OP4, the tine modulator, at 14.00.
 * All detune is 0. There is no inharmonic carrier here (unlike
 * PIANO 1's 1.58); the mellowness comes from the tine modulator
 * decaying fast (OP4 R2=92) and the body pairs being plain 1:1.
 *
 * Integer math only (no FPU, no 64-bit division):
 * - Phase is uint32, 2^32 = one cycle; increments come from NOTE_INC.
 * - Sine is a 256-entry Q15 table with linear interpolation.
 * - A modulator output y (Q15) bends its carrier's phase by y<<18.
 *   (Dexed: phase deviation in cycles = gain * sin, and here Q15
 *   full scale is a Dexed gain of 2.0, so cycles = 2*y/32768 and
 *   the uint32 offset is y * 2^32 * 2 / 32768 = y << 18.)
 * - Feedback on OP6: Dexed fb_phase = 2*pi*g*(s1+s2) / 2^(9-feedback).
 *   With the Q15 scaling this reduces to (gain_q15 * (s1+s2)) >>
 *   (6-feedback), where s1,s2 are OP6's previous two sine values
 *   (Q15). FB_SHIFT = 6 - FEEDBACK, so feedback 4 shifts by 2 and
 *   feedback 6 (v0.17's PIANO 1) shifts by 0 -- the divisor is
 *   parameterised, not hardcoded. The product is int32 (it fits:
 *   32767*65534 < 2^31) and the shift is arithmetic, which GCC/Clang
 *   guarantee for signed values.
 * - Ratios: x1 is the base increment; x14 is base<<3 + base<<2 +
 *   base<<1 = 14x. If base > (2^32-1)/14 = 306783378 the product
 *   would overflow, so the increment is clamped to 0xFFFFFFFF
 *   (Nyquist, effectively silent) -- that only happens for notes
 *   above MIDI 91, past the top of the keyboard with OCT+2.
 * - Detune is a small cents offset: inc += (inc>>11) * detune,
 *   about 0.9 cents per detune unit at C5 (Dexed's osc_freq).
 *   E.PIANO 4 has detune 0 on every operator, so this is a no-op
 *   here and is kept only for consistency with the v0.17 engine.
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
 * a hold). No operator in this patch has a rate of 0, so every
 * segment moves.
 * Segments 0-2 are attack/decay/sustain; segment 3 (release) is
 * entered only by piano_off, and segment 4 is done.
 * Note OP2's L4 = 99 (the only operator with L4 != 0): on release
 * its envelope RISES toward GAIN_T[89+99] -- that is what the
 * patch says (Dexed does the same), a brief brightening of the
 * release. OP2 is a modulator, so it cannot keep the voice alive.
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
 * every KVS in this patch (VEL_DATA[50] = 241, near 239), so the
 * sound at vel 100 is the nominal patch; the sensitivity only
 * matters at other velocities.
 *
 * The piano-first sound rule still holds: the only sound this
 * engine makes is piano notes. There are no buzzes, beeps, clicks
 * or error sounds -- a note starts from silence (all phases and
 * envelopes at zero) and decays to silence on release.
 */
#include "fm_tables.h"

/* E.PIANO 4 operator parameters (from dx7_pianos_ranked.json, rom1b_05).
 * mult: 0 = x1.00, 1 = x14.00 (the tine modulator OP4). */
typedef struct { uint8_t ol; uint8_t r[4]; uint8_t l[4]; int8_t det; uint8_t mult; uint8_t rs; uint8_t kvs; } ep4op_t;
static const ep4op_t EP4[6] = {
    /* ol,  R1  R2  R3  R4,   L1  L2  L3  L4, det, mult, rs, kvs */
    { 99, {96, 43, 31, 85}, {99, 75,  0,  0}, 0, 0, 3, 0 },  /* OP1 carrier <- OP2 */
    { 89, {95, 99, 25, 68}, {99, 75,  0, 99}, 0, 0, 5, 2 },  /* OP2 mod -> OP1 */
    { 99, {95, 40, 29, 75}, {99, 90,  0,  0}, 0, 0, 3, 1 },  /* OP3 carrier <- OP4 */
    { 68, {97, 92, 42, 52}, {99, 60, 40,  0}, 0, 1, 3, 4 },  /* OP4 mod -> OP3, x14.00 tine */
    { 87, {95, 78, 29, 57}, {99, 95,  0,  0}, 0, 0, 4, 2 },  /* OP5 carrier <- OP6 */
    { 71, {95, 78, 20, 75}, {99, 95,  0,  0}, 0, 0, 3, 0 },  /* OP6 mod -> OP5, feedback */
};

/* Dexed feedback level for this patch; the shift is derived from it. */
#define FB_LEVEL 4u
#define FB_SHIFT (6u - FB_LEVEL)

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

#define NV 6u
static struct {
    fm_op_t op[6];
    int32_t fb1, fb2;   /* OP6 feedback: its previous two sine values, Q15 */
    volatile int32_t env;     /* carrier gain sum, for voice allocation */
    volatile int32_t target;  /* 1 while sounding, 0 when done */
    volatile uint8_t rel;
    uint8_t note;
    uint8_t vel;
    uint32_t age;
} voice[NV];
static volatile uint32_t ages;

/* sine, Q15, with linear interpolation (256-entry table). */
static int32_t fsin(uint32_t ph) {
    uint32_t i = ph >> 24;
    uint32_t frac = (ph >> 16) & 0xFFu;
    int32_t s0 = SINE_T[i];
    int32_t s1 = SINE_T[(i + 1u) & 0xFFu];
    return s0 + (((s1 - s0) * (int32_t)frac) >> 8);
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

/* GAIN_T index, clamped to the table (0..198). */
static uint32_t gain_idx(uint32_t idx) {
    if (idx > 198u) idx = 198u;
    return idx;
}

/* one envelope step for one operator. */
static void eg_step(fm_op_t *o, const ep4op_t *p) {
    int32_t diff;
    if (o->seg >= 4) return;
    diff = o->tgt - o->eg;
    if (diff > 512) {
        int32_t step = diff >> o->sh;
        if (step < 1) step = 1;
        o->eg += step;
        if (o->eg > o->tgt) o->eg = o->tgt;
    } else if (diff < -512) {
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

static void piano_on(uint8_t note, uint8_t vel) {
    uint32_t v, pick = 0, best = 0xFFFFFFFFu;
    uint32_t base, inc;
    uint8_t o;
    if (note > 127u || !vel) return;
    for (v = 0; v < NV; v++) {
        uint32_t score = (voice[v].note == note && voice[v].env > 1000)
            ? 0u : (uint32_t)voice[v].env + 1u;
        if (score < best) { best = score; pick = v; }
    }
    base = NOTE_INC[note];
    for (o = 0; o < 6; o++) {
        const ep4op_t *p = &EP4[o];
        fm_op_t *op = &voice[pick].op[o];
        int32_t ole;
        /* velocity-adjusted output level, clamped to 0..99. */
        ole = (int32_t)p->ol + vel_ol_offset(vel, p->kvs);
        if (ole < 0) ole = 0;
        if (ole > 99) ole = 99;
        op->ol = (uint8_t)ole;
        op->rsadj = rate_adj(note, p->rs);
        switch (p->mult) {
        case 1:  /* x14.00 tine: 8+4+2 */
            if (base > 306783378u) inc = 0xFFFFFFFFu;  /* would overflow; clamp to Nyquist (silent) */
            else inc = (base << 3) + (base << 2) + (base << 1);
            break;
        default:
            inc = base;
            break;
        }
        /* detune, ~0.9 cents per unit at C5 (all 0 in this patch). */
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
    voice[pick].note = note;
    voice[pick].vel = vel;
    voice[pick].age = ++ages;
    voice[pick].env = 1;
    voice[pick].target = 1;
    voice[pick].rel = 0;
}

static void piano_off(uint8_t note) {
    uint32_t v;
    uint8_t o;
    for (v = 0; v < NV; v++) {
        if (voice[v].note != note) continue;
        voice[v].rel = 1;
        for (o = 0; o < 6; o++) {
            const ep4op_t *p = &EP4[o];
            fm_op_t *op = &voice[v].op[o];
            if (op->seg < 3) {
                op->seg = 3;
                op->tgt = (int32_t)GAIN_T[gain_idx((uint32_t)op->ol + p->l[3])] << 16;
                op->sh = eg_shift(rate_eff(p->r[3], op->rsadj));
            }
        }
    }
}

static void app_audio(int32_t *out, uint32_t n) {
    uint32_t v, i;
    uint8_t o;
    for (v = 0; v < NV; v++) {
        fm_op_t *op;
        int32_t car_sum;
        if (!voice[v].target && !voice[v].env) continue;
        op = voice[v].op;
        for (i = 0; i < n; i++) {
            int32_t s2, s4, s6, s1, s3, s5;
            int32_t y2, y4, y6, out1, out3, out5, s;
            int32_t fb;
            for (o = 0; o < 6; o++) eg_step(&op[o], &EP4[o]);
            /* OP2 -> OP1: first pair. */
            s2 = fsin(op[1].ph);
            y2 = ((op[1].eg >> 16) * s2) >> 15;
            s1 = fsin(op[0].ph + ((uint32_t)y2 << 18));
            out1 = ((op[0].eg >> 16) * s1) >> 15;
            /* OP4 (x14 tine) -> OP3: second pair. */
            s4 = fsin(op[3].ph);
            y4 = ((op[3].eg >> 16) * s4) >> 15;
            s3 = fsin(op[2].ph + ((uint32_t)y4 << 18));
            out3 = ((op[2].eg >> 16) * s3) >> 15;
            /* OP6 with feedback -> OP5: third pair. */
            fb = ((op[5].eg >> 16) * (voice[v].fb1 + voice[v].fb2)) >> FB_SHIFT;
            s6 = fsin(op[5].ph + (uint32_t)fb);
            y6 = ((op[5].eg >> 16) * s6) >> 15;
            voice[v].fb2 = voice[v].fb1;
            voice[v].fb1 = s6;
            s5 = fsin(op[4].ph + ((uint32_t)y6 << 18));
            out5 = ((op[4].eg >> 16) * s5) >> 15;
            /* the three carriers, mixed and velocity-scaled. */
            s = (out1 + out3 + out5) >> 2;
            s = (s * (int32_t)voice[v].vel) >> 7;
            out[2u * i] += s;
            out[2u * i + 1u] += s;
            for (o = 0; o < 6; o++) op[o].ph += op[o].inc;
        }
        /* voice level for allocation; done when released and all
         * carriers have finished their release segment. (v0.17
         * tested car_sum == 0, but L4 = 0 still leaves gain 7, so
         * a released voice never reached 0 and was never freed.) */
        car_sum = (op[0].eg >> 16) + (op[2].eg >> 16) + (op[4].eg >> 16);
        voice[v].env = car_sum;
        if (voice[v].rel && op[0].seg == 4 && op[2].seg == 4 && op[4].seg == 4)
            voice[v].target = 0;
    }
}
