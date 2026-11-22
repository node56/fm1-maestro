#!/usr/bin/env python3
"""Maestro v0.26 hardware report: "pressing the first note does not
start the session" -- the parked kick-off fails on the real FM-1
while the host suites pass. Two suspect classes:

  cold   the persisted-selection boot paths. The host store is RAM
         (lost at exit), so every earlier suite booted at sel 0;
         the DINGHY_SEED_SEL hook in tests/host.c now pre-seeds
         blob 0 with the settings blob a previous run would have
         saved, and each book entry is booted cold and kicked.
  park   the kick-off demands timestamp == start_x() EXACTLY.
         Host scripts always kick from a surgically exact park;
         a human gets epsilon-off parks: a brief PLAY roll stopped
         a few px past the first note, a fine-scrub detent, a
         coarse scrub back to the sheet start (CURSOR_X, before
         the first note). Presses there SOUND (a ghost) but can
         never start a take -- and look parked. An octave shift
         is the control perturbation: the press is the wrong
         pitch, sounds, and must NOT kick off, before or after.

Detection: the scroll region (rows 48..203) hashed per screenshot;
a rolling tape changes it, a parked one never does. A kick-off is
"rolls at shot2 and still rolls at shot3"; a dead press is
"identical hashes across the press".
"""
import hashlib
import os
import subprocess
import sys
import wave

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
HOST = os.path.join(ROOT, "build", "host")
BIN = os.path.join(HOST, "dinghy")
SCR = os.path.join(HERE, "scripts")
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


def write_script(name, ev, end):
    path = os.path.join(SCR, name)
    with open(path, "w") as f:
        f.write("100 T\n")
        for t, text in ev:
            f.write(f"{t} {text}\n")
        f.write(f"{end} end\n")
    return name


def run(script, out, seed=None, film=None):
    args = [BIN, os.path.join(HOST, out), os.path.join(SCR, script)]
    if film:
        args.append(str(film))
    env = dict(os.environ)
    if seed is not None:
        env["DINGHY_SEED_SEL"] = str(seed)
    r = subprocess.run(args, cwd=ROOT, capture_output=True, text=True, env=env)
    if r.returncode != 0:
        check(f"run {script}", False, r.stderr[-300:])


def scroll_hash(path):
    im = np.array(Image.open(path).convert("RGB"))
    return hashlib.sha256(im[48:204].tobytes()).hexdigest()


def shot(out, ms):
    return os.path.join(HOST, f"{out}-{ms}.ppm")


def rolls(out, parked_ms, ms2, ms3):
    """the tape rolled after the press: both later views differ
    from the parked one and from each other."""
    h0 = scroll_hash(shot(out, parked_ms))
    h2 = scroll_hash(shot(out, ms2))
    h3 = scroll_hash(shot(out, ms3))
    return h0 != h2 and h2 != h3


def still(out, ms_list):
    hs = [scroll_hash(shot(out, ms)) for ms in ms_list]
    return all(h == hs[0] for h in hs)


def wav_rms(out, t0, t1):
    w = wave.open(os.path.join(HOST, f"{out}.wav"))
    sr = w.getframerate()
    nch = w.getnchannels()
    a = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(float)
    if nch > 1:
        a = a.reshape(-1, nch)[:, 0].copy()
    d = a[int(t0 * sr):int(t1 * sr)]
    return float(np.sqrt((d ** 2).mean())) if len(d) else 0.0


def led_bits(out, bit, t0, t1):
    vals = []
    for line in open(os.path.join(HOST, f"{out}-log.txt")):
        p = line.split()
        if len(p) >= 4 and p[0].isdigit() and t0 <= int(p[0]) <= t1:
            vals.append((int(p[3]) >> bit) & 1)
    return vals


def play_duties(out, t0, t1):
    """(green, red) duty samples of PLAY's colour channels within
    [t0, t1] ms (v0.27; film-log fields 11/12)."""
    vals = []
    for line in open(os.path.join(HOST, f"{out}-log.txt")):
        p = line.split()
        if len(p) >= 15 and p[0].isdigit() and t0 <= int(p[0]) <= t1:
            vals.append((int(p[11]), int(p[12])))
    return vals


build()

# ---------- cold: boot with a persisted selection, press the first note ----------
COLD = [(0, 0x80, "L01 First Five Notes"), (1, 0x800, "L02 Ode to Joy"),
        (2, 0x80, "L03 Twinkle"), (3, 0x800, "L04 Lightly Row"),
        (4, 0x80, "L05 Aura Lee"), (5, 0x100, "drill"),
        (7, 0x80, "song (percussion part)")]
for sel, mask, name in COLD:
    ev = [(500, "shot"), (1000, f"keys {mask:#x}"), (1300, "keys 0"),
          (2200, "shot"), (3700, "shot")]
    run(write_script(f"v030_cold{sel}.txt", ev, 4200), f"v030c{sel}", seed=sel)
    check(f"cold boot sel={sel} ({name}): first-note press kicks off",
          rolls(f"v030c{sel}", 500, 2200, 3700))
    ev = [(500, "shot"), (2200, "shot"), (3700, "shot")]
    run(write_script(f"v030_cold{sel}_np.txt", ev, 4200), f"v030n{sel}", seed=sel)
    check(f"cold boot sel={sel} ({name}): parked view is stable without a press",
          still(f"v030n{sel}", [500, 2200, 3700]))
    check(f"cold boot sel={sel} ({name}): seeded park == no-press park",
          scroll_hash(shot(f"v030c{sel}", 500)) == scroll_hash(shot(f"v030n{sel}", 500)))

# a v0.24-era blob could hold at most sel 6; a stray byte is clamped by the load
run(write_script("v030_cold200.txt",
                 [(500, "shot"), (1000, "keys 0x80"), (1300, "keys 0"),
                  (2200, "shot"), (3700, "shot")], 4200), "v030c200", seed=200)
check("cold boot sel=200 (clamped to piece 0): first-note press kicks off",
      rolls("v030c200", 500, 2200, 3700))

# ---------- Free Play cold (sel 6): no kick-off exists there by design ----------
ev = [(500, "shot"), (1000, "keys 0x80"), (1300, "keys 0"), (2200, "shot"), (3700, "shot")]
run(write_script("v030_free_a.txt", ev, 4200), "v030fa", seed=6)
check("cold Free Play: a stopped press with REC off is a ghost (no roll)",
      still("v030fa", [500, 2200, 3700]))
ev = [(500, "shot"), (700, "btn 13"), (1200, "keys 0x80"), (1500, "keys 0"),
      (2200, "shot"), (3700, "shot")]
run(write_script("v030_free_b.txt", ev, 4200), "v030fb", seed=6)
check("cold Free Play: REC arm + press auto-starts the tape",
      rolls("v030fb", 500, 2200, 3700))

# ---------- controls: SELECT into the piece, then press (the old suites' path) ----------
ev = [(300, "enc 0 -1"), (600, "shot"), (1000, "keys 0x80"), (1300, "keys 0"),
      (2200, "shot"), (3700, "shot")]
run(write_script("v030_ctl_song.txt", ev, 4200), "v030ts")
check("control: SELECT into the song, first-note press kicks off",
      rolls("v030ts", 600, 2200, 3700))
check("control: seeded song park is pixel-identical to the SELECT park",
      scroll_hash(shot("v030ts", 600)) == scroll_hash(shot("v030c7", 500)))
ev = [(300, "enc 0 5"), (600, "shot"), (1000, "keys 0x100"), (1300, "keys 0"),
      (2200, "shot"), (3700, "shot")]
run(write_script("v030_ctl_drill.txt", ev, 4200), "v030td")
check("control: SELECT to the drill, first-note press kicks off",
      rolls("v030td", 600, 2200, 3700))

# ---------- the PLAY colour at a seeded park (task d) ----------
run(write_script("v030_led.txt", [(1400, "shot")], 1600), "v030led", seed=7, film=20)
duties = play_duties("v030led", 200, 1400)
check("seeded song park: PLAY green (stopped and ready: a press will kick off) (v0.27 colour; was LED solid on)",
      len(duties) > 10 and all(d == (255, 0) for d in duties), f"duties={duties[:8]}")

# ---------- park perturbations (piece 0 unless noted; mask 0x80 = its first note) ----------
# E1: a brief PLAY roll (300 ms, ~11 px) stopped just past the park.
ev = [(400, "shot"), (500, "btn 12"), (800, "btn 12"),
      (1500, "keys 0x80"), (1800, "keys 0"), (2000, "shot"), (2600, "shot"),
      (3200, "btn 8"), (3800, "keys 0x80"), (4100, "keys 0"),
      (4400, "shot"), (5600, "shot")]
run(write_script("v030_e1.txt", ev, 6000), "v030e1")
check("E1: the dead press still sounds (ghost) after a brief PLAY roll",
      wav_rms("v030e1", 1.5, 1.8) > 150, f"rms={wav_rms('v030e1', 1.5, 1.8):.0f}")
check("E1: press a few px past the park kicks off (tap-overshoot forgiven)",
      not still("v030e1", [2000, 2600]))
check("E1: after HOME rewinds, the press kicks off",
      rolls("v030e1", 2600, 4400, 5600))

# E2: one fine-scrub detent (+4 px) past the park; then HOME forces a
# known park and an out-and-back detent pair returns to it exactly.
ev = [(400, "shot"), (500, "enc 4 1"), (1100, "keys 0x80"), (1400, "keys 0"),
      (1700, "shot"), (2300, "shot"),
      (2600, "btn 8"), (2900, "enc 4 1"), (3100, "enc 4 -1"),
      (3400, "keys 0x80"), (3700, "keys 0"), (4000, "shot"), (5200, "shot")]
run(write_script("v030_e2.txt", ev, 5600), "v030e2")
check("E2: press one scrub detent past the park kicks off",
      not still("v030e2", [1700, 2300]))
check("E2: out-and-back detents return to the exact park; the press kicks off",
      not still("v030e2", [4000, 5200]))

# E6: coarse scrub back from the park lands on the sheet start (CURSOR_X),
# before the first note: a first-note press there can only mean "start".
ev = [(400, "shot"), (500, "enc 5 -1"), (1100, "keys 0x80"), (1400, "keys 0"),
      (1700, "shot"), (2900, "shot")]
run(write_script("v030_e6.txt", ev, 3300), "v030e6")
check("E6: press at the sheet start (before the first note) kicks off",
      not still("v030e6", [1700, 2900]))

# E3: octave shift -- the press is the wrong pitch: sounds, must never kick.
ev = [(400, "shot"), (400, "btn 1"), (1000, "keys 0x80"), (1300, "keys 0"),
      (1600, "shot"), (2200, "shot"),
      (2500, "btn 0"), (3100, "keys 0x80"), (3400, "keys 0"),
      (3700, "shot"), (4900, "shot")]
run(write_script("v030_e3.txt", ev, 5300), "v030e3")
check("E3: with OCT+ on, the same key sounds (ghost sounds an octave up)",
      wav_rms("v030e3", 1.0, 1.3) > 150, f"rms={wav_rms('v030e3', 1.0, 1.3):.0f}")
check("E3: with OCT+ on, the press does NOT kick off (wrong pitch)",
      still("v030e3", [1600, 2200]))
check("E3: with the octave back to 0, the press kicks off",
      not still("v030e3", [3700, 4900]))

# E4: very short taps at the exact park still kick off.
for dur in (1, 10, 50):
    ev = [(500, "shot"), (1000, "keys 0x80"), (1000 + dur, "keys 0"),
          (2200, "shot"), (3400, "shot")]
    run(write_script(f"v030_tap{dur}.txt", ev, 3800), f"v030t{dur}")
    check(f"E4: a {dur} ms tap at the park kicks off",
          rolls(f"v030t{dur}", 500, 2200, 3400))

# E5: the full ritual on the song: PLAY, HOME (to the book), PLAY (enter +
# roll), stop ~30 px past the park -- too far for tap forgiveness, so the
# press stays dead by design; HOME rewind is the revive.
ev = [(400, "shot"), (500, "btn 12"), (900, "btn 8"), (1500, "btn 12"),
      (2100, "btn 12"), (2400, "shot"), (2600, "keys 0x80"), (2900, "keys 0"),
      (3200, "shot"), (3600, "btn 8"), (4200, "keys 0x80"), (4500, "keys 0"),
      (4800, "shot"), (6000, "shot")]
run(write_script("v030_e5.txt", ev, 6400), "v030e5", seed=7)
check("E5: press ~30 px past the park does not kick off (a real roll, not a tap)",
      still("v030e5", [2400, 3200]))
check("E5: HOME rewind revives the kick-off",
      not still("v030e5", [4800, 6000]))

print()
if FAILS:
    print(f"v030: {len(FAILS)} check(s) failed")
    sys.exit(1)
print("v030: all checks passed")
