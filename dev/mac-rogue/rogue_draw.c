/*
 * rogue_draw.c -- painting the playfield, the sprites and the HUD.
 * ---------------------------------------------------------------------------
 * Transcribed from TMS_Rogue.asm: render_map, emit_tl_cell / emit_bl_cell,
 * place_all_sprites, update_hud and its three fall-through halves,
 * print_byte_2/3digits, clear_msg_rows, print_msg_row, redraw_game.
 *
 * Screen layout, unchanged from the TMS9918 build:
 *   char rows  0..19  playfield -- 16x10 logical tiles, each drawn as a 2x2
 *                     block of 8x8 chars, so one logical cell is 16x16 px
 *                     and a 16x16 sprite sits exactly on top of it
 *   char rows 20..23  HUD
 */

#include "rogue.h"
#include "vdp.h"

/* Dense TILE_* -> pattern-table char base. Slots 6 and 7 are the transient
 * corridor markers; finalize_doors clears them long before anything renders,
 * but mapping them to blank means a half-carved buffer draws cleanly instead
 * of flashing garbage. */
static const u8 kTileCharBase[8] = {
    CHAR_EMPTY, CHAR_WALL, CHAR_DOOR, CHAR_STAIRS_DOWN,
    CHAR_STAIRS_UP, CHAR_TRAP_PIT, CHAR_EMPTY, CHAR_EMPTY
};

/* --- Playfield ------------------------------------------------------------ */

void RenderMap(void)
{
    short r, c;

    for (r = 0; r < LOGICAL_ROWS; r++) {
        for (c = 0; c < LOGICAL_COLS; c++) {
            short idx = r * LOGICAL_COLS + c;
            short nr = r * 2, nc = c * 2;
            u8 nib, id, base;

            if (!gVis[idx]) {
                /* Out of the torchlight: four blanks. Corridors the
                 * generator carved stay black until you actually see them. */
                VdpSetName(nr,     nc,     0);
                VdpSetName(nr,     nc + 1, 0);
                VdpSetName(nr + 1, nc,     0);
                VdpSetName(nr + 1, nc + 1, 0);
                continue;
            }

            nib = gMap[idx];
            id  = (u8)(nib & 7);

            /* A hidden pit draws as plain floor -- that is the whole trap. */
            if (id == TILE_TRAP_PIT && !(nib & TILE_REVEAL_BIT)) {
                VdpSetName(nr,     nc,     0);
                VdpSetName(nr,     nc + 1, 0);
                VdpSetName(nr + 1, nc,     0);
                VdpSetName(nr + 1, nc + 1, 0);
                continue;
            }

            base = kTileCharBase[id];
            VdpSetName(nr,     nc,     (u8)(base + 0));   /* TL */
            VdpSetName(nr,     nc + 1, (u8)(base + 1));   /* TR */
            VdpSetName(nr + 1, nc,     (u8)(base + 2));   /* BL */
            VdpSetName(nr + 1, nc + 1, (u8)(base + 3));   /* BR */
        }
    }
}

/* --- Sprites ---------------------------------------------------------------
 * Logical (col, row) -> pixel (col*16, row*16), so every entity rides the
 * same 16x16 grid the tiles are drawn on. Items are emitted LAST, which on
 * the real chip gave them the lowest priority: a monster or the player
 * standing on a drop covers it -- "loot under your feet". The software
 * compositor draws in the same order, so the look survives. */
void PlaceAllSprites(void)
{
    short i;

    VdpSatReset();

    /* Slot 0: the player. A hit is COL_HURT on a colour screen and a
     * punched-out negative on a 1-bit one; the compositor picks, so both
     * the colour and the flag travel together. */
    VdpSatAdd((u8)(gPlayerRow * 16), (u8)(gPlayerCol * 16),
              SPRITE_NAME_PLAYER,
              (u8)(gPlayerHurt ? COL_HURT : COL_PLAYER),
              (u8)(gPlayerHurt ? SPR_INVERT : 0));

    /* The dagger quiver icon used to sit on rows 20-21 at the right edge;
     * row 21 is the message line now, so it joins the buff icons on the
     * bottom strip, after the four timers. */
    VdpSatAdd(176, 128, SPRITE_NAME_DAGGER, COL_DAGGER, 0);

    /* Live monsters, FOV-gated: one in a dark cell is dropped entirely, so
     * nothing leaks through the fog. */
    for (i = 0; i < MON_COUNT; i++) {
        Monster *m = &gMon[i];
        u8 flags, color;

        if (m->type == 0)
            continue;
        if (!gVis[(short)m->row * LOGICAL_COLS + m->col])
            continue;           /* dark anchor -> drop the whole thing */

        flags = (u8)(m->hurt ? SPR_INVERT : 0);
        color = (u8)(m->hurt ? COL_HURT : m->color);

        if (m->type == MON_TYPE_BOSS) {
            u8 top  = (u8)(m->row * 16);
            u8 left = (u8)(m->col * 16);
            /* 32x32 demon = four tiled 16x16 slots around one anchor. */
            VdpSatAdd(top,             left,             SPRITE_NAME_BOSS_TL, color, flags);
            VdpSatAdd(top,             (u8)(left + 16),  SPRITE_NAME_BOSS_TR, color, flags);
            VdpSatAdd((u8)(top + 16),  left,             SPRITE_NAME_BOSS_BL, color, flags);
            VdpSatAdd((u8)(top + 16),  (u8)(left + 16),  SPRITE_NAME_BOSS_BR, color, flags);
        } else {
            VdpSatAdd((u8)(m->row * 16), (u8)(m->col * 16),
                      m->name, color, flags);
        }
    }

    /* Floor items, same FOV gate. */
    for (i = 0; i < ITEM_COUNT; i++) {
        WorldItem *w = &gItem[i];
        if (w->type == 0)
            continue;
        if (!gVis[(short)w->row * LOGICAL_COLS + w->col])
            continue;
        VdpSatAdd((u8)(w->row * 16), (u8)(w->col * 16),
                  kItemSprite[w->type], kItemColor[w->type], 0);
    }

    /* Active buff icons along the bottom, left-packed; the matching
     * countdowns are painted to the cells just right of each one. */
    if (gWeaponTimer) VdpSatAdd(176,  0, SPRITE_NAME_WEAPON, COL_WEAPON, 0);
    if (gArmorTimer)  VdpSatAdd(176, 32, SPRITE_NAME_ARMOR,  COL_ARMOR,  0);
    if (gRingTimer)   VdpSatAdd(176, 64, SPRITE_NAME_RING,   COL_RING,   0);
    if (gTorchTimer)  VdpSatAdd(176, 96, SPRITE_NAME_TORCH,  COL_TORCH,  0);

    /* A dagger in mid-flight. */
    if (gThrowActive)
        VdpSatAdd((u8)(gCurY * 16), (u8)(gCurX * 16),
                  SPRITE_NAME_DAGGER, COL_DAGGER, 0);
}

/* --- Numbers --------------------------------------------------------------
 * The font glyphs sit at their ASCII ids, so '0' + digit is the char id. */

void PutByte3(short row, short col, u8 v)
{
    VdpSetName(row, col,     (u8)('0' + v / 100));
    VdpSetName(row, col + 1, (u8)('0' + (v / 10) % 10));
    VdpSetName(row, col + 2, (u8)('0' + v % 10));
}

void PutByte2(short row, short col, u8 v)
{
    VdpSetName(row, col,     (u8)('0' + (v / 10) % 10));
    VdpSetName(row, col + 1, (u8)('0' + v % 10));
}

/* --- HUD -------------------------------------------------------------------
 *   row 20  DEPTH NNN                              [dagger icon spans 20-21]
 *   row 21  ATK:NN DEF:NN                                                NN
 *   row 22                                                       HP HH/HM
 *   row 23  nn  nn  nn  nn                                          XP:NNN
 *           ^wpn ^arm ^rng ^trc  (icons sit on rows 22-23 to their left)
 */
void UpdateHud(void)
{
    /* Row 20 packs every live stat into exactly 32 columns, which is what
     * freed row 21 for the message line:
     *   DEPTH 002 ATK:03 DEF:01 HP 11/15
     *   0     6   10  14 17  21 24 27 30 */
    VdpFillRow(20, 0, VDP_COLS, ' ');
    VdpPutString(20, 0, "DEPTH ");
    PutByte3(20, 6, gDepth);
    VdpPutString(20, 10, "ATK:");
    PutByte2(20, 14, gPlayerDmg);
    VdpPutString(20, 17, "DEF:");
    PutByte2(20, 21, gPlayerDef);
    VdpPutString(20, 24, "HP ");
    PutByte2(20, 27, gHp);
    VdpSetName(20, 29, '/');
    PutByte2(20, 30, gHpMax);

    /* Row 21: what just happened. Empty most turns, and that is fine -- an
     * empty message line is how a roguelike says "nothing to report". */
    VdpFillRow(21, 0, VDP_COLS, ' ');
    VdpPutString(21, 0, MsgLine());

    /* Row 22 is mostly the top half of the buff icons (sprites, cols 0-17);
     * only the right end carries text. */
    VdpFillRow(22, 18, 14, ' ');
    VdpPutString(22, 26, "XP:");
    PutByte3(22, 29, gPlayerXp);

    /* Row 23: each countdown immediately right of its icon, and the dagger
     * quiver after them. */
    VdpFillRow(23, 2, 30, ' ');
    if (gWeaponTimer) PutByte2(23,  2, gWeaponTimer);
    if (gArmorTimer)  PutByte2(23,  6, gArmorTimer);
    if (gRingTimer)   PutByte2(23, 10, gRingTimer);
    if (gTorchTimer)  PutByte2(23, 14, gTorchTimer);
    PutByte2(23, 18, gDaggerQty);
}

/* Wipe any stale one-line prompt ("NO DAGGER", "DIRECTION?") before the HUD
 * repaints over it. Order matters -- doing this after update_hud would erase
 * the timers and XP it just wrote. */
void ClearMsgRows(void)
{
    VdpFillRow(22, 0, VDP_COLS, ' ');
    VdpFillRow(23, 0, VDP_COLS, ' ');
}

void ClearMsgRow23(void)
{
    VdpFillRow(23, 0, VDP_COLS, ' ');
}

void PrintMsgRow(short row, const char *s)
{
    VdpPutString(row, 0, s);
    ShellPresent();
}

/* Full repaint used on every modal exit. compute_fov is NOT re-run: nothing
 * moved while the modal was up, so visibility is unchanged. */
void RedrawGame(void)
{
    VdpClearNames();
    RenderMap();
    PlaceAllSprites();
    UpdateHud();
    ShellPresent();
}
