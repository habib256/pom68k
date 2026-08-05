/*
 * rogue_map.c -- the map buffer, tile queries, collision, and the PRNG.
 * ---------------------------------------------------------------------------
 * Transcribed from TMS_Rogue.asm: map_get_a / map_set_x / set_tile /
 * tile_at_target / reveal_pit_at_target / check_collision / handle_input,
 * plus lib/m6502/prng16.asm and rand_mod from lib/games/rogue/dungeon.asm.
 *
 * The cell VALUE is identical to the 6502 nibble -- dense TILE_* in bits
 * 0..2, pit-reveal flag in bit 3 -- only the packing is gone (see rogue.h).
 *
 * The LFSR is bit-exact with the Apple-1 build, so the same seed carves the
 * same dungeon on both machines. That is the port's cheapest regression
 * test and the reason not to "improve" the generator.
 */

#include "rogue.h"

u8 gMap[MAP_CELLS];
u8 gVis[MAP_CELLS];

/* --- 16-bit Galois LFSR, polynomial $B400 (taps 16, 14, 13, 11) ---------- */
static u8 gPrngLo = 1, gPrngHi = 1;

void PrngSeed(unsigned short lo, unsigned short hi)
{
    gPrngLo = (u8)lo;
    gPrngHi = (u8)hi;
    /* A zeroed LFSR stays zero forever. */
    if (gPrngLo == 0 && gPrngHi == 0)
        gPrngLo = 1;
}

u8 Prng16(void)
{
    u8 carry = (u8)(gPrngLo & 1);
    gPrngLo = (u8)((gPrngLo >> 1) | ((gPrngHi & 1) << 7));
    gPrngHi = (u8)(gPrngHi >> 1);
    if (carry)
        gPrngHi ^= 0xB4;
    return gPrngLo;
}

/* Uniform-ish [0, max) by repeated subtraction, exactly as rand_mod did.
 * The residue bias below 1 % for max <= 16 is part of the original's feel;
 * replacing it with a modulo would silently reshuffle every dungeon. */
u8 RandMod(u8 max)
{
    u8 a;
    if (max == 0)
        return 0;               /* rand_mod #0 was undefined on the 6502 */
    a = Prng16();
    while (a >= max)
        a = (u8)(a - max);
    return a;
}

/* --- Map access ---------------------------------------------------------- */

void MapFillWalls(void)
{
    short i;
    for (i = 0; i < MAP_CELLS; i++)
        gMap[i] = TILE_WALL;
}

void ClearVisBuffer(void)
{
    short i;
    for (i = 0; i < MAP_CELLS; i++)
        gVis[i] = 0;
}

u8 MapGet(short idx)
{
    if (idx < 0 || idx >= MAP_CELLS)
        return TILE_WALL;
    return gMap[idx];
}

void MapSet(short idx, u8 nib)
{
    if (idx < 0 || idx >= MAP_CELLS)
        return;
    gMap[idx] = (u8)(nib & 0x0F);
}

/* Dense tile id at (col, row), reveal bit stripped.
 *
 * finalize_doors probes col-1 / col+1 / row-1 / row+1 around corridor cells;
 * the 6502 comment argues those stay inside the grid by construction, and it
 * is right, but off-grid has an unambiguous answer here -- the screen frame
 * is wall -- so the guard costs nothing and closes the case. */
u8 TileAt(short col, short row)
{
    if (col < 0 || col >= LOGICAL_COLS || row < 0 || row >= LOGICAL_ROWS)
        return TILE_WALL;
    return (u8)(gMap[row * LOGICAL_COLS + col] & 7);
}

void SetTile(u8 tile)
{
    if (gTgtCol >= LOGICAL_COLS || gTgtRow >= LOGICAL_ROWS)
        return;
    gMap[gTgtRow * LOGICAL_COLS + gTgtCol] = (u8)(tile & 0x0F);
}

void RevealPitAtTarget(void)
{
    if (gTgtCol >= LOGICAL_COLS || gTgtRow >= LOGICAL_ROWS)
        return;
    gMap[gTgtRow * LOGICAL_COLS + gTgtCol] |= TILE_REVEAL_BIT;
}

/* --- Movement ------------------------------------------------------------ */

/* check_collision. Doors are ALWAYS passable, even the ones sitting on the
 * screen frame: main_loop reads a frame door as "leave this map" and warps
 * instead of walking the player off-grid. TILE_STAIRS_UP is deliberately
 * absent from the whitelist -- climbing back the way you came is a wall. */
int CheckCollision(void)
{
    u8 tile = TileAt(gTgtCol, gTgtRow);

    if (tile == TILE_DOOR)
        return 1;
    if (gTgtRow < PLAY_TOP_ROW || gTgtRow > PLAY_BOT_ROW)
        return 0;
    if (gTgtCol < PLAY_LEFT_COL || gTgtCol > PLAY_RIGHT_COL)
        return 0;
    return (tile == TILE_EMPTY || tile == TILE_STAIRS_DOWN
            || tile == TILE_TRAP_PIT);
}

/* Sets (gTgtCol, gTgtRow) from the player's position plus one cardinal step.
 * Returns 0 when the key is not a movement key. */
int HandleInput(char key)
{
    gTgtCol = gPlayerCol;
    gTgtRow = gPlayerRow;

    switch (key) {
    case KEY_WEST:  gTgtCol--; return 1;
    case KEY_EAST:  gTgtCol++; return 1;
    case KEY_NORTH: gTgtRow--; return 1;
    case KEY_SOUTH: gTgtRow++; return 1;
    default:        return 0;
    }
}
