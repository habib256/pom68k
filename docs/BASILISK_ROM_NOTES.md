> **Historical name.** This file started as a Basilisk II source study; it is
> now POM68K's **`$067C` ROM-behaviour oracle** — what the real, unpatched Mac
> ROM expects from the machine underneath it. Other files cite it by section
> number (`§2.2`, `§4.3`, `§5.1`, `§7.5`, `§9`), so **section numbers are
> stable IDs, not reading order**.

# `$067C` Mac ROM behaviour — oracle notes

Consult this when a machine misbehaves in a way that smells like a *ROM
expectation* rather than a device bug: UniversalInfo / DecoderInfo selection,
`defaultRSRCs` and the no-FPU SANE, HWCfgFlags, XPRAM seeds and the bytes the
ROM reads out of them, the low-memory globals the ROM installs, the A-trap
dispatcher, the boot device scan, the slot declaration-ROM grammar.

## Two evidence tiers — do not confuse them

| Tier | Where | Weight |
|---|---|---|
| **FIRSTHAND** — read out of the real ROM dumps with `tools/rominfo`, or traced on a booting POM68K machine | **§ 8** (placed first, below), plus the "verified" callouts inside §9 | **Authoritative.** Outranks everything else in this file. |
| *Secondhand* — derived from reading Basilisk II's sources | §1-§7, §9 grammar | Inventory and hypotheses. Basilisk patches ROMs POM68K runs unpatched, so its *patch sites* are a map of what real hardware must provide — but its *claims* have not been checked against a dump unless §8 says so. |

Secondhand provenance: <https://github.com/cebix/macemu>, cloned at
`/home/gistarcade/src/macemu`, commit `96e512bd` (2025-01-06). Paths below are
relative to `BasiliskII/src/`; line numbers refer to that checkout.

## § 0. Lookup index

| You arrive asking… | Go to |
|---|---|
| Is this fact real, or Basilisk hearsay? | **§8** = firsthand; everything else = secondhand |
| What does the ROM read **XPRAM `$AE`** for? | **§8.5** — ROM-resource combo, i.e. FPU vs integer `PACK 4` |
| What does the ROM read **XPRAM `$8A`** for? | §5.1 (24/32-bit boot mode), §6, §7.5 |
| What must a factory PRAM/XPRAM contain? | **§8.8** (what POM68K actually seeds), §5.1 (Basilisk's block) |
| How does the ROM find its **UniversalInfo**, and what is in it? | §2.1-§2.2 (layout) → **§8.3** (the real 13-record dump) |
| What is `defaultRSRCs` / the **HWCfgFlags FPU bit**? | §2.2 → **§8.5** |
| Which **hardware bases** will the ROM install into low memory? | §2.3 (the ROM+`$94A` pair table) → **§8.4** (real values vs the V8 map) |
| Which **low-memory globals** matter, and at what address? | §4.3 |
| How does an **A-trap** reach ROM code? Where is trap `$Axxx`? | §4.1-§4.2 → **§8.6** (`rominfo --trap A053`) |
| Where in the ROM is *X* (RAM test, InitMMU, XPRAM read, ModelID, SetupTimeK, InitADB)? | §3.2 (fixed offsets), §3.3 (pattern searches) |
| What does the **boot handoff** leave in registers? | §2.4 |
| What must my machine provide that Basilisk stubbed out? | §3.4 (unpatched = must work), §5 (Egret/ADB/PRAM service contract) |
| 24 vs 32-bit addressing, PMMU | §6 |
| What does the **Slot Manager** walk in a declaration ROM? | §9 → **§9.1** trailing block (verified on `FF7439EE`) |
| Why did vector 10 read `$00000000` on the LC II? | §7 — **closed**, and all three hypotheses were wrong |

---

# § 8. FIRSTHAND — verified on the real ROMs

*(numbered 8 for citation stability; placed first because it outranks §1-§7)*

### 8.1 How to re-derive any of this in one command

The parsers of §1-§4 are implemented in **`tools/rominfo.cpp`** (build target
`rominfo`; standalone, no emulator core):

```
build/rominfo "roms/512KB ROMs/1992-03 - 35C28F5F - Mac LC II.ROM" \
              [--resources] [--traps] [--trap A053] [--all]
```

Everything below was produced by that tool on the ROMs in `roms/` (first pass
2026-07-15, re-verified 2026-07-30). If a claim here and a claim in §1-§7
disagree, **this section wins** — and `rominfo` settles it in seconds.

### 8.2 LC II ROM `$35C28F5F` — header

| Field | Value | Note |
|---|---|---|
| size | 512 KB | |
| checksum (+$00) | `$35C28F5F` | **verifies** (32-bit sum of big-endian words from offset 4 to end). Basilisk only prints it (§1.1). |
| reset PC (+$04) | **`$0000002A`** | ROM-*relative*, not `$4080002A`. §3.1's "should read `$4080002A`" is **wrong**: the longword in the image is `$0000002A`; the `$40800000` base is added by the address map, not stored. Same on every `$067C` ROM checked. |
| version (+$08) | `$067C` | 32-bit-clean family |
| sub-version (+$12) | `$19F2` | |
| resource map (+$1A) | `$0007EC10` | |
| trap table (+$22) | `$0004C160` | |

There is **no separate initial-SSP field**: offset `$00` is the checksum, and
that is also what a CPU booting with ROM overlaid at 0 loads as SSP. (§3.1's
"initial SSP at +0 = `$00002000`-class" is likewise wrong for the dump.)

### 8.3 Universal table — LC II ROM, 13 records at `$32D8`

Full `rominfo --universal` dump (field meanings: §2.2):

| info | productKind | hwCfg | FPU bit | rom85 | defRSRC | AddrMapFlags | UnivROMFlags | decoder | model |
|---|---|---|---|---|---|---|---|---|---|
| `$0036C0` | 5 | `$DC00` | yes | `$3FFF` | 1 | `$0000773F` | `$00000000` | `$00348C` | Gestalt 11 = IIci |
| `$003700` | 5 | `$DC00` | yes | `$3FFF` | 1 | `$0000773F` | `$00000001` | `$00348C` | Gestalt 11 = IIci |
| `$003800` | 6 | `$DC00` | yes | `$3FFF` | 2 | `$00039807` | `$00000004` | `$00348C` | Gestalt 12 |
| `$0037C0` | 7 | `$DC00` | yes | `$3FFF` | 2 | **`$00000000`** | `$00000004` | `$003530` | Gestalt 13 = IIfx |
| `$003740` | 9 | `$DC00` | yes | `$3FFF` | 1 | `$0000773F` | `$00000000` | `$00348C` | Gestalt 15 |
| `$003780` | 10 | `$DC00` | yes | `$3FFF` | 1 | `$0000773F` | `$00000000` | `$00348C` | Gestalt 16 |
| `$003B66` | 12 | `$DC00` | yes | `$3FFF` | 4 | `$0000773F` | `$00000126` | `$00348C` | Gestalt 18 = IIsi |
| `$003BA6` | 13 | `$DC00` | yes | `$3FFF` | 4 | `$0000773F` | `$000001A6` | `$003AE6` | Gestalt 19 = LC |
| `$003840` | `$FD` | `$DC00` | yes | `$3FFF` | 1 | `$00000000` | `$00000000` | `$0033E8` | resolved at runtime |
| `$003880` | `$FD` | `$DC00` | yes | `$3FFF` | 1 | `$00000000` | `$00000000` | `$00348C` | " |
| `$0038C0` | `$FD` | `$DC00` | yes | `$3FFF` | 2 | `$00000000` | `$00000000` | `$003530` | " |
| **`$003BE6`** | `$FD` | **`$CC00`** | **no** | `$7FFF` | **4** | `$00000000` | `$00000000` | **`$003AE6`** | " — **the LC II-shaped record** |
| `$003900` | `$FD` | `$0000` | no | `$FFFF` | 1 | `$00000000` | `$00000000` | `$003344` | " (all-empty decoder) |

Firsthand consequences:

- `productKind $FD` = *unset*; the V8-class ROM resolves the model at runtime
  (cf. the `$5FFFFFFC` machine-ID read, §3.3). Five of thirteen records are
  `$FD`, so "which record am I?" is a **runtime** question on this ROM.
- The **LC-class DecoderInfo `$3AE6`** is shared by the named LC record and by
  the `$FD` record at `$3BE6` (hwCfgWord `$CC00` = **no FPU**, rom85 `$7FFF`,
  defaultRSRCs 4) — the LC II shape.
- **`AddrMapFlags $773F` is not universal.** The IIci/IIsi/LC records carry it
  (this is the value `V8Memory` reproduces, `src/V8Memory.cpp:386-389`), the
  **IIfx record carries `$00000000`**, Gestalt 12 carries `$00039807`, and every
  `$FD` record carries `$00000000`. Do not quote "$773F" as an invariant.
- The FPU bit is **hwCfgWord bit 12** = bit 28 of the long at +$10 (§2.2). The
  two spellings in POM68K docs ("bit 28", "bit 12") are the same bit; `$DC00`
  has it, `$CC00` does not.

### 8.4 LC-class DecoderInfo `$3AE6` — hardware bases vs the V8 map

Read through the ROM+`$94A` (decoder index, low-mem global) pair table (§2.3).
Every populated entry matches `V8Memory`:

| decoder[] | base | → LMG | POM68K |
|---|---|---|---|
| 2 | `$50F00000` | `$1D4` VIA1 | `V8Memory` VIA1 |
| 3 / 4 | `$50F04000` | `$1D8` SCCRd / `$1DC` SCCWr | `Scc8530` |
| 5 | `$50F16000` | `$1E0` SWIM | `Swim1` |
| 8 / 9 / 10 | `$50F10000` / `$50F12000` / `$50F06000` | `$C00` / `$C04` / `$C08` | SCSI triplet over `Ncr5380` |
| 12 | `$50F14000` | `$CC0` ASCBase | `AscV8` |
| **13** | **`$50F26000`** | `$CEC` VIA2/RBV | `PseudoVia` (`src/V8Memory.cpp:381,525`) |
| 11 (real VIA2) | **`$00000000`** | `$CEC` | *unpopulated on V8 — the pseudo-VIA is decoder[13], not [11]* |
| 6, 7, 15, 16, 17, 18 | `$00000000` | `$B0A`/`$312`/`$266`, `$C00-$C08`, `$1D8`/`$1DC`, `$1E0`, `$CEC` | unused on this board |

Contrast (same ROM, other boards): the IIci-class decoder `$348C` puts the real
VIA2 at decoder[11] = `$50F02000` and leaves decoder[13] at 0 — i.e. **11 vs 13
is exactly the "discrete VIA2" vs "RBV/pseudo-VIA" distinction**, and it is
selected purely by which DecoderInfo the record points at. The IIfx decoder
`$3530` moves SCC to `$50F04020` (decoder[17]), SWIM to `$50F12020`
(decoder[16]), ASC to `$50F10000`, VIA2 to decoder[18] `$50F1A000`.

### 8.5 ROM resources, the no-FPU SANE, and **XPRAM `$AE`**

`rominfo --resources` on the LC II ROM: **63 resources**. Notable:

| offset | type/id | size | name |
|---|---|---|---|
| `$06C3E0` | `DRVR 4` | 18112 | `.Sony` |
| `$06AB90` | `SERD 0` | 6192 | |
| `$073940` | `PACK 4` | 9536 | |
| `$070AC0` | `PACK 4` | 4352 | |
| `$066900` | `DRVR 9` | 17008 | `.MPP` (LocalTalk — see §8.8) |
| `$065210` | `DRVR 10` | 5824 | `.ATP` |
| `$075EA0` | `DRVR 40` | 2608 | `.XPP` |
| `$051410` | `DRVR 3` | 2368 | `.Sound` |

**No `DRVR 51` (`.EDisk`) exists in this ROM** — §1.3/§3.3 list it because
Basilisk hunts for it; do not expect it on the LC II.

**Two `PACK 4` resources ⇒ the LC II ROM does carry a no-FPU SANE** (§1.3
criterion). So a bare, FPU-less LC II failing with system error 10 is a
*selection* problem, not a missing SANE. The selector, traced end to end
(CHANGELOG 2026-07-21, on the `FF7439EE` ROM) and re-verified byte-for-byte in
the LC II ROM (2026-07-30):

- Each ROM resource-directory **entry carries an 8-byte "combo mask" at entry
  +$00** — a field Basilisk never reads (its walker starts at +8, §1.3). In
  *both* ROMs: integer `PACK 4` entry mask = `08 00 00 00 …`, FPU `PACK 4`
  entry mask = `70 00 00 00 …` (bit numbering is MSB-first from entry+$00 bit
  0, so `$70000000` = combos 1-3, `$08000000` = combo 4).
- The InitResources walkers test **combo bit N where N = XPRAM byte `$AE`**,
  read with `_ReadXPRam #$1_00AE` (boot walker `$4081AB28`, System-era walker
  `$408A07A6`).
- Validation (`$4084BF86`): N = 0, or N > the directory header's max (4), falls
  back to **`UniversalInfo+$16` = defaultRSRCs**; combo 4 is then *promoted* to
  3 when **HWCfgFlags bit 12 says "FPU fitted"**. There is **no 3→4 demotion** —
  zapping PRAM is Apple's own cure for a stale FPU combo.
- Applied to the LC II shape (§8.3): `$AE` = 0 → fall back to defaultRSRCs = 4
  → mask `$08000000` → the **integer** `PACK 4`; hwCfgWord `$CC00` has bit 12
  clear so there is no promotion to 3. The mechanism therefore exists and is
  correctly parameterised on this ROM — see the open item in
  `TODO.md § 5 (LC II / V8) — "No-FPU SANE"`, which still wants the 030 path
  re-tested rather than re-diagnosed.

### 8.6 Trap → ROM offset (breakpoint fodder for `lcii_trace`)

LC II ROM: `_ClkNoMem $A053` → ROM `$04B1E4`; `_ADBOp $A07C` → ROM `$03A3DC`.
Any other trap: `rominfo --trap XXXX` (format: §4.2).

### 8.7 Other ROMs spot-verified

| ROM | checksum | sub-ver | resource map | trap table | resources |
|---|---|---|---|---|---|
| Mac LC II 512 KB | `$35C28F5F` ✓ | `$19F2` | `$07EC10` | `$04C160` | 63, `PACK 4` ×2 |
| LC 475 / Quadra 605 1 MB | `$FF7439EE` ✓ | `$26F1` | `$07EC10` | `$0D2800` | 151, `PACK 4` ×2 |

Both carry reset PC `$0000002A`. On `FF7439EE` the two `PACK 4` bodies are at
`$073940` (integer, entry `$073910`) and `$0E9A20` (FPU, entry `$0E99F0`) —
the entry masks quoted in §8.5.

Declaration-ROM trailer of `FF7439EE` (grammar: §9.1) — read directly off the
image: last 20 bytes are
`00FF0C92 00000000 00000000 0101 5A932BC7 000F`, i.e. **fhLength = 0 and
fhCRC = 0 in the real built-in declaration ROM** (Apple leaves them unset; do
not treat them as validated fields). The live directory offset is the 24-bit
signed field at ROM−`$14`: `$FF0C92` = −`$F36E` → directory at ROM offset
`$F0C7E`, one entry only (the board sResource "Unknown Macintosh"); the video
sResources are inserted at boot from the detected DrHW. POM68K's builder
implements the same trailer and the §9.1 CRC (`src/DeclRom.h`,
`src/DeclRom.cpp:85-101`).

### 8.8 What this put into the POM68K code — and the one Basilisk default rejected

`Egret::factoryDefaults()` (`src/Egret.cpp:47-105`) seeds Basilisk's known-good
XPRAM block when no battery file carries the system's `'NuMc'` validity
signature at `$0C-$0F`: `'NuMc'`, `$01` = DynWait, the standard classic-PRAM
block at `$08-$1F`, OSDefault = MacOS at `$76-$77`, plus a POM68K-only built-in
video sPRAM seed at `$58` (`$83` = 8 bpp). A valid signature also spares the
ROM's cold-PRAM detours (full-RAM burn-in, PRAM re-init) on a first boot.

Two deliberate divergences from Basilisk — **both load-bearing, do not "restore"
them**:

1. **XPRAM `$8A` is NOT forced to `$05`.** Basilisk forces "32-bit always"
   (§5.1, §6) because it cannot survive the 24-bit boot + `_SwapMMUMode` path.
   The V8 machine handles the ROM's real 24-bit startup, so POM68K leaves `$8A`
   alone.
2. **XPRAM `$E0-$E3` is left at zero**, *not* set to Basilisk's
   `$00/$F1/$00/$0A`. An earlier revision of this file recorded that
   substitution as "load-bearing"; **that was wrong and has been reverted**
   (CHANGELOG 2026-07-20/21). Primary sources (`LAPMgrEqu.a` ATalkPRAM,
   `NetBootlmgr.a InstallE`): `$E0-$E3` is only the **connection selector**
   (low byte = `'atlk'` resource id, 0 = built-in LocalTalk), and a *bad* id
   falls back to built-in — so Basilisk's value disabled nothing. The real
   "AppleTalk inactive at boot" flag is the classic-PRAM **SPConfig** byte, low
   nibble = port B use (1 = useATalk, 2 = useAsync); on Egret machines SysParam
   bytes 0-15 live at **XPRAM `$10-$1F`, so SPConfig = XPRAM `$13`**, and
   `$22` = both ports async ⇒ `.MPP` (present in ROM, §8.5) never brings up
   LocalTalk. AppleTalk 57.x *self-heals* SPConfig 0/`$F` → 1 (= active), so
   `Egret::factoryDefaults` reseeds `$13` even when `'NuMc'` is already there.

`lcii_trace` logs the WarmStart `'WLSC'` milestone at `$CFC` (§4.3) — the ROM's
own "low memory is valid" marker — and applies the same factory defaults as the
GUI (`tests/lcii_trace.cpp:461`).

---

# Secondhand tier — the Basilisk II study (§1-§7, §9)

Basilisk II runs **real, patched** ROMs. Every patch site is therefore a map of
something a *real* machine (or POM68K's model) must provide, and every structure
it parses is documentation of the ROM's internal layout. POM68K runs the
**unpatched** ROM, so the value here is the inventory, not the patches.

## § 1. How Basilisk II identifies ROM families

### 1.1 Detection = version word at ROM offset 8, nothing else

There is **no checksum table** in Basilisk II. `CheckROM()` reads one
big-endian word at ROM offset **8** (`rom_patches.cpp:830-842`):

```cpp
ROMVersion = ntohs(*(uint16 *)(ROMBaseHost + 8));
```

The known families (`include/rom_patches.h:25-31`):

```cpp
enum {
    ROM_VERSION_64K     = 0x0000,  // Original Macintosh (64KB)
    ROM_VERSION_PLUS    = 0x0075,  // Mac Plus ROMs (128KB)
    ROM_VERSION_CLASSIC = 0x0276,  // SE/Classic ROMs (256/512KB)
    ROM_VERSION_II      = 0x0178,  // Not 32-bit clean Mac II ROMs (256KB)
    ROM_VERSION_32      = 0x067c   // 32-bit clean Mac II ROMs (512KB/1MB)
};
```

Only `ROM_VERSION_CLASSIC` and `ROM_VERSION_32` are actually patchable
(`PatchROM()`, `rom_patches.cpp:1834-1852`). The LC II ROM is in the
`ROM_VERSION_32` family (verified §8.2), and `patch_rom_32()` explicitly
carries `ROMSize <= 0x80000` branches for the 512 KB members (see §3.3).
Accepted ROM sizes are 64K/128K/256K/512K/1MB (`Unix/main_unix.cpp:691-697`).
The ROM checksum at offset 0 is only *printed* (`rom_patches.cpp:301`), never
verified — `rominfo` does verify it (§8.2). For Classic ROMs the ROM's own
checksum complaint is even patched out (`rom_patches.cpp:859-861`).

Sub-version is the word at offset **18** (`rom_patches.cpp:303`).

CPU configuration derived from the family (`main.cpp:72-100`): `$067C` →
CPUType 2-4 (68020/030/040), `TwentyFourBitAddressing = false`; UAE memory
map puts the ROM at Mac address **`$40800000`** for `$067C` ROMs
(`uae_cpu/basilisk_glue.cpp:81-95`) — the canonical 32-bit-clean "$40-prefixed"
ROM address. For 24-bit families the ROM sits at `$400000` (Plus/Classic) or
`$A00000` (Mac II).

### 1.2 ROM header fields Basilisk II uses

| ROM offset | Size | Meaning | Cited at |
|---|---|---|---|
| +$00 | long | checksum (printed only; **also the SSP a ROM-overlaid CPU loads**, §8.2) | `rom_patches.cpp:301` |
| +$04 | long | reset PC — `$067C` entry point is ROMBase+`$2A` (stored ROM-relative, §8.2) | `uae_cpu/newcpu.cpp:1185` |
| +$08 | word | ROM version (family ID) | `rom_patches.cpp:833` |
| +$12 (18) | word | sub-version | `rom_patches.cpp:303` |
| +$1A (26) | long | offset of **ROM resource map** | `rom_patches.cpp:94, 168, 304` |
| +$22 (34) | long | offset of **compressed ROM trap address table** | `rom_patches.cpp:125, 305` |

### 1.3 ROM resource map format (as parsed)

`find_rom_resource()` (`rom_patches.cpp:90-116`): the long at ROM+`$1A` points
to a header whose first long is the offset of the first resource entry; each
entry (all offsets ROM-relative):

| entry offset | meaning |
|---|---|
| +0 | **8-byte combo mask** — Basilisk skips it; it is the FPU/no-FPU selector (§8.5) |
| +8 | long: offset of next entry (0 = end) |
| +12 | long: offset of resource **data** |
| +16 | long: type (FOURCC) |
| +20 | word: ID |
| +23 | byte: name length, name follows at +24 (`rom_patches.cpp:176-178`) |

Resource data is preceded by a length long at data−8 (`rom_patches.cpp:181`,
`emul_op.cpp:532`: size = `ReadMacInt32(adr-8) & 0xffffff`). Resources Basilisk
locates this way: `DRVR 4` (`.Sony`), `DRVR 51` (`.EDisk` — **absent from the
LC II ROM**, §8.5), `SERD 0`, `PACK 4` (SANE — **two** `PACK 4` ⇒ the ROM has a
no-FPU SANE; one ⇒ it requires an FPU, `rom_patches.cpp:1812-1815`).

## § 2. Universal ROM structures (UniversalInfo / DecoderInfo)

### 2.1 Locating UniversalInfo

`patch_rom_32()` finds the boot UniversalInfo record by scanning ROM offsets
**`$3400-$3C00`** for the 8-byte signature `DC 00 05 05 3F FF 01 00` and
subtracting **`$10`** (`rom_patches.cpp:1193-1197`):

```cpp
static const uint8 universal_dat[] = {0xdc,0x00,0x05,0x05,0x3f,0xff,0x01,0x00};
base = find_rom_data(0x3400, 0x3c00, universal_dat, sizeof(universal_dat));
UniversalInfo = base - 0x10;
```

That signature is the hwCfgWord/productKind/rom85/defaultRSRCs block of the
*IIci* record (`DC00` = hwCfgWord, `05` = productKind, `3FFF` = rom85) — i.e.
Basilisk always boots as a IIci. The diagnostic scanner
(`list_universal_infos()`, `rom_patches.cpp:276-296`) is more general, and is
what `rominfo` implements: scan from `$3000` for the long `DC000505`, back up
16 bytes to the info record, then walk *backwards* for a long whose value
equals the distance to that record — that long is an entry of the **Universal
table**, a null-terminated array of **self-relative 32-bit offsets**, one per
UniversalInfo record (one per machine the ROM supports). Real dump: §8.3.

### 2.2 UniversalInfo record layout (offsets Basilisk reads/writes)

| offset | size | field | used at |
|---|---|---|---|
| +$00 | long | self-relative offset to **DecoderInfo / AddrMap** | `rom_patches.cpp:1217`, `emul_op.cpp:106` |
| +$0C | long | self-relative offset to **nuBusInfo** (16 bytes, one per slot `$9`-`$E` area; Basilisk writes `03 08 08 …` to disable slots) | `rom_patches.cpp:1199-1203` |
| +$10 | long | **HWCfgFlags/IDs** (hi word = hwCfgWord, then productKind byte) — **bit 28 = hwCfgWord bit 12 = FPU fitted** | `emul_op.cpp:101-105`, `rom_patches.cpp:262` |
| +$12 | byte | **productKind** = Gestalt machine ID − 6 (LC II Gestalt 37 ⇒ productKind **31**); `$FD` = unset/runtime (§8.3). Basilisk overwrites it from the `modelid` pref (default 5 = IIci) | `rom_patches.cpp:261, 1206-1207`, `prefs_items.cpp:56,88` |
| +$14 | word | **rom85** word | `rom_patches.cpp:263` |
| +$16 | byte | **defaultRSRCs** — the ROM-resource combo fallback (§8.5); 4 = "FPU optional" | `rom_patches.cpp:1234-1238` |
| +$18 | long | **AddrMapFlags** (not a constant — §8.3) | `emul_op.cpp:99` |
| +$1C | long | **UnivROMFlags** | `emul_op.cpp:100` |

The Gestalt-ID↔name table (`MacDesc[]`, `rom_patches.cpp:196-257`) contains
`{"Mac LCII", 37}` at line 227; `tools/rominfo.cpp:66-79` carries the abridged
copy.

### 2.3 DecoderInfo and the hardware-base → low-mem-global copy table

The DecoderInfo (a.k.a. AddrMap) is an array of **32-bit hardware base
addresses**. `patch_rom_32()` documents that the ROM itself contains, at fixed
ROM offset **`$94A`**, a word-pair table that the ROM's StartInit uses to copy
decoder entries into low-memory globals (`rom_patches.cpp:1216-1227`):

```cpp
base = ROMBaseMac + UniversalInfo + ReadMacInt32(ROMBaseMac + UniversalInfo); // decoderInfoPtr
wp = (uint16 *)(ROMBaseHost + 0x94a);
while (*wp != 0xffff) {
    int16 ofs = ntohs(*wp++);   // offset in decoderInfo (/4)
    int16 lmg = ntohs(*wp++);   // address of LowMem global
    if (lmg != 0xcc0)           // don't touch ASCBase
        WriteMacInt32(base + ofs*4, ScratchMemBase);
}
```

I.e. **(decoderInfo index, low-mem global address) pairs, `$FFFF`-terminated,
at ROM+`$94A`** in every `$067C` ROM. Basilisk redirects all of them (except
ASCBase `$CC0`) to a scratch RAM page so the ROM's later hardware pokes land
harmlessly. For POM68K this table is a **self-describing list of every hardware
base the ROM will install into low memory** — dumped for real in §8.4.

### 2.4 Boot handoff register contract (EMUL_OP_RESET)

`M68K_EMUL_OP_RESET` (`emul_op.cpp:84-111`) reproduces what the skipped
hardware-detection code must leave behind before the common boot path at
ROM+`$BA`:

- **BootGlobs** built at top of RAM, at `RAMBase+RAMSize-0x1C`: +0 first-bank
  base, +4 bank size, +8 `$FFFFFFFF` end-of-bank-table marker (the memory-bank
  table grows downward from top of RAM).
- `d0` = AddrMapFlags (+$18), `d1` = UnivROMFlags (+$1C), `d2` = HWCfgFlags/IDs
  (+$10, bit 28 = FPU), `a0` = DecoderInfo ptr, `a1` = UniversalInfo ptr,
  `a6` = BootGlobs, `a7` = RAMBase+`$10000`.

`M68K_EMUL_OP_PATCH_BOOT_GLOBS` (`emul_op.cpp:191-197`) documents BootGlobs-
relative fields written slightly later (a4 = just past BootGlobs): `a4-20` =
MemTop, `a4-26`/`a4-25` = MMU-type/flags bytes ("No MMU": byte at −26 = 0,
bit 0 of byte at −25 set).

## § 3. Boot sequence: what Basilisk II patches in `$067C` ROMs, and why

Everything in `patch_rom_32()` (`rom_patches.cpp:1187-1832`). Two kinds of
site: **fixed offsets** (identical layout assumed across *all* `$067C` ROMs —
the early boot code is common) and **byte-pattern searches** (with explicit
`ROMSize <= 0x80000` ranges = the 512 KB V8/LC-class members).

> **Unverified tier.** The offsets in §3.2 have *not* been checked against a
> POM68K ROM dump; they are Basilisk's assumptions about the family. Treat them
> as leads for `capstone`/`lcii_trace` breakpoints, not as facts.

### 3.1 CPU entry

The UAE core does not fetch the reset vector: it jumps straight to
**ROMBase+`$2A` with A7=`$2000`, SR=`$2700`, VBR=0**
(`uae_cpu/newcpu.cpp:1182-1199`). So for `$067C` ROMs the reset vector at ROM+4
points to base+`$2A` — **verified: the stored longword is `$0000002A`, and
offset 0 is the checksum, not an SSP constant (§8.2).**

### 3.2 Fixed-offset patch map (early boot, common to all `$067C` ROMs)

| ROM offset | What the real code does there | Basilisk patch | Lines |
|---|---|---|---|
| $8C | start of **hardware detection + RAM sizing/tests**; ends at $BA | `EMUL_OP_RESET` + `jmp ROM+$BA` | 1240-1245 |
| $C2 | call GetHardwareInfo | 2×NOP | 1247-1250 |
| $C6 | **init VIA1/VIA2** (30 bytes of calls) | 15×NOP | 1252-1268 |
| $10E | finalize BootGlobs (MemTop, MMU flags) | `EMUL_OP_PATCH_BOOT_GLOBS` | 1367-1370 |
| $190 | EnableExtCache | 2×NOP | 1406-1409 |
| $226 | **EnableOneSecInts** (starts with `lea $xxx` = 41 F9) | 5×NOP | 1513-1522 |
| $230 | **EnableParityPatch / Enable60HzInts** | 5×NOP | 1524-1538 |
| $2EE | **EnableSlotInts** | 5×NOP | 1615-1623 |
| $490 | **CompBootStack** — compute boot SP; replacement derives it from BufPtr ($10C) and SysZone ($2A6): `SP = ((BufPtr+SysZone)/2 & ~1) - $400` | replaced + `EMUL_OP_FIX_MEMSIZE` | 1540-1553 |
| $7C0 | **CPU type test** (returns type in d7) | `moveq #CPUType,d7; rts` | 1270-1273 |
| $800 | **SetupTimeK** — DBRA speed calibration; writes TimeDBRA $D00, TimeSCCDB $D02, TimeSCSIDB $B24, TimeRAMDBRA $CEA | canned `#10000` writes | 1415-1430 |
| $9A0 | InitSCSI | `rts` | 1393-1395 |
| $9C0 | InitIWM | `rts` | 1389-1391 |
| ~$A00-$B00 | `clr.l (a2)+ / move.w a2,d3 / bne` loop: **clear from end of BootGlobs up to end of RAM (address xxxx0000)** — pattern `42 9A 36 0A 66 FA` | clr NOPed | 1275-1283 |
| ~$A00-$A80 | InitSCC (pattern `08 38 00 01 0D D1 67 04` = `btst #1,$DD1`) | `rts` | 1372-1377 |
| $1142 | open `.Sound` driver during InitDevices | `EMUL_OP_INSTALL_DRIVERS` | 1564-1566 |
| $1144 | access SonyVars | NOPs | 1568-1575 |
| $4232 | (ROM32 only) access **$50F1A101** (I/O space probe) | 5×NOP if pattern matches | 1379-1387 |
| $5B78 | **GetDevBase** mangles frame-buffer base | short-circuited | 1625-1630 |
| $9BC4 | **VIA1 level-1 interrupt handler** — normally reads VIA IFR to classify | forced `moveq #2,d0` (always 60 Hz) | 1817-1823 |
| $9F4C | DisableIntSources | `rts` | 1411-1413 |
| $A296 | 60 Hz handler body | `EMUL_OP_IRQ` inserted | 1825-1830 |
| $A8A8 / $A662 (old ROMs) or $B2C6A / $B2D2E (ROM22+) | **InitADB VIA transactions** | NOPs | 1577-1613 |
| $B0E2 | InitTimeMgr VIA timer writes | early return | 1469-1473 |
| $CCAA | InitMemMgr's "handle at 0" setup | fake handle → scratch mem | 1441-1453 |
| $1B8F4 | **vCheckLoad** resource-load hook | `jmp` to glue that chains via LM vector $7F0 then `EMUL_OP_CHECKLOAD` | 1776-1788 |

Boot beep: for `$067C` ROMs the chime is silenced indirectly by killing
**InitASC** (search `$4000-$5000` for `26 68 00 30 12 00 EB 01`,
`rom_patches.cpp:1397-1404`); for Classic ROMs the startup sound call at `$6A`
is NOPed (`rom_patches.cpp:868-871`).

### 3.3 Pattern searches with explicit 512 KB (V8/LC-class) ranges

The closest thing Basilisk has to LC II-specific knowledge — the same routine
lives in a different region in 512 KB vs 1 MB `$067C` ROMs:

| Routine | 512 KB ROM search range | 1 MB range | Pattern (bytes) | Lines |
|---|---|---|---|---|
| **InitMMU** CPU-type dispatch (`cmp.w #N,d7; bhi`) | $4000-$50000, `0C 47 00 03 62 00 FE` (68030 max) | $80000-$90000, `0C 47 00 04 62 00 FD` (68040 max) | NOPed, `moveq #0,d0` | 1285-1301 |
| **InitMMU** RBV presence test (`btst #13,d6; beq`) | $4000-$50000 | $80000-$90000 | `08 06 00 0D 67` → `bra` | 1303-1314 |
| **InitMMU** actual MMU setup (`cmp.b #1,-26(a6); bne` — tests the BootGlobs MMU byte!) | $4000-$50000 | $80000-$90000 | `0C 2E 00 01 FF E6 66 0C 4C ED 03 87 FF E8` | 1316-1325 |
| Read **XPRAM** (ROM10/11 style, VIA at $50F00000) | $40000-$50000 | — | `26 4E 41 F9 50 F0 00 00 …` | 1327-1343 |
| Read **XPRAM** (ROM15 style) | — | $80000-$90000 | `48 E7 E0 60 02 01 00 70 0C 01 00 20` | 1344-1353 |
| **ModelID read from $5FFFFFFC** (VIA-decoded machine-ID register — how a `$FD` productKind record gets resolved, §8.3) | $4000-$5000 (`45 F9 5F FF FF FC 20 12`, ROM27/32) and $40000-$50000 (`20 7C 5F FF FF FC 72 07 C2 90`, ROM20) | | forced `d0=0` | 1475-1496 |
| **VIA2 write through LM $CEC** (`movea.l $CEC,a0; move.w #$90xx,…`) | $A000-$A400 (all) + $40000-$44000 (ROM19/20) | | `rts` | 1644-1658 |
| NuBus slot probe | $5000-$6000 | | `45 FA 00 0A 42 A7 10 11` | 1502-1511 |
| ClkNoMem fallback (when the trap entry is a `jmp (a5)` thunk) | — | $B0000-$B8000 | `40 C2 00 7C 07 00 48 42` | 1355-1365 |
| BlockMove PTEST / SANE PTEST | skipped for ≤512 KB ROMs (`ROMSize > 0x80000` guard — V8-class ROMs never drive an '040) | $87000-$87800 / whole ROM | | 1660-1686 |
| MemoryDispatch un-implementation | $4F100-$4F180 | | `30 3C A8 9F A7 46 30 3C A0 5C A2 47` | 1688-1695 |
| physical/logical RAM size fixup (writes $1EF4/$1EF8) | $4C000-$4C080 | | | 1555-1562, `emul_op.cpp:204-210` |
| `.EDisk` ROM-area scan (`$ROMBase..$E00000`) | inside `DRVR 51` (**not present in the LC II ROM**, §8.5) | | `D5 FC 00 01 00 00 B5 FC 00 E0 00 00` | 1697-1708 |

Driver replacement: `.Sony` (`DRVR 4`) is overwritten in place with an EMUL_OP
stub driver; `.Disk`/`.AppleCD` are appended at +$100/+$200 inside the same
resource, icons at +$400.., scrap patches at +$C00/+$D00
(`rom_patches.cpp:1710-1727, 1794-1810`). Serial drivers overwrite `SERD 0`
(+$100..+$400, `rom_patches.cpp:1729-1738`). A **slot declaration ROM** (board
+ video + CPU + Ethernet sResources) is synthesized into the **last bytes of
the ROM image** — grammar in §9.

### 3.4 What is *not* patched — and therefore must work

Everything else in the `$067C` boot path executes natively: exception-vector-
table installation, trap-dispatch-table construction, Memory Manager init,
Resource Manager init, the entire Slot Manager (fed by the fake declaration
ROM), Gestalt, and the boot-volume search. **That untouched set is the "must be
correct" contract for POM68K's CPU core and address map.**

## § 4. Trap dispatcher details

### 4.1 The RAM dispatch tables at $400 / $1400 / $E00: not referenced

**Basilisk II never reads, writes, or searches for the RAM trap dispatch
tables.** There is no occurrence of the OS table base `$400` or a Toolbox table
base (`$1400`/`$E00`/`$1E00`) anywhere in `BasiliskII/src` (verified by grep).
It cannot *confirm* those bases; it relies on the ROM's own dispatcher. All
trap manipulation goes through the official traps, executed *as* traps:

- `SetOSTrapAddress` = `$A247` (`rom_patches.cpp:717,722`)
- `SetToolBoxTrapAddress` = `$A647` (`emul_op.cpp:242,249`)
- `DrvrInstallRsrvMem` = `$A43D`/`$A53D`, `HLock` `$A029`, `Open` `$A000`,
  `NewPtrSysClear` `$A71E` (`rom_patches.cpp:709-810`)

`Execute68kTrap()` (`uae_cpu/basilisk_glue.cpp:181-219`) pushes the raw A-trap
word + a magic `M68K_EXEC_RETURN` (`$7100`) word on the guest stack, points PC
at it, and runs the CPU. The A-trap word executes as an instruction →
`op_illg()` raises `Exception(0xA)` (`uae_cpu/newcpu.cpp:1250-1252`) → handler
fetched from `VBR + $28` (`newcpu.cpp:803`) → **the guest ROM's Line-A
dispatcher does the whole job**, including reading the RAM dispatch tables. So
every host-initiated Mac call in Basilisk II is living proof that guest vector
10 + both dispatch tables are functional from InitDevices time onward.

### 4.2 The ROM-side compressed trap address table (header +$22)

`find_rom_trap()` (`rom_patches.cpp:123-155`) documents the format of the
*ROM's* master trap table (the data the ROM expands into the RAM tables at
boot). Pointer: long at ROM+`$22`. Encoding — a cumulative-offset byte stream,
**Toolbox traps first (`$A800-$ABFF`, 1024 entries), then OS traps
(`$A000-$A3FF`, 1024 entries)**:

| byte | meaning |
|---|---|
| `$80` | trap unimplemented |
| `$FF` | next 4 bytes = absolute routine offset (replaces the accumulator) |
| high bit set (≠`$80`,`$FF`) | add `(b & $7F) << 1` to the offset accumulator |
| high bit clear | 2-byte big-endian value, add `value << 1` (0 ⇒ end) |

Implemented in `tools/rominfo.cpp:158-197` (`rominfo --trap A053`, §8.6).
Basilisk resolves through it: `_ClkNoMem` `$A053`, `_InsTime` `$A058`,
`_RmvTime` `$A059`, `_PrimeTime` `$A05A`, `_PowerOff` `$A05B`, `_ADBOp`
`$A07C`, `_SCSIDispatch` `$A815`, `_PutScrap` `$A9FE`, `_GetScrap` `$A9FD`
(`rom_patches.cpp:1355, 1741-1810`).

(For contrast: SheepShaver's NewWorld ROMs use a *flat* table —
`SheepShaver/src/rom_patches.cpp:260-268`: Toolbox = `lp + 4*(trap & $3FF)`,
OS = `lp + 4*((trap & $FF) + $400)`. Not applicable to `$067C` ROMs, but a
useful cross-check of the OS/Toolbox split.)

### 4.3 Low-memory globals Basilisk II relies on

| Addr | Global | Use | Cited at |
|---|---|---|---|
| $10C | BufPtr | boot stack computation | `rom_patches.cpp:1542` |
| $11C | UTableBase | driver install (unit table) | `rom_patches.cpp:728,746` |
| $14C | EventQueue head (QHdr $14A+2) | idle-sleep test | `emul_op.cpp:562` |
| $16A | Ticks | incremented per 60 Hz | `emul_op.cpp:451` |
| $1D4 | VIA (VIA1 base) | VIA-poking code patched in resources | `rsrc_patches.cpp:199,258` |
| $1D8 / $1DC | SCCRd / SCCWr | `.Infra` driver patch | `rsrc_patches.cpp:323` |
| $1E0 | SWIM/IWM base | decoder pair table (§8.4) | — |
| $28E | ROM85 | DRVR 41 patch | `rsrc_patches.cpp:347-352` |
| $2A6 | SysZone | boot stack computation | `rom_patches.cpp:1544` |
| $2AE | ROMBase | boot-resource patches | `rsrc_patches.cpp:107,132` |
| $2B6 | ExpandMem | SynchIdleTime patch | `rsrc_patches.cpp:69`, `emul_op.cpp:564` |
| $308 | DrvQHdr | free-drive-number scan | `macos_util.cpp:57` |
| $7F0 | vCheckLoad chain vector (original routine ptr) | resource-patch glue | `rom_patches.cpp:1783` |
| $824 | ScrnBase | Classic-ROM video patch writes fb base here | `rom_patches.cpp:1043-1051` |
| $828/$82A | MTemp (y/x), $82C/$82E RawMouse | absolute mouse injection | `adb.cpp:399-402` |
| $8CE/$8CF | CrsrNew/CrsrCouple | cursor-changed flag | `adb.cpp:403` |
| $8FC | JIODone | driver IOReturn path | `rom_patches.cpp:357,405,…` |
| $B24 | TimeSCSIDB | SetupTimeK | `rom_patches.cpp:1424` |
| $C00/$C04/$C08 | SCSI base triplet | decoder pair table (§8.4) | — |
| $CC0 | ASCBase | faked ASC register block (version byte at +$800 must read `$0F`) | `rom_patches.cpp:1226`, `emul_op.cpp:252-260` |
| $CEA | TimeRAMDBRA | SetupTimeK | `rom_patches.cpp:1427` |
| $CEC | VIA2 / RBV / pseudo-VIA base | VIA2 write suppression; **decoder[11] vs [13] picks which**, §8.4 | `rom_patches.cpp:1645` |
| $CF8 | ADBBase | ADB injection (see §5) | `adb.cpp:331`, `rom_patches.cpp:694` |
| $CFC | WarmStart flag = `'WLSC'` when low memory is valid | gates all host→Mac callbacks (`HasMacStarted()`); POM68K logs it, §8.8 | `include/macos_util.h:278-281` |
| $D00/$D02 | TimeDBRA / TimeSCCDB | SetupTimeK | `rom_patches.cpp:1418-1421` |
| $DD8 | pointer consulted by GetDevBase frame-base mangling | | `rom_patches.cpp:1634` |
| $1EF4/$1EF8 | logical / physical RAM size (boot globals) | FIX_MEMSIZE | `emul_op.cpp:204-210` |
| $1EFC | boot-mode flag byte loaded from XPRAM `$8A` by `_ReadXPRam` | the LC II vector-10 chain ran through here, §7 | POM68K trace, CHANGELOG 2026-07-15 |

## § 5. Egret / Cuda / ADB / PRAM: what Basilisk II stubs at ROM level

Basilisk II contains **zero** Egret/Cuda/PMU code (grep confirms). It bypasses
that entire layer by replacing the *services* the ROM implements on top of it.
The replacement list = **the service contract POM68K's Egret/Cuda must honour**.

### 5.1 `_ClkNoMem` (`$A053`) — RTC + PRAM/XPRAM access

(`emul_op.cpp:113-179`.) Protocol as the ROM presents it: command in `d1`, data
in `d2`, bit 7 of d1 = read. `(d1 & $78) == $38` selects **extended XPRAM**
addressing, register = `((d1<<5)&$E0)|((d1>>10)&$1F)` (256 bytes); otherwise
classic clock-chip registers `(d1>>2)&$1F`: regs 0-3 = seconds counter bytes,
regs 8-11 and 16-31 = classic PRAM. Returns status 0 in d0, data in d1/d2.
Notable byte semantics Basilisk forces:

- **XPRAM `$8A` |= `$05`: "32-bit mode is always enabled"** (`emul_op.cpp:127,
  149-150`) — the startup addressing-mode byte the `$067C` ROM consults to
  decide 24 vs 32-bit boot. **POM68K deliberately does not force it** (§8.8).
- `$E0-$E3` rewritten to "disable LocalTalk" — **this does not do what Basilisk
  thinks; POM68K rejected it** (§8.8) (`emul_op.cpp:129-144`).
- XPRAM signature `'NuMc'` at `$0C-$0F`, default PRAM values
  (`main.cpp:106-133`); boot volume/driver at `$78-$7B`.
- Not modelled by Basilisk but read by the ROM: **XPRAM `$AE`** = the
  ROM-resource combo (§8.5).

### 5.2 The ROM's *second* PRAM path

Raw XPRAM readers inside the ROM talk to the VIA at `$50F00000` directly,
bypassing `_ClkNoMem`, and are stubbed separately
(`rom_patches.cpp:1327-1353`) — evidence the ROM has **two** PRAM paths.

### 5.3 `_ADBOp` (`$A07C`)

Replaced wholesale by `adbop_patch` (`rom_patches.cpp:682-702`, handler
`emul_op.cpp:212-214`, `adb.cpp:100-…`): masks interrupts, runs the host ADB
engine, then calls the caller's completion routine with `a0` = data,
`a1` = completion, `a2` = buffer, `a3` = `ADBBase` (from `$CF8`). Command byte
format (`adb.cpp:115-118`): `addr:4 | cmd:2 | reg:2`, `(op & $0F) == 0` = ADB
reset. Device register-3 defaults: mouse `$63 01` (addr 3, handler 1, extended
handler 4 supported), keyboard `$62 05` (`adb.cpp:60-65`).

### 5.4 InitADB's VIA transactions NOPed

(`rom_patches.cpp:1577-1613`.) On a real machine this is the
ROM↔transceiver (Egret on the LC II) handshake; the ADB manager data structures
still get built, because only the hardware pokes are removed. So this site
encodes **where** that code is, not its protocol.

### 5.5 Input injection without hardware

`ADBInterrupt()` (`adb.cpp:325-460`) fakes autopoll results by locating
`ADBBase` (`$CF8`), using `ADBBase+4` = keyboard entry (handler ptr, then data
ptr) and `ADBBase+16` = mouse entry, staging Talk-0 data at `ADBBase+$163`, and
`Execute68k()`-calling the registered ADB *handler* directly with `d0` =
`(reg3<<4)|$0C` (Talk 0). **This documents the in-RAM ADB manager table layout
the ROM builds.**

### 5.6 Interrupts

VIA1 IFR classification bypassed (`moveq #2,d0` = "it's the 60 Hz tick",
`rom_patches.cpp:1817-1823`) and all interrupt-source enables NOPed (§3.2):
Basilisk keeps only a synthetic 60 Hz / 1 Hz / ADB stream delivered as level 1,
and lets `_DoVBLTask` (`$A072`) run the rest (`emul_op.cpp:444-505`).
`PowerOff` (`$A05B`) → shutdown stub (`rom_patches.cpp:1790-1792`).

**Net contract:** to an unpatched ROM the LC II's Egret must provide RTC seconds
+ 256-byte XPRAM (with `$8A` and `$AE` meaningful), ADB autopoll/Talk/Listen
with the reset semantics above, and power-off — **plus** the VIA1
shift-register/handshake transport that InitADB exercises.

## § 6. 24/32-bit addressing & MMU handling in the UAE core

- `$067C` config: ROM at **`$40800000`**, `TwentyFourBitAddressing = false`
  (`main.cpp:90-97`, `basilisk_glue.cpp:90-92`). Basilisk never emulates dynamic
  mode switching (`_SwapMMUMode`): it **forces XPRAM `$8A` to `$05`** so the
  system commits to 32-bit mode from the start (`emul_op.cpp:127,149-150`), and
  **guts InitMMU** (three patches, §3.3) so no PMMU setup is attempted; the
  BootGlobs MMU byte is set to "no MMU" (`emul_op.cpp:194-195`). POM68K does the
  opposite on purpose — it runs the real 24-bit startup (§8.8).
- PMMU instructions are decoded but inert: `PFLUSH` clears a fake MMUSR,
  `PTEST` is a no-op (`uae_cpu/newcpu.cpp:1269-1278`); `PTEST`-using ROM/System
  code ('040 BlockMove, SANE) is patched out (`rom_patches.cpp:1660-1686`,
  `rsrc_patches.cpp:219-232`).
- When a 24-bit configuration *is* used (Classic ROMs), the UAE memory map
  implements 24-bit truncation by **mirroring every low bank through all 256
  16 MB windows** of the 4 GB space (`uae_cpu/memory.cpp:654-668`:
  `if (TwentyFourBitAddressing) endhioffs = 0x10000;`) rather than masking
  addresses — `$00xxxxxx` and `$40xxxxxx` accesses land on the same bank. That
  is the whole of Basilisk's "24-bit mode": aliasing, not translation.
- Exceptions: `Exception(nr)` fetches the handler from **`regs.vbr + 4*nr`**
  (`uae_cpu/newcpu.cpp:803`); `m68k_reset()` sets **VBR = 0**
  (`newcpu.cpp:1199`); `movec` to/from VBR is supported (`newcpu.cpp:875,907`).
  No Basilisk patch ever touches VBR — classic Mac OS leaves it at 0, so vector
  10 is always fetched from **address `$28` of the current address map**.

## § 7. CLOSED — the LC II "vector 10 = `$00000000`" hunt (2026-07-15)

> **Resolved. All three hypotheses below were wrong.** Kept because the ROM
> facts they rest on are real and reusable, and because the *shape* of the
> reasoning recurs. Actual root cause (CHANGELOG 2026-07-15, "the LC II ROM
> boots to the blinking-? screen"): **Egret command `$02` = `ReadXPram(count,
> offset)` was unimplemented.** The ROM's `_ReadXPRam` (`$A0CC64`) loads the
> boot-mode flag byte from **XPRAM `$8A`** into low-mem **`$1EFC`**; with no
> reply data, `$1EFC` kept the `$FFFFFFFF` low-memory fill, its **bit 4**
> selected `_InitZone` on a diagnostic zone-at-zero pblock (`$A00518`), and the
> zone-header clear **wiped exception vectors 0-12**. The very next OS trap
> (`$A02D`) dispatched through the zeroed vector 10 into address 0. Found by
> walking the chain backwards with `lcii_trace` (vector watch → InitZone pblock
> → `$1EFC` → `_ReadXPRam` → the unanswered Egret exchange). Basilisk
> independently corroborated it: it stubs the same service (`_ClkNoMem`) and
> forces XPRAM `$8A` itself.

Standing conclusions from the Basilisk sources that survived the hunt:

1. **VBR is not the variable.** The UAE core, like the real ROM, runs the whole
   classic-Mac life with VBR = 0 (`newcpu.cpp:1199`); nothing in the `$067C`
   patch set ever sets VBR. Vector 10 comes from **physical `$28` as decoded at
   that instant**.
2. **Vector installation is ROM code Basilisk executes unpatched.** Basilisk
   enters at +`$2A`, skips only `$8C`→`$BA` plus the §3.2 sites, never installs
   vector 10 itself, yet from the first `Execute68kTrap()` onward it *depends*
   on guest `$28` dispatching A-traps (`basilisk_glue.cpp:194-204` +
   `newcpu.cpp:1250-1252,803`). So in the `$067C` flow the exception vector
   table is fully installed by common ROM code **after the `$BA` join point and
   before InitDevices (`$1142`)**. "Trap table at `$400` built while `$28`
   reads 0" therefore means the vector write happened and was **lost**, not
   that it never ran.
3. *(suspect A — wrong)* **ROM overlay still asserted when vectors are
   written.** Basilisk sidesteps overlay entirely (CPU starts at ROMBase+`$2A`,
   RAM at 0 from instruction one). If a machine's overlay drop fires later than
   on real hardware, stores to `$0-$FF` land in the overlay and are discarded.
   Diagnostic that remains useful: log overlay state at the cycle of every store
   to `$0-$FF`.
4. *(suspect B — wrong)* **A boot-time RAM-clearing loop aliasing page 0.** Real
   ROM fact behind it: the `clr.l (a2)+ / move.w a2,d3 / bne.s` loop at ROM
   ~`$A00-$B00` clears from end of BootGlobs up to end of RAM and stops when the
   *low word* of the address wraps to 0 (`rom_patches.cpp:1275-1283`). Basilisk
   NOPs it only because its RAM top adjoins host mappings; on real hardware it
   runs. If a memory map **mirrors modulo bank size**, such a loop can write
   through aliases of physical page 0. Same class of risk for the `$8C-$BA` RAM
   test Basilisk skips wholesale.
5. *(suspect C — wrong, but the XPRAM lead was right for the wrong reason)*
   **24/32-bit map divergence.** The ROM consults **XPRAM `$8A`** for the boot
   addressing mode; Basilisk hard-wires it to `$05` precisely because the 24-bit
   + `_SwapMMUMode` path is the one it cannot survive. The real failure was one
   step earlier — the `$8A` *read* never completed — which is why seeding a
   known-good XPRAM (`'NuMc'` at `$0C-$0F` and the `main.cpp:106-133` block)
   remains the right baseline. What POM68K actually seeds, and the two
   divergences from Basilisk: **§8.8**.
6. **Interrupts cannot be the trigger before `$226`/`$230`/`$2EE`.** Those
   fixed-offset enables are where the `$067C` boot first unmasks sources;
   before them the ROM runs at IPL 7 from reset (`newcpu.cpp:1198`).
7. **Cheap corroborations, still valid:** the reset vector at ROM+4 (§8.2); the
   decoder→low-mem table at ROM+`$94A` (§2.3) as the list of every hardware base
   the ROM will demand (§8.4); `rominfo --trap` (§4.2/§8.6) to breakpoint any
   A-trap's ROM implementation; the WarmStart `'WLSC'` flag at `$CFC` (§4.3) as
   the ROM's own "low memory is valid" milestone, which brackets any fault as
   before/after low-mem validity.

None of Basilisk II's *patches* should be replicated in POM68K — the point is
that the machine models provide for real what Basilisk stubs out. This document
is the checklist of what "for real" must cover.

## § 9. Slot declaration ROMs (`slot_rom.cpp`) — studied for the Quadra 605

Agent sweep 2026-07-18 for the Q5 Slot-Manager blocker (since closed; the
Quadra 605 boots). Basilisk II builds a synthetic declaration ROM appended to
the top of the Mac ROM; it is our field-by-field reference for every structure
the Slot Manager walks. POM68K's implementation: `src/DeclRom.h/.cpp`. All
cites `BasiliskII/src/slot_rom.cpp`.

### 9.1 Format block (last 20 bytes of the ROM, lines 440-446)

| Offset from end | Size | Field |
|---|---|---|
| −20 | 4 | fhDirOffset — **self-relative** offset to the sResource directory (24-bit signed in practice) |
| −16 | 4 | fhLength — length of the declaration ROM (**0 in the real `FF7439EE` ROM**, §8.7) |
| −12 | 4 | fhCRC (**0 in the real `FF7439EE` ROM**) |
| −8 | 2 | fhROMRev / fhFormat = `$0101` |
| −6 | 4 | fhTstPat = `$5A932BC7` |
| −2 | 2 | reserved `$00` + fhByteLanes (`$0F` = all four lanes) |

CRC (lines 461-478): zero the CRC field, then over every byte
`crc = rotl32(crc,1) + byte`, store big-endian. Same algorithm in
`src/DeclRom.cpp:85-101`.

Real-ROM instance and its directory offset: §8.7.

### 9.2 Record grammar (lines 53-106)

Directory and sResource lists are arrays of 4-byte entries: `Offs(type,
target)` = type byte + **24-bit self-relative offset**; `Rsrc(type, data)` =
type byte + 24-bit immediate; terminator `EndOfList()` = `$FF000000`. Entries
must be ascending by type.

### 9.3 Video sResource fields (VMonitor, lines 178-227)

`$01` sRsrcType → Offs to `Word(category=3 Display) Word(cType=1) Word(DrSW=1)
Word(DrHW)`; `$02` sRsrcName → C-string; `$04` sRsrcDrvrDir → Offs to a driver
directory (`$02` → driver code block, block = `Long(size) code…`); `$08` = Rsrc
hardware id; `$0A`/`$0B` minorBase/minorLength → Offs to Long; `$40` gamma
directory (table layout lines 324-349: header 38 B + 256 byte entries, id
`$2000`, "Mac HiRes Std Gamma"); `$7D` video attributes (Rsrc, bit1 built-in |
bit2 color); `$80+i` one Offs per depth mode → 50-byte VModeParms block (lines
108-166: `Long(50) Long(0) Word(rowBytes) Rect(bounds) Word(version) …
HRes/VRes $00480000 … pixelType/pixelSize/cmpCount/cmpSize planeBytes`).

### 9.4 Findings that killed wrong hypotheses

- `grep 0x40900000` over Basilisk II **and** MAME: zero hits — the faulting
  address in the old Q5.1 blocker is emergent (end-of-ROM overrun), not a
  hardware window anybody maps.
- Built-in video on Quadra-class machines is **not** a slot-9 declaration ROM:
  MAME `djmemc.cpp:30-32` integrates DAFB II inside MEMCjr (`$F9800000` regs,
  `$F9000000` VRAM), and `djmemc.cpp:29,142` mirrors the 1 MB ROM over
  `$40000000-$4FFFFFFF` (`.mirror(0x0ff00000).nopw()`).
- Basilisk assigns built-in video slot ids from `$80` upward (`video.cpp:45`),
  matching the runtime-inserted sResources seen in the `FF7439EE` ROM (§8.7).
