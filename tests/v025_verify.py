#!/usr/bin/env python3
"""Verify v0.22: the take as Standard MIDI File data.

  midi_unit  tests/midi_test.c: VLQ, SMF parse/write round-trip
             (note / chord / JUMP marker / End of Track, Note On vel 0
             as Note Off, PPQ rescale, malformed input rejected),
             delta-time insert/remove keeping absolute times.
  mid_lesson an ordinary .mid file converts to a lesson (C5 q, E5 q,
             rest q, G5 h, 120 BPM) and, in a host build using it,
             its blue notes draw at the right positions (blue notes
             are silent guides: only the take sounds) and the tape
             stops at the piece's end bar.
  sheets     SELECT from anywhere (on the tape, no HOME): the take
             and the offset survive the round trip exactly; tempo is
             per sheet (200 on free, 72 on sheet 0); SELECT while
             moving keeps the tape moving at the new sheet's tempo.
  chords     a 3-note chord at one delta time sounds together, once
             each (WAV == PLAY-only control over the live window;
             all three fundamentals in the playback window).
  capacity   1030 notes recorded (> the old 512-note take): the last
             notes are present at the end of the tape and survive a
             SELECT round trip.

Usage: python3 tests/v025_verify.py   (from the build tree root)
"""
import hashlib
import os
import shutil
import subprocess
import sys
import wave
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
REPO = os.path.join(os.path.dirname(ROOT), "fm1-maestro")
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

def max_abs_diff(a, b, t0_ms, t1_ms):
    i0, i1 = int(t0_ms * FS / 1000), int(t1_ms * FS / 1000)
    n = min(len(a), len(b), i1)
    return np.abs(a[i0:n] - b[i0:n]).max() if n > i0 else 0.0

def band_peak(seg, f0, tol=15):
    seg = seg - seg.mean()
    w = np.hanning(len(seg))
    spec = np.abs(np.fft.rfft(seg * w))
    freqs = np.fft.rfftfreq(len(seg), 1 / FS)
    m = (freqs >= f0 - tol) & (freqs <= f0 + tol)
    return float(spec[m].max()) if m.any() else 0.0

def led_series(out):
    rows = []
    for line in open(os.path.join(OUT, out + "-log.txt")):
        p = line.split()
        if len(p) >= 4:
            rows.append((int(p[0]), int(p[3])))
    return rows

def led_at(rows, ms, bit):
    v = None
    for t, leds in rows:
        if t <= ms:
            v = (leds >> bit) & 1
        else:
            break
    return v

# ---------------------------------------------------------------- MIDI unit tests
r = subprocess.run(["./build/host/midi_test"], cwd=ROOT, capture_output=True, text=True)
print(r.stdout.strip().splitlines()[-1])
check("midi_unit: midi_test.c passes", r.returncode == 0, r.stdout.strip().splitlines()[-1] if r.returncode else "")

# ---------------------------------------------------------------- .mid as a lesson
def vlq(v):
    out = bytearray([v & 0x7F])
    v >>= 7
    while v:
        out.append(v & 0x7F)
        v >>= 7
    out = bytes(reversed(out))
    return bytes([c | 0x80 for c in out[:-1]] + [out[-1]])

def write_test_mid(path):
    name = b"Mid Test Tune"
    tr = bytearray()
    tr += vlq(0) + bytes([0xFF, 0x03, len(name)]) + name
    tr += vlq(0) + bytes([0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20])  # 500000 us = 120 BPM
    tr += vlq(0) + bytes([0x90, 60, 100])
    tr += vlq(96) + bytes([0x80, 60, 0])
    tr += vlq(0) + bytes([0x90, 64, 100])
    tr += vlq(96) + bytes([0x80, 64, 0])
    tr += vlq(96) + bytes([0x90, 67, 100])   # a quarter rest before G5
    tr += vlq(192) + bytes([0x80, 67, 0])
    tr += vlq(0) + bytes([0xFF, 0x2F, 0x00])
    mid = (b"MThd" + (6).to_bytes(4, "big") + (0).to_bytes(2, "big")
           + (1).to_bytes(2, "big") + (96).to_bytes(2, "big"))
    mid += b"MTrk" + len(tr).to_bytes(4, "big") + bytes(tr)
    open(path, "wb").write(mid)

TMP = "/tmp/v025_mid"
shutil.rmtree(TMP, ignore_errors=True)
os.makedirs(TMP + "/lessons")
write_test_mid(TMP + "/lessons/test-tune.mid")
r = subprocess.run([sys.executable, os.path.join(REPO, "tools/lessons2c.py"),
                    TMP + "/lessons", "-o", TMP + "/lessons_data.c"],
                   capture_output=True, text=True)
check("mid_lesson: lessons2c.py converts the .mid", r.returncode == 0, r.stderr.strip())
gen = open(TMP + "/lessons_data.c").read() if r.returncode == 0 else ""
for frag, name in [("{ 60, 96, 0 }", "C5 q"), ("{ 64, 96, 0 }", "E5 q"),
                   ("{ 0, 96, 1 }", "rest q"), ("{ 67, 192, 0 }", "G5 h"),
                   ('"Mid Test Tune"', "title"), ("0, 60, 120, 4", "tempo 120")]:
    check(f"mid_lesson: generated C has {name}", frag in gen)
# a host build whose only lesson is the .mid tune: its blue notes
# must draw and play.
shutil.copytree(os.path.join(ROOT, "firmware"), TMP + "/firmware")
shutil.copytree(os.path.join(ROOT, "tests"), TMP + "/tests")
shutil.copy(TMP + "/lessons_data.c", TMP + "/firmware/src/lessons_data.c")
r = subprocess.run(["cc", "-O2", "-Wall", "-Wno-unused-function", "-DDINGHY_HOST",
                    '-DAPP_VER="dev"', "-Ifirmware/hal", "-Ifirmware/src",
                    "-Ifirmware/gen", "-o", TMP + "/dinghy", "firmware/src/dinghy.c"],
                   cwd=TMP, capture_output=True, text=True)
check("mid_lesson: host build with the .mid lesson", r.returncode == 0, r.stderr.strip()[:200])
if r.returncode == 0:
    open(TMP + "/script.txt", "w").write(
        "100 btn 12\n1000 shot\n4500 shot\n5500 shot\n"
        "5700 btn 8\n5800 btn 8\n5900 enc 0 6\n6100 shot\n6500 end\n")
    subprocess.run([TMP + "/dinghy", TMP + "/out", TMP + "/script.txt"],
                   cwd=TMP, capture_output=True, text=True, check=True)
    h_lesson = scroll_hash(TMP + "/out-1000.ppm")
    h_free = scroll_hash(TMP + "/out-6100.ppm")
    check("mid_lesson: blue notes draw (lesson != clean free play)", h_lesson != h_free)
    # Blue notes are pure blue (0,0,255). v0.24: PLAY enters the piece
    # parked at its first note (scroll 20), so at 1000 ms the tape has
    # run ~0.9 s at 120 BPM (64 px/s) from there: scroll ~77, and the
    # .mid notes at piece-x 80/112/176 (C5/E5/G5) sit at screen
    # x ~3/~35/~99.
    im = read_ppm(TMP + "/out-1000.ppm")
    blue = (im[:, :, 2].astype(int) - im[:, :, 0].astype(int) > 30)
    cols = blue.sum(axis=0)
    def has_blue(x0, x1):
        return bool(cols[x0:x1 + 1].max() > 3)
    check("mid_lesson: C5 blue note at the right place", has_blue(0, 11), f"cols {cols[0:12].max()}")
    check("mid_lesson: E5 blue note at the right place", has_blue(27, 43), f"cols {cols[27:44].max()}")
    check("mid_lesson: G5 blue note at the right place", has_blue(91, 107), f"cols {cols[91:108].max()}")
    # The piece ends at the end bar (piece-x 320); at 120 BPM the
    # playhead passes it ~4.1 s after PLAY, and the tape must stop:
    # the scroll is frozen between the 4500 and 5500 ms shots.
    h4500 = scroll_hash(TMP + "/out-4500.ppm")
    h5500 = scroll_hash(TMP + "/out-5500.ppm")
    check("mid_lesson: tape stops at the piece end (scroll frozen)", h4500 == h5500)

# ---------------------------------------------------------------- per-sheet switching
run("v025_sheets.txt", "v025_sheets", film=2)
hA = scroll_hash(os.path.join(OUT, "v025_sheets-1100.ppm"))
hB = scroll_hash(os.path.join(OUT, "v025_sheets-1500.ppm"))
hC = scroll_hash(os.path.join(OUT, "v025_sheets-1900.ppm"))
check("sheets: SELECT away shows another sheet (B != A)", hB != hA)
check("sheets: take + offset survive the round trip (C == A)", hC == hA)
check("sheets: SELECT needed no HOME (on the tape, stopped)", hC == hA)
rows = led_series("v025_sheets")
check("sheets: PLAY LED on while moving before the switch", led_at(rows, 3000, 12) == 1)
check("sheets: PLAY LED stays on across a switch while moving", led_at(rows, 4000, 12) == 1)
def shift(out, a, b):
    return bar_shift(os.path.join(OUT, f"{out}-{a}.ppm"),
                     os.path.join(OUT, f"{out}-{b}.ppm"))
s200 = shift("v025_sheets", 2500, 3500)
s72 = shift("v025_sheets", 3900, 4900)
check("sheets: free-play tempo restored (200 BPM -> ~107 px/s)",
      s200 is not None and abs(s200 - 106.7) <= 4, f"got {s200}")
check("sheets: sheet-0 tempo restored (72 BPM -> ~38 px/s)",
      s72 is not None and abs(s72 - 38.4) <= 4, f"got {s72}")

# ---------------------------------------------------------------- chords
run("v025_chords.txt", "v025_chords")
run("v025_chords_ctrl.txt", "v025_chords_ctrl")
a, b = wav(os.path.join(OUT, "v025_chords.wav")), wav(os.path.join(OUT, "v025_chords_ctrl.wav"))
d = max_abs_diff(a, b, 400, 1100)
check("chords: REC chord sounds once (WAV == PLAY control)", d == 0, f"max |diff| {d:.0f}")
seg = a[int(1.65 * FS):int(2.1 * FS)]
peaks = {f: band_peak(seg, f) for f in (523.25, 587.33, 659.25)}
check("chords: playback sounds C5+E5+G5 together",
      all(v > 100 for v in peaks.values()),
      f"peaks { {k: round(v) for k, v in peaks.items()} }")

# ---------------------------------------------------------------- capacity: 1030 notes
lines = ["# v025 capacity: 1030 notes, generated",
         "100 enc 0 6", "200 enc 5 75", "300 shot",
         "400 btn 8", "500 btn 13"]
t = 600
for i in range(1030):
    mask = 1 << (i % 27)
    lines.append(f"{t} keys 0x{mask:x}")
    lines.append(f"{t + 30} keys 0")
    t += 60
lines += [f"{t + 200} btn 12", f"{t + 400} btn 13", f"{t + 600} btn 8",
          f"{t + 800} enc 5 75", f"{t + 1000} shot",
          f"{t + 1200} enc 0 1", f"{t + 1400} enc 0 -1",
          f"{t + 1600} shot", f"{t + 1800} end"]
open(os.path.join(SCRIPTS, "v025_capacity.txt"), "w").write("\n".join(lines) + "\n")
run("v025_capacity.txt", "v025_capacity")
h_clean = scroll_hash(os.path.join(OUT, "v025_capacity-300.ppm"))
h_end = scroll_hash(os.path.join(OUT, f"v025_capacity-{t + 1000}.ppm"))
h_back = scroll_hash(os.path.join(OUT, f"v025_capacity-{t + 1600}.ppm"))
check("capacity: last notes present at the end of the tape", h_end != h_clean)
check("capacity: 1030-note take survives a SELECT round trip", h_back == h_end)

print()
print("FAILURES:" if failures else "ALL V025 CHECKS PASSED", failures if failures else "")
sys.exit(1 if failures else 0)
