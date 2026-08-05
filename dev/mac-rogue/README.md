# mac-rogue — TMS_Rogue on the Macintosh

A Retro68 port of **TMS_Rogue**, the Berlin-Interpretation roguelike written
for the Apple-1 + P-LAB TMS9918 graphic card
(POM1 `sketchs/tms9918/game_rogue/TMS_Rogue.asm`, ~6 800 lines of 6502).

It runs as an ordinary Macintosh application: a window on the TMS9918's exact
256×192 frame — doubled to 512×384 when the screen has room — with Apple, File
and Display menus, a close box, and a proper event loop. **It paints in colour
where colour exists and in black and white where it does not**, so one binary
covers every POM68K profile from the **Mac Plus** to the **Quadra 950**.

> **Status: playable, in colour.** Verified on POM68K's Macintosh LC II
> (System 7.1, 640×480): boots, generates a dungeon, moves, fights, and paints
> the HUD. `m68k-apple-macos-gcc 16.1.0`, `-Os -Wall`, **zero warnings**.
> text 27 992 + data 5 024 + bss 58 120 = ~91 KB against a 1 MB partition.

## Building

The toolchain already lives in this repo (`dev/Retro68-build/toolchain`,
`m68k-apple-macos-gcc 16.1.0`), the same one `dev/bonjour-pomme-one` uses.

```bash
cd dev/mac-rogue
mkdir -p build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=../../Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake
make
```

Output in `build/`: `Rogue.bin` (MacBinary, 34 KB) and `Rogue.dsk` (an 800 K
mountable image — the quickest way in is `./POM68K <ROM> <system.dsk>
Rogue.dsk`). `Rogue.APPL` and `Rogue.ad` come out **0 bytes on Linux and that
is normal**: the resource fork lives beside them in `%Rogue.ad` / `.rsrc`.
Use the `.bin` or the `.dsk`.

The artwork tables are generated, not committed by hand. Re-run the
extractor whenever the POM1 sources change:

```bash
python3 tools/extract_rogue_assets.py     # -> rogue_gfx.c / rogue_gfx.h
```

It slices `tileset_rogue` (2 048 B), `tileset_color_table` (32 B),
`sprite_pats` (448 B) and `boss_sprite_pats` (128 B) straight out of the
6502 sources and checks each length, so a drifted upstream table fails loudly
instead of rendering garbage.

## Controls

| Key | Action |
|---|---|
| `I` `J` `K` `L` — or the **arrow keys** | move north / west / south / east |
| bump a monster | attack it |
| `B` | open the bag; type a letter inside to use that slot |
| `T` | throw a dagger, then a direction key |
| `H` or `?` | help card |
| `.` | rest one turn |
| ⌘Q, or the close box | quit |
| Display → Color | force monochrome / colour (greyed out on a 1-bit Mac) |
| Display → Actual / Double Size | 1× (256×192) or 2× (512×384) |
| Display → Sound | toggle effects (greyed out below System 6.0.4) |
| Display → Music | toggle the background music (greyed out when the second channel cannot open) |
| `B` **at the title screen** | hidden code: start in the depth-13 boss arena |

Pickup is automatic. Stairs down descend; walking into a door on the screen
frame warps to a sibling room at the same depth. Reach depth 13 and kill the
demon to win. One life.

## How the port is built

The 6502 original drove a real video chip: it wrote char ids into a 32×24
name table and 4-byte entries into a Sprite Attribute Table, and the TMS9918
composited. **`vdp.c` keeps exactly that interface and does the compositing
itself** — about 200 lines. That single decision is why the game logic could
be transcribed subroutine-for-subroutine instead of rewritten around
QuickDraw, and why `place_all_sprites` in the assembly and `PlaceAllSprites`
in C read the same way.

```
mac_shell.c   Toolbox: window, menus, event pump, CopyBits, the cold-start longjmp
vdp.c         software TMS9918 Graphics-I: name table + SAT -> 1-bit frame
rogue_gfx.c   GENERATED tile / sprite / colour tables
rogue_map.c   map buffer, tile queries, collision, Galois LFSR + rand_mod
rogue_gen.c   rooms, L-corridors, door classification, spawns
rogue_fov.c   Björn Bergström recursive shadowcasting
rogue_mon.c   the five AIs, combat, XP, the 2x2 boss
rogue_item.c  floor items, 26-letter bag, timed buffs, throwing
rogue_draw.c  render_map, place_all_sprites, HUD
rogue_ui.c    title, briefing, help, bag modal, death / win screens
rogue_main.c  start, turn loop, level transitions
```

Two mechanical translations worth knowing:

- **`JMP $4000` → `longjmp`.** The original ended a run with a cartridge cold
  start issued from deep inside `finish_turn`. `ColdStart()` is the same
  statement in C, so no caller in between needs "did the game just end?"
  plumbing.
- **Blocking key reads.** `ShellWaitKey()` pumps the event loop and returns a
  character, so the game keeps its original straight-line shape instead of
  becoming a state machine.

The **PRNG is bit-exact** with the Apple-1 build — same Galois LFSR ($B400),
same `rand_mod` repeated-subtraction reduction, same number of rolls in the
same order. A given seed carves the same dungeon on both machines, which is
the cheapest regression test this port has and the reason not to "improve"
the generator.

## Divergences from the 6502 original

Everything else — tuning constants, AI, generation, FOV, the HUD layout — is
transcribed unchanged. These are the exceptions, all deliberate:

1. **Colour is detected, not assumed.** The compositor emits TMS palette
   indices; how they reach the screen depends on the machine. Colour needs
   two yeses — Color QuickDraw in the ROM (`Gestalt`), *and* a main screen
   deeper than 1 bit, since a Quadra driving a mono monitor has the first
   without the second. When both hold, an 8-bit `GWorld` carrying the
   TMS9918A palette as its CLUT makes presenting a straight row copy. When
   either fails, the same buffer packs to 1 bit under an ink rule: anything
   that is neither transparent nor black becomes ink. The hit-flash follows:
   `COL_HURT` in colour, an **inverted silhouette** in mono, because a 1-bit
   screen has no second colour to flash. Both paths are live — Display →
   Color toggles them.
2. **Unpacked buffers.** `map_buffer` was two cells per byte and `vis_buffer`
   one bit per cell. The stored *values* are unchanged (tile id in bits 0..2,
   pit-reveal in bit 3); only the packing is gone. The 68k has the RAM the
   Apple-1 did not, and the packing was a standing source of transcription
   bugs.
3. **No sprite budget.** The chip's 32-sprite and 4-per-scanline limits are
   not enforced. No game logic depended on them — they only ever cost the
   original pixels it wanted to draw.
4. **Boss kills by thrown dagger now win the game.** *This is a bug fix, not
   a port artifact.* In the assembly only `player_attack_monster` routed a
   boss death into `win_screen`; the thrown-dagger path just cleared the slot.
   Since the depth-13 arena has no stairs, no doors, no items and no pits,
   finishing the demon at range left the player alone on an unwinnable,
   unquittable floor. `rogue_item.c` routes both paths to `WinScreen()`.
   **Worth fixing upstream in POM1 too.**
5. **The inventory multiplier prints.** The original wrote a lowercase `x`
   between the stack count and the item name — but the Quale font only
   carries uppercase, so that glyph was a blank tile and the line read
   `1  SWORD`. Now `1X SWORD`. Same class of upstream nit.
6. **Arrow keys** work alongside `IJKL`. The 6502 build bound `IJKL` at the
   title screen because those four keys sit in the same physical place under
   QWERTY and AZERTY; that stays, with the Mac's arrows folded onto the same
   four codes.
7. **Seeding.** The original counted busy-loop iterations until the first
   keypress. Here it is `TickCount()` at the keypress, XORed with the key —
   the same reaction-time entropy, expressed in Toolbox.

## Next

- **Exercise the monochrome path on a real 1-bit profile.** Colour is verified
  on the LC II; the mono packing and the inverted hit-flash have only been
  reasoned about, not seen. A Mac Plus boot is the test.
- ~~Sound. The original had none.~~ **Done (2026-08-04):** one Sound
  Manager square-wave channel (`snd.c`), seventeen queued-command motifs —
  combat, pickups, potions, the pit, stairs, XP thresholds, a low-HP alarm
  tail, title/death/win fanfares. Fully asynchronous (the channel's own
  128-deep queue is the sequencer; a new effect flushes the last), bright
  timbre for good news and buzzy for violence, Display → Sound to mute,
  silent degrade below System 6.0.4. The Apple-1 original stays silent;
  this is the Macintosh talking. **2026-08-05:** the short percussive
  effects dropped ~30-40 % in amplitude (a square wave has no envelope, so
  every note edge is a click whose loudness *is* the amp), and a **second
  channel now carries background music** — a calm A-minor bass loop
  (E2..E3, near-sine timbre, well under the effects), refilled from the
  event pump by deadline rather than by interrupt-time callback, with its
  own Display → Music toggle. The UI cards got the same-day facelift:
  double-size colour headlines (`VdpPutStringBig`) and per-cell text
  colour (`VdpPutStringColor`), both invisible-identical in monochrome
  under the ink rule.
- ~~Animation. Everything held still between keypresses.~~ **Done
  (2026-08-05):** an idle clock (`gVdpPhase`, 4 Hz) rides the event pump —
  the game blocks in `ShellWaitKey`, so the wait *is* the animation loop.
  Floor loot hovers (`SPR_BOB`, ±2 px, desynced per column), torches
  flicker through flame shades (`SPR_FLICKER`) both on the floor and in
  the HUD, the ghost floats, and the big headlines (title, GAME OVER,
  CONGRATULATIONS) pulse warm (`VDP_FG_PULSE`). Level transitions are a
  fall-and-rise curtain: `LevelWipe` darkens the floor being left
  top-to-bottom, `LevelReveal` gives the new one back four rows at a
  time with the SAT still empty, so the map appears first and its
  inhabitants pop in after. Fresh wounds tremble: `SPR_SHAKE` jolts the
  silhouette 1 px left/right for exactly as long as the hurt flash (keyed
  off the raw phase, not the column-desynced step, so the 2x2 boss jolts
  as one body). The stairs-down glints through the *tile* pass — the one
  animated name-table element, pulling the eye to the exit. Two torches
  flank the title and the win screen hovers the amulet. The shell only
  recomposites when the scene actually carries an animated element
  (`VdpAnimated`), so a static screen still costs a Mac Plus nothing. Bob
  and shake read in monochrome (position moves); flicker, glint and pulse
  shades are all ink, so the 1-bit picture is unchanged by them.
- The font's `/` glyph is drawn as a backslash, so the HUD reads `HP 11\15`.
  That comes from `tileset_rogue.inc` (char 47, sliced from Quale's
  `font_quale_punct_plus`) and the Apple-1 build has it too — an upstream
  POM1 nit, not a port artifact.

## Credits and licence

Game, dungeon design and asset pipeline: **VERHILLE Arnaud**.
Tiles and most sprites: **Quale**, *SCROLL-O-SPRITES* (CC-BY-3.0).
Boss: **Hexany Ives**, *Monster Menagerie* `creature_024` (CC0).
Code follows POM68K: **GPLv3**.
