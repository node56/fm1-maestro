# Tests

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

## Host mode (needs the Dinghy template)

`tests/run_host.sh` (from the template) compiles the same `maestro.c`
for your computer: screen → PNGs, sound → WAV, scripted keys/MIDI.
Iterate on lesson feel here before touching hardware.

## Device

`tests/run_tests.sh` (template): host app, flash maps, update path,
loader, installer. Run before every release tag.
