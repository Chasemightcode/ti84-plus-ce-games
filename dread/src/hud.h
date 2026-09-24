#ifndef DREAD_HUD_H
#define DREAD_HUD_H

#include <stdbool.h>
#include <stdint.h>

/* Status bar parts; hud_dirty() queues a redraw of the parts that changed. */
#define HUD_AMMO   0x01
#define HUD_HEALTH 0x02
#define HUD_ARMOR  0x04
#define HUD_ARMS   0x08
#define HUD_FACE   0x10
#define HUD_KEYS   0x20
#define HUD_TABLE  0x40
#define HUD_PANEL  0x80
#define HUD_ALL    0xFF

extern bool hud_show_fps;

/* Returns NULL on success or a message naming what is missing. */
const char *hud_load(void);
void hud_dirty(uint8_t parts);
void hud_tic(void);                /* face animation and message timeout */
void hud_draw(void);               /* changed status bar parts -> draw buffer */
void hud_overlay(unsigned fps10);  /* message line and fps over the 3D view */
void hud_message(const char *msg);
void hud_pain(void);
void hud_grin(void);

/* Screen flashes by palette tint: yellow for pickups, red for damage. */
void fx_init(void);                /* after base_palette is loaded */
void fx_reset(void);
void fx_pickup(void);
void fx_damage(int amount);
void fx_tic(void);
void fx_apply(void);               /* upload the palette if the tint changed */

#endif
