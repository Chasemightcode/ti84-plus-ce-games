/*
 * NEON SLICE - high scores stored in the AppVar "NSLICESV".
 *
 * During play the AppVar lives in RAM; on exit (after graphics are shut
 * down, so a garbage-collect prompt can show safely) it is archived so it
 * survives RAM clears.
 */
#include <fileioc.h>
#include <string.h>
#include "neon.h"

#define SAVE_NAME   "NSLICESV"
#define SAVE_MAGIC  0x4C53
#define SAVE_VER    1

record_t records[NUM_LEVELS];

typedef struct {
    uint16_t magic;
    uint8_t version;
    record_t rec[NUM_LEVELS];
} save_file_t;

void save_load(void)
{
    save_file_t f;
    uint8_t h;

    memset(records, 0, sizeof records);
    h = ti_Open(SAVE_NAME, "r");
    if (!h) return;
    if (ti_Read(&f, sizeof f, 1, h) == 1 && f.magic == SAVE_MAGIC && f.version == SAVE_VER)
        memcpy(records, f.rec, sizeof records);
    ti_Close(h);
}

void save_write(bool archive)
{
    save_file_t f;
    uint8_t h;

    memset(&f, 0, sizeof f);
    f.magic = SAVE_MAGIC;
    f.version = SAVE_VER;
    memcpy(f.rec, records, sizeof records);
    h = ti_Open(SAVE_NAME, "w");
    if (!h) return;
    ti_Write(&f, sizeof f, 1, h);
    if (archive) ti_SetArchiveStatus(true, h);
    ti_Close(h);
}
