# Lesson design

Pedagogy follows the beginner-method arc (five-finger positions →
intervals → simple harmony), but every exercise is original or
public-domain. Nothing from any copyrighted method book ships here.

## Levels

| Level | Name | Skills |
|---|---|---|
| 0 | First notes | Find C; play quarter notes with a metronome; finger numbers 1–5 |
| 1 | Steps | C–D–E–F–G stepwise melodies; half/whole notes; rests |
| 2 | Skips | Thirds; 3/4 time; simple public-domain tunes (Ode to Joy, …) |
| 3 | New positions | G position; eighth notes; dotted rhythms |
| 4 | Two-part feel | Simple bass notes under the melody (octave-shift on 27 keys) |
| 5 | Fluent reader | Seeded sight-reading at tempo; dynamics (SMK-37 velocity) |

27 keys ≈ two octaves plus, with OCT±. Lessons stay inside one or two
five-finger positions; pieces that need more range use the octave keys
explicitly (the UI shows when).

## Lesson types

* **Note ID** — the screen names a note ("E"), you find and play it.
  Graded on correctness only. The Duolingo warm-up.
* **Rhythm** — clap/play a shown pattern on one key. Graded on timing
  only (any pitch counts).
* **Melody** — piano-roll play-along of a tune. Graded on pitch +
  timing. Missed notes loop the phrase until clean.
* **Drill (generated)** — seeded melody or rhythm pattern; infinite,
  adaptive (see `docs/CONTENT.md`). The daily practice core.
* **Challenge** — a generated piece at tempo, one take, stars.

## Grading (`grade.c`)

* Pitch: exact MIDI match required (beginner levels transpose the
  lesson to the player's chosen position instead of failing on octave).
* Timing: note-on within ±window of the expected tick; window shrinks
  with level (L0 ±160 ms → L5 ±70 ms).
* Score: 100 − penalties; 1–3 stars at 70 / 85 / 95. Streaks counted
  per lesson; best streak saved.

All integer math (ticks; quarter = 96). No floats anywhere in the
grading path — the AC791N has no FPU.

## Progression

Lessons unlock by stars (1 star opens the next). Drills unlock by
level and adapt: three consecutive ≥90% scores widen the generator's
range/rhythms/tempo; two consecutive <70% narrow them. Progress (stars,
best scores, drill parameters) persists in a flash blob with CRC.
