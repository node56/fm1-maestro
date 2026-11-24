#!/usr/bin/env python3
"""Verify the v024 unified playhead model (v0.21).

  transport  PLAY toggles the velocity (moving <-> stopped); REC toggles
             the write bit without touching the velocity. Checked via
             the film log: PLAY LED (bit 12) follows velocity != 0,
             REC LED (bit 13) follows rec_write.
  knob_move  KNOB4 updates the timestamp while the sheet is moving: the
             2500 ms scroll differs from the no-knob control (the
             playhead jumped), and the scroll keeps advancing 2500 ->
             3500 (the velocity is unchanged).
  ghost      a key press with REC off and the tape stopped sounds once,
             through the playhead: the WAV equals the ST_PLAY live-only
             control (v023_rec_double_ctrl) over the held note.

Usage: python3 tests/v024_verify.py   (from the build tree root)
"""
import os, subprocess, sys, wave
import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# v0.30: the book opens on the song; this suite's scripts were
# written against a boot on the first lesson, now slot 1 --
# seed the persisted selection there. Navigation in the
# scripts is relative from that slot and lands as before.
os.environ["DINGHY_SEED_SEL"] = "1"
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

def max_abs_diff(a, b, t0_ms, t1_ms):
    i0, i1 = int(t0_ms * FS / 1000), int(t1_ms * FS / 1000)
    n = min(len(a), len(b), i1)
    return np.abs(a[i0:n] - b[i0:n]).max() if n > i0 else 0.0

def led_series(out):
    """film log -> list of (ms, btnLEDs). PLAY = bit 12, REC = bit 13."""
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

# ---------------------------------------------------------------- transport
run("v024_transport.txt", "v024_transport", film=2)
rows = led_series("v024_transport")
check("transport: PLAY LED on while moving (1000 ms)", led_at(rows, 1000, 12) == 1,
      f"got {led_at(rows, 1000, 12)}")
check("transport: PLAY LED off after PLAY stops it (2000 ms)", led_at(rows, 2000, 12) == 0,
      f"got {led_at(rows, 2000, 12)}")
check("transport: PLAY LED on after PLAY starts it (3000 ms)", led_at(rows, 3000, 12) == 1,
      f"got {led_at(rows, 3000, 12)}")
check("transport: REC LED on after REC arms (4000 ms)", led_at(rows, 4000, 13) == 1,
      f"got {led_at(rows, 4000, 13)}")
check("transport: REC LED off after REC disarms (5000 ms)", led_at(rows, 5000, 13) == 0,
      f"got {led_at(rows, 5000, 13)}")
check("transport: PLAY LED off after PLAY stops it (6000 ms)", led_at(rows, 6000, 12) == 0,
      f"got {led_at(rows, 6000, 12)}")
check("transport: REC LED stays off while PLAY toggles (6000 ms)", led_at(rows, 6000, 13) == 0,
      f"got {led_at(rows, 6000, 13)}")

# ---------------------------------------------------------------- knob moving
run("v024_knob_moving.txt", "v024_knob")
run("v024_knob_moving_ctrl.txt", "v024_knob_ctrl")
def shift(out, a, b):
    return bar_shift(os.path.join(OUT, f"{out}-{a}.ppm"),
                     os.path.join(OUT, f"{out}-{b}.ppm"))
s_knob = shift("v024_knob", 1500, 2500)
s_ctrl = shift("v024_knob_ctrl", 1500, 2500)
check("knob_move: KNOB4 while moving changes the scroll (vs control)",
      s_knob is not None and s_ctrl is not None and s_knob != s_ctrl,
      f"knob {s_knob} ctrl {s_ctrl}")
s_after = shift("v024_knob", 2500, 3500)
check("knob_move: tape keeps moving after the knob (~38 px/s)",
      s_after is not None and abs(s_after - 38.4) <= 4, f"got {s_after}")

# ---------------------------------------------------------------- ghost
run("v024_ghost.txt", "v024_ghost")
run("v023_rec_double_ctrl.txt", "v023_rec_ctrl")
a, b = wav(os.path.join(OUT, "v024_ghost.wav")), wav(os.path.join(OUT, "v023_rec_ctrl.wav"))
d = max_abs_diff(a, b, 1000, 3000)
check("ghost: stopped live key sounds once (WAV == PLAY control)", d == 0, f"max |diff| {d:.0f}")

print()
print("ALL V024 CHECKS PASSED" if not failures else f"{len(failures)} FAILURES: {failures}")
sys.exit(1 if failures else 0)
