/* PC preview stand-in for the GraphX library (only what Neon Slice uses).
   NoClip routines check their bounds and report violations, because on the
   calculator an out-of-bounds unclipped draw corrupts memory. */
#ifndef MOCK_GRAPHX_H
#define MOCK_GRAPHX_H
#include <stdint.h>
#include <stdbool.h>

typedef enum { gfx_screen = 0, gfx_buffer = 1 } gfx_location_t;

typedef struct gfx_sprite_t {
    uint8_t width;
    uint8_t height;
    uint8_t data[];
} gfx_sprite_t;

#define gfx_UninitedSprite(name, width, height) \
    uint8_t name##_data[2 + (width) * (height)]; \
    gfx_sprite_t *name = (gfx_sprite_t *)name##_data

extern uint16_t mock_palette[256];
#define gfx_palette mock_palette
#define gfx_RGBTo1555(r, g, b) \
    ((uint16_t)(((((r) & 255) >> 3) << 10) | ((((g) & 255) >> 3) << 5) | (((b) & 255) >> 3)))
#define gfx_SetDrawBuffer() gfx_SetDraw(gfx_buffer)
#define gfx_SetDrawScreen() gfx_SetDraw(gfx_screen)

void gfx_Begin(void);
void gfx_End(void);
void gfx_SetDraw(uint8_t location);
void gfx_SwapDraw(void);
uint8_t gfx_SetColor(uint8_t index);
uint8_t gfx_SetTransparentColor(uint8_t index);
void gfx_FillScreen(uint8_t index);
void gfx_ZeroScreen(void);
void gfx_SetPixel(int x, int y);
uint8_t gfx_GetPixel(int x, int y);
gfx_sprite_t *gfx_GetSprite(gfx_sprite_t *sprite_buffer, int x, int y);
void gfx_Sprite(const gfx_sprite_t *sprite, int x, int y);
void gfx_Sprite_NoClip(const gfx_sprite_t *sprite, int x, int y);
void gfx_Line(int x0, int y0, int x1, int y1);
void gfx_Line_NoClip(int x0, int y0, int x1, int y1);
void gfx_HorizLine(int x, int y, int length);
void gfx_HorizLine_NoClip(int x, int y, int length);
void gfx_VertLine(int x, int y, int length);
void gfx_VertLine_NoClip(int x, int y, int length);
void gfx_Rectangle(int x, int y, int width, int height);
void gfx_Rectangle_NoClip(int x, int y, int width, int height);
void gfx_FillRectangle(int x, int y, int width, int height);
void gfx_FillRectangle_NoClip(int x, int y, int width, int height);
void gfx_FillTriangle(int x0, int y0, int x1, int y1, int x2, int y2);
void gfx_FillCircle(int x, int y, int radius);
void gfx_Circle(int x, int y, int radius);
void gfx_SetTextScale(uint8_t width_scale, uint8_t height_scale);
void gfx_SetTextXY(int x, int y);
uint8_t gfx_SetTextFGColor(uint8_t color);
uint8_t gfx_SetTextBGColor(uint8_t color);
uint8_t gfx_SetTextTransparentColor(uint8_t color);
void gfx_PrintString(const char *string);
void gfx_PrintStringXY(const char *string, int x, int y);
unsigned int gfx_GetStringWidth(const char *string);

#endif
