/*
 * Neon Slice PC preview driver.
 *
 *   slice_preview --validate
 *   slice_preview --frames 900 --dump 100,400 --keys "30:enter,34:,60:2nd,64:"
 *   slice_preview --frames 3200 --autoplay 1 --keys "20:enter,22:,40:down,42:,50:2nd,52:"
 *
 * --keys  frame:key+key,...  keys are held from that frame until the next entry
 *         (names: up down left right enter 2nd clear del mode, empty = none)
 * --dump  list of frames and/or ranges a-b/step to save as PNG
 * --autoplay 1 = perfect bot, 2 = sloppy bot (misses some, hits some bombs)
 */
#undef main
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mock/keypadc.h"
#include "preview.h"
#include "../src/neon.h"

int game_main(void);
uint8_t host_state(void);
int host_song(void);
int host_frame_sub(void);
uint16_t host_note_count(void);
bool host_paused(void);

const char *preview_out_dir = "preview/out";

static struct { int frame; uint8_t kb[8]; } script[512];
static int nscript;
static struct { int a, b, step; } dumps[64];
static int ndumps;
static int max_frames = 600;
static int autoplay;
static int scale = 2;

/* ---- options ----------------------------------------------------------- */
/* split *s at delim; returns the token or NULL when the input is used up */
static char *next_tok(char **s, char delim)
{
    char *start = *s, *end;
    if (!start || !*start) return NULL;
    end = strchr(start, delim);
    if (end) {
        *end = 0;
        *s = end + 1;
    } else {
        *s = NULL;
    }
    return start;
}

static void parse_keys(const char *s)
{
    static char buf[4096];
    char *rest = buf, *tok;
    strncpy(buf, s, sizeof buf - 1);
    while ((tok = next_tok(&rest, ',')) && nscript < 512) {
        char *colon = strchr(tok, ':'), *names, *name;
        memset(&script[nscript], 0, sizeof script[0]);
        script[nscript].frame = atoi(tok);
        if (colon) {
            names = colon + 1;
            while ((name = next_tok(&names, '+'))) {
                uint8_t *kb = script[nscript].kb;
                if (!strcmp(name, "up")) kb[7] |= kb_Up;
                else if (!strcmp(name, "down")) kb[7] |= kb_Down;
                else if (!strcmp(name, "left")) kb[7] |= kb_Left;
                else if (!strcmp(name, "right")) kb[7] |= kb_Right;
                else if (!strcmp(name, "enter")) kb[6] |= kb_Enter;
                else if (!strcmp(name, "clear")) kb[6] |= kb_Clear;
                else if (!strcmp(name, "2nd")) kb[1] |= kb_2nd;
                else if (!strcmp(name, "del")) kb[1] |= kb_Del;
                else if (!strcmp(name, "mode")) kb[1] |= kb_Mode;
                else fprintf(stderr, "unknown key '%s'\n", name);
            }
        }
        nscript++;
    }
}

static void parse_dumps(const char *s)
{
    static char buf[1024];
    char *rest = buf, *tok;
    strncpy(buf, s, sizeof buf - 1);
    while ((tok = next_tok(&rest, ',')) && ndumps < 64) {
        int a = atoi(tok), b = a, step = 1;
        char *dash = strchr(tok, '-'), *slash = strchr(tok, '/');
        if (dash) b = atoi(dash + 1);
        if (slash) step = atoi(slash + 1);
        dumps[ndumps].a = a;
        dumps[ndumps].b = b;
        dumps[ndumps].step = step > 0 ? step : 1;
        ndumps++;
    }
}

/* ---- frame capture ----------------------------------------------------- */
void preview_frame_done(uint8_t fb[240][320], const uint16_t *pal)
{
    static uint8_t rgb[240 * 4][320 * 4 * 3];
    int i, x, y, want = 0;
    char path[512];
    for (i = 0; i < ndumps; i++)
        if (mock_frame >= dumps[i].a && mock_frame <= dumps[i].b &&
            (mock_frame - dumps[i].a) % dumps[i].step == 0)
            want = 1;
    if (!want) return;
    for (y = 0; y < 240 * scale; y++) {
        for (x = 0; x < 320 * scale; x++) {
            uint16_t c = pal[fb[y / scale][x / scale]];
            uint8_t *p = &rgb[0][0] + ((size_t)y * 320 * scale + x) * 3;
            p[0] = (uint8_t)(((c >> 10) & 31) * 255 / 31);
            p[1] = (uint8_t)(((c >> 5) & 31) * 255 / 31);
            p[2] = (uint8_t)((c & 31) * 255 / 31);
        }
    }
    snprintf(path, sizeof path, "%s/frame_%05d.png", preview_out_dir, mock_frame);
    write_png(path, &rgb[0][0], 320 * scale, 240 * scale);
}

/* ---- keys + autoplay bot ----------------------------------------------- */
static uint8_t ap_done[MAX_NOTES];

static uint8_t autoplay_lanes(void)
{
    static int last_frame = -1, last_song = 1 << 30, blocks_seen, bombs_seen;
    static uint8_t lanes, prev_lanes;
    int song, fs, i, n;

    if (mock_frame == last_frame) return lanes;
    last_frame = mock_frame;
    prev_lanes = lanes;
    lanes = 0;
    if (host_state() != ST_PLAY || host_paused()) return 0;
    song = host_song();
    if (song < last_song - SUB_BEAT) memset(ap_done, 0, sizeof ap_done);  /* new run */
    last_song = song;
    fs = host_frame_sub();
    n = host_note_count();
    for (i = 0; i < n; i++) {
        const note_t *nt = &notes[i];
        int t = (int)nt->tick << SUB_SHIFT;
        /* the press lands right after this frame: take the nearest frame */
        if (t > song + fs / 2) break;
        if (ap_done[i] || nt->state != NS_PENDING) continue;
        if (prev_lanes & (1 << nt->lane)) continue;   /* must release first */
        ap_done[i] = 1;
        if (nt->type == NT_BLOCK) {
            if (autoplay == 2 && ++blocks_seen % 6 == 0) continue;
            lanes |= (uint8_t)(1 << nt->lane);
        } else if (autoplay == 2 && ++bombs_seen % 4 == 0) {
            lanes |= (uint8_t)(1 << nt->lane);
        }
    }
    return lanes;
}

/* only count frames of actual gameplay for the cost estimate */
int preview_measure(void)
{
    return host_state() == ST_PLAY && !host_paused();
}

void preview_keys(uint8_t kb[8])
{
    int i;
    memset(kb, 0, 8);
    for (i = 0; i < nscript; i++)
        if (script[i].frame <= mock_frame) memcpy(kb, script[i].kb, 8);
    if (autoplay) {
        uint8_t l = autoplay_lanes();
        if (l & 1) kb[7] |= kb_Left;
        if (l & 2) kb[7] |= kb_Down;
        if (l & 4) kb[7] |= kb_Up;
        if (l & 8) kb[7] |= kb_Right;
    }
    if (mock_frame >= max_frames) kb[6] |= kb_Clear;
}

/* ---- chart validation -------------------------------------------------- */
static int validate(void)
{
    int errors = 0;
    uint8_t lv;
    for (lv = 0; lv < NUM_LEVELS; lv++) {
        const level_t *L = &levels[lv];
        const char *p = L->chart;
        int bar = 0, i, j, maxbeat = 0;
        chart_info_t info;
        uint16_t n;

        while (*p) {
            int steps = 0;
            while (*p && *p != '|') {
                if (*p == '[') {
                    int blocks = 0, lanes = 0;
                    p++;
                    while (*p && *p != ']') {
                        const char *k = strchr("LDURldur", *p);
                        if (!k || *p == 0) {
                            printf("  %s bar %d: bad char '%c' in group\n", L->name, bar + 1, *p);
                            errors++;
                        } else {
                            int ln = (int)(k - "LDURldur") & 3;
                            if (lanes & (1 << ln)) {
                                printf("  %s bar %d: lane used twice in a group\n", L->name, bar + 1);
                                errors++;
                            }
                            lanes |= 1 << ln;
                            if (k - "LDURldur" < 4) blocks++;
                        }
                        p++;
                    }
                    if (*p != ']') {
                        printf("  %s bar %d: unclosed group\n", L->name, bar + 1);
                        errors++;
                    } else {
                        p++;
                    }
                    if (blocks > 2) {
                        printf("  %s bar %d: more than 2 blocks at once\n", L->name, bar + 1);
                        errors++;
                    }
                } else {
                    if (!strchr(".LDURldur", *p)) {
                        printf("  %s bar %d: bad char '%c'\n", L->name, bar + 1, *p);
                        errors++;
                    }
                    p++;
                }
                steps++;
            }
            if (steps == 0 || TPBAR % steps) {
                printf("  %s bar %d: %d steps does not divide the bar\n", L->name, bar + 1, steps);
                errors++;
            }
            bar++;
            if (*p == '|') p++;
        }

        n = chart_load(lv, &info);
        if (n >= MAX_NOTES) {
            printf("  %s: too many notes (%u)\n", L->name, n);
            errors++;
        }
        for (i = 0; i < n; i++) {
            if (i && notes[i].tick < notes[i - 1].tick) {
                printf("  %s: notes out of order at %d\n", L->name, i);
                errors++;
            }
            if (notes[i].type != NT_BOMB) continue;
            for (j = 0; j < n; j++) {
                int d = (int)notes[j].tick - (int)notes[i].tick;
                if (notes[j].type == NT_BLOCK && notes[j].lane == notes[i].lane && d > -12 && d < 12) {
                    printf("  %s: bomb in lane %d at bar %d too close to a block (%d ticks)\n",
                           L->name, notes[i].lane, notes[i].tick / TPBAR + 1, d);
                    errors++;
                }
            }
        }
        /* densest single beat */
        for (i = 0; i < n; i++) {
            int cnt = 0;
            for (j = i; j < n && notes[j].tick < notes[i].tick + TPB; j++)
                if (notes[j].type == NT_BLOCK) cnt++;
            if (cnt > maxbeat) maxbeat = cnt;
        }
        {
            double secs = (double)bar * 4 * 60 / L->bpm;
            printf("%d %-10s bpm %3u  bars %2d  %5.1fs  blocks %3u  bombs %2u  doubles %2u  "
                   "avg %.2f/s  peak %d/beat\n",
                   lv + 1, L->name, L->bpm, bar, secs, info.blocks, info.bombs, info.doubles,
                   info.blocks / secs, maxbeat);
        }
    }
    printf(errors ? "%d chart error(s)\n" : "charts OK\n", errors);
    return errors ? 1 : 0;
}

int main(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--validate")) return validate();
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) max_frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--keys") && i + 1 < argc) parse_keys(argv[++i]);
        else if (!strcmp(argv[i], "--dump") && i + 1 < argc) parse_dumps(argv[++i]);
        else if (!strcmp(argv[i], "--autoplay") && i + 1 < argc) autoplay = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc) scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) preview_out_dir = argv[++i];
        else if (!strcmp(argv[i], "--probe") && i + 1 < argc) {
            extern int probe_x, probe_y, probe_frame;
            sscanf(argv[++i], "%d,%d,%d", &probe_frame, &probe_x, &probe_y);
        }
        else {
            fprintf(stderr, "unknown option %s\n", argv[i]);
            return 2;
        }
    }
    if (scale < 1) scale = 1;
    if (scale > 4) scale = 4;

    game_main();

    printf("frames %d, drawing warnings %d\n", mock_frame, mock_warnings);
    {
        extern long long cost_sum, cost_max, cost_frames;
        if (cost_frames)
            printf("est. eZ80 cycles/gameplay frame: avg %lld  max %lld  (30 fps budget 1600000)\n",
                   cost_sum / cost_frames, cost_max);
    }
    if (last_result.score || last_result.counts[J_MISS] || last_result.cleared) {
        const result_t *r = &last_result;
        printf("last result: level %u %s  score %lu  combo %u  P/G/Gd/M %u/%u/%u/%u  "
               "bombs %u  acc %u.%u%%  grade %c%s\n",
               r->level + 1, r->cleared ? "CLEAR" : "FAILED", (unsigned long)r->score,
               r->max_combo, r->counts[0], r->counts[1], r->counts[2], r->counts[3],
               r->bombs_hit, r->acc / 10, r->acc % 10, "-DCBAS"[r->grade],
               r->full_combo ? "  FULL COMBO" : "");
    }
    return mock_warnings ? 3 : 0;
}
