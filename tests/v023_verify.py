#!/usr/bin/env python3
"""Verify the v023 REC double-trigger fix.

  single   in REC, a note sounds once: the REC WAV is sample-identical
           to the PLAY control (live only) after the key press.
  seq      three notes in sequence in REC sound once each: the REC WAV
           is sample-identical to the PLAY control.
  overdub  a note recorded in pass 1 sounds when the playhead crosses
           it in REC during pass 2 (overdub monitoring), with no key
           pressed: 523 Hz peak in the 2000-2200 ms window.
  playback a note recorded in REC plays back in ST_PLAY (normal
           playback): 523 Hz peak when the playhead crosses it.
  held     a note held 2 s in REC while the playhead moves sounds once
           (covered by `single`: the 2 s hold WAV is identical to
           the control) and is recorded (it plays back: see `playback`,
           whose note was held 0.5 s).

Usage: python3 tests/v023_verify.py   (from the build tree root)
"""
import os, subprocess, sys, wave
import numpy as np

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

def run(script, out):
    cmd = [HOST, os.path.join(OUT, out), os.path.join(SCRIPTS, script)]
    subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, check=True)

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

def max_abs_diff(a, b, t0_ms, t1_ms):
    i0, i1 = int(t0_ms * FS / 1000), int(t1_ms * FS / 1000)
    n = min(len(a), len(b), i1)
    return np.abs(a[i0:n] - b[i0:n]).max() if n > i0 else 0.0

# 1) single attack in REC: REC WAV == PLAY control WAV after the key press.
run("v023_rec_double.txt", "v023_rec")
run("v023_rec_double_ctrl.txt", "v023_rec_ctrl")
a, b = wav(os.path.join(OUT, "v023_rec.wav")), wav(os.path.join(OUT, "v023_rec_ctrl.wav"))
d = max_abs_diff(a, b, 1000, 3000)
check("single: REC note sounds once (WAV == PLAY control)", d == 0, f"max |diff| {d:.0f}")

# 2) three notes in sequence in REC: each sounds once.
run("v023_seq.txt", "v023_seq")
run("v023_seq_ctrl.txt", "v023_seq_ctrl")
a, b = wav(os.path.join(OUT, "v023_seq.wav")), wav(os.path.join(OUT, "v023_seq_ctrl.wav"))
d = max_abs_diff(a, b, 1000, 2900)
check("seq: 3 notes in REC sound once each (WAV == control)", d == 0, f"max |diff| {d:.0f}")

# 3) overdub: the pass-1 C5 sounds in REC during pass 2, no key pressed.
run("v023_overdub.txt", "v023_overdub")
x = wav(os.path.join(OUT, "v023_overdub.wav"))
f, mag = fft_peak(x[int(2.05 * FS):int(2.25 * FS)])
check("overdub: pass-1 C5 sounds in REC (pass 2, no key)", abs(f - 523.25) < 5 and mag > 100,
      f"peak {f:.1f} Hz mag {mag:.0f}")
# and the pass-2 D5 (587 Hz) sounds once: its attack is a single rise.
# Compare the D5 region against a live-only D5: use the envelope dip test.
# (The D5 is the only note sounding 2600-3100 ms; a double attack would
# show a dip-and-rise in the 2 ms RMS envelope within 60 ms of the press.)
seg = x[int(2.6 * FS):int(2.75 * FS)]
win = 88
n = len(seg) // win
rms = np.sqrt((seg[:n * win].reshape(n, win) ** 2).mean(axis=1))
peak = rms.max()
# a double attack dips below 75% of the local level then rises back above 90%
dips = 0
for i in range(1, len(rms) - 2):
    if rms[i] < 0.75 * peak and rms[i + 2] > 0.90 * peak and rms[i - 1] > 0.85 * peak:
        dips += 1
check("overdub: pass-2 D5 sounds once (no dip-and-rise)", dips == 0, f"dips {dips}")

# 4) playback: the recorded C5 plays back in ST_PLAY.
run("v023_playback.txt", "v023_playback")
x = wav(os.path.join(OUT, "v023_playback.wav"))
f, mag = fft_peak(x[int(3.35 * FS):int(3.65 * FS)])
check("playback: recorded C5 sounds in ST_PLAY", abs(f - 523.25) < 5 and mag > 100,
      f"peak {f:.1f} Hz mag {mag:.0f}")

print()
print("ALL V023 CHECKS PASSED" if not failures else f"{len(failures)} FAILURES: {failures}")
sys.exit(1 if failures else 0)
