/* No-op platform implementations so the app links on the host.
 * Mirrors what Dinghy's host mode provides; the real implementations
 * live in the template's platform.c. */
#include "platform_api.h"

void plat_clear(uint16_t color) { (void)color; }
void plat_text(int x, int y, int size, const char *s, uint16_t color) {
    (void)x; (void)y; (void)size; (void)s; (void)color;
}
void plat_rect(int x, int y, int w, int h, uint16_t color) {
    (void)x; (void)y; (void)w; (void)h; (void)color;
}
void plat_line(int x0, int y0, int x1, int y1, uint16_t color) {
    (void)x0; (void)y0; (void)x1; (void)y1; (void)color;
}
void plat_pixel(int x, int y, uint16_t color) {
    (void)x; (void)y; (void)color;
}
void plat_present(void) {}

uint8_t plat_key_count(void) { return 27; }
uint8_t plat_key_down(uint8_t key_index) { (void)key_index; return 0; }
uint8_t plat_key_base_midi(void) { return 48; }   /* C3-ish; board-specific */
uint8_t plat_button_pressed(const char *name) { (void)name; return 0; }

void plat_midi_note_on(uint8_t midi, uint8_t vel) { (void)midi; (void)vel; }
void plat_midi_note_off(uint8_t midi) { (void)midi; }

int plat_blob_read(uint8_t slot, uint8_t *buf, uint32_t len) {
    (void)slot; (void)buf; (void)len;
    return -1;   /* no saved progress on host */
}
int plat_blob_write(uint8_t slot, const uint8_t *buf, uint32_t len) {
    (void)slot; (void)buf; (void)len;
    return 0;
}
