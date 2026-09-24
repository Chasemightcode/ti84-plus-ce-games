/*
 * NEON SLICE - level definitions and hand-written beat charts.
 *
 * Chart notation (one string per level):
 *   |         ends a 4/4 bar
 *   .         rest
 *   L D U R   block in the left / down / up / right lane
 *   l d u r   bomb in that lane (do NOT press)
 *   [..]      several objects at the same moment, e.g. [LR] = double
 * Every bar is split evenly by its number of steps: 4 steps = quarter
 * notes, 8 = eighths, 12 = eighth triplets, 16 = sixteenths.
 */
#include <string.h>
#include "neon.h"

note_t notes[MAX_NOTES];

/* ---- 1: WARM UP - single blocks, one lane at a time, quarter notes ----- */
static const char chart1[] =
    "....|"
    "L...|R...|L...|R...|"
    "D...|U...|D...|U...|"
    "L.R.|L.R.|D.U.|D.U.|"
    "L.D.|U.R.|R.U.|D.L.|"
    "L.L.|R.R.|D.D.|U.U.|"
    "LDUR|....|RUDL|....|"
    "L.R.|D.U.|L.U.|D.R.|"
    "LRLR|DUDU|L.D.|U.R.|"
    "L...|....|";

/* ---- 2: GROOVE - eighth-note patterns across the lanes ------------------ */
static const char chart2[] =
    "........|"
    "L...R...|D...U...|L...R...|D.U.L.R.|"
    "L.D.U.R.|R.U.D.L.|L.D.U.R.|R...L...|"
    "L.R.L.R.|D.U.D.U.|L.U.R.D.|L...R...|"
    "L..DU..R|L..DU..R|R..UD..L|R..UD..L|"
    "LD..UR..|LD..UR..|RU..DL..|RU..DL..|"
    "L.R.L.R.|LRLR....|D.U.D.U.|DUDU....|"
    "LDURLDUR|R...L...|RUDLRUDL|L...R...|"
    "L.DU.R.D|U.LR.D.U|L.DU.R.D|U.L.R...|"
    "LDUR.RUD|LDUR.RUD|L.L.R.R.|U.U.D...|"
    "L.D.U.R.|LDURLDUR|R.U.D.L.|RUDLRUDL|"
    "L.......|";

/* ---- 3: OVERDRIVE - double blocks enter ----------------------------------- */
static const char chart3[] =
    "........|"
    "L...R...|[LR]...[LR]...|D...U...|[DU]...[DU]...|"
    "L.R.[LR]...|D.U.[DU]...|L.D.[LD]...|U.R.[UR]...|"
    "L.D.U.R.|[LR]...[DU]...|R.U.D.L.|[LU]...[DR]...|"
    "LDUR[LR]...|RUDL[DU]...|L.R.L.R.|[LR].[LR].[DU]...|"
    "L.DU.R[LR].|D.LR.U[DU].|L.DU.R[LR].|[LR].[DU].[LR]...|"
    "[LD].[UR].[LD].[UR].|L.D.U.R.|[LU].[DR].[LU].[DR].|R.U.D.L.|"
    "[LR].......|[DU].......|L.R.D.U.|[LR]...[DU]...|"
    "LRDU[LR]...|LRDU[DU]...|LDURLDUR|[LR].[DU].[LR].[DU].|"
    "L.[LR].R.[LR].|D.[DU].U.[DU].|[LD][UR][LD][UR]....|[LU][DR][LU][DR]....|"
    "LDURRUDL|[LR]...[DU]...|L.R.[LR].[DU].|[LD].[UR].[LU].[DR].|"
    "[LR].......|........|";

/* ---- 4: HYPERBEAT - bombs, syncopation, sixteenth bursts ---------------- */
static const char chart4[] =
    "........|L...R...|D...U...|L.D.U.R.|"
    "[Ld]...[Ru]...|[Dl]...[Ur]...|d.u.L.R.|l.r.D.U.|"
    "LD.UR.LD|UR.LD.UR|[LR]..d[LR]..u|L.RL.RL.|"
    "D.UD.UD.|[Lu].[Rd].[Lu].[Rd].|LDUR....RUDL....|[lr]D.U[lr]D.U|"
    "LDUR[LR]...|RUDL[DU]...|l.R.r.L.d.U.u.D.|[LR]...[du]...[DU]...[lr]...|"
    "LRLR[DU]...LRLR[DU]...|DUDU[LR]...DUDU[LR]...|L..D..U..R..L...|R..U..D..L..R...|"
    "[LR]...[du]...|[DU]...[lr]...|L.[dr].R.[lu].|D.[lr].U.[lr].|"
    "LD.UR.LD|U.R.[LR]...|[Ld].[Ru].[Ld].[Ru].|LDURLDUR|"
    "RUDLRUDL|[LR].d.u.[DU].l.r.|L.RL.RL.|D.UD.UD.|"
    "LDUR....RUDL....|LRLR....DUDU....|[LR]..d[LR]..u|[DU]..l[DU]..r|"
    "L.D.U.R.|[lr].D.U.[lr].|R.U.D.L.|[du].L.R.[du].|"
    "LDUR[LR]...|RUDL[DU]...|[LR].......|........|";

/* ---- 5: INSANE - dense doubles, bomb walls, streams ---------------------- */
static const char chart5[] =
    "........|L.D.U.R.|[LR].[DU].[LR].[DU].|LDURRUDL|"
    "[LR]...d...[DU]...l...|[LU].[DR].[LU].[DR].|LDURLDUR|[Ld].[Ru].[Ld].[Ru].|"
    "L.RL.RL.|D.UD.UD.|[LR].[DU].LDUR|[LU].[DR].RUDL|"
    "LDURLDURRUDLRUDL|[LR]...[DU]...[LR]...[DU]...|LRLR[DU]...DUDU[LR]...|[lr]DU.[lr]UD.|"
    "[Ld][Ru][Dl][Ur]|LDUR[LR]...|RUDL[DU]...|[LR].u.[DU].r.|"
    "L.D.[LR].U.R.[DU].|[LU][DR][LU][DR]LDUR|RUDL[LR][DU][LR][DU]|[Ld].[Ru].[Dl].[Ur].|"
    "LDURLDUR|[LR]d.u[DU]l.r|[DU]l.r[LR]d.u|[LR]...[DU]...|"
    "[lr]...[du]...|L.R.D.U.|[lr]...[du]...|[LR].[DU].[LU].[DR].|"
    "LDURLDURRUDLRUDL|[LR].[DU].[LR].[DU].|LRDULRDU|[Ld][Ru][Ld][Ru]|"
    "DULRDULR|[Dl][Ur][Dl][Ur]|LDUR[LR].[DU].|RUDL[DU].[LR].|"
    "[LU][DR]..[LU][DR]..|[LD][UR]..[LD][UR]..|LRLR[DU]...DUDU[LR]...|LDURLDURLDURLDUR|"
    "[LR]d.u[DU]l.r|[LR]...[DU]...[LR].[DU].[LR][DU][LR][DU]|L.R.[LR].D.U.[DU].|[lr]DU.[du]LR.|"
    "LDURRUDL|[LR][DU][LR][DU]|L.D.U.R.|RUDLLDUR|"
    "[Ld].[Ru].[Dl].[Ur].|LRLRDUDU[LR].[DU].|[LR].......|........|";

const level_t levels[NUM_LEVELS] = {
    /* name        chart   bpm  *  appr  P   Gr   Go  miss bomb theme ui */
    {"WARM UP",   chart1, 100, 1, 2000, 55, 100, 150,  8, 15, 1, C_CYAN},
    {"GROOVE",    chart2, 116, 2, 1700, 48,  90, 135, 10, 15, 2, C_LIME},
    {"OVERDRIVE", chart3, 132, 3, 1450, 44,  82, 122, 11, 16, 3, C_PINK},
    {"HYPERBEAT", chart4, 150, 4, 1250, 38,  72, 110, 12, 18, 4, C_PURPLE},
    {"INSANE",    chart5, 174, 5, 1050, 32,  62,  95, 14, 20, 5, C_RED},
};

static int8_t lane_of(char c)
{
    const char *k = "LDURldur";
    const char *p = strchr(k, c);
    return (p && c) ? (int8_t)(p - k) : -1;
}

/* Parse a level's chart into notes[]; returns the number of objects. */
uint16_t chart_load(uint8_t level, chart_info_t *info)
{
    const char *p = levels[level].chart;
    uint16_t n = 0, bar = 0;

    memset(info, 0, sizeof *info);
    while (*p) {
        const char *start = p;
        uint8_t steps = 0, step = 0;
        uint16_t len;

        /* pass 1: count the steps of this bar */
        while (*p && *p != '|') {
            if (*p == '[') {
                while (*p && *p != ']') p++;
                if (*p) p++;
            } else {
                p++;
            }
            steps++;
        }
        len = steps ? TPBAR / steps : TPBAR;

        /* pass 2: emit objects */
        while (start < p) {
            uint16_t tick = bar + step * len;
            uint16_t blocks = 0, a = 0, b = 0;
            bool group = (*start == '[');
            if (group) start++;
            do {
                int8_t l = lane_of(*start);
                if (l >= 0 && n < MAX_NOTES) {
                    note_t *nt = &notes[n];
                    nt->tick = tick;
                    nt->lane = (uint8_t)(l & 3);
                    nt->type = l >= 4 ? NT_BOMB : NT_BLOCK;
                    nt->state = NS_PENDING;
                    nt->link = 0;
                    if (nt->type == NT_BLOCK) {
                        if (blocks == 0) a = n; else b = n;
                        blocks++;
                        info->blocks++;
                    } else {
                        info->bombs++;
                    }
                    info->end_tick = tick;
                    n++;
                }
                start++;
            } while (group && start < p && *start != ']');
            if (group && start < p) start++;       /* skip ']' */
            if (blocks == 2) {
                notes[a].link = (int8_t)(b - a);
                notes[b].link = (int8_t)(a - b);
                info->doubles++;
            }
            step++;
        }
        bar += TPBAR;
        if (*p == '|') p++;
    }
    info->notes = n;
    return n;
}
