#include "store.h"
#include "platform_api.h"

#include <string.h>

#define PROGRESS_BLOB_SLOT 0

uint32_t crc32(const uint8_t *data, uint32_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t i;
    int b;
    for (i = 0; i < len; i++) {
        crc ^= data[i];
        for (b = 0; b < 8; b++) {
            if (crc & 1) crc = (crc >> 1) ^ 0xEDB88320u;
            else         crc >>= 1;
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

static uint32_t progress_crc(const progress_t *p) {
    return crc32((const uint8_t *)p,
                 (uint32_t)((const uint8_t *)&p->crc - (const uint8_t *)p));
}

void progress_load(progress_t *p) {
    memset(p, 0, sizeof(*p));
    if (plat_blob_read(PROGRESS_BLOB_SLOT, (uint8_t *)p, sizeof(*p)) != 0)
        return;   /* no saved progress yet */
    if (p->magic != PROGRESS_MAGIC || p->crc != progress_crc(p))
        memset(p, 0, sizeof(*p));   /* corrupt: start clean */
}

void progress_save(const progress_t *p) {
    progress_t tmp = *p;
    tmp.magic = PROGRESS_MAGIC;
    tmp.crc = progress_crc(&tmp);
    plat_blob_write(PROGRESS_BLOB_SLOT, (const uint8_t *)&tmp, sizeof(tmp));
}
