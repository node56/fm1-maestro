# Licensing and credits

## This project

All original code, lesson files, and docs in this repo are
**GPL-3.0-only** — see `LICENSE`. Whoever gets the firmware gets its
source.

## Platform (from the Dinghy template — not in this tree)

When you build from the template, you also get:

* **Dinghy** by zvenson (GPL-3.0-only): the app skeleton, host mode,
  docs, installer tooling.
* **Felucca** by Leo Kuroshita / Hügelton Instruments (GPL-3.0-only):
  the platform this is built on, as carried by **SLOOP** (isod89).
  `firmware/src/fm6_core.c` is Apache-2.0 (integer C port of msfa);
  some DSP files are MIT — see Felucca's own licensing notes.
* **JieLi AC79 SDK** (Apache-2.0): three of its files ship in every
  firmware package; none are in this tree.

## Lesson content

All `lessons/*.lesson` files are either original exercises written for
this project or melodies in the public domain (composer dead 100+
years, or genuinely traditional). Each file cites its source. No
copyrighted method-book material is included — by design, so this
firmware can be freely shared and listed.

## Piano samples

`firmware/src/piano_data.c` contains 9 piano notes from the **Salamander
Grand Piano V3** by Alexander Holm, a Yamaha C5 grand sampled in minor
thirds, used under **CC-BY 3.0**
(https://creativecommons.org/licenses/by/3.0/). The samples were
downmixed to mono, resampled to 22050 Hz, trimmed, normalized, and given
crossfaded sustain loops for this project. Thank you, Alexander.

## Trademarks

M-VAVE and FM-1 are trademarks of their respective owners. This
project is not affiliated with or endorsed by M-VAVE.
