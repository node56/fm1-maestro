#!/usr/bin/env python3
"""Verify v0.23: idle screen blank.

  idle_a  no input: the book screen is up at 0.5 s; a KNOB1 detent at
          60 s resets the timer, so the screen is still up at 150 s
          (past the 120 s deadline from power-on) but fully black at
          185 s (120 s after the last input). A key press at 190 s
          wakes it -- at 190.2 s, key still held, the blue lesson
          notes are all back (same blue count as the book shot) --
          and the key sounds (WAV loud at 190 s, silent before).
  idle_b  a free-play take with a jump loops forever; the last input
          is at 1.1 s, so the screen blanks at 121.1 s mid-playback:
          the 125 s screenshot is fully black while the WAV stays
          loud on both sides of the blank point (the tape never
          stops). A KNOB1 detent at 130 s wakes it (131 s shot).

Usage: python3 tests/v026_verify.py   (from the build tree root)
"""
import os
import subprocess
import sys
import wave
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

r = subprocess.run(["cc", "-O2", "-Wall", "-Wno-unused-function", "-DDINGHY_HOST",
                    '-DAPP_VER="dev"', "-Ifirmware/hal", "-Ifirmware/src",
                    "-Ifirmware/gen", "-o", HOST, "firmware/src/dinghy.c"],
                   cwd=ROOT, capture_output=True, text=True)
check("host build", r.returncode == 0, r.stderr.strip()[:300])

def run(script, out):
    subprocess.run([HOST, os.path.join(OUT, out), os.path.join(SCRIPTS, script)],
                   cwd=ROOT, capture_output=True, text=True, check=True)

def read_ppm(path):
    return np.array(Image.open(path).convert("RGB")).astype(int)

def is_black(path):
    return read_ppm(path).max() == 0

def is_lit(path):
    return read_ppm(path).mean() > 20

def blue_px(path):
    im = read_ppm(path)
    return int((im[:, :, 2] - im[:, :, 0] > 30).sum())

def wav(path):
    with wave.open(path, "rb") as w:
        n = w.getnframes()
        data = np.frombuffer(w.readframes(n), dtype=np.int16)
    return data[0::2].astype(np.float64)

def rms(a, t0, t1):
    seg = a[int(t0 * FS):int(t1 * FS)]
    return float(np.sqrt((seg ** 2).mean())) if len(seg) else 0.0

# ---------------------------------------------------------------- idle A
run("v026_idle_a.txt", "v026_idle_a")
A = os.path.join(OUT, "v026_idle_a")
check("idle_a: book screen up at 0.5 s", is_lit(A + "-500.ppm"))
check("idle_a: input at 60 s resets the timer (still up at 150 s)",
      is_lit(A + "-150000.ppm"))
check("idle_a: fully black at 185 s (120 s after the last input)",
      is_black(A + "-185000.ppm"))
nb = blue_px(A + "-500.ppm")
check("idle_a: key press wakes -- every blue note back at 190.2 s",
      nb > 100 and blue_px(A + "-190200.ppm") == nb,
      f"blue {blue_px(A + '-190200.ppm')} vs book {nb}")
check("idle_a: screen stays on after the wake key is released",
      is_lit(A + "-191000.ppm"))
a = wav(A + ".wav")
check("idle_a: silent before the wake key (100-110 s)", rms(a, 100, 110) < 5,
      f"rms {rms(a, 100, 110):.1f}")
check("idle_a: the wake key sounds (190-190.6 s)", rms(a, 190, 190.6) > 200,
      f"rms {rms(a, 190, 190.6):.1f}")

# ---------------------------------------------------------------- idle B
run("v026_idle_b.txt", "v026_idle_b")
B = os.path.join(OUT, "v026_idle_b")
check("idle_b: fully black at 125 s (blanked mid-playback)",
      is_black(B + "-125000.ppm"))
check("idle_b: knob wakes -- sheet music back at 131 s",
      is_lit(B + "-131000.ppm"))
b = wav(B + ".wav")
check("idle_b: take playing before the blank (115-121 s)", rms(b, 115, 121) > 200,
      f"rms {rms(b, 115, 121):.1f}")
check("idle_b: take still playing after the blank (122-128 s)", rms(b, 122, 128) > 200,
      f"rms {rms(b, 122, 128):.1f}")

print()
print("FAILURES:" if failures else "ALL V026 CHECKS PASSED", failures if failures else "")
sys.exit(1 if failures else 0)
