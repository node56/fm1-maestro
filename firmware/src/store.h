#ifndef MAESTRO_STORE_H
#define MAESTRO_STORE_H

/* Progress persistence in a platform flash blob (4 KB, CRC'd).
 * Pure C except plat_blob_read/write (platform_api.h).
 */

#include <stdint.h>

#define PROGRESS_LESSONS 16
#define PROGRESS_MAGIC   0x4D414553u   /* "MAES" */

typedef struct {
    uint32_t magic;
    uint8_t  stars[PROGRESS_LESSONS];       /* 0..3 per lesson index */
    uint16_t best_score[PROGRESS_LESSONS];  /* 0..10000 */
    uint8_t  drill_level;                   /* adaptive drill level 0..5 */
    uint8_t  drill_range;                   /* generator range (semitones) */
    uint8_t  drill_tempo;                   /* generator tempo */
    uint8_t  _pad;
    uint32_t crc;                            /* CRC32 over everything above */
} progress_t;

uint32_t crc32(const uint8_t *data, uint32_t len);

/* Load progress (zeroed if the blob is missing or the CRC fails). */
void progress_load(progress_t *p);
/* Save progress. */
void progress_save(const progress_t *p);

#endif /* MAESTRO_STORE_H */
