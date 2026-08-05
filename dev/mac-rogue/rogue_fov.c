/*
 * rogue_fov.c -- recursive shadowcasting field of view.
 * ---------------------------------------------------------------------------
 * Björn Bergström's algorithm (RogueBasin, 2002), transcribed from POM1's
 * lib/games/rogue/shadowcast.asm plus compute_fov and
 * strip_invisible_pit_reveals in TMS_Rogue.asm.
 *
 * The plane around the player is cut into 8 octants; each is scanned row by
 * row at increasing depth, columns from the outer edge (slope ~1) inward to
 * slope 0. Every cell's left and right slopes are tested against the live
 * cone by cross-multiplication; a floor->wall transition recurses one row
 * deeper with the cone narrowed to the wall's left slope, and the wall's
 * right slope becomes the new cone start when the row crosses back to floor.
 * The result is symmetric -- you see a cell exactly when it can see you --
 * with no slope artifacts and no holes.
 *
 * The 6502 needed a 4x4 -> 8-bit multiply to keep the cross-products in one
 * byte; here they are plain ints. Every other decision, including the
 * comparison tie-breaks, follows the assembly exactly, because those
 * tie-breaks are what make the lit region look the way it does.
 *
 * Torchlight only -- no remembered terrain. compute_fov wipes the buffer on
 * every call, so a cell that leaves the radius goes dark immediately.
 */

#include "rogue.h"

/* Canonical (col, depth) -> grid offset, one row per octant. Each row has
 * exactly one non-zero in {xx, xy} and one in {yx, yy}; together the eight
 * partition the plane without overlap. */
static const s8 kOctXX[8] = {  0,  1, -1,  0,  0, -1,  1,  0 };
static const s8 kOctXY[8] = {  1,  0,  0, -1, -1,  0,  0,  1 };
static const s8 kOctYX[8] = { -1,  0,  0, -1,  1,  0,  0,  1 };
static const s8 kOctYY[8] = {  0, -1, -1,  0,  0,  1,  1,  0 };

static s8 oct_xx, oct_xy, oct_yx, oct_yy;

static void MarkVisibleAtCur(void)
{
    gVis[(short)gCurY * LOGICAL_COLS + gCurX] = 1;
}

/* Walls AND doors block sight. A door is the threshold of a new room, and
 * the whole point of "the fog comes back at every door" is that you cannot
 * peek through one from the corridor: the ray stops AT the door (which is
 * itself marked visible on the same step) and the room beyond stays dark
 * until you walk in. Doors remain passable -- opacity is purely sight. */
static int IsOpaqueAtCur(void)
{
    u8 t = (u8)(gMap[(short)gCurY * LOGICAL_COLS + gCurX] & 7);
    return (t == TILE_WALL || t == TILE_DOOR);
}

static void CastOctant(short depth, short startN, short startD,
                       short endN, short endD)
{
    /* Empty cone: start <= end. */
    if (startN * endD <= endN * startD)
        return;

    for (; depth <= gFovR; depth++) {
        short col;
        int   blocked = 0;
        short saveN = startN, saveD = startD;

        for (col = depth;; col--) {
            short lslopeN = (short)(2 * col + 1);
            short lslopeD = (short)(2 * depth - 1);
            short rslopeN = (col == 0) ? 0 : (short)(2 * col - 1);
            short rslopeD = (short)(2 * depth + 1);
            short cx, cy;

            /* Outside the cone on the steep side -- not reached yet. */
            if (rslopeN * startD > startN * rslopeD)
                goto next_col;
            /* Past the shallow edge of the cone -- the row is done. */
            if (endN * lslopeD > lslopeN * endD)
                break;

            cx = (short)(gPlayerCol + col * oct_xx + depth * oct_xy);
            cy = (short)(gPlayerRow + col * oct_yx + depth * oct_yy);

            if (cx < 0 || cx >= LOGICAL_COLS || cy < 0 || cy >= LOGICAL_ROWS) {
                /* Off-grid counts as wall for the state machine, but there
                 * is nothing to light and nothing behind it to recurse into. */
                blocked = 1;
                saveN = rslopeN;
                saveD = rslopeD;
                goto next_col;
            }

            gCurX = (u8)cx;
            gCurY = (u8)cy;
            MarkVisibleAtCur();

            if (IsOpaqueAtCur()) {
                if (!blocked) {
                    blocked = 1;
                    /* Floor -> wall: everything the wall occludes at deeper
                     * rows lies below its left slope. */
                    if (depth < gFovR)
                        CastOctant((short)(depth + 1), startN, startD,
                                   lslopeN, lslopeD);
                }
                /* Latest wall in the chain wins the pending new start. */
                saveN = rslopeN;
                saveD = rslopeD;
            } else if (blocked) {
                /* Wall -> floor: the cone reopens at the last wall's right
                 * slope. */
                blocked = 0;
                startN = saveN;
                startD = saveD;
            }

        next_col:
            if (col == 0)
                break;
        }

        /* An unbroken wall chain across the cone tail shadows every deeper
         * row -- nothing left to scan in this octant. */
        if (blocked)
            return;
    }
}

/* Revealed pits that drift out of the torchlight go back into hiding, so
 * the player has to remember where they were (or step in again). */
static void StripInvisiblePitReveals(void)
{
    short i;
    for (i = MAP_CELLS - 1; i >= 0; i--) {
        if (!(gMap[i] & TILE_REVEAL_BIT))
            continue;
        if (gVis[i])
            continue;
        gMap[i] &= (u8)~TILE_REVEAL_BIT;
    }
}

void ComputeFov(void)
{
    short o;

    gFovR = gTorchTimer ? TORCH_RADIUS : FOV_RADIUS;

    ClearVisBuffer();

    /* The octant scan starts at depth 1, so the player's own cell needs its
     * own seed. */
    gCurX = gPlayerCol;
    gCurY = gPlayerRow;
    MarkVisibleAtCur();

    for (o = 0; o < 8; o++) {
        oct_xx = kOctXX[o];
        oct_xy = kOctXY[o];
        oct_yx = kOctYX[o];
        oct_yy = kOctYY[o];
        CastOctant(1, 1, 1, 0, 1);
    }

    StripInvisiblePitReveals();
}
