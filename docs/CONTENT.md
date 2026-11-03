# Content plan: public domain + generated

No copyrighted lesson content ships in this repo. Two tracks:

## 1. Public-domain tunes

Criteria: melody in the public domain (composer dead 100+ years, or
traditional), playable in a five-finger position, recognizable enough
to be rewarding. Each ships as a `lessons/*.lesson` file (see
`lessons/format.md`); sources are cited in the file header.

Shipped in v0:

| Lesson | Tune | Source |
|---|---|---|
| l02 | Ode to Joy | Beethoven, Sym. 9 (1824) |
| l03 | Twinkle, Twinkle, Little Star | French traditional, 18th c. |
| l04 | Lightly Row | German folk |
| l05 | Aura Lee | G. R. Poulton (1861) |
| l06 | Go Tell Aunt Rhody | American folk |

Candidates for later: "Merrily We Roll Along", "Good King Wenceslas",
"Jingle Bells" (1857 — public domain), "Ode" variants, Bach's
"Minuet in G" (BWV Anh. 114), "Jesu, Joy of Man's Desiring" (melody
only, simplified).

Rule: when in doubt about a tune's status, leave it out. Folk
attribution must be genuinely traditional, not a modern arrangement.

## 2. Generative drills (`drills.c`)

The differentiator: infinite, adaptive, deterministic exercises.

* **PRNG:** xorshift32, seeded per drill. Same seed → same drill
  (shareable, repeatable, testable on host).
* **Melody generator constraints:**
  - stays inside the level's five-finger position (e.g. C–D–E–F–G)
  - stepwise motion (±1) with probability ~0.7; skips ≤ a third
    with probability ~0.25; repeats ~0.05
  - rhythmic vocabulary per level: L0 {q}, L1 {q,h}, L2 {q,h,e},
    L3 {q,h,e, dotted-q}, L4+ {…, s}
  - 4–8 bars in 4/4 or 3/4; final bar resolves to the tonic
* **Rhythm generator:** patterns on one pitch from the level's
  vocabulary, with rests from L1.
* **Adaptation:** drill parameters (range, vocabulary, tempo 60–132)
  widen on three consecutive ≥90% scores, narrow on two <70%.
  Parameters persist per level in flash.

Because drills are generated, the firmware never stores a large
corpus: a few KB of code replaces megabytes of content — which
matters on a chip where flash is precious.

## Inspiration (open / public-domain sources)

* Public-domain sheet-music collections (e.g. the Mutopia Project
  model: public-domain scores in machine-readable form) for tune
  selection and phrasing reference.
* ABC-notation folk corpora for studying how traditional melodies are
  structured (we re-encode chosen tunes by hand into our own format —
  no bulk copying).
* Open-source training software (e.g. PianoBooster-style MIDI
  play-along) for grading and UI ideas — ideas only, no code reuse
  beyond license-compatible parts, all credited in `LICENSING.md`.
