#ifndef MAESTRO_PLATFORM_API_H
#define MAESTRO_PLATFORM_API_H

/* The interface OUR app needs from the platform.
 *
 * Implemented by the Dinghy template's platform.c — see its
 * docs/API.md for the real call names and semantics. When
 * integrating, bind each plat_* below to the platform's call
 * (usually a one-line rename in ui.c / store.c / maestro.c).
 *
 * tests/host_stub.c provides no-op implementations so the app
 * links on the host.
 */

#include <stdint.h>

/* ---- screen: 240x240, RGB565 ---- */
void plat_clear(uint16_t color);
void plat_text(int x, int y, int size, const char *s, uint16_t color);
void plat_rect(int x, int y, int w, int h, uint16_t color);
void plat_line(int x0, int y0, int x1, int y1, uint16_t color);
void plat_pixel(int x, int y, uint16_t color);
/* Flush the frame if the platform needs it; no-op otherwise. */
void plat_present(void);

/* ---- input ---- */
/* Number of keys on this board (27 FM-1, 37 SMK-37). */
uint8_t plat_key_count(void);
/* 1 if key_index is currently held. */
uint8_t plat_key_down(uint8_t key_index);
/* Lowest MIDI note of key 0 (for mapping keys to pitches). */
uint8_t plat_key_base_midi(void);
/* Buttons/encoders by platform name, e.g. "BTN_PLAY", "ENC_KNOB1".
 * Returns 1 on a fresh press (edge-triggered). */
uint8_t plat_button_pressed(const char *name);

/* ---- MIDI (out to the synth engine / USB) ---- */
void plat_midi_note_on(uint8_t midi, uint8_t vel);
void plat_midi_note_off(uint8_t midi);

/* ---- storage: 8 blobs x 4KB with CRC, per board ---- */
int plat_blob_read(uint8_t slot, uint8_t *buf, uint32_t len);
int plat_blob_write(uint8_t slot, const uint8_t *buf, uint32_t len);

#endif /* MAESTRO_PLATFORM_API_H */
