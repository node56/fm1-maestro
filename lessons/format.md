# Lesson file format

Plain text, hand-editable, parsed at build time by
`tools/lessons2c.py` into `firmware/src/lessons_data.c`. One file per
lesson. There is no runtime lesson parser in the firmware.

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
duration (`R q`). `|` is a bar line — visual only, ignored by the
parser (but keep bars honest to `time:`; the host test warns on
mismatches).

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

## Rules for content

* Only public-domain or original material. Cite the source in
  `source:` (composer + year, or "Traditional").
* Stay inside the position's five fingers unless the lesson is
  explicitly about OCT± (then note it in a comment).
* Integer durations only — the parser rejects anything else, because
  the chip has no FPU.
