# Tests

## Unit tests (no hardware, no template needed)

```sh
cd tests
gcc -Wall -Wextra -std=c99 -I ../firmware/src test_core.c \
    ../firmware/src/lesson.c ../firmware/src/drills.c \
    ../firmware/src/grade.c ../firmware/src/store.c \
    ../firmware/src/ui.c host_stub.c -o test_core
./test_core
```

Covers: lesson parsing (incl. error cases), the practice engine,
drill determinism + invariants, grading math, CRC32, tick conversion.

## Host mode (needs the Dinghy template)

`tests/run_host.sh` (from the template) compiles the same `maestro.c`
for your computer: screen → PNGs, sound → WAV, scripted keys/MIDI.
Iterate on lesson feel here before touching hardware.

## Device

`tests/run_tests.sh` (template): host app, flash maps, update path,
loader, installer. Run before every release tag.
