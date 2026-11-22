#!/usr/bin/env python3
"""v032 -- the cold-path matrix (refactor round 1): the standing gate
for "power on, press the first note".

Every book entry is booted cold from a PERSISTED selection (the
DINGHY_SEED_SEL hook in tests/host.c pre-seeds the settings blob a
previous run would have saved) and performs its designed first
gesture:

  sel 0..5, 7   guided entries (lessons, drill, the song): parked at
                the first note, PLAY green -- the light's promise
                that a press will start a take -- and the first-note
                press kicks off (the tape rolls).
  sel 6         Free Play has no kick-off by design: a stopped press
                with REC off is a ghost (nothing rolls); with REC
                armed the same press auto-starts the tape.
  sel 200       an out-of-range persisted byte is clamped at load
                (to piece 0) and kicks off like piece 0.

Why a separate suite when v030 grew cold-boot checks as the Nov 22
kick-off bug's repro: v030's cold section is entangled with that
bug's perturbation narrative (and stays untouched). This matrix is
the uniform, entry-by-entry statement of the designed behaviour,
and it adds one assertion v030 makes only for the song: PLAY is
green at EVERY guided entry's seeded park. It is part of the
standard regression (tests/run_all.py).

Detection follows v030: the scroll region (rows 48..203) hashed per
screenshot -- a rolling tape changes it, a parked one never does;
PLAY's colour duties are film-log fields 11/12.

Usage: python3 tests/v032_verify.py   (from the build tree root --
tests/run_all.py syncs this repo's tests there and runs it)
"""
import hashlib
import os
import subprocess
import sys

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
    h0 = scroll_hash(shot(out, parked_ms))
    h2 = scroll_hash(shot(out, ms2))
    h3 = scroll_hash(shot(out, ms3))
    return h0 != h2 and h2 != h3


def still(out, ms_list):
    hs = [scroll_hash(shot(out, ms)) for ms in ms_list]
    return all(h == hs[0] for h in hs)


def play_duties(out, t0, t1):
    """(green, red) duty samples of PLAY's colour channels within
    [t0, t1] ms (film-log fields 11/12)."""
    vals = []
    for line in open(os.path.join(HOST, f"{out}-log.txt")):
        p = line.split()
        if len(p) >= 15 and p[0].isdigit() and t0 <= int(p[0]) <= t1:
            vals.append((int(p[11]), int(p[12])))
    return vals


PRESS = [(500, "shot"), (1000, "keys {mask:#x}"), (1300, "keys 0"),
         (2200, "shot"), (3700, "shot")]
NOPRESS = [(500, "shot"), (2200, "shot"), (3700, "shot")]

# (sel, first-note key mask, name) -- the guided entries
GUIDED = [(0, 0x80, "L01 First Five Notes"), (1, 0x800, "L02 Ode to Joy"),
          (2, 0x80, "L03 Twinkle"), (3, 0x800, "L04 Lightly Row"),
          (4, 0x80, "L05 Aura Lee"), (5, 0x100, "drill"),
          (7, 0x80, "song (percussion part)")]

build()

for sel, mask, name in GUIDED:
    ev = [(t, txt.format(mask=mask) if "{mask" in txt else txt) for t, txt in PRESS]
    run(write_script(f"v032_cold{sel}.txt", ev, 4200), f"v032c{sel}", seed=sel)
    run(write_script(f"v032_cold{sel}_np.txt", NOPRESS, 4200), f"v032n{sel}", seed=sel)
    check(f"sel={sel} ({name}): parked view is stable without a press",
          still(f"v032n{sel}", [500, 2200, 3700]))
    check(f"sel={sel} ({name}): seeded park == no-press park",
          scroll_hash(shot(f"v032c{sel}", 500)) == scroll_hash(shot(f"v032n{sel}", 500)))
    check(f"sel={sel} ({name}): first-note press kicks off",
          rolls(f"v032c{sel}", 500, 2200, 3700))
    run(write_script(f"v032_led{sel}.txt", [(1400, "shot")], 1600),
        f"v032led{sel}", seed=sel, film=20)
    duties = play_duties(f"v032led{sel}", 200, 1400)
    check(f"sel={sel} ({name}): PLAY green at the seeded park",
          len(duties) > 10 and all(d == (255, 0) for d in duties),
          f"duties={duties[:8]}")

# Free Play (sel 6): no kick-off by design -- a ghost with REC off,
# an auto-start with REC armed.
ev = [(500, "shot"), (1000, "keys 0x80"), (1300, "keys 0"), (2200, "shot"), (3700, "shot")]
run(write_script("v032_free_a.txt", ev, 4200), "v032fa", seed=6)
check("sel=6 (Free Play): a stopped press with REC off is a ghost (no roll)",
      still("v032fa", [500, 2200, 3700]))
ev = [(500, "shot"), (700, "btn 13"), (1200, "keys 0x80"), (1500, "keys 0"),
      (2200, "shot"), (3700, "shot")]
run(write_script("v032_free_b.txt", ev, 4200), "v032fb", seed=6)
check("sel=6 (Free Play): REC arm + press auto-starts the tape",
      rolls("v032fb", 500, 2200, 3700))

# Out-of-range persisted selection: clamped at load to piece 0.
ev = [(t, txt.format(mask=0x80) if "{mask" in txt else txt) for t, txt in PRESS]
run(write_script("v032_cold200.txt", ev, 4200), "v032c200", seed=200)
check("sel=200 (clamped to piece 0): first-note press kicks off",
      rolls("v032c200", 500, 2200, 3700))

print()
if FAILS:
    print(f"v032: {len(FAILS)} check(s) failed")
    sys.exit(1)
print("v032: all checks passed")
