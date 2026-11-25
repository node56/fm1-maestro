#!/usr/bin/env python3
"""v031 -- the lights (v0.27): the note to be played is lit bright,
everything else is dim; PLAY is a colour (green stopped, red moving).

The film log's rows carry, past the encoder fields (indices 11-14,
appended by tests/host.c for this suite): PLAY's green channel
duty, PLAY's red channel duty (the play driver is
fm1_led_play_set(brightness, balance): the v0.27 call sites use
full brightness, balance hard green stopped / hard red moving, so
the duties read 255/0 or 0/255), the key glow layer and the button
glow layer. The bright key field (index 2) and the button field
(index 3) are the layers the older suites already read.

Semantics asserted here:
  parked on a guided sheet   the kick-off group is bright (all its
                             pitches; they commit only when the
                             group completes), every other key glows;
                             rolling, the target is the next group,
                             its already-matched pitches dark
  rolling (record/playalong) the next guide group at/after the
                             playhead; PLAY red in EVERY row (steady,
                             not the v0.26 beat flash)
  a chord group              all its pitches bright together
  the song's percussion part the drum-pad key is the target
  Free Play                  no guide, no target: no key bright
  the take's end (v0.31)     no replay any more: the take ends
                             straight into the next piece, parked,
                             and the target is the NEW piece's
                             first note -- never the old take's
  HOME at the park (v0.31)    no book view any more: HOME at the
                             park leaves the parked view, PLAY
                             green, the kick-off group still lit
  octave shift               the target pitch lights the key that
                             sounds it under the current shift

The lesson sections run on First Five Notes (72 BPM, 38.4 px/s):
13 notes at piece-x 80,112,...,464 (C5 D5 E5 F5 G5 F5 E5 D5 C5
D5 E5 D5 C5), end bar 576. Key of MIDI m (no shift) is m-53:
C5=7, D5=9, E5=11, F5=12, G5=14. In the v0.30 book order that
lesson is slot 1 (the song is slot 0), so the suite seeds the
persisted selection at 1 for its unseeded runs; the song and
Free Play sections seed their own slots (0 and 7).

Usage: python3 tests/v031_verify.py   (from the build tree root)
"""
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.environ["DINGHY_SEED_SEL"] = "1"   # First Five Notes (see docstring)
HOST = os.path.join(ROOT, "build/host/dinghy")
OUT = os.path.join(ROOT, "build/host")
SCRIPTS = os.path.join(ROOT, "tests/scripts")
failures = []


def check(name, ok, detail=""):
    print(("  ok   " if ok else "  FAIL ") + name + ((" -- " + str(detail)) if (detail and not ok) else ""))
    if not ok:
        failures.append(name)


def build():
    r = subprocess.run(
        ["cc", "-O2", "-Wno-unused-function", "-DDINGHY_HOST", '-DAPP_VER="dev"',
         "-Ifirmware/hal", "-Ifirmware/src", "-Ifirmware/gen",
         "-o", HOST, "firmware/src/dinghy.c"],
        cwd=ROOT, capture_output=True, text=True)
    check("host build", r.returncode == 0, r.stderr[-400:])


def run(name, text, env_extra=None):
    """Run one script at 60 fps; return rows of
    (ms, keybright, buttons, green_duty, red_duty, keyglow, btnglow)."""
    sp = os.path.join(SCRIPTS, name + ".txt")
    with open(sp, "w") as f:
        f.write(text)
    env = dict(os.environ)
    if env_extra:
        env.update(env_extra)
    r = subprocess.run([HOST, HOST, sp, "60"], cwd=ROOT, env=env,
                       capture_output=True, text=True)
    if r.returncode != 0:
        check(name + ": run", False, r.stderr[-300:])
        return []
    rows = []
    with open(os.path.join(OUT, "dinghy-log.txt")) as f:
        for line in f:
            p = line.split()
            if len(p) >= 15:
                rows.append((int(p[0]), int(p[2]), int(p[3]), int(p[11]),
                             int(p[12]), int(p[13]), int(p[14])))
    return rows


def at(rows, ms):
    v = None
    for r in rows:
        if r[0] <= ms:
            v = r
        else:
            break
    return v


def exists(rows, lo, hi, pred):
    return any(lo <= r[0] <= hi and pred(r) for r in rows)


def all_rows(rows, lo, hi, pred):
    sel = [r for r in rows if lo <= r[0] <= hi]
    return bool(sel) and all(pred(r) for r in sel)


ALLKEYS = (1 << 27) - 1
KC, KD, KE, KF, KG = 1 << 7, 1 << 9, 1 << 11, 1 << 12, 1 << 14

build()

# ---------------------------------------------------------------- A: parked on L01
rows = run("v031_park", "2000 end\n")
r = at(rows, 1000)
check("A park: the first note (C5, key 7) is the one bright key",
      r is not None and r[1] == KC, r)
check("A park: every other key sits at the dim glow",
      r is not None and r[5] == (ALLKEYS ^ KC), r)
check("A park: PLAY green (duty 255), red dark, PLAY not in the button field or its glow",
      r is not None and r[3] == 255 and r[4] == 0 and not (r[2] >> 12) & 1 and not (r[6] >> 12) & 1, r)

# ---------------------------------------------------------------- B: kick-off, target advances by match and by passing
rows = run("v031_kick", "300 keys 0x80\n900 keys 0\n9000 end\n")
check("B kick: PLAY red in every rolling row (steady, no beat flash), green dark",
      all_rows(rows, 400, 8500, lambda r: r[4] == 255 and r[3] == 0))
check("B kick: once C is matched the target is D (key 9)",
      exists(rows, 400, 1200, lambda r: r[1] == KD))
check("B roll: unplayed D passes out of the window and the target becomes E (key 11)",
      exists(rows, 1700, 2300, lambda r: r[1] == KE))
check("B roll: then F (key 12)",
      exists(rows, 2500, 3100, lambda r: r[1] == KF))

# ---------------------------------------------------------------- C: play-along (no REC): advance by position alone
rows = run("v031_along", "300 btn 12\n6000 end\n")
first = {}
for r in rows:
    for name, bit in (("C", KC), ("D", KD), ("E", KE)):
        if r[1] == bit and name not in first:
            first[name] = r[0]
check("C along: PLAY red in every rolling row",
      all_rows(rows, 400, 5500, lambda r: r[4] == 255 and r[3] == 0))
check("C along: the target walks C -> D -> E as the groups pass",
      first.get("C", 1 << 30) < first.get("D", 1 << 30) < first.get("E", 1 << 30), first)

# ---------------------------------------------------------------- D: chord group (custom build, v027's technique)
TMP = "/tmp/v031_chord"
shutil.rmtree(TMP, ignore_errors=True)
shutil.copytree(os.path.join(ROOT, "firmware"), TMP + "/firmware")
shutil.copytree(os.path.join(ROOT, "tests"), TMP + "/tests")
open(TMP + "/firmware/src/lessons_data.c", "w").write(
    "/* Hand-written for the v031 chord target test: the first note\n"
    " * is a two-pitch chord (a zero-tick C5 under an E5). */\n"
    "#include \"lesson.h\"\n\n"
    "static const lesson_t lesson_chord_test = {\n"
    "    \"Chord Test\", \"v031 host test\", KIND_MELODY,\n"
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
    "const piece_t ode_piece = { \"Ode to Joy\", 96, 4, 1, { { SCORE_PATCH_DRUM, 0, 0, \"Percussion\", 0, 0 } } };\n")
r = subprocess.run(["cc", "-O2", "-Wall", "-Wno-unused-function", "-DDINGHY_HOST",
                    '-DAPP_VER="dev"', "-Ifirmware/hal", "-Ifirmware/src",
                    "-Ifirmware/gen", "-o", TMP + "/dinghy", "firmware/src/dinghy.c"],
                   cwd=TMP, capture_output=True, text=True)
check("D chord: host build with the chord lesson", r.returncode == 0, r.stderr.strip()[:200])
if r.returncode == 0:
    open(TMP + "/script.txt", "w").write(
        "100 btn 13\n"
        "1000 keys 0x80\n"     # C5 alone: half the chord -- no kick-off
        "1500 keys 0\n"
        "2000 keys 0x800\n"    # E5 completes the chord -- kick-off
        "2600 keys 0\n"
        "5000 end\n")
    subprocess.run([TMP + "/dinghy", TMP + "/out", TMP + "/script.txt", "60"],
                   cwd=TMP, capture_output=True, text=True, check=True)
    crows = []
    for line in open(TMP + "/out-log.txt"):
        p = line.split()
        if len(p) >= 15:
            crows.append((int(p[0]), int(p[2]), int(p[3]), int(p[11]),
                          int(p[12]), int(p[13]), int(p[14])))
    r = at(crows, 600)
    check("D chord: parked, both chord pitches bright together (keys 7+11), the rest glow",
          r is not None and r[1] == (KC | KE) and r[5] == (ALLKEYS ^ (KC | KE)), r)
    r = at(crows, 1300)
    check("D chord: C alone does not kick (still stopped, PLAY green) and the group stays lit whole -- kick-off pitches commit only when the group completes",
          r is not None and r[1] == (KC | KE) and r[3] == 255 and r[4] == 0, r)
    check("D chord: after the kick the target is the next group (G5, key 14), PLAY red",
          exists(crows, 2100, 3400, lambda r: r[1] == KG and r[4] == 255))

# ---------------------------------------------------------------- E: the song's percussion part
rows = run("v031_song", "300 keys 0x80\n900 keys 0\n5000 end\n",
           {"DINGHY_SEED_SEL": "0"})
r = at(rows, 200)
check("E song: parked on the percussion part, the drum-pad key (7) is the target",
      r is not None and r[1] == KC and r[3] == 255 and r[4] == 0, r)
check("E song: kicked and rolling, PLAY red and the target stays the drum pad (every pulse is that key)",
      all_rows(rows, 400, 2000, lambda r: r[4] == 255 and r[3] == 0)
      and exists(rows, 400, 2000, lambda r: r[1] == KC))

# ---------------------------------------------------------------- F: Free Play -- no guide, no target
rows = run("v031_free", "300 btn 12\n3000 end\n", {"DINGHY_SEED_SEL": "7"})
r = at(rows, 200)
check("F free: parked, no key bright, all keys glow, PLAY green",
      r is not None and r[1] == 0 and r[5] == ALLKEYS and r[3] == 255 and r[4] == 0, r)
check("F free: rolling, PLAY red and still no target",
      all_rows(rows, 400, 2500, lambda r: r[4] == 255 and r[3] == 0 and r[1] == 0))

# ---------------------------------------------------------------- G: the take ends into the load (v0.31)
# (a GOOD take -- every blue note pressed as the playhead passes
# -- rolls the piece's full ~13.4 s, then the NEXT piece loads at
# once, parked -- no replay in between. The target after the load
# is the new piece's first note (E5, key 11), never the old take's
# C.)
_notes = [(80, 0x80), (112, 0x200), (144, 0x800), (176, 0x1000),
          (208, 0x4000), (240, 0x1000), (272, 0x800), (304, 0x200),
          (336, 0x80), (368, 0x200), (400, 0x800), (432, 0x200),
          (464, 0x80)]
_g = []
for _x, _m in _notes:
    _t = 300 if _x == 80 else 300 + round((_x - 80) * 1000 / 38.4) - 120
    _g.append(f"{_t} keys 0x{_m:x}")
    _g.append(f"{_t + 200} keys 0")
_g.append("40000 end")
rows = run("v031_replay", "\n".join(_g) + "\n")
reloads = [r[0] for r in rows if r[0] > 14000 and r[3] == 255 and r[4] == 0]
check("G load: the take ends and the next piece loads, parked (PLAY green again)",
      bool(reloads), reloads[:1])
check("G load: the target is the new piece's first note (E5, key 11), PLAY green",
      exists(rows, 14000, 20000, lambda r: r[1] == KE and r[3] == 255 and r[4] == 0),
      [r for r in rows if 14000 <= r[0] <= 20000][:1])
check("G load: the old take's C (key 7) is never the target after the load",
      not any(r[0] > 14000 and r[1] == KC for r in rows))

# ---------------------------------------------------------------- H: HOME at the park (v0.31)
# There is no book view any more: HOME at the park leaves the
# parked view -- PLAY green, the kick-off group (C5, key 7) still
# the target, every other key at the dim glow.
rows = run("v031_book", "300 btn 8\n2000 end\n")
r = at(rows, 1000)
check("H home: HOME at the park leaves the parked view (PLAY green, C5 still the target, the rest glow)",
      r is not None and r[3] == 255 and r[4] == 0 and r[1] == KC and r[5] == (ALLKEYS ^ KC), r)

# ---------------------------------------------------------------- I: the target follows the octave shift
rows = run("v031_oct", "300 btn 0\n1500 end\n")   # OCT_DOWN: shift -1
r = at(rows, 1000)
check("I oct: shifted down, C5 lights key 19 (the key that sounds it now)",
      r is not None and r[1] == (1 << 19) and r[3] == 255 and r[4] == 0, r)

print()
if failures:
    print(f"v031: {len(failures)} FAILURE(S): {failures}")
    sys.exit(1)
print("v031: all checks passed")
