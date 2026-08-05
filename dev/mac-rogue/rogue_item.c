/*
 * rogue_item.c -- floor items, the 26-letter bag, buff activation, throwing.
 * ---------------------------------------------------------------------------
 * Transcribed from TMS_Rogue.asm: item_at_target, spawn_typed_item,
 * try_pickup_item, find_inv_empty, find_inv_stack, lookup_item_value,
 * lookup_item_name, init_inventory, consume_inv_slot, dispatch_use_slot and
 * handle_throw.
 *
 * The MVP4 model, kept intact: every gear category is a CONSUMABLE buff.
 * Using a sword does not equip it, it burns it for WEAPON_DURATION turns.
 * Re-using a fresh one overwrites the timer -- no stacking, latest wins --
 * which turns "when do I pop this" into the actual decision.
 *
 * Daggers are the exception: they are ammo, not inventory. They never take
 * a bag letter, they live in gDaggerQty with their own HUD counter.
 */

#include "rogue.h"
#include "snd.h"
#include "vdp.h"

WorldItem gItem[ITEM_COUNT];
InvSlot   gInv[INV_COUNT];

/* --- Per-category payload, indexed by sub-type (all single-entry now) ----- */
static const u8 kWeaponValue[1] = { 2 };            /* sword: ATK +2        */
static const u8 kArmorValue[1]  = { 1 };            /* tunic: DEF +1        */
static const u8 kRingValue[1]   = { RING_F_REGEN }; /* amulet: regen flag   */
static const u8 kPotionValue[1] = { 5 };            /* potion: heal +5      */
static const u8 kScrollValue[1] = { SUB_SCROLL_MAP };
static const u8 kFoodValue[1]   = { FOOD_HEAL };
static const u8 kDaggerValue[1] = { 2 };
static const u8 kTorchValue[1]  = { SUB_TORCH_PLAIN };

/* Indexed by ITEM_T_*; entry 0 is unreachable but kept so a stray index
 * cannot read past the end. */
const u8 kItemSprite[9] = {
    0, SPRITE_NAME_WEAPON, SPRITE_NAME_ARMOR, SPRITE_NAME_RING,
    SPRITE_NAME_POTION, SPRITE_NAME_SCROLL, SPRITE_NAME_FOOD,
    SPRITE_NAME_DAGGER, SPRITE_NAME_TORCH
};
const u8 kItemColor[9] = {
    0, COL_WEAPON, COL_ARMOR, COL_RING, COL_POTION,
    COL_SCROLL, COL_FOOD, COL_DAGGER, COL_TORCH
};

static const char *kItemNames[9] = {
    "?", "SWORD", "TUNIC", "AMULET", "POTION", "SCROLL", "RATION",
    "DAGGER", "TORCH"
};

u8 LookupItemValue(u8 type, u8 subtype)
{
    if (subtype != 0)
        return 0;               /* one sub-type per category since MVP4 */
    switch (type) {
    case ITEM_T_WEAPON: return kWeaponValue[0];
    case ITEM_T_ARMOR:  return kArmorValue[0];
    case ITEM_T_RING:   return kRingValue[0];
    case ITEM_T_POTION: return kPotionValue[0];
    case ITEM_T_SCROLL: return kScrollValue[0];
    case ITEM_T_FOOD:   return kFoodValue[0];
    case ITEM_T_DAGGER: return kDaggerValue[0];
    case ITEM_T_TORCH:  return kTorchValue[0];
    default:            return 0;
    }
}

const char *LookupItemName(u8 type, u8 subtype)
{
    (void)subtype;
    if (type >= 1 && type <= 8)
        return kItemNames[type];
    return kItemNames[0];
}

/* --- World pool ----------------------------------------------------------- */

short ItemAtTarget(void)
{
    short i;
    for (i = 0; i < ITEM_COUNT; i++)
        if (gItem[i].type != 0
            && gItem[i].col == gTgtCol && gItem[i].row == gTgtRow)
            return i;
    return -1;
}

/* First free world slot; silently lost if all 8 are taken -- a level rarely
 * fills the pool from generation alone, and the player has usually eaten
 * something by the time drops pile up. */
void SpawnTypedItem(u8 type, u8 subtype)
{
    short i;
    for (i = 0; i < ITEM_COUNT; i++) {
        if (gItem[i].type == 0) {
            gItem[i].type    = type;
            gItem[i].col     = gTgtCol;
            gItem[i].row     = gTgtRow;
            gItem[i].subtype = subtype;
            return;
        }
    }
}

/* --- Bag ------------------------------------------------------------------- */

static short FindInvEmpty(void)
{
    short i;
    for (i = 0; i < INV_COUNT; i++)
        if (gInv[i].type == 0)
            return i;
    return -1;
}

/* Everything stacks on a (type, sub-type) match now: picking up a second
 * sword should grow the pile, not eat another bag letter. */
static short FindInvStack(u8 type, u8 subtype)
{
    short i;
    for (i = 0; i < INV_COUNT; i++)
        if (gInv[i].type == type && gInv[i].subtype == subtype)
            return i;
    return -1;
}

static void ConsumeInvSlot(short slot)
{
    if (--gInv[slot].qty == 0)
        gInv[slot].type = 0;
}

void InitInventory(void)
{
    short i;
    for (i = 0; i < INV_COUNT; i++) {
        gInv[i].type = gInv[i].subtype = gInv[i].qty = gInv[i].value = 0;
    }
    gRingFlags = gRegenTick = 0;
    gPlayerXp = gXpAtkBonus = gXpDefBonus = 0;
    gThrowActive = 0;
    gDaggerQty = 0;
    gWeaponTimer = gWeaponBoost = 0;
    gArmorTimer = gArmorBoost = 0;
    gTorchTimer = 0;
    gRingTimer = gRingBoost = 0;
    gHpMax = HP_MAX;
    gHpTick = 5;
    gAtkTick = 10;
    gDefTick = 20;
    RecomputePlayerStats();
}

/* Pickup is automatic on contact. A full bag leaves the item on the floor --
 * the player can come back after using something. */
void TryPickupItem(void)
{
    short w, slot;
    u8 type, subtype;

    gTgtCol = gPlayerCol;
    gTgtRow = gPlayerRow;
    w = ItemAtTarget();
    if (w < 0)
        return;

    type    = gItem[w].type;
    subtype = gItem[w].subtype;

    if (type == ITEM_T_DAGGER) {
        /* Ammo. Past the 2-digit HUD cap the pickup is still consumed --
         * "you cannot carry any more daggers". */
        if (gDaggerQty < DAGGER_QTY_MAX)
            gDaggerQty++;
        gItem[w].type = 0;
        SfxPlay(SFX_PICKUP);
        return;
    }

    slot = FindInvStack(type, subtype);
    if (slot >= 0) {
        gInv[slot].qty++;
    } else {
        slot = FindInvEmpty();
        if (slot < 0)
            return;             /* bag full -- leave it on the ground */
        gInv[slot].type    = type;
        gInv[slot].subtype = subtype;
        gInv[slot].qty     = 1;
        gInv[slot].value   = LookupItemValue(type, subtype);
    }
    gItem[w].type = 0;
    SfxPlay(SFX_PICKUP);
}

/* --- Using a slot ----------------------------------------------------------
 * Returns 1 when the action cost a turn. Buff activation is free, so the
 * player can pop a sword and still move the same turn -- timing is a lever,
 * not a tax. */
int DispatchUseSlot(short slot)
{
    InvSlot *s = &gInv[slot];
    u8 heal;

    switch (s->type) {
    case ITEM_T_WEAPON:
        gWeaponBoost = s->value;
        gWeaponTimer = WEAPON_DURATION;
        ConsumeInvSlot(slot);
        RecomputePlayerStats();
        SfxPlay(SFX_BUFF);
        RedrawGame();
        return 0;

    case ITEM_T_ARMOR:
        gArmorBoost = s->value;
        gArmorTimer = ARMOR_DURATION;
        ConsumeInvSlot(slot);
        RecomputePlayerStats();
        SfxPlay(SFX_BUFF);
        RedrawGame();
        return 0;

    case ITEM_T_TORCH:
        /* compute_fov reads gTorchTimer and switches radius on its own; the
         * immediate recompute is what makes the room light up on the spot. */
        gTorchTimer = TORCH_DURATION;
        ConsumeInvSlot(slot);
        SfxPlay(SFX_BUFF);
        ComputeFov();
        RedrawGame();
        return 0;

    case ITEM_T_RING:
        gRingBoost  = s->value;
        gRingFlags |= s->value;
        gRingTimer  = RING_DURATION;
        gRegenTick  = 0;        /* first pulse lands next turn: grace bonus */
        ConsumeInvSlot(slot);
        SfxPlay(SFX_BUFF);
        RedrawGame();
        return 0;

    case ITEM_T_FOOD:
    case ITEM_T_POTION:
        heal = (u8)(gHp + s->value);
        gHp = (heal > gHpMax) ? gHpMax : heal;
        ConsumeInvSlot(slot);
        SfxPlay(SFX_QUAFF);
        RedrawGame();
        return 1;

    case ITEM_T_SCROLL: {
        /* One-shot full-map reveal, shown as a modal. It does NOT persist:
         * the next move's compute_fov wipes the buffer and drops the player
         * back to torchlight. */
        short i;
        SfxPlay(SFX_SCROLL);
        for (i = 0; i < MAP_CELLS; i++)
            gVis[i] = 1;
        VdpClearNames();
        RenderMap();
        PlaceAllSprites();
        UpdateHud();
        ShellPresent();
        ShellDrainKeys();
        ShellWaitKey();
        ConsumeInvSlot(slot);
        ComputeFov();
        RedrawGame();
        return 1;
    }

    default:
        /* Daggers land here: they are thrown, never "used". */
        PrintMsgRow(23, "NOT USABLE");
        return 0;
    }
}

/* --- Throwing --------------------------------------------------------------
 * Fire and forget: the dagger always vanishes where it stops -- monster hit,
 * wall, door, stairs, pit, off-grid, or end of range. No floor drop. */
static int ParseDirection(char key, s8 *dx, s8 *dy)
{
    switch (key) {
    case KEY_WEST:  *dx = -1; *dy =  0; return 1;
    case KEY_EAST:  *dx =  1; *dy =  0; return 1;
    case KEY_NORTH: *dx =  0; *dy = -1; return 1;
    case KEY_SOUTH: *dx =  0; *dy =  1; return 1;
    default:        return 0;
    }
}

int HandleThrow(void)
{
    s8 dx = 0, dy = 0;
    short step;
    char key;

    if (gDaggerQty == 0) {
        PrintMsgRow(23, "NO DAGGER");
        return 0;
    }

    PrintMsgRow(23, "DIRECTION? ");
    ShellDrainKeys();
    key = ShellWaitKey();
    if (!ParseDirection(key, &dx, &dy)) {
        /* Wipe the prompt: leaving "DIRECTION?" up made players think the
         * game was still waiting. Bag intact, free action. */
        ClearMsgRow23();
        RedrawGame();
        return 0;
    }

    gDaggerQty--;
    SfxPlay(SFX_THROW);

    gCurX = gPlayerCol;
    gCurY = gPlayerRow;
    gThrowActive = 1;           /* PlaceAllSprites now emits the projectile */

    for (step = THROW_RANGE; step > 0; step--) {
        short mon;

        gCurX = (u8)(gCurX + dx);
        gCurY = (u8)(gCurY + dy);

        if (gCurY < PLAY_TOP_ROW || gCurY > PLAY_BOT_ROW)
            break;
        if (gCurX < PLAY_LEFT_COL || gCurX > PLAY_RIGHT_COL)
            break;
        /* Anything that is not open floor stops it -- wall, door, stairs,
         * even a pit. */
        if (TileAt(gCurX, gCurY) != TILE_EMPTY)
            break;

        gTgtCol = gCurX;
        gTgtRow = gCurY;
        mon = MonsterAtTarget();
        if (mon >= 0) {
            Monster *m = &gMon[mon];
            u8 wasBoss = (u8)(m->type == MON_TYPE_BOSS);
            m->hp = (u8)(m->hp > DAGGER_DMG ? m->hp - DAGGER_DMG : 0);
            SfxPlay(m->hp ? SFX_HIT : SFX_KILL);
            if (m->hp == 0) {
                m->type = 0;    /* thrown kills give XP but never food */
                AwardXp();
                /* DIVERGENCE from the 6502 original, deliberate: there,
                 * only player_attack_monster routed a boss kill into
                 * win_screen, so finishing the demon with a thrown dagger
                 * left the player alone in an arena with no stairs and no
                 * doors -- an unwinnable, unquittable floor. A boss is dead
                 * whatever killed it. See README "Divergences". */
                if (wasBoss) {
                    gThrowActive = 0;
                    WinScreen();        /* never returns */
                }
            } else {
                m->hurt = 1;
            }
            break;
        }

        RedrawGame();           /* rebuilds the SAT with the dagger in flight */
        ShellDelayTicks(5);     /* ~80 ms, same cadence as the 6502 loop */
    }

    gThrowActive = 0;
    RedrawGame();
    return 1;
}
