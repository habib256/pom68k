/*
 * rogue.h -- shared state and constants for the Macintosh port of TMS_Rogue.
 * ---------------------------------------------------------------------------
 * Source of truth: POM1 `sketchs/tms9918/game_rogue/TMS_Rogue.asm` (6502,
 * Apple-1 + P-LAB TMS9918). Every constant below is transcribed from that
 * file so the two builds play identically -- same tuning, same PRNG, same
 * dungeon for a given seed.
 *
 * Two deliberate divergences from the 6502 original, both invisible to play:
 *
 *   1. map_buffer / vis_buffer are UNPACKED here (one byte per cell instead
 *      of a nibble / a bit). The stored VALUE is unchanged -- a map cell
 *      still carries the dense TILE_* id in bits 0..2 and the pit-reveal
 *      flag in bit 3 -- only the physical packing is gone. The 68k has the
 *      RAM the Apple-1 did not, and dropping the packing drops a whole
 *      class of transcription bug.
 *
 *   2. The TMS9918's 32-sprite / 4-per-scanline budget is not enforced.
 *      Nothing in the game logic depended on it; the chip's limit only ever
 *      cost the original frames it could not draw.
 */

#ifndef ROGUE_H
#define ROGUE_H

/* --- Geometry: the logical 16x10 tile grid in the top 20 char rows ------- */
#define LOGICAL_COLS    16
#define LOGICAL_ROWS    10
#define MAP_CELLS       (LOGICAL_COLS * LOGICAL_ROWS)
#define PLAY_TOP_ROW    1
#define PLAY_BOT_ROW    8
#define PLAY_LEFT_COL   1
#define PLAY_RIGHT_COL  14

/* --- Vital stats --------------------------------------------------------- */
#define HP_MAX          14
#define PLAYER_DMG      1
#define FOOD_HEAL       3
#define PIT_DMG         3
#define LAST_ATTACKER_PIT 7

#define WEAPON_DURATION 20
#define ARMOR_DURATION  30
#define RING_DURATION   15
#define TORCH_DURATION  50
#define TORCH_RADIUS    7
#define FOV_RADIUS      3

#define THROW_RANGE     8
#define DAGGER_DMG      2
#define DAGGER_QTY_MAX  99

#define RING_F_REGEN      0x01
#define RING_REGEN_PERIOD 5

/* --- Monster pool -------------------------------------------------------- */
#define MON_COUNT       16
#define MON_TYPE_UNDEAD   1
#define MON_TYPE_GHOST    2
#define MON_TYPE_TROLL    3
#define MON_TYPE_SKELETON 4
#define MON_TYPE_DEATH    5
#define MON_TYPE_BOSS     6

/* Sprite-pattern slot numbers (SAT name byte); slots are 4 apart in 16x16
 * mode. kSpritePats covers 0..52, kBossPats continues at 56. */
#define SPRITE_NAME_PLAYER    0
#define SPRITE_NAME_UNDEAD    4
#define SPRITE_NAME_GHOST     8
#define SPRITE_NAME_DEATH     12
#define SPRITE_NAME_FOOD      16
#define SPRITE_NAME_SKELETON  20
#define SPRITE_NAME_DAGGER    24
#define SPRITE_NAME_POTION    28
#define SPRITE_NAME_SCROLL    32
#define SPRITE_NAME_WEAPON    36
#define SPRITE_NAME_ARMOR     40
#define SPRITE_NAME_RING      44
#define SPRITE_NAME_TROLL     48
#define SPRITE_NAME_TORCH     52
#define SPRITE_NAME_BOSS_TL   56
#define SPRITE_NAME_BOSS_TR   60
#define SPRITE_NAME_BOSS_BL   64
#define SPRITE_NAME_BOSS_BR   68

/* TMS9918 palette indices. Kept even though the shipping renderer is
 * monochrome: the compositor's ink rule reads them, and a colour back end
 * (see README "Next") needs nothing else. */
#define COL_PLAYER      5
#define COL_HURT        8
#define MON_COL_UNDEAD  15
#define MON_COL_GHOST   14
#define MON_COL_DEATH   10
#define MON_COL_BOSS    8
#define MON_COL_SKELETON 7
#define MON_COL_TROLL   2
#define COL_FOOD        11
#define COL_DAGGER      14
#define COL_POTION      13
#define COL_SCROLL      7
#define COL_WEAPON      15
#define COL_ARMOR       6
#define COL_RING        10
#define COL_TORCH       9

/* --- World item pool ----------------------------------------------------- */
#define ITEM_COUNT      8
#define ITEM_T_WEAPON   1
#define ITEM_T_ARMOR    2
#define ITEM_T_RING     3
#define ITEM_T_POTION   4
#define ITEM_T_SCROLL   5
#define ITEM_T_FOOD     6
#define ITEM_T_DAGGER   7
#define ITEM_T_TORCH    8

#define SUB_WEAPON_SWORD 0
#define SUB_ARMOR_TUNIC  0
#define SUB_RING_AMULET  0
#define SUB_POT_HEAL     0
#define SUB_SCROLL_MAP   0
#define SUB_FOOD_RATION  0
#define SUB_DAGGER_PLAIN 0
#define SUB_TORCH_PLAIN  0

/* --- Inventory ----------------------------------------------------------- */
#define INV_COUNT       26

/* --- Dense tile ids (bits 0..2 of a map cell) ---------------------------- */
#define TILE_EMPTY       0
#define TILE_WALL        1
#define TILE_DOOR        2
#define TILE_STAIRS_DOWN 3
#define TILE_STAIRS_UP   4
#define TILE_TRAP_PIT    5
#define TILE_CORR        6
#define TILE_CORR_DROP   7
#define TILE_REVEAL_BIT  0x08

/* --- Char base ids in the pattern table (from tileset_rogue.inc) --------- */
#define CHAR_EMPTY       0
#define CHAR_WALL        4
#define CHAR_STAIRS_DOWN 8
#define CHAR_DOOR        12
#define CHAR_STAIRS_UP   16
#define CHAR_TRAP_PIT    20

/* --- Movement / command keys. The original bound IJKL at the title screen
 * because those four sit on the same physical keys under QWERTY and AZERTY;
 * the Macintosh port keeps them and adds the arrow keys on top. ---------- */
#define KEY_NORTH 'I'
#define KEY_SOUTH 'K'
#define KEY_WEST  'J'
#define KEY_EAST  'L'

typedef unsigned char u8;
typedef signed char   s8;

typedef struct {
    u8 type;        /* 0 = dead / empty slot */
    u8 hp;
    u8 col, row;
    u8 name;        /* sprite slot */
    u8 color;       /* TMS palette index */
    u8 dmg;
    u8 hurt;        /* red-flash this frame; doubles as "provoked" on trolls */
} Monster;

typedef struct {
    u8 type;        /* 0 = empty */
    u8 col, row;
    u8 subtype;
} WorldItem;

typedef struct {
    u8 type;        /* 0 = empty */
    u8 subtype;
    u8 qty;
    u8 value;       /* cached effect payload, looked up once at pickup */
} InvSlot;

/* ===================== Game state (was the 6502 zero page) ============== */
extern u8 gMap[MAP_CELLS];      /* tile id in bits 0..2 + reveal bit 3 */
extern u8 gVis[MAP_CELLS];      /* 0 = dark, 1 = lit this turn */
extern Monster  gMon[MON_COUNT];
extern WorldItem gItem[ITEM_COUNT];
extern InvSlot  gInv[INV_COUNT];

extern u8 gPlayerCol, gPlayerRow;
extern u8 gTgtCol, gTgtRow;
extern u8 gDepth, gTransMode, gWrapAnchor, gBossCheat;
extern u8 gHp, gHpMax, gPlayerHurt, gLastAttacker;
extern u8 gPlayerDmg, gPlayerDef, gPlayerXp;
extern u8 gXpAtkBonus, gXpDefBonus, gHpTick, gAtkTick, gDefTick;
extern u8 gRingFlags, gRegenTick;
extern u8 gWeaponTimer, gWeaponBoost, gArmorTimer, gArmorBoost;
extern u8 gTorchTimer, gRingTimer, gRingBoost;
extern u8 gDaggerQty;
extern u8 gMonTypePool, gMonHpBonus, gMonDmgBonus;
extern u8 gFovR;
extern u8 gThrowActive, gCurX, gCurY;

/* ===================== rogue_map.c ====================================== */
void  MapFillWalls(void);
u8    MapGet(short idx);            /* raw cell (reveal bit kept) */
void  MapSet(short idx, u8 nib);
u8    TileAt(short col, short row); /* dense id, reveal stripped; OOB = WALL */
void  SetTile(u8 tile);             /* writes at (gTgtCol, gTgtRow) */
void  RevealPitAtTarget(void);
int   CheckCollision(void);         /* 1 = passable */
int   HandleInput(char key);        /* 1 = movement requested */
void  ClearVisBuffer(void);

void  PrngSeed(unsigned short lo, unsigned short hi);
u8    Prng16(void);
u8    RandMod(u8 max);              /* uniform [0, max) */

/* ===================== rogue_gen.c ====================================== */
void  GenDungeon(void);
void  GenBigRoom(void);
void  GenBossRoom(void);
void  SpawnBoss(void);
void  SpawnMonsters(void);
void  SpawnLevelItems(void);
void  SpawnLevelPits(void);
int   FindEmptyCell(void);          /* 1 = found, sets gTgtCol/gTgtRow */
void  ApplyWrapSpawn(void);

/* ===================== rogue_fov.c ====================================== */
void  ComputeFov(void);

/* ===================== rogue_mon.c ====================================== */
short MonsterAtTarget(void);        /* index, or -1 */
void  PlayerAttackMonster(short i);
void  AwardXp(void);
void  MoveMonsters(void);
void  ClearHurtFlags(void);
void  RecomputePlayerStats(void);
void  TriggerPit(void);

/* ===================== rogue_item.c ===================================== */
extern const u8 kItemSprite[9];     /* SAT name byte, indexed by ITEM_T_* */
extern const u8 kItemColor[9];      /* TMS palette index, same indexing   */
short ItemAtTarget(void);           /* index, or -1 */
void  SpawnTypedItem(u8 type, u8 subtype);
void  TryPickupItem(void);
void  InitInventory(void);
u8    LookupItemValue(u8 type, u8 subtype);
const char *LookupItemName(u8 type, u8 subtype);
int   DispatchUseSlot(short slot);  /* 1 = turn consumed */
int   HandleThrow(void);            /* 1 = turn consumed */

/* ===================== rogue_draw.c ===================================== */
void  RenderMap(void);
void  PlaceAllSprites(void);
void  UpdateHud(void);
void  ClearMsgRows(void);
void  ClearMsgRow23(void);
void  PrintMsgRow(short row, const char *s);
void  PutByte3(short row, short col, u8 v);
void  PutByte2(short row, short col, u8 v);
void  LevelWipe(void);
void  LevelReveal(void);
void  RedrawGame(void);

/* ===================== rogue_msg.c ====================================== */
void  MsgNewTurn(void);             /* clear the line at the start of a turn */
void  MsgReset(void);               /* new game */
void  Msg(const char *s);
void  Msg2(const char *a, const char *b);
void  MsgNum(const char *a, u8 v);
const char *MsgLine(void);
short MsgHistCount(void);
const char *MsgHist(short i);       /* 0 = most recent */

/* ===================== rogue_ui.c ======================================= */
void  DrawTitle(void);
void  DrawBriefing(void);
void  WaitKbChoice(void);
void  ShowHelp(void);
void  ShowLog(void);                /* the message history, a free action */
int   ShowInventory(void);          /* 1 = turn consumed */
void  DeathScreen(void);            /* never returns */
void  WinScreen(void);              /* never returns */

/* ===================== rogue_main.c ===================================== */
void  RogueRun(void);               /* one full game; exits via ColdStart() */
void  NewLevel(void);
void  FinishTurn(void);

/* ===================== mac_shell.c ====================================== */
#define COLD_RESTART 1
#define COLD_QUIT    2
void  ColdStart(short reason);      /* longjmp back to main(); never returns */
char  ShellWaitKey(void);           /* blocks; returns uppercase ASCII */
void  ShellDrainKeys(void);
void  ShellDelayTicks(short ticks);
void  ShellPresent(void);           /* composite + blit the game window */
unsigned long ShellTickCount(void); /* 60ths of a second since boot */

#endif /* ROGUE_H */
