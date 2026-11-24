# Tests

The full regression runs from this repo:

```sh
python3 tests/run_all.py            # everything
python3 tests/run_all.py v032       # just the cold-path matrix
MAESTRO_BUILD_TREE=/path/to/dinghy python3 tests/run_all.py
```

The runner syncs the repo into the Dinghy build tree (the overlay
of `../BUILDING.md`, plus these tests), builds the host binary
there, and runs every suite below in order, failing if any does.
The repo's copies are canonical; build-tree copies are a cache.

## Unit tests (no hardware, no template needed)

```sh
cd tests
gcc -Wall -Wextra -std=c99 -I ../firmware/src test_core.c -o test_core
./test_core
```

`test_core.c` #includes `store.c` and `drills.c`, so it is one
translation unit; `blob_load`/`blob_save` are stubbed with a RAM
buffer above the includes. Covers: drill determinism + invariants,
and the persistent settings (flash blob 0) — defaults on an empty
or bad blob, save/load round-trip including `sel` persistence,
clamping of out-of-range values, CRC and magic validation.
(The runner also builds and runs it, first.)

## Host mode (needs the Dinghy template)

The suites `verify_cleanup.py` and `v022_verify.py` …
`v032_verify.py` run the real firmware on your computer:
`firmware/src/dinghy.c` compiled with `tests/host.c` (a fake FM-1)
and driven by the scripts in `tests/scripts/` — screen → PPM
screenshots, sound → WAV, plus a per-frame film log (button/key
LEDs, PLAY's colour duties, encoder positions). Each suite checks
one version's behaviour; run them via the runner, not by hand.

- `tests/host.c` carries two Maestro hooks: `DINGHY_SEED_SEL=N`
  pre-seeds the settings blob (cold boots from a persisted
  selection), and an exact-duty mock of the platform's
  `fm1_led_play_set` (film-log fields 11–14).
- `tests/midi_test.c` is the unit test for `firmware/src/midi.c`
  (built and run as part of `v025_verify.py`'s checks).
- `tests/lessons2c_test.py` is the compiler suite for
  `tools/lessons2c.py`: fixture books under
  `tests/fixtures/lessons2c/` (a valid song book, a song-less
  book, one fixture per validation failure, the off-staff
  warning), plus a host build of the song-less book walked
  through every book entry. The runner runs it from the repo,
  after `verify_cleanup.py`.
- `tests/v032_verify.py` is the cold-path matrix: every book
  entry booted from a persisted selection, first gesture checked.
- `tests/v033_verify.py` is the accidentals suite (v0.29): a
  black-key pitch wears the sharp glyph in the blue guide, the
  black take and the amber replay, matched against the glyph
  bitmap in `glyphs.h`; white keys wear nothing.
- `tests/run_host.sh` (from the template) compiles the same
  `maestro.c` for interactive host runs: screen → PNGs, sound →
  WAV, scripted keys/MIDI. Iterate on lesson feel here before
  touching hardware.

## Device

`tests/run_tests.sh` (template, build tree only): host app, flash
maps, update path, loader, installer. Run before every release tag.
