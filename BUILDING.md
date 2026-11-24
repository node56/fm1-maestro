# Building and testing Maestro

Maestro is an **overlay** on the Dinghy template: the template
supplies the platform (boot, USB, MIDI, screen, storage, the update
loader, `hal/`, the build scripts) and this repo supplies everything
Maestro-specific. `SETUP.md` covers getting the template, the
toolchain and the SDK; this file is the exact file map, the build
commands, and how the tests run. A from-scratch rebuild needs only:
this repo + a Dinghy template checkout + the JieLi toolchain/SDK.

## The overlay: which files go where

Copy (or let `tests/run_all.py` sync) these repo paths over the
template checkout:

| repo | destination in the template tree |
| --- | --- |
| `firmware/src/*` | `firmware/src/` (the app: `maestro.c`, `ui.c`, `drills.c`, `store.c`, `fm_piano.c`, `midi.c`, headers) |
| `firmware/overlay/platform.c` | `firmware/src/platform.c` — **replaces** the template's |
| `firmware/overlay/dinghy.c` | `firmware/src/dinghy.c` — **replaces** the template's (the app entry: it `#include`s `platform.c`, then the app sources, as one translation unit) |
| `lessons/` + `tools/lessons2c.py` | generates `firmware/src/lessons_data.c` (below) |
| `tests/*` | `tests/` (the host suites; the runner syncs them) |

`firmware/overlay/platform.c` is the template's platform file plus
Maestro's additions, kept here so the repo alone can rebuild the
shipped firmware (until refactor round 1 they lived only in the
build tree). The additions are marked in the file: the dim key-glow
layer `fm1_led_key_glow` and, from v0.27, the PLAY colour driver
`fm1_led_play_set(brightness, balance)` — PLAY's green and red dies
PWM-mixed — in both the FM-1 and SMK-37 branches. Everything else
in the file is the template's, unchanged. (This supersedes the
"keep the template's platform.c" line in `SETUP.md` §0, which
predates v0.27.)

`lessons_data.c` is generated, never edited:

```sh
python3 tools/lessons2c.py lessons/ -o firmware/src/lessons_data.c
```

The compiler reads the book directory — the `.lesson` / `.mid`
lessons plus the optional single `.song` (the multi-part song;
absent, the book gets the empty song) — validates it against the
firmware's limits, and emits the C. The formats and the checks are
documented in `lessons/format.md`; the compiler has its own test
suite (`tests/lessons2c_test.py`, fixtures in
`tests/fixtures/lessons2c/`), run by the regression below.

## Build

In the template checkout, with the overlay applied:

```sh
PYTHON=~/.venvs/maestro/bin/python ./build.sh --board fm1     # or smk37
PYTHON=~/.venvs/maestro/bin/python ./build.sh --release 0.27  # a release
```

Dev builds are byte-deterministic: the same sources produce the
same `build/dinghy.bin` (the FM-1 app image) every time. A release
differs only by the identity/version defines the release flag sets.

## Tests

The repo's tests are canonical; the copies in a build tree are a
cache the runner overwrites. From this repo:

```sh
python3 tests/run_all.py            # full regression
python3 tests/run_all.py v032       # just the cold-path matrix
MAESTRO_BUILD_TREE=/path/to/dinghy python3 tests/run_all.py
```

The runner syncs the repo into the build tree (the overlay above,
plus `tests/`), builds the host binary and the unit tests there,
then runs, in order:

- `tests/test_core.c` — template-free unit tests (drill generator,
  persistent settings), compiled straight from the repo;
- `tests/verify_cleanup.py` and `tests/v022_verify.py` …
  `tests/v031_verify.py` — the host-mode suites: the real firmware
  (`firmware/src/dinghy.c` under `tests/host.c`, a fake FM-1) driven
  by the scripts in `tests/scripts/`, checked against screenshots,
  WAV output and the film log. `tests/host.c` carries two
  Maestro-specific hooks: `DINGHY_SEED_SEL=N` (pre-seed the
  settings blob, for cold boots from a persisted selection) and
  the exact-duty mock of `fm1_led_play_set` (film-log fields
  11–14);
- `tests/lessons2c_test.py` — the compiler suite: fixture books
  (`tests/fixtures/lessons2c/`) for the valid song book, the
  song-less book, every validation failure and the off-staff
  warning, plus a host build of the song-less book walked through
  every entry. It runs from the repo (the runner points it at the
  build tree for the host part);
- `tests/v032_verify.py` — the cold-path matrix: every book entry
  (0–5 lessons + drill, 6 Free Play, 7 the song, plus an
  out-of-range persisted value) booted cold from a persisted
  selection, performing its designed first gesture;
- `tests/v033_verify.py` — the accidentals suite (v0.29): the
  sharp glyph a black-key pitch wears, checked in the blue
  guide, the black take and the amber replay against the bitmap
  in `glyphs.h`.

The template's own checks (`tests/run_tests.sh` in the build tree:
loader, OTA, installer, flash maps) are separate and still run
from the build tree before a release.
