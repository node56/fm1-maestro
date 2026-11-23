# Lesson file format

Plain text, hand-editable, parsed at build time by
`tools/lessons2c.py` into `firmware/src/lessons_data.c`. One file per
lesson. There is no runtime lesson parser in the firmware, so the
compiler is where a book is checked: anything malformed or beyond
the firmware's limits fails the build, naming the file and the
problem (see "What the compiler checks" below). A book may also
carry one multi-part song (see "Songs").

```
# lines starting with '#' are comments
title:    First Five Notes
kind:     melody            # note-id | rhythm | melody | drill | challenge
source:   Original exercise — no rights reserved beyond this repo's license
level:    0                 # 0..5, see docs/LESSON-DESIGN.md
hand:     right
position: C5                # five-finger position root (lowest note)
tempo:    72                # quarter-note BPM
time:     4/4
notes:
C5 q | D5 q | E5 q | F5 q |
G5 q | F5 q | E5 q | D5 q |
C5 h | R h |
```

## Notes

`A`–`G` with optional `#`/`b` and an octave digit. Octave convention:
**C5 == MIDI 60 (middle C)**.

## Durations (ticks; quarter = 96)

| Symbol | Meaning | Ticks |
|---|---|---|
| `w` | whole | 384 |
| `h` | half | 192 |
| `q` | quarter | 96 |
| `e` | eighth | 48 |
| `s` | sixteenth | 24 |

Append `.` for dotted (×1.5): `q.` = 144. `R` is a rest and takes any
duration (`R q`). `|` separates notes — readability only, ignored by
the parser. **One line of the `notes:` section is one bar**, and the
compiler checks that every line adds up to the declared `time:`.

## Standard MIDI files

A `.mid` file in `lessons/` is a lesson too, parsed directly by
`tools/lessons2c.py` — no re-encoding, no intermediate format. An
ordinary public-domain `.mid` file can be dropped in as-is:

* Format 0 or 1, any PPQ (ticks are rescaled to the firmware's 96).
* Notes are Note On / Note Off pairs (a Note On with velocity 0 is
  a Note Off). The lesson is a single melodic line: at each onset
  the highest pitch wins, a note starting while another sounds is
  skipped, and gaps between notes become rests.
* Title from the track-name meta (0x03), else the file name; tempo
  from the Set Tempo meta (0x51), else 72 BPM; metre from the time
  signature meta (0x58), else 4/4. Kind is always `melody`, level 0,
  and the position is the lowest note in the file.
* A `.mid` is data, not authorship: a tempo outside 40–200 BPM is
  clamped into range, and a file that flattens to more than 256
  events keeps the first 256 with a warning. Its notes are still
  validated like a `.lesson`'s (an unplayable pitch is an error);
  its bars are not checked — the firmware's layout rounds a
  partial last bar up.

## Kinds

* `note-id`: `notes:` lists single target notes; the player finds each.
  Timing is not graded.
* `rhythm`: all notes share one pitch (use the position root); only
  timing is graded.
* `melody`: pitch + timing graded.
* `drill`: `notes:` may be empty — the drill generator
  (`drills.c`) builds the exercise from `level`, `position`,
  `tempo`, and a seed given on-device.
* `challenge`: like `melody`, one take at tempo, stars only.

## Songs

A book may carry **one** multi-part song, described by a `.song`
file (a second one is an error — the firmware has one song slot):

```
title:    Ode to Joy
melody:   l02-ode-to-joy.lesson   # the lesson that is the melody part
chart:    C | G | F | C           # one chord per bar of the melody
parts:    percussion bass harmony melody   # learning order
```

The melody lesson sets the song's tempo, metre and length, and
must fill whole bars. The other parts are generated at build time
from the chart; the firmware only plays what it is given:

| part | channel | generated as |
|---|---|---|
| `percussion` | 9 (GM drums) | a quarter-note pulse on every beat, written on the middle-C key (the drum pad) |
| `bass` | 1 | the chart's chord root on beats 1 and 3, rests between, written an octave above where it sounds |
| `harmony` | 2 | reserved — no events yet, by design |
| `melody` | 0 | the lesson's events, unchanged |

`chart:` is required when `bass` or `percussion` is among the
parts; only the chord roots are used (`C`, `F#`, `Bbm7` all parse —
the suffix is ignored). A book with no `.song` file is fine: it
gets the empty song (one reserved part, no events), which is all
the firmware needs to link and run.

## What the compiler checks

Each check guards a real firmware limit; a failure names the file
and the problem and stops the build.

* Note names and durations parse; durations are the integer tick
  values above (the chip has no FPU).
* Every `notes:` line adds up to the `time:` signature's bar.
* At most 256 events per lesson and per song part
  (`LESSON_MAX_EVENTS`: the fixed `lesson_t.events` array, which a
  song part is copied into at runtime).
* `tempo:` within 40–200 BPM (the device's own range); `level:`
  within 0–5.
* Every note playable: within MIDI 29–103 — the keys cover 53–79
  and the OCT± shift reaches two octaves either way. A note
  outside the staff's visible band (it would not draw on the
  sheet) is a warning, not an error: it still sounds.
* Titles and sources fit their fixed C arrays (48 / 96 bytes).
* Two lesson files must not sanitise to the same C symbol
  (`a-b.lesson` vs `a_b.lesson`).
* A song's chart has exactly one chord per melody bar, its parts
  are known and distinct, there is exactly one `melody` part, and
  every generated part spans the melody's length.

## Rules for content

* Only public-domain or original material. Cite the source in
  `source:` (composer + year, or "Traditional").
* Stay inside the position's five fingers unless the lesson is
  explicitly about OCT± (then note it in a comment).
* Integer durations only — the parser rejects anything else, because
  the chip has no FPU.
