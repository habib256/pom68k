# LC 520 bring-up — a worked example: the ROM as the only oracle

**What this file is.** The record of how the EDE66CBD all-in-one family was
brought up **with no working MAME driver to copy** — MAME's `maclc520` /
`maclc550` are non-booting stubs, so the ROM itself had to be the oracle.
It is kept for the **method** as much as the result: this is the playbook for
the next machine that has a ROM dump and nothing else. The three machines this
file first named as candidates (IIfx, Quadra 900/950, SE/30) have all since
shipped with MAME drivers to lean on; the remaining ROM-only entry is the
**PowerBook 150** (`docs/68K_FAMILY_SCOPE.md` § 3, `docs/DUO_BRINGUP.md`).

Contrast with the *other* bring-up shape, which is cheaper and should always
be tried first: the **Mac IIvx** booted on the first try by recombining proven
pieces per the MAME map (Sonora shell + V8 peripherals), no reverse
engineering at all. Recombine first; reverse-engineer only when there is
nothing to recombine.

Status: **DONE.** All three siblings boot System 7.5 to the Finder —
gates `lc520_boot_etalon`, `lc550_boot_etalon`, `cclassic2_boot_etalon`.
The machine as-built is `DEV.md` § 2.5 (Sonora); the code is
`SonoraMemory.*` / `SonoraCpu.*` / `SonoraVideo.h` with **`cudaAdb = true`**.

ROM: `roms/1MB ROMs/1993-10 - EDE66CBD - ...ROM` (header checksum
`$EDE66CBD`; user-provided, `roms/` is gitignored), shared by LC 520, LC 550,
Color Classic II and the Performa 275/550/560. CPU: LC 520 = 68030 @ 25 MHz,
LC 550 / CC II = 33.33 MHz (`SonoraMemory::kCpuHz` / `kCpuHzPlus`, selected by
the `kProfiles[]` table in `runLc3`, `main.cpp:1527-1556`).
**The Mac TV is not in this family** — different ROM, different ASIC, see
§ Siblings.

## The method

1. **Build the harness first, not the machine.** `tests/lc520_boot_etalon.cpp`
   was written before anything booted, with every knob needed to interrogate a
   dead ROM (below). Everything after that is bisection.
2. **Trace control transfers, not instructions.** `POM68K_HALT` dumps a
   128-deep **branch-target trail** — deduplicated control transfers, so it
   survives polling loops that would drown an instruction trace. This is what
   turned "black screen" into "the ROM took the monitor path at `$4084A6F6`".
3. **Read the ROM's own tables before assuming a bug.** Walls 2/3 were a
   misread: dumping the whole machine table showed the entries were there all
   along. Disassembly: Capstone m68k at base `$40800000` (scratchpad `dis.py`).
4. **Recognise the failure signatures.** "Polls SCC RR0 forever" = the ROM's
   serial monitor, i.e. startup tests were skipped, i.e. something upstream
   timed out. An audible chime early in a dead boot is likely the **error**
   chime, not success.
5. **When the ROM disagrees with MAME's device wiring, MAME's *other* drivers
   still know the answer** — `maclc3.cpp:379` had the right MCU all along.

### Harness knobs (`tests/lc520_boot_etalon.cpp`, pinned in `CMakeLists.txt`)

- `POM68K_BOXID=<hex>` — override the `$5FFFFFFC` model longword.
- `POM68K_SENSE=<n>` — override the monitor sense (`setMonitorSense`).
- `POM68K_AIO_EGRET=1` — build the machine with the Egret wiring instead of
  the Cuda: the Wall-4 A/B, kept because the ROM's `$2000-$2003` machine-table
  entries carry MCU type 0.
- `POM68K_DIAG=1` — periodic PC / SCSI / depth / d7 + Cuda-MCU-PC trace.
- `POM68K_PROBE=1` — 200-frame run + final-state dump.
- `POM68K_HALT=<hex pc>` — run until PC hits [pc, pc+8] or d7 bit 17
  (ROM-monitor flag) rises, then dump the 128-deep branch-target trail +
  registers. Two special values: `=2` prints the first 400 branch targets from
  reset and exits; `=1` traps on `SP == $2600` instead of a PC.
- `POM68K_FRAMES=<n>` — override the 16 000-frame budget (`kFrames`), or the
  200 frames `POM68K_PROBE` imposes.
- `POM68K_DUMP=1` — write `lc520_screen.ppm` at the end.

## The four walls, in boot order

### Wall 1 — the chime that was not a chime (fixed in `AscSonora::reset`)

The sound engine (`$408BD3F0`+) spins on ASC `$804` bit 3 waiting for FIFO B
empty. `AscSonora::reset` only latched FIFO A empty (`$02`); MAME sets `$0A`
(asc.cpp:465). Fix: `fifoStat_ = STAT_EMPTY_OR_FULL_A | STAT_EMPTY_OR_FULL_B`.

**The lesson is the misread**, not the fix: the chime that then played was the
**error** chime — the ROM monitor's bootstrap plays it *after* startup tests
fail. A boot that makes noise is not a boot (see Wall 4).

### Walls 2 & 3 — machine table + monitor sense (no bug; a bad dump)

The early probe suggested `$A55A0100` was absent from the `$408D1E6C` machine
table. Wrong: a full table walk shows **box `$0100` twice** (video type `[12]`
= `$32` and `$4B`) and `$0101` twice (`$4A` / `$4D`). The sense check at
`$40804B4E` is precisely the selector between them: sense `$60` (res 6,
built-in 640×480) wants `[12]` ∈ {`$32`, `$4A`}, sense `$20` (res 2, 512×384)
wants {`$4B`, `$4D`}.

So MAME's model ids were right and **sense 6 is the correct default**; the
`$1003-$1006` / `$2000-$2003` / `$2BAD` entries are other family members
(selected when sense ∉ {2, 6}). Also read off the table: d0 = entry flags
`$18(a1)` (`$773F` for the AIOs), d2 = entry `$10(a1)` hw code (byte 1 = `$32`
routes the PA0/PRAM config path at `$4084713C`).

### Wall 4 — the real one: wrong MCU (fixed in `SonoraMemory`)

The root-cause chain, established by branch-trail tracing:

1. At reset the ROM does an MCU handshake at `$408D1AE6`: raise ORB bits 4/5,
   then **ByteAck (PB4) low and wait for /TREQ (PB3) to deassert** within
   `$23CF` polls. The Egret 341S0851 firmware never answers — it is a
   **Cuda-protocol** handshake. Timeout → `$408D1C68` sets `d7 = $30`, skips
   **all** startup tests (bit 26 of d7 = "tests passed", set only at
   `$408471EC`), and funnels to `$4084A6F6` → error chime → serial-monitor
   bootstrap (`$408B9888` / `$408B98C2`, SCC init `$408B9F54`, command loop
   `$408B9906` polling SCC RR0 — the "polls SCC forever" Sad Mac signature).
2. MAME confirms it elsewhere: `maclc3.cpp:379` gives the LC 520/550 a
   **CUDA_V2XX (341s0060, Cuda 2.40)**, not the LC III's Egret.
   → `SonoraMemory` grew the `cudaAdb` ctor flag: Egret-HLE Cuda polarity +
   `CudaLle::Flavor::Cuda` + firmware search order 341s0060 → 341s0788, and
   `setI2cDfac(true)` (the DFAC2 I2C ACK — the same missing ACK that was the
   Color Classic "0417 wedge").
3. **Firmware revision matters.** With Cuda 2.37 (341s0788) the boot
   *livelocks* at `$408B399C`: the ROM's early config path sends pseudo-command
   `[01 0E]` (the LC 475 / Q605 ROM sends `[01 07]` there) and 2.37 keeps
   re-asserting TREQ while the host drains 2 bytes, forever. **Cuda 2.40
   (341s0060) answers `$0E`** and the boot sails through — RAM test, video
   init, SCSI, Finder.
4. `SonoraMemory` VIA1 port B undriven input bits now read pulled-up
   (`0xC7 | session<<3`), matching `V8Memory` / `Q605Memory`.

## Result

`lc520_boot_etalon`: model `$A55A0100`, sense 6, Cuda 341S0060 LLE →
System 7.5 Finder at **640×480×8 bpp color** (menu bar + color desktop weave +
drive windows), ~1750 SCSI commands at 16 000 frames. The gate signature is
**luminance-based**: the sibling gates' blue-channel ratio reads the
orange/green desktop weave as black.

## Siblings

- **LC 550 / Performa 550** — box `$0101`, 33.33 MHz (`kCpuHzPlus`), sense 6 →
  vid `$4A`, 640×480×8 Finder. Gate `lc550_boot_etalon`.
- **Color Classic II / Performa 275** — the same `$0101` board with **sense 2**
  → vid `$4D`, **512×384×8** Finder. The sense line is the whole machine
  difference. Gate `cclassic2_boot_etalon`.
- **Mac TV — not an EDE66CBD sibling** (2026-07-25). The shared AIO ROM's
  `$2000-$2003` entries (MCU-type 0 = Egret) look Mac-TV-ish but stall with
  **both** MCUs — wrong machine. The real Mac TV boots its **own**
  `eaf1678d.bin` (`$EAF1678D`) on the **Tinker Bell** ASIC (`v8tkbell` in
  `v8.cpp`: a V8/Spice evolution, 8 MB cap, TV video-in), Cuda MCU, 68030 @
  31.3344 MHz. Emulated as `V8Memory::Model::MacTv` — a `spiceClass()` sibling
  of the Color Classic plus a `cpuHz` ctor param, **not** a Sonora machine.
  Gate `mactv_boot_etalon`, dispatch on `$EAF1678D`. CHANGELOG 2026-07-25.
  *A ROM entry that mentions a machine is not evidence the machine uses that
  ROM* — that misread cost a round.
- The `$1003-$1006` entries (vid `$21/$17/$1A/$20`) route to the ROM monitor
  when probed blind; the `$2BAD` (IOSB!) entries are other derivatives —
  untested.

## Source of truth

- The EDE66CBD ROM disassembly (Capstone m68k, base `$40800000`) — the
  scratchpad `dis.py` pattern + the harness `POM68K_HALT` branch trails.
- MAME `maclc3.cpp` (macvail maps + MCU wiring), `mv_sonora.cpp`, `asc.cpp`,
  `cuda.cpp`. MAME's LC 520/550 do not boot; **the ROM decides.**
