# UI: 240×240 — the screen is sheet music (v0.1)

Diegetic UI: there are no menus. The device boots into a book of pieces;
the screen always shows notation, like sheet music on a piano's stand.

## The book
The selected piece's title sits at the top of the staff with its opening
notes laid out statically, like opening a book. Turn SELECT to flip through
the pieces (5 lessons + a drill); PLAY starts. HOME returns to the book.

## Playing: the scrolling staff
* A treble staff scrolls smoothly right-to-left under a fixed amber cursor
  line. Rendering: a 1-bit offscreen buffer (240×240×1 = 7.2 KB) shifts left
  in RAM each frame; only the newly exposed right columns are drawn from the
  piece data; the buffer is expanded to RGB565 and blitted at ~30 fps. No
  full-screen clear per frame, no flicker.
* Notation: whole/half/quarter/eighth notes (open heads, stems, flags,
  augmentation dots), whole/half/quarter rests, ledger lines. Quarter = 20 px.
* Your played notes are drawn on the staff at the cursor in green (correct
  pitch and timing) or red (wrong), so accuracy is visible where it happened.
* KNOB1 moves the cursor (30–180 px, default 60 — left gives more time to
  read ahead). KNOB2 sets scroll speed (0.5×–2×). Both persist in flash.
* The expected key's LED lights; notes sound with a piano-ish voice (one
  wavetable cycle: sine + 2nd + 3rd harmonic, exponential decay) and go out
  over USB MIDI.
* At the end of the piece a tally (score %) is drawn at the end of the staff
  with best-score persistence. PLAY replays; HOME returns to the book.

## Design notes
* One thing well: the scrolling staff. No deep menus because a real piano
  has none; piece selection is a single SELECT-knob flip.
* Smoothness comes from the shift-and-draw-column technique, not from
  redrawing the scene: the CPU only ever draws the new right-hand columns.
* Color is reserved for meaning: amber = cursor, green = you played it right,
  red = you didn't. The notation itself is ink-on-paper.
