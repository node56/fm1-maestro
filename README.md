# Maestro

**Duolingo-style music lessons, running entirely on the M-VAVE FM-1. No phone needed.**

Maestro turns the FM-1 into a patient music teacher: pick a lesson on the
screen, play the 27-key keyboard, and get instant feedback on pitch and
timing — note reading, rhythm, five-finger melodies, and endless
algorithmically generated sight-reading drills. All lesson content is
public-domain or generated; nothing copyrighted ships in this repo.

> **Status: scaffold.** The lesson format, the first lesson set, the
> platform-independent lesson engine (parser, drill generator, grader),
> and the docs are real. The on-device UI glue and audio hooks are
> clearly-marked integration points against the Dinghy platform
> (`docs/API.md` in the template). See `SETUP.md` for the build path.

## How it works

```
lessons/*.lesson ──tools/lessons2c.py──▶ firmware/src/lessons_data.c
                                              │
firmware/src/maestro.c      ◀── platform.c ────┤ (Dinghy template:
  app_main()   screen + keys                    boot, USB, MIDI,
  app_audio()  sound                             screen, storage)
       │
       ├── drills.c   seeded generator: infinite sight-reading & rhythm drills
       └── ui.c       240×240 screens: select / practice / results
```

* **Practice screen:** a scrolling piano roll. Target notes fall toward a
  play line; the key you should press is highlighted on the mini-keyboard
  strip. Hit it in time → green flash + score; miss → red, and the
  phrase loops until you get it.
* **Drills:** a seeded PRNG (xorshift32) generates melodies constrained to
  your level's five-finger position. Same seed = same drill, so drills are
  shareable and repeatable. Difficulty adapts: range, rhythms and tempo
  widen as your scores improve.
* **Progress:** stars and best scores per lesson live in a flash blob
  (`blob_*` storage), with CRCs. Nothing leaves the device.

## Repo layout

| Path | What |
|---|---|
| `lessons/` | Lesson files in the plain-text format (`lessons/format.md`), plus the first set |
| `firmware/src/maestro.c` | App entry: `app_main()` / `app_audio()` — start here |
| `firmware/src/lesson.{h,c}` | Lesson parser + practice engine (pure C, host-testable) |
| `firmware/src/drills.{h,c}` | Seeded generative drills (pure C, host-testable) |
| `firmware/src/grade.{h,c}` | Pitch/timing grading, integer math (pure C, host-testable) |
| `firmware/src/ui.{h,c}` | Screen wrappers; bind to platform calls per Dinghy `docs/API.md` |
| `firmware/src/store.h` | Progress persistence via the platform's `blob_*` storage |
| `tools/lessons2c.py` | Converts `lessons/*.lesson` → `lessons_data.c` |
| `web/simulator.html` | Browser simulator: the lesson UI, 27 keys, real lessons — no hardware needed |
| `web-installer/` | One-click installer page stub (hub pattern) |
| `docs/` | Lesson design, content plan, UI mock, SMK-37 port, hub submission |

**Not in this tree** (comes from the template, see `SETUP.md`):
`firmware/src/platform.c`, `hal/`, `build.sh`, `tools/fm1_install.py`,
`.github/workflows`.

## Building

Short version (full steps in `SETUP.md`):

```sh
# 0. Start from the Dinghy template, then overlay this tree
# 1. Toolchain once:
pip3 install Pillow mido python-rtmidi
tools/get_toolchain.sh
git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
    https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
# 2. Try ideas without hardware:
tests/run_host.sh            # screen PNGs + WAV out
# 3. Build + install:
/build.sh
python3 tools/fm1_install.py build/maestro.fwsc
```

`./build.sh --board smk37` targets the M-VAVE SMK-37 Pro/Elite (37
velocity-sensitive keys) — same app, bigger keyboard. See `docs/SMK37.md`.

## Installing (no terminal needed)

The web installer flashes the FM-1 straight from Chrome or Edge over
USB-MIDI — no Python, no command line:

**https://node56.github.io/fm1-maestro/** (served via GitHub Pages)

Connect the FM-1 with a USB data cable, press Connect, approve the MIDI
prompt, then Install. If an install is ever interrupted, the page offers to
resume it. The installer page and protocol live in `docs/`; the update
protocol (`vendor/fm1ota.js`) and package tools (`vendor/fm1pkg.js`) are by
Leo Kuroshita / Hügelton Instruments (GPL-3.0), vendored verbatim.

## Getting listed

When a build is tested on a real FM-1: tag `vX.Y` → CI builds the
`.fwsc` → GitHub Release → submit to the FM-1 Firmware Hub
(`docs/MAKE-IT-YOURS.md` checklist). fwradar picks up working builds
with a real following on its daily checks.

## License

GPL-3.0-only, like Felucca, SLOOP and Dinghy: whoever gets the firmware
gets its source. See `LICENSE`, and `LICENSING.md` for credits and
third-party parts.

## Credits

* Platform: [Dinghy](https://github.com/zvenson/dinghy) by zvenson —
  the smallest FM-1 firmware to build on (updates, USB rescue, MIDI,
  screen, storage, SMK-37 layer).
* Dinghy's platform: [Felucca](https://github.com/hugelton/Felucca) by
  Leo Kuroshita / Hügelton Instruments, as carried by
  [SLOOP](https://github.com/isod89/sloop-fm1).
* M-VAVE and FM-1 are trademarks of their respective owners. This
  project is not affiliated with or endorsed by M-VAVE.
