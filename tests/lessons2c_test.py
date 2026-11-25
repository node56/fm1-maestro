#!/usr/bin/env python3
"""Verify the lesson/song compiler, tools/lessons2c.py.

  valid     a fixture book with a .song compiles: lessons, the
            generated parts (percussion pulse, bass roots following
            the chart, the reserved harmony, the melody lesson) and
            the song_t all land in the generated C.
  no_song   a book with no .song file compiles cleanly -- no stub,
            no warning -- and gets the empty ode_song.
  host      a host build of that song-less book boots, SELECT walks
            every entry including the empty song slot, the parked
            song screen is stable and distinct, a key press sounds,
            PLAY/KNOB2/HOME on the empty song neither crash nor
            hang, and a cold boot seeded straight at the song slot
            lands on the same parked screen.
  errors    one fixture per validation failure: the compile fails,
            and the message names the file and the problem.
  warning   a playable note outside the staff band compiles with a
            warning, not an error.

Fixtures live in tests/fixtures/lessons2c/. Run by tests/run_all.py
(the repo copy is what runs; the host part uses the Dinghy build
tree, like the other host suites).

Usage: python3 tests/lessons2c_test.py
"""
import hashlib
import os
import shutil
import subprocess
import sys
import wave

import numpy as np
from PIL import Image

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOL = os.path.join(REPO, "tools/lessons2c.py")
FIX = os.path.join(REPO, "tests/fixtures/lessons2c")
BT = os.environ.get("MAESTRO_BUILD_TREE",
                    os.path.join(os.path.dirname(REPO), "maestro-build"))
TMP = "/tmp/lessons2c_test"
failures = []


def check(name, ok, detail=""):
    print(f"{'ok  ' if ok else 'FAIL'}  {name}  {detail if not ok else ''}")
    if not ok:
        failures.append(name)


def run_tool(book, out):
    return subprocess.run([sys.executable, TOOL, book, "-o", out],
                          capture_output=True, text=True)


def compile_ok(case):
    out = os.path.join(TMP, case + ".c")
    r = run_tool(os.path.join(FIX, case), out)
    gen = open(out).read() if r.returncode == 0 else ""
    return r, gen


# ---------------------------------------------------------------- valid book
os.makedirs(TMP, exist_ok=True)
r, gen = compile_ok("book_ok")
check("valid: book with a song compiles", r.returncode == 0, r.stderr.strip())
for frag, name in [
        ("&lesson_melody,", "melody lesson in the book"),
        ("&lesson_second,", "second lesson in the book"),
        ("all_lessons_count = 2", "two lessons"),
        ('"Fixture Song",\n    90, 4, 4,', "song header (tempo from the melody)"),
        ("song_fixture_song_percussion_events[8]", "percussion array (2 bars x 4 beats)"),
        ('{ SCORE_PATCH_DRUM, 0, 38, "Percussion", 8, song_fixture_song_percussion_events }', "percussion part"),
        ('{ SCORE_PATCH_BASS, -12, 0, "Bass", 8, song_fixture_song_bass_events }', "bass part"),
        ("{ 65, 96, 0 }", "bass follows the chart (F root in bar 2)"),
        ("song_fixture_song_harmony_events[8]", "harmony array (2 bars x 4 beats)"),
        ('{ SCORE_PATCH_PIANO1, 0, 0, "Harmony", 8, song_fixture_song_harmony_events }', "harmony part"),
        ("{ 69, 96, 0 }", "harmony follows the chart (F bar: 3rd of root-3rd-5th-3rd)"),
        ('{ SCORE_PATCH_PRESET, 0, 0, "Melody", 7, song_fixture_song_melody_events }', "melody part")]:
    check(f"valid: generated C has {name}", frag in gen)

# ---------------------------------------------------------------- no song
r, gen = compile_ok("book_nosong")
check("no_song: book without a .song compiles", r.returncode == 0,
      r.stderr.strip())
check("no_song: the compiler says so, without a warning",
      "no song" in r.stdout and "warning" not in r.stderr.lower(),
      f"stdout {r.stdout.strip()!r} stderr {r.stderr.strip()!r}")
for frag, name in [('"Song",\n    96, 4, 1,', "empty song header"),
                   ('{ SCORE_PATCH_DRUM, 0, 0, "Percussion", 0, 0 }', "one reserved part")]:
    check(f"no_song: generated C has {name}", frag in gen)
check("no_song: no part arrays are emitted",
      "static const lesson_event_t song_" not in gen)

# ---------------------------------------------------------------- host check
def scroll_hash(path):
    im = np.array(Image.open(path).convert("RGB"))
    return hashlib.sha256(im[48:204].tobytes()).hexdigest()


def wav_rms(path, t0, t1):
    with wave.open(path, "rb") as w:
        data = np.frombuffer(w.readframes(w.getnframes()),
                             dtype=np.int16)[0::2].astype(np.float64)
    seg = data[int(t0 * 44100):int(t1 * 44100)]
    return float(np.sqrt((seg ** 2).mean())) if len(seg) else 0.0


HOST_TMP = os.path.join(TMP, "host")
if not os.path.isfile(os.path.join(BT, "firmware/src/dinghy.c")):
    check("host: build tree present", False,
          f"{BT} does not look like the Dinghy build tree")
else:
    shutil.rmtree(HOST_TMP, ignore_errors=True)
    shutil.copytree(os.path.join(BT, "firmware"), HOST_TMP + "/firmware")
    shutil.copytree(os.path.join(BT, "tests"), HOST_TMP + "/tests")
    shutil.copy(os.path.join(TMP, "book_nosong.c"),
                HOST_TMP + "/firmware/src/lessons_data.c")
    r = subprocess.run(
        ["cc", "-O2", "-Wall", "-Wno-unused-function", "-DDINGHY_HOST",
         '-DAPP_VER="dev"', "-Ifirmware/hal", "-Ifirmware/src",
         "-Ifirmware/gen", "-o", HOST_TMP + "/dinghy",
         "firmware/src/dinghy.c"],
        cwd=HOST_TMP, capture_output=True, text=True)
    check("host: build with the song-less book", r.returncode == 0,
          r.stderr.strip()[:200])
    if r.returncode == 0:
        def host_run(name, script, seed=None):
            open(HOST_TMP + f"/{name}.txt", "w").write(script)
            env = dict(os.environ)
            if seed is not None:
                env["DINGHY_SEED_SEL"] = str(seed)
            try:
                rr = subprocess.run(
                    [HOST_TMP + "/dinghy", HOST_TMP + "/" + name,
                     HOST_TMP + f"/{name}.txt", "2"],
                    cwd=HOST_TMP, capture_output=True, text=True,
                    timeout=60, env=env)
                return rr.returncode
            except subprocess.TimeoutExpired:
                return "timeout"

        # SELECT through every entry of the song-less book, then
        # live on the empty song slot for a while. v0.30 order:
        # the song is slot 0 (where the boot lands), the lessons
        # follow, empty piece slots are skipped, Free Play last.
        rc = host_run("walk",
                      "T\n"
                      "200 shot\n"          # sel 0: the empty song (boot)
                      "400 enc 0 1\n"
                      "700 shot\n"          # sel 1: first lesson
                      "900 enc 0 1\n"
                      "1200 shot\n"         # sel 2: second lesson
                      "1400 enc 0 1\n"
                      "1700 shot\n"         # sel 6: the drill (slots 3-5 skipped)
                      "1900 enc 0 1\n"
                      "2200 shot\n"         # sel 7: Free Play
                      "2400 enc 0 1\n"
                      "2700 shot\n"         # sel 0 again: the song, wrapped
                      "3200 shot\n"         # still parked, 0.5 s on
                      "3400 btn 12\n"       # PLAY on the empty song
                      "4400 shot\n"
                      "4600 keys 0x80\n"    # a press (C5): a ghost
                      "4800 keys 0\n"
                      "5000 btn 12\n"
                      "5200 enc 3 1\n"      # KNOB2: no part to cue
                      "5400 enc 3 -1\n"
                      "5600 btn 8\n"        # HOME
                      "5800 enc 0 1\n"      # SELECT wraps onward
                      "6100 shot\n"
                      "9000 end\n")
        check("host: the walk runs to its end (no crash, no hang)",
              rc == 0, f"rc {rc}")
        if rc == 0:
            check("host: SELECT moved (lesson 2 != lesson 1)",
                  scroll_hash(HOST_TMP + "/walk-1200.ppm")
                  != scroll_hash(HOST_TMP + "/walk-700.ppm"))
            check("host: the empty song's screen is not Free Play's",
                  scroll_hash(HOST_TMP + "/walk-2700.ppm")
                  != scroll_hash(HOST_TMP + "/walk-2200.ppm"))
            check("host: the parked empty song is stable",
                  scroll_hash(HOST_TMP + "/walk-2700.ppm")
                  == scroll_hash(HOST_TMP + "/walk-3200.ppm"))
            check("host: a press on/near the empty song sounds",
                  wav_rms(HOST_TMP + "/walk.wav", 4.6, 4.95) > 100,
                  f"rms {wav_rms(HOST_TMP + '/walk.wav', 4.6, 4.95):.1f}")
            last = 0
            for line in open(HOST_TMP + "/walk-log.txt"):
                p = line.split()
                if p:
                    last = max(last, int(p[0]))
            check("host: the firmware was still running at 9 s",
                  last >= 8500, f"last frame {last} ms")
        rc = host_run("seeded", "T\n300 shot\n1300 shot\n2000 end\n",
                      seed=0)
        check("host: cold boot seeded at the song slot runs",
              rc == 0, f"rc {rc}")
        if rc == 0:
            check("host: the seeded song park is stable",
                  scroll_hash(HOST_TMP + "/seeded-300.ppm")
                  == scroll_hash(HOST_TMP + "/seeded-1300.ppm"))
            check("host: seeded park == SELECT-walk park",
                  scroll_hash(HOST_TMP + "/seeded-300.ppm")
                  == scroll_hash(HOST_TMP + "/walk-2700.ppm"))

# ---------------------------------------------------------------- errors
FAIL_CASES = [
    # (fixture dir, file the message must name, fragments it must contain)
    ("bad_note", "bad-note.lesson", ["bad note token", "'H5'"]),
    ("bad_duration", "bad-duration.lesson", ["bad duration token"]),
    ("bad_tempo", "bad-tempo.lesson", ["tempo", "not an integer"]),
    ("tempo_range", "tempo-range.lesson", ["tempo 240", "40..200"]),
    ("level_range", "level-range.lesson", ["level 9", "0..5"]),
    ("bar_mismatch", "bar-mismatch.lesson", ["bar 1", "288"]),
    ("pitch_unplayable", "unplayable.lesson", ["A8", "can never be played"]),
    ("title_long", "title-long.lesson", ["title", "48"]),
    ("dup_ident", "a_b.lesson", ["lesson_a_b", "also comes from"]),
    ("song_bad_chord", "book.song", ["bad chord", "'Q'"]),
    ("song_chart_bars", "book.song", ["chart has 1 bars", "melody has 2"]),
    ("song_no_melody", "book.song", ["nope.lesson", "not found"]),
    ("song_unknown_part", "book.song", ["unknown part", "kazoo"]),
    ("song_bad_patch", "book.song", ["unknown patch", "BANJO"]),
]
for case, fname, frags in FAIL_CASES:
    r = run_tool(os.path.join(FIX, case), os.path.join(TMP, case + ".c"))
    ok = (r.returncode != 0 and fname in r.stderr
          and all(f in r.stderr for f in frags))
    check(f"errors: {case} fails, naming the file and the problem", ok,
          f"rc {r.returncode}, stderr {r.stderr.strip()[:160]!r}")

# generated fixtures: too bulky or too derived to check in
GEN = os.path.join(TMP, "gen")
shutil.rmtree(GEN, ignore_errors=True)
os.makedirs(GEN + "/many_events")
with open(GEN + "/many_events/many.lesson", "w") as f:
    f.write("title:    Many Events\nkind:     melody\nsource:   generated\n"
            "level:    1\nposition: C5\ntempo:    90\ntime:     4/4\nnotes:\n")
    f.write("C5 q | D5 q | E5 q | F5 q |\n" * 65)   # 260 events, honest bars
r = run_tool(GEN + "/many_events", GEN + "/many.c")
check("errors: a lesson over LESSON_MAX_EVENTS fails",
      r.returncode != 0 and "260 events" in r.stderr
      and "LESSON_MAX_EVENTS" in r.stderr, r.stderr.strip()[:160])

shutil.copytree(os.path.join(FIX, "book_ok"), GEN + "/two_songs")
shutil.copy(os.path.join(FIX, "book_ok/book.song"), GEN + "/two_songs/second.song")
r = run_tool(GEN + "/two_songs", GEN + "/two.c")
check("errors: two .song files fail (one song slot)",
      r.returncode != 0 and ".song files" in r.stderr
      and "one song slot" in r.stderr, r.stderr.strip()[:160])

os.makedirs(GEN + "/part_cap")
with open(GEN + "/part_cap/whole.lesson", "w") as f:
    f.write("title:    Whole Notes\nkind:     melody\nsource:   generated\n"
            "level:    1\nposition: C5\ntempo:    90\ntime:     4/4\nnotes:\n")
    f.write("C5 w |\n" * 65)   # 65 events, 65 bars: the lesson is legal
with open(GEN + "/part_cap/book.song", "w") as f:
    f.write("title:    Part Cap\nmelody:   whole.lesson\nchart:    "
            + " | ".join(["C"] * 65)
            + "\nparts:    percussion melody\n")
r = run_tool(GEN + "/part_cap", GEN + "/part.c")
check("errors: a generated part over the cap fails",
      r.returncode != 0 and "part 'Percussion' has 260 events" in r.stderr,
      r.stderr.strip()[:160])

# ------------------------------------------------------- the real book
# The shipped book's harmony part (v0.30): every guide pitch must
# sit inside the playable range and on the staff -- the compiler's
# generated-part validation already guarantees it; assert it here
# against the real lessons/ so a future chart change that breaks
# the recipe's fold rule fails loudly.
import importlib.util
import re as _re
_spec = importlib.util.spec_from_file_location("lessons2c_tool", TOOL)
_tool = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_tool)
r = run_tool(os.path.join(REPO, "lessons"), os.path.join(TMP, "real.c"))
check("real book: compiles", r.returncode == 0, r.stderr.strip()[:200])
if r.returncode == 0:
    gen = open(os.path.join(TMP, "real.c")).read()
    m = _re.search(r"song_\w+_harmony_events\[\d+\] = \{(.*?)\};",
                   gen, _re.S)
    check("real book: a harmony part is generated", m is not None)
    if m:
        pitches = [int(p) for p in
                   _re.findall(r"\{ (\d+), \d+, 0 \}", m.group(1))]
        check("real book: harmony guide pitches all playable "
              "(29..103) and on the staff (y 48..204)",
              bool(pitches) and all(
                  _tool.PLAY_MIN <= p <= _tool.PLAY_MAX
                  and _tool.SCROLL_MIN <= _tool.staff_y(p)
                  <= _tool.SCROLL_MAX for p in pitches),
              f"pitches {pitches}")

# ---------------------------------------------------------------- warning
r, _gen = compile_ok("pitch_offstaff")
check("warning: an off-staff note still compiles",
      r.returncode == 0, r.stderr.strip()[:160])
check("warning: ... and the warning names the note and the staff band",
      "warning" in r.stderr and "C4" in r.stderr and "staff y" in r.stderr,
      r.stderr.strip()[:160])

print()
print("FAILURES:" if failures else "ALL LESSONS2C CHECKS PASSED",
      failures if failures else "")
sys.exit(1 if failures else 0)
