/*
 * Progress and best times in the DREADSV AppVar.
 *
 * The save is read once at startup (archived or not) into `save`. It is
 * rewritten in RAM whenever a level is finished and archived when DREAD
 * quits, so a flash write only happens at exit. Deleting and recreating a
 * RAM variable can move the ones after it, so the arena is rebound after
 * every write.
 */
#include <fileioc.h>
#include <string.h>

#include "dread.h"

#define SAVE_VAR   "DREADSV"
#define SAVE_MAGIC "DSV1"

save_t save;

/* tools/emu/test_campaign.py builds saves with this layout */
_Static_assert(sizeof(save_t) == 54 && offsetof(save_t, health) == 6 &&
               offsetof(save_t, ammo) == 16 && offsetof(save_t, best) == 22 &&
               offsetof(save_t, won) == 52, "DREADSV layout");

static void save_clear(void)
{
    memset(&save, 0, sizeof save);
    memcpy(save.magic, SAVE_MAGIC, sizeof save.magic);
}

void save_load(void)
{
    uint8_t h = ti_Open(SAVE_VAR, "r");

    save_clear();
    if (!h)
        return;
    if (ti_GetSize(h) == sizeof save)
        memcpy(&save, ti_GetDataPtr(h), sizeof save);
    ti_Close(h);
    if (memcmp(save.magic, SAVE_MAGIC, sizeof save.magic) || save.skill > SKILL_HARD ||
        save.level > NUM_LEVELS)
        save_clear();
}

bool save_write(bool archive)
{
    uint8_t h;
    bool ok;

    ti_Delete(SAVE_VAR);                 /* an archived copy can't be rewritten */
    h = ti_Open(SAVE_VAR, "w");
    if (!h) {
        arena_rebind();
        return false;
    }
    ok = ti_Write(&save, sizeof save, 1, h) == 1;
    if (ok && archive)
        ti_SetArchiveStatus(true, h);
    ti_Close(h);
    arena_rebind();
    return ok;
}

void save_loadout(const player_t *p, uint8_t level_num)
{
    uint8_t i;

    save.skill = game_skill;
    save.level = level_num;
    save.health = p->health;
    save.armor = p->armor;
    save.armor_class = p->armor_class;
    save.weapons = p->weapons;
    save.weapon = p->weapon;
    for (i = 0; i < NUM_AMMO; i++)
        save.ammo[i] = (uint16_t)p->ammo[i];
}

void load_loadout(player_t *p)
{
    uint8_t i;

    game_skill = save.skill;
    p->health = save.health;
    p->armor = save.armor;
    p->armor_class = save.armor_class;
    p->weapons = save.weapons;
    p->weapon = save.weapon;
    for (i = 0; i < NUM_AMMO; i++)
        p->ammo[i] = save.ammo[i];
    p->keys = 0;
}
