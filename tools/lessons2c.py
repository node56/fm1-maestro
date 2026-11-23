#!/usr/bin/env python3
"""Convert a book directory into firmware/src/lessons_data.c.

Usage: python3 tools/lessons2c.py lessons/ -o firmware/src/lessons_data.c

A book directory holds lessons (lessons/*.lesson in the text format
of lessons/format.md, and/or *.mid Standard MIDI Files) and,
optionally, ONE song (a *.song file). Everything compiles through
one path: parse -> validate -> emit. There is no runtime parser in
the firmware, so this compiler is where the book is checked against
the firmware's limits; anything malformed or unusable fails here,
with the file and the problem named, instead of on the device.

The song is optional. A book with no .song file gets the empty
song: ode_song with a single reserved part and no events -- the
shape the firmware (which references ode_song unconditionally)
handles, and the one the host suites hand-write for song-less
builds. No stub with fake content is emitted any more.

A .mid file is parsed directly -- no re-encoding: its notes become
the lesson's events, its Set Tempo meta becomes the tempo, its
time-signature meta the metre, its track name the title.
"""
import re
import sys
from pathlib import Path

DURS = {"w": 384, "h": 192, "q": 96, "e": 48, "s": 24}
SEMI = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}
KINDS = {"note-id": "KIND_NOTE_ID", "rhythm": "KIND_RHYTHM",
         "melody": "KIND_MELODY", "drill": "KIND_DRILL",
         "challenge": "KIND_CHALLENGE"}
NOTE_RE = re.compile(r"^([A-Ga-g])([#bB]?)(\d)$")
DUR_RE = re.compile(r"^([whqes])(\.?)$")
CHORD_RE = re.compile(r"^([A-Ga-g])([#b]?)([A-Za-z0-9]*)$")

# ---- the firmware's limits (the reason each validation exists) ----
MAX_EVENTS = 256        # lesson.h LESSON_MAX_EVENTS: lesson_t.events[]
                        # is a fixed array, and maestro.c's
                        # load_song_part copies a song part's events
                        # into one with no bounds check -- so song
                        # parts are held to the same cap.
TITLE_BYTES = 48        # lesson.h LESSON_TITLE_LEN (room for the NUL)
SOURCE_BYTES = 96       # lesson.h LESSON_SOURCE_LEN
TEMPO_MIN = 40          # maestro.c TEMPO_MIN / TEMPO_MAX: the range
TEMPO_MAX = 200         # the device itself can set.
LEVEL_MAX = 5           # lesson.h: level is 0..5.
KEY_MIN, KEY_MAX = 53, 79   # the FM-1's 27 keys (key bit k = MIDI 53+k)
OCT_RANGE = 24          # the OCT+- shift reaches +-2 octaves, so a
PLAY_MIN = KEY_MIN - OCT_RANGE    # written pitch is playable iff it
PLAY_MAX = KEY_MAX + OCT_RANGE    # lies in 29..103.
SONG_MAX_PARTS = 4      # lesson.h SONG_MAX_PARTS
SONG_CHANNELS = (0, 1, 2, 9)      # the take's part channels (lesson.h)
# A guide note draws on the sheet iff its notehead's staff y (ui.c
# ui_y_of, treble clef) falls inside the scroll region (ui.h:
# SCROLL_TOP 48, SCROLL_H 156 -> 48..204). Outside it the note still
# sounds when played, but the player cannot see it coming.
SCROLL_MIN, SCROLL_MAX = 48, 204
_DIA = (0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6)   # ui.c ui_dia table


class CompileError(Exception):
    """A problem in the book, reported with its file and cause."""


def staff_y(midi):
    """ui.c's ui_dia + ui_y_of (treble clef), in Python."""
    letter = _DIA[midi % 12]
    if letter < 0:
        letter = _DIA[(midi - 1) % 12]
    dia = (midi // 12) * 7 + letter
    return 172 - (dia - 37) * 7   # STAFF_BOT - (dia - clef_base) * SPC/2


def note_name(midi):
    names = ("C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B")
    return f"{names[midi % 12]}{midi // 12}"   # C5 == MIDI 60


def note_to_midi(tok):
    m = NOTE_RE.match(tok)
    if not m:
        raise ValueError(f"bad note token: {tok!r}")
    letter, acc, octv = m.groups()
    s = SEMI[letter.upper()] + (1 if acc == "#" else -1 if acc else 0)
    midi = int(octv) * 12 + s
    if not 0 <= midi <= 127:
        raise ValueError(f"note out of range: {tok!r}")
    return midi


def dur_to_ticks(tok):
    m = DUR_RE.match(tok)
    if not m:
        raise ValueError(f"bad duration token: {tok!r}")
    t = DURS[m.group(1)]
    return t + t // 2 if m.group(2) else t


# ---- lessons ---------------------------------------------------------------

class Lesson:
    def __init__(self, path, headers, events, bars):
        self.path = path            # Path of the source file
        self.headers = headers      # raw header strings, as written
        self.events = events        # [(midi, ticks, is_rest)]
        self.bars = bars            # ticks per bar, if | was used, else []
        self.ident = c_ident(path)
        # filled in by validate_lesson:
        self.position_midi = None
        self.beats = None           # time-signature numerator (int)
        self.bar_ticks = None


def c_ident(path):
    return "lesson_" + re.sub(r"\W+", "_", path.stem)


def parse_file(path):
    headers, events, bars = {}, [], []
    in_notes = False
    for raw in path.read_text().splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if not in_notes:
            if line == "notes:":
                in_notes = True
                continue
            k, _, v = line.partition(":")
            headers[k.strip()] = v.strip()
            continue
        # One line of the notes section is one bar (format.md);
        # "|" separates notes within the bar and is ignored here,
        # but the line's ticks are kept for the bar check.
        toks = [t for t in re.split(r"\s+", line) if t != "|"]
        if len(toks) % 2:
            raise CompileError(f"{path}: dangling token in {line!r}")
        line_ticks = 0
        for ntok, dtok in zip(toks[::2], toks[1::2]):
            try:
                ticks = dur_to_ticks(dtok)
                ev = (0, ticks, 1) if ntok.upper() == "R" \
                    else (note_to_midi(ntok), ticks, 0)
            except ValueError as e:
                raise CompileError(f"{path}: {e}") from e
            events.append(ev)
            line_ticks += ticks
        if toks:
            bars.append(line_ticks)
    for req in ("title", "kind", "level", "position", "tempo", "time"):
        if req not in headers:
            raise CompileError(f"{path}: missing header {req!r}")
    if headers["kind"] not in KINDS:
        raise CompileError(f"{path}: bad kind {headers['kind']!r}")
    return Lesson(path, headers, events, bars)


# ---- Standard MIDI Files ---------------------------------------------------
# A .mid lesson is read straight from the file: no intermediate
# format, no re-encoding. Notes are Note On / Note Off pairs (a Note
# On with velocity 0 is a Note Off); the lesson is a single melodic
# line, so overlapping notes and chords are flattened -- at each
# onset the highest pitch wins, and a note that starts while another
# is still sounding is skipped. Gaps between notes become rests.
# Ticks are rescaled to the firmware's PPQ of 96.

def _vlq(data, pos):
    v = 0
    while True:
        if pos >= len(data):
            raise ValueError("truncated VLQ")
        b = data[pos]
        pos += 1
        v = (v << 7) | (b & 0x7F)
        if not b & 0x80:
            return v, pos


def _parse_mid_track(data, ppq, notes, meta):
    pos, tick, running = 0, 0, 0
    open_notes = {}   # pitch -> start tick (rescaled)
    while pos < len(data):
        delta, pos = _vlq(data, pos)
        tick += delta
        if pos >= len(data):
            raise ValueError("truncated track")
        st = data[pos]
        if st < 0x80:
            if not running:
                raise ValueError("running status without a status byte")
            st = running
        else:
            pos += 1
            if st < 0xF0:
                running = st
        if st == 0xFF:   # meta event
            mtype = data[pos]
            pos += 1
            mlen, pos = _vlq(data, pos)
            payload = data[pos:pos + mlen]
            if len(payload) != mlen:
                raise ValueError("truncated meta event")
            pos += mlen
            running = 0
            if mtype == 0x03 and "title" not in meta:
                meta["title"] = payload.decode("utf-8", "replace")
            elif mtype == 0x51 and len(payload) == 3:
                us = int.from_bytes(payload, "big")
                if us:
                    meta["tempo"] = max(40, min(200, round(60_000_000 / us)))
            elif mtype == 0x58 and len(payload) >= 1:
                meta["beats"] = payload[0]
        elif st in (0xF0, 0xF7):   # SysEx: skipped
            mlen, pos = _vlq(data, pos)
            pos += mlen
            running = 0
        elif st >= 0xF0:   # system common / realtime: no data
            running = 0
        else:
            hi = st & 0xF0
            ndata = 1 if hi in (0xC0, 0xD0) else 2
            if pos + ndata > len(data):
                raise ValueError("truncated channel event")
            d1 = data[pos]
            d2 = data[pos + 1] if ndata == 2 else 0
            pos += ndata
            if hi in (0x90, 0x80):
                t96 = (tick * 96 + ppq // 2) // ppq
                if hi == 0x90 and d2:
                    open_notes.setdefault(d1 & 0x7F, t96)
                elif (d1 & 0x7F) in open_notes:
                    notes.append((open_notes.pop(d1 & 0x7F), t96, d1 & 0x7F))
    return notes, meta


def parse_mid_file(path):
    try:
        data = path.read_bytes()
        if len(data) < 14 or data[:4] != b"MThd" or int.from_bytes(data[4:8], "big") != 6:
            raise ValueError(f"{path}: not a Standard MIDI File")
        fmt = int.from_bytes(data[8:10], "big")
        ntrk = int.from_bytes(data[10:12], "big")
        ppq = int.from_bytes(data[12:14], "big")
        if fmt > 1 or not ntrk or ppq & 0x8000:
            raise ValueError(f"{path}: unsupported SMF (format {fmt}, PPQ {ppq})")
        notes, meta = [], {}
        pos = 14
        for _ in range(ntrk):
            if pos + 8 > len(data) or data[pos:pos + 4] != b"MTrk":
                raise ValueError(f"{path}: bad track chunk")
            tlen = int.from_bytes(data[pos + 4:pos + 8], "big")
            pos += 8
            if pos + tlen > len(data):
                raise ValueError(f"{path}: truncated track chunk")
            _parse_mid_track(data[pos:pos + tlen], ppq, notes, meta)
            pos += tlen
    except ValueError as e:
        raise CompileError(str(e)) from e
    if not notes:
        raise CompileError(f"{path}: no notes found")
    # Flatten to one melodic line, in time order. At each onset the
    # highest pitch wins; a note starting while another sounds is
    # skipped; gaps become rests.
    notes.sort(key=lambda n: (n[0], -n[2]))
    events, cur = [], 0
    for start, end, pitch in notes:
        if start < cur:
            continue
        if start > cur:
            events.append((0, start - cur, 1))
        events.append((pitch, max(end - start, 1), 0))
        cur = end
    if len(events) > MAX_EVENTS:
        # A dropped-in .mid is data, not authorship: keep the head
        # and say so, rather than failing the whole book build.
        print(f"{path}: warning: {len(events)} events, keeping the first "
              f"{MAX_EVENTS}", file=sys.stderr)
        events = events[:MAX_EVENTS]
    title = meta.get("title") or re.sub(r"[-_]+", " ", path.stem).title()
    headers = {
        "title": title,
        "kind": "melody",
        "source": f"Standard MIDI File: {path.name}",
        "level": "0",
        "position": str(min(p for _, _, p in notes)),
        "tempo": str(meta.get("tempo", 72)),
        "time": f"{meta.get('beats', 4)}/4",
    }
    return Lesson(path, headers, events, [])


def validate_lesson(les, warn):
    """Check one parsed lesson against the firmware's limits."""
    h, path = les.headers, les.path

    def header_int(name):
        try:
            return int(h[name])
        except ValueError:
            raise CompileError(
                f"{path}: header {name!r} is not an integer: {h[name]!r}")

    level = header_int("level")
    if not 0 <= level <= LEVEL_MAX:
        raise CompileError(f"{path}: level {level} outside 0..{LEVEL_MAX}")
    tempo = header_int("tempo")
    if not TEMPO_MIN <= tempo <= TEMPO_MAX:
        raise CompileError(
            f"{path}: tempo {tempo} outside the device's "
            f"{TEMPO_MIN}..{TEMPO_MAX} BPM range")
    beats_txt = h["time"].split("/")[0]
    try:
        beats = int(beats_txt)
    except ValueError:
        raise CompileError(f"{path}: bad time signature {h['time']!r}")
    if not 1 <= beats <= 255:
        raise CompileError(f"{path}: bad time signature {h['time']!r}")
    les.beats = beats
    les.bar_ticks = beats * 96

    # a .mid lesson's position is already a MIDI number; a .lesson
    # file's is a note name (C5).
    if h["position"].isdigit():
        les.position_midi = int(h["position"])
        if not 0 <= les.position_midi <= 127:
            raise CompileError(f"{path}: position out of range: {h['position']!r}")
    else:
        try:
            les.position_midi = note_to_midi(h["position"])
        except ValueError as e:
            raise CompileError(f"{path}: {e}") from e

    if len(h["title"].encode()) >= TITLE_BYTES:
        raise CompileError(
            f"{path}: title is {len(h['title'].encode())} bytes; "
            f"lesson_t.title holds {TITLE_BYTES} including the NUL")
    if len(h.get("source", "").encode()) >= SOURCE_BYTES:
        raise CompileError(
            f"{path}: source is {len(h.get('source', '').encode())} bytes; "
            f"lesson_t.source holds {SOURCE_BYTES} including the NUL")

    if len(les.events) > MAX_EVENTS:
        raise CompileError(
            f"{path}: {len(les.events)} events exceeds LESSON_MAX_EVENTS "
            f"({MAX_EVENTS}); lesson_t.events is a fixed array")

    for midi, _ticks, is_rest in les.events:
        if is_rest:
            continue
        if not PLAY_MIN <= midi <= PLAY_MAX:
            raise CompileError(
                f"{path}: note {note_name(midi)} (MIDI {midi}) can never "
                f"be played: the keys cover {KEY_MIN}..{KEY_MAX} and the "
                f"octave shift reaches {PLAY_MIN}..{PLAY_MAX}")
        y = staff_y(midi)
        if not SCROLL_MIN <= y <= SCROLL_MAX:
            warn(f"{path}: note {note_name(midi)} (MIDI {midi}) draws at "
                 f"staff y {y}, outside the sheet's {SCROLL_MIN}.."
                 f"{SCROLL_MAX} band -- it will sound, but the player "
                 f"cannot see it on the staff")

    # Bar honesty: one line of the notes section is one bar, and
    # every line must add up to the declared time signature
    # (lessons/format.md). (A .mid has no lines; its bars are
    # whatever the file's notes make of them, and the firmware's
    # layout rounds a partial last bar up, so .mid lessons skip
    # this check.)
    for i, ticks in enumerate(les.bars):
        if ticks != les.bar_ticks:
            raise CompileError(
                f"{path}: bar {i + 1} has {ticks} ticks, but "
                f"{h['time']} needs {les.bar_ticks}")


# ---- the song ---------------------------------------------------------------
# A book may carry one multi-part song, described by a .song file:
#
#   title:    Ode to Joy
#   melody:   l02-ode-to-joy.lesson   (the lesson that is the melody)
#   chart:    C | G | F | C           (one chord per bar)
#   parts:    percussion bass harmony melody   (learning order)
#
# The parts are generated here, at build time, from the chart; the
# firmware stays a player and nothing is generated at runtime.
#   percussion (channel 9, the GM drum channel): a quarter-note
#     pulse on every beat, written on the middle-C key -- the
#     drum pad. The channel sounds at one fixed low pitch.
#   bass (channel 1): the chord root on beats 1 and 3, rests
#     between, written at a readable octave (root + 60). The bass
#     channel is a transposing instrument: it sounds an octave
#     below written, like a double bass.
#   harmony (channel 2): reserved -- no events yet, by design.
#   melody (channel 0): the lesson's events, unchanged.

PART_RECIPES = {   # name -> (channel, display name)
    "percussion": (9, "Percussion"),
    "bass": (1, "Bass"),
    "harmony": (2, "Harmony"),
    "melody": (0, "Melody"),
}


class SongPart:
    def __init__(self, key, channel, name, events):
        self.key, self.channel, self.name, self.events = key, channel, name, events


class Song:
    def __init__(self, path, title, tempo, beats, parts):
        self.path, self.title = path, title
        self.tempo, self.beats, self.parts = tempo, beats, parts


def chord_root(tok, path):
    m = CHORD_RE.match(tok)
    if not m:
        raise CompileError(f"{path}: bad chord {tok!r} in chart")
    letter, acc, _suffix = m.groups()
    return SEMI[letter.upper()] + (1 if acc == "#" else -1 if acc == "b" else 0)


def parse_song(path, lessons_by_stem):
    headers = {}
    for raw in path.read_text().splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        k, _, v = line.partition(":")
        headers[k.strip()] = v.strip()
    for req in ("title", "melody", "parts"):
        if req not in headers:
            raise CompileError(f"{path}: missing header {req!r}")
    title = headers["title"]
    if len(title.encode()) >= TITLE_BYTES:
        raise CompileError(
            f"{path}: title is {len(title.encode())} bytes; "
            f"song_t.title holds {TITLE_BYTES} including the NUL")

    mel_ref = headers["melody"]
    mel_stem = mel_ref[:-len(".lesson")] if mel_ref.endswith(".lesson") else mel_ref
    melody = lessons_by_stem.get(mel_stem)
    if melody is None:
        raise CompileError(
            f"{path}: melody lesson {mel_ref!r} not found in this book")

    keys = headers["parts"].split()
    for k in keys:
        if k not in PART_RECIPES:
            raise CompileError(
                f"{path}: unknown part {k!r} (known: "
                f"{', '.join(sorted(PART_RECIPES))})")
    if len(set(keys)) != len(keys):
        raise CompileError(f"{path}: duplicate part in {headers['parts']!r}")
    if keys.count("melody") != 1:
        raise CompileError(
            f"{path}: a song needs exactly one melody part, "
            f"got {headers['parts']!r}")
    if len(keys) > SONG_MAX_PARTS:
        raise CompileError(
            f"{path}: {len(keys)} parts exceeds SONG_MAX_PARTS "
            f"({SONG_MAX_PARTS})")

    beats = melody.beats
    bar_ticks = melody.bar_ticks
    total = sum(t for _, t, _ in melody.events)
    if total % bar_ticks:
        raise CompileError(
            f"{path}: the melody ({melody.path.name}) does not fill "
            f"whole bars ({total} ticks is not a multiple of {bar_ticks})")
    nbars = total // bar_ticks

    chart = None
    if "bass" in keys or "percussion" in keys:
        if "chart" not in headers:
            raise CompileError(
                f"{path}: parts {headers['parts']!r} are generated from "
                f"a chart, but no chart header was given")
        chart = [t.strip() for t in headers["chart"].split("|") if t.strip()]
        if len(chart) != nbars:
            raise CompileError(
                f"{path}: chart has {len(chart)} bars, the melody "
                f"has {nbars}")
        roots = [chord_root(tok, path) for tok in chart]

    parts = []
    for key in keys:
        channel, name = PART_RECIPES[key]
        if channel not in SONG_CHANNELS:   # the recipe table's contract
            raise CompileError(f"{path}: part {key!r} channel {channel} "
                               f"is not one of {SONG_CHANNELS}")
        if key == "percussion":
            events = [(60, 96, 0)] * (nbars * beats)
        elif key == "bass":
            events = []
            for bar in range(nbars):
                root = 60 + roots[bar]
                for beat in range(beats):
                    events.append((root, 96, 0) if beat in (0, 2)
                                  else (0, 96, 1))
        elif key == "harmony":
            events = []
        else:   # melody
            events = list(melody.events)
        if len(events) > MAX_EVENTS:
            raise CompileError(
                f"{path}: part {name!r} has {len(events)} events, over "
                f"the {MAX_EVENTS} a lesson_t can hold (load_song_part "
                f"copies a part into one, unchecked)")
        if events and sum(t for _, t, _ in events) != total:
            raise CompileError(
                f"{path}: part {name!r} spans "
                f"{sum(t for _, t, _ in events)} ticks, the melody "
                f"spans {total} -- the parts must share one bar grid")
        parts.append(SongPart(key, channel, name, events))
    return Song(path, title, melody.headers["tempo"], beats, parts)


# ---- emission ----------------------------------------------------------------
# The text below is the generated file's contract: for a book that
# compiled before this refactor, the output is byte-identical.

def ev_list(events):
    return ",\n        ".join(f"{{ {m}, {t}, {r} }}" for m, t, r in events)


def emit_lesson(les):
    h = les.headers
    return (
        f"static const lesson_t {les.ident} = {{\n"
        f'    "{h["title"]}",\n'
        f'    "{h.get("source", "")}",\n'
        f"    {KINDS[h['kind']]},\n"
        f"    {h['level']}, {les.position_midi}, {h['tempo']}, "
        f"{h['time'].split('/')[0]},\n"
        f"    {len(les.events)},\n"
        f"    {{\n        {ev_list(les.events)}\n    }}\n"
        f"}};\n")


def emit_song(song):
    ident = "song_" + re.sub(r"\W+", "_", song.title.strip().lower())
    chunks = []
    for p in song.parts:
        if not p.events:
            continue
        chunks.append(
            f"static const lesson_event_t {ident}_{p.key}_events"
            f"[{len(p.events)}] = {{\n"
            f"        {ev_list(p.events)}\n    }};\n")
    lines = ["const song_t ode_song = {",
             f'    "{song.title}",',
             f"    {song.tempo}, {song.beats}, {len(song.parts)},",
             "    {"]
    for p in song.parts:
        ref = f"{ident}_{p.key}_events" if p.events else "0"
        lines.append(f'        {{ {p.channel}, "{p.name}", '
                     f'{len(p.events)}, {ref} }},')
    lines.append("    }")
    lines.append("};")
    chunks.append("\n".join(lines) + "\n")
    return chunks


def emit_empty_song():
    # The firmware references ode_song unconditionally (its tempo
    # seeds the song sheet at boot), and an event-less part is the
    # shape it already handles: the part exists, its channel is
    # reserved, and there is nothing to learn yet (lesson.h). This
    # is the same placeholder the host suites hand-write for
    # song-less builds.
    return ("/* no song: this book has no .song file, so ode_song is\n"
            " * the empty song -- one reserved part, no events. */\n"
            "const song_t ode_song = {\n"
            '    "Song",\n'
            "    96, 4, 1,\n"
            "    {\n"
            '        { 9, "Percussion", 0, 0 },\n'
            "    }\n"
            "};\n")


def compile_book(book_dir, warn):
    book = Path(book_dir)
    files = sorted(book.glob("*.lesson")) + sorted(book.glob("*.mid"))
    if not files:
        sys.exit("no .lesson or .mid files found")
    lessons = []
    for f in files:
        les = parse_mid_file(f) if f.suffix == ".mid" else parse_file(f)
        validate_lesson(les, warn)
        lessons.append(les)
    seen = {}
    for les in lessons:
        if les.ident in seen:
            raise CompileError(
                f"{les.path}: lesson symbol {les.ident} also comes from "
                f"{seen[les.ident]} -- rename one of the files")
        seen[les.ident] = les.path

    song_files = sorted(book.glob("*.song"))
    if len(song_files) > 1:
        raise CompileError(
            f"{book}: {len(song_files)} .song files "
            f"({', '.join(f.name for f in song_files)}); a book has "
            f"one song slot")
    song = None
    if song_files:
        by_stem = {f.stem: les for f, les in zip(files, lessons)}
        song = parse_song(song_files[0], by_stem)
    return lessons, song


def main():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("dir", help="lessons/ directory")
    ap.add_argument("-o", "--out", required=True)
    args = ap.parse_args()

    def warn(msg):
        print(f"lessons2c: warning: {msg}", file=sys.stderr)

    try:
        lessons, song = compile_book(args.dir, warn)
    except CompileError as e:
        sys.exit(f"lessons2c: {e}")

    chunks = ["/* Generated by tools/lessons2c.py — do not edit. */",
              '#include "lesson.h"', ""]
    for les in lessons:
        chunks.append(emit_lesson(les))
    chunks.append("const lesson_t *all_lessons[] = {")
    chunks.extend(f"    &{les.ident}," for les in lessons)
    chunks.append("};")
    chunks.append(f"const uint8_t all_lessons_count = {len(lessons)};")
    if song is not None:
        chunks.extend(emit_song(song))
    else:
        chunks.append(emit_empty_song())
    chunks.append("")
    Path(args.out).write_text("\n".join(chunks))
    print(f"wrote {args.out}: {len(lessons)} lessons"
          + (f", song '{song.title}'" if song else ", no song"))


if __name__ == "__main__":
    main()
