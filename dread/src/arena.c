/*
 * RAM arena: large buffers that don't fit in the program's static memory
 * region live in a temporary AppVar created in free user RAM.
 *
 * TI-OS inserts new variables after existing ones, so the arena stays put
 * while DREAD runs as long as no variable before it is deleted, archived
 * or resized. Anything that does that (saving, in stage 5) must call
 * arena_rebind() afterwards.
 */
#include <fileioc.h>

#include "dread.h"

#define ARENA_VAR "DREADTMP"

uint8_t *arena;
uint8_t *rc_automap;            /* arena + ARENA_AUTOMAP, for raycast.s */

static uint8_t *align(uint8_t *p)
{
    return (uint8_t *)(((uintptr_t)p + 255) & ~(uintptr_t)255);
}

const char *arena_init(void)
{
    uint8_t h;

    ti_Delete(ARENA_VAR);                   /* left over from a crash */
    h = ti_Open(ARENA_VAR, "w");
    if (!h)
        return "cannot create DREADTMP";
    if (ti_Resize(ARENA_SIZE + 255, h) <= 0) {
        ti_Close(h);
        ti_Delete(ARENA_VAR);
        return "needs 25 KB free RAM";
    }
    arena = align((uint8_t *)ti_GetDataPtr(h));
    rc_automap = arena + ARENA_AUTOMAP;
    ti_Close(h);
    return NULL;
}

void arena_rebind(void)
{
    uint8_t h = ti_Open(ARENA_VAR, "r");

    if (h) {
        arena = align((uint8_t *)ti_GetDataPtr(h));
        rc_automap = arena + ARENA_AUTOMAP;
        ti_Close(h);
    }
}

void arena_free(void)
{
    ti_Delete(ARENA_VAR);
}
