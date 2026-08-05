/*
 * rogue_gen.c -- procedural dungeon generation and level population.
 * ---------------------------------------------------------------------------
 * Transcribed from TMS_Rogue.asm: gen_dungeon, gen_big_room, gen_boss_room,
 * gen_two_rooms, pick_random_room, carve_room, dig_corridor,
 * carve_corridor_marker, finalize_doors, place_perimeter_doors,
 * apply_wrap_spawn, spawn_monsters, spawn_boss, spawn_level_items,
 * spawn_level_pits, find_empty_cell.
 *
 * The order and count of RandMod() calls is load-bearing: it is what makes a
 * given seed produce the same floor here as on the Apple-1. Adding or
 * removing a roll anywhere in this file desynchronises the two builds.
 */

#include "rogue.h"

/* Generation scratch (was zero page). */
static u8 room_x, room_y, room_w, room_h;
static u8 cx, cy, prev_cx, prev_cy;
static u8 corr_row;

/* --- Per-type monster init tables, indexed by MON_TYPE ------------------- */
static const u8 kMonInitHp[7]    = { 0, 1, 2, 3, 2, 3, 15 };
static const u8 kMonInitDmg[7]   = { 0, 1, 1, 1, 2, 2, 4 };
static const u8 kMonInitName[7]  = {
    0, SPRITE_NAME_UNDEAD, SPRITE_NAME_GHOST, SPRITE_NAME_TROLL,
    SPRITE_NAME_SKELETON, SPRITE_NAME_DEATH, SPRITE_NAME_BOSS_TL
};
static const u8 kMonInitColor[7] = {
    0, MON_COL_UNDEAD, MON_COL_GHOST, MON_COL_TROLL,
    MON_COL_SKELETON, MON_COL_DEATH, MON_COL_BOSS
};

/* --- find_empty_cell ------------------------------------------------------
 * Random start offset followed by a deterministic sweep of all 160 cells.
 * The 6502 comment records why: the earlier "32 random tries then give up"
 * form silently dropped a monster or an item about 1.4 % of the time on
 * cramped two-room floors. */
int FindEmptyCell(void)
{
    short cursor = RandMod(MAP_CELLS);
    short remaining;

    for (remaining = MAP_CELLS; remaining > 0; remaining--) {
        gTgtCol = (u8)(cursor & 0x0F);
        gTgtRow = (u8)(cursor >> 4);

        if (TileAt(gTgtCol, gTgtRow) == TILE_EMPTY
            && !(gTgtCol == gPlayerCol && gTgtRow == gPlayerRow)
            && MonsterAtTarget() < 0
            && ItemAtTarget() < 0)
            return 1;

        cursor++;
        if (cursor >= MAP_CELLS)
            cursor = 0;
    }
    return 0;
}

/* --- Room carving --------------------------------------------------------- */

static void CarveRoom(void)
{
    u8 r, c;
    for (r = 0; r < room_h; r++) {
        for (c = 0; c < room_w; c++) {
            gTgtCol = (u8)(room_x + c);
            gTgtRow = (u8)(room_y + r);
            SetTile(TILE_EMPTY);
        }
    }
}

/* Two non-overlapping rooms, one per half. The 1-column gap at col 8 is
 * what guarantees the L-corridor always crosses at least one wall, which is
 * what makes the door-marker pass produce clean thresholds. */
static void PickRandomRoom(u8 roomIdx)
{
    room_w = (u8)(RandMod(3) + 4);          /* [4, 6] */
    room_h = (u8)(RandMod(3) + 3);          /* [3, 5] */
    room_y = (u8)(RandMod((u8)(9 - room_h)) + 1);
    if (roomIdx == 0)
        room_x = (u8)(RandMod((u8)(8 - room_w)) + 1);   /* left  half */
    else
        room_x = (u8)(RandMod((u8)(7 - room_w)) + 9);   /* right half */
}

/* Freshly-dug wall cells become TILE_CORR markers; room interiors are left
 * alone, which is what lets finalize_doors read "neighbour is TILE_EMPTY"
 * as "neighbour is untouched room interior". */
static void CarveCorridorMarker(void)
{
    if (TileAt(gTgtCol, gTgtRow) == TILE_WALL)
        SetTile(TILE_CORR);
}

/* L-corridor: horizontal along row prev_cy, then vertical along col cx, so
 * the corner sits at (cx, prev_cy). Both segments are inclusive. */
static void DigCorridor(void)
{
    u8 lo, hi;

    gTgtRow = prev_cy;
    lo = (prev_cx < cx) ? prev_cx : cx;
    hi = (prev_cx < cx) ? cx : prev_cx;
    for (gTgtCol = lo;; gTgtCol++) {
        CarveCorridorMarker();
        if (gTgtCol >= hi)
            break;
    }

    gTgtCol = cx;
    lo = (prev_cy < cy) ? prev_cy : cy;
    hi = (prev_cy < cy) ? cy : prev_cy;
    for (gTgtRow = lo;; gTgtRow++) {
        CarveCorridorMarker();
        if (gTgtRow >= hi)
            break;
    }
}

/* --- finalize_doors -------------------------------------------------------
 * Three passes, and the split matters. Writing TILE_EMPTY during pass 1
 * would make later cells in the same pass see a false room-interior
 * neighbour and stay flagged as doors -- that was the alternating-door
 * cascade bug the 6502 comment records. */
static void FinalizeDoors(void)
{
    short idx;

    /* Pass 1: classify each corridor marker as threshold or mid-corridor. */
    for (idx = MAP_CELLS - 1; idx >= 0; idx--) {
        u8 col, row;
        int boundary;

        if ((MapGet(idx) & 7) != TILE_CORR)
            continue;
        col = (u8)(idx & 0x0F);
        row = (u8)(idx >> 4);

        boundary = (TileAt(col - 1, row) == TILE_EMPTY)
                || (TileAt(col + 1, row) == TILE_EMPTY)
                || (TileAt(col, row - 1) == TILE_EMPTY)
                || (TileAt(col, row + 1) == TILE_EMPTY);

        MapSet(idx, boundary ? TILE_DOOR : TILE_CORR_DROP);
    }

    /* Pass 2: mid-corridor markers become plain floor. */
    for (idx = MAP_CELLS - 1; idx >= 0; idx--)
        if ((MapGet(idx) & 7) == TILE_CORR_DROP)
            MapSet(idx, TILE_EMPTY);

    /* Pass 3: collapse conga lines of doors. A corridor running alongside a
     * room flags every cell of the run as a threshold; sweeping high-to-low
     * and demoting any door whose west or north neighbour is also a door
     * leaves exactly the top-leftmost one standing. */
    for (idx = MAP_CELLS - 1; idx >= 0; idx--) {
        u8 col, row;
        if ((MapGet(idx) & 7) != TILE_DOOR)
            continue;
        col = (u8)(idx & 0x0F);
        row = (u8)(idx >> 4);
        if (TileAt(col - 1, row) == TILE_DOOR
            || TileAt(col, row - 1) == TILE_DOOR)
            MapSet(idx, TILE_EMPTY);
    }
}

/* --- place_perimeter_doors ------------------------------------------------
 * One door per cardinal screen edge; walking onto one exits the map. In wrap
 * modes the entry edge is skipped so apply_wrap_spawn's aligned return door
 * is alone on that wall. The +3 margin keeps doors out of the corner cells,
 * which would defeat the wrap alignment. */
#define DOOR_BASE 3

typedef struct { u8 skipMode, span, axis, fixed; } DoorEdge;

static const DoorEdge kDoorTable[4] = {
    { 5, 10, 0,  0 },   /* N: col randomised, row 0  */
    { 4, 10, 0,  9 },   /* S: col randomised, row 9  */
    { 2,  4, 1,  0 },   /* W: row randomised, col 0  */
    { 3,  4, 1, 15 }    /* E: row randomised, col 15 */
};

static void PlacePerimeterDoors(void)
{
    short i;
    for (i = 0; i < 4; i++) {
        const DoorEdge *e = &kDoorTable[i];
        u8 r;
        if (e->skipMode == gTransMode)
            continue;
        r = (u8)(RandMod(e->span) + DOOR_BASE);
        if (e->axis == 0) {
            gTgtCol = r;
            gTgtRow = e->fixed;
        } else {
            gTgtRow = r;
            gTgtCol = e->fixed;
        }
        SetTile(TILE_DOOR);
    }
}

/* --- Layouts -------------------------------------------------------------- */

/* Single full-screen room. Stairs-up at (14, 2) so its east neighbour is the
 * frame; the player spawns one cell west of it -- the spec's "you arrive to
 * the left of the stairs". Stairs-down at (1, 8), same rule mirrored. */
void GenBigRoom(void)
{
    room_x = 1;
    room_y = 1;
    room_w = 14;
    room_h = 8;
    CarveRoom();

    gTgtCol = 14;
    gTgtRow = 2;
    SetTile(TILE_STAIRS_UP);
    gPlayerCol = 13;
    gPlayerRow = 2;

    gTgtCol = 1;
    gTgtRow = 8;
    SetTile(TILE_STAIRS_DOWN);

    PlacePerimeterDoors();
}

/* Depth-13 arena: big-room geometry stripped of stairs, doors, items and
 * pits. Once the player descends, the only way out is through the demon. */
void GenBossRoom(void)
{
    room_x = 1;
    room_y = 1;
    room_w = 14;
    room_h = 8;
    CarveRoom();

    gPlayerCol = 7;
    gPlayerRow = 8;
}

static void GenTwoRooms(void)
{
    u8 roomIdx;

    for (roomIdx = 0; roomIdx < 2; roomIdx++) {
        PickRandomRoom(roomIdx);
        CarveRoom();

        /* Centre, rounding the half-extent up so it stays inside the room.
         * (The 6502 got this free from LSR's carry feeding a bare ADC.) */
        cx = (u8)(room_x + ((room_w + 1) >> 1));
        cy = (u8)(room_y + ((room_h + 1) >> 1));

        if (roomIdx == 0) {
            gPlayerCol = (u8)(room_x + room_w - 2);
            gPlayerRow = room_y;
            /* Room 0's centre row is the corridor's horizontal run; the
             * stairs-down placement below needs it to avoid landing its
             * west-anchor cell on a door. */
            corr_row = cy;
        } else {
            DigCorridor();
        }
        prev_cx = cx;
        prev_cy = cy;
    }

    FinalizeDoors();

    /* Stairs-up one cell east of the spawn: its east neighbour is room 0's
     * wall, so the sprite is anchored as the spec demands. */
    gTgtCol = (u8)(gPlayerCol + 1);
    gTgtRow = gPlayerRow;
    SetTile(TILE_STAIRS_UP);

    /* Stairs-down at room 1's top-left interior corner so its WEST
     * neighbour is a wall -- unless the corridor entered horizontally on
     * that very row, in which case the wall is a door and we drop one row.
     * room_h >= 3 guarantees ry+1 is still interior. */
    gTgtCol = room_x;
    gTgtRow = room_y;
    if (gTgtRow == corr_row)
        gTgtRow++;
    SetTile(TILE_STAIRS_DOWN);
}

void GenDungeon(void)
{
    MapFillWalls();
    if (RandMod(2) == 0)
        GenBigRoom();
    else
        GenTwoRooms();
}

/* --- apply_wrap_spawn -----------------------------------------------------
 * Respawn on the opposite edge of the fresh big-room and stamp an aligned
 * TILE_DOOR on the entry edge, so walking back out the way you came warps
 * again and the world reads as continuous even though every wrap
 * regenerates the floor. */
void ApplyWrapSpawn(void)
{
    switch (gTransMode) {
    case 2:                              /* exited E -> spawn at W edge */
        gPlayerCol = 1;
        gPlayerRow = gWrapAnchor;
        gTgtRow = gWrapAnchor;
        gTgtCol = 0;
        break;
    case 3:                              /* exited W -> spawn at E edge */
        gPlayerCol = 14;
        gPlayerRow = gWrapAnchor;
        gTgtRow = gWrapAnchor;
        gTgtCol = 15;
        break;
    case 4:                              /* exited N -> spawn at S edge */
        gPlayerRow = 8;
        gPlayerCol = gWrapAnchor;
        gTgtCol = gWrapAnchor;
        gTgtRow = 9;
        break;
    default:                             /* mode 5: exited S -> spawn at N */
        gPlayerRow = 1;
        gPlayerCol = gWrapAnchor;
        gTgtCol = gWrapAnchor;
        gTgtRow = 0;
        break;
    }
    SetTile(TILE_DOOR);
}

/* --- Population ----------------------------------------------------------- */

/* The 6502 wiped monsters and items with one 160-byte sweep because the two
 * pools were laid out contiguously at $E300. Only the type byte actually
 * gates anything, but zeroing the whole slot keeps a stale row/col from ever
 * surfacing through a future code path. */
static void WipePools(void)
{
    short i;
    for (i = 0; i < MON_COUNT; i++) {
        Monster *m = &gMon[i];
        m->type = m->hp = m->col = m->row = 0;
        m->name = m->color = m->dmg = m->hurt = 0;
    }
    for (i = 0; i < ITEM_COUNT; i++) {
        WorldItem *w = &gItem[i];
        w->type = w->col = w->row = w->subtype = 0;
    }
}

/* Three knobs, all keyed off depth:
 *   count     = min(depth + 2, 16)
 *   type pool = 3 (depth 1-4) -> 4 (5-9) -> 5 (10+)
 *   hp bonus  = depth / 3,  dmg bonus = depth / 6
 * so encounters get both longer and sharper as you descend. */
void SpawnMonsters(void)
{
    short count, i;

    WipePools();

    count = gDepth + 2;
    if (count > MON_COUNT)
        count = MON_COUNT;

    if (gDepth >= 10)
        gMonTypePool = 5;
    else if (gDepth >= 5)
        gMonTypePool = 4;
    else
        gMonTypePool = 3;

    gMonHpBonus  = (u8)(gDepth / 3);
    gMonDmgBonus = (u8)(gMonHpBonus >> 1);

    for (i = 0; i < count; i++) {
        u8 type;
        Monster *m;

        if (!FindEmptyCell())
            break;                      /* out of floor -- fewer monsters */

        type = (u8)(RandMod(gMonTypePool) + 1);
        m = &gMon[i];
        m->type  = type;
        m->hp    = (u8)(kMonInitHp[type] + gMonHpBonus);
        m->col   = gTgtCol;
        m->row   = gTgtRow;
        m->name  = kMonInitName[type];
        m->color = kMonInitColor[type];
        m->dmg   = (u8)(kMonInitDmg[type] + gMonDmgBonus);
        m->hurt  = 0;
    }
}

void SpawnBoss(void)
{
    Monster *m;

    WipePools();
    gMonHpBonus  = (u8)(gDepth / 3);
    gMonDmgBonus = (u8)(gMonHpBonus >> 1);

    m = &gMon[0];
    m->type  = MON_TYPE_BOSS;
    m->hp    = (u8)(kMonInitHp[MON_TYPE_BOSS] + gMonHpBonus);
    m->dmg   = (u8)(kMonInitDmg[MON_TYPE_BOSS] + gMonDmgBonus);
    m->name  = kMonInitName[MON_TYPE_BOSS];
    m->color = kMonInitColor[MON_TYPE_BOSS];
    m->col   = 7;                       /* 2x2 footprint: (7,4)..(8,5) */
    m->row   = 4;
    m->hurt  = 0;
}

/* Cumulative thresholds out of 32; first entry above the roll wins.
 * Mass: food 6, dagger 8, potion 3, scroll 2, weapon 4, armor 3, ring 2,
 * torch 4. Weapons and armour are consumable buffs now, so they have to
 * drop often enough that the player does not run dry after one floor. */
static const u8 kItemThresh[8] = { 6, 14, 17, 19, 23, 26, 28, 32 };
static const u8 kItemType[8]   = {
    ITEM_T_FOOD, ITEM_T_DAGGER, ITEM_T_POTION, ITEM_T_SCROLL,
    ITEM_T_WEAPON, ITEM_T_ARMOR, ITEM_T_RING, ITEM_T_TORCH
};
/* One sub-type per category since the MVP4 simplification, so the sub-type
 * roll always lands on 0 -- but the roll still happens, and the LFSR still
 * advances, which is why it is written out rather than folded away. */
static const u8 kItemSubCount[8] = { 1, 1, 1, 1, 1, 1, 1, 1 };

void SpawnLevelItems(void)
{
    short n = RandMod(3) + 1;           /* 1..3 items */
    short i;

    for (i = 0; i < n; i++) {
        u8 roll, type, sub;
        short k;

        if (!FindEmptyCell())
            return;

        roll = RandMod(32);
        for (k = 0; k < 8 && roll >= kItemThresh[k]; k++)
            ;
        if (k > 7)
            k = 7;
        type = kItemType[k];
        sub  = RandMod(kItemSubCount[k]);
        SpawnTypedItem(type, sub);
    }
}

/* 0..2 hidden pits per floor. They are not consumed: re-stepping costs HP
 * again, and strip_invisible_pit_reveals re-hides them the moment they leave
 * the torchlight, so the player has to actually remember the layout. */
void SpawnLevelPits(void)
{
    u8 n = RandMod(3);
    while (n--) {
        if (!FindEmptyCell())
            return;
        SetTile(TILE_TRAP_PIT);
    }
}
