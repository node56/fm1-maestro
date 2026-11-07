/* SPDX-License-Identifier: GPL-3.0-only */
/* FM piano: the DX7 factory patch PIANO 1 (ROM1A #8), Algorithm 19,
 * feedback 6, decoded from the ROM1A SysEx dump (see dx7_piano1.json).
 * This replaces the sampled piano (Salamander Grand Piano V3): the
 * device is built to modulate, so the piano is now six operators
 * instead of nine samples, and any note is a frequency, not a file.
 *
 * Topology (Alg 19): a serial stack OP3 -> OP2 -> OP1, and carriers
 * OP4 / OP5 sharing modulator OP6 (feedback on OP6). The one element
 * that makes it a piano rather than an organ is the inharmonic
 * carrier OP5 at ratio 1.58 -- a real partial, not a detune.
 * There is no high-ratio tine: the highest ratio is 3.00, and the
 * "attack" is the 3.00 modulator at the top of the stack, not a
 * separate ping carrier.
 *
 * Integer math only (no FPU, no 64-bit division):
 * - Phase is uint32, 2^32 = one cycle; increments come from NOTE_INC.
 * - Sine is a 256-entry Q15 table with linear interpolation.
 * - A modulator output y (Q15) bends its carrier's phase by y<<18.
 *   (Dexed: phase deviation in cycles = gain * sin, and here Q15
 *   full scale is a Dexed gain of 2.0, so cycles = 2*y/32768 and
 *   the uint32 offset is y * 2^32 * 2 / 32768 = y << 18.)
 * - Feedback on OP6: the phase offset is gain6 * (s[n-1] + s[n-2])
 *   in uint32 phase units, where s are OP6's previous two sine
 *   values (Q15). (Dexed: fb cycles = gain * (s1+s2)/8, and with
 *   the same Q15 scaling that reduces to gain_q15 * (s1+s2).)
 * - Ratios: x1 is the base increment; x3 is base*3; x1.58 is
 *   base + base/2 + base/16 + base/64 + base/512 = 1.58008
 *   (0.09 cents sharp of 1.58 -- inaudible, and no multiply).
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
 * a hold, which is what OP2/OP6's R2=0 and R4=0 mean).
 * Segments 0-2 are attack/decay/sustain; segment 3 (release) is
 * entered only by piano_off, and segment 4 is done.
 *
 * The piano-first sound rule still holds: the only sound this
 * engine makes is piano notes. There are no buzzes, beeps, clicks
 * or error sounds -- a note starts from silence (all phases and
 * envelopes at zero) and decays to silence on release.
 */
#include "fm_tables.h"

/* PIANO 1 operator parameters (from dx7_piano1.json).
 * mult: 0 = x1.00, 1 = x3.00, 2 = x1.58. */
typedef struct { uint8_t ol; uint8_t r[4]; uint8_t l[4]; int8_t det; uint8_t mult; } p1op_t;
static const p1op_t P1[6] = {
    /* ol,  R1  R2  R3  R4,   L1  L2  L3  L4, det, mult */
    { 99, {81, 25, 20, 48}, {99, 82,  0,  0}, -2, 0 },  /* OP1 carrier <- OP2 */
    { 87, {99,  0, 25,  0}, {99, 75,  0,  0}, +2, 0 },  /* OP2 mod -> OP1 <- OP3 */
    { 57, {81, 25, 25, 14}, {99, 99, 99,  0},  0, 1 },  /* OP3 mod -> OP2, x3.00 */
    { 99, {81, 23, 22, 45}, {99, 78,  0,  0}, +1, 0 },  /* OP4 carrier <- OP6 */
    { 93, {81, 58, 36, 39}, {99, 14,  0,  0}, -1, 2 },  /* OP5 carrier <- OP6, x1.58 */
    { 82, {99,  0, 25,  0}, {99, 75,  0,  0}, -1, 0 },  /* OP6 mod -> OP4,OP5, fb */
};

/* one operator: phase, envelope (Q15.16), and its EG segment. */
typedef struct {
    uint32_t ph;    /* phase accumulator, 2^32 = one cycle */
    uint32_t inc;   /* phase increment per sample */
    int32_t eg;     /* envelope level, Q15.16 (gain << 16) */
    int32_t tgt;    /* envelope segment target, Q15.16 */
    uint8_t seg;    /* 0,1,2 = attack/decay/sustain, 3 = release, 4 = done */
    uint8_t sh;     /* EG step shift for the current segment */
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

/* one envelope step for one operator. */
static void eg_step(fm_op_t *o, const p1op_t *p) {
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
            o->tgt = (int32_t)GAIN_T[p->ol + p->l[o->seg]] << 16;
            o->sh = eg_shift(p->r[o->seg]);
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
        const p1op_t *p = &P1[o];
        fm_op_t *op = &voice[pick].op[o];
        switch (p->mult) {
        case 1:
            inc = base * 3u;
            if (inc < base) inc = 0xFFFFFFFFu;  /* overflow guard, unreachable notes */
            break;
        case 2:  /* x1.58 by shifts: 1 + 1/2 + 1/16 + 1/64 + 1/512 */
            inc = base + (base >> 1) + (base >> 4) + (base >> 6) + (base >> 9);
            break;
        default:
            inc = base;
            break;
        }
        /* detune, ~0.9 cents per unit at C5 */
        inc += (uint32_t)((int32_t)(inc >> 11) * (int32_t)p->det);
        op->inc = inc;
        op->ph = 0;
        op->eg = 0;
        op->seg = 0;
        op->sh = eg_shift(p->r[0]);
        op->tgt = (int32_t)GAIN_T[p->ol + p->l[0]] << 16;
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
            const p1op_t *p = &P1[o];
            fm_op_t *op = &voice[v].op[o];
            if (op->seg < 3) {
                op->seg = 3;
                op->tgt = (int32_t)GAIN_T[p->ol + p->l[3]] << 16;
                op->sh = eg_shift(p->r[3]);
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
            int32_t s3, s2, s1, s6, s4, s5;
            int32_t y3, y2, y6, out1, out4, out5, s;
            uint32_t fb;
            for (o = 0; o < 6; o++) eg_step(&op[o], &P1[o]);
            /* OP3 (no input) -> OP2 -> OP1: the serial stack. */
            s3 = fsin(op[2].ph);
            y3 = ((op[2].eg >> 16) * s3) >> 15;
            s2 = fsin(op[1].ph + ((uint32_t)y3 << 18));
            y2 = ((op[1].eg >> 16) * s2) >> 15;
            s1 = fsin(op[0].ph + ((uint32_t)y2 << 18));
            out1 = ((op[0].eg >> 16) * s1) >> 15;
            /* OP6 with feedback -> OP4 and OP5. */
            fb = (uint32_t)(op[5].eg >> 16) * (uint32_t)(voice[v].fb1 + voice[v].fb2);
            s6 = fsin(op[5].ph + fb);
            y6 = ((op[5].eg >> 16) * s6) >> 15;
            voice[v].fb2 = voice[v].fb1;
            voice[v].fb1 = s6;
            s4 = fsin(op[3].ph + ((uint32_t)y6 << 18));
            out4 = ((op[3].eg >> 16) * s4) >> 15;
            s5 = fsin(op[4].ph + ((uint32_t)y6 << 18));
            out5 = ((op[4].eg >> 16) * s5) >> 15;
            /* the three carriers, mixed and velocity-scaled. */
            s = (out1 + out4 + out5) >> 2;
            s = (s * (int32_t)voice[v].vel) >> 7;
            out[2u * i] += s;
            out[2u * i + 1u] += s;
            for (o = 0; o < 6; o++) op[o].ph += op[o].inc;
        }
        /* voice level for allocation; done when released and silent. */
        car_sum = (op[0].eg >> 16) + (op[3].eg >> 16) + (op[4].eg >> 16);
        voice[v].env = car_sum;
        if (voice[v].rel && car_sum == 0) voice[v].target = 0;
    }
}
