#!/usr/bin/env python3
"""Convert lessons/*.lesson and lessons/*.mid into firmware/src/lessons_data.c.

Usage: python3 tools/lessons2c.py lessons/ -o firmware/src/lessons_data.c

Implements the format in lessons/format.md (same C5==60 convention,
same tick values). A .mid file (Standard MIDI File, format 0 or 1) is
parsed directly -- no re-encoding: its notes become the lesson's
events, its Set Tempo meta becomes the tempo, its time-signature
meta the metre, its track name the title. Fails loudly on anything
malformed.
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


def parse_file(path):
    headers, events = {}, []
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
        toks = [t for t in re.split(r"\s+", line) if t != "|"]
        if len(toks) % 2:
            raise ValueError(f"{path}: dangling token in {line!r}")
        for ntok, dtok in zip(toks[::2], toks[1::2]):
            ticks = dur_to_ticks(dtok)
            if ntok.upper() == "R":
                events.append((0, ticks, 1))
            else:
                events.append((note_to_midi(ntok), ticks, 0))
    for req in ("title", "kind", "level", "position", "tempo", "time"):
        if req not in headers:
            raise ValueError(f"{path}: missing header {req!r}")
    if headers["kind"] not in KINDS:
        raise ValueError(f"{path}: bad kind {headers['kind']!r}")
    return headers, events


def c_ident(path):
    return "lesson_" + re.sub(r"\W+", "_", path.stem)


# ---- Standard MIDI Files -----------------------------------------------------
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
    if not notes:
        raise ValueError(f"{path}: no notes found")
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
    if len(events) > 256:
        print(f"{path}: warning: {len(events)} events, keeping the first 256",
              file=sys.stderr)
        events = events[:256]
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
    return headers, events


def main():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("dir", help="lessons/ directory")
    ap.add_argument("-o", "--out", required=True)
    args = ap.parse_args()

    files = sorted(Path(args.dir).glob("*.lesson")) + sorted(Path(args.dir).glob("*.mid"))
    if not files:
        sys.exit("no .lesson or .mid files found")

    chunks = ["/* Generated by tools/lessons2c.py — do not edit. */",
              '#include "lesson.h"', ""]
    idents = []
    for f in files:
        headers, events = parse_mid_file(f) if f.suffix == ".mid" else parse_file(f)
        ident = c_ident(f)
        idents.append(ident)
        # a .mid lesson's position is already a MIDI number; a
        # .lesson file's is a note name (C5).
        pos = headers["position"] if headers["position"].isdigit() else note_to_midi(headers["position"])
        beats = headers["time"].split("/")[0]
        ev = ",\n        ".join(f"{{ {m}, {t}, {r} }}" for m, t, r in events)
        chunks.append(
            f"static const lesson_t {ident} = {{\n"
            f'    "{headers["title"]}",\n'
            f'    "{headers.get("source", "")}",\n'
            f"    {KINDS[headers['kind']]},\n"
            f"    {headers['level']}, {pos}, {headers['tempo']}, {beats},\n"
            f"    {len(events)},\n"
            f"    {{\n        {ev}\n    }}\n"
            f"}};\n")
    chunks.append("const lesson_t *all_lessons[] = {")
    chunks.extend(f"    &{i}," for i in idents)
    chunks.append("};")
    chunks.append(f"const uint8_t all_lessons_count = {len(idents)};")
    chunks.append("")
    Path(args.out).write_text("\n".join(chunks))
    print(f"wrote {args.out}: {len(idents)} lessons")


if __name__ == "__main__":
    main()
