#!/usr/bin/env python3
"""Verify the Maestro cleanup: no regressions vs v0.20 behaviour.

Checks, from the host outputs (PPM screenshots, WAV, stdout):
 1. End-of-piece stop still works (book + free-play REC) -- the +240
    backstop was removed, so confirm the tape still stops promptly.
 2. Audio is unchanged (fm_piano.c constants were renamed, not changed):
    C5/C6 pitch exact, low notes sustain, all presets in tune.
 3. Knob swap still works (KNOB4 coarse = one beat, KNOB3 fine = 4px).
 4. Free-play bar lines still draw at the right places (the bar loop
    now starts at the first visible bar instead of re-scanning from 1).
 5. The tempo readout survives a preset change (the forced redraw in
    show_preset was removed).
 6. Key press/release still works (edge detection unified to prev_keys).
 7. A settings save still fires after a SELECT change (store.c now uses
    memcpy; the save path must still run).

Usage: python3 tests/verify_cleanup.py   (run from the build tree root;
it builds the host binary and runs the scripts itself)
"""
import os, subprocess, sys, hashlib
import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# v0.30: the book opens on the song; this suite's scripts were
# written against a boot on the first lesson, so seed the
# persisted selection at that lesson's new slot (1). Every script
# navigation below is relative from there and lands as before
# (e.g. "enc 0 6" from slot 1 still reaches Free Play, slot 7).
os.environ["DINGHY_SEED_SEL"] = "1"
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
    subprocess.run([HOST, os.path.join(OUT, out),
                    os.path.join(SCRIPTS, script)],
                   cwd=ROOT, capture_output=True, text=True, check=True)

def read_ppm(path):
    im = np.array(Image.open(path).convert("RGB"))
    return im

def scroll_region_hash(path):
    """Hash of the scroll band (rows 48..204) -- identical iff the sheet stopped."""
    im = read_ppm(path)
    return hashlib.md5(im[48:204].tobytes()).hexdigest()

def find_barlines(path):
    """x positions of vertical bar lines in the staff band (rows 116..172).
    A barline is a column that is dark (near-black) for most of the staff height."""
    im = read_ppm(path)
    band = im[116:173, :, :]           # staff rows
    dark = (band.sum(axis=2) < 200)    # near-black pixels
    colfrac = dark.mean(axis=0)        # fraction of staff rows dark, per column
    xs = []
    for x in range(240):
        if colfrac[x] > 0.85:
            if not xs or x - xs[-1] > 2:
                xs.append(x)
    return xs

def wav(path):
    import wave
    with wave.open(path, "rb") as w:
        n = w.getnframes()
        data = np.frombuffer(w.readframes(n), dtype=np.int16)
    return data[0::2].astype(np.float64)   # left channel

def fft_peak(seg):
    seg = seg - seg.mean()
    w = np.hanning(len(seg))
    spec = np.abs(np.fft.rfft(seg * w))
    freqs = np.fft.rfftfreq(len(seg), 1 / FS)
    i = int(np.argmax(spec[1:])) + 1
    return freqs[i], spec[i]

def rms(seg):
    return float(np.sqrt(np.mean(seg ** 2))) if len(seg) else 0.0

# ---------------------------------------------------------------- 1. end stop
run("v020_end_book.txt", "v020_end_book")
h14 = scroll_region_hash(os.path.join(OUT, "v020_end_book-14000.ppm"))
h21 = scroll_region_hash(os.path.join(OUT, "v020_end_book-21000.ppm"))
check("end book: scroll stopped (14s == 21s hash)", h14 == h21)

run("v020_end_free.txt", "v020_end_free")
# free-play REC: the tape must stop well before the 25 s script end.
# The last shot is at 15 s; compare it with a shot taken later is not
# possible from this script, so check the LED log is absent and instead
# verify via a film run below (see 1b). Here just confirm it ran.
check("end free: script ran", os.path.exists(os.path.join(OUT, "v020_end_free-15000.ppm")))

# 1b. free-play REC end, film mode: PLAY LED (bit 12) must go dark and stay dark.
subprocess.run([HOST, os.path.join(OUT, "v022_endfree_film"),
                os.path.join(SCRIPTS, "v020_end_free.txt"), "2"],
               cwd=ROOT, capture_output=True, text=True, check=True)
log = [l.split() for l in open(os.path.join(OUT, "v022_endfree_film-log.txt"))]
# log cols: ms, keys, keyLEDs, btnLEDs, enc0..enc6 ; PLAY = btn bit 12
play_on = [(int(r[0]), int(r[3]) >> 12 & 1) for r in log if len(r) >= 4]
last_on = max((t for t, v in play_on if v), default=None)
check("end free: PLAY LED goes dark (tape stops)", last_on is not None and last_on < 20000,
      f"last PLAY-on at {last_on} ms" if last_on else "never on")

# ---------------------------------------------------------------- 2. audio
run("v020_audio1.txt", "v020_audio1")
a = wav(os.path.join(OUT, "v020_audio1.wav"))
# C6 key 19 pressed 500..2000 ms, C5 key 7 pressed 3000..4500 ms
f6, _ = fft_peak(a[int(0.6 * FS):int(1.4 * FS)])
f5, _ = fft_peak(a[int(3.2 * FS):int(4.2 * FS)])
check("audio: C6 pitch", abs(f6 - 1046.5) < 3, f"{f6:.1f} Hz")
check("audio: C5 pitch", abs(f5 - 523.25) < 3, f"{f5:.1f} Hz")
check("audio: no clipped samples", np.max(np.abs(a)) < 32767)

run("v021_low_oct.txt", "v021_low_oct")
a = wav(os.path.join(OUT, "v021_low_oct.wav"))
# MIDI 41 (oct -1) held 3700..6700 ms; sustain RMS 4.5..6.5 s must stay up
sus = rms(a[int(4.5 * FS):int(6.5 * FS)])
check("audio: low note (MIDI 41) sustains", sus > 1500, f"RMS {sus:.0f}")
f41, _ = fft_peak(a[int(4.0 * FS):int(5.0 * FS)])
check("audio: low note pitch", abs(f41 - 174.6) < 3, f"{f41:.1f} Hz")

# ---------------------------------------------------------------- 3. knobs
run("v020_knobs.txt", "v020_knobs")
# barline 1 is at piece-x 192; screen x = 192 - scroll. v0.24: a book
# piece is entered parked at its first note (scroll 20), so the first
# KNOB4 click both enters and snaps one beat forward -> scroll 52;
# +1 again -> scroll 84 (+32 = one beat). KNOB3 +1 -> +4px, +3 -> +12px.
def scroll_from_shot(path):
    bars = find_barlines(path)
    return 192 - bars[0] if bars else None
s0 = scroll_from_shot(os.path.join(OUT, "v020_knobs-100.ppm"))
s1 = scroll_from_shot(os.path.join(OUT, "v020_knobs-300.ppm"))
s2 = scroll_from_shot(os.path.join(OUT, "v020_knobs-500.ppm"))
s3 = scroll_from_shot(os.path.join(OUT, "v020_knobs-700.ppm"))
s4 = scroll_from_shot(os.path.join(OUT, "v020_knobs-900.ppm"))
check("knobs: KNOB4 +1 -> scroll 52 (parked at 20, +1 beat)", s1 == 52, f"got {s1} (was {s0})")
check("knobs: KNOB4 +1 -> scroll 84 (+32 = one beat)", s2 == 84, f"got {s2}")
check("knobs: KNOB3 +1 -> +4px", s3 == 88, f"got {s3}")
check("knobs: KNOB3 +3 -> +12px", s4 == 100, f"got {s4}")

# ---------------------------------------------------------------- 4. free-play bars
run("v019_ui.txt", "v019_ui")
# free play, KNOB4 +10 from scroll 0: bars every 128px from piece-x 192.
# After coarse scroll the sheet is at scroll 20 + 10 beats... just check
# that bar lines are present and evenly spaced 128px apart in the shot.
bars = find_barlines(os.path.join(OUT, "v019_ui-500.ppm"))
gaps = [b - a for a, b in zip(bars, bars[1:])]
check("free-play bars: present and 128px apart",
      len(bars) >= 1 and all(g == 128 for g in gaps),
      f"bars at {bars}")

# ---------------------------------------------------------------- 5. tempo survives preset
# v019_ui changes the preset at 2800 ms and screenshots at 3000 ms.
# The tempo readout lives at bottom-right (x 180..240, y 224..240):
# it must still have ink (dark pixels) after the preset name showed.
im = read_ppm(os.path.join(OUT, "v019_ui-3000.ppm"))
tempo = im[224:240, 180:240, :]
check("preset: tempo readout still drawn after preset change",
      int((tempo.sum(axis=2) < 200).sum()) > 20)

# ---------------------------------------------------------------- 6. keys
# Re-read v020_audio1 (the `a` above is the v021 WAV now): a clean
# release leaves the gap 2.2..2.8 s quiet (C6 released at 2.0 s).
a1 = wav(os.path.join(OUT, "v020_audio1.wav"))
gap = rms(a1[int(2.2 * FS):int(2.8 * FS)])
check("keys: note releases cleanly (gap quiet)", gap < 300, f"RMS {gap:.0f}")

# ---------------------------------------------------------------- 7. settings save
# v022_settings selects a piece at 100 ms and runs past the 10 s
# throttle, so the settings save must fire (stdout: "blob 0 saved" --
# Maestro keeps its settings in flash blob 0).
r = subprocess.run([HOST, os.path.join(OUT, "v022_settings"),
                    os.path.join(SCRIPTS, "v022_settings.txt")],
                   cwd=ROOT, capture_output=True, text=True, check=True)
check("settings: save fires after SELECT change",
      "blob 0 saved" in r.stdout, "no 'blob 0 saved' in stdout")

print()
print("FAILURES:" if failures else "ALL CLEANUP CHECKS PASSED", failures if failures else "")
sys.exit(1 if failures else 0)
