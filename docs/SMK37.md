# SMK-37 port

The M-Vave SMK-37 Pro / Elite (and the Donner Starrykey-37 Play and
EROK MKE-P37, same board family) run the **same JieLi AC791N chip**
as the FM-1. The Dinghy platform abstracts the board: the same app
builds for it with a flag.

```sh
./build.sh --board smk37
```

What changes for Maestro:

* **37 velocity-sensitive keys** instead of 27 silicone: real
  five-finger positions across more range, less OCT± juggling, and
  dynamics become teachable (velocity-graded lessons from L3).
* **16 coloured pads:** map to lesson shortcuts (retry, metronome,
  drill new seed) or rhythm-drill input.
* **Faders/wheels:** tempo and metronome volume on hardware.
* **Screen:** same UI code; wider key map, dynamics lane.

Lesson content is board-independent (`lessons/*.lesson` + generator
parameters); only the UI strip width and velocity grading switch per
board at compile time (`BOARD_SMK37`). The FM-1 remains the primary
target: if it runs there, it runs on the SMK-37.

Note: the SMK-37 is the "real keyboard feel" path — full-size keys
for hand position and technique — while the FM-1 is the
pocket practice path. Same firmware, same progress format.
