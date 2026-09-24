/*
 * PC preview platform: software stand-ins for GraphX, KeypadC, FileIOC and
 * the 32768 Hz clock, plus a PNG frame writer.  Only used on the PC to look
 * at frames and test game logic; none of this goes into SLICE.8xp.
 *
 * It also keeps a rough per-frame cost estimate in eZ80 cycles, built from
 * how the GraphX routines work (e.g. its triangle filler divides twice per
 * scanline), so drawing changes can be compared against the 30 fps budget.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "mock/graphx.h"
#include "mock/keypadc.h"
#include "mock/fileioc.h"
#include "preview.h"
#include "gfx_font.h"

#define W 320
#define H 240

uint16_t mock_palette[256];
uint8_t mock_kb_data[8];
int mock_frame;
int mock_warnings;

static uint8_t buffers[2][H][W];
static int draw_buf = 0;            /* index of the buffer being drawn */
static int shown_buf = 1;
static uint8_t color, text_fg, text_bg = 255, text_tp = 255, transp;
static uint8_t text_sx = 1, text_sy = 1;
static int text_x, text_y;
static unsigned long sim_clock = 100000;

/* ---- cost model -------------------------------------------------------- */
#define LOGIC_CYCLES 200000         /* game logic + palette upload, per frame */
static long long frame_cost;
long long cost_sum, cost_max, cost_frames;

static void cost(long long c) { frame_cost += c; }

/* ---- reporting --------------------------------------------------------- */
static void warn(const char *fmt, int a, int b, int c, int d)
{
    if (mock_warnings < 40) {
        fprintf(stderr, "[frame %d] ", mock_frame);
        fprintf(stderr, fmt, a, b, c, d);
        fputc('\n', stderr);
    }
    mock_warnings++;
}

/* ---- debug probe: which draw call last wrote a pixel ------------------- */
#define MAX_CALLS 30000
static char call_log[MAX_CALLS][72];
static int ncalls;
static uint16_t owner[H][W];
int probe_x = -1, probe_y = -1, probe_frame = -1;

#define LOG_CALL(...) do { if (mock_frame == probe_frame && ncalls < MAX_CALLS - 1) \
    snprintf(call_log[++ncalls], 72, __VA_ARGS__); } while (0)

/* ---- pixels ------------------------------------------------------------ */
static int px(int x, int y, uint8_t c)
{
    if (x < 0 || x >= W || y < 0 || y >= H) return 0;
    buffers[draw_buf][y][x] = c;
    if (mock_frame == probe_frame) owner[y][x] = (uint16_t)ncalls;
    return 1;
}

static int hline(int x, int y, int len, uint8_t c)
{
    int x1 = x + len, n = 0;
    if (y < 0 || y >= H || len <= 0) return 0;
    if (x < 0) x = 0;
    if (x1 > W) x1 = W;
    for (; x < x1; x++) n += px(x, y, c);
    return n;
}

static bool noclip_ok(const char *fn, int x, int y, int w, int h)
{
    if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > W || y + h > H || h > 255) {
        char msg[96];
        snprintf(msg, sizeof msg, "%s out of bounds: x=%%d y=%%d w=%%d h=%%d", fn);
        warn(msg, x, y, w, h);
        return false;
    }
    return true;
}

/* ---- GraphX ------------------------------------------------------------ */
void gfx_Begin(void) { memset(buffers, 0, sizeof buffers); }
void gfx_End(void) {}
void gfx_SetDraw(uint8_t location) { draw_buf = location ? 1 - shown_buf : shown_buf; }

void gfx_SwapDraw(void)
{
    if (mock_frame == probe_frame) {
        FILE *f = fopen("preview/out/calls.txt", "w");
        int i;
        for (i = 1; f && i <= ncalls; i++) {
            fputs(call_log[i], f);
            fputc('\n', f);
        }
        if (f) fclose(f);
        if (probe_x >= 0) {
            int id = owner[probe_y][probe_x];
            fprintf(stderr, "probe %d,%d color %d written by call #%d: %s\n", probe_x, probe_y,
                    buffers[draw_buf][probe_y][probe_x], id, id ? call_log[id] : "(none)");
        }
    }
    ncalls = 0;
    if (preview_measure()) {
        long long c = frame_cost + LOGIC_CYCLES;
        cost_sum += c;
        if (c > cost_max) cost_max = c;
        cost_frames++;
    }
    frame_cost = 0;
    preview_frame_done(buffers[draw_buf], mock_palette);
    shown_buf = draw_buf;
    draw_buf = 1 - draw_buf;
    mock_frame++;
}

uint8_t gfx_SetColor(uint8_t index) { uint8_t o = color; color = index; return o; }
uint8_t gfx_SetTransparentColor(uint8_t index) { uint8_t o = transp; transp = index; return o; }
void gfx_FillScreen(uint8_t index) { memset(buffers[draw_buf], index, W * H); cost(W * H * 5 / 2); }
void gfx_ZeroScreen(void) { gfx_FillScreen(0); }

void gfx_SetPixel(int x, int y)
{
    LOG_CALL("Pixel %d,%d c%d", x, y, color);
    cost(120);
    px(x, y, color);
}

uint8_t gfx_GetPixel(int x, int y)
{
    cost(120);
    return (x >= 0 && x < W && y >= 0 && y < H) ? buffers[draw_buf][y][x] : 0;
}

void gfx_Line(int x0, int y0, int x1, int y1)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, n = 0, drawn = 0;
    LOG_CALL("Line %d,%d %d,%d c%d", x0, y0, x1, y1, color);
    for (;;) {
        drawn += px(x0, y0, color);
        if ((x0 == x1 && y0 == y1) || ++n > 4000) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
    cost(350 + drawn * 25);
}

void gfx_Line_NoClip(int x0, int y0, int x1, int y1)
{
    if (x0 < 0 || x1 < 0 || x0 >= W || x1 >= W || y0 < 0 || y1 < 0 || y0 >= H || y1 >= H)
        warn("gfx_Line_NoClip out of bounds: %d,%d -> %d,%d", x0, y0, x1, y1);
    gfx_Line(x0, y0, x1, y1);
}

void gfx_HorizLine(int x, int y, int length)
{
    LOG_CALL("HLine %d,%d len%d c%d", x, y, length, color);
    cost(180 + hline(x, y, length, color) * 5 / 2);
}

void gfx_HorizLine_NoClip(int x, int y, int length)
{
    if (noclip_ok("gfx_HorizLine_NoClip", x, y, length, 1))
        gfx_HorizLine(x, y, length);
}

void gfx_VertLine(int x, int y, int length)
{
    int i, n = 0;
    LOG_CALL("VLine %d,%d len%d c%d", x, y, length, color);
    for (i = 0; i < length; i++) n += px(x, y + i, color);
    cost(180 + n * 10);
}

void gfx_VertLine_NoClip(int x, int y, int length)
{
    if (noclip_ok("gfx_VertLine_NoClip", x, y, 1, length))
        gfx_VertLine(x, y, length);
}

void gfx_Rectangle(int x, int y, int w, int h)
{
    LOG_CALL("Rectangle %d,%d %dx%d c%d", x, y, w, h, color);
    if (w <= 0 || h <= 0) return;
    gfx_HorizLine(x, y, w);
    gfx_HorizLine(x, y + h - 1, w);
    gfx_VertLine(x, y, h);
    gfx_VertLine(x + w - 1, y, h);
}

void gfx_Rectangle_NoClip(int x, int y, int w, int h)
{
    if (noclip_ok("gfx_Rectangle_NoClip", x, y, w, h))
        gfx_Rectangle(x, y, w, h);
}

void gfx_FillRectangle(int x, int y, int w, int h)
{
    int i, n = 0;
    LOG_CALL("FillRect %d,%d %dx%d c%d", x, y, w, h, color);
    for (i = 0; i < h; i++) n += hline(x, y + i, w, color);
    cost(250 + (h > 0 ? h : 0) * 35 + n * 5 / 2);
}

void gfx_FillRectangle_NoClip(int x, int y, int w, int h)
{
    if (noclip_ok("gfx_FillRectangle_NoClip", x, y, w, h))
        gfx_FillRectangle(x, y, w, h);
}

void gfx_FillTriangle(int x0, int y0, int x1, int y1, int x2, int y2)
{
    int t, y, n = 0;
    LOG_CALL("FillTri %d,%d %d,%d %d,%d c%d", x0, y0, x1, y1, x2, y2, color);
    if (y0 > y1) { t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
    if (y1 > y2) { t = y1; y1 = y2; y2 = t; t = x1; x1 = x2; x2 = t; }
    if (y0 > y1) { t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
    if (y0 == y2) {
        int a = x0 < x1 ? x0 : x1, b = x0 > x1 ? x0 : x1;
        if (x2 < a) a = x2;
        if (x2 > b) b = x2;
        hline(a, y0, b - a + 1, color);
        cost(1500);
        return;
    }
    for (y = y0; y <= y2; y++) {
        int xa = x0 + (x2 - x0) * (y - y0) / (y2 - y0);
        int xb;
        if (y < y1 || y1 == y2)
            xb = (y1 == y0) ? x1 : x0 + (x1 - x0) * (y - y0) / (y1 - y0);
        else
            xb = x1 + (x2 - x1) * (y - y1) / (y2 - y1);
        if (xa > xb) { t = xa; xa = xb; xb = t; }
        n += hline(xa, y, xb - xa + 1, color);
    }
    /* GraphX divides twice per scanline in its triangle filler */
    cost(1500 + (long long)(y2 - y0 + 1) * 1000 + n * 5 / 2);
}

void gfx_FillCircle(int x, int y, int r)
{
    int dy, n = 0;
    LOG_CALL("FillCircle %d,%d r%d c%d", x, y, r, color);
    for (dy = -r; dy <= r; dy++) {
        int dx = (int)sqrt((double)(r * r - dy * dy) + 0.5);
        n += hline(x - dx, y + dy, 2 * dx + 1, color);
    }
    cost(600 + (2 * r + 1) * 220 + n * 5 / 2);
}

void gfx_Circle(int x, int y, int r)
{
    int dx = r, dy = 0, err = 1 - r, n = 0;
    while (dx >= dy) {
        n += px(x + dx, y + dy, color) + px(x - dx, y + dy, color);
        n += px(x + dx, y - dy, color) + px(x - dx, y - dy, color);
        n += px(x + dy, y + dx, color) + px(x - dy, y + dx, color);
        n += px(x + dy, y - dx, color) + px(x - dy, y - dx, color);
        dy++;
        if (err < 0) err += 2 * dy + 1;
        else { dx--; err += 2 * (dy - dx) + 1; }
    }
    cost(600 + n * 30);
}

/* ---- sprites ----------------------------------------------------------- */
gfx_sprite_t *gfx_GetSprite(gfx_sprite_t *s, int x, int y)
{
    int i, j;
    for (j = 0; j < s->height; j++)
        for (i = 0; i < s->width; i++)
            s->data[j * s->width + i] =
                (x + i >= 0 && x + i < W && y + j >= 0 && y + j < H) ? buffers[draw_buf][y + j][x + i] : 0;
    return s;
}

void gfx_Sprite(const gfx_sprite_t *s, int x, int y)
{
    int i, j, n = 0;
    LOG_CALL("Sprite %dx%d at %d,%d", s->width, s->height, x, y);
    for (j = 0; j < s->height; j++)
        for (i = 0; i < s->width; i++)
            n += px(x + i, y + j, s->data[j * s->width + i]);
    cost(300 + s->height * 50 + n * 5 / 2);
}

void gfx_Sprite_NoClip(const gfx_sprite_t *s, int x, int y)
{
    if (noclip_ok("gfx_Sprite_NoClip", x, y, s->width, s->height))
        gfx_Sprite(s, x, y);
}

/* ---- text -------------------------------------------------------------- */
void gfx_SetTextScale(uint8_t ws, uint8_t hs) { text_sx = ws; text_sy = hs; }
void gfx_SetTextXY(int x, int y) { text_x = x; text_y = y; }
uint8_t gfx_SetTextFGColor(uint8_t c) { uint8_t o = text_fg; text_fg = c; return o; }
uint8_t gfx_SetTextBGColor(uint8_t c) { uint8_t o = text_bg; text_bg = c; return o; }
uint8_t gfx_SetTextTransparentColor(uint8_t c) { uint8_t o = text_tp; text_tp = c; return o; }

unsigned int gfx_GetStringWidth(const char *s)
{
    unsigned w = 0;
    while (*s) w += gfx_font_spacing[(uint8_t)*s++] * text_sx;
    return w;
}

void gfx_PrintString(const char *s)
{
    int w = (int)gfx_GetStringWidth(s);
    LOG_CALL("Text '%.20s' at %d,%d fg%d", s, text_x, text_y, text_fg);
    if (text_x < 0 || text_y < 0 || text_x + w > W || text_y + 8 * text_sy > H)
        warn("unclipped text off screen at %d,%d (w=%d scale=%d)", text_x, text_y, w, text_sx);
    while (*s) {
        uint8_t ch = (uint8_t)*s++;
        int cw = gfx_font_spacing[ch], row, col, i, j;
        /* GraphX walks every glyph cell, scaled, whether set or not */
        cost(250 + cw * 8 * text_sy * (20 + 9 * text_sx));
        for (row = 0; row < 8; row++) {
            uint8_t bits = gfx_font_data[ch * 8 + row];
            for (col = 0; col < cw; col++) {
                uint8_t c = (bits & (0x80 >> col)) ? text_fg : text_bg;
                if (c == text_tp) continue;
                for (j = 0; j < text_sy; j++)
                    for (i = 0; i < text_sx; i++)
                        px(text_x + col * text_sx + i, text_y + row * text_sy + j, c);
            }
        }
        text_x += cw * text_sx;
    }
}

void gfx_PrintStringXY(const char *s, int x, int y)
{
    gfx_SetTextXY(x, y);
    gfx_PrintString(s);
}

/* ---- keypad ------------------------------------------------------------ */
void kb_Scan(void)
{
    preview_keys(mock_kb_data);
}

/* ---- clock ------------------------------------------------------------- */
unsigned long mock_clock(void)
{
    sim_clock += 37;
    return sim_clock;
}

/* ---- FileIOC ----------------------------------------------------------- */
static FILE *files[8];

uint8_t ti_Open(const char *name, const char *mode)
{
    char path[512];
    uint8_t h;
    FILE *f;
    snprintf(path, sizeof path, "%s/%s.8xv", preview_out_dir, name);
    f = fopen(path, mode[0] == 'r' ? "rb" : "wb");
    if (!f) return 0;
    for (h = 1; h < 8; h++) {
        if (!files[h]) {
            files[h] = f;
            return h;
        }
    }
    fclose(f);
    return 0;
}

int ti_Close(uint8_t h)
{
    if (h && h < 8 && files[h]) {
        fclose(files[h]);
        files[h] = NULL;
    }
    return 1;
}

size_t ti_Read(void *data, size_t size, size_t count, uint8_t h)
{
    return (h && h < 8 && files[h]) ? fread(data, size, count, files[h]) : 0;
}

size_t ti_Write(const void *data, size_t size, size_t count, uint8_t h)
{
    return (h && h < 8 && files[h]) ? fwrite(data, size, count, files[h]) : 0;
}

int ti_SetArchiveStatus(uint8_t archive, uint8_t h)
{
    (void)archive;
    (void)h;
    return 1;
}

/* ---- PNG writer (uncompressed deflate) --------------------------------- */
static uint32_t crc_table[256];

static uint32_t crc(uint32_t c, const uint8_t *p, size_t n)
{
    size_t i;
    if (!crc_table[1]) {
        uint32_t k, j;
        for (k = 0; k < 256; k++) {
            uint32_t v = k;
            for (j = 0; j < 8; j++) v = (v & 1) ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            crc_table[k] = v;
        }
    }
    c ^= 0xFFFFFFFFu;
    for (i = 0; i < n; i++) c = crc_table[(c ^ p[i]) & 255] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

static void be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

static void chunk(FILE *f, const char *tag, const uint8_t *data, uint32_t len)
{
    uint8_t hdr[8];
    uint32_t c;
    be32(hdr, len);
    memcpy(hdr + 4, tag, 4);
    fwrite(hdr, 1, 8, f);
    if (len) fwrite(data, 1, len, f);
    c = crc(0, hdr + 4, 4);
    c = crc(c, data, len);
    be32(hdr, c);
    fwrite(hdr, 1, 4, f);
}

int write_png(const char *path, const uint8_t *rgb, int w, int h)
{
    size_t raw_len = (size_t)(w * 3 + 1) * h;
    size_t blocks = (raw_len + 65534) / 65535;
    size_t zlen = 2 + raw_len + blocks * 5 + 4;
    uint8_t *raw = malloc(raw_len), *z = malloc(zlen), ihdr[13];
    size_t i, pos = 0, off = 0;
    uint32_t a = 1, b = 0;
    FILE *f = fopen(path, "wb");
    if (!f || !raw || !z) return 0;
    for (i = 0; i < (size_t)h; i++) {
        raw[i * (w * 3 + 1)] = 0;
        memcpy(raw + i * (w * 3 + 1) + 1, rgb + i * w * 3, (size_t)w * 3);
    }
    z[pos++] = 0x78;
    z[pos++] = 0x01;
    while (off < raw_len) {
        size_t n = raw_len - off > 65535 ? 65535 : raw_len - off;
        z[pos++] = off + n >= raw_len ? 1 : 0;
        z[pos++] = (uint8_t)n; z[pos++] = (uint8_t)(n >> 8);
        z[pos++] = (uint8_t)~n; z[pos++] = (uint8_t)(~n >> 8);
        memcpy(z + pos, raw + off, n);
        pos += n;
        off += n;
    }
    for (i = 0; i < raw_len; i++) {
        a = (a + raw[i]) % 65521;
        b = (b + a) % 65521;
    }
    be32(z + pos, (b << 16) | a);
    pos += 4;
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    be32(ihdr, (uint32_t)w);
    be32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", z, (uint32_t)pos);
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw);
    free(z);
    return 1;
}
