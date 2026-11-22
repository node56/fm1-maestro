#!/usr/bin/env python3
"""Verify the v022 coverage tests (the behaviours that had no test).

  tempo      KNOB1 changes scroll speed; clamps at 40..200 BPM.
  save       SAVE marks a free-play end; PLAY stops there.
  seq        SEQ inserts a jump; SEQ again at the same spot removes it.
  presetwrap PRESETS wraps past the last patch and before the first.
  clef       OCT- -> bass clef (+ -1); OCT+ -> treble again.
  del_jump   EDIT + scrub erases a jump; EDIT + PLAY erases it too.
  scrub_back backward scrub sounds the note it crosses.
  select     SELECT wraps at both ends; a new piece keeps the take
             (v0.22: SELECT preserves it, per sheet).

Usage: python3 tests/v022_verify.py   (from the build tree root)
"""
import os, subprocess, sys, hashlib, wave
import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOST = os.path.join(ROOT, "build/host/dinghy")
OUT = os.path.join(ROOT, "build/host")
SCRIPTS = os.path.join(ROOT, "tests/scripts")
FS = 44100
failures = []

def check(name, ok, detail=""):
    print(f"{'ok  ' if ok else 'FAIL'}  {name}  {detail if not ok else ''}")
    if not ok:
        failures.append(name)

def run(script, out, film=None):
    cmd = [HOST, os.path.join(OUT, out), os.path.join(SCRIPTS, script)]
    if film:
        cmd.append(str(film))
    subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, check=True)

def read_ppm(path):
    return np.array(Image.open(path).convert("RGB"))

def scroll_hash(path):
    return hashlib.md5(read_ppm(path)[48:204].tobytes()).hexdigest()

def find_barlines(path):
    im = read_ppm(path)
    band = im[116:173, :, :]
    dark = (band.sum(axis=2) < 200)
    colfrac = dark.mean(axis=0)
    xs = []
    for x in range(240):
        if colfrac[x] > 0.85:
            if not xs or x - xs[-1] > 2:
                xs.append(x)
    return xs

def bar_shift(p1, p2):
    b1, b2 = find_barlines(p1), find_barlines(p2)
    if not b1 or not b2:
        return None
    return (b1[0] - b2[0]) % 128

def wav(path):
    with wave.open(path, "rb") as w:
        n = w.getnframes()
        data = np.frombuffer(w.readframes(n), dtype=np.int16)
    return data[0::2].astype(np.float64)

def fft_peak(seg):
    seg = seg - seg.mean()
    w = np.hanning(len(seg))
    spec = np.abs(np.fft.rfft(seg * w))
    freqs = np.fft.rfftfreq(len(seg), 1 / FS)
    i = int(np.argmax(spec[1:])) + 1
    return freqs[i], spec[i]

def band_energy(seg, lo, hi):
    seg = seg - seg.mean()
    w = np.hanning(len(seg))
    spec = np.abs(np.fft.rfft(seg * w))
    freqs = np.fft.rfftfreq(len(seg), 1 / FS)
    m = (freqs >= lo) & (freqs <= hi)
    return float(spec[m].max()) if m.any() else 0.0

def led_series(out):
    """film log -> list of (ms, btnLEDs). PLAY = bit 12, SEQ = bit 11."""
    rows = []
    for line in open(os.path.join(OUT, out + "-log.txt")):
        p = line.split()
        if len(p) >= 4:
            rows.append((int(p[0]), int(p[3])))
    return rows

def led_at(rows, ms, bit):
    """LED bit value at (or just after) ms."""
    v = None
    for t, leds in rows:
        if t <= ms:
            v = (leds >> bit) & 1
        else:
            break
    return v

def bit_in(rows, bit, t0, t1):
    """every sample of a button LED bit within [t0, t1] ms (an armed
    REC pulses, so a state is a pattern over a window, not a point
    sample; v0.27: PLAY is a steady colour -- see play_series)."""
    return [((m >> bit) & 1) for (t, m) in rows if t0 <= t <= t1]

def play_series(out):
    """film log -> list of (ms, green duty, red duty): PLAY's colour
    channels (v0.27; log fields 11/12, appended by tests/host.c).
    The button field's bit 12 now reads 'red lit', so PLAY colour
    states are asserted here, not via led_at/bit_in."""
    rows = []
    for line in open(os.path.join(OUT, out + "-log.txt")):
        p = line.split()
        if len(p) >= 15:
            rows.append((int(p[0]), int(p[11]), int(p[12])))
    return rows

def play_at(rows, ms):
    v = None
    for t, g, r in rows:
        if t <= ms:
            v = (g, r)
        else:
            break
    return v

def play_in(rows, t0, t1):
    return [(g, r) for (t, g, r) in rows if t0 <= t <= t1]

# ---------------------------------------------------------------- tempo
run("v022_tempo.txt", "v022_tempo")
def shift(a, b):
    return bar_shift(os.path.join(OUT, f"v022_tempo-{a}.ppm"),
                     os.path.join(OUT, f"v022_tempo-{b}.ppm"))
s72 = shift(1500, 2500)
s200 = shift(4000, 5000)
s40 = shift(6500, 7500)
s200c = shift(9000, 10000)
s40c = shift(11500, 12500)
check("tempo: 72 BPM -> ~38 px/s", s72 is not None and abs(s72 - 38.4) <= 4, f"got {s72}")
check("tempo: 200 BPM -> ~107 px/s", s200 is not None and abs(s200 - 106.7) <= 4, f"got {s200}")
check("tempo: 40 BPM -> ~21 px/s", s40 is not None and abs(s40 - 21.3) <= 4, f"got {s40}")
check("tempo: clamp up stays 200", s200c is not None and abs(s200c - 106.7) <= 4, f"got {s200c}")
check("tempo: clamp down stays 40", s40c is not None and abs(s40c - 21.3) <= 4, f"got {s40c}")

# ---------------------------------------------------------------- save
run("v022_save.txt", "v022_save", film=2)
rows = led_series("v022_save")
prows = play_series("v022_save")
check("save: PLAY red steady while playing to the end (v0.27: rolling = red; was the v0.26 beat flash)",
      bool(play_in(prows, 5200, 6800)) and all(gr == 0 and rd == 255 for gr, rd in play_in(prows, 5200, 6800)),
      f"got {play_in(prows, 5200, 6800)[:4]}")
check("save: PLAY green after the marked end (v0.27: stopped = green; was dark)",
      play_at(prows, 9000) == (255, 0), f"got {play_at(prows, 9000)}")
h8 = scroll_hash(os.path.join(OUT, "v022_save-8000.ppm"))
h12 = scroll_hash(os.path.join(OUT, "v022_save-12000.ppm"))
check("save: scroll stopped after the marked end", h8 == h12)

# ---------------------------------------------------------------- seq toggle
# insert at 500, remove at 1500; film rows are every 500 ms, so the
# bright state is visible at 1000 and the dark state at 2000.
run("v022_seq.txt", "v022_seq", film=2)
rows = led_series("v022_seq")
check("seq: LED bright after insert", led_at(rows, 1000, 11) == 1)
check("seq: LED dark after remove", led_at(rows, 2000, 11) == 0)
# the double bar is two vertical lines 5px apart; free-play bars are
# 128px apart, so any adjacent pair 5px apart is the jump.
def has_double_bar(path):
    b = find_barlines(path)
    return any(b[i + 1] - b[i] == 5 for i in range(len(b) - 1))
check("seq: double bar shown after insert",
      has_double_bar(os.path.join(OUT, "v022_seq-700.ppm")))
check("seq: double bar gone after remove",
      not has_double_bar(os.path.join(OUT, "v022_seq-1700.ppm")))

# ---------------------------------------------------------------- preset wrap
run("v022_preset_wrap.txt", "v022_preset_wrap")
a = wav(os.path.join(OUT, "v022_preset_wrap.wav"))
# first note (+5 wraps to E.PIANO 4, the dark one): 5th-8th harmonics low
# second note (-1 wraps to E.PIANO 1, brighter): 5th-8th higher
f1, _ = fft_peak(a[int(0.5 * FS):int(1.2 * FS)])
f2, _ = fft_peak(a[int(2.0 * FS):int(2.7 * FS)])
hi1 = band_energy(a[int(0.5 * FS):int(1.2 * FS)], 2400, 4200)
hi2 = band_energy(a[int(2.0 * FS):int(2.7 * FS)], 2400, 4200)
lo1 = band_energy(a[int(0.5 * FS):int(1.2 * FS)], 500, 550)
lo2 = band_energy(a[int(2.0 * FS):int(2.7 * FS)], 500, 550)
check("preset wrap: first note in tune (C5)", abs(f1 - 523.25) < 3, f"{f1:.1f} Hz")
check("preset wrap: second note in tune (C5)", abs(f2 - 523.25) < 3, f"{f2:.1f} Hz")
check("preset wrap: +5 lands on the dark patch (low highs)",
      hi1 / max(lo1, 1) < hi2 / max(lo2, 1),
      f"high/fund {hi1 / max(lo1, 1):.3f} vs {hi2 / max(lo2, 1):.3f}")

# ---------------------------------------------------------------- clef
run("v022_clef.txt", "v022_clef")
h100 = scroll_hash(os.path.join(OUT, "v022_clef-100.ppm"))
h500 = scroll_hash(os.path.join(OUT, "v022_clef-500.ppm"))
h1500 = scroll_hash(os.path.join(OUT, "v022_clef-1500.ppm"))
check("clef: OCT- changes the staff (bass)", h100 != h500)
check("clef: OCT+ returns to treble", h100 == h1500)
# the -1 indicator (top right, x 220..240, y 10..26) has ink at 500 ms
im = read_ppm(os.path.join(OUT, "v022_clef-500.ppm"))
oct_ind = im[10:26, 220:240, :]
check("clef: -1 indicator shown", int((oct_ind.sum(axis=2) < 200).sum()) > 10)

# ---------------------------------------------------------------- delete jump
# insert 500, scrub-erase 1300, re-insert 1900, play-erase 2500;
# film rows every 500 ms, so each LED state is read at the row after
# its transition. The double bar (5px pair) must be present at 700
# and 2100, gone at 1500.
run("v022_del_jump.txt", "v022_del_jump", film=2)
rows = led_series("v022_del_jump")
check("del jump: LED bright after insert", led_at(rows, 1000, 11) == 1)
check("del jump: LED dark after scrub-erase", led_at(rows, 1500, 11) == 0)
check("del jump: LED bright after re-insert", led_at(rows, 2000, 11) == 1)
check("del jump: LED dark after play-erase", led_at(rows, 3000, 11) == 0)
check("del jump: PLAY red steady after play-erase (no loop; v0.27: rolling = red; was the v0.26 beat flash)",
      bool(play_in(play_series("v022_del_jump"), 3300, 4700))
      and all(gr == 0 and rd == 255 for gr, rd in play_in(play_series("v022_del_jump"), 3300, 4700)),
      f"got {play_in(play_series('v022_del_jump'), 3300, 4700)[:4]}")
check("del jump: double bar shown after insert",
      has_double_bar(os.path.join(OUT, "v022_del_jump-700.ppm")))
check("del jump: double bar gone after scrub-erase",
      not has_double_bar(os.path.join(OUT, "v022_del_jump-1500.ppm")))
check("del jump: double bar shown after re-insert",
      has_double_bar(os.path.join(OUT, "v022_del_jump-2100.ppm")))

# ---------------------------------------------------------------- backward scrub
run("v022_scrub_back.txt", "v022_scrub_back")
a = wav(os.path.join(OUT, "v022_scrub_back.wav"))
# the backward pass is ~2600-3200 ms; C5 (523 Hz) must sound there.
f, mag = fft_peak(a[int(2.7 * FS):int(3.1 * FS)])
check("scrub back: note sounds on the backward pass", abs(f - 523.25) < 5 and mag > 100,
      f"peak {f:.1f} Hz mag {mag:.0f}")

# ---------------------------------------------------------------- select
# 200 = clean free play (reference), 1300 = free play with the note,
# 1900 = First Five Notes, 2300 = free play after the round trip
# (v0.22: SELECT preserves the take, so it must match 1300, not the
# clean reference), 2700 = First Five Notes again (must match 1900 --
# wrap is deterministic).
run("v022_select.txt", "v022_select")
h200 = scroll_hash(os.path.join(OUT, "v022_select-200.ppm"))
h1300 = scroll_hash(os.path.join(OUT, "v022_select-1300.ppm"))
h1900 = scroll_hash(os.path.join(OUT, "v022_select-1900.ppm"))
h2300 = scroll_hash(os.path.join(OUT, "v022_select-2300.ppm"))
h2700 = scroll_hash(os.path.join(OUT, "v022_select-2700.ppm"))
check("select: note was recorded (1300 != clean 200)", h1300 != h200)
check("select: take preserved (2300 == 1300, not clean)", h2300 == h1300 and h2300 != h200)
check("select: wrap deterministic (1900 == 2700)", h1900 == h2700)

print()
print("FAILURES:" if failures else "ALL V022 CHECKS PASSED", failures if failures else "")
sys.exit(1 if failures else 0)
