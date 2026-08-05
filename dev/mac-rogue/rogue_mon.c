/*
 * rogue_mon.c -- the bestiary: per-type AI, movement, combat and XP.
 * ---------------------------------------------------------------------------
 * Transcribed from TMS_Rogue.asm: monster_at_target, player_attack_monster,
 * award_xp, move_monsters, step_monster and the five AIs, try_step_*,
 * try_flee_*, apply_step, the boss 2x2 variants, boss_cell_passable,
 * compute_abs_deltas, clear_hurt_flags, recompute_player_stats, trigger_pit.
 *
 * The AI split is deliberately Pac-Man-shaped -- each undead reads as a
 * behaviour, not a difficulty number:
 *   UNDEAD   greedy chase on the longer axis
 *   GHOST    uniform random walk; it bites you by accident
 *   TROLL    flees until you hit it, then hunts you forever
 *   SKELETON chases on the SHORTER axis, so it flanks instead of charging
 *   DEATH    coin-flips whether to act at all, but hits hard when it does
 *   BOSS     greedy chase with a 2x2 footprint
 */

#include "rogue.h"
#include "snd.h"

Monster gMon[MON_COUNT];

static u8 mon_abs_dx, mon_abs_dy;

/* Indexed by MON_TYPE. Slot 0 covers "nothing has bitten you yet" and slot 7
 * is the pit sentinel, so the death screen can share the table. */
static const char *kMonName[8] = {
    "UNKNOWN", "UNDEAD", "GHOST", "TROLL", "SKELETON", "DEATH",
    "THE DEMON", "A PIT"
};

const char *MonName(u8 type)
{
    return (type <= 7) ? kMonName[type] : kMonName[0];
}

/* --- Queries -------------------------------------------------------------- */

/* Index of the live monster covering (gTgtCol, gTgtRow), or -1. The boss
 * answers for all four cells of its footprint. */
short MonsterAtTarget(void)
{
    short i;
    for (i = 0; i < MON_COUNT; i++) {
        Monster *m = &gMon[i];
        if (m->type == 0)
            continue;
        if (m->type == MON_TYPE_BOSS) {
            short dc = (short)gTgtCol - (short)m->col;
            short dr = (short)gTgtRow - (short)m->row;
            if (dc >= 0 && dc < 2 && dr >= 0 && dr < 2)
                return i;
        } else if (m->col == gTgtCol && m->row == gTgtRow) {
            return i;
        }
    }
    return -1;
}

/* |player - monster| on both axes. Returns 0 when the monster is standing on
 * the player, which never happens in play but the 6502 guarded it too. */
static int ComputeAbsDeltas(const Monster *m)
{
    mon_abs_dx = (u8)(gPlayerCol > m->col ? gPlayerCol - m->col
                                          : m->col - gPlayerCol);
    mon_abs_dy = (u8)(gPlayerRow > m->row ? gPlayerRow - m->row
                                          : m->row - gPlayerRow);
    return (mon_abs_dx | mon_abs_dy) != 0;
}

/* --- Player stats --------------------------------------------------------- */

/* player_dmg = (weapon buff active ? its boost : 1 bare-handed) + XP ATK
 * player_def = (armor buff active  ? its boost : 0 unarmored)  + XP DEF */
void RecomputePlayerStats(void)
{
    gPlayerDmg = (u8)((gWeaponTimer ? gWeaponBoost : 1) + gXpAtkBonus);
    gPlayerDef = (u8)((gArmorTimer ? gArmorBoost : 0) + gXpDefBonus);
}

/* --- Pits ----------------------------------------------------------------- */

/* Not consumed: every step onto a pit costs HP again. Caller has left the
 * destination cell in (gTgtCol, gTgtRow). */
void TriggerPit(void)
{
    gHp = (u8)(gHp > PIT_DMG ? gHp - PIT_DMG : 0);
    gPlayerHurt = 1;
    gLastAttacker = LAST_ATTACKER_PIT;
    Msg("YOU FALL IN A PIT");
    SfxPlay(SFX_PIT);
    RevealPitAtTarget();
}

/* --- XP ------------------------------------------------------------------- */

/* +1 kill, then three independent countdowns fire their bonus and reload:
 * +1 hp_max every 5, +1 ATK every 10, +1 DEF every 20. Keeping counters
 * instead of dividing player_xp is how the 6502 avoided a division per kill;
 * it is kept because the reload points are the tuning. */
void AwardXp(void)
{
    if (gPlayerXp == 0xFF)
        return;
    gPlayerXp++;

    if (--gHpTick == 0) {
        gHpTick = 5;
        gHpMax++;
        if (gHp < gHpMax)
            gHp++;              /* small heal-on-up */
        Msg("YOU FEEL TOUGHER");
        SfxPlay(SFX_LEVELUP);
    }
    if (--gAtkTick == 0) {
        gAtkTick = 10;
        gXpAtkBonus++;
        RecomputePlayerStats();
        Msg("YOU FEEL STRONGER");
        SfxPlay(SFX_LEVELUP);
    }
    if (--gDefTick == 0) {
        gDefTick = 20;
        gXpDefBonus++;
        RecomputePlayerStats();
        Msg("YOUR SKIN HARDENS");
        SfxPlay(SFX_LEVELUP);
    }
}

/* --- Player -> monster ----------------------------------------------------- */

void PlayerAttackMonster(short i)
{
    Monster *m = &gMon[i];

    m->hp = (u8)(m->hp > gPlayerDmg ? m->hp - gPlayerDmg : 0);
    if (m->hp) {
        m->hurt = 1;
        Msg2("YOU HIT THE", MonName(m->type));
        SfxPlay(SFX_HIT);
        return;
    }
    Msg2("YOU KILL THE", MonName(m->type));
    SfxPlay(SFX_KILL);

    /* Killed. Stash the corpse cell before freeing the slot. */
    gTgtCol = m->col;
    gTgtRow = m->row;

    if (m->type == MON_TYPE_BOSS) {
        m->type = 0;
        AwardXp();
        WinScreen();            /* never returns */
        return;
    }

    m->type = 0;
    AwardXp();
    /* 25 % food drop. Tuned down from 50 %: combined with level-spawned food
     * and +1 HP every 5 kills, a higher rate let the player hoard enough
     * healing to ignore positioning entirely. */
    if (RandMod(4) == 0)
        SpawnTypedItem(ITEM_T_FOOD, SUB_FOOD_RATION);
}

/* --- Monster -> world ------------------------------------------------------ */

/* Shared tail of every one-cell step. Returns 1 when the monster spent its
 * turn (bit the player, or moved), 0 when blocked so the caller can try the
 * other axis. */
static int ApplyStep(short i)
{
    Monster *m = &gMon[i];
    u8 tile;

    if (gTgtCol == gPlayerCol && gTgtRow == gPlayerRow) {
        /* Bite. Armour subtracts from every hit, with a hard floor at 0 --
         * which is exactly why the tunic is DEF 1 and not 2: at 2 every
         * 1-damage monster became harmless and depths 1-5 went flat. */
        u8 dmg = (u8)(m->dmg > gPlayerDef ? m->dmg - gPlayerDef : 0);
        gHp = (u8)(gHp > dmg ? gHp - dmg : 0);
        gPlayerHurt = 1;
        gLastAttacker = m->type;
        if (dmg) {
            Msg2(MonName(m->type), "HITS YOU");
            /* At 3 HP or less the hit carries a two-tone alarm tail: the
             * HUD number alone was easy to miss in a melee. */
            SfxPlay(gHp <= 3 ? SFX_HURT_LOW : SFX_HURT);
        } else {
            Msg2(MonName(m->type), "GLANCES OFF");   /* armour ate it whole */
            SfxPlay(SFX_GLANCE);
        }
        return 1;
    }

    /* The screen frame belongs to the player: monsters stay in the playable
     * interior, so they can never sit on a wrap door. */
    if (gTgtRow < PLAY_TOP_ROW || gTgtRow > PLAY_BOT_ROW)
        return 0;
    if (gTgtCol < PLAY_LEFT_COL || gTgtCol > PLAY_RIGHT_COL)
        return 0;

    tile = TileAt(gTgtCol, gTgtRow);
    if (MonsterAtTarget() >= 0)
        return 0;
    /* Monsters cannot trample floor items: it keeps the sprite visible and
     * lets the player use a drop as a bait cell. */
    if (ItemAtTarget() >= 0)
        return 0;

    if (tile == TILE_TRAP_PIT) {
        m->hp = (u8)(m->hp > PIT_DMG ? m->hp - PIT_DMG : 0);
        RevealPitAtTarget();
        if (m->hp == 0) {
            Msg2(MonName(m->type), "FALLS IN A PIT");
            m->type = 0;        /* no XP, no drop -- the player didn't earn it */
            return 1;
        }
        m->hurt = 1;
    } else if (tile != TILE_EMPTY && tile != TILE_STAIRS_DOWN) {
        /* Doors are forbidden to monsters by design: each undead is confined
         * to its spawn room or to a corridor segment, so the player can rest
         * next door and the pursuit stops at the threshold. */
        return 0;
    }

    m->col = gTgtCol;
    m->row = gTgtRow;
    return 1;
}

static int TryStepX(short i)
{
    Monster *m = &gMon[i];
    if (mon_abs_dx == 0)
        return 0;
    gTgtRow = m->row;
    gTgtCol = (u8)(m->col < gPlayerCol ? m->col + 1 : m->col - 1);
    return ApplyStep(i);
}

static int TryStepY(short i)
{
    Monster *m = &gMon[i];
    if (mon_abs_dy == 0)
        return 0;
    gTgtCol = m->col;
    gTgtRow = (u8)(m->row < gPlayerRow ? m->row + 1 : m->row - 1);
    return ApplyStep(i);
}

/* Step AWAY. The flee step always grows the distance, so apply_step can
 * never land on the player -- an unprovoked troll never bites by accident. */
static int TryFleeX(short i)
{
    Monster *m = &gMon[i];
    if (mon_abs_dx == 0)
        return 0;
    gTgtRow = m->row;
    gTgtCol = (u8)(m->col < gPlayerCol ? m->col - 1 : m->col + 1);
    return ApplyStep(i);
}

static int TryFleeY(short i)
{
    Monster *m = &gMon[i];
    if (mon_abs_dy == 0)
        return 0;
    gTgtCol = m->col;
    gTgtRow = (u8)(m->row < gPlayerRow ? m->row - 1 : m->row + 1);
    return ApplyStep(i);
}

static void AiUndead(short i)
{
    if (!ComputeAbsDeltas(&gMon[i]))
        return;
    if (mon_abs_dx < mon_abs_dy) {
        if (!TryStepY(i))
            TryStepX(i);
    } else {
        if (!TryStepX(i))
            TryStepY(i);
    }
}

/* Anti-greedy: shorter axis first, so it circles in from the side. */
static void AiSkeleton(short i)
{
    if (!ComputeAbsDeltas(&gMon[i]))
        return;
    if (mon_abs_dx < mon_abs_dy) {
        if (!TryStepX(i))
            TryStepY(i);
    } else {
        if (!TryStepY(i))
            TryStepX(i);
    }
}

static void AiDeath(short i)
{
    if (RandMod(2) != 0)
        AiUndead(i);            /* acts about half its turns */
}

static const s8 kGhostDx[4] = {  0,  0, -1,  1 };
static const s8 kGhostDy[4] = { -1,  1,  0,  0 };

static void AiGhost(short i)
{
    u8 d = RandMod(4);
    gTgtCol = (u8)(gMon[i].col + kGhostDx[d]);
    gTgtRow = (u8)(gMon[i].row + kGhostDy[d]);
    ApplyStep(i);
}

/* MON_HURT doubles as the troll's permanent "provoked" flag --
 * clear_hurt_flags leaves it alone on trolls -- so the red tint you get for
 * free is also the "this one is angry now" tell. */
static void AiTroll(short i)
{
    if (gMon[i].hurt) {
        AiUndead(i);
        return;
    }
    if (!ComputeAbsDeltas(&gMon[i]))
        return;
    if (mon_abs_dx < mon_abs_dy) {
        if (!TryFleeY(i))
            TryFleeX(i);
    } else {
        if (!TryFleeX(i))
            TryFleeY(i);
    }
}

/* --- Boss ------------------------------------------------------------------ */

/* The arena has no doors, stairs, items or pits, so a wall is the only
 * blocker a footprint cell can meet. */
static int BossCellPassable(short col, short row)
{
    if (row < PLAY_TOP_ROW || row > PLAY_BOT_ROW)
        return 0;
    if (col < PLAY_LEFT_COL || col > PLAY_RIGHT_COL)
        return 0;
    return TileAt((u8)col, (u8)row) == TILE_EMPTY;
}

/* (gTgtCol, gTgtRow) is the prospective new anchor of the 2x2 footprint. */
static int ApplyBossStep(short i)
{
    Monster *m = &gMon[i];
    short dc = (short)gPlayerCol - (short)gTgtCol;
    short dr = (short)gPlayerRow - (short)gTgtRow;

    if (dc >= 0 && dc < 2 && dr >= 0 && dr < 2) {
        /* The player is standing inside the new footprint: bite, don't move. */
        u8 dmg = (u8)(m->dmg > gPlayerDef ? m->dmg - gPlayerDef : 0);
        gHp = (u8)(gHp > dmg ? gHp - dmg : 0);
        gPlayerHurt = 1;
        gLastAttacker = MON_TYPE_BOSS;
        Msg("THE DEMON STRIKES YOU");
        SfxPlay(SFX_BOSS_HIT);
        return 1;
    }

    if (!BossCellPassable(gTgtCol, gTgtRow)
        || !BossCellPassable(gTgtCol + 1, gTgtRow)
        || !BossCellPassable(gTgtCol + 1, gTgtRow + 1)
        || !BossCellPassable(gTgtCol, gTgtRow + 1))
        return 0;

    m->col = gTgtCol;
    m->row = gTgtRow;
    return 1;
}

static int TryBossStepX(short i)
{
    Monster *m = &gMon[i];
    if (mon_abs_dx == 0)
        return 0;
    gTgtRow = m->row;
    gTgtCol = (u8)(m->col < gPlayerCol ? m->col + 1 : m->col - 1);
    return ApplyBossStep(i);
}

static int TryBossStepY(short i)
{
    Monster *m = &gMon[i];
    if (mon_abs_dy == 0)
        return 0;
    gTgtCol = m->col;
    gTgtRow = (u8)(m->row < gPlayerRow ? m->row + 1 : m->row - 1);
    return ApplyBossStep(i);
}

static void AiBoss(short i)
{
    if (!ComputeAbsDeltas(&gMon[i]))
        return;
    if (mon_abs_dx < mon_abs_dy) {
        if (!TryBossStepY(i))
            TryBossStepX(i);
    } else {
        if (!TryBossStepX(i))
            TryBossStepY(i);
    }
}

/* --- Turn ------------------------------------------------------------------ */

void MoveMonsters(void)
{
    short i;
    for (i = 0; i < MON_COUNT; i++) {
        switch (gMon[i].type) {
        case 0:                  break;
        case MON_TYPE_GHOST:     AiGhost(i);    break;
        case MON_TYPE_DEATH:     AiDeath(i);    break;
        case MON_TYPE_SKELETON:  AiSkeleton(i); break;
        case MON_TYPE_TROLL:     AiTroll(i);    break;
        case MON_TYPE_BOSS:      AiBoss(i);     break;
        default:                 AiUndead(i);   break;
        }
    }
}

/* Trolls keep their flag (it is the provoked state); everyone else uses
 * MON_HURT purely as a one-turn flash, so it is wiped each turn. A killed
 * troll has type 0, falls through, and gets its slot properly cleared. */
void ClearHurtFlags(void)
{
    short i;
    gPlayerHurt = 0;
    for (i = 0; i < MON_COUNT; i++)
        if (gMon[i].type != MON_TYPE_TROLL)
            gMon[i].hurt = 0;
}
