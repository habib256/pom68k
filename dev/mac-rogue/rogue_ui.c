/*
 * rogue_ui.c -- title, briefing, help, the bag modal, and the two endings.
 * ---------------------------------------------------------------------------
 * Transcribed from TMS_Rogue.asm: draw_title, draw_briefing, wait_kb_choice,
 * show_help, show_inventory, draw_inv_line, draw_inv_utility, death_screen,
 * win_screen, paint_scores, wait_for_restart.
 *
 * Text constraint inherited from the artwork: the pattern table only carries
 * uppercase letters, digits and `+ - . / : ? [ ]`. Lowercase and most
 * punctuation are blank tiles, so every string here is upper-case and
 * parenthesis-free. (The 6502 build printed a lowercase 'x' as the inventory
 * multiplier and it came out invisible on real silicon -- fixed here to 'X'.)
 */

#include "rogue.h"
#include "snd.h"
#include "vdp.h"

/* fg is a TMS palette index; 0 keeps the char-group default. Every value
 * used below is >= 2, so the monochrome ink rule renders these screens
 * exactly as before -- the colour is free on a 1-bit Mac. */
typedef struct { short row, col; unsigned char fg; const char *text; } TextLine;

static void PaintLines(const TextLine *t, short n)
{
    short i;
    for (i = 0; i < n; i++)
        VdpPutStringColor(t[i].row, t[i].col, t[i].text, t[i].fg);
}

/* --- Title ---------------------------------------------------------------- */

static const TextLine kTitle[] = {
    {  5,  9,  6, "--------------" },
    {  7,  4,  7, "MACINTOSH 68K ROGUELIKE" },
    {  9,  7, 14, "BY VERHILLE ARNAUD" },
    { 13,  8,  3, "MOVE IJKL OR ARROWS" },
    { 15,  8, 15, "ANY KEY TO START" },
    { 20,  6, 10, "[H] FOR HELP IN GAME" }
};

void DrawTitle(void)
{
    /* VDP_FG_PULSE: the headline breathes through warm shades on the idle
     * clock while the title waits for its key. Mono sees a steady title.
     * Two lit torches flank it -- flickering AND hovering, so the 1-bit
     * Macs get motion at the title too, not just a recolour they cannot
     * see. (DrawBriefing resets the SAT, so they never outlive the card.) */
    VdpPutStringBig(2, 11, "ROGUE", VDP_FG_PULSE);
    VdpSatAdd(16,  56, SPRITE_NAME_TORCH, COL_TORCH, SPR_FLICKER | SPR_BOB);
    VdpSatAdd(16, 184, SPRITE_NAME_TORCH, COL_TORCH, SPR_FLICKER | SPR_BOB);
    PaintLines(kTitle, (short)(sizeof kTitle / sizeof kTitle[0]));
    ShellPresent();
    SfxPlay(SFX_TITLE);
}

/* --- Briefing -------------------------------------------------------------- */

static const TextLine kBriefing[] = {
    {  5,  4, 10, "[AMULET] FIND IT" },
    {  7,  4, 11, "[STAIRS] GO DOWN 12 FLOORS" },
    {  9,  4, 15, "[SWORD] BUMP MONSTERS" },
    { 11,  4,  3, "[BAG] PICK UP ITEMS" },
    { 13,  4,  9, "[TORCH] LIGHT THE DARK" },
    { 15,  4,  6, "[PIT] WATCH YOUR STEP" },
    { 17,  4, 14, "[SKULL] ONE LIFE" },
    /* ':' not '=': the font has no '=' glyph and it printed as a blank. */
    { 19,  4,  3, "DEPTH 13: ESCAPE" },
    { 22,  9, 14, "PRESS ANY KEY" }
};

void DrawBriefing(void)
{
    VdpSatReset();              /* retire the title torches */
    VdpPutStringBig(1, 9, "MISSION", 11);
    PaintLines(kBriefing, (short)(sizeof kBriefing / sizeof kBriefing[0]));
    ShellPresent();
}

/* --- Help ------------------------------------------------------------------ */

static const TextLine kHelp[] = {
    {  4,  2, 11, "MOVEMENT KEYS" },
    {  5,  4,  0, "I UP J LEFT K DOWN L RIGHT" },
    {  6,  4,  0, "OR THE ARROW KEYS" },
    {  8,  2, 11, "COMMANDS" },
    {  9,  4,  0, "B  BAG OPEN/USE ITEMS" },
    { 10,  4,  0, "T  THROW DAGGER" },
    { 11,  4,  0, "H/?  THIS HELP SCREEN" },
    { 12,  4,  0, ".  REST ONE TURN" },
    { 14,  2, 11, "TIPS" },
    { 15,  4,  0, "BUMP MONSTER TO ATTACK" },
    { 16,  4,  0, "PICKUP IS AUTOMATIC" },
    { 17,  4,  0, "STAIRS DOWN: DESCEND" },
    { 18,  4,  0, "EDGE DOOR: WARP LEVEL" },
    { 19,  4,  0, "REACH DEPTH 13 TO WIN" },
    { 22,  9, 14, "PRESS ANY KEY" }
};

/* Always a free action: the caller does not tick monsters afterwards. */
void ShowHelp(void)
{
    VdpSatReset();              /* hide every gameplay sprite under the card */
    VdpClearNames();
    VdpPutStringBig(1, 12, "HELP", 7);
    PaintLines(kHelp, (short)(sizeof kHelp / sizeof kHelp[0]));
    ShellPresent();
    ShellDrainKeys();
    ShellWaitKey();
    RedrawGame();
}

/* --- Seeding ---------------------------------------------------------------
 * The Apple-1 build counted busy-loop iterations until the first keypress and
 * XORed the key in -- a hardware RNG fed by reaction time. TickCount is the
 * Macintosh's version of the same idea, and mixing the key in keeps different
 * start keys producing divergent dungeons at the same reaction time. */
void WaitKbChoice(void)
{
    char key;
    unsigned long ticks;
    unsigned short lo, hi;

    gBossCheat = 0;
    key = ShellWaitKey();

    ticks = ShellTickCount();
    lo = (unsigned short)(ticks & 0xFF);
    hi = (unsigned short)((ticks >> 8) & 0xFF);
    lo ^= (unsigned short)key;
    if ((lo & 0xFF) == 0 && (hi & 0xFF) == 0)
        lo = 1;                 /* a zeroed LFSR never recovers */
    PrngSeed(lo, hi);

    /* Hidden code: B at the title drops you straight into the depth-13
     * arena with full HP and an empty bag. */
    if (key == 'B')
        gBossCheat = 1;
}

/* --- Inventory modal --------------------------------------------------------
 * Each slot takes TWO display rows because its 16x16 pictogram spans two
 * tile rows: row N carries "[L] QX   NAME UTIL", row N+1 is the bottom half
 * of the sprite. Rows 3..20, so nine slots fit; row 22 is the footer.
 *
 * Typing a letter inside the modal activates that slot directly -- no
 * separate "use" command, which is why the playfield only has B and T. */

static void DrawInvUtility(short row, short col, const InvSlot *s)
{
    switch (s->type) {
    case ITEM_T_WEAPON:
        VdpPutString(row, col, "ATK+");
        VdpSetName(row, col + 4, (u8)('0' + s->value));
        break;
    case ITEM_T_ARMOR:
        VdpPutString(row, col, "DEF+");
        VdpSetName(row, col + 4, (u8)('0' + s->value));
        break;
    case ITEM_T_RING:
        VdpPutString(row, col, "REGEN");
        break;
    case ITEM_T_POTION:
    case ITEM_T_FOOD:
        VdpPutString(row, col, "HP+");
        VdpSetName(row, col + 3, (u8)('0' + s->value));
        break;
    case ITEM_T_SCROLL:
        VdpPutString(row, col, "REVEAL");
        break;
    case ITEM_T_TORCH:
        VdpPutString(row, col, "FOV+");
        VdpSetName(row, col + 4, (u8)('0' + (TORCH_RADIUS - FOV_RADIUS)));
        break;
    default:
        VdpPutString(row, col, "THROW");
        break;
    }
}

static void DrawInvLine(short row, short slot)
{
    const InvSlot *s = &gInv[slot];
    const char *name = LookupItemName(s->type, s->subtype);
    short col = 4;
    u8 qty = s->qty;

    VdpSetName(row, col++, '[');
    VdpSetName(row, col++, (u8)('A' + slot));
    VdpSetName(row, col++, ']');
    VdpSetName(row, col++, ' ');
    /* Display-clamped at 9 so a big pile does not spill past the digits;
     * the real count keeps living in s->qty. */
    if (qty > 9)
        qty = 9;
    VdpSetName(row, col++, (u8)('0' + qty));
    VdpSetName(row, col++, 'X');
    /* Cols 10-11 stay blank: the slot's pictogram is drawn over them. */
    VdpSetName(row, col++, ' ');
    VdpSetName(row, col++, ' ');
    VdpSetName(row, col++, ' ');
    VdpPutString(row, col, name);
    while (*name) { col++; name++; }
    col++;                      /* one-space gap */
    DrawInvUtility(row, col, s);
}

int ShowInventory(void)
{
    short i, row, drawn = 0;
    char key;
    short slot;

    /* Sprite pass first: the whole gameplay SAT is replaced by one pictogram
     * per listed slot, so the player and the monsters vanish for the modal. */
    VdpSatReset();
    row = 3;
    for (i = 0; i < INV_COUNT; i++) {
        if (gInv[i].type == 0 || gInv[i].type == ITEM_T_DAGGER)
            continue;           /* daggers are HUD ammo, never a bag letter */
        if (row >= 21)
            break;
        VdpSatAdd((u8)((row - 1) * 8), 80,
                  kItemSprite[gInv[i].type], kItemColor[gInv[i].type], 0);
        row += 2;
    }

    VdpClearNames();
    VdpPutStringBig(0, 7, "INVENTORY", 10);

    row = 3;
    for (i = 0; i < INV_COUNT; i++) {
        if (gInv[i].type == 0 || gInv[i].type == ITEM_T_DAGGER)
            continue;
        if (row >= 21)
            break;
        DrawInvLine(row, i);
        row += 2;
        drawn++;
    }
    if (drawn == 0)
        VdpPutString(4, 11, "-NOTHING-");

    VdpPutStringColor(22, 9, "PRESS ANY KEY", 14);
    ShellPresent();

    ShellDrainKeys();
    key = ShellWaitKey();

    if (key >= 'A' && key <= 'Z') {
        slot = (short)(key - 'A');
        if (slot < INV_COUNT && gInv[slot].type != 0
            && gInv[slot].type != ITEM_T_DAGGER) {
            int turn = DispatchUseSlot(slot);
            RedrawGame();
            return turn;
        }
    }
    RedrawGame();
    return 0;                   /* dismissed -- free action */
}

/* --- Endings ---------------------------------------------------------------- */

/* Indexed by gLastAttacker: 0 = nothing has bitten you yet, 1..5 the random
 * tiers, 6 the boss, 7 the pit sentinel. */
static const char *kAttackerName[8] = {
    "UNKNOWN", "UNDEAD", "GHOST", "TROLL", "SKELETON", "DEATH",
    "THE DEMON", "A PIT"
};

/* Shared stats block. ATK/DEF BASE are the permanent XP-driven growth --
 * buff timers are long gone by the time an ending paints. */
static void PaintScores(void)
{
    VdpPutStringColor(9, 10, "DEPTH: ", 14);
    PutByte3(9, 17, gDepth);
    VdpPutStringColor(10, 10, "KILLS: ", 14);
    PutByte3(10, 17, gPlayerXp);
    VdpPutStringColor(11, 10, "HP MAX: ", 14);
    PutByte2(11, 18, gHpMax);
    VdpPutStringColor(12, 10, "ATK BASE: ", 14);
    PutByte2(12, 20, (u8)(gXpAtkBonus + 1));    /* +1 bare-handed base */
    VdpPutStringColor(13, 10, "DEF BASE: ", 14);
    PutByte2(13, 20, gXpDefBonus);
    VdpPutStringColor(16, 9, "PRESS ANY KEY", 14);
}

/* ~1.3 s of deaf time, so a movement key still held from the killing turn
 * cannot skip the screen, then an explicit acknowledgement. */
static void WaitForRestart(void)
{
    ShellPresent();
    ShellDelayTicks(78);
    ShellDrainKeys();
    ShellWaitKey();
    ColdStart(COLD_RESTART);    /* never returns */
}

void DeathScreen(void)
{
    short who = gLastAttacker;
    if (who < 0 || who > 7)
        who = 0;

    SfxPlay(SFX_DEATH);
    VdpSatReset();
    VdpClearNames();
    VdpPutStringBig(2, 7, "GAME OVER", VDP_FG_PULSE);
    VdpPutString(6, 5, "YOU DIED ON LEVEL ");
    PutByte3(6, 23, gDepth);
    VdpPutString(7, 7, "KILLED BY ");
    VdpPutStringColor(7, 17, kAttackerName[who], 8);
    PaintScores();
    WaitForRestart();
}

void WinScreen(void)
{
    SfxPlay(SFX_WIN);
    VdpSatReset();
    VdpClearNames();
    /* 15 chars * 2 cells = 30 cols: the widest string the big face fits. */
    VdpPutStringBig(2, 1, "CONGRATULATIONS", VDP_FG_PULSE);
    VdpPutStringColor(6, 7, "DUNGEON CONQUERED", 3);
    /* The amulet you came for, hovering under the scores. */
    VdpSatAdd(148, 120, SPRITE_NAME_RING, COL_RING, SPR_BOB);
    PaintScores();
    WaitForRestart();
}
