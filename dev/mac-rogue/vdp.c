/*
 * vdp.c -- software TMS9918 Graphics-I compositor. See vdp.h for the model.
 *
 * The compositor's output is gPix: one TMS palette index per pixel, exactly
 * what the real chip put on the wire. Everything Macintosh-specific -- 1-bit
 * packing, an 8-bit CLUT, the 2x stretch -- happens in mac_shell.c, so this
 * file never has to know what kind of screen it is feeding.
 */

#include "vdp.h"
#include "rogue_gfx.h"
#include "rogue.h"   /* CHAR_* tile bases */

unsigned char gName[VDP_ROWS * VDP_COLS];
SatEntry      gSat[VDP_SAT_MAX];
short         gSatCount;
unsigned char gPix[VDP_HEIGHT * VDP_WIDTH];
unsigned char gFrame[VDP_HEIGHT * VDP_ROWBYTES];
unsigned char gVdpPhase;

/* The three idle-animation cycles, all indexed by (gVdpPhase + column) & 3
 * so neighbours drift out of step instead of marching in unison. Every
 * flame and pulse shade is ink, so monochrome renders them all the same. */
static const signed char   kBobDy[4]  = { 0, -1, -2, -1 };   /* hover      */
static const unsigned char kFlame[4]  = { 9, 10, 11, 10 };   /* torch fire */
static const unsigned char kPulse[4]  = { 8,  9, 10,  9 };   /* headlines  */
static const unsigned char kGlint[4]  = { 11, 15, 11, 10 };  /* stairs down */

/* The TMS9918A's fixed 16-entry palette. Index 0 is transparent: on real
 * silicon the backdrop (register 7) showed through, and this game's backdrop
 * is black, so 0 and 1 carry the same RGB and nothing downstream needs a
 * special case. */
const unsigned char kTmsPaletteRGB[16][3] = {
    {   0,   0,   0 },   /*  0 transparent -> backdrop, black here */
    {   0,   0,   0 },   /*  1 black                                */
    {  33, 200,  66 },   /*  2 medium green                         */
    {  94, 220, 120 },   /*  3 light green                          */
    {  84,  85, 237 },   /*  4 dark blue                            */
    { 125, 118, 252 },   /*  5 light blue   -- the player           */
    { 212,  82,  77 },   /*  6 dark red     -- armour               */
    {  66, 235, 245 },   /*  7 cyan         -- skeleton, scrolls    */
    { 252,  85,  84 },   /*  8 medium red   -- hit flash, the boss  */
    { 255, 121, 120 },   /*  9 light red    -- torch flame          */
    { 212, 193,  84 },   /* 10 dark yellow  -- gold, DEATH          */
    { 230, 206, 128 },   /* 11 light yellow -- food                 */
    {  33, 176,  59 },   /* 12 dark green                           */
    { 201,  91, 186 },   /* 13 magenta      -- potions              */
    { 204, 204, 204 },   /* 14 gray         -- walls, ghost, steel  */
    { 255, 255, 255 }    /* 15 white        -- undead, weapons      */
};

/* --------------------------------------------------------------------------
 * Ink rule for the monochrome packing.
 *
 * Palette 0 is transparent and 1 is black; this artwork sets every colour
 * table background nibble to 1, so the dungeon is light-on-black on a TV. A
 * Macintosh screen is paper, not a phosphor field: anything that is neither
 * transparent nor black becomes ink, and ink becomes a set bit -- which
 * classic QuickDraw draws black. Same silhouettes, printed rather than
 * glowing.
 * -------------------------------------------------------------------------- */
#define IS_INK(c)  ((c) != 0 && (c) != 1)

/* Per-cell colour override for the UI cards (0 = use the char's group
 * colour) and the big-text overlay list. Both share the name table's
 * lifecycle: VdpClearNames wipes them, so every screen starts neutral. */
static unsigned char gCellFg[VDP_ROWS * VDP_COLS];

#define VDP_BIG_MAX 4
typedef struct { short row, col; unsigned char fg; const char *text; } BigText;
static BigText gBigText[VDP_BIG_MAX];
static short   gBigCount;

void VdpClearNames(void)
{
    short i;
    for (i = 0; i < VDP_ROWS * VDP_COLS; i++) {
        gName[i] = 0;
        gCellFg[i] = 0;
    }
    gBigCount = 0;
}

void VdpSetName(short row, short col, unsigned char ch)
{
    if (row < 0 || row >= VDP_ROWS || col < 0 || col >= VDP_COLS)
        return;
    gName[row * VDP_COLS + col] = ch;
}

void VdpPutString(short row, short col, const char *s)
{
    while (*s) {
        VdpSetName(row, col, (unsigned char)*s);
        col++;
        s++;
    }
}

void VdpFillRow(short row, short col, short count, unsigned char ch)
{
    while (count-- > 0)
        VdpSetName(row, col++, ch);
}

void VdpPutStringColor(short row, short col, const char *s, unsigned char fg)
{
    while (*s) {
        if (row >= 0 && row < VDP_ROWS && col >= 0 && col < VDP_COLS) {
            gName[row * VDP_COLS + col] = (unsigned char)*s;
            gCellFg[row * VDP_COLS + col] = fg;
        }
        col++;
        s++;
    }
}

void VdpPutStringBig(short row, short col, const char *s, unsigned char fg)
{
    BigText *t;
    if (gBigCount >= VDP_BIG_MAX)
        return;
    t = &gBigText[gBigCount++];
    t->row = row;
    t->col = col;
    t->fg = fg;
    t->text = s;
}

void VdpSatReset(void)
{
    gSatCount = 0;
}

void VdpSatAdd(unsigned char y, unsigned char x,
               unsigned char name, unsigned char color, unsigned char flags)
{
    SatEntry *e;
    if (gSatCount >= VDP_SAT_MAX)
        return;
    e = &gSat[gSatCount++];
    e->y = y;
    e->x = x;
    /* Defensive, mirroring place_all_sprites: a 16x16 sprite name must be a
     * multiple of 4 or the chip fetches a skewed quadrant set. */
    e->name  = name & 0xFC;
    e->color = color & 0x0F;
    e->flags = flags;
}

/* --- Per-char colour --------------------------------------------------------
 * The TMS9918's Graphics-I mode allowed ONE colour per group of EIGHT chars:
 * kTileColors is 32 entries for 256 chars. That is why doors and stairs-down
 * shared a shade (both in group 1) and why a revealed pit looked exactly like
 * stairs-up (both in group 2). It was silicon, not design -- and this is not
 * that silicon, so each tile gets its own colour here.
 *
 * Monochrome is untouched: every value below is >= 6, so the ink rule (any
 * colour that is neither transparent nor black) sees no change at all.
 * -------------------------------------------------------------------------- */
static unsigned char gCharFg[256];
static unsigned char gCharBg[256];
static int gColorsReady = 0;

static void InitCharColors(void)
{
    short c;
    /* Start from the chip's own table so anything not overridden below keeps
     * exactly the colour the Apple-1 build gave it. */
    for (c = 0; c < 256; c++) {
        unsigned char attr = kTileColors[c >> 3];
        gCharFg[c] = (unsigned char)(attr >> 4);
        gCharBg[c] = (unsigned char)(attr & 0x0F);
    }
    /* The six dungeon tiles, four chars each (TL, TR, BL, BR). */
    for (c = 0; c < 4; c++) {
        gCharFg[CHAR_WALL        + c] = 14;  /* gray   -- masonry           */
        gCharFg[CHAR_DOOR        + c] = 10;  /* dark yellow -- wood         */
        gCharFg[CHAR_STAIRS_DOWN + c] = 11;  /* light yellow -- the way on  */
        gCharFg[CHAR_STAIRS_UP   + c] = 14;  /* gray -- stone, and a wall
                                              * to the player anyway        */
        gCharFg[CHAR_TRAP_PIT    + c] = 6;   /* dark red -- danger, and no
                                              * longer a twin of the stairs */
    }
    gColorsReady = 1;
}

/* --- Tile pass: 32x24 cells of 8x8 out of the pattern table. -------------- */
static void CompositeTiles(void)
{
    short row, col, y, b;

    if (!gColorsReady)
        InitCharColors();

    for (row = 0; row < VDP_ROWS; row++) {
        for (col = 0; col < VDP_COLS; col++) {
            unsigned char ch = gName[row * VDP_COLS + col];
            const unsigned char *pat = &kTileset[(short)ch * 8];
            unsigned char ov = gCellFg[row * VDP_COLS + col];
            unsigned char fg = ov ? ov : gCharFg[ch];
            unsigned char bg = gCharBg[ch];

            /* The way down glints on the idle clock (half rate -- a glimmer,
             * not a strobe), pulling the eye to the level's one exit. Every
             * shade is ink, so monochrome stairs hold still. */
            if (ch >= CHAR_STAIRS_DOWN && ch < CHAR_STAIRS_DOWN + 4 && !ov)
                fg = kGlint[(gVdpPhase >> 1) & 3];
            unsigned char *dst = &gPix[(row * 8) * VDP_WIDTH + col * 8];

            for (y = 0; y < 8; y++) {
                unsigned char bits = pat[y];
                for (b = 0; b < 8; b++)
                    dst[b] = (bits & (0x80 >> b)) ? fg : bg;
                dst += VDP_WIDTH;
            }
        }
    }
}

/* --- Big-text pass ----------------------------------------------------------
 * Each glyph is fetched from the same pattern table the tiles use and every
 * pixel becomes a 2x2 block, so a string covers two cell rows. Only set
 * pixels paint -- the tile background stays visible -- and the fg is a plain
 * palette index, so the mono ink rule needs no special case. */
static void CompositeBigText(void)
{
    short i, y, b;

    for (i = 0; i < gBigCount; i++) {
        const BigText *t = &gBigText[i];
        const char *s = t->text;
        short px = t->col * 8;
        short py = t->row * 8;
        unsigned char fg = (t->fg == VDP_FG_PULSE)
                             ? kPulse[gVdpPhase & 3] : t->fg;

        for (; *s; s++, px += 16) {
            const unsigned char *pat =
                &kTileset[(short)(unsigned char)*s * 8];
            for (y = 0; y < 8; y++) {
                short dy = (short)(py + y * 2);
                unsigned char bits = pat[y];
                if (dy < 0 || dy + 1 >= VDP_HEIGHT)
                    continue;
                for (b = 0; b < 8; b++) {
                    short dx;
                    unsigned char *d;
                    if (!(bits & (0x80 >> b)))
                        continue;
                    dx = (short)(px + b * 2);
                    if (dx < 0 || dx + 1 >= VDP_WIDTH)
                        continue;
                    d = &gPix[dy * VDP_WIDTH + dx];
                    d[0] = d[1] = fg;
                    d[VDP_WIDTH] = d[VDP_WIDTH + 1] = fg;
                }
            }
        }
    }
}

/* --- Sprite pass -----------------------------------------------------------
 * 16x16, four 8x8 quadrants stored TL, BL, TR, BR -- so the left column's 16
 * rows are contiguous at pat[0..15] and the right column's at pat[16..31].
 * Colour 0 is transparent, which here means "leave the tile underneath".
 * -------------------------------------------------------------------------- */
static void CompositeSprite(const SatEntry *e, int monoFlash)
{
    const unsigned char *pat;
    short slot = e->name;
    short y, b;
    int negative = monoFlash && (e->flags & SPR_INVERT);
    /* Idle animation: the phase is offset by the sprite's cell column so a
     * row of loot shimmers instead of pumping in lockstep. */
    unsigned char step = (unsigned char)((gVdpPhase + (e->x >> 4)) & 3);
    short dy = (e->flags & SPR_BOB) ? kBobDy[step] : 0;
    /* Shake keys off the raw phase, NOT the column-desynced step: the boss
     * is four SAT entries in two columns, and a per-column parity would
     * tear its halves 2 px apart instead of jolting it as one body. */
    short dx = (e->flags & SPR_SHAKE) ? ((gVdpPhase & 1) ? 1 : -1) : 0;
    unsigned char color = (e->flags & SPR_FLICKER) ? kFlame[step] : e->color;

    if (slot >= 56) {
        if (slot > 68)
            return;
        pat = &kBossPats[(slot - 56) * 8];
    } else {
        if (slot > 52)
            return;
        pat = &kSpritePats[slot * 8];
    }

    for (y = 0; y < 16; y++) {
        short sy = (short)e->y + dy + y;
        unsigned short bits;
        unsigned char *dst;

        if (sy < 0 || sy >= VDP_HEIGHT)
            continue;
        bits = (unsigned short)(((unsigned short)pat[y] << 8) | pat[16 + y]);
        dst = &gPix[sy * VDP_WIDTH];

        for (b = 0; b < 16; b++) {
            short sx = (short)e->x + dx + b;
            int on;
            if (sx < 0 || sx >= VDP_WIDTH)
                continue;
            on = (bits & (0x8000 >> b)) != 0;
            if (negative) {
                /* A 1-bit screen has no second colour for a hit, so the flash
                 * becomes a filled block with the silhouette punched out of
                 * it. White reads as ink, black as paper -- see IS_INK. */
                dst[sx] = on ? 1 : 15;
            } else if (on) {
                dst[sx] = color;
            }
        }
    }
}

void VdpComposite(int monoFlash)
{
    short i;
    CompositeTiles();
    CompositeBigText();
    for (i = 0; i < gSatCount; i++)
        CompositeSprite(&gSat[i], monoFlash);
}

int VdpAnimated(void)
{
    short i;
    for (i = 0; i < gBigCount; i++)
        if (gBigText[i].fg == VDP_FG_PULSE)
            return 1;
    for (i = 0; i < gSatCount; i++)
        if (gSat[i].flags & (SPR_BOB | SPR_FLICKER | SPR_SHAKE))
            return 1;
    /* The stairs-down glint animates through the name table, not the SAT.
     * Chars 8..11 only ever come from RenderMap -- UI text is ASCII. */
    for (i = 0; i < VDP_ROWS * VDP_COLS; i++)
        if (gName[i] >= CHAR_STAIRS_DOWN && gName[i] < CHAR_STAIRS_DOWN + 4)
            return 1;
    return 0;
}

void VdpPackMono(void)
{
    short y, xb, b;
    for (y = 0; y < VDP_HEIGHT; y++) {
        const unsigned char *src = &gPix[y * VDP_WIDTH];
        unsigned char *dst = &gFrame[y * VDP_ROWBYTES];
        for (xb = 0; xb < VDP_ROWBYTES; xb++) {
            unsigned char out = 0;
            for (b = 0; b < 8; b++)
                if (IS_INK(src[b]))
                    out |= (unsigned char)(0x80 >> b);
            *dst++ = out;
            src += 8;
        }
    }
}
