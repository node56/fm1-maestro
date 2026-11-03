#include "lesson.h"

#include <string.h>
#include <ctype.h>
#include <stdlib.h>

static int semis_of(char letter) {
    switch (toupper((unsigned char)letter)) {
    case 'C': return 0;
    case 'D': return 2;
    case 'E': return 4;
    case 'F': return 5;
    case 'G': return 7;
    case 'A': return 9;
    case 'B': return 11;
    default:  return -1;
    }
}

/* "C5" -> 60, "F#4" -> 54, "Bb3" -> 46. Convention: C5 == MIDI 60. */
static int parse_note_token(const char *tok, uint8_t *midi_out) {
    size_t n = strlen(tok);
    int s, oct, midi;
    size_t i;
    if (n < 2 || n > 3) return -1;
    s = semis_of(tok[0]);
    if (s < 0) return -1;
    i = 1;
    if (tok[i] == '#') { s += 1; i++; }
    else if (tok[i] == 'b' || tok[i] == 'B') { s -= 1; i++; }
    if (i + 1 != n || !isdigit((unsigned char)tok[i])) return -1;
    oct = tok[i] - '0';
    midi = oct * 12 + s;
    if (midi < 0 || midi > 127) return -1;
    *midi_out = (uint8_t)midi;
    return 0;
}

/* "q" -> 96, "h." -> 288. */
static int parse_dur_token(const char *tok, uint16_t *ticks_out) {
    size_t n = strlen(tok);
    uint16_t t;
    if (n < 1 || n > 2) return -1;
    switch (tok[0]) {
    case 'w': t = 384; break;
    case 'h': t = 192; break;
    case 'q': t = 96;  break;
    case 'e': t = 48;  break;
    case 's': t = 24;  break;
    default:  return -1;
    }
    if (n == 2) {
        if (tok[1] != '.') return -1;
        t = (uint16_t)(t + t / 2);
    }
    *ticks_out = t;
    return 0;
}

static void copy_trimmed(char *dst, size_t cap, const char *src) {
    size_t n;
    while (*src == ' ' || *src == '\t') src++;
    n = strlen(src);
    while (n > 0 && (src[n - 1] == ' ' || src[n - 1] == '\t' ||
                     src[n - 1] == '\r')) n--;
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static lesson_kind_t kind_of(const char *s) {
    if (strcmp(s, "note-id") == 0)   return KIND_NOTE_ID;
    if (strcmp(s, "rhythm") == 0)    return KIND_RHYTHM;
    if (strcmp(s, "melody") == 0)    return KIND_MELODY;
    if (strcmp(s, "drill") == 0)     return KIND_DRILL;
    if (strcmp(s, "challenge") == 0) return KIND_CHALLENGE;
    return KIND_UNKNOWN;
}

int lesson_parse(const char *text, lesson_t *out) {
    const char *p = text;
    char line[256];
    char val[128];
    int in_notes = 0;

    memset(out, 0, sizeof(*out));
    out->kind = KIND_UNKNOWN;

    while (*p) {
        /* extract one line */
        size_t i = 0;
        char *s, *colon;
        while (*p && *p != '\n' && i < sizeof(line) - 1) line[i++] = *p++;
        if (*p == '\n') p++;
        line[i] = '\0';

        s = line;
        while (*s == ' ' || *s == '\t' || *s == '\r') s++;
        if (*s == '\0' || *s == '#') continue;

        if (!in_notes) {
            if (strncmp(s, "notes:", 6) == 0) { in_notes = 1; continue; }
            colon = strchr(s, ':');
            if (!colon) return -1;
            *colon = '\0';
            copy_trimmed(val, sizeof(val), colon + 1);
            if (strcmp(s, "title") == 0)
                copy_trimmed(out->title, sizeof(out->title), val);
            else if (strcmp(s, "source") == 0)
                copy_trimmed(out->source, sizeof(out->source), val);
            else if (strcmp(s, "kind") == 0)
                out->kind = kind_of(val);
            else if (strcmp(s, "level") == 0)
                out->level = (uint8_t)atoi(val);
            else if (strcmp(s, "position") == 0) {
                if (parse_note_token(val, &out->position_midi)) return -3;
            } else if (strcmp(s, "tempo") == 0)
                out->tempo = (uint16_t)atoi(val);
            else if (strcmp(s, "time") == 0)
                out->beats_per_bar = (uint8_t)atoi(val);
            /* "hand:" validated by convention only; ignored. */
            continue;
        }

        /* note section: whitespace-separated tokens, pairs of note+dur */
        {
            char *tok;
            int expect_note = 1;   /* 1 = note token, 0 = duration token */
            uint8_t midi = 0;
            uint8_t is_rest = 0;

            tok = s;
            while (*tok) {
                char *end;
                char saved;
                while (*tok == ' ' || *tok == '\t' || *tok == '\r') tok++;
                if (*tok == '\0') break;
                end = tok;
                while (*end && *end != ' ' && *end != '\t' && *end != '\r')
                    end++;
                saved = *end;
                *end = '\0';

                if (strcmp(tok, "|") != 0) {
                    if (expect_note) {
                        if (tok[0] == 'R' || tok[0] == 'r') {
                            is_rest = 1;
                        } else {
                            if (parse_note_token(tok, &midi)) return -3;
                            is_rest = 0;
                        }
                        expect_note = 0;
                    } else {
                        uint16_t ticks;
                        lesson_event_t *ev;
                        if (parse_dur_token(tok, &ticks)) return -4;
                        if (out->event_count >= LESSON_MAX_EVENTS) return -2;
                        ev = &out->events[out->event_count++];
                        ev->midi = midi;
                        ev->ticks = ticks;
                        ev->is_rest = is_rest;
                        expect_note = 1;
                    }
                }
                *end = saved;
                tok = (*end == '\0') ? end : end + 1;
            }
            if (!expect_note) return -4; /* dangling note without duration */
        }
    }

    if (out->kind == KIND_UNKNOWN || out->tempo == 0 ||
        out->beats_per_bar == 0 || out->event_count == 0)
        return -5;
    return 0;
}

/* ---------------- practice engine ---------------- */

void practice_start(practice_t *p, const lesson_t *lesson) {
    p->lesson = lesson;
    p->idx = 0;
    p->elapsed = 0;
    p->now = 0;
}

void practice_set_now(practice_t *p, uint32_t now) {
    p->now = now;
}

int practice_done(const practice_t *p) {
    return p->idx >= p->lesson->event_count;
}

uint32_t practice_event_start(const practice_t *p) {
    return p->elapsed;
}

const lesson_event_t *practice_current(practice_t *p) {
    /* Skip rests whose time has fully passed. */
    while (!practice_done(p)) {
        const lesson_event_t *ev = &p->lesson->events[p->idx];
        if (!ev->is_rest) break;
        if (p->elapsed + ev->ticks > p->now) break;
        p->elapsed += ev->ticks;
        p->idx++;
    }
    if (practice_done(p)) return NULL;
    return &p->lesson->events[p->idx];
}

int practice_play(practice_t *p, uint8_t midi) {
    const lesson_event_t *ev = practice_current(p);
    if (!ev) return -1;
    if (p->lesson->kind == KIND_RHYTHM || ev->is_rest) {
        p->elapsed += ev->ticks;
        p->idx++;
        return 1;
    }
    if (midi == ev->midi) {
        p->elapsed += ev->ticks;
        p->idx++;
        return 1;
    }
    return 0;
}
