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

typedef struct {
    unsigned char y, x;             /* pixel position of the top-left corner */
    unsigned char name;             /* sprite slot; 16x16 mode uses steps of 4 */
    unsigned char color;            /* TMS palette index (kept for the colour path) */
    unsigned char flags;            /* SPR_* */
} SatEntry;

extern unsigned char gName[VDP_ROWS * VDP_COLS];
extern SatEntry      gSat[VDP_SAT_MAX];
extern short         gSatCount;

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

void VdpSatReset(void);
void VdpSatAdd(unsigned char y, unsigned char x,
               unsigned char name, unsigned char color, unsigned char flags);

/* Rebuild gPix from the name table and the SAT. `monoFlash` asks for the
 * hit-flash to be rendered as a punched-out negative, which is the only way
 * a 1-bit screen can show it; a colour screen passes 0 and gets COL_HURT. */
void VdpComposite(int monoFlash);

/* Pack gPix down to the 1-bit gFrame using the ink rule. */
void VdpPackMono(void);

#endif /* VDP_H */
