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

typedef struct { short row, col; const char *text; } TextLine;

static void PaintLines(const TextLine *t, short n)
{
    short i;
    for (i = 0; i < n; i++)
        VdpPutString(t[i].row, t[i].col, t[i].text);
}

/* --- Title ---------------------------------------------------------------- */

static const TextLine kTitle[] = {
    {  4, 13, "ROGUE" },
    {  7,  4, "MACINTOSH 68K ROGUELIKE" },
    {  9,  7, "BY VERHILLE ARNAUD" },
    { 13,  8, "MOVE IJKL OR ARROWS" },
    { 15,  8, "ANY KEY TO START" },
    { 20,  6, "[H] FOR HELP IN GAME" }
};

void DrawTitle(void)
{
    PaintLines(kTitle, (short)(sizeof kTitle / sizeof kTitle[0]));
    ShellPresent();
    SfxPlay(SFX_TITLE);
}

/* --- Briefing -------------------------------------------------------------- */

static const TextLine kBriefing[] = {
    {  2, 13, "MISSION" },
    {  4,  4, "[AMULET] FIND IT" },
    {  7,  4, "[STAIRS] GO DOWN 12 FLOORS" },
    { 10,  4, "[SWORD] BUMP MONSTERS" },
    { 12,  4, "[BAG] PICK UP ITEMS" },
    { 14,  4, "[TORCH] LIGHT THE DARK" },
    { 16,  4, "[PIT] WATCH YOUR STEP" },
    { 18,  4, "[SKULL] ONE LIFE" },
    { 20,  4, "DEPTH 13 = ESCAPE" },
    { 22,  9, "PRESS ANY KEY" }
};

void DrawBriefing(void)
{
    PaintLines(kBriefing, (short)(sizeof kBriefing / sizeof kBriefing[0]));
    ShellPresent();
}

/* --- Help ------------------------------------------------------------------ */

static const TextLine kHelp[] = {
    {  1, 14, "HELP" },
    {  3,  2, "MOVEMENT KEYS" },
    {  4,  4, "I UP J LEFT K DOWN L RIGHT" },
    {  5,  4, "OR THE ARROW KEYS" },
    {  7,  2, "COMMANDS" },
    {  8,  4, "B  BAG OPEN/USE ITEMS" },
    {  9,  4, "T  THROW DAGGER" },
    { 10,  4, "H/?  THIS HELP SCREEN" },
    { 11,  4, ".  REST ONE TURN" },
    { 14,  2, "TIPS" },
    { 15,  4, "BUMP MONSTER TO ATTACK" },
    { 16,  4, "PICKUP IS AUTOMATIC" },
    { 17,  4, "STAIRS DOWN: DESCEND" },
    { 18,  4, "EDGE DOOR: WARP LEVEL" },
    { 19,  4, "REACH DEPTH 13 TO WIN" },
    { 22,  9, "PRESS ANY KEY" }
};

/* Always a free action: the caller does not tick monsters afterwards. */
void ShowHelp(void)
{
    VdpSatReset();              /* hide every gameplay sprite under the card */
    VdpClearNames();
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
    VdpPutString(1, 11, "INVENTORY");

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

    VdpPutString(22, 9, "PRESS ANY KEY");
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
    VdpPutString(9, 10, "DEPTH: ");
    PutByte3(9, 17, gDepth);
    VdpPutString(10, 10, "KILLS: ");
    PutByte3(10, 17, gPlayerXp);
    VdpPutString(11, 10, "HP MAX: ");
    PutByte2(11, 18, gHpMax);
    VdpPutString(12, 10, "ATK BASE: ");
    PutByte2(12, 20, (u8)(gXpAtkBonus + 1));    /* +1 bare-handed base */
    VdpPutString(13, 10, "DEF BASE: ");
    PutByte2(13, 20, gXpDefBonus);
    VdpPutString(16, 9, "PRESS ANY KEY");
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
    VdpPutString(3, 11, "GAME OVER");
    VdpPutString(6, 5, "YOU DIED ON LEVEL ");
    PutByte3(6, 23, gDepth);
    VdpPutString(7, 7, "KILLED BY ");
    VdpPutString(7, 17, kAttackerName[who]);
    PaintScores();
    WaitForRestart();
}

void WinScreen(void)
{
    SfxPlay(SFX_WIN);
    VdpSatReset();
    VdpClearNames();
    VdpPutString(3, 8, "CONGRATULATIONS");
    VdpPutString(6, 7, "DUNGEON CONQUERED");
    PaintScores();
    WaitForRestart();
}
