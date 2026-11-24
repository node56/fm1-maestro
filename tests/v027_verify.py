#!/usr/bin/env python3
"""Verify v0.24: the practice loop, added on top of the tape recorder.

  park       a book piece is entered / switched to parked at its first
             note: the first blue note sits at the cursor (x ~60),
             where the book page shows it at x ~80.
  wrong      a wrong first note while parked sounds, but starts
             nothing and records nothing (tape stopped, view frozen).
  kick       the correct first note kicks off REC: write bit on, tape
             rolls, the note is recorded (proved by A/B playback:
             it sounds when the playhead crosses it later).
  kick_free  the kick-off also works with REC never armed (it turns
             the write bit on itself).
  scroll     KNOB4/KNOB3 still scrub while parked, and KNOB4 still
             snaps the playhead while a take rolls.
  good       a well-played take (every blue note pressed as the
             playhead passes) plays back once (v0.25 replay), then
             auto-loads the NEXT piece, parked at its first note,
             tape stopped.
  terrible   a take with 1 of 13 notes matched replays, then
             reloads the SAME piece, parked (take cleared).
  playalong  PLAY without REC still just stops at the end (no load).
  free       Free Play is unchanged: an arbitrary note with REC armed
             still auto-starts the tape.
  chord      (custom one-lesson build) a first-note CHORD needs all
             its pitches: one pitch alone starts nothing (but
             sounds); the second pitch kicks the take off.

Piece 0 = First Five Notes (72 BPM, 38.4 px/s): 13 notes at piece-x
80,112,...,464 (C5 D5 E5 F5 G5 F5 E5 D5 C5 D5 E5 D5 C5), end bar 576.
Piece 1 = Ode to Joy, first note E5 at piece-x 80.

In the v0.30 book order the song is slot 0 and these two lessons
are slots 1 and 2, so the suite seeds the persisted selection at
1 (the "piece 0" of this suite) for every run; all script
navigation is relative from there (SELECT +1 reaches "piece 1",
+6 reaches Free Play), and the chord build's one lesson likewise
sits at slot 1 of its book.

Usage: python3 tests/v027_verify.py   (from the build tree root)
"""
import hashlib
import os
import shutil
import subprocess
import sys
import wave
import numpy as np
from PIL import Image

os.environ["DINGHY_SEED_SEL"] = "1"   # "piece 0" (see docstring)
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

def run(script, out, film=None, host=None, script_path=None):
    cmd = [host or HOST, os.path.join(OUT, out) if not host else out,
           script_path or os.path.join(SCRIPTS, script)]
    if film:
        cmd.append(str(film))
    subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, check=True)

def read_ppm(path):
    return np.array(Image.open(path).convert("RGB")).astype(int)

def scroll_hash(path):
    im = np.array(Image.open(path).convert("RGB"))
    return hashlib.md5(im[48:204].tobytes()).hexdigest()

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

def blue_mask(path):
    im = read_ppm(path)
    return (im[:, :, 2] - im[:, :, 0] > 30)

def leftmost_blue(path):
    cols = blue_mask(path)[48:204].sum(axis=0)
    xs = [x for x in range(240) if cols[x] > 3]
    return min(xs) if xs else None

def blue_centroid_row(path):
    """mean row of the blue pixels near the cursor (the parked note)."""
    m = blue_mask(path)[100:210, 45:76]
    rows = np.nonzero(m.any(axis=1))[0]
    if len(rows) == 0:
        return None
    ys, _ = np.nonzero(m)
    return float(ys.mean()) + 100

def dark_ink(path, x0, x1, y0, y1):
    im = read_ppm(path)
    box = im[y0:y1, x0:x1, :]
    return int((box.sum(axis=2) < 200).sum())

def wav(path):
    with wave.open(path, "rb") as w:
        n = w.getnframes()
        data = np.frombuffer(w.readframes(n), dtype=np.int16)
    return data[0::2].astype(np.float64)

def rms(a, t0, t1):
    seg = a[int(t0 * FS):int(t1 * FS)]
    return float(np.sqrt((seg ** 2).mean())) if len(seg) else 0.0

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

def shot(out, ms):
    return os.path.join(OUT, f"{out}-{ms}.ppm")

# ---------------------------------------------------------------- park / refs
# v0.25: the ref script boots parked (100), goes to the book with
# HOME (500), re-enters parked with REC (900), SELECTs piece 1 (1300).
run("v027_ref.txt", "v027_ref")
lb_book = leftmost_blue(shot("v027_ref", 500))
lb_park0 = leftmost_blue(shot("v027_ref", 900))
lb_park1 = leftmost_blue(shot("v027_ref", 1300))
check("park: book page shows the first note at x ~80", lb_book is not None and 68 <= lb_book <= 84,
      f"got {lb_book}")
check("park: piece 0 entered parked (first note at the cursor)", lb_park0 is not None and 48 <= lb_park0 <= 62,
      f"got {lb_park0}")
check("park: SELECT on the tape parks piece 1 too", lb_park1 is not None and 48 <= lb_park1 <= 62,
      f"got {lb_park1}")
cent0 = blue_centroid_row(shot("v027_ref", 900))
cent1 = blue_centroid_row(shot("v027_ref", 1300))
check("park: piece 0's first note is C5, piece 1's is E5 (rows differ)",
      cent0 is not None and cent1 is not None and abs(cent0 - cent1) > 8,
      f"C5 row {cent0}, E5 row {cent1}")
park0_hash = scroll_hash(shot("v027_ref", 900))
park1_hash = scroll_hash(shot("v027_ref", 1300))

# ---------------------------------------------------------------- wrong note
run("v027_wrong.txt", "v027_wrong", film=2)
rows = led_series("v027_wrong")
check("wrong: PLAY green (still parked: the tape never starts) (v0.27 colour; was LED on)",
      play_at(play_series("v027_wrong"), 2000) == (255, 0),
      f"got {play_at(play_series('v027_wrong'), 2000)}")
check("wrong: REC LED pulses (armed, just not started)",
      0 in bit_in(rows, 13, 1200, 3400) and 1 in bit_in(rows, 13, 1200, 3400),
      f"got {bit_in(rows, 13, 1200, 3400)}")
check("wrong: view frozen at the park (nothing recorded, nothing moved)",
      scroll_hash(shot("v027_wrong", 3000)) == scroll_hash(shot("v027_wrong", 400)))
a = wav(os.path.join(OUT, "v027_wrong.wav"))
check("wrong: the wrong note sounds (live ghost)", rms(a, 1.0, 1.6) > 200,
      f"rms {rms(a, 1.0, 1.6):.1f}")

# ---------------------------------------------------------------- kick-off, armed
run("v027_kick.txt", "v027_kick", film=2)
rows = led_series("v027_kick")
check("kick: PLAY LED on after the kick (tape rolls)", led_at(rows, 2000, 12) == 1,
      f"got {led_at(rows, 2000, 12)}")
check("kick: REC LED on (write bit on)", led_at(rows, 2000, 13) == 1,
      f"got {led_at(rows, 2000, 13)}")
s = bar_shift(shot("v027_kick", 400), shot("v027_kick", 2900))
check("kick: the tape rolled ~1.9 s at 72 BPM from the park", s is not None and abs(s - 73) <= 6,
      f"shift {s}")
# Recording proof, A/B (the take note sits exactly under its blue
# guide, so it is not visible): SELECT away and back WHILE THE TAPE
# ROLLS (SELECT never stops the transport). The switch back parks at
# the first note with the take intact, and the still-rolling tape
# sounds the recorded C5 as it leaves the park (~3.0 s). The control
# presses a wrong note instead: no kick-off, empty take, silence.
run("v027_kick_rec.txt", "v027_kick_rec")
run("v027_kick_rec_ctrl.txt", "v027_kick_rec_ctrl")
a_kick = wav(os.path.join(OUT, "v027_kick_rec.wav"))
a_ctrl = wav(os.path.join(OUT, "v027_kick_rec_ctrl.wav"))
rk, rc = rms(a_kick, 3.0, 3.7), rms(a_ctrl, 3.0, 3.7)
check("kick: the kicked note is recorded (sounds when the tape rolls over it)",
      rk > 200 and rk > 2 * rc, f"rms kick {rk:.1f} vs ctrl {rc:.1f}")

# ---------------------------------------------------------------- kick-off, unarmed
run("v027_kick_unarmed.txt", "v027_kick_unarmed", film=2)
rows = led_series("v027_kick_unarmed")
check("kick(unarmed): REC LED off before the press", led_at(rows, 700, 13) == 0,
      f"got {led_at(rows, 700, 13)}")
check("kick(unarmed): PLAY green before the press (parked) (v0.27 colour; was LED on)",
      play_at(play_series("v027_kick_unarmed"), 700) == (255, 0),
      f"got {play_at(play_series('v027_kick_unarmed'), 700)}")
check("kick(unarmed): the kick-off turns REC on", led_at(rows, 1500, 13) == 1,
      f"got {led_at(rows, 1500, 13)}")
check("kick(unarmed): the kick-off starts the tape (PLAY red steady as it rolls) (v0.27 colour; was beat flash)",
      bool(play_in(play_series("v027_kick_unarmed"), 1100, 2900))
      and all(gr == 0 and rd == 255 for gr, rd in play_in(play_series("v027_kick_unarmed"), 1100, 2900)),
      f"got {play_in(play_series('v027_kick_unarmed'), 1100, 2900)[:4]}")
check("kick(unarmed): the view scrolled after the kick",
      scroll_hash(shot("v027_kick_unarmed", 2500)) != scroll_hash(shot("v027_kick_unarmed", 900)))

# ---------------------------------------------------------------- scrolling
run("v027_scroll_park.txt", "v027_scroll_park")
s1 = bar_shift(shot("v027_scroll_park", 300), shot("v027_scroll_park", 700))
s2 = bar_shift(shot("v027_scroll_park", 700), shot("v027_scroll_park", 1100))
check("scroll: KNOB4 scrubs a parked piece one beat (+32 px)", s1 is not None and abs(s1 - 32) <= 2,
      f"got {s1}")
check("scroll: KNOB3 scrubs a parked piece fine (+4 px)", s2 is not None and abs(s2 - 4) <= 2,
      f"got {s2}")
run("v027_scroll_take.txt", "v027_scroll_take")
run("v027_scroll_take_ctrl.txt", "v027_scroll_take_ctrl")
st = bar_shift(shot("v027_scroll_take", 2000), shot("v027_scroll_take", 4000))
sc = bar_shift(shot("v027_scroll_take_ctrl", 2000), shot("v027_scroll_take_ctrl", 4000))
check("scroll: take rolls on after the kick (control ~77 px in 2 s)",
      sc is not None and 73 <= sc <= 80, f"got {sc}")
# KNOB4 at 2.5 s: the playhead sits at px 137.6, mid-beat; the coarse
# knob snaps it forward to the next beat (144), +6.4 px -- so the take
# run's 2 s shift exceeds the control's by just that snap distance,
# and the tape keeps rolling afterwards.
check("scroll: KNOB4 during a take snaps the playhead to the beat",
      st is not None and 80 <= st <= 86 and sc is not None and 3 <= st - sc <= 10,
      f"got {st} (ctrl {sc})")

# ---------------------------------------------------------------- a good take -> next piece
NOTES0 = [(80, 0x80), (112, 0x200), (144, 0x800), (176, 0x1000), (208, 0x4000),
          (240, 0x1000), (272, 0x800), (304, 0x200), (336, 0x80), (368, 0x200),
          (400, 0x800), (432, 0x200), (464, 0x80)]
lines = ["T", "# v027 good take: kick off, then press every note as the",
         "# playhead passes it (press 120 ms early, inside the match window)",
         "100 btn 13"]
for x, mask in NOTES0:
    if x == 80:
        t = 500                      # the kick-off press, at the park
    else:
        t = 500 + round((x - 80) * 1000 / 38.4) - 120
    lines.append(f"{t} keys 0x{mask:x}")
    lines.append(f"{t + 200} keys 0")
lines += ["28000 shot", "29500 shot", "31000 end"]
open(os.path.join(SCRIPTS, "v027_good.txt"), "w").write("\n".join(lines) + "\n")
run("v027_good.txt", "v027_good", film=2)
rows = led_series("v027_good")
check("good: the take ends into a replay (PLAY still on at 15000)",
      led_at(rows, 15000, 12) == 1, f"got {led_at(rows, 15000, 12)}")
check("good: PLAY green after the replay (auto-loaded, parked) (v0.27 colour; was LED on)",
      play_at(play_series("v027_good"), 28500) == (255, 0),
      f"got {play_at(play_series('v027_good'), 28500)}")
check("good: the next piece (1) is loaded, parked at its first note",
      scroll_hash(shot("v027_good", 28000)) == park1_hash)
check("good: the loaded piece stays parked (28000 == 29500)",
      scroll_hash(shot("v027_good", 29500)) == scroll_hash(shot("v027_good", 28000)))
centg = blue_centroid_row(shot("v027_good", 28000))
check("good: the parked note is piece 1's E5 (row matches)",
      centg is not None and cent1 is not None and abs(centg - cent1) <= 3,
      f"row {centg} vs ref {cent1}")

# ---------------------------------------------------------------- a terrible take -> same piece
run("v027_terrible.txt", "v027_terrible", film=2)
rows = led_series("v027_terrible")
check("terrible: the take ends into a replay (PLAY still on at 15000)",
      led_at(rows, 15000, 12) == 1, f"got {led_at(rows, 15000, 12)}")
check("terrible: PLAY green after the replay (reloaded, parked) (v0.27 colour; was LED on)",
      play_at(play_series("v027_terrible"), 28500) == (255, 0),
      f"got {play_at(play_series('v027_terrible'), 28500)}")
check("terrible: the reloaded piece stays parked (28000 == 29500)",
      scroll_hash(shot("v027_terrible", 29500)) == scroll_hash(shot("v027_terrible", 28000)))
centt = blue_centroid_row(shot("v027_terrible", 28000))
check("terrible: the SAME piece reloaded (parked note is piece 0's C5)",
      centt is not None and cent0 is not None and abs(centt - cent0) <= 3
      and cent1 is not None and abs(centt - cent1) > 8,
      f"row {centt} vs piece0 {cent0} / piece1 {cent1}")
lb_t = leftmost_blue(shot("v027_terrible", 28000))
check("terrible: reloaded parked at the first note (note at the cursor)",
      lb_t is not None and 48 <= lb_t <= 62, f"got {lb_t}")

# ---------------------------------------------------------------- play-along still stops
run("v027_playalong.txt", "v027_playalong", film=2)
rows = led_series("v027_playalong")
check("playalong: PLAY LED off after the end (stopped)",
      led_at(rows, 15000, 12) == 0, f"got {led_at(rows, 15000, 12)}")
h_end1 = scroll_hash(shot("v027_playalong", 14000))
h_end2 = scroll_hash(shot("v027_playalong", 15500))
check("playalong: the view is the stopped end, not a parked piece",
      h_end1 == h_end2 and h_end1 != park0_hash and h_end1 != park1_hash)

# ---------------------------------------------------------------- free play unchanged
run("v027_free.txt", "v027_free", film=2)
rows = led_series("v027_free")
check("free: an arbitrary note with REC armed still auto-starts (PLAY on)",
      led_at(rows, 1500, 12) == 1, f"got {led_at(rows, 1500, 12)}")
check("free: REC LED on", led_at(rows, 1500, 13) == 1,
      f"got {led_at(rows, 1500, 13)}")

# ---------------------------------------------------------------- chord kick-off (custom build)
TMP = "/tmp/v027_chord"
shutil.rmtree(TMP, ignore_errors=True)
shutil.copytree(os.path.join(ROOT, "firmware"), TMP + "/firmware")
shutil.copytree(os.path.join(ROOT, "tests"), TMP + "/tests")
open(TMP + "/firmware/src/lessons_data.c", "w").write(
    "/* Hand-written for the v027 chord kick-off test: the first note\n"
    " * is a two-pitch chord (a zero-tick C5 under an E5). */\n"
    "#include \"lesson.h\"\n\n"
    "static const lesson_t lesson_chord_test = {\n"
    "    \"Chord Test\", \"v027 host test\", KIND_MELODY,\n"
    "    0, 60, 72, 4,\n"
    "    4,\n"
    "    {\n"
    "        { 60, 0, 0 },\n"
    "        { 64, 96, 0 },\n"
    "        { 67, 96, 0 },\n"
    "        { 60, 192, 0 }\n"
    "    }\n"
    "};\n\n"
    "const lesson_t *all_lessons[] = { &lesson_chord_test };\n"
    "const uint8_t all_lessons_count = 1;\n"
    "/* stub song (v0.26): the firmware references ode_song; this\n"
    " * variant has no song, so it provides an empty one. */\n"
    "const song_t ode_song = { \"Ode to Joy\", 96, 4, 1, { { 9, \"Percussion\", 0, 0 } } };\n")
r = subprocess.run(["cc", "-O2", "-Wall", "-Wno-unused-function", "-DDINGHY_HOST",
                    '-DAPP_VER="dev"', "-Ifirmware/hal", "-Ifirmware/src",
                    "-Ifirmware/gen", "-o", TMP + "/dinghy", "firmware/src/dinghy.c"],
                   cwd=TMP, capture_output=True, text=True)
check("chord: host build with the chord lesson", r.returncode == 0, r.stderr.strip()[:200])
if r.returncode == 0:
    open(TMP + "/script.txt", "w").write(
        "T\n"
        "100 btn 13\n"
        "1000 keys 0x80\n"     # C5 alone: half the chord -- no kick-off
        "1500 keys 0\n"
        "2000 keys 0x800\n"    # E5 completes the chord -- kick-off
        "2600 keys 0\n"
        "4000 end\n")
    subprocess.run([TMP + "/dinghy", TMP + "/out", TMP + "/script.txt", "2"],
                   cwd=TMP, capture_output=True, text=True, check=True)
    rows = []
    prows = []
    for line in open(TMP + "/out-log.txt"):
        p = line.split()
        if len(p) >= 4:
            rows.append((int(p[0]), int(p[3])))
        if len(p) >= 15:
            prows.append((int(p[0]), int(p[11]), int(p[12])))
    def led(ms, bit):
        v = None
        for t, leds in rows:
            if t <= ms:
                v = (leds >> bit) & 1
            else:
                break
        return v
    def play(ms):
        v = None
        for t, g, r in prows:
            if t <= ms:
                v = (g, r)
            else:
                break
        return v
    check("chord: one pitch alone does not kick off (PLAY green at 1500 ms: still parked) (v0.27 colour; was LED on)",
          play(1500) == (255, 0), f"got {play(1500)}")
    check("chord: the second pitch kicks off (PLAY red steady as it rolls) (v0.27 colour; was beat flash)",
          bool([(g, r) for (t, g, r) in prows if 2050 <= t <= 3950])
          and all(g == 0 and r == 255 for (t, g, r) in prows if 2050 <= t <= 3950),
          f"got {[(g, r) for (t, g, r) in prows if 2050 <= t <= 3950][:4]}")
    check("chord: REC on after the kick", led(2500, 13) == 1,
          f"got {led(2500, 13)}")
    a = wav(TMP + "/out.wav")
    check("chord: the lone pitch still sounded (live ghost)", rms(a, 1.0, 1.5) > 200,
          f"rms {rms(a, 1.0, 1.5):.1f}")

print()
print("FAILURES:" if failures else "ALL V027 CHECKS PASSED", failures if failures else "")
sys.exit(1 if failures else 0)
