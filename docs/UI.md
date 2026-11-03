# UI: 240×240

The FM-1 screen is 240×240, text in two sizes, rectangles, lines,
pixels (platform `docs/API.md`). Design for glanceability at arm's
length; the player is looking at keys, not the screen.

## Screens

### Lesson select
Vertical list: level badge (L0–L5), title, stars earned (0–3).
ENC_KNOB1 or up/down scrolls; PLAY/ENTER opens. Completed lessons
show best score. "Daily drill" row on top (seed = date).

### Practice (piano roll)
* Top bar: lesson title, score, streak flame count.
* Main area: notes scroll right→left toward a fixed play line, like a
  piano roll rotated 90°. Each note is a bar whose length = duration.
* Bottom strip: a 27-key mini-map; the expected key is highlighted,
  pressed keys light up.
* Hit: key flashes green, +points, streak++. Miss (wrong key or
  outside the timing window): red flash, streak reset, phrase loops.
* Metronome: flashing border tick + optional click (levels 0–2).

Five-finger position is drawn as a shaded band; if the tune needs
OCT±, an arrow shows when to shift.

### Results
Big score %, stars (1–3), accuracy, best streak, "best" badge if new.
Buttons: Retry / Next lesson / Menu.

### Settings
Metronome on/off, timing strictness, left-hand mode (mirrors
positions), erase progress (with confirm).

## SMK-37
37 keys → wider mini-map, less scrolling. Velocity → dynamics lane:
notes show target velocity as bar brightness; grading adds a dynamics
score from L3. See `docs/SMK37.md`.
