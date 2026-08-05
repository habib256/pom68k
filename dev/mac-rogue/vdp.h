/*
 * vdp.h -- a software TMS9918 Graphics-I display, in 200 lines of C.
 * ---------------------------------------------------------------------------
 * The 6502 original drove a real VDP: it wrote char ids into a 32x24 name
 * table at VRAM $1800 and 4-byte entries into a Sprite Attribute Table at
 * $1B00, and the chip composited. This port keeps *exactly* that interface
 * and does the compositing itself, which is why the game logic could be
 * transcribed line-for-line instead of rewritten around QuickDraw.
 *
 *   name table  ->  VdpSetName / VdpPutString / VdpClearNames
 *   SAT         ->  VdpSatReset / VdpSatAdd
 *   the chip    ->  VdpComposite (name table + sprites -> 1-bit frame)
 *
 * Mode I geometry: 32x24 cells of 8x8 px = 256x192, sprites 16x16 with no
 * magnification. The frame is 1 bpp so it blits to every Macintosh ever
 * made, from the Plus to the Quadra 950 -- see README "Colour".
 */

#ifndef VDP_H
#define VDP_H

#define VDP_COLS      32
#define VDP_ROWS      24
#define VDP_WIDTH     256
#define VDP_HEIGHT    192
#define VDP_ROWBYTES  32            /* 256 px / 8 px per byte */
#define VDP_SAT_MAX   48            /* the chip had 32; nothing here needs a cap */

/* Sprite flags. The TMS9918 recoloured a sprite to signal a hit; a 1-bit
 * Macintosh has no second colour, so the flash becomes an inverted
 * silhouette -- the cell fills with ink and the sprite is punched out of
 * it. Reads as a flash, costs one bit of state. */
#define SPR_INVERT    0x01
/* Idle animation, driven by gVdpPhase (the shell advances it ~4x a second
 * while the game waits for a key). BOB floats the sprite up to 2 px above
 * its anchor -- position moves, so it reads on a 1-bit screen too. FLICKER
 * cycles the colour through flame shades; every shade is ink, so monochrome
 * sees no change and nothing needs a special case. */
#define SPR_BOB       0x02
#define SPR_FLICKER   0x04
/* SHAKE trembles the silhouette 1 px left/right on alternate phases -- the
 * body language of a fresh wound. It rides the same SAT entry as the hurt
 * colour/invert, so it starts and stops exactly with the flash. */
#define SPR_SHAKE     0x08

/* Big-text fg sentinel: cycle warm shades on the idle clock instead of a
 * fixed palette index. All shades are ink -- invisible-identical in mono. */
#define VDP_FG_PULSE  0xFF

typedef struct {
    unsigned char y, x;             /* pixel position of the top-left corner */
    unsigned char name;             /* sprite slot; 16x16 mode uses steps of 4 */
    unsigned char color;            /* TMS palette index (kept for the colour path) */
    unsigned char flags;            /* SPR_* */
} SatEntry;

extern unsigned char gName[VDP_ROWS * VDP_COLS];
extern SatEntry      gSat[VDP_SAT_MAX];
extern short         gSatCount;

/* The idle-animation clock. The shell owns the cadence (it ticks this from
 * the event pump); the compositor only ever reads it. Wrapping is free --
 * every consumer masks it down to a small cycle. */
extern unsigned char gVdpPhase;

/* The composited frame, one TMS palette index per pixel. This is the real
 * output of the chip; the two presenters below are just ways of getting it
 * onto whichever Macintosh happens to be running. */
extern unsigned char gPix[VDP_HEIGHT * VDP_WIDTH];

/* The 1-bit packing of gPix, ready for a BitMap on a monochrome screen. */
extern unsigned char gFrame[VDP_HEIGHT * VDP_ROWBYTES];

/* TMS9918A palette as 8-bit RGB. Index 0 is transparent (the backdrop shows
 * through); index 1 is black, which is this game's backdrop, so the two are
 * given the same colour here and nothing else has to special-case it. */
extern const unsigned char kTmsPaletteRGB[16][3];

void VdpClearNames(void);
void VdpSetName(short row, short col, unsigned char ch);
/* Writes a NUL-terminated string left to right; the pattern table carries
 * font glyphs at their matching ASCII ids, so plain C strings just work. */
void VdpPutString(short row, short col, const char *s);
void VdpFillRow(short row, short col, short count, unsigned char ch);

/* Same as VdpPutString, but the cells take `fg` instead of the char's group
 * colour (fg 0 = keep the default). Real hardware could not do this -- one
 * colour per eight chars -- but the UI cards are not the dungeon, and on a
 * 1-bit screen the ink rule makes any fg >= 2 invisible-identical anyway. */
void VdpPutStringColor(short row, short col, const char *s, unsigned char fg);

/* Double-size text: each glyph is pixel-doubled to 16x16, so a string takes
 * two cell rows and two cell columns per char. Composited over the tiles,
 * under the sprites; only set pixels paint (the background shows through).
 * The pointer is kept, not copied -- pass string literals. The overlay list
 * is cleared by VdpClearNames along with the name table. */
void VdpPutStringBig(short row, short col, const char *s, unsigned char fg);

void VdpSatReset(void);
void VdpSatAdd(unsigned char y, unsigned char x,
               unsigned char name, unsigned char color, unsigned char flags);

/* Rebuild gPix from the name table and the SAT. `monoFlash` asks for the
 * hit-flash to be rendered as a punched-out negative, which is the only way
 * a 1-bit screen can show it; a colour screen passes 0 and gets COL_HURT. */
void VdpComposite(int monoFlash);

/* Pack gPix down to the 1-bit gFrame using the ink rule. */
void VdpPackMono(void);

/* 1 if the current scene carries any phase-driven element (a BOB/FLICKER
 * sprite or a pulsing headline). The shell asks before spending a whole
 * composite-and-blit on an idle frame -- a static scene costs nothing. */
int VdpAnimated(void);

#endif /* VDP_H */
