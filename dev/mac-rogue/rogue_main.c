/*
 * rogue_main.c -- the start sequence, the turn loop, level transitions.
 * ---------------------------------------------------------------------------
 * Transcribed from TMS_Rogue.asm: start, main_loop, new_level, finish_turn.
 *
 * The 6502 ended a run with `JMP $4000` -- a cartridge cold start from
 * anywhere in the call chain, including from deep inside finish_turn. The
 * faithful C translation of that is a longjmp, which is exactly what
 * ColdStart() does; RogueRun() therefore never returns normally.
 */

#include "rogue.h"
#include "snd.h"
#include "vdp.h"

/* --- Game state (was the 6502 zero page) --------------------------------- */
u8 gPlayerCol, gPlayerRow;
u8 gTgtCol, gTgtRow;
u8 gDepth, gTransMode, gWrapAnchor, gBossCheat;
u8 gHp, gHpMax, gPlayerHurt, gLastAttacker;
u8 gPlayerDmg, gPlayerDef, gPlayerXp;
u8 gXpAtkBonus, gXpDefBonus, gHpTick, gAtkTick, gDefTick;
u8 gRingFlags, gRegenTick;
u8 gWeaponTimer, gWeaponBoost, gArmorTimer, gArmorBoost;
u8 gTorchTimer, gRingTimer, gRingBoost;
u8 gDaggerQty;
u8 gMonTypePool, gMonHpBonus, gMonDmgBonus;
u8 gFovR;
u8 gThrowActive, gCurX, gCurY;

/* --- new_level -------------------------------------------------------------
 *   mode 0     initial random layout, depth unchanged
 *   mode 1     stairs down, depth advances (and depth 13 routes to the boss)
 *   modes 2-5  edge-door wrap: force a big-room and respawn on the opposite
 *              edge, depth unchanged. gWrapAnchor carries the row (2/3) or
 *              the column (4/5) the player walked out on.
 * The LFSR state survives across calls -- no reseeding per floor. */
void NewLevel(void)
{
    int boss = 0;

    if (gTransMode == 1) {
        gDepth++;
        if (gDepth >= 13)
            boss = 1;
    }

    if (boss) {
        /* Fixed arena, no doors, no stairs, no items, no pits, and the usual
         * spawn_monsters wipe is bypassed so it cannot clobber the demon the
         * moment it lands. */
        MapFillWalls();
        GenBossRoom();
        SpawnBoss();
    } else {
        if (gTransMode >= 2) {
            MapFillWalls();
            GenBigRoom();
            ApplyWrapSpawn();
        } else {
            GenDungeon();
        }
        SpawnMonsters();
        SpawnLevelItems();
        SpawnLevelPits();
    }

    VdpClearNames();
    ClearVisBuffer();
    ComputeFov();
    RenderMap();
    PlaceAllSprites();
    UpdateHud();
    ShellPresent();
}

/* --- finish_turn -----------------------------------------------------------
 * Regen runs FIRST so a tick that lifts HP back to 1 can rescue the player
 * from an otherwise fatal bump in the same turn -- classic Rogue's "ring of
 * regeneration absorbs the killing blow". */
void FinishTurn(void)
{
    if (gRingFlags & RING_F_REGEN) {
        if (gRegenTick == 0) {
            gRegenTick = RING_REGEN_PERIOD;
            if (gHp < gHpMax)
                gHp++;
        } else {
            gRegenTick--;
        }
    }

    /* Buff countdowns. Weapon and armour expiry has to recompute the derived
     * stats or the boost would linger in gPlayerDmg / gPlayerDef. Torch does
     * not: the next compute_fov reads gTorchTimer directly. */
    if (gWeaponTimer && --gWeaponTimer == 0)
        RecomputePlayerStats();
    if (gArmorTimer && --gArmorTimer == 0)
        RecomputePlayerStats();
    if (gTorchTimer)
        gTorchTimer--;
    if (gRingTimer && --gRingTimer == 0) {
        /* Clear only the bit this ring owned, so a future second ring type
         * could coexist and expire independently. */
        gRingFlags &= (u8)~gRingBoost;
        gRingBoost = 0;
    }

    ClearMsgRows();
    UpdateHud();
    ShellPresent();

    if (gHp == 0)
        DeathScreen();          /* never returns */
}

/* --- The turn loop --------------------------------------------------------- */

static void MonsterTurnAndFinish(void)
{
    MoveMonsters();
    PlaceAllSprites();
    FinishTurn();
}

static void MainLoop(void)
{
    for (;;) {
        char key;
        short mon;
        u8 tile;

        /* Reset the flash flags so last turn's red does not leak into this
         * turn's repaint. The flash itself was visible during the wait. */
        ClearHurtFlags();
        key = ShellWaitKey();

        if (key == 'B') {                       /* bag; I is a movement key */
            if (ShowInventory())
                MonsterTurnAndFinish();
            continue;
        }
        if (key == 'T') {
            if (HandleThrow())
                MonsterTurnAndFinish();
            continue;
        }
        if (key == 'H' || key == '?') {
            ShowHelp();
            continue;
        }
        if (key == '.') {                       /* rest: burn a turn in place */
            MonsterTurnAndFinish();
            continue;
        }

        if (!HandleInput(key))
            continue;

        /* Bump-to-attack is tested BEFORE collision, so the player can hit a
         * monster standing on a frame door instead of warping past it. */
        mon = MonsterAtTarget();
        if (mon >= 0) {
            PlayerAttackMonster(mon);
            MonsterTurnAndFinish();
            continue;
        }

        if (!CheckCollision())
            continue;

        tile = TileAt(gTgtCol, gTgtRow);

        if (tile == TILE_STAIRS_DOWN) {
            gTransMode = 1;
            SfxPlay(SFX_STAIRS);
            NewLevel();
            FinishTurn();                       /* the descent costs a turn */
            continue;
        }
        /* A move whose target sits on the screen frame is an exit, not a
         * step: warp to a sibling big-room at the same depth. */
        if (gTgtCol == 0) {
            gTransMode = 3; gWrapAnchor = gTgtRow;
            NewLevel(); FinishTurn(); continue;
        }
        if (gTgtCol == 15) {
            gTransMode = 2; gWrapAnchor = gTgtRow;
            NewLevel(); FinishTurn(); continue;
        }
        if (gTgtRow == 0) {
            gTransMode = 4; gWrapAnchor = gTgtCol;
            NewLevel(); FinishTurn(); continue;
        }
        if (gTgtRow == 9) {
            gTransMode = 5; gWrapAnchor = gTgtCol;
            NewLevel(); FinishTurn(); continue;
        }

        /* Regular move inside the playable interior. */
        gPlayerCol = gTgtCol;
        gPlayerRow = gTgtRow;

        /* Stepping ONTO a door wipes the fog, so the room behind goes black
         * again and compute_fov rebuilds from the threshold. Combined with
         * doors being sight-opaque, every room is a fresh scene: no peeking
         * through, no remembered layout once you have left. */
        if (tile == TILE_DOOR)
            ClearVisBuffer();
        if (tile == TILE_TRAP_PIT)
            TriggerPit();

        TryPickupItem();
        MoveMonsters();
        ComputeFov();
        RenderMap();
        PlaceAllSprites();
        FinishTurn();
    }
}

/* --- start ----------------------------------------------------------------- */

void RogueRun(void)
{
    VdpSatReset();
    VdpClearNames();
    DrawTitle();
    WaitKbChoice();             /* blocks; seeds the PRNG; reads the B cheat */

    VdpClearNames();
    DrawBriefing();
    ShellDrainKeys();
    ShellWaitKey();

    gDepth = 1;
    gTransMode = 0;             /* first big-room gets all four doors */
    gHp = HP_MAX;
    gPlayerHurt = 0;
    gLastAttacker = 0;
    InitInventory();

    if (gBossCheat) {
        /* depth 12 + mode 1 makes new_level increment to 13 and take its
         * boss branch, which does the whole setup in one call. */
        gDepth = 12;
        gTransMode = 1;
        NewLevel();
        MainLoop();
        return;
    }

    VdpClearNames();
    GenDungeon();               /* also sets the spawn to the first room */
    ClearVisBuffer();
    ComputeFov();
    RenderMap();
    SpawnMonsters();
    SpawnLevelItems();
    SpawnLevelPits();
    PlaceAllSprites();
    UpdateHud();
    ShellPresent();

    MainLoop();
}
