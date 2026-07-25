# Macintosh LC 520 / LC 550 / Color Classic II bring-up

Status: **LC 520 BOOTS System 7.5 to the 8-bpp color Finder** (2026-07-24,
gate `lc520_boot_etalon`). The EDE66CBD-ROM all-in-one family (LC 520,
LC 550, Color Classic II, Performa 275/550/560) runs on the Sonora
machine (`SonoraMemory`/`SonoraCpu`/`SonoraVideo`) with **cudaAdb=true**.
MAME's `maclc520`/`maclc550` are non-booting stubs, so the ROM itself was
the oracle; this note records the walls and their resolutions so the
remaining siblings (LC 550, CC II) can follow. **Mac TV is NOT in this
family** — it uses its own 1 MB ROM (`$EAF1678D`) on the Tinker Bell
ASIC (see § Siblings).

ROM: `roms/1MB ROMs/1993-10 - EDE66CBD - ...ROM` (header checksum
`$EDE66CBD`). CPU: LC 520 = 68030 @ 25 MHz, LC 550 / CC II = 33 MHz.

## Harness / gate

`tests/lc520_boot_etalon.cpp` (CTest gate). Debug env knobs:
- `POM68K_BOXID=<hex>` — override the `$5FFFFFFC` model longword.
- `POM68K_SENSE=<n>` — override the monitor sense (`setMonitorSense`).
- `POM68K_DIAG=1` — periodic PC/SCSI/depth/d7 + Cuda-MCU-PC trace.
- `POM68K_PROBE=1` — 200-frame run + final-state dump.
- `POM68K_HALT=<hex pc>` — run until PC hits [pc, pc+8] or d7 bit 17
  (ROM-monitor flag) rises, then dump a 128-deep **branch-target trail**
  (dedup'd control transfers — survives polling loops) + registers.
  `POM68K_HALT=2` = print the first 400 branch targets from reset.
- `POM68K_FRAMES=<n>` — override the 16 000-frame budget.
- `POM68K_DUMP=1` — write `lc520_screen.ppm` at the end.

## Walls found (in boot order) — all resolved

### Wall 1 — ASC boot chime (FIXED in Asc.cpp)
The sound engine (`$408BD3F0`+) spins on ASC `$804` bit 3 waiting for FIFO B
empty. `AscSonora::reset` only latched FIFO A empty (`$02`); MAME sets `$0A`
(asc.cpp:465). Fixed: `fifoStat_ = STAT_EMPTY_OR_FULL_A | STAT_EMPTY_OR_FULL_B`.
NOTE: that first audible chime was later understood to be the **error chime**
— the ROM monitor's bootstrap plays it after startup tests fail (see Wall 4).

### Wall 2/3 — machine-table box search + monitor sense (MISREAD, no bug)
The early probe suggested `$A55A0100` was absent from the `$408D1E6C`
machine table. Wrong: the full table walk (see `scratchpad` dumper) shows
**box `$0100` twice** (video type `[12]` = `$32` and `$4B`) and `$0101`
twice (`$4A`/`$4D`). The Wall-3 sense check at `$40804B4E` is precisely the
selector between them: sense `$60` (res 6, built-in 640×480) wants
`[12]` ∈ {`$32`,`$4A`}, sense `$20` (res 2, 512×384) wants {`$4B`,`$4D`}.
So the MAME model ids are correct and **sense 6 is the right default**; the
`$1003-$1006`/`$2000-$2003`/`$2BAD` entries are other family members
(selected when sense ∉ {2,6}). d0 = entry flags `$18(a1)` (`$773F` for the
AIOs), d2 = entry `$10(a1)` hw code (byte 1 = `$32` routes the PA0/PRAM
config path at `$4084713C`).

### Wall 4 — "sExec resource → monitor" = Cuda missing (FIXED)
The real root cause chain, established by branch-trail tracing:
1. At reset the ROM does an MCU handshake at `$408D1AE6`: raise ORB bits
   4/5, then **ByteAck (PB4) low and wait for /TREQ (PB3) to deassert**
   within `$23CF` polls. The Egret 341S0851 firmware never answers this —
   it is a **Cuda-protocol** handshake. Timeout → `$408D1C68` sets
   `d7=$30`, skips ALL startup tests (bit 26 of d7 = "tests passed", set
   only at `$408471EC`), and funnels to `$4084A6F6` → error chime →
   serial-monitor bootstrap (`$408B9888/$408B98C2`, SCC init `$408B9F54`,
   command loop `$408B9906` polling SCC RR0 — the classic "polls SCC
   forever" Sad Mac signature).
2. MAME confirms: `maclc3.cpp:379` gives the LC 520/550 a **CUDA_V2XX
   (341s0060, Cuda 2.40)**, not the LC III's Egret. → `SonoraMemory` grew a
   `cudaAdb` constructor flag: Egret HLE cuda polarity + `CudaLle`
   `Flavor::Cuda` + firmware order 341s0060 → 341s0788.
3. With Cuda 2.37 (341s0788) the boot **livelocks** at `$408B399C`: the
   ROM's early config path sends pseudo-command `[01 0E]` (the LC 475/Q605
   ROM sends `[01 07]` there) and 2.37 keeps re-asserting TREQ while the
   host drains 2 bytes and retries forever. **Cuda 2.40 (341s0060) answers
   `$0E`** and the boot sails through — RAM test, video init, SCSI, Finder.
4. `SonoraMemory` VIA1 port B undriven input bits now read pulled-up
   (`0xC7 | session<<3`), matching `V8Memory`/`Q605Memory`.

## Result

`lc520_boot_etalon`: model `$A55A0100`, sense 6, Cuda 341S0060 LLE →
System 7.5 Finder at **640×480×8 bpp color** (menu bar + color desktop
weave + drive windows), ~1750 SCSI commands at 16 000 frames. The gate
signature is luminance-based (the sibling gates' blue-channel ratio reads
the orange/green desktop weave as black).

## Siblings

- **LC 550 / Performa 550 — DONE**: box `$0101`, 33.33 MHz (`kCpuHzPlus`),
  sense 6 → vid `$4A`, 640×480×8 Finder. Gate `lc550_boot_etalon`.
- **Color Classic II / Performa 275 — DONE**: the same `$0101` board with
  **sense 2** → vid `$4D`, **512×384×8** Finder (the sense line is the
  whole machine difference). Gate `cclassic2_boot_etalon`.
- **Mac TV — DONE 2026-07-25, NOT an EDE66CBD sibling.** The shared Sonora
  AIO ROM's `$2000-$2003` entries (MCU-type 0 = Egret) look Mac-TV-ish but
  stall with **both** MCUs — wrong machine. Real Mac TV (MAME `mactv`)
  boots its **own** `eaf1678d.bin` (`$EAF1678D`) on the **Tinker Bell**
  ASIC (`v8tkbell` in `v8.cpp`: V8/Spice evolution, 8 MB cap, TV video-in),
  Cuda MCU, 68030 @ 31.3344 MHz. Emulated as `V8Memory::Model::MacTv` (a
  `spiceClass()` sibling of the Color Classic + a `cpuHz` ctor param, not a
  Sonora machine); gate `mactv_boot_etalon`, dispatch on `$EAF1678D`. See
  CHANGELOG 2026-07-25.
- The `$1003-$1006` entries (vid `$21/$17/$1A/$20`) route to the ROM
  monitor when probed blind; the `$2BAD` (IOSB!) entries are other
  derivatives — untested.

## Source of truth
- The EDE66CBD ROM disassembly (Capstone m68k, base `$40800000`) — the
  scratchpad `dis.py` pattern + the harness `POM68K_HALT` branch trails.
- MAME `maclc3.cpp` (macvail maps + MCU wiring), `mv_sonora.cpp`,
  `asc.cpp`, `cuda.cpp`. MAME's LC 520/550 do not boot; the ROM decides.
