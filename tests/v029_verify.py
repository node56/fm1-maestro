#!/usr/bin/env python3
"""v0.26 host checks: the guided assembly (v0.30: four parts).

Three runs against the host build (plus a look at the song parked):

  open  -- the boot itself lands on the song (slot 0 in the v0.30
           book order), parked at the percussion part's first
           note: the guide showing is all on the pad key's pitch.
  flow  -- a full percussion take ends and auto-cues the bass
           part (v0.31: no replay -- each take ends straight
           into its cue); during the bass take the percussion
           take is audible as backing (a beat the bass rests on
           carries the drum alone); a full bass take cues the
           harmony (v0.30: a real part now -- chord tones in
           their own score, sounding through its fixed PIANO 1
           patch), whose take is backed by BOTH earlier takes;
           a full harmony take cues the melody with all three
           backing takes audible; after the melody take the full
           assembly plays once as ordinary playback (the
           engine's host counters show 4+ voices at once, never
           more than 6, no backing note dropped, no live voice
           ever stolen); the song re-parks at the first part --
           proved by kicking the percussion part off again and
           hearing the NEW take's first hit with the bass
           backing at beat 3, and NO drum at beat 2, where the
           old percussion take had a hit (re-starting the part
           cleared only its own take).
  retry -- a terrible bass take (kick + three wrong notes) sounds
           its wrong note as it is played (on the bass score's
           patch), then the retry reloads the bass part with the
           bass take cleared and the percussion take intact: at
           the wrong note's old position the drum backing sounds
           and the wrong bass note does not.
  led   -- the panel lights (v0.27): PLAY green while parked at
           the song (where the boot lands); REC pulsing while
           merely armed, solid while a take rolls; PLAY red
           steady while the tape rolls; HOME retry parks the
           song part again (PLAY green), a second HOME stays
           parked (v0.31: no book view any more).

The song runs at 96 BPM = 51.2 px/s; the park is x=80 and the
piece ends at x=576. Takes are played the way the v028 suite
plays them: the kick-off press at the park, later presses 120 ms
ahead of the playhead reaching the note, held 200 ms -- so a
recorded note sits ~6 px before its guide note, and sounds that
far into a later roll. A part cycle is ~9.7 s of take + ~10.1 s
of replay; the scripts leave ~0.7 s of slack per cue.
"""
import os, subprocess, sys, wave
import numpy as np
from PIL import Image

ROOT = os.path.expanduser("~/workspace/maestro-build")
HOST = os.path.join(ROOT, "build", "host")
BIN = os.path.join(HOST, "dinghy")
SCR = os.path.join(ROOT, "tests", "scripts")
os.makedirs(SCR, exist_ok=True)
PXPS = 51.2
fails = []

def check(name, cond, extra=""):
    print(("PASS " if cond else "FAIL ") + name + (f"  [{extra}]" if extra else ""))
    if not cond:
        fails.append(name)

# ---------- script generation ----------
PERC = [(80 + 32 * i, 7) for i in range(16)]                       # pad key, every beat
BASS = [(80, 7), (144, 7), (208, 14), (272, 14),
        (336, 12), (400, 12), (464, 7), (528, 7)]                  # C C G G F F C C (written)
HARM = [(80, 7), (112, 11), (144, 14), (176, 11),                   # C bar: 60 64 67 64
        (208, 14), (240, 18), (272, 9), (304, 18),                  # G bar: 67 71 62 71
        (336, 12), (368, 16), (400, 7), (432, 16),                  # F bar: 65 69 60 69
        (464, 7), (496, 11), (528, 14), (560, 11)]                  # C bar: 60 64 67 64
MEL = [(80, 11), (112, 11), (144, 12), (176, 14), (208, 14), (240, 12), (272, 11),
       (304, 9), (336, 7), (368, 7), (400, 9), (432, 11), (464, 11), (512, 9), (528, 7)]

def take(ev, t0, notes):
    for x, k in notes:
        t = t0 if x == 80 else t0 + round((x - 80) * 1000 / PXPS) - 120
        ev.append((t, f"keys 0x{1 << k:x}"))
        ev.append((t + 200, "keys 0"))

def write_script(name, ev, end):
    ev.append((end, "end"))
    ev.sort(key=lambda e: e[0])
    with open(os.path.join(SCR, name), "w") as f:
        for t, a in ev:
            f.write(f"{t} {a}\n")
    return name

ev = [(100, "T")]
take(ev, 1000, PERC)
take(ev, 21500, BASS)
take(ev, 42000, HARM)
take(ev, 62500, MEL)
take(ev, 94000, [(80, 7)])            # restart kick only: the rest of the old take is gone
FLOW = write_script("v029_flow.txt", ev, 98500)

OPEN = write_script("v029_open.txt", [(100, "T"), (600, "shot")], 900)

ev = [(100, "T")]
take(ev, 1000, PERC)
ev.append((21500, "keys 0x80")); ev.append((21700, "keys 0"))       # bass kick (right)
for x in (144, 208, 272):                                            # three wrong notes (key 20)
    t = 21500 + round((x - 80) * 1000 / PXPS) - 120
    ev.append((t, "keys 0x100000")); ev.append((t + 200, "keys 0"))
ev.append((42000, "keys 0x80")); ev.append((42200, "keys 0"))        # retry kick, nothing else
RETRY = write_script("v029_retry.txt", ev, 46000)

ev = [(100, "T"),                  # the boot lands on the song, parked
      (2000, "btn 13"),            # arm REC: pulse
      (5600, "btn 13"),            # disarm
      (7000, "keys 0x80"), (7200, "keys 0"),   # kick off the percussion take
      (12500, "btn 8"),            # HOME: retry (clear + park)
      (14500, "btn 8")]            # HOME: empty take -> the book
LED = write_script("v029_led.txt", ev, 16500)

# ---------- build & run ----------
subprocess.run(["cc", "-O2", "-Wall", "-Wno-unused-function", "-DDINGHY_HOST",
                '-DAPP_VER="dev"', "-Ifirmware/hal", "-Ifirmware/src", "-Ifirmware/gen",
                "-o", BIN, "firmware/src/dinghy.c"], cwd=ROOT, check=True)

def run(script, out, fps):
    r = subprocess.run([BIN, os.path.join(HOST, out), os.path.join(SCR, script), str(fps)],
                       cwd=ROOT, capture_output=True, text=True)
    fm = {}
    for line in r.stderr.splitlines():
        if "FMHOST" in line:
            for kv in line.split()[1:]:
                k, v = kv.split("=")
                fm[k] = int(v)
    return fm

def load(out):
    w = wave.open(os.path.join(HOST, f"{out}.wav"))
    sr = w.getframerate()
    nch = w.getnchannels()
    a = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(float)
    if nch > 1:   # the harness writes stereo; the timeline is frames
        a = a.reshape(-1, nch)[:, 0].copy()
    return a, sr

def rms(a, sr, t0, t1):
    d = a[int(t0 * sr):int(t1 * sr)]
    return float(np.sqrt((d ** 2).mean())) if len(d) else 0.0

def peak_hz(a, sr, t0, t1):
    d = a[int(t0 * sr):int(t1 * sr)]
    if not len(d):
        return 0.0
    d = d * np.hanning(len(d))
    return float(np.argmax(np.abs(np.fft.rfft(d))) * sr / len(d))

def band_rms(a, sr, t0, t1, lo, hi):
    """RMS of the signal restricted to [lo, hi] Hz. Sounding
    pitches in this build (the engine's NOTE_INC is built for a
    22050 Hz render and the harness renders at 44100, so every
    pitch sounds an octave above its written value -- a property
    of the engine since long before v0.26, unchanged here): the
    bass's lowest root ~262 Hz, the melody's lowest note ~523 Hz.
    The drum (v0.30) is a pitch drop: the hit sweeps from ~588 Hz
    down to its ~147 Hz thump over ~50 ms, so a drum reads as a
    strong 300 Hz-4 kHz attack band in its first 100 ms plus a
    settled ~147 Hz peak once the sweep lands -- the drum checks
    below assert that pair instead of the v0.29 signature (a
    stationary 147 Hz peak from the first sample)."""
    d = a[int(t0 * sr):int(t1 * sr)]
    if not len(d):
        return 0.0
    sp = np.fft.rfft(d)
    freqs = np.fft.rfftfreq(len(d), 1 / sr)
    sp[(freqs < lo) | (freqs > hi)] = 0
    return float(np.sqrt((np.fft.irfft(sp, len(d)) ** 2).mean()))

def blue_count(img):
    r, g, b = img[:, :, 0], img[:, :, 1], img[:, :, 2]
    return int(((b > 150) & (r < 80) & (g < 80)).sum())

def led_series(out):
    rows = []
    for line in open(os.path.join(HOST, f"{out}-log.txt")):
        p = line.split()
        if len(p) >= 4 and p[0].isdigit():
            rows.append((int(p[0]), int(p[3])))
    return rows

def bit_in(rows, bit, t0, t1):
    return [((m >> bit) & 1) for (t, m) in rows if t0 <= t <= t1]

def play_series(out):
    """film log -> list of (ms, green duty, red duty): PLAY's colour
    channels (v0.27; log fields 11/12, appended by tests/host.c).
    The button field's bit 12 now reads 'red lit', so PLAY colour
    states are asserted here, not via bit_in."""
    rows = []
    for line in open(os.path.join(HOST, f"{out}-log.txt")):
        p = line.split()
        if len(p) >= 15 and p[0].isdigit():
            rows.append((int(p[0]), int(p[11]), int(p[12])))
    return rows

def play_in(rows, t0, t1):
    return [(g, r) for (t, g, r) in rows if t0 <= t <= t1]

PLAY, REC = 12, 13

# ---------- open: the song parked at the percussion part ----------
run(OPEN, "v029o", 2)
img = np.array(Image.open(os.path.join(HOST, "v029o-600.ppm")).convert("RGB")).astype(int)
check("song opens parked with the percussion guide showing (blue notes)",
      blue_count(img[48:204]) > 200, f"blue={blue_count(img[48:204])}")
check("the guide is the percussion part (a note head on the pad's pitch at the cursor)",
      blue_count(img[179:194, 54:67]) > 30, f"blue at cursor head={blue_count(img[179:194, 54:67])}")

# ---------- the drum's v0.30 signature (a lone hit) ----------
# The kick-off press sounds one hit and the tape rolls on in
# silence (the guide never sounds). On the v0.29 patch this same
# script measured 2011 in the attack band (this suite's
# estimator; a Hann-window share estimator reads the same pair
# of renders as 2728 -> 3356) -- the hit lived under the FM-1
# speaker's roll-off and was barely heard. The re-authored
# patch (pitch drop x4 -> the thump, OP2 at x3) must clear 2500
# in the first 30 ms while the tail stays the same thump.
DRUMHIT = write_script("v029_drumhit.txt",
                       [(100, "T"), (1000, "keys 0x80"), (1300, "keys 0")], 3000)
run(DRUMHIT, "v029d", 2)
a, sr = load("v029d")
check("drum hit: attack-band energy up vs v0.29 (300 Hz-4 kHz, first 30 ms)",
      band_rms(a, sr, 1.0, 1.03, 300, 4000) > 2500,
      f"band={band_rms(a, sr, 1.0, 1.03, 300, 4000):.0f} (v0.29 patch: 2011)")
check("drum hit: the tail is still the thump (100-200 Hz, settled)",
      band_rms(a, sr, 1.12, 1.28, 100, 200) > 100,
      f"band={band_rms(a, sr, 1.12, 1.28, 100, 200):.0f}")

# ---------- flow ----------
fm = run(FLOW, "v029f", 2)
a, sr = load("v029f")
check("percussion take has no backing (drum alone at beat 2: "
      "the v0.30 attack band, then the settled thump)",
      rms(a, sr, 1.45, 1.8) > 500
      and band_rms(a, sr, 1.5, 1.6, 300, 4000) > 800
      and 130 < peak_hz(a, sr, 1.65, 1.85) < 180,
      f"rms={rms(a, sr, 1.45, 1.8):.0f} atk={band_rms(a, sr, 1.5, 1.6, 300, 4000):.0f} "
      f"peak={peak_hz(a, sr, 1.65, 1.85):.0f}")
check("bass take beat 1 sounds the bass (peak ~262 Hz)",
      245 < peak_hz(a, sr, 21.5, 21.9) < 280, f"peak={peak_hz(a, sr, 21.5, 21.9):.0f}")
check("bass take beat 2 is backed by the percussion take alone",
      rms(a, sr, 22.0, 22.35) > 500
      and band_rms(a, sr, 22.0, 22.1, 300, 4000) > 800
      and 130 < peak_hz(a, sr, 22.15, 22.35) < 180,
      f"rms={rms(a, sr, 22.0, 22.35):.0f} atk={band_rms(a, sr, 22.0, 22.1, 300, 4000):.0f} "
      f"peak={peak_hz(a, sr, 22.15, 22.35):.0f}")
check("harmony take beat 2 sounds the harmony over the drum backing "
      "(659 Hz band: the harmony's E, where the bass-take's beat 2 "
      "carries the drum alone)",
      band_rms(a, sr, 42.5, 42.9, 620, 700) > 400
      and band_rms(a, sr, 42.5, 42.9, 620, 700)
          > 3 * band_rms(a, sr, 22.5, 22.9, 620, 700),
      f"harm={band_rms(a, sr, 42.5, 42.9, 620, 700):.0f} "
      f"drumonly={band_rms(a, sr, 22.5, 22.9, 620, 700):.0f}")
check("melody take beat 1 has all four parts (peak ~659 Hz, loud)",
      620 < peak_hz(a, sr, 62.5, 62.9) < 700 and rms(a, sr, 62.5, 62.9) > 2500,
      f"rms={rms(a, sr, 62.5, 62.9):.0f} peak={peak_hz(a, sr, 62.5, 62.9):.0f}")
check("the full assembly plays once after the melody part (v0.31: ordinary playback, no replay before it -- the assembly runs ~72.2-82.3 s)",
      rms(a, sr, 74.5, 75.0) > 2000, f"rms={rms(a, sr, 74.5, 75.0):.0f}")
check("voice ceiling: 4+ voices at once in the assembly, never over 6",
      4 <= fm.get("max_active", 0) <= 6, str(fm))
check("no backing note dropped, no live voice stolen for a backing note",
      fm.get("drops") == 0 and fm.get("steal_live") == 0, str(fm))
check("re-kick after the assembly: beat 1 sounds the new drum hit "
      "(the settled-thump band, with the bass backing's root in it)",
      band_rms(a, sr, 94.0, 94.35, 130, 165) > 60,
      f"lf={band_rms(a, sr, 94.0, 94.35, 130, 165):.0f}")
check("re-kick: beat 2 has no drum (only that part's take was cleared)",
      band_rms(a, sr, 94.5, 94.85, 130, 165) < 30,
      f"lf={band_rms(a, sr, 94.5, 94.85, 130, 165):.0f}")
b3 = band_rms(a, sr, 94.95, 95.35, 245, 280)
b2 = band_rms(a, sr, 94.5, 94.85, 245, 280)
check("re-kick: beat 3 still has the bass backing (other takes untouched)",
      b3 > 200 and b3 > 2 * b2, f"bassband={b3:.0f} vs beat2 {b2:.0f}")

# ---------- retry ----------
run(RETRY, "v029r", 2)
a, sr = load("v029r")
# The wrong note (written 73, routed -12 on the bass score to 61,
# sounding ~554 Hz) is heard as it is played in the bass take: the
# first wrong note is pressed at ~22.6 s. After the terrible take
# ends (~31.2 s) the bass part reloads at once with its take
# cleared; the retry kick at 42 s rolls over the same position at
# ~43.1 s, where the drum backing (intact) hits and the wrong bass
# note must not sound. The 535-575 Hz band is the discriminator.
wrong = band_rms(a, sr, 22.6, 23.0, 535, 575)
check("terrible bass take: the wrong note sounds as it is played (bass patch, ~554 Hz)",
      wrong > 150, f"band={wrong:.0f}")
gone = band_rms(a, sr, 43.05, 43.45, 535, 575)
check("after the retry the bass take is cleared (no bass note at the old position)",
      gone < wrong / 2.5, f"band={gone:.0f} vs take {wrong:.0f}")
check("after the retry the percussion take is intact (drum at the same position)",
      band_rms(a, sr, 43.05, 43.15, 300, 4000) > 800,
      f"atk={band_rms(a, sr, 43.05, 43.15, 300, 4000):.0f}")

# ---------- led ----------
run(LED, "v029l", 10)
rows = led_series("v029l")
prows = play_series("v029l")
check("PLAY green while parked on the boot sheet (the song, v0.30; v0.27 colour)",
      bool(play_in(prows, 300, 900)) and all(gr == 255 and rd == 0 for gr, rd in play_in(prows, 300, 900)))
check("PLAY green while parked at the song's first part (v0.27 colour; was solid on)",
      bool(play_in(prows, 1300, 1900)) and all(gr == 255 and rd == 0 for gr, rd in play_in(prows, 1300, 1900)))
check("REC pulses while merely armed (both phases sampled)",
      0 in bit_in(rows, REC, 2300, 5500) and 1 in bit_in(rows, REC, 2300, 5500))
check("REC dark after disarming", not any(bit_in(rows, REC, 5900, 6500)))
check("PLAY red steady while the take rolls (v0.27 colour; was beat flash, both phases)",
      bool(play_in(prows, 7300, 12000)) and all(gr == 0 and rd == 255 for gr, rd in play_in(prows, 7300, 12000)))
check("REC solid while a take is actually recording",
      bool(bit_in(rows, REC, 7300, 12000)) and all(bit_in(rows, REC, 7300, 12000)))
check("PLAY green again after the HOME retry parks the part (v0.27 colour; was solid on)",
      bool(play_in(prows, 12800, 14000)) and all(gr == 255 and rd == 0 for gr, rd in play_in(prows, 12800, 14000)))
check("PLAY green after the second HOME too (still parked: no book view any more) (v0.27: stopped is stopped)",
      bool(play_in(prows, 14800, 16000)) and all(gr == 255 and rd == 0 for gr, rd in play_in(prows, 14800, 16000)))

print()
if fails:
    print(f"v029: {len(fails)} FAILURES")
    sys.exit(1)
print("v029: all checks passed")
