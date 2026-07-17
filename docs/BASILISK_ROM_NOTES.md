# Basilisk II — Mac ROM knowledge, studied for POM68K O6 (LC II)

Source studied: https://github.com/cebix/macemu, cloned at
`/home/gistarcade/src/macemu` (commit `96e512bd`, 2025-01-06).
All paths below are relative to `BasiliskII/src/` unless noted; line numbers
refer to that checkout.

Basilisk II runs **real, patched** ROMs. Every patch site is therefore a map
of something a *real* machine (or POM68K's V8 model) must provide, and every
structure it parses is documentation of the ROM's internal layout. POM68K
runs the **unpatched** LC II ROM, so the value here is the inventory, not
the patches themselves.

---

## 1. How Basilisk II identifies ROM families

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
(`PatchROM()`, `rom_patches.cpp:1834-1852`). The **LC II ROM (checksum
$35C28F5F, 512 KB) is version $067C** — it is in the `ROM_VERSION_32`
family, and `patch_rom_32()` explicitly carries `ROMSize <= 0x80000`
branches for the 512 KB members of that family (see §3.3). Accepted ROM
sizes are 64K/128K/256K/512K/1MB (`Unix/main_unix.cpp:691-697`). The ROM
checksum at offset 0 is only *printed* (`rom_patches.cpp:301`), never
verified (for Classic ROMs the ROM's own checksum complaint is even patched
out, `rom_patches.cpp:859-861`).

Sub-version is the word at offset **18** (`rom_patches.cpp:303`).

CPU configuration derived from the family (`main.cpp:72-100`): `$067C` →
CPUType 2-4 (68020/030/040), `TwentyFourBitAddressing = false`; UAE memory
map puts the ROM at Mac address **$40800000** for `$067C` ROMs
(`uae_cpu/basilisk_glue.cpp:81-95`) — i.e. the canonical 32-bit-clean
"$40-prefixed" ROM address. For 24-bit families the ROM sits at $400000
(Plus/Classic) or $A00000 (Mac II).

### 1.2 ROM header fields Basilisk II uses

| ROM offset | Size | Meaning | Cited at |
|---|---|---|---|
| +$00 | long | checksum (printed only) | `rom_patches.cpp:301` |
| +$04 | long | reset PC — for $067C ROMs the entry point is **ROMBase+$2A** (Basilisk hardcodes it, see §3.1) | `uae_cpu/newcpu.cpp:1185` |
| +$08 | word | ROM version (family ID) | `rom_patches.cpp:833` |
| +$12 (18) | word | sub-version | `rom_patches.cpp:303` |
| +$1A (26) | long | offset of **ROM resource map** | `rom_patches.cpp:94, 168, 304` |
| +$22 (34) | long | offset of **compressed ROM trap address table** | `rom_patches.cpp:125, 305` |

### 1.3 ROM resource map format (as parsed)

`find_rom_resource()` (`rom_patches.cpp:90-116`): the long at ROM+$1A points
to a header whose first long is the offset of the first resource entry;
each entry (all offsets ROM-relative):

| entry offset | meaning |
|---|---|
| +8 | long: offset of next entry (0 = end) |
| +12 | long: offset of resource **data** |
| +16 | long: type (FOURCC) |
| +20 | word: ID |
| +23 | byte: name length, name follows at +24 | (`rom_patches.cpp:176-178`) |

Resource data is preceded by a length long at data-8
(`rom_patches.cpp:181`, `emul_op.cpp:532`: size = `ReadMacInt32(adr-8) &
0xffffff`). Resources Basilisk locates this way: `DRVR 4` (.Sony),
`DRVR 51` (.EDisk), `SERD 0`, `PACK 4` (SANE — **two** PACK 4 resources
present ⇒ ROM has a no-FPU SANE; only one ⇒ ROM requires an FPU,
`rom_patches.cpp:1812-1815`).

---

## 2. Universal ROM structures (UniversalInfo / DecoderInfo)

### 2.1 Locating UniversalInfo

`patch_rom_32()` finds the boot UniversalInfo record by scanning ROM
offsets **$3400-$3C00** for the 8-byte signature
`DC 00 05 05 3F FF 01 00` and subtracting **$10**
(`rom_patches.cpp:1193-1197`):

```cpp
static const uint8 universal_dat[] = {0xdc,0x00,0x05,0x05,0x3f,0xff,0x01,0x00};
base = find_rom_data(0x3400, 0x3c00, universal_dat, sizeof(universal_dat));
UniversalInfo = base - 0x10;
```

That signature is the hwCfgWord/productKind/rom85/defaultRSRCs block of the
*IIci* record (`DC00` = hwCfgWord, `05` = productKind, `3FFF` = rom85).
The diagnostic scanner (`list_universal_infos()`,
`rom_patches.cpp:276-296`) is more general: it scans from $3000 for the
long `DC000505`, backs up 16 bytes to the info record, then walks
*backwards* looking for a long whose value equals the distance to that
record — that long is an entry of the **Universal table**: a
null-terminated array of **self-relative 32-bit offsets**, one per
UniversalInfo record (one per supported machine in the ROM).

### 2.2 UniversalInfo record layout (offsets Basilisk reads/writes)

| offset | size | field | used at |
|---|---|---|---|
| +$00 | long | self-relative offset to **DecoderInfo / AddrMap** | `rom_patches.cpp:1217`, `emul_op.cpp:106` |
| +$0C | long | self-relative offset to **nuBusInfo** (16 bytes, one per slot $9-$E area; Basilisk writes `03 08 08 ...` to disable slots) | `rom_patches.cpp:1199-1203` |
| +$10 | long | **HWCfgFlags/IDs** (hi word = hwCfgWord, then productKind byte) — bit 28 = FPU present | `emul_op.cpp:101-105`, `rom_patches.cpp:262` |
| +$12 | byte | **productKind** = Gestalt machine ID − 6 (LC II Gestalt 37 ⇒ productKind **31**); Basilisk overwrites it from the `modelid` pref (default 5 = IIci) | `rom_patches.cpp:261, 1206-1207`, `prefs_items.cpp:56,88` |
| +$14 | word | **rom85** word | `rom_patches.cpp:263` |
| +$16 | byte | **defaultRSRCs**; value 4 = "FPU optional" (patched when no FPU) | `rom_patches.cpp:1234-1238` |
| +$18 | long | **AddrMapFlags** | `emul_op.cpp:99` |
| +$1C | long | **UnivROMFlags** | `emul_op.cpp:100` |

The Gestalt-ID↔name table (`MacDesc[]`, `rom_patches.cpp:196-257`) contains
`{"Mac LCII", 37}` at line 227.

### 2.3 DecoderInfo and the hardware-base → low-mem-global copy table

The DecoderInfo (a.k.a. AddrMap) is an array of **32-bit hardware base
addresses**. Crucially, `patch_rom_32()` documents that the ROM itself
contains, at fixed ROM offset **$94A**, a word-pair table that the ROM's
StartInit uses to copy decoder entries into low-memory globals
(`rom_patches.cpp:1216-1227`):

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

I.e. **(decoderInfo index, low-mem global address) pairs, terminated by
$FFFF, at ROM+$94A** in every $067C ROM. Basilisk redirects all of them
(except ASCBase $CC0) to a scratch RAM page so that the ROM's later
hardware pokes through those globals land harmlessly. For POM68K this table
in the real LC II ROM is a self-describing list of every hardware base the
ROM will install into low memory — worth dumping from the real ROM.

### 2.4 Boot handoff register contract (EMUL_OP_RESET)

`M68K_EMUL_OP_RESET` (`emul_op.cpp:84-111`) reproduces what the skipped
hardware-detection code must leave behind before the common boot path at
ROM+$BA:

- **BootGlobs** built at top of RAM: at `RAMBase+RAMSize-0x1C`:
  +0 first-bank base, +4 bank size, +8 `$FFFFFFFF` end-of-bank-table
  marker (memory-bank table grows downward from top of RAM).
- `d0` = AddrMapFlags (UniversalInfo+$18), `d1` = UnivROMFlags (+$1C),
  `d2` = HWCfgFlags/IDs (+$10, bit 28 = FPU),
  `a0` = DecoderInfo ptr, `a1` = UniversalInfo ptr,
  `a6` = BootGlobs, `a7` = RAMBase+$10000.

`M68K_EMUL_OP_PATCH_BOOT_GLOBS` (`emul_op.cpp:191-197`) further documents
BootGlobs-relative fields written slightly later (a4 = just past
BootGlobs): `a4-20` = MemTop, `a4-26`/`a4-25` = MMU-type/flags bytes
("No MMU": byte at -26 = 0, bit 0 of byte at -25 set).

---

## 3. Boot sequence: what Basilisk II patches in $067C ROMs, and why

Everything in `patch_rom_32()` (`rom_patches.cpp:1187-1832`). Two kinds of
sites: **fixed offsets** (identical layout assumed across *all* $067C
ROMs — the early boot code is common) and **byte-pattern searches**
(with explicit `ROMSize <= 0x80000` ranges = the 512 KB V8/LC-class
members, directly relevant to the LC II).

### 3.1 CPU entry

The UAE core does not fetch the reset vector: it jumps straight to
**ROMBase+$2A with A7=$2000, SR=$2700, VBR=0**
(`uae_cpu/newcpu.cpp:1182-1199`). So for $067C ROMs the reset vector at
ROM+4 points to base+$2A. POM68K sanity check: the LC II ROM long at +4
should read `$4080002A` (and initial SSP at +0 = `$00002000`-class value).

### 3.2 Fixed-offset patch map (early boot, common to all $067C ROMs)

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
| $1B8F4 | **vCheckLoad** resource-load hook | `jmp` to glue that chains via LM vector $7F0 then `EMUL_OP_CHECKLOAD` | 1776-1788 |
| $CCAA | InitMemMgr's "handle at 0" setup | fake handle → scratch mem | 1441-1453 |
| $B0E2 | InitTimeMgr VIA timer writes | early return | 1469-1473 |

Boot beep: for $067C ROMs the chime is silenced indirectly by killing
**InitASC** (search $4000-$5000 for `26 68 00 30 12 00 EB 01`,
`rom_patches.cpp:1397-1404`); for Classic ROMs the startup sound call at
$6A is NOPed (`rom_patches.cpp:868-871`).

### 3.3 Pattern searches with explicit 512 KB (V8/LC-class) ranges

These branches are the closest thing Basilisk has to LC II-specific
knowledge — the same routine lives in a different region in 512 KB vs 1 MB
$067C ROMs:

| Routine | 512 KB ROM search range | 1 MB range | Pattern (bytes) | Lines |
|---|---|---|---|---|
| **InitMMU** CPU-type dispatch (`cmp.w #N,d7; bhi`) | $4000-$50000, `0C 47 00 03 62 00 FE` (68030 max) | $80000-$90000, `0C 47 00 04 62 00 FD` (68040 max) | NOPed, `moveq #0,d0` | 1285-1301 |
| **InitMMU** RBV presence test (`btst #13,d6; beq`) | $4000-$50000 | $80000-$90000 | `08 06 00 0D 67` → `bra` | 1303-1314 |
| **InitMMU** actual MMU setup (`cmp.b #1,-26(a6); bne` — tests the BootGlobs MMU byte!) | $4000-$50000 | $80000-$90000 | `0C 2E 00 01 FF E6 66 0C 4C ED 03 87 FF E8` | 1316-1325 |
| Read **XPRAM** (ROM10/11 style, VIA at $50F00000) | $40000-$50000 | — | `26 4E 41 F9 50 F0 00 00 ...` | 1327-1343 |
| Read **XPRAM** (ROM15 style) | — | $80000-$90000 | `48 E7 E0 60 02 01 00 70 0C 01 00 20` | 1344-1353 |
| **ModelID read from $5FFFFFFC** (VIA-decoded machine-ID register) | $4000-$5000 (`45 F9 5F FF FF FC 20 12`, ROM27/32) and $40000-$50000 (`20 7C 5F FF FF FC 72 07 C2 90`, ROM20) | | forced `d0=0` | 1475-1496 |
| **VIA2 write through LM $CEC** (`movea.l $CEC,a0; move.w #$90xx,...`) | $A000-$A400 (all) + $40000-$44000 (ROM19/20) | | `rts` | 1644-1658 |
| NuBus slot probe | $5000-$6000 | | `45 FA 00 0A 42 A7 10 11` | 1502-1511 |
| ClkNoMem fallback (when trap entry is a `jmp (a5)` thunk) | — | $B0000-$B8000 | `40 C2 00 7C 07 00 48 42` | 1355-1365 |
| BlockMove PTEST / SANE PTEST | skipped for ≤512 KB ROMs (`ROMSize > 0x80000` guard — V8-class ROMs never drive an '040) | $87000-$87800 / whole ROM | | 1660-1686 |
| MemoryDispatch un-implementation | $4F100-$4F180 | | `30 3C A8 9F A7 46 30 3C A0 5C A2 47` | 1688-1695 |
| physical/logical RAM size fixup (writes $1EF4/$1EF8) | $4C000-$4C080 | | | 1555-1562, `emul_op.cpp:204-210` |
| .EDisk ROM-area scan ($ROMBase..$E00000) | inside `DRVR 51` | | `D5 FC 00 01 00 00 B5 FC 00 E0 00 00` | 1697-1708 |

Driver replacement: `.Sony` (`DRVR 4`) is overwritten in place with an
EMUL_OP stub driver; `.Disk`/`.AppleCD` are appended at +$100/+$200 inside
the same resource, icons at +$400.., scrap patches at +$C00/+$D00
(`rom_patches.cpp:1710-1727, 1794-1810`). Serial drivers overwrite
`SERD 0` (+$100..+$400, `rom_patches.cpp:1729-1738`). A **slot
declaration ROM** (board + video + CPU + Ethernet sResources, format per
*Designing Cards and Drivers*) is synthesized and dropped into the **last
bytes of the ROM image** with the standard format block (test pattern
`$5A932BC7`, byte lanes $0F) and Apple CRC (`slot_rom.cpp:229-478`).

### 3.4 What is *not* patched — and therefore must work

Everything else in the $067C boot path executes natively, notably:
exception-vector-table installation, trap-dispatch-table construction,
memory manager init, resource manager init, the entire Slot Manager (fed
by the fake declaration ROM), Gestalt, and the boot-volume search. That
untouched set is the "must be correct" contract for POM68K's CPU core.

---

## 4. Trap dispatcher details

### 4.1 The RAM dispatch tables at $400 / $1400 / $E00: not referenced

**Basilisk II never reads, writes, or searches for the RAM trap dispatch
tables.** There is no occurrence of the OS table base $400 or a Toolbox
table base ($1400/$E00/$1E00) anywhere in `BasiliskII/src` (verified by
grep). It cannot *confirm* those bases; it simply relies on the ROM's own
dispatcher. All trap manipulation goes through the official traps, executed
*as* traps:

- `SetOSTrapAddress` = `$A247` (`rom_patches.cpp:717,722`)
- `SetToolBoxTrapAddress` = `$A647` (`emul_op.cpp:242,249`)
- `DrvrInstallRsrvMem` = `$A43D`/`$A53D`, `HLock` `$A029`, `Open` `$A000`,
  `NewPtrSysClear` `$A71E` (`rom_patches.cpp:709-810`)

`Execute68kTrap()` (`uae_cpu/basilisk_glue.cpp:181-219`) pushes the raw
A-trap word + a magic `M68K_EXEC_RETURN` ($7100) word on the guest stack,
points PC at it, and runs the CPU. The A-trap word executes as an
instruction → `op_illg()` raises `Exception(0xA)`
(`uae_cpu/newcpu.cpp:1250-1252`) → handler fetched from `VBR + $28`
(`newcpu.cpp:803`) → **the guest ROM's Line-A dispatcher does the whole
job**, including reading the RAM dispatch tables. So every host-initiated
Mac call in Basilisk II is living proof that guest vector 10 + both
dispatch tables are functional from InitDevices time onward.

### 4.2 The ROM-side compressed trap address table (header +$22)

`find_rom_trap()` (`rom_patches.cpp:123-155`) documents the format of the
*ROM's* master trap table (the data the ROM expands into the RAM tables at
boot). Pointer: long at ROM+$22. Encoding — a cumulative-offset byte
stream, **Toolbox traps first ($A800-$ABFF, 1024 entries), then OS traps
($A000-$A3FF, 1024 entries)**:

| byte | meaning |
|---|---|
| `$80` | trap unimplemented |
| `$FF` | next 4 bytes = absolute routine offset (replaces accumulator) |
| high bit set (≠$80,$FF) | add `(b & $7F) << 1` to offset accumulator |
| high bit clear | 2-byte big-endian value, add `value << 1` (0 ⇒ end) |

Basilisk resolves through it: `_ClkNoMem` $A053, `_InsTime` $A058,
`_RmvTime` $A059, `_PrimeTime` $A05A, `_PowerOff` $A05B, `_ADBOp` $A07C,
`_SCSIDispatch` $A815, `_PutScrap` $A9FE, `_GetScrap` $A9FD
(`rom_patches.cpp:1355, 1741-1810`).

(For contrast: SheepShaver's NewWorld ROMs use a *flat* table —
`SheepShaver/src/rom_patches.cpp:260-268`: Toolbox = `lp + 4*(trap & $3FF)`,
OS = `lp + 4*((trap & $FF) + $400)` — note the OS traps sitting at index
+$400, matching the classic "OS table is 256→ mirrored, Toolbox 1024"
arrangement. Not applicable to the LC II ROM but a useful cross-check of
the OS/Toolbox split.)

### 4.3 Low-memory globals Basilisk II relies on

| Addr | Global | Use | Cited at |
|---|---|---|---|
| $10C | BufPtr | boot stack computation | `rom_patches.cpp:1542` |
| $11C | UTableBase | driver install (unit table) | `rom_patches.cpp:728,746` |
| $14C | EventQueue head (QHdr $14A+2) | idle-sleep test | `emul_op.cpp:562` |
| $16A | Ticks | incremented per 60 Hz | `emul_op.cpp:451` |
| $1D4 | VIA (VIA1 base) | VIA-poking code patched in resources | `rsrc_patches.cpp:199,258` |
| $1D8 | SCCRd | .Infra driver patch | `rsrc_patches.cpp:323` |
| $28E | ROM85 | DRVR 41 patch | `rsrc_patches.cpp:347-352` |
| $2A6 | SysZone | boot stack computation | `rom_patches.cpp:1544` |
| $2AE | ROMBase | boot-resource patches | `rsrc_patches.cpp:107,132` |
| $2B6 | ExpandMem | SynchIdleTime patch | `rsrc_patches.cpp:69`, `emul_op.cpp:564` |
| $308 | DrvQHdr | free-drive-number scan | `macos_util.cpp:57` |
| $7F0 | vCheckLoad chain vector (original routine ptr) | resource-patch glue | `rom_patches.cpp:1783` |
| $824 | ScrnBase | Classic-ROM video patch writes fb base here | `rom_patches.cpp:1043-1051` |
| $828/$82A | MTemp (y/x), $82C/$82E RawMouse | absolute mouse injection | `adb.cpp:399-402` |
| $8CE/$8CF | CrsrNew/CrsrCouple | cursor changed flag | `adb.cpp:403` |
| $8FC | JIODone | driver IOReturn path | `rom_patches.cpp:357,405,...` |
| $B24 | TimeSCSIDB | SetupTimeK | `rom_patches.cpp:1424` |
| $CC0 | ASCBase | faked ASC register block (version byte at +$800 must read $0F) | `rom_patches.cpp:1226`, `emul_op.cpp:252-260` |
| $CEA | TimeRAMDBRA | SetupTimeK | `rom_patches.cpp:1427` |
| $CEC | VIA2/RBV base | VIA2 write suppression | `rom_patches.cpp:1645` |
| $CF8 | ADBBase | ADB injection (see §5) | `adb.cpp:331`, `rom_patches.cpp:694` |
| $CFC | WarmStart flag = `'WLSC'` when low memory is valid | gates all host→Mac callbacks (`HasMacStarted()`) | `include/macos_util.h:278-281` |
| $D00/$D02 | TimeDBRA / TimeSCCDB | SetupTimeK | `rom_patches.cpp:1418-1421` |
| $DD8 | pointer consulted by GetDevBase frame-base mangling | | `rom_patches.cpp:1634` |
| $1EF4/$1EF8 | logical / physical RAM size (boot globals) | FIX_MEMSIZE | `emul_op.cpp:204-210` |

---

## 5. Egret / Cuda / ADB / PRAM: what Basilisk II stubs at ROM level

Basilisk II contains **zero** Egret/Cuda/PMU code (grep confirms). It
bypasses that entire layer by replacing the *services* the ROM implements
on top of it. The replacement list = the service contract POM68K's Egret
must honor:

1. **`_ClkNoMem` ($A053) — RTC + PRAM/XPRAM access** (`emul_op.cpp:113-179`).
   Protocol as the ROM presents it: command in `d1`, data in `d2`,
   bit 7 of d1 = read. `(d1 & $78) == $38` selects **extended XPRAM**
   addressing, register = `((d1<<5)&$E0)|((d1>>10)&$1F)` (256 bytes);
   otherwise classic clock chip registers `(d1>>2)&$1F`: regs 0-3 =
   seconds counter bytes, regs 8-11 and 16-31 = classic PRAM. Returns
   status 0 in d0, data in d1/d2. Notable byte semantics Basilisk forces:
   - **XPRAM $8A |= $05: "32bit mode is always enabled"**
     (`emul_op.cpp:127, 149-150`) — this is the startup addressing-mode
     byte the $067C ROM consults to decide 24 vs 32-bit boot.
   - $E0-$E3 rewritten to disable LocalTalk (`emul_op.cpp:129-144`).
   - XPRAM signature `'NuMc'` at $0C-$0F, default PRAM values
     (`main.cpp:106-133`); boot volume/driver at $78-$7B.
2. **Raw XPRAM readers** in the ROM (which talk to the VIA at $50F00000
   directly, bypassing ClkNoMem) are separately stubbed
   (`rom_patches.cpp:1327-1353`) — evidence the ROM has *two* PRAM paths.
3. **`_ADBOp` ($A07C)** — replaced wholesale by `adbop_patch`
   (`rom_patches.cpp:682-702`, handler `emul_op.cpp:212-214`,
   `adb.cpp:100-…`): masks interrupts, runs the host ADB engine, then
   calls the caller's completion routine with `a0`=data, `a1`=completion,
   `a2`=buffer, `a3`=`ADBBase` (from $CF8). Command byte format
   (`adb.cpp:115-118`): `addr:4 | cmd:2 | reg:2`, `(op & $0F)==0` = ADB
   reset. Device register-3 defaults: mouse `$63 01` (addr 3, handler 1,
   extended handler 4 supported), keyboard `$62 05` (`adb.cpp:60-65`).
4. **InitADB's VIA transactions NOPed** (`rom_patches.cpp:1577-1613`) —
   on a real machine this is the ROM↔transceiver (Egret on LC II)
   handshake; the ADB manager data structures still get built because only
   the hardware pokes are removed.
5. **Input injection without hardware**: `ADBInterrupt()`
   (`adb.cpp:325-460`) fakes autopoll results by locating `ADBBase`
   ($CF8), using `ADBBase+4` = keyboard entry (handler ptr, then data ptr)
   and `ADBBase+16` = mouse entry, staging Talk-0 data at `ADBBase+$163`,
   and `Execute68k()`-calling the registered ADB *handler* directly with
   `d0` = `(reg3<<4)|$0C` (Talk 0). This documents the in-RAM ADB manager
   table layout the ROM builds.
6. **VIA1 IFR classification bypassed** (`moveq #2,d0` = "it's the 60 Hz
   tick", `rom_patches.cpp:1817-1823`) and all interrupt-source enables
   NOPed (§3.2) — i.e. Basilisk keeps only a synthetic 60 Hz/1 Hz/ADB
   interrupt stream, delivered as level-1, and lets `_DoVBLTask` ($A072)
   run the rest (`emul_op.cpp:444-505`).
7. **PowerOff ($A05B)** → shutdown stub (`rom_patches.cpp:1790-1792`).

So: what the LC II's Egret must actually provide to an unpatched ROM =
RTC seconds + 256-byte XPRAM (with $8A meaningful), ADB autopoll/Talk/
Listen with reset semantics above, and power-off — plus the VIA1
shift-register/handshake transport that InitADB exercises (the exact part
Basilisk NOPs, so it encodes *where* that code is, not its protocol).

---

## 6. 24/32-bit addressing & MMU handling in the UAE core

- `$067C` config: ROM at **$40800000**, `TwentyFourBitAddressing=false`
  (`main.cpp:90-97`, `basilisk_glue.cpp:90-92`). Basilisk never emulates
  dynamic mode switching (`_SwapMMUMode`): instead it **forces XPRAM $8A
  to $05** so the ROM/system commits to 32-bit mode from the start
  (`emul_op.cpp:127,149-150`), and it **guts InitMMU** (three patches,
  §3.3) so no PMMU setup is attempted; the BootGlobs MMU byte is set to
  "no MMU" (`emul_op.cpp:194-195`).
- PMMU instructions are decoded but inert: `PFLUSH` clears a fake MMUSR,
  `PTEST` is a no-op (`uae_cpu/newcpu.cpp:1269-1278`); `PTEST`-using
  ROM/System code ('040 BlockMove, SANE) is patched out
  (`rom_patches.cpp:1660-1686`, `rsrc_patches.cpp:219-232`).
- When a 24-bit configuration *is* used (Classic ROMs), the UAE memory
  map implements 24-bit truncation by **mirroring every low bank through
  all 256 16 MB windows** of the 4 GB space
  (`uae_cpu/memory.cpp:654-668`: `if (TwentyFourBitAddressing) endhioffs =
  0x10000;`) rather than masking addresses — both `$00xxxxxx` and
  `$40xxxxxx` style accesses land on the same bank. That is the whole of
  Basilisk's "24-bit mode": aliasing, not translation.
- Exceptions: `Exception(nr)` fetches the handler from **`regs.vbr +
  4*nr`** (`uae_cpu/newcpu.cpp:803`); `m68k_reset()` sets **VBR=0**
  (`newcpu.cpp:1199`); `movec` to/from VBR is supported
  (`newcpu.cpp:875,907`). No Basilisk patch ever touches VBR — classic
  MacOS leaves it at 0, so vector 10 is always fetched from **address
  $28 of the current address map**.

---

## 7. Relevance to POM68K LC II (O6) — and the vector-10 = $00000000 window

Context: POM68K runs the *real, unpatched* LC II ROM ($067C, 512 KB) on an
emulated V8 machine; the ROM populates the OS trap table at $400, but a
Line-A dispatch through exception vector 10 fetches $00000000 around that
phase.

What the Basilisk II sources contribute to that investigation:

1. **VBR is not the variable.** The UAE core, like the real ROM, runs the
   whole classic-Mac life with VBR=0 (`newcpu.cpp:1199`); nothing in the
   $067C patch set ever sets VBR. Unless Moira's 68030 reset left VBR
   non-zero or the LC II ROM did a `movec` you can trace, vector 10 comes
   from **physical $28 as decoded by V8 at that instant**.

2. **Vector installation is ROM code Basilisk executes unpatched.**
   Basilisk enters the ROM at +$2A and only skips $8C→$BA (hardware
   detect/RAM test) plus the specific I/O sites of §3.2. It never installs
   vector 10 itself, yet from the first `Execute68kTrap()` call onward it
   *depends* on guest vector $28 dispatching A-traps
   (`basilisk_glue.cpp:194-204` + `newcpu.cpp:1250-1252,803`). Conclusion:
   in the $067C flow, the exception vector table (including 10) is fully
   installed by common ROM code that lies **after the $BA hardware-detect
   join point and before InitDevices ($1142)** — and empirically before
   the first internal A-trap the ROM issues. If POM68K sees the OS trap
   table at $400 built while $28 still reads 0, the vector *write* very
   likely happened but was **lost or overwritten**, since the ROM's own
   ordering (proved by Basilisk running it) installs vectors early enough.

3. **Prime suspect A — ROM overlay still asserted when vectors are
   written.** Basilisk sidesteps overlay entirely (CPU starts at
   ROMBase+$2A; RAM bank at 0 from the first instruction). On V8, ROM is
   overlaid at $00000000 out of reset; if POM68K's overlay switch-off
   condition (first access to the ROM's native address range, or a V8
   register write) fires *later* than on real hardware, the ROM's vector-
   table stores at $0-$FF go to the overlay (discarded), and once the
   overlay drops, reads of $28 return never-initialized RAM = 0. The trap
   tables at $400 could still be intact if they were written *after* the
   overlay dropped — which would exactly reproduce "table at $400
   populated, vector 10 zero". Log the overlay state at the cycle of each
   store to $0-$FF.

4. **Prime suspect B — a boot-time RAM-clearing loop aliasing page 0.**
   Basilisk explicitly disarms one: the `clr.l (a2)+ / move.w a2,d3 /
   bne.s` loop found at ROM ~$A00-$B00 that "clears end of BootGlobs up to
   end of RAM (address xxxx0000)" (`rom_patches.cpp:1275-1283`) — it runs
   until the *low word* of the address wraps to 0. Basilisk NOPs it only
   because its RAM top adjoins host mappings; on a real machine it runs.
   On POM68K, if V8 RAM decoding **mirrors modulo bank size** (as the
   Plus map does), a clearing loop running above BootGlobs can write
   through mirror aliases of physical page 0 and wipe an
   already-installed vector table while leaving later-written structures
   (the $400 table) intact. Same class of risk for the RAM-test code in
   $8C-$BA that Basilisk skips wholesale. Watch for `clr.l` bursts whose
   physical target aliases $0000-$00FF.

5. **Prime suspect C — 24/32-bit map divergence.** The ROM consults
   **XPRAM $8A** for the boot addressing mode; Basilisk hard-wires it to
   $05 ("32-bit always", `emul_op.cpp:127,149-150`) precisely because the
   alternative path (24-bit boot + `_SwapMMUMode`) is the one it cannot
   survive. On POM68K, an uninitialized/garbage Egret PRAM $8A can send
   the LC II ROM down a mode path where the vector table is written under
   one mapping and vector 10 fetched under another (e.g. writes at
   $28 in 32-bit context vs fetch through a 24-bit-truncated V8 decode).
   Initialize XPRAM with Basilisk's defaults (`main.cpp:106-133`) as a
   known-good baseline, including the `'NuMc'` signature at $0C-$0F —
   without it the ROM may take a PRAM-init detour that reorders early
   boot.

6. **Interrupts cannot be the trigger before $226/$230/$2EE.** The
   fixed-offset enables (one-sec, 60 Hz/parity, slot) show where the $067C
   boot first unmasks sources; before that the ROM runs at IPL 7 from
   reset (`newcpu.cpp:1198`). If POM68K's Line-A hits *before* those
   offsets have executed, it is the ROM's own first trap call, and a zero
   vector 10 means (3)/(4)/(5) above — not an early spurious interrupt.

7. **Cheap corroborations available from the Basilisk knowledge base**:
   reset vector at ROM+4 must be `$xxxx002A`-style (§3.1); the
   decoder→low-mem table at ROM+$94A (§2.3) tells you every hardware base
   the ROM will demand from the V8 map; `find_rom_trap` (§4.2) lets a
   POM68K debug tool resolve any A-trap to a ROM offset for breakpointing
   the dispatcher install; and the warm-start flag `'WLSC'` at $CFC
   (§4.3) is the ROM's own "low memory is now valid" milestone —
   whether $CFC is set when the fault occurs brackets the failing phase.

None of Basilisk II's *patches* should be replicated in POM68K — the point
of O6 is that the V8 model provides for real what Basilisk stubs out. This
document is the checklist of what that "for real" must cover.

---

## 8. Verified on the real LC II ROM (`tools/rominfo`, 2026-07-15)

The parsers above are implemented in **`tools/rominfo.cpp`** (build target
`rominfo`; standalone, no emulator core). Run against the real ROM
(`docs/512KB ROMs/1992-03 - 35C28F5F - Mac LC II.ROM`) they confirm:

- **Header**: checksum $35C28F5F verifies (32-bit sum of words from
  offset 4); reset PC = base+$2A as predicted (§3.1); version $067C,
  sub-version $19F2; resource map at $7EC10, trap table at $4C160.
- **Two `PACK 4` resources** ⇒ the LC II ROM **does carry a no-FPU
  SANE** (§1.3). The TODO § O6 "bare-LC II no-FPU path" question is
  therefore not about a missing ROM SANE — the FPU-less path exists in
  ROM; investigate why error 10 escapes it (selection happens via the
  UniversalInfo FPU bit / defaultRSRCs, §2.2).
- **Universal table at $32D8, 13 records.** Named records: IIci (×2),
  IIfx, IIsi, LC, plus several with productKind $FD (unset — the V8-class
  ROM resolves the model at runtime, cf. the $5FFFFFFC machine-ID read,
  §3.3). The **LC-class DecoderInfo at $3AE6** is shared by the LC record
  and by a productKind-$FD record with **hwCfgWord $CC00 (no FPU) and
  rom85 $7FFF** — the LC II-shaped entry. All named records carry
  AddrMapFlags $773F, matching the value V8Memory reproduces.
- **DecoderInfo $3AE6 hardware bases** (through the ROM+$94A pair table)
  agree with the V8 map: VIA1 $50F00000→$1D4, SCC $50F04000→$1D8/$1DC,
  SWIM $50F16000→$1E0, SCSI $50F10000/$50F12000/$50F06000→$C00/$C04/$C08,
  ASC $50F14000→$CC0, pseudo-VIA $50F26000→$CEC (decoder[11], the real
  VIA2, is 0 on V8 machines — only decoder[13] RBV/pseudo-VIA is
  populated).
- **Trap resolutions** (breakpoint fodder for lcii_trace): `_ClkNoMem`
  $A053 → ROM $4B1E4, `_ADBOp` $A07C → ROM $3A3DC.

Applied to the code base (2026-07-15): `Egret::factoryDefaults()` seeds
Basilisk's known-good XPRAM block (§5.1/§7.5 — 'NuMc' signature, DynWait,
standard PRAM values, OSDefault=MacOS; $8A deliberately *not* forced) when
no battery file carries the signature — **including the LocalTalk-off
$E0-$E3 = $00/$F1/$00/$0A substitution** (emul_op.cpp:129-144), which
proved load-bearing: GISTPERSO's System otherwise opens .MPP and hangs
its SCC transaction loop on our SCC stub at the « Bienvenue » bar.
`lcii_trace` logs the WarmStart `'WLSC'` milestone at $CFC (§4.3) and
applies the same factory defaults as the GUI.
