#!/usr/bin/env python3
"""v034 -- Maestro v0.31: the mode kills and the score/piece model.

New coverage for the behaviours v0.31 changes, each of which the
old suites could not have caught:

  A  SELECT on the tape through >= 2 sheets: the title band
     changes at every step, and the intermediate sheet's title
     band equals a fresh boot seeded at that sheet -- the check
     that would have caught the stale-title bug (the old suites
     only compared the return park, where the stale title
     happened to be right again).
  B  HOME-then-first-note kicks off: scrub away from the park,
     HOME rewinds, the first-note press kicks the take off.
     (v0.31: HOME rewinds to the park instead of opening a book
     view; the kick-off gate no longer requires "not in book".)
  C  KNOB2 free stepping: after the percussion score is done,
     two detents step past the first unplayed score (harmony)
     to the melody -- the old frontier would have blocked both.
     Proved behaviourally: the pad key ghosts (wrong for the
     melody) while the melody's first note (E5) kicks off.
  D  per-score takes: complete percussion + bass, KNOB2 back,
     HOME retries the percussion score only -- its take is
     cleared (no drum at beat 2) while the bass take is intact
     (its beat-3 root still sounds as backing).
  E  the assembly as ordinary playback: after all four scores,
     a key press during the assembly is a normal ghost press --
     it sounds, it does not skip the assembly (PLAY stays red
     mid-assembly), and the assembly still ends by re-parking
     at the first score.
  F  the tempo overlay (v0.31 fix): 100 -> 98 BPM leaves no
     stale glyph -- the 98 band after 100 -> 98 is pixel-equal
     to a fresh 98 band, and differs from the 100 band.
  G  the long title truncates inside x=8..220 (v0.32): the
     octave indicator at x=220 erased the tail of the 29-char
     percussion title, and the 236 px title blit ran 4 px past
     the screen edge into the device's unclipped lcd_blit. The
     song's longest title must render fully within the band, with
     no ink at x>=220, and the truncation must be deterministic.
  H  only the current score's take draws (v0.32): the take loop
     drew every score's take in black on the current score's
     sheet, so the melody sheet showed the drum/bass/harmony
     takes over the blue guide. Proved differentially: the
     melody sheet during the melody take is pixel-identical
     whether or not the other three takes are recorded. And a
     Free Play take still draws.

Usage: python3 tests/v034_verify.py   (from the build tree root --
tests/run_all.py syncs this repo's tests there and runs it)
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

PXPS = 51.2   # the song at 96 BPM: px per second


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


def shot(out, ms):
    return os.path.join(HOST, f"{out}-{ms}.ppm")


def scroll_hash(path):
    im = np.array(Image.open(path).convert("RGB"))
    return hashlib.sha256(im[48:204].tobytes()).hexdigest()


def band_hash(path, rows):
    """hash of a horizontal band (title: rows 0..48; tempo: 224..240)."""
    im = np.array(Image.open(path).convert("RGB"))
    return hashlib.sha256(im[rows[0]:rows[1]].tobytes()).hexdigest()


def dark_px(path, rows, x0=0, x1=240):
    """count of near-black pixels in a band (INK text and notes)."""
    im = np.array(Image.open(path).convert("RGB")).astype(np.int16)
    band = im[rows[0]:rows[1], x0:x1]
    return int(((band[:, :, 0] < 100) & (band[:, :, 1] < 100)
                & (band[:, :, 2] < 100)).sum())


def rightmost_ink(path, rows):
    """rightmost column with a near-black pixel in a band, or -1."""
    im = np.array(Image.open(path).convert("RGB")).astype(np.int16)
    band = im[rows[0]:rows[1]]
    dark = (band[:, :, 0] < 100) & (band[:, :, 1] < 100) & (band[:, :, 2] < 100)
    cols = np.where(dark.any(axis=0))[0]
    return int(cols.max()) if len(cols) else -1


def still(out, ms_list):
    hs = [scroll_hash(shot(out, ms)) for ms in ms_list]
    return all(h == hs[0] for h in hs)


def rolls(out, parked_ms, ms2, ms3):
    h0 = scroll_hash(shot(out, parked_ms))
    h2 = scroll_hash(shot(out, ms2))
    h3 = scroll_hash(shot(out, ms3))
    return h0 != h2 and h2 != h3


def wav_load(out):
    w = wave.open(os.path.join(HOST, f"{out}.wav"))
    sr = w.getframerate()
    nch = w.getnchannels()
    a = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(float)
    if nch > 1:
        a = a.reshape(-1, nch)[:, 0].copy()
    return a, sr


def wav_rms(a, sr, t0, t1):
    d = a[int(t0 * sr):int(t1 * sr)]
    return float(np.sqrt((d ** 2).mean())) if len(d) else 0.0


def band_rms(a, sr, t0, t1, lo, hi):
    d = a[int(t0 * sr):int(t1 * sr)]
    if not len(d):
        return 0.0
    sp = np.fft.rfft(d)
    freqs = np.fft.rfftfreq(len(d), 1 / sr)
    sp[(freqs < lo) | (freqs > hi)] = 0
    return float(np.sqrt((np.fft.irfft(sp, len(d)) ** 2).mean()))


def peak_hz(a, sr, t0, t1):
    d = a[int(t0 * sr):int(t1 * sr)]
    if not len(d):
        return 0.0
    d = d * np.hanning(len(d))
    return float(np.argmax(np.abs(np.fft.rfft(d))) * sr / len(d))


def play_series(out):
    rows = []
    for line in open(os.path.join(HOST, f"{out}-log.txt")):
        p = line.split()
        if len(p) >= 15 and p[0].isdigit():
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


# the song's scores, as the v029 suite plays them (press 120 ms
# early, held 200 ms -- a recorded note sits ~6 px before its guide)
PERC = [(80 + 32 * i, 7) for i in range(16)]
BASS = [(80, 7), (144, 7), (208, 14), (272, 14),
        (336, 12), (400, 12), (464, 7), (528, 7)]
HARM = [(80, 7), (112, 11), (144, 14), (176, 11),
        (208, 14), (240, 18), (272, 9), (304, 18),
        (336, 12), (368, 16), (400, 7), (432, 16),
        (464, 7), (496, 11), (528, 14), (560, 11)]
MEL = [(80, 11), (112, 11), (144, 12), (176, 14), (208, 14), (240, 12), (272, 11),
       (304, 9), (336, 7), (368, 7), (400, 9), (432, 11), (464, 11), (512, 9), (528, 7)]


def take(ev, t0, notes):
    for x, k in notes:
        t = t0 if x == 80 else t0 + round((x - 80) * 1000 / PXPS) - 120
        ev.append((t, f"keys 0x{1 << k:x}"))
        ev.append((t + 200, "keys 0"))


build()

# ---------------------------------------------------------------- A: the title band follows SELECT
ev = [(500, "shot"), (700, "enc 0 1"), (1000, "shot"),
      (1200, "enc 0 1"), (1500, "shot"), (1700, "enc 0 -2"),
      (2000, "shot")]
run(write_script("v034_title.txt", ev, 2300), "v034a", seed=0)
run(write_script("v034_title_ref1.txt", [(500, "shot")], 800), "v034ar1", seed=1)
run(write_script("v034_title_ref2.txt", [(500, "shot")], 800), "v034ar2", seed=2)
t_song = band_hash(shot("v034a", 500), (0, 48))
t_l01 = band_hash(shot("v034a", 1000), (0, 48))
t_l02 = band_hash(shot("v034a", 1500), (0, 48))
t_back = band_hash(shot("v034a", 2000), (0, 48))
check("A title: SELECT song -> L01 changes the title band",
      t_song != t_l01)
check("A title: SELECT L01 -> L02 changes the title band again",
      t_l01 != t_l02)
check("A title: SELECT back returns to the song's title band",
      t_back == t_song)
check("A title: the intermediate L01 title band == a fresh boot seeded at L01",
      t_l01 == band_hash(shot("v034ar1", 500), (0, 48)),
      "the stale-title check: the old code showed the song's title here")
check("A title: the intermediate L02 title band == a fresh boot seeded at L02",
      t_l02 == band_hash(shot("v034ar2", 500), (0, 48)))

# ---------------------------------------------------------------- B: HOME, then the first note kicks off
ev = [(400, "shot"), (500, "enc 5 2"), (1000, "shot"),
      (1100, "btn 8"), (1500, "keys 0x80"), (1800, "keys 0"),
      (2100, "shot"), (3300, "shot")]
run(write_script("v034_homekick.txt", ev, 3700), "v034b", seed=1)
check("B home: the coarse scrub moved the sheet before HOME",
      scroll_hash(shot("v034b", 1000)) != scroll_hash(shot("v034b", 400)))
check("B home: HOME rewinds, and the first-note press kicks off",
      rolls("v034b", 1000, 2100, 3300))

# ---------------------------------------------------------------- C: KNOB2 steps freely past the frontier
ev = [(100, "T")]
take(ev, 1000, PERC)          # the percussion take ends ~10.7 s; the bass cues
ev += [(11500, "enc 3 1"),    # -> harmony (the old frontier blocked this)
       (11600, "enc 3 1"),    # -> melody (blocked too)
       (12000, "shot"),
       (12500, "keys 0x80"), (12800, "keys 0"),   # the pad key: wrong for the melody
       (13300, "shot"), (13900, "shot"),
       (14500, "keys 0x800"), (14800, "keys 0"),  # the melody's first note (E5)
       (16000, "shot"), (17500, "shot")]
run(write_script("v034_knob2.txt", ev, 18000), "v034c", seed=0)
check("C knob2: the pad key does NOT kick off (not the melody's first note)",
      still("v034c", [13300, 13900]))
check("C knob2: the melody's first note DOES kick off (two free steps reached it)",
      rolls("v034c", 13900, 16000, 17500))

# ---------------------------------------------------------------- D: HOME retries one score only
ev = [(100, "T")]
take(ev, 1000, PERC)
take(ev, 11500, BASS)         # the bass take ends ~21.2 s; the harmony cues
ev += [(22000, "enc 3 -1"),   # -> bass
       (22100, "enc 3 -1"),   # -> percussion
       (22500, "btn 8")]      # HOME: retry the percussion score only
take(ev, 23000, [(80, 7)])    # one fresh drum hit, then the take rolls on
run(write_script("v034_perscore.txt", ev, 27000), "v034d", seed=0, film=2)
a, sr = wav_load("v034d")
check("D perscore: the fresh drum hit sounds at the re-kick",
      band_rms(a, sr, 23.0, 23.1, 300, 4000) > 800,
      f"atk={band_rms(a, sr, 23.0, 23.1, 300, 4000):.0f}")
check("D perscore: no drum at beat 2 (the old percussion take is cleared)",
      band_rms(a, sr, 23.5, 23.6, 300, 4000) < 800,
      f"atk={band_rms(a, sr, 23.5, 23.6, 300, 4000):.0f}")
check("D perscore: the bass backing is intact at beat 3 (peak ~262 Hz)",
      245 < peak_hz(a, sr, 24.1, 24.5) < 280,
      f"peak={peak_hz(a, sr, 24.1, 24.5):.0f}")

# ---------------------------------------------------------------- E: the assembly is ordinary playback
ev = [(100, "T")]
take(ev, 1000, PERC)
take(ev, 11500, BASS)
take(ev, 22000, HARM)
take(ev, 32500, MEL)          # the melody take ends ~42.2 s; the assembly starts
ev += [(44000, "keys 0x1"), (44300, "keys 0"),   # a key press mid-assembly
       (50000, "shot"), (54000, "shot")]
run(write_script("v034_assembly.txt", ev, 55000), "v034e", seed=0, film=2)
a, sr = wav_load("v034e")
check("E assembly: the mid-assembly press sounds (a normal ghost press)",
      wav_rms(a, sr, 44.0, 44.3) > 100,
      f"rms={wav_rms(a, sr, 44.0, 44.3):.0f}")
prows = play_series("v034e")
check("E assembly: the press does NOT skip it (PLAY still red at 50 s)",
      bool(play_in(prows, 49500, 50500))
      and all(gr == 0 and rd == 255 for gr, rd in play_in(prows, 49500, 50500)),
      f"got {play_in(prows, 49500, 50500)[:4]}")
check("E assembly: it ends by re-parking at the first score (PLAY green at 53.5 s)",
      play_at(prows, 53500) == (255, 0),
      f"got {play_at(prows, 53500)}")
# (the full scroll hash differs from the boot park: the recorded
# takes are drawn in black on the re-parked sheet. The BLUE guide
# -- the part that proves the park position -- is identical.)
def blue_mask(path):
    im = np.array(Image.open(path).convert("RGB")).astype(np.int16)
    return (im[:, :, 2] - im[:, :, 0] > 30)
check("E assembly: the re-parked sheet shows the same guide, at the park",
      np.array_equal(blue_mask(shot("v034e", 54000)),
                     blue_mask(shot("v034a", 500))))

# ---------------------------------------------------------------- F: the tempo overlay after 100 -> 98 BPM
# (the song boots at 96 BPM; KNOB1 is enc 2, 2 BPM per detent.)
ev = [(500, "shot"), (700, "enc 2 2"), (1000, "shot"),
      (1200, "enc 2 -1"), (1500, "shot")]
run(write_script("v034_tempo.txt", ev, 1800), "v034f", seed=0)
ev = [(500, "shot"), (700, "enc 2 1"), (1000, "shot")]
run(write_script("v034_tempo_fresh.txt", ev, 1300), "v034ff", seed=0)
band = lambda out, ms: np.array(Image.open(shot(out, ms)).convert("RGB"))[224:240, 180:240]
check("F tempo: 100 -> 98 leaves no stale glyph (band == a fresh 98 band)",
      np.array_equal(band("v034f", 1500), band("v034ff", 1000)))
check("F tempo: the 100 band differs from the 98 band (the change happened)",
      not np.array_equal(band("v034f", 1000), band("v034ff", 1000)))

# ---------------------------------------------------------------- G: the long title truncates inside x=8..220
# (v0.32: the octave indicator at x=220 erased the tail of the
# 29-char percussion title, and the 236 px title blit ran 4 px past
# the screen edge into the device's unclipped lcd_blit.)
ev = [(500, "shot")]
run(write_script("v034_title32.txt", ev, 800), "v034g", seed=0)
check("G title: no ink at x>=220 in the title band (the octave strip owns it)",
      dark_px(shot("v034g", 500), (0, 48), 220, 240) == 0,
      f"ink={dark_px(shot('v034g', 500), (0, 48), 220, 240)}")
check("G title: the truncated title ends at x=213 (26 chars, 'Ode to Joy (Song): Perc...')",
      rightmost_ink(shot("v034g", 500), (10, 26)) == 213,
      f"rightmost={rightmost_ink(shot('v034g', 500), (10, 26))}")
check("G title: the truncated band is deterministic (== the v034a boot band)",
      band_hash(shot("v034g", 500), (0, 48)) == t_song)

# ---------------------------------------------------------------- H: only the current score's take draws
# (v0.32: the take loop drew every score's take in black on the
# current score's sheet, so the melody sheet showed the drum/bass/
# harmony takes over the blue guide. Backing is heard, not seen.)
# H1: the melody sheet during the melody take is pixel-identical
# whether or not the other three takes are recorded.
ev = [(100, "T")]
take(ev, 1000, PERC)
take(ev, 11500, BASS)
take(ev, 22000, HARM)
take(ev, 32500, MEL)
ev += [(35500, "shot"), (38500, "shot")]
run(write_script("v034_takefilter_all.txt", ev, 39500), "v034h1", seed=0)
ev = [(100, "T"),
      (500, "enc 3 1"), (600, "enc 3 1"), (700, "enc 3 1")]  # percussion -> bass -> harmony -> melody
take(ev, 1000, MEL)
ev += [(4000, "shot"), (7000, "shot")]
run(write_script("v034_takefilter_mel.txt", ev, 8000), "v034h2", seed=0)
check("H take: the melody sheet 3 s into the take is the same with all takes recorded as with the melody alone",
      scroll_hash(shot("v034h1", 35500)) == scroll_hash(shot("v034h2", 4000)))
check("H take: the same 6 s into the take",
      scroll_hash(shot("v034h1", 38500)) == scroll_hash(shot("v034h2", 7000)))
# H2: a Free Play take still draws (its events are tagged with its
# score, and the filter keeps the current score's take).
ev = [(100, "enc 0 7"),
      (200, "btn 12"), (300, "btn 13"),
      (400, "keys 0x80"), (700, "keys 0"),
      (800, "keys 0x800"), (1100, "keys 0"),
      (1200, "keys 0x4000"), (1500, "keys 0"),
      (1600, "btn 13"), (1700, "btn 12"),
      (2000, "shot")]
run(write_script("v034_free_take.txt", ev, 2500), "v034h3", seed=0)
ev = [(100, "enc 0 7"),
      (200, "btn 12"), (300, "btn 13"),
      (1600, "btn 13"), (1700, "btn 12"),
      (2000, "shot")]
run(write_script("v034_free_notake.txt", ev, 2500), "v034h4", seed=0)
n_take = dark_px(shot("v034h3", 2000), (48, 204))
n_free = dark_px(shot("v034h4", 2000), (48, 204))
check("H free: a Free Play take draws (its band differs from the take-free band)",
      scroll_hash(shot("v034h3", 2000)) != scroll_hash(shot("v034h4", 2000)))
check("H free: the take adds black noteheads to the scroll band",
      n_take > n_free + 50,
      f"take={n_take} free={n_free}")

print()
if FAILS:
    print(f"v034: {len(FAILS)} check(s) failed: {FAILS}")
    sys.exit(1)
print("v034: all checks passed")
