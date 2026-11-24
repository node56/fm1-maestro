#!/usr/bin/env python3
"""v033 -- accidentals (v0.29): a black-key pitch wears a sharp sign
before its notehead.

ui_dia() puts a sharp on its natural's diatonic step, and until
v0.29 ui_accidental() was never called, so a C# drew exactly as a
C. The generated drill is a semitone walk (root 60, range 7), so
its first note is a black key about a third of the time -- the
boot drill (seed 7) opens on C# itself. A learner reads the sheet
as drawn, presses the natural, and the kick-off silently refuses.

The rule under test is deliberately simple: pitch classes
1/3/6/8/10 wear the sharp glyph, white keys wear nothing; no key
signatures, no naturals. ui_note() draws the sign, so it lands in
whichever plane the note itself is in. The glyph bitmap is read
from firmware/src/glyphs.h and matched against the frame masks:
every glyph pixel must be present in the note's plane (staff-line
and ledger intrusions only ever ADD pixels, so presence is the
discriminating direction), with a bounded excess where the box is
known to be clean.

  A  the drill, seeded boot (sel 5): the parked C# wears the
     sharp in blue; the white-key D beside it wears nothing; the
     film log's bright key at the park is the C# key (8) -- the
     sheet and the light say the same thing; pressing C# kicks
     off (the scroll moves).
  B  a fixture lesson with sharps, compiled by lessons2c at test
     time (quarters, then a sixteenth-note chromatic run): the
     signs are present at every density, white notes unsigned.
  C  Free Play: a take note recorded on the black key wears its
     sharp in black (ink); the same take played on C natural
     does not -- the glyph pattern is absent there.
  D  the drill's end-of-take replay: play only the first note,
     and the missed C# (eighth) and D# draw their sharps in amber.

Usage: python3 tests/v033_verify.py   (from the build tree root --
tests/run_all.py syncs this repo's tests there and runs it)
"""
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
HOST = os.path.join(ROOT, "build", "host")
BIN = os.path.join(HOST, "dinghy")
SCR = os.path.join(HERE, "scripts")
FAILS = []

# the sharp glyph, parsed from the firmware's own glyph table
_src = open(os.path.join(ROOT, "firmware", "src", "glyphs.h")).read()
SHARP_W = int(re.search(r"#define SHARP_W (\d+)", _src).group(1))
SHARP_H = int(re.search(r"#define SHARP_H (\d+)", _src).group(1))
SHARP_BPR = int(re.search(r"#define SHARP_BPR (\d+)", _src).group(1))
_body = re.search(r"glyph_sharp\[\] = \{(.*?)\};", _src, re.S).group(1)
_vals = [int(v, 16) for v in re.findall(r"0x[0-9A-Fa-f]+", _body)]
GLYPH = np.zeros((SHARP_H, SHARP_W), dtype=bool)
for _r in range(SHARP_H):
    for _c in range(SHARP_W):
        GLYPH[_r, _c] = bool(
            _vals[_r * SHARP_BPR + (_c >> 3)] & (0x80 >> (_c & 7)))
ACC_DX = 23   # ui_note: the glyph's left edge sits at note_x - 23


def check(name, cond, extra=""):
    print(("ok   " if cond else "FAIL "), name, " ", extra if not cond else "")
    if not cond:
        FAILS.append(name)


def build():
    r = subprocess.run(
        ["cc", "-O2", "-Wno-unused-function", "-DDINGHY_HOST",
         '-DAPP_VER="dev"', "-Ifirmware/hal", "-Ifirmware/src",
         "-Ifirmware/gen", "-o", BIN, "firmware/src/dinghy.c"],
        cwd=ROOT, capture_output=True, text=True)
    check("host build", r.returncode == 0, r.stderr[-400:])


def run(script, name, seed=None, film=None, binpath=None, cwd=None):
    sp = os.path.join(SCR, name + ".txt")
    with open(sp, "w") as f:
        f.write(script)
    out = os.path.join(HOST, name)
    args = [binpath or BIN, out, sp]
    if film:
        args.append(str(film))
    env = dict(os.environ)
    if seed is not None:
        env["DINGHY_SEED_SEL"] = str(seed)
    r = subprocess.run(args, cwd=cwd or ROOT, capture_output=True,
                       text=True, env=env)
    if r.returncode != 0:
        check(f"run {name}", False, r.stderr[-300:])
    return out


def masks(path):
    im = np.array(Image.open(path).convert("RGB")).astype(np.int16)
    blue = (im[:, :, 2] - im[:, :, 0] > 30)
    amber = ((im[:, :, 0] > 200) & (im[:, :, 1] > 80)
             & (im[:, :, 1] < 210) & (im[:, :, 2] < 100))
    ink = (im[:, :, 0] < 60) & (im[:, :, 1] < 60) & (im[:, :, 2] < 60)
    return blue, amber, ink


def glyph_at(mask, nx, y):
    """(missing, excess) of the sharp glyph drawn for a notehead at
    (nx, y): pattern pixels absent from the mask, and mask pixels
    inside the glyph box that are not the pattern. Pattern rows
    landing outside the scroll band [48, 204) are clipped by the
    firmware and excluded here too."""
    ax, ay = nx - ACC_DX, y - SHARP_H // 2
    missing = excess = 0
    for r in range(SHARP_H):
        sy = ay + r
        if sy < 48 or sy >= 204:
            continue
        for c in range(SHARP_W):
            sx = ax + c
            if sx < 0 or sx >= 240:
                continue
            if GLYPH[r, c]:
                if not mask[sy, sx]:
                    missing += 1
            elif mask[sy, sx]:
                excess += 1
    return missing, excess


def find_glyph(mask, y, xlo, xhi):
    """best (missing, nx) sliding the notehead x over [xlo, xhi]."""
    best = (1 << 30, None)
    for nx in range(xlo, xhi + 1):
        m, _ = glyph_at(mask, nx, y)
        if m < best[0]:
            best = (m, nx)
    return best


def scroll_hash(path):
    im = np.array(Image.open(path).convert("RGB"))
    return hashlib.sha256(im[48:204].tobytes()).hexdigest()


build()

# ---------------------------------------------------------------- A: the drill
# seeded boot at the drill; the boot drill is seed 7: C# D C# D D D# ...
out = run("100 T\n500 shot\n1500 end\n", "v033_drill_park", seed=5)
blue, _, _ = masks(out + "-500.ppm")
m, e = glyph_at(blue, 60, 186)
check("A drill park: the C# first note wears the sharp (blue)",
      m == 0 and e <= 6, f"missing {m} excess {e}")
box = blue[179 - 19:179 + 20, 92 - 23:92 - 9].copy()
box[186 - (179 - 19), :] = False   # the C#'s ledger line crosses at its own row
check("A drill park: the white-key D wears no accidental",
      int(box.sum()) == 0, f"{int(box.sum())} blue px in the D's box")

out = run("100 T\n500 shot\n1000 keys 0x100\n1300 keys 0\n"
          "2500 shot\n3200 end\n", "v033_drill_kick", seed=5)
check("A drill: pressing the C# the sheet shows kicks off",
      scroll_hash(out + "-500.ppm") != scroll_hash(out + "-2500.ppm"))

out = run("1500 end\n", "v033_drill_film", seed=5, film=60)
row = None
for line in open(out + "-log.txt"):
    p = line.split()
    if len(p) >= 15 and int(p[0]) <= 1000:
        row = p
check("A drill park (film): the bright key is the C# key (8)",
      row is not None and int(row[2]) == (1 << 8)
      and int(row[13]) == ((1 << 27) - 1) ^ (1 << 8),
      str(row))
check("A drill park (film): PLAY green while parked",
      row is not None and int(row[11]) == 255 and int(row[12]) == 0,
      str(row))

# ---------------------------------------------------------------- B: a sharps lesson
LESSON = """title:    Sharps
kind:     melody
source:   v033 fixture -- original exercise
level:    3
hand:     right
position: C5
tempo:    96
time:     4/4
notes:
C#5 q | D5 q | F#5 q | G5 q |
D5 e | D#5 e | E5 e | F5 e | F#5 e | G5 e | E5 e | D5 e |
C5 s | C#5 s | D5 s | D#5 s | E5 s | F5 s | F#5 s | G5 s | A5 s | G5 s | F#5 s | F5 s | E5 s | D#5 s | D5 s | C#5 s |
C5 h | R h |
"""
REPO = os.path.join(os.path.dirname(ROOT), "fm1-maestro")
TOOL = os.path.join(REPO, "tools", "lessons2c.py")
TMP = tempfile.mkdtemp(prefix="v033_")
book = os.path.join(TMP, "book")
os.makedirs(book)
with open(os.path.join(book, "sharps.lesson"), "w") as f:
    f.write(LESSON)
gen = os.path.join(TMP, "book.c")
r = subprocess.run([sys.executable, TOOL, book, "-o", gen],
                   capture_output=True, text=True)
check("B fixture: lessons2c compiles the sharps lesson",
      r.returncode == 0, r.stderr[-300:])
if r.returncode == 0:
    shutil.copytree(os.path.join(ROOT, "firmware"),
                    os.path.join(TMP, "firmware"))
    shutil.copytree(os.path.join(ROOT, "tests"),
                    os.path.join(TMP, "tests"))
    shutil.copy(gen, os.path.join(TMP, "firmware", "src",
                                  "lessons_data.c"))
    fbin = os.path.join(TMP, "dinghy")
    r = subprocess.run(
        ["cc", "-O2", "-Wno-unused-function", "-DDINGHY_HOST",
         '-DAPP_VER="dev"', "-Ifirmware/hal", "-Ifirmware/src",
         "-Ifirmware/gen", "-o", fbin, "firmware/src/dinghy.c"],
        cwd=TMP, capture_output=True, text=True)
    check("B fixture: host build with the sharps book",
          r.returncode == 0, r.stderr[-300:])
    if r.returncode == 0:
        def frun(script, name):
            sp = os.path.join(TMP, name + ".txt")
            with open(sp, "w") as f:
                f.write(script)
            o = os.path.join(TMP, name)
            rr = subprocess.run([fbin, o, sp], cwd=TMP,
                                capture_output=True, text=True)
            if rr.returncode != 0:
                check(f"B run {name}", False, rr.stderr[-300:])
            return o

        o = frun("100 T\n500 shot\n1200 end\n", "fixpark")
        blue, _, _ = masks(o + "-500.ppm")
        m, e = glyph_at(blue, 60, 186)
        check("B quarters: C# wears the sharp (blue)",
              m == 0 and e <= 6, f"missing {m} excess {e}")
        m, e = glyph_at(blue, 124, 165)
        check("B quarters: F# wears the sharp (blue)",
              m == 0 and e <= 4, f"missing {m} excess {e}")
        box = blue[158 - 19:158 + 20, 156 - 23:156 - 9]
        check("B quarters: the white-key G wears no accidental",
              int(box.sum()) == 0, f"{int(box.sum())} blue px")

        o = frun("100 T\n400 enc 5 7\n900 shot\n1600 end\n", "fix16")
        blue, _, _ = masks(o + "-900.ppm")
        m, _ = glyph_at(blue, 100, 186)
        check("B sixteenths: the C# of the chromatic run is signed",
              m == 0, f"missing {m}")
        m, _ = glyph_at(blue, 140, 165)
        check("B sixteenths: the F# of the chromatic run is signed",
              m == 0, f"missing {m}")

# ---------------------------------------------------------------- C: a take in black
def free_take(keys, name):
    return run("100 T\n500 btn 12\n2500 btn 13\n"
               f"3000 keys {keys}\n3400 keys 0\n"
               "4000 btn 12\n4500 btn 8\n5200 shot\n6000 end\n",
               name, seed=6)

out = free_take("0x100", "v033_take_sharp")
_, _, ink = masks(out + "-5200.ppm")
m, nx = find_glyph(ink, 186, 146, 166)
_, e = glyph_at(ink, nx, 186) if nx is not None else (0, 1 << 30)
check("C free play: the recorded C# wears its sharp in black",
      m == 0 and e <= 24, f"missing {m} excess {e} at nx {nx}")

out = free_take("0x80", "v033_take_nat")
_, _, ink = masks(out + "-5200.ppm")
m, nx = find_glyph(ink, 186, 146, 166)
check("C free play: the recorded C natural wears no sharp",
      m >= 100, f"best missing {m} at nx {nx}")

# ---------------------------------------------------------------- D: misses in amber
out = run("100 T\n1000 keys 0x100\n1300 keys 0\n"
          "12500 shot\n14000 end\n", "v033_replay", seed=5)
_, amber, _ = masks(out + "-12500.ppm")
m, nx = find_glyph(amber, 186, 60, 110)
check("D replay: the missed C# (eighth) draws its sharp in amber",
      m == 0, f"missing {m} at nx {nx}")
m, nx = find_glyph(amber, 179, 110, 200)
check("D replay: a missed D# draws its sharp in amber",
      m == 0, f"missing {m} at nx {nx}")

print()
if FAILS:
    print(f"v033: {len(FAILS)} check(s) FAILED: {', '.join(FAILS)}")
    sys.exit(1)
print("v033: all checks passed")
