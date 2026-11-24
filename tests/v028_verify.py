#!/usr/bin/env python3
"""Maestro v0.25 checks: boot state, clean retries, HOME as the
manual retry, and the end-of-take replay.

  boot       the device powers on on the tape, parked at the
             persisted piece's first note -- exactly the state
             PLAY+HOME used to be the ritual to reach. The boot
             parked view is pixel-identical to the view the
             ritual + REC re-entry produces, and a first key
             press at boot is a parked press (it kicks the take
             off; the sheet rolls from the park, never jumps
             sideways -- the v0.24 bug shifted it +60 px).
  retry      a terrible take replays first (its wrong note is
             audible in the replay), then the same piece reloads
             with the take CLEARED: during the retry's roll, the
             old take's note position is silent (A/B against the
             replay, where the same note sounded).
  home       HOME on a book piece with a take -- stopped or
             still rolling -- clears the take and parks (one
             press = go again); HOME again, parked with an empty
             take, reaches the book view.
  inspect    a good take missing exactly one note (D5) replays
             with that note drawn AMBER and the matched notes
             still blue (no amber anywhere during the take
             itself); the replay sounds the recorded notes; when
             it finishes, the next piece loads.
  skip       a key press mid-replay skips straight to the load.
  keep       a good take is still intact on its sheet after the
             replay and the auto-load: SELECT back and PLAY, and
             the recorded first note sounds.

Piece 0 at 72 BPM: 38.4 px/s; first note C5 (MIDI 72, key 7,
mask 0x80) at piece-x 80; end bar at 576. A full take ends at
~13.4 s; its replay takes another ~13.4 s.
"""
import hashlib
import os
import subprocess
import sys
import wave

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
# v0.30: the book opens on the song; this suite works the first
# lesson (72 BPM), which is now slot 1 -- seed the persisted
# selection there. Its navigation is relative from that slot.
os.environ["DINGHY_SEED_SEL"] = "1"
ROOT = os.path.dirname(HERE)
HOST = os.path.join(ROOT, "build", "host")
BIN = os.path.join(HOST, "dinghy")
SCRIPTS = os.path.join(HERE, "scripts")
FAILS = []


def check(name, cond, extra=""):
    print(("ok   " if cond else "FAIL "), name, " ", extra if not cond else "")
    if not cond:
        FAILS.append(name)


def build():
    r = subprocess.run(
        ["cc", "-O2", "-Wno-unused-function", "-DDINGHY_HOST", '-DAPP_VER="dev"',
         "-Ifirmware/hal", "-Ifirmware/src", "-Ifirmware/gen",
         "-o", BIN, "firmware/src/dinghy.c"],
        cwd=ROOT, capture_output=True, text=True)
    check("host build", r.returncode == 0, r.stderr[-400:])


def run(script, out, film=None):
    args = [BIN, os.path.join(HOST, out), os.path.join(SCRIPTS, script)]
    if film:
        args.append(str(film))
    r = subprocess.run(args, cwd=ROOT, capture_output=True, text=True)
    if r.returncode != 0:
        check(f"run {script}", False, r.stderr[-300:])


def shot(out, ms):
    return os.path.join(HOST, f"{out}-{ms}.ppm")


def scroll_hash(path):
    im = np.array(Image.open(path).convert("RGB"))
    return hashlib.sha256(im[48:204].tobytes()).hexdigest()


def leftmost_blue(path):
    im = np.array(Image.open(path).convert("RGB")).astype(int)
    m = (im[:, :, 2] - im[:, :, 0] > 30)
    cols = m[48:204].sum(axis=0)
    xs = [x for x in range(240) if cols[x] > 3]
    return min(xs) if xs else None


def amber_px(path):
    im = np.array(Image.open(path).convert("RGB")).astype(int)[48:204]
    m = (im[:, :, 0] > 200) & (im[:, :, 1] > 150) & (im[:, :, 2] < 80)
    return int(m.sum())


def blue_px(path):
    im = np.array(Image.open(path).convert("RGB")).astype(int)[48:204]
    m = (im[:, :, 2] > 150) & (im[:, :, 0] < 80) & (im[:, :, 1] < 80)
    return int(m.sum())


def led_series(out):
    rows = []
    for line in open(os.path.join(HOST, f"{out}-log.txt")):
        p = line.split()
        if len(p) >= 4:
            rows.append((int(p[0]), int(p[3])))
    return rows


def led_at(rows, ms, bit):
    v = 0
    for t, mask in rows:
        if t > ms:
            break
        v = (mask >> bit) & 1
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
    for line in open(os.path.join(HOST, f"{out}-log.txt")):
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


def wav_rms(out, t0, t1):
    w = wave.open(os.path.join(HOST, f"{out}.wav"))
    sr = w.getframerate()
    w.setpos(int(t0 * sr))
    d = np.frombuffer(w.readframes(int((t1 - t0) * sr)), dtype=np.int16).astype(float)
    return float(np.sqrt((d ** 2).mean())) if len(d) else 0.0


def peak_hz(out, t0, t1):
    w = wave.open(os.path.join(HOST, f"{out}.wav"))
    sr = w.getframerate()
    w.setpos(int(t0 * sr))
    d = np.frombuffer(w.readframes(int((t1 - t0) * sr)), dtype=np.int16).astype(float)
    if not len(d):
        return 0.0
    d = d * np.hanning(len(d))
    sp = np.abs(np.fft.rfft(d))
    return float(np.argmax(sp) * sr / len(d))


def gen_take(out_script, skip_x=None):
    """A well-played piece-0 take (the v027 good-take pattern):
    the kick-off press at the park (500), then each blue note's
    key pressed as the playhead passes it (120 ms early, held
    200 ms), optionally skipping the note at piece-x skip_x.
    Then a during-take shot, a during-replay shot, and a
    post-load shot."""
    notes = [(80, 0x80), (112, 0x200), (144, 0x800), (176, 0x1000),
             (208, 0x4000), (240, 0x1000), (272, 0x800), (304, 0x200),
             (336, 0x80), (368, 0x200), (400, 0x800), (432, 0x200),
             (464, 0x80)]
    ev = [(100, "btn 13")]
    for x, mask in notes:
        if x == skip_x:
            continue
        t = 500 if x == 80 else 500 + round((x - 80) * 1000 / 38.4) - 120
        ev.append((t, f"keys 0x{mask:x}"))
        ev.append((t + 200, "keys 0"))
    ev += [(1500, "shot"), (14600, "shot"), (27000, "shot"), (28500, "end")]
    ev.sort()
    with open(os.path.join(SCRIPTS, out_script), "w") as f:
        f.write("T\n")
        for t, a in ev:
            f.write(f"{t} {a}\n")


build()

# ------------------------------------------------------------ references
run("v027_ref.txt", "v028_ref")
park0_hash = scroll_hash(shot("v028_ref", 900))
park1_hash = scroll_hash(shot("v028_ref", 1300))
book_hash = scroll_hash(shot("v028_ref", 500))

# ------------------------------------------------------------ boot
run("v028_boot_a.txt", "v028_boot_a", film=2)
check("boot: powers on parked (view == the parked reference)",
      scroll_hash(shot("v028_boot_a", 500)) == park0_hash)
lb0 = leftmost_blue(shot("v028_boot_a", 500))
check("boot: first note at the cursor", lb0 is not None and 48 <= lb0 <= 62, f"got {lb0}")
rows = led_series("v028_boot_a")
check("boot: the first press kicks the take off (PLAY red steady as it rolls) (v0.27 colour; was beat flash)",
      bool(bit_in(rows, 12, 1400, 2900)) and all(b == 1 for b in bit_in(rows, 12, 1400, 2900)),
      f"got {bit_in(rows, 12, 1400, 2900)}")
lb1 = leftmost_blue(shot("v028_boot_a", 1600))
check("boot: the sheet rolls left from the park (no +60 px jump)",
      lb1 is not None and lb0 is not None and 0 < lb1 < lb0, f"{lb0} -> {lb1}")
run("v028_boot_b.txt", "v028_boot_b", film=2)
check("boot: the PLAY+HOME ritual + REC lands on the identical view",
      scroll_hash(shot("v028_boot_b", 3000)) == scroll_hash(shot("v028_boot_a", 500)))

# ------------------------------------------------------------ terrible retry
run("v028_retry.txt", "v028_retry", film=2)
rows = led_series("v028_retry")
check("retry: the take ends into a replay (PLAY on at 15000)",
      led_at(rows, 15000, 12) == 1, f"got {led_at(rows, 15000, 12)}")
pk = peak_hz("v028_retry", 15.35, 15.95)
check("retry: the failed take sounds in its replay (F#3 ~185 Hz)",
      165 < pk < 205, f"peak {pk:.0f} Hz")
check("retry: after the replay the piece is parked, take cleared",
      scroll_hash(shot("v028_retry", 28000)) == park0_hash)
check("retry: the old take is silent during the retry's roll",
      wav_rms("v028_retry", 30.05, 30.65) < 120,
      f"rms {wav_rms('v028_retry', 30.05, 30.65):.0f}")
pk2 = peak_hz("v028_retry", 30.05, 30.65)
check("retry: no F#3 peak at the old note's position",
      not (165 < pk2 < 205), f"peak {pk2:.0f} Hz")

# ------------------------------------------------------------ replay skip
run("v028_skip.txt", "v028_skip", film=2)
srows = led_series("v028_skip")
check("skip: a key press mid-replay loads at once (PLAY green at 16000: parked) (v0.27 colour; was LED on)",
      play_at(play_series("v028_skip"), 16000) == (255, 0),
      f"got {play_at(play_series('v028_skip'), 16000)}")
check("skip: the no-skip run is still replaying at 16000 (A/B)",
      led_at(rows, 16000, 12) == 1, f"got {led_at(rows, 16000, 12)}")
check("skip: the skipped load is the same piece, cleared, parked",
      scroll_hash(shot("v028_skip", 16000)) == park0_hash)

# ------------------------------------------------------------ HOME retry
run("v028_home.txt", "v028_home", film=2)
check("home: a stopped take is cleared and parked by HOME",
      scroll_hash(shot("v028_home", 4000)) == park0_hash)
check("home: HOME again (parked, empty take) reaches the book",
      scroll_hash(shot("v028_home", 5000)) == book_hash)
lb = leftmost_blue(shot("v028_home", 5000))
check("home: the book page shows the first note at x ~80",
      lb is not None and 68 <= lb <= 84, f"got {lb}")
run("v028_home_roll.txt", "v028_home_roll", film=2)
check("home: a rolling take is stopped, cleared and parked by HOME",
      scroll_hash(shot("v028_home_roll", 3000)) == park0_hash)
hrows = led_series("v028_home_roll")
check("home: PLAY green after the rolling HOME retry (parked) (v0.27 colour; was LED on)",
      play_at(play_series("v028_home_roll"), 3200) == (255, 0),
      f"got {play_at(play_series('v028_home_roll'), 3200)}")

# ------------------------------------------------------------ replay inspection
gen_take("v028_inspect.txt", skip_x=112)   # miss exactly D5
run("v028_inspect.txt", "v028_inspect", film=2)
check("inspect: no amber anywhere during the take itself",
      amber_px(shot("v028_inspect", 1500)) == 0,
      f"got {amber_px(shot('v028_inspect', 1500))}")
check("inspect: the missed note (D5) draws amber in the replay",
      amber_px(shot("v028_inspect", 14600)) > 20,
      f"got {amber_px(shot('v028_inspect', 14600))}")
check("inspect: matched notes stay blue in the replay",
      blue_px(shot("v028_inspect", 14600)) > 100,
      f"got {blue_px(shot('v028_inspect', 14600))}")
check("inspect: the replay sounds the take's notes (G5 ~17.2 s)",
      wav_rms("v028_inspect", 17.05, 17.45) > 200,
      f"rms {wav_rms('v028_inspect', 17.05, 17.45):.0f}")
check("inspect: a finished replay loads the next piece, parked",
      scroll_hash(shot("v028_inspect", 27000)) == park1_hash)

# ------------------------------------------------------------ good take kept
gen_take("v028_keep.txt")
lines = open(os.path.join(SCRIPTS, "v028_keep.txt")).read().splitlines()
lines = [l for l in lines if not l.endswith(" shot") and not l.endswith(" end")]
lines += ["27000 shot", "27500 enc 0 -1", "27800 shot", "28000 btn 12", "30000 end"]
lines.sort(key=lambda l: int(l.split()[0]) if l.split()[0].isdigit() else 0)
open(os.path.join(SCRIPTS, "v028_keep.txt"), "w").write("\n".join(lines) + "\n")
run("v028_keep.txt", "v028_keep", film=2)
check("keep: back on piece 0, its take is still on the sheet",
      scroll_hash(shot("v028_keep", 27800)) != park0_hash)
check("keep: the kept take sounds from the top (recorded C5)",
      wav_rms("v028_keep", 28.05, 28.65) > 200,
      f"rms {wav_rms('v028_keep', 28.05, 28.65):.0f}")

print()
if FAILS:
    print("FAILURES:", FAILS)
    sys.exit(1)
print("ALL V028 CHECKS PASSED")
