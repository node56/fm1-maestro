# Setup: from zero to your firmware on the FM-1

This repo is an **overlay**: the platform (boot, USB, MIDI, screen,
storage, update loader) comes from the Dinghy template. Our code is the
app on top (`maestro.c`, the lesson engine, lessons, tools).

## 0. Start from the template

On GitHub, open [zvenson/dinghy](https://github.com/zvenson/dinghy) →
**Use this template** → create your repo (rename it here — see
`docs/MAKE-IT-YOURS.md`). Clone it, then copy this tree over it:

```sh
git clone <your-repo-url> fm1-maestro && cd fm1-maestro
# copy: README.md SETUP.md LICENSE LICENSING.md docs/ lessons/
#        firmware/src/maestro.c firmware/src/{lesson,drills,grade,ui}.* 
#        firmware/src/store.h tools/lessons2c.py web-installer/ tests/
# (keep the template's platform.c, hal/, build.sh, tools/fm1_install.py,
#  .github/workflows, tests/run_host.sh, tests/run_tests.sh)
```

Rename nothing else until the unchanged template builds on your machine.

## 1. Toolchain (once)

Linux x86-64 (or macOS/Windows with Docker or WSL):

```sh
pip3 install Pillow mido python-rtmidi
tools/get_toolchain.sh                      # JieLi clang → ~/.jieli/toolchain
git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
    https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
```

On macOS the build runs the toolchain in a `linux/amd64` Docker
container by itself. `JIELI_TOOLCHAIN` / `AC79_SDK` override the paths.

## 2. Develop without hardware (host mode)

The same `maestro.c` compiles on your computer: screen → PNG images,
sound → WAV, scripted keys/knobs/MIDI.

```sh
# pure-C core unit checks (no platform needed):
gcc -Wall -Wextra -std=c99 -I firmware/src -c firmware/src/lesson.c \
    firmware/src/drills.c firmware/src/grade.c

# full host run (from the template):
tests/run_host.sh        # → build/host/*.png, build/host/*.wav
```

Most lesson-design iteration happens here, in seconds.

## 3. Build the firmware

```sh
python3 tools/lessons2c.py lessons/ -o firmware/src/lessons_data.c
./build.sh               # → build/maestro.fwsc
```

The build **fails** on anything that bricks or breaks an FM-1 firmware:
a 64-bit division, a call into the chip ROM, too much RAM, a register
touched outside `hal/`. There is no FPU — the lesson engine uses
integer math throughout (durations in ticks, quarter = 96).

## 4. Install on the FM-1

```sh
python3 tools/fm1_install.py build/maestro.fwsc   # over USB; asks before writing
python3 tools/fm1_install.py --info               # what's on the connected FM-1
```

Or the web installer in Chrome/Edge, or the FM-1 Firmware Hub
one-click install once listed. Back to stock: the hub, or M-VAVE's
M-UPGRADE with official firmware.

**Safety net (from the platform):** hold **OCT-** while switching on for
USB rescue (updater only, no sound). Two boot crashes in a row → rescue
by itself. A crash screen shows the fault PC, then restarts.

## 5. SMK-37 build

```sh
./build.sh --board smk37     # 37 velocity-sensitive keys, pads, faders
```

See `docs/SMK37.md`.

## 6. Release

Tag `vX.Y` on GitHub → CI builds, tests, and releases the `.fwsc`
with a build attestation. Then the hub-submission checklist in
`docs/MAKE-IT-YOURS.md`.
