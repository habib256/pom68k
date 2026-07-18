# CHANGELOG

## 2026-07-18 — Q2+Q4: the 68LC040 integer core executes in Moira,
## WinUAE-differential (5 400/5 400), no-FPU F-line included

Phase 3 (Mac OS 8.1 on an LC 475 / Quadra 605) CPU side, first two
milestones, converged by the established loop (fuzz040.py WinUAE-solo →
sst68040 replay → fix → fresh-seed re-verify). `Model::M68LC040` now
executes on Moira's shared C68020 core; every change is runtime-gated on
`cpuModel >= M68EC040`, so the 3 082-vector sst68030 gate and the
1 000 058-vector sst68000 gate are byte-identical. Full change list in
`extern/moira/POM68K_VENDOR.md § Q2/Q4`; highlights:

- MOVE16/CINV/CPUSH/MOVEC-040 execute; odd instruction-flow targets take
  the 040's format $2 address error with WinUAE's per-instruction A7/PC
  conventions (RTS re-pushes, RTD/RTR restore, Bcc/DBcc pre-condition
  checks); RTE handles formats $3/$4/$7 (SSW.CT continuation copy); the
  040 trace one-shot ("an SR write with old Tx set traces once") and
  post-instruction staged trace; 040 undefined-CCR rows for
  DIV/DIVL/CHK/ABCD.
- Q4 folded in: F2xx with no FPU → vector 11, format $4 frame with
  per-shape word consumption and EA (fpp.c `fault_if_no_fpu` call-site
  conventions), FBcc pseudo-conditions, FMOVEM invalid-EA Line-F. The
  sst68040 harness never attaches an FPU — an attached 68882 would mask
  the format-$4 path.
- **Why non-obvious**: two corpus-poisoning ORACLE-GLUE state leaks were
  unmasked (not Moira bugs, both sequence-order-dependent): stale
  `regs.t1/t0` at state load armed WinUAE's MakeFromSR one-shot trace
  (phantom vector-9 frames carrying the previous vector's `trace_pc` —
  an untraced RTS "taking a trace"), and the `mmu040_movem` restart
  latch made a MOVEM reuse the previous vector's faulted EA. Fixed in
  `oracle/uae/glue.c` (VENDOR.md); the corpora regenerate clean.
- Gate: **`sst68040`** (new CMake gate, tests/data/sst68040, 2 400
  pinned vectors core/random/mmu × off, soft-skip when absent) +
  3 000/3 000 fresh-seed re-verify; full CTest 25/25 including both
  boot etalons.

## 2026-07-18 — GISTPERSO (7.5) boot hang: heap corruption racing an
## app launch at Finder startup — NOT the pending changes, NOT the disk

User report after the host-machine crash: on the LC II, GISTPERSO-boot
(System 7.5) draws the Finder menu bar + desktop pattern, the clock
ticks, but the icons never come and "the menu is dead". Other volumes
(7.1/7.6/8.1) fine. Differential investigation (full detail in TODO
§ app-compat):

- The CPU spins forever in the ROM Memory Manager heap-walk/coalesce
  loop ($40A0E148) — a garbage block header, i.e. guest **heap
  corruption during Finder startup**, while VBL keeps the clock alive.
- **Exonerated: the uncommitted work.** HEAD and the working tree hang
  byte-identically (same screenshot md5, same hot-PC histogram).
  **Exonerated: PRAM** (known-good PRAM hangs the same) and **HFS
  structure** (catalog walks; SC2K + city play fine launched by hand).
- Startup-key probes (new `LCII_HOLD_KEYS` in lcii_trace, hex ADB codes
  held through boot): Option or Shift → desktop with icons (and no
  SC2K auto-launch); Cmd → same hang. A normal boot auto-launches
  SimCity 2000 while the Finder is still building the desktop, and one
  GUI boot survived exactly that → **timing-dependent race around the
  boot-time app launch**, deterministic loss headless, occasional win
  under GUI timing.

Workaround: boot holding Shift/Option, remove SC2K from the startup
launch; manual launch is unaffected. The deterministic headless repro
is pinned in TODO for the differential hunt for the real corruption
site. `hdv/GISTPERSO-boot.vhd.avant-reparation` backs up the image.

## 2026-07-17 — LC II runs on a dedicated machine thread; boot &
## secondary SCSI volumes selectable from a "Disques" menu

Interrupted by the host crash, finished and verified 07-18 (24/24
gates, long GUI sessions). Two changes from the perf/UX queue:

- **Machine thread** (`LcMachine`, main.cpp): the emulation loop +
  audio-clocked pacing move off the vsync'd ImGui thread, so a slow GPU
  frame or compositor stall no longer steals emulation time. Contract:
  input/reset cross as queued commands applied between frame slices
  (ADB/CPU objects are touched only by the machine thread), the
  framebuffer crosses as a decoded copy under a mutex (publish throttled
  to ~60 Hz when idle), the status line is relaxed atomics, and the ASC
  ring keeps its SPSC discipline (producer moved threads). Emscripten
  keeps the single-thread path — same `stepTick()`, two drivers. The
  destructor joins the thread so a stray `exit()` (Xlib's default error
  handler) can't destroy a joinable thread → std::terminate.
- **Multi-volume SCSI** (`Ncr5380` targets by ID 0-6, `V8Memory`
  `attachScsi(path, wb, id)`, CLI `argv[3..]` → IDs 1-6): the System's
  boot-time bus scan mounts the secondary volumes. The GUI "Disques"
  menu picks the boot volume and toggles secondaries from the images
  found next to the current one; any change relaunches the emulator
  (the ROM only scans the SCSI bus at boot), same execv mechanism as
  the machine switch. "Redémarrer" = one-click power cycle.

## 2026-07-17 — i-cache overlay folded into Moira's fetch path (-15%)

Second perf step from the 0.40×→1.91× pass's queue: the 68030 i-cache
timing overlay fired a **virtual** `willFetchInstr` on every instruction
word — an indirect call plus an out-of-line model per fetch, ~11% of the
whole emulator. The MC68030UM §6 model (16×4-LW logical direct-mapped,
CACR-gated, miss penalty) now lives inline in `mmuFetchWord` as
`Moira::PomIcache` behind an `armed` flag (default off; `Cpu030` arms it
and keeps the knobs/stats — POM68K_VENDOR.md § willFetchInstr). Same
model, same numbers: lcii_boot_etalon metrics byte-identical
(0.09/0.48/9583 SCSI commands), wall time **143 s → 122 s**, 24/24
green. Next in the queue: the dedicated machine thread.

## 2026-07-17 — Lode Runner "dead arrow keys": not a bug — the game
## binds the numeric keypad by default

User report: in Lode Runner (LC II) the arrows do nothing although the
game is otherwise perfect. Two-sided verification concluded the input
chain is correct and the behaviour matches real hardware:

- **Emulator side** — a KeyMap probe against the booted System (boot
  etalon + `AdbBus::keyEvent`, the exact entry point the GUI uses)
  shows every injected ADB raw code sets the right virtual-key bit in
  the System's KeyMap ($174): arrows $3B-$3E → virtual **$7B-$7E**,
  keypad $52-$5C → virtual $52-$5C, letters identity. That raw→virtual
  arrow remap is Apple's own KMAP doing its job — on ADB keyboards the
  old M0110A arrow codes $3B-$3E were reassigned to modifiers, and
  arrows moved to $7B-$7E.
- **Game side** — extracted the app's CODE resources off the HFS image
  (it is *Lode Runner: The Legend Returns*, Presage 1994) and
  disassembled its input path: gameplay polls a `_GetKeys` snapshot
  against a per-action **key-binding table stored in its `pref 200`
  resource**. The defaults are `56 58 5B 57 59 5C 53 54 55` — keypad
  4/6/8/5/7/9/1/2/3. No $7B-$7E (arrow) code appears in either the
  factory or the user's saved bindings: **the game simply doesn't bind
  the arrows**, on a real LC II either.

Conclusion: play with the numeric keypad (4← 6→ 8↑ 5↓, 7/9 = dig) or
rebind inside the game's options. No emulator change.

## 2026-07-17 — Performance pass: 0.40× → 1.91× realtime at the Finder
## (the sound stutter was the emulator falling behind real time)

The audio-clocked pacing needs ≥1× realtime to hold tempo; a gprof
profile (i7-10700F, headless Finder workload at cache-boost 4) showed
0.40× with **38% of all time inside two 22-entry ATC scans run on every
memory access** (`mmuAtcLookup` 25% + `mmuAtcTouch`'s second scan), and
every 16-bit bus access splitting into two full `read8` decode cascades
(1.8G calls/10s). Four fixes, semantics strictly preserved:

- **Moira ATC, O(1) pseudo-LRU** (`mmuAtcTouch`): a counter mirrors the
  number of set history bits, replacing the per-access 22-entry "any
  clear bit left?" walk. Byte-identical behaviour (every mru transition
  goes through touch or the resets).
- **Moira ATC, last-hit probe** (`mmuAtcLookup`): remembers the line
  that satisfied the previous lookup per (fc, direction) and probes it
  first — page-local streams check one entry instead of scanning 22.
  Same checks, same write-upgrade invalidation, same LRU touch; a stale
  remembered line just falls through to the full scan.
- **V8Memory word fast paths**: `read16`/`write16` service RAM, ROM and
  VRAM as single word accesses (side-effect-free regions only — I/O and
  the overlay keep the sequenced two-byte path); `ramIndex` inlined into
  the header.
- **Build: `-march=native` + LTO** by default (`POM68K_NATIVE=ON`; the
  emulator is built from source on the machine that runs it — pass
  `-DPOM68K_NATIVE=OFF` for a portable binary).

Result: **4.8× faster** (0.40× → 1.91× realtime, boost 4 included), so
the DAC-paced sound now has ~2× headroom at the Finder. The full CTest
suite drops from 316 s to 182 s; 24/24 green including sst68030's 3082
vectors (the ATC changes are semantics-exact by construction and the
MMU/PTEST corpus agrees); SC2K and Lode Runner repros unregressed.

Not done (next steps if a heavier workload still starves, in order of
bang-for-buck): a dedicated machine thread (decouple emulation from the
vsync'd ImGui loop — the 16-core host runs everything on one core
today), trimming the per-fetch i-cache overlay cost (~11%), and only
then a 68k→x86 JIT (weeks of work, and it would obsolete the fuzzed
Moira interpreter's exactness guarantees — last resort).

## 2026-07-17 — Lode Runner launch freeze: odd-SP interrupt frames were
## corrupt (vendored Moira fix) + sound tempo locked to the host DAC

**Lode Runner froze the machine at launch** (hard halt, no bomb). Chain,
established with a headless keyboard-nav repro (`scratchpad/lrtest*`):
the game's launch drawing runs QuickDraw's conversion blit whose 3-byte-
per-pixel stack temps leave **SP odd** (legal on the 68030); a level-1
interrupt accepted there pushed its frame through
`writeStackFrame0000`'s 010/020 branch, which still applied the 68000's
`& ~1` A0 masking — the whole frame landed one byte low while RTE reads
at true addresses → spurious FORMAT ERROR (vector 14) on the RTE → ROM
system error path → its bomb renderer died on a second fault → bus-error
cascade until the SSP went odd → double fault → frozen Mac. Fixed in the
vendored Moira (frames written byte-exact at the true SP; also fixed a
double ×4 of the stacked vector offset — $190 instead of $64 — passed by
`execInterrupt<C68020>`); details in POM68K_VENDOR.md § Odd-SP interrupt
frames, minimal repro `scratchpad/oddframe.cpp`. 24/24 CTest (sst68000
and sst68030 corpora unaffected by construction — verified green), Lode
Runner now reaches its title screen, SC2K repro still crashes=0.

**Sound tempo wobble fixed by audio-clocked pacing** (GUI): while the
guest streams sound, the emulation speed IS the musical tempo, so the
frame loop now paces itself against the host DAC instead of the host
CPU: when sound was heard recently, each GUI tick emulates only enough
60.15 Hz frames to keep `MacAudioHost`'s ring near ~100 ms
(`buffered()`), pushing silence too (`pushRaw` — inter-note gaps are
part of the timeline); with no sound, the old time-budgeted turbo runs
(fast boot/Finder). The DAC's fixed 22 254 Hz consumption locks the
tempo to real time and absorbs the vsync-60.00 vs frame-60.15 Hz drift
with no resampler; a starvation guard keeps the machine alive if the
audio device disappears. `src/main.cpp` LC II frame lambda +
`MacAudioHost::{buffered,pushRaw,started}`.

## 2026-07-17 — SC2K "coprocesseur absent" ROOT-CAUSED AND FIXED: Egret
## mid-flight packet retraction manufactured ghost ADB sessions

The crash that survived every timing fix (the ★ TODO item) was never the
VBL/A5 phase race — measurement killed that theory (`racecheck.cpp`:
over 83K interrupts, ZERO were accepted with A5=$4FA8; the O6.12
`irqDelay=2` guard protects the $A4B414-$A4B418 window exactly as
designed; every in-window acceptance lands at $A4B41C, after the movem).

The real chain, established with deterministic single-step harnesses
(`stackwatch2` → `dispatchtrace` → `jumptrace` → `coptrace` → `d2trace`
in the session scratchpad):

1. `Egret::tick` retracted an initiated packet the host hadn't acked for
   150K cycles (`kAbortDelay`). But initiation had already loaded the
   sync byte into the VIA1 shift register — the host's level-1 interrupt
   was in flight. Under SC2K's per-VBL redraw load the ROM's byte-read
   loop is legitimately preempted for 300K+ cycles, so the retraction
   fired routinely during play with the mouse moving.
2. The late host then consumed the stale sync, found XCVR_SESSION
   already low, and recorded a **1-byte session**.
3. The ROM Egret driver computes the ADB record's data length as
   `received - 4` (header) with no guard — the real Egret can never
   deliver a short initiated packet — giving **D2 = -3 = $FFFD**
   ($A14ACE/$A14AD2 `move.w ($10,A2),D0; subq.w #4` → $A151F4
   `move.w D0,D2`).
4. The ADB response dispatcher ($A0A494/$A0A4B4-$A0A4C0) then runs
   `move.b (A2)+,(A0)+ / dbra D2` — 64K iterations — **copying 64KB
   over the stack** starting at its own frame (A0=$91ACCA = entry SP),
   wiping the saved-SP cell, the caller frames, and the application's
   A5 world up to the jump table.
5. The epilogue's `movea.l (A7),A7; rts` then pops a smashed cell and
   returns into a QuickDraw dither pattern ($6D $B6 $DB repeating —
   decodes as `blt.s -74` chains, the -$48-stride PC walk in the crash
   rings); the wander takes an address error, the System Error handler
   calls _ExitToShell, whose patch dispatches through the (also smashed)
   jump table entry `$1400094E` → Line-F → the ROM's generic
   "coprocesseur arithmétique absent" bomb. All red herrings.

FIX: an initiated packet is COMMITTED once its sync byte is on the wire
— the retraction is removed entirely (`Egret::tick`; the transfer is
synchronous and host-clocked on the real part, and collisions are
host-handled — the ROM's senders check XCVR first). The boot-era
"bus-quiet deadlock" the retraction had been added for does not return:
24/24 CTest green including `egret_test` and both boot etalons. The
crash repro (loadcity640 navigation + 6000-frame mouse wiggle, formerly
2 Line-F bombs) now runs clean: **crashes=0**; a 30000-frame endurance
run is clean too. The abort machinery (`kAbortDelay`, `abortTimer_`)
is deleted outright. The i-cache overlay + irqDelay remain as-is — they
were correct; they just weren't this bug.

## 2026-07-17 — 68030 instruction-cache timing overlay (replaces the flat boost)

Replaced the constant boost with a real (if small) model of the 68030
instruction cache. New vendored Moira hook `willFetchInstr(addr, super)` fires
on every instruction-word fetch (`mmuFetchWord`, the mode-5 030 fetch choke
point); `Cpu030` models the on-chip 256-byte i-cache (16 lines × 4 longwords,
logical, direct-mapped, flushed on the CACR clear strobes — needed because the
blit is self-modifying) and charges a fetch-bus penalty (`icacheMiss_`) only on
a MISS, while the core runs at a resident-code ceiling (`cacheBoost_`). Net:
cache-resident code runs near the ceiling, miss-heavy cold code is throttled
toward real speed — the per-code-path behaviour of the real cache instead of a
flat fudge that can't tell them apart. Both knobs live-tunable
(POM68K_CACHE_BOOST ceiling, POM68K_ICACHE_MISS penalty). 24/24 CTest green
(incl. sst68030 3082 vectors — the hook is state-neutral, and lcii_boot_etalon).

**Validated by measurement** (`scratchpad/icachestat.cpp`, `Cpu030::
icacheStats`): the guest runs CACR-I=1 (i-cache enabled); boot is 80% hit; and
the SimCity **redraw hot path is 95% cache-resident** over 453 M fetches —
confirming the overlay boosts exactly the code that livelocks.

**Honest limit — the overlay does NOT cure the black-forest crash, and that's
now diagnosed.** A headless sweep shows the crash is *non-monotonic* in the
boost (boost 4 crashes more than 2): if it were a throughput problem more boost
would always help. It doesn't — so it's a **VBL/redraw phase race** (the VBL is
taken at $A4B416, between SC2K lowering IPL at $A4B414 and restoring A5 at
$A4B418; whether it lands there shifts with the boost). Clearing it by brute
throughput needs ~24×, which no realistic cache (~2-4×) can produce. So the
overlay is the correct throughput model and gives uniform tempo, but black
forest needs a separate structural fix to the interrupt timing (TODO §
App-compat). Workaround: raise POM68K_CACHE_BOOST.

## 2026-07-17 — retire the adaptive cache boost for a constant ratio

The adaptive cache boost (base 2, spiking to maxBoost 24 during heavy per-VBL
redraws) gave a *varying* CPU/peripheral ratio. Once app sound worked
(pseudo-VIA fix below) that varying ratio made the sound tempo wobble audibly
(the emulated sound production isn't locked to the host audio clock, so the
2↔24 flips over/under-ran the ASC out ring), and it still only *deferred* the
SimCity livelock. Replaced it with a SINGLE CONSTANT ratio (`Cpu030
cacheBoost_`, default 2, tunable live via POM68K_CACHE_BOOST, range widened to
1-64), like real hardware which has one fixed CPU/peripheral ratio → uniform
tempo. Removed `maxBoost_`, the IRQ-rate hysteresis, and the `willInterrupt`
counter; `flushTicks` now scales by the constant. 24/24 CTest green.

**What the boost physically is** (verified while doing this): Moira emulates
the 68030 on its `Core::C68020` execution core (MoiraTypes.h:66) — 68020
per-instruction cycle counts (advisory placeholders, POM68K_VENDOR.md), **no
i-cache, no d-cache**, less pipeline overlap than a real 030. The real LC II
68030 has a 256 B I-cache + 256 B D-cache (the System enables them via CACR),
so tight loops run from cache with no fetch bus cycles and it executes far
more instructions per unit machine-time than Moira charges. The boost is the
scalar that compensates for that whole gap.

**Why a constant can't fully win** (headless sweep on the biggest SimCity city
"black forest monstre"): boost 2 (near real-time) still crashes it, boost 4 is
worse (non-monotonic — it's a VBL/redraw *phase race*, not pure throughput),
and clearing it needs ~16-24 which runs ~10× slow. Root reason: the blit is a
*tight cached loop* that gains ~5×+ from the real cache while other code gains
~1-2× — a single scalar can't say "tight loops fast, rest normal". Only a real
68030 i-cache timing model captures that. Intended long-term fix: a cache
overlay on Moira's prefetch path keyed on `cpuModel >= M68030` + CACR (NOT a
new `Core::C68030` — the 020/030 share the execution core by design; the
overlay is the right granularity). Tracked in TODO § App-compat.

## 2026-07-17 — app sound reaches the ASC (pseudo-VIA ASC IRQ was edge-only)

Apps on the LC II were silent — only the ROM boot chime ever played. Traced
with new ASC/trap diagnostics (`AscV8::onWrite`/`onRead` taps,
`PseudoVia::reg()` peek, `scratchpad/{ascprobe,sndtrace,sndtrace2,sndtrace3}
.cpp`): SimCity 2000 *does* call the Sound Manager (`SndNewChannel`,
`SndPlay`, `SndDoCommand`, 281×`SoundDispatch`) and the Sound Manager *does*
init the ASC (reads version $E8, sets FIFO mode) and *does* enable the ASC
interrupt (pseudo-VIA IER bit 4) — but **IFR bit 4 never latched**, so the
level-2 handler never serviced the ASC and never refilled the FIFO → silence.

Root cause: the ASC IRQ is **level-triggered**, but `PseudoVia` only updated
IFR bit 4 on line *transitions* (the edge-driven `onIrq` callback), and
`recalcIrqs()` erases any non-enabled pending bit. After the boot chime the
FIFO empties and the ASC half-empty line asserts and **stays asserted**;
while the ASC interrupt is disabled that pending bit is masked away. When the
Sound Manager later enables the interrupt, the line is *already* high — no new
transition — so IFR bit 4 was never re-latched. The slot IRQs already
re-derive their level every `recalcIrqs()`; the ASC path uniquely relied on
edges.

Fix (`src/PseudoVia.*`): store the raw ASC line level (`ascLine_`) and
re-sample it into IFR bit 4 on every `recalcIrqs()`, exactly like the slot
lines — so the level survives an enable/disable and re-latches when the
System enables the ASC interrupt with the line already high. Verified: SC2K
now writes 3996 non-silent samples to the FIFO on city-load (was 0); 24/24
CTest green (incl. pseudovia_test, asc_test, lcii_boot_etalon). Known
follow-ups (TODO § App-compat): the emulated sound *production* isn't locked
to the host audio clock and the adaptive cache boost's varying CPU/peripheral
ratio makes the tempo wobble, and the SC2K livelock crash still recurs — both
are the same open timing project.

## 2026-07-17 — adaptive cache boost (fixes big-city SimCity crash)

The user's biggest SimCity 2000 city ("black forest monstre") still crashed
on load at 640×480 — reproduced headless (`scratchpad/loadcity640.cpp`:
navigates SC2K → Charger ville → SIM VILLES → the save, then wiggles the
mouse during the redraw). At 512×384 it was fine; the 640×480 redraw is
~1.6× heavier and overran the fixed boost=2. A fixed boost=6 fixed it but
slowed the whole emulator (6× more instructions/frame); boost=4 was worse
than 2 (crash count is non-monotonic — the boost shifts *when* the livelock
trips).

Fix: `Cpu030` boost is now ADAPTIVE. A normal frame takes few interrupts; a
heavy per-VBL redraw takes many dozen (the redraw handler re-enters). When
last frame's IRQ count crosses a threshold, the CPU runs at maxBoost_ (24)
with ~0.75 s hysteresis, then falls back to the base boost (2). So normal
play stays fast and only heavy redraws briefly slow down — no crash.
Verified: black forest monstre loads clean at 640×480; 24/24 CTest green.
POM68K_CACHE_BOOST still sets the base floor.

(maxBoost_ was raised 16→24 once flushTicks was corrected to scale by the
*active* boost — see the second audit below. Before that fix flushTicks
divided by the base boost while the CPU ran at 16×, so peripheral time ran
8× fast and accidentally out-ran the livelock; scaling correctly restored
real VBL cadence, which needed a higher ceiling to clear the redraw.)

## 2026-07-17 — adversarial subsystem audit #2: 9 correctness fixes

A second adversarial multi-agent audit (LC II subsystems vs MAME/Basilisk/
Z8530 & 6522 & SCSI-1 manuals, findings attacked by skeptics) surfaced 17
candidates; after maintainer verification 9 applied, the rest rejected as
false positives or deferred as defensive fixes for access patterns the
guest never performs. 24/24 CTest green.

- **flushTicks scaled by the base boost, not the active one** (`Cpu030`):
  during an adaptive-boosted redraw the CPU ran at maxBoost× but flushTicks
  divided elapsed cycles by cacheBoost_, so VBL/VIA/ASC time ran up to 8×
  fast. Added `activeBoost_` (the boost in force this frame) and scale by
  it. This restored real peripheral cadence (correct sound/timer pitch);
  maxBoost_ raised 16→24 to keep black forest crash-free at true speed.
- **VIA CA2/CB2 flag over-clear** (`src/Via6522.cpp`): an ORA/ORB access
  cleared CA2/CB2 unconditionally, but in the ROM's independent-interrupt
  PCR mode ($22) the RTC 1-second flag must survive a racing port access
  (R6522 §3.2.3). Added `clearCaFlags`/`clearCbFlags` that honour the PCR
  mode.
- **VIA T2 low-byte latch** (`src/Via6522.cpp`): T2CL wrote the counter
  directly; on the 6522 it only stages a low latch that T2CH commits
  (§5.6). Added `t2ll_` staging.
- **ASC FIFO overrun wrote past FULL** (`src/Asc.cpp`): a push at cap=0x400
  still stored and wrapped wr_, overwriting the oldest unread byte. Guarded
  the store by `cap_ < 0x400` (MAME stalls on full).
- **V8Video 16bpp fetch could read one byte past VRAM** (`src/V8Video.h`):
  bounds-guarded the `vram[off+1]` high-byte read.
- **RTC PRAM write underflow** (`src/Rtc.cpp`): addr 14/15 fell through to
  `pram_[addr-16]` = `pram_[-2]/[-1]`. Added an `addr < 16` guard.
- **Egret second-accumulator not reset** (`src/Egret.cpp`): `secAcc_`
  survived reset; zeroed it.
- **SCC RR2 vector priority Ext-over-Tx** (`src/Scc8530.cpp`): within a
  channel the Z8530 ranks Tx above Ext/Status; the status-low code checked
  extPending first. Swapped to Tx-first.
- **SCSI MODE SENSE / REQUEST SENSE conformance** (`src/ScsiDisk.cpp`):
  MODE SENSE(6) now fills the mode-data-length (byte 0 = n−1) and the
  block-descriptor block length (512); REQUEST SENSE clamps the
  additional-length to `min(10, alloc−8)` instead of a fixed 10.

Deferred (not bugs in practice): a defensive SCC word-access fast-path —
a 16-bit access to SCC space double-advances the read pointer, but the
Mac SCC driver only ever uses `move.b`, so it never triggers. Noted in
TODO rather than adding an unverifiable fast-path.

## 2026-07-17 — adversarial subsystem audit: 3 correctness fixes

An adversarial multi-agent audit (11 LC II subsystems vs MAME/Basilisk/
manuals, each finding attacked by two skeptics) surfaced 6 candidates;
after maintainer verification, 3 applied, 3 rejected. 24/24 CTest green.

- **irqDelay not cleared on reset** (`extern/moira/Moira/Moira.cpp`): the
  O6.12 SR-write IRQ-recognition delay survived /RESET (reset zeroes ipl/
  fcl/mmu/fpu but not this new field). Added `irqDelay = 0;`. State
  hygiene; post-reset IPL=7 masks any observable effect.
- **SCC IRQ line was latch-high only** (`src/V8Memory.cpp` tick): was
  `if (scc_.irqAsserted()) sccIrq_ = true;` — one-directional, so a
  de-asserted SCC could leave a stuck IPL 4. Now `sccIrq_ =
  scc_.irqAsserted();` (bidirectional; updateIrq() applies it just after).
  Behaviour-preserving in every currently-reachable state, removes the
  fragility.
- **SCC Break/Abort ext-int latch was order-dependent** (`src/Scc8530.cpp`):
  it latched only when WR15 bit 7 was written after WR1 bit 0, not the
  reverse. Per the Z8530 UM the request must fire when the last required
  enable bit is set, order-independent. Added the symmetric WR1-last block.

Rejected: ADB Talk-reg-3 "missing length byte" (FALSE POSITIVE — Basilisk's
`data[0]=2` is the ADBOp buffer format, not the ADB wire the Egret HLE
speaks; reg 0 already works without it, so adding it would break ADB init);
WRITE(10) length-0 → 65536 (FALSE POSITIVE — 16-bit CDB length 0 = 0 blocks
per SBC, not 65536); MMU hardcoded page mask (harmless — re-masked
downstream at :782 and oracle-matched, not worth risking the 3082-vector
sst68030 gate).

## 2026-07-17 — LC II GUI defaults to 640×480

The GUI now boots the LC II at 640×480 (13"/14" RGB) instead of 512×384:
it's the roomiest built-in mode and some software needs ≥640×400 (Lode
Runner errors out at 512×384 with "requires a monitor of 640×400 or
greater", Error #713). `POM68K_MONITOR=512` forces the old mode; the CPU
window buttons still switch live. Tests keep V8Memory's own 512×384
default (only the GUI path picks 640). Note: a freshly-selected monitor
comes up B&W (depth is per-monitor); pick 256 colors in Tableaux de bord
→ Moniteurs and restart once — the choice then persists via SCSI
write-back.

## 2026-07-16 — LC II keyboard: arrow keys + numeric keypad

The LC II ADB key table (`main.cpp`) was missing the arrow keys and the
numeric keypad, so games that steer with the arrows (Lode Runner) got no
character movement. Added them with the correct ADB raw codes (arrows
$3B-$3E, keypad $52-$5C; the table stores `code<<1` and sends `code>>1`).

## 2026-07-16 — SimCity 2000 crash fixed: 68030 i-cache throughput model

SC2K crashed in-game with "coprocesseur arithmétique absent" (Line-F).
Full diagnosis (memory `pom68k-simcity-crash`, TODO § O6): NOT an FPU bug
and NOT a Moira bug — a WinUAE co-simulation (`scratchpad/cosim.cpp` +
`dumpstate.cpp` against `oracle_uae`) matched Moira instruction-for-
instruction over 2M+ steps, and the interrupt exception frame was correct
too. It's an interrupt-timing LIVELOCK: SC2K's per-VBL screen redraw is a
QuickDraw blit that lowers IPL just before restoring A5 ($A4B416→$A4B418);
an interrupt fires in that 1-instruction window, its redraw handler runs
~a frame, and by the time it returns the next VBL is already pending →
taken again before A5 is restored → the redraw re-enters with A5 still the
blit working value ($4FA8) → `jsr (A5+$14AA)` into garbage. Measured: from
that IRQ, 2M instrs never reach the A5 restore.

Root cause: the real 68030 runs tight loops from its 256-byte on-chip
instruction cache at ~1 cycle/word, executing far more instructions per
frame than Moira (no i-cache model) accounts for, so its redraw finishes
within the frame. We model that throughput: `Cpu030` runs the core `cacheBoost_`× more
instructions per unit of peripheral time, and `flushTicks()` scales
elapsed Moira cycles back down so the VBL / VIA timer / ASC cadences stay
at their real rate (the sound/clock are unaffected — only the CPU does
more work per frame). Default 2, overridable with **POM68K_CACHE_BOOST**.

The real trigger was found (2026-07-16): the crash needs the MOUSE MOVING
during the initial map redraw. Moving the mouse makes the System redraw
the cursor every VBL, which tips the already-heavy redraw over its frame
budget → the livelock returns (headless repro `scratchpad/navtest.cpp`
with a mouse "wiggle": 12427 crashes). The throughput model alone wasn't
enough. The real fix is **Moira's IRQ-recognition delay after a mask-
lowering SR write** (POM68K_VENDOR.md § O6.12): the 68k doesn't sample
interrupts until after the instruction following a mask change, which
guarantees the blit reaches its A5 restore between IRQs. Delay depth 2 +
cache boost 2 together take the wiggle repro to **0 crashes**; 24/24 CTest
green (incl. sst68030's 3082 vectors, cpu_smoke, lcii_boot_etalon). Verified: SC2K boots to gameplay and advances jan 1900 → jan
1901 with no crash (headless nav, `scratchpad/navtest.cpp`), and
`lcii_boot_etalon` still passes.

A stricter fix — Moira's one-instruction interrupt-recognition delay after
a mask-lowering SR write (M68000 PRM, which guarantees forward progress) —
was tried and REVERTED: it perturbed IRQ timing elsewhere and actually
reintroduced the crash. The throughput model is the shipping fix.

## 2026-07-16 — Selectable resolution (512×384 / 640×480) + per-monitor depth

The LC II built-in V8 video drives two color modes; which one is picked
by the *monitor sense code* (`montype_`), the ID resistors a real Mac
reads off its video connector at reset. Exposed it two ways: env
`POM68K_MONITOR=640` at launch, and live buttons in the CPU window
(512×384 / 640×480). Switching does a Mac reset (the ROM reads the sense
at boot only). Window grew to 1320×1040 so 640×480 at 2× fits.

Only these two — 640×870 portrait needs a framebuffer wider than the V8
provides (870×1024 > the 512 KB VRAM), and nothing larger existed on the
LC II's built-in video. Larger modes would mean emulating a different
machine (NuBus video card, later Mac).

Per-monitor depth: a real Mac keeps each display's bit-depth separately,
but our sPRAM models one shared video block ($58-$5A: depth + mode), so
booting a second monitor rewrote the first's depth — alternating 512↔640
turned the 512 back to B&W (found immediately). `setMonitorSense` now
parks the outgoing monitor's $58-$5A and restores the incoming one's, so
each resolution keeps its own color choice within a session. Known limit:
the parked sets aren't persisted to the `.pram` (which holds one block),
so quitting in 640 B&W means the next launch starts 512 in B&W until you
re-pick 256 colors — recoverable, and 512 (the default) normally stays
color across restarts.

## 2026-07-16 — SCSI write-back (persist guest disk writes)

`ScsiDisk::open(path, writeBack)`: with write-back on, every WRITE(6)/
WRITE(10) is written through to the backing file immediately and flushed
(no exit-time step to miss if the process is killed) in addition to the
in-memory image. The GUI (`main.cpp`, both the Plus and LC II loops)
attaches with write-back ON — the emulated Mac is a daily driver, saves
made inside it must survive. Tests keep the default (`writeBack=false`,
read-only) so the reference images (`hdv/boot.vhd`, `GISTPERSO-boot.vhd`,
`HD20SC.vhd`) are never modified — verified: their mtimes are unchanged
after a full 24/24 CTest run. If the file can't be opened read-write (or
a write later fails, e.g. disk full) it warns on stderr and falls back to
in-memory-only rather than aborting. Direct `image()` pokes (the etalon's
$6A DDM fixup) still bypass the file, as before. Verified end to end: a
writable copy's bytes change across a boot and it re-boots cleanly from
its own modified state.

## 2026-07-16 — LC II color (8 bpp by default) + peripheral-tick batching

- **The Finder ran black & white**: the machine was fine — the ROM
  video driver exposes 1/2/4/8/16 bpp through Monitors (verified by
  scripting the ADB mouse through Tableaux de bord → Moniteurs headless)
  — but the depth defaults to B&W until the user picks a mode, and the
  choice is only committed to XPRAM at the next Mac restart. XPRAM $58
  is the built-in-video sPRAM byte: $80 = ROM cold-boot flag (mode 0 =
  1 bpp), low bits = mode index ($83 = 8 bpp). `Egret::factoryDefaults`
  now seeds $83, so a fresh PRAM boots straight into 256 colors; an
  existing `.pram` keeps whatever Monitors last committed.
- **GUI felt slow**: three causes. (1) The fixed turbo ×8 asked for 8×
  real time when the core sustains ~1.4× on a free host core — every
  GUI frame blocked ~100 ms of emulation and the UI (and mouse) dropped
  to ~10 fps. The LC II loop now time-budgets turbo: one frame slice
  always (real time at vsync), more only while <10 ms of the frame
  budget is spent. (2) `Cpu030::sync` ran the full `V8Memory::tick`
  sweep (VIA + Egret + ASC + SWIM + SCC + IRQ resolve) on every bus
  access; peripherals only need to be current at a device-space access,
  so ticks now batch up to 128 cycles (8 µs) and `V8Memory` flushes
  before any I/O register touch — +17 % core throughput (18.4 →
  21.5 MHz emulated, CPU time). (3) Not the emulator: the host was
  saturated by stale test processes from earlier sessions.
- NB: SCSI writes remain in-memory only (`ScsiDisk`) — Finder/desktop
  changes and the Monitors 'scrn' resource are lost when the emulator
  exits; only XPRAM (the `.pram` file) persists. Write-back is backlog.

## 2026-07-16 — LC II GUI showed a black screen (texture alpha)

The V8 machine booted fine (etalon green, headless framebuffer decode
normal) but the GUI stayed black: `V8Video::decode()` packs `00RRGGBB`
(alpha byte 0), and ImGui renders textures with alpha blending enabled,
so the whole screen texture drew fully transparent over the dark window
background. The Plus path never hit this because `MacVideo` emits
`FF000000`/`FFFFFFFF`. Fix in `main.cpp` only (the decode contract and
its pinned tests keep `00RRGGBB`): force `A=$FF` on each pixel before
the `GL_BGRA` upload.

## 2026-07-16 — review fixes (8-angle bug hunt) + UI: mouse capture, drag fix, machine menu

Confirmed findings from the multi-angle review of the pending O6 work,
all fixed and re-gated (24/24 CTest):

- **`read16` byte order was compiler-dependent** (`V8Memory.cpp`,
  `MacMemory.cpp`): the two side-effecting `read8` calls were unsequenced
  operands of `|` — a right-first compiler would byte-swap every 16-bit
  SCSI pseudo-DMA blind transfer (silent disk corruption on e.g. the
  planned WASM/clang target). Now two sequenced statements.
- **VIA1 port-B input register**: `setInB` replaced the whole register
  with only the XCVR bit, so every other input pin read 0 instead of the
  6522 pull-up 1 (incl. the ROM's legacy RTC-probe lines PB0-PB2). Now
  `$C7 | xcvr<<3`. NB pull-ups must NOT extend to PB4/PB5: those are
  host-driven Egret handshake lines and the HLE is edge-triggered —
  modeling them pulled-up while DDRB is still 0 at reset reads as a
  phantom session rise and wedges the transport (found the hard way:
  black-screen etalon).
- **SCC TxIP was latch-on-enable**: a real 8530 sets Tx-Int-Pending when
  the buffer BECOMES empty, not because Tx IE is enabled over a
  never-filled buffer — the old model fired a spurious level-2 on the
  Mac Plus for any app arming WR1 bit 1. Now edge-triggered via a
  became-empty event, consumed by Reset Tx Int Pending.
- **SCC Break/Abort re-latch was a free-running ~7.8 kHz timer**: any
  LC II software arming WR15 bit 7 + MIE would have received a perpetual
  interrupt storm. Now event-driven — each Reset Ext/Status the driver
  issues re-arms a ~130 µs countdown (ties the abort stream to actual
  servicing, which is all the LAP retry rundown needs); an
  armed-but-unserviced channel latches exactly once. Both abort paths
  now also require WR1 bit 0 (per-channel Ext Int Enable), like the DCD
  path. `scc_ext_test` extended to pin all of this (18 checks).
- **Audio silence gate was DC-blind** (`MacAudioHost.h`): an underrun
  ASC FIFO repeats its stale byte (MAME-faithful — the core is correct),
  a full-scale DC stream the old `peak < 0.01` gate happily pushed into
  the ring from power-on. Gate is now on min/max span (AC amplitude).
- **`localTalkWatchdog` fired silently**: it pokes guest RAM on a
  fingerprint pinned to one AppleTalk version's globals — now logs to
  stderr when it releases the mutex.
- **`lcii_trace` "stopped" flag was `(SR & 0)`** — constant false; the
  one diagnostic separating a STOP-parked CPU from a spin loop never
  reported. Added `Moira::isStopped()` (vendored, POM68K_VENDOR.md).
- CLAUDE.md gate count corrected (24, not 22).

UI (user request): the **Delete key toggles hard mouse capture** (GLFW
disabled cursor, raw deltas, ImGui mouse off); the emulated screen is
now an InvisibleButton, so a **drag started on the Mac screen never
moves the ImGui window** (title bar still does) and keeps feeding the
Mac after the pointer leaves the item — Finder drag-and-drop works; a
**main-menu-bar "Machine" menu switches between the Mac Plus and
Mac LC II profiles** (relaunches the process on the other ROM, PRAM
saved first; entries grey out when the ROM is absent). Shared
`ScreenInput` helper + one key table remain a TODO cleanup.

## 2026-07-16 — O6.11 RESOLVED: GISTPERSO boots to the Finder — Egret XPRAM protocol fix makes AppleTalk genuinely inactive

The clean fix option (a) landed: AppleTalk is now **inactive at boot**, so
`.MPP` never brings up LocalTalk and GISTPERSO's System 7.5 boots to the
Finder desktop (menu bar + mounted volume, screenshot-verified; the
downstream $8009372A wedge is gone with it — it was fallout of the
watchdog's crude half-initialised give-up, which no longer triggers).

**Root cause — an Egret HLE protocol divergence, not AppleTalk itself.**
Where classic Mac OS keeps the flag (primary sources: Apple's leaked
System 7.1 "supermario" tree; verified against this System's own `lmgr`
disassembly):
- "AppleTalk active" = classic-PRAM **SPConfig** byte, low nibble =
  port B use: 1 = useATalk, 2 = useAsync (Patches Release Notes radar
  #1032330; `BeforePatches.a` sets `emAppleTalkInactiveOnBoot` from it).
  On Egret machines SysParam bytes 0-15 live at **XPRAM $10-$1F** (so
  SPConfig = XPRAM $13, Basilisk's default $22 = inactive), bytes 16-19
  at $08-$0B.
- XPRAM `$E0-$E3` (`LAPMgrEqu.a: ATalkPRAM`) is only the **connection
  selector** (low byte = 'atlk' resource id, 0 = built-in LocalTalk); a
  bad id **falls back to built-in** (`NetBootlmgr.a InstallE`), so
  Basilisk's `$00F1000A` never disabled anything — dropped, zeros now.
- AppleTalk 57.x self-heals SPConfig 0/$F → 1 (= active!). That is what
  fired here: `lmgr` found $1FB=$FF and wrote $F1 → PortBUse=1 → the LAP.

Why $1FB was $FF: the ROM's SysParam restore never completed, because
**three Egret XPRAM wire-protocol behaviours were wrong** (pinned from
the ROM's own drivers: sender $40A0C5CC/$40A1557A, transaction engine
$40A14912, 24-bit reader $A4A33C):
- **ReadXPram $02 and GetPram $07 are byte STREAMS with no length on the
  wire** — [1,2,1,addr] / [1,7,addrHi,addrLo]; the ROM's 'NuMc' check
  reads 4 bytes, the SysParam restore reads 16 (at $10 → $1F8!) then 4
  through ONE GetPram each, the boot-flag read takes a single byte, all
  with identical commands. Egret now streams 32 bytes.
- **The HOST terminates a stream** by dropping SYS_SESSION after its
  count, then waits for XCVR_SESSION release ($40A149C4, $A4A3B4-BC).
  `Egret::portBChanged` now aborts the reply and releases XCVR on a
  session drop during RESP_SEND. (Old 1-byte replies only worked because
  XCVR happened to drop with the single byte.)
- **WriteXPram $08 existed only on the wire** ([1,8,1,addr,data…], length
  = the data) — unhandled, it was ack-swallowed, so the ROM's 'NuMc'
  signature write never stuck and **every boot re-ran the cold-PRAM
  XPRAM re-init, twice** (~160M cycles: WarmStart now at 170M vs 330M).

Chain after the fix: 'NuMc' validates → warm-PRAM path → SysParam block
restored ($1F8 = A8 00 00 **22** CC 0A CC 0A …) → SPConfig port B nibble
= 2 (async) → `lmgr` LInit sees AppleTalk inactive → `.MPP` never opens
LocalTalk (0 SCC WR15 arming, 0 level-4 IRQs the whole boot). The
Chooser can turn AppleTalk back on (writes SPConfig=1 to XPRAM $13,
battery-persisted) — that path re-enters the O6.10/O6.11 SCC + watchdog
machinery, which stays as the fallback.

Correction to the 2026-07-16 entry below: GISTPERSO's System (F1-7.5,
French System 7.5) **does** carry `ltlk` 0-7 + `atlk` 1/3 + `lmgr` 0 —
the LAP code at $A5F4C-$A73A4 is `ltlk` 0 itself (resource-offset match
on the `$63e` mutex spin at +$5F4). The earlier "no ltlk exists" claim
came from runtime `GetResource` probes that ran before the System's
resource map was current.

Gates: `egret_test` extended (stream reads, host-terminated session,
WriteXPram round-trip); full suite green incl. `lcii_boot_etalon`.
Dev tooling: `lcii_trace` gained `TRAJ_AT` (retargetable jump-trajectory
dump), `WATCH_SP` (SysParam/PortBUse write watch), `WATCH_XPTRAP`
(_ReadXPRam/_WriteXPRam trap log).

## 2026-07-16 — O6.11: LocalTalk LAP — SCC abort stream + HLE watchdog

GISTPERSO's AppleTalk-active System runs the built-in `.MPP` LocalTalk LAP
(no `ltlk` ADEV exists in it — verified by disk scan and by every runtime
`GetResource('ltlk',0)` returning null; Basilisk's ltlk-resource patch is
therefore inapplicable, and its XPRAM `$E0-$E3` disable had no effect
because this System does not gate LocalTalk on those bytes). The LAP arms
an SDLC transaction on the SCC and its caller busy-waits at RAM `$A6540`
on a driver mutex (`$63e`) for a completion that never arrives. Two
changes, the first a real hardware fix, the second an HLE aid:

- **`Scc8530` streams the standing Break/Abort** (`Scc8530::tick`, wired
  into `V8Memory::tick`). Traced state: WR9 MIE set, WR15(B) Break/Abort
  IE armed, but a single latch — the driver services one abort, resets
  ext/status (clearing it), and waits for the next. On an open LocalTalk
  line the SDLC receiver keeps detecting aborts, so once IE+MIE are armed
  the ext/status interrupt must RE-present; the tick re-latches it every
  ~2000 cycles. This is correct 8530 behaviour and now delivers a stream
  of level-4 SCC interrupts (verified: 20+ taken vs 4-then-silent
  before); `scc_ext_test` green, Plus mouse path (`input_etalon`) green.
- **`V8Memory::localTalkWatchdog`** (HLE, `POM68K_NO_LTALK_WD` disables).
  The abort stream is necessary but not sufficient: the LAP completion is
  woven through the SCC ISR (which only resets the channel, ROM $A6C8E)
  and a Time-Manager timeout — three subsystems. Rather than emulate all
  of it, the watchdog recognises the wedged transaction (the LAP mutex,
  at the AppleTalk globals `*(*(ExpandMem)+$70)+$63e`, held ~0.5 s while
  the abort stream runs) and releases the mutex byte the caller spins on,
  so its retry loop runs down and `.MPP` moves on. This clears `$A6540`
  (dominant hot PC 100M→ gone; the LAP now only retries ~19×).

**Status: this advances GISTPERSO past the LocalTalk LAP but NOT to the
Finder.** Boot proceeds into 32-bit-mode System code and wedges again at
a new spot ($8009372A, a tight loop scanning unmapped memory — a
different subsystem, A2 base $B11B8 vs the LAP's $96AC8). The crude LAP
"give up" likely leaves AppleTalk half-initialised, so a downstream scan
runs off the end of a structure. Reaching the Finder needs either a
clean "AppleTalk inactive" (its System resists the XPRAM route) or real
LocalTalk/Time-Manager completion — tracked in TODO § O6.11. The
24 CTest gates stay green; boot.vhd (no AppleTalk) is unaffected (the
watchdog never arms without Break/Abort IE).

## 2026-07-15 — O6.9 resolved: GISTPERSO's vector-2 storm — RTE honors a cleared SSW.DF

- **The 6.8M-deep vector-2 storm was NOT the pipe-stage words.** The
  RAM routine at $1313E/$1315E is Mac OS's slot-probe recovery; its
  reads at ($e,A7)/($a,A7) sit above a pushed D0, so they are the
  **SSW** and the **format nibble**, not pipe stage B. Protocol
  (decoded with the new `lcii_trace --dasm`): probe `move.b (A1),D0`
  on $FCFFFFFF; on the $B bus-fault frame, RTE **with DF set** = retry
  the data cycle (budget 64); then `bclr #0` on the stacked SSW high
  byte (= clear DF) + RTE = "cycle done, complete the instruction with
  the frame's data input buffer". Moira's $B RTE discarded the frame
  and re-ran the instruction unconditionally → re-fault forever.
- **Fix (vendored Moira, POM68K_VENDOR.md § RTE $B honors a
  software-cleared SSW.DF)**: the RTE pops now capture SSW / fault
  address / data input buffer; the bit-9 "frame carried DF" marker
  (WinUAE encoding, already stacked by `mmuPageFault`) with bit 8
  cleared arms a one-shot latch that `mmuRead`/`mmuWrite` consume when
  the restarted instruction re-issues the exact faulted access: reads
  return the data input buffer, writes are skipped. The mode-5 WinUAE
  oracle zero-fills the pipe and retries *inside* its RTE step, so it
  cannot testify byte-for-byte — pinned instead by `berr030_test` § 5
  (the exact Mac probe pattern: retry once with DF set, then DF-cleared
  continuation). Result: 520 bus errors (4 probes × 64 retries × 2
  passes — the designed cost) instead of millions; boot proceeds to
  the French System 7 « Bienvenue » screen.
- **Storm gone, boot now reaches the graphical « Bienvenue » (Welcome)
  splash with progress bar** (screenshot; ~170 s of Mac time, VBL and
  Time Manager ticking, 856 SCSI selections). This is the O6.9 fix: the
  reported bug — GISTPERSO dying in a vector-2 storm — is resolved.
- **Beyond the storm: AppleTalk/LocalTalk (new, O6.11).** GISTPERSO's
  System has AppleTalk active, so it opens `.MPP` and drives the SCC at
  $50F04000. Two things were done here, and the exact state is worth
  recording precisely:
  - The V8 `Scc8530` replaces the earlier read-only SCC stub, and models
    an **open (peer-less) LocalTalk line**: RR0 carries a standing
    Break/Abort (bit 7), the correct hardware state. This is what moved
    the hang forward from the carrier-sense wait ($A5B28) to the
    transmit wait ($A6540) — the driver reads RR0 in *polled* mode and
    branches on the new value.
  - XPRAM $E0-$E3 = $00/$F1/$00/$0A ("network ≠ LocalTalk", Basilisk II
    `emul_op.cpp:129-144`) in `Egret::factoryDefaults()`.
  - SCC ext/status + Tx-Buffer-Empty interrupts were added and gated
    (`scc_ext_test`) — correct 8530 behaviour — **but the LC II drives
    the SCC purely polled: WR9 master-int-enable is never set, so no SCC
    interrupt is delivered.** The remaining hang ($A6540) is a `.MPP`
    transaction whose completion (`jmp ($634)` → clear the $63E mutex)
    is never reached; it is a Time-Manager-timeout / LocalTalk SDLC
    frame-completion path, not an interrupt. Fully modelling it needs
    LocalTalk SDLC transmit emulation (or Basilisk-style `.MPP` HLE) —
    there is no differential oracle for LocalTalk, so it is scoped as
    O6.11 rather than guessed. See TODO § O6.11.

## 2026-07-15 — Basilisk II knowledge applied: rominfo, XPRAM defaults

- **`tools/rominfo`** (new dev tool, no emulator core): Mac ROM
  introspection with the parsers pinned in the Basilisk II study —
  header + verified checksum, resource map, the compressed A-trap →
  ROM-offset table (`--trap A053` = breakpoint fodder for lcii_trace),
  UniversalInfo records, and the ROM+$94A decoder→low-mem pair table.
  Run on the real LC II ROM it settled two questions (pinned in
  `docs/BASILISK_ROM_NOTES.md` §8): the ROM **does carry a no-FPU SANE**
  (two `PACK 4` resources) — the O6 "error 10 without 68882" polish item
  is a selection problem, not a missing SANE; and the DecoderInfo
  hardware bases confirm the V8 map byte-for-byte (VIA $50F00000,
  pseudo-VIA $50F26000 → $CEC with real-VIA2 decoder[11] empty, SCSI
  triplet, ASC, SWIM).
- **`Egret::factoryDefaults()`**: when no battery file carries the
  system's `'NuMc'` XPRAM signature, seed Basilisk II's known-good
  defaults (DynWait, standard PRAM block, OSDefault=MacOS) instead of
  an all-zero PRAM; $8A intentionally not forced (V8 handles the real
  24-bit startup). Wired into the GUI's PRAM load; verified equivalent
  boot to 800M cycles (overlay, WarmStart, SCSI scan identical).
- `lcii_trace` now logs the **WarmStart `'WLSC'` milestone at $CFC**
  (the ROM's own "low memory is valid" marker, what Basilisk gates all
  host callbacks on) — brackets any fault as before/after low-mem
  validity; fires at ~332M cycles on the boot.vhd path.

## 2026-07-15 — O6: **Mac LC II boots to the Finder desktop**

- **POM68K boots a System disk (`hdv/boot.vhd`, volume "MacPack") all
  the way to the Finder** on the emulated V8 machine: menu bar, an open
  window of folders, desktop icons and Trash, "1.1 GB in disk / 223.3 MB
  available" — the whole classic Mac OS Toolbox running on the O6
  hardware (68030+PMMU, Egret, V8 RAM/video, ASC, SCSI). O6.8 gated by
  `tests/lcii_boot_etalon.cpp` (Finder signature + SCSI command count;
  soft-skips without the ROM/image; adds the $6A DDM entry in memory).
- The user's own disk `hdv/GISTPERSO.vhd` (wrapped bootable) *starts*
  booting — driver, partition map, blessed System Folder, System files
  loaded — but stalls in an exception storm at RAM $13160 (its System
  trips something boot.vhd's doesn't). Tracked as O6.9.
- **Two final gaps closed after the ?-icon stage:**
  1. **SCSI driver load** — the LC II ROM's boot scan ($A07264) only
     loads the driver whose DDM entry `ddType` is **$6A**; a bare-HFS or
     type-$0001-only image is silently rejected and the ROM keeps
     blinking the ?. Added `tools/wrap_hfs.py` to wrap a bare HFS volume
     into an Apple-partitioned disk with a DDM $6A driver entry, and
     modeled the **5380 IRQ latch** (BSR bit 4 sets on a DMA-phase change
     — enterStatus/MsgIn/BusFree; cleared by the RPI register) that the
     SCSI Manager polls to end a blind transfer. The Plus path is
     unchanged (its ROM ignores the latch), all 22 gates stay green.
  2. **68882 default-on** — the target software issues FPU instructions
     and a bare LC II faults with "system error 10" (Line-F). The LC II's
     68882 is a PDS option; POM68K attaches it by default (the O5 core),
     `POM68K_NOFPU` models a bare machine. This was the last thing
     between a rendered system-error dialog and the live Finder.

## 2026-07-15 — O6: the LC II ROM boots to the blinking-? screen

- **Milestone**: the real, unpatched LC II ROM ($35C28F5F) now completes
  its entire ROM phase on the emulated V8 machine — POST, boot chime,
  32-bit switch through the PMMU, low-memory + trap-table setup,
  QuickDraw init — and paints the gray desktop with the mouse cursor
  and the blinking-? boot icon while scanning SCSI (hundreds of
  selections, READ(6) traffic against the attached .vhd).
- **The blocker was Egret command $02 = ReadXPram(count, offset)** — an
  Egret-specific command absent from the Cuda documentation. The ROM's
  `_ReadXPRam` ($A0CC64) loads the boot-mode flag byte from XPRAM $8A
  into low-mem $1EFC; with no reply data, $1EFC kept the $FFFFFFFF
  low-memory fill, its bit 4 selected _InitZone on a diagnostic
  zone-at-zero pblock ($A00518), the zone-header clear wiped exception
  vectors 0-12, and the very next OS trap ($A02D) dispatched through a
  zeroed vector 10 into address 0. Found by walking the chain backwards
  with lcii_trace (vector watch → InitZone pblock → $1EFC → _ReadXPRam
  → the unanswered Egret exchange). Independently confirmed by the
  Basilisk II study (docs/BASILISK_ROM_NOTES.md): Basilisk stubs the
  same service (`_ClkNoMem`) and forces XPRAM $8A itself.
- lcii_trace grew the debugging instruments that made this tractable:
  --ring-from instruction rings with register columns, --probe register
  dumps at a pc, --hot-from tail histograms, automatic MMU table walks
  and root-table dumps on unexpected faults, low-mem watches, PPM
  screenshots, PRAM persistence.

## 2026-07-15 — O6 (LC II machine): first six slices

- **New machine core**: `V8Memory` (address map masked $80FFFFFF, RAM
  config register with the fixed $800000 2 MB alias, overlay cleared by
  any $A00000 read, bus errors on unmapped I/O — the ROM's AddrMapFlags
  probe reproduces the real-hardware $773F), `PseudoVia`, `Cpu030`,
  `Egret` + `AdbBus`, `AscV8`, `V8Video` + `Ariel`, SCSI pseudo-DMA
  windows over the Plus `Ncr5380`, SWIM1 shim over the Plus `Iwm`.
  Six new CTest gates (20 total green).
- **Moira vendored patches driven by the real ROM** (all documented in
  POM68K_VENDOR.md): external `extBusError()` producing exact $A/$B
  frames; RTE of format $A; **prefetch-pipe carry across PMOVE to
  TC/CRP/SRP** — the ROM enables the MMU with `pmove tc; nop; bne;
  jmp (A5)` and those three pipe words must execute under the
  pre-switch mapping (start-of-instruction refetch read RAM garbage and
  sank the boot into the POST debug console); **POLL_IPL at mode-5
  instruction boundaries** — the ROM's TimeDBRA calibration parks in a
  data-access-free `dbra` loop waiting for a VIA1 T2 level-1 interrupt,
  and without per-instruction IPL sampling the interrupt landed after
  the loop timed out, double-storing a result and derailing the table-
  driven dispatch. Neither divergence is visible to single-instruction
  differential fuzzing — machine-level etalons are the only net that
  catches this class.
- **Egret wire protocol decoded from the ROM itself** (no public doc):
  replies are `[sync, status, status, cmdEcho, data…]`, XCVR_SESSION
  drops WITH the last byte, next-byte trigger is the VIA_FULL falling
  edge, PRAM addresses are 16-bit, periodic TIMER packets (first one
  after reset is a 10-byte boot heartbeat, later ticks 3 bytes), and an
  unacknowledged Egret-initiated packet must be retracted or the ROM's
  bus-quiet waits deadlock. Pinned in `src/Egret.cpp` comments +
  `egret_test`.

## 2026-07-15 — O5 follow-ups: 68882 timing + FRESTORE frame acceptance

- **FPU instruction timing** replaces the `CYCLES_68020(6/20)`
  placeholders in `extern/moira/Moira/MoiraExecFPU_cpp.h` with the
  MC68881/MC68882UM Section 8 figures (68882 column), kept in a single
  table/section: Table 8-3 per-opmode totals + per-format spread +
  FMOVE-out row + FMOVECR, Table 8-6 control/FMOVEM (cache case, +2
  MC68882 footnote), Table 8-7 conditionals, Table 8-8 FSAVE/FRESTORE
  per frame. EA cycles reuse the integer `cp` mechanism unchanged
  (`computeEA` accumulates, `CYCLES_68020` adds); 68000/68010 timing
  untouched. **Why**: cycles are advisory in Phase 2 (SST030 `length`
  is not compared) but `emuCycles` drives event ordering — a 570-cycle
  FTWOTOX billed as 20 would skew every interrupt/VBL interleave on the
  future LC II. Two exact timing smokes in `fpu_sanity` (FADD.X 56,
  FMOVECR 32).
- **FSAVE BUSY frames: decided, not implemented** — WinUAE's 6888x
  support never generates them (no mid-instruction save window in its
  coprocessor model) and the oracle is the convergence target, so FSAVE
  stays NULL/IDLE-only (documented in `execFSave` +
  POM68K_VENDOR.md § FPU).
- **FRESTORE accepts every documented frame exactly like WinUAE**
  (`fpuop_restore`, fpp.c:2593-2812): NULL $00, 68881 IDLE $1F18,
  68882 IDLE $1F38 (BIU bit 27 clear re-arms a pending exception),
  BUSY $1FB4/$1FD4 skipped, **and the $41 68040 version-hack frames**
  (fpp.c:2799: active because the oracle runs
  `fpu_no_unimplemented = false` — $41 idle/unimp/busy are accepted or
  skipped, $40 and everything else is a format error, vector 14).
  **Why the hack is replicated**: oracle wins over spec; MC68882UM § 6
  knows nothing of it, but fuzzed $41 frames hit it in WinUAE.
- `oracle/fuzz/gen030.py` plants a well-formed frame image at ~60 % of
  FRESTORE operand addresses (all matrix rows, plausible IDLE
  internals, CU_SAVEPC kept off $FE so WinUAE's unimplemented
  68040-busy resume never triggers). Fresh-seed verify (29/31, n=200,
  fpu × off/identity, WinUAE-solo): **800/800 at first replay**,
  93 FRESTOREs covering the whole matrix. Gates unchanged: ctest
  15/15, sst68030 2 082/2 082, sst68000 1 000 058/1 000 058.
  Still open from the O5 list: FMOVEM indirect-EA read order.
- **Post-retirement repin — gate now 3 082/3 082** (12 corpus files):
  the duo/solo split merged into the standard names (`fpu_off` 1 099,
  `fpu_identity` 732 — duo seeds 1/7 + solo 11/17 + frame-planted
  29/31), and `fpu_tt` grew 11 → **211** with the first full
  FPU-through-translation solo cell (seed 41, 200/200 on first
  replay — the O5 fault-class fixes hold under `tt` too).

## 2026-07-15 — Musashi oracle retired: the loop is WinUAE-solo

- The second oracle (`oracle/musashi/`, MAME m68kmmu on kstenerud
  Musashi 4.60) is **deleted**, along with its build dir and every
  loop/fuzzer reference. **Why**: across all arbitrations D1-D22 it won
  **zero** rulings — WinUAE (+ manual) took every contested call; its
  fault model is architecturally divergent (D8/D9: faulted instructions
  run to completion, zero-filled $A/$B frames) and blind to 68030
  address errors (D11); its 6888x manages ~13 % agreement (D18-D20)
  and cannot testify on FSAVE/FRESTORE, packed decimal, FDBcc/FTRAPcc
  or FMOVEM-fp (D22). Patching it to each ruling cost more than its
  testimony was worth — it was no longer up to standard.
- The differential loop becomes **WinUAE-solo + manual arbitration**:
  solo cells mean "the oracle's word is law"; Moira-vs-oracle disputes
  are settled by MC68030UM / MC68881-882UM readings under the standing
  oracle-wins-over-spec policy (real-hardware traces welcome). Ruling
  appended as `oracle/fuzz/disputes/NOTES.md § Musashi retired`
  (D1-D22 history untouched).
- `loop.sh` now builds one oracle and fuzzes a single grid
  (`{core,mmu,random,fpu} × {off,identity,tt}` + `fault ×
  {identity,tt}`) into the standard `${family}_${mmu}.json` names; the
  duo/solo corpus split (`fpusolo_*`) merges away on the next repin.
  `fuzz030.py` keeps the generic `--b` slot for a future second oracle
  but drops `ruled_for_a` (standing-ruling auto-arbitration only made
  sense against a known-deficient B). Pinned corpora untouched: ctest
  15/15, sst68030 2 082/2 082, sst68000 1 000 058/1 000 058.

## 2026-07-15 — O5 slice 2: 68882 FPU execution in Moira

- **Moira executes the full MC68882 instruction set** (the LC II PDS
  FPU), backed by a new vendored `extern/softfloat/` (SoftFloat-2a +
  Previous/WinUAE FPSP transcendentals, copied from the oracle vendor
  tree — GPLv2+, linked as a separate static lib so Moira stays MIT).
  **Why this softfloat**: it is the exact arithmetic the primary oracle
  (WinUAE) runs, and the oracle wins disputes by project policy — so
  numerical convergence with the differential corpus holds by
  construction. Semantics ported from WinUAE `fpp.c`/`fpp_softfloat.c`
  6888x branches with line citations (constant-ROM garbage entries,
  alias opmodes, packed-decimal k-factor quirks, the FPIAR
  only-when-enabled economy, the 68882 NULL-frame word `$00380000`, the
  68030-build FSAVE that *skips* the internal longs — all oracle-exact
  on purpose).
- Attach/detach follows the CPU-model mechanism: `setFPUModel()`
  rebuilds the jump table; `FPUModel::NONE` (default) keeps it
  byte-identical to stock Moira. **Nothing moved on the gates**:
  sst68030 1 040/1 040 (FPU-less corpus), sst68000
  1 000 058/1 000 058, Mac Plus etalons green.
- `tests/sst68030.cpp` learned the SST030 FP fields (`fp0`-`fp7` as
  3×u32 raw words — fixed contract with the fuzzer — plus
  `fpcr/fpsr/fpiar`) and attaches the 68882 per vector. New gate
  `fpu_sanity` (hand-computed FMOVECR pi, 2+2, 1/0=+inf DZ,
  sqrt(-1)=NaN OPERR, FCMP/FScc orderings, FMOVEM raw image, packed
  1.0, FPCR roundtrip, detached F-line) — ctest now 15/15. The first
  parallel-generated FPU corpus replays 41/41.
- Known-incomplete for the loop (documented in POM68K_VENDOR.md § FPU):
  FSAVE BUSY frames, FPU timing, FMOVEM indirect-EA read order.
- **Same-day solo-corpus convergence** (WinUAE-solo FPU corpus, 700
  vectors, 617→700 after three fixes; duo corpora 41/41 + 90/90 and all
  hard gates unchanged): (1) state-restore convention — loading FPU
  state through the setters leaves the FPU non-null, so a subsequent
  FSAVE emits the IDLE frame like the oracle glue's forced
  `fpu_state = 1`; (2) ruling **D21** — FRESTORE bad-frame format error
  (vector 14) stacks the PC past all consumed words (WinUAE
  `m68k_getpc()`), not Moira's generic `reg.pc - 2`; (3)
  post-instruction FP traps stack the **format $3** floating-point
  frame (next PC + operand fp_ea, newcpu_common.c:1616) instead of a
  format $0 stub.
- **Fresh-seed re-verify (seeds 17/19) closed two FPU-through-MMU fault
  classes**: FMOVEM FP-block transfers are *unlogged* with integer-MOVEM
  bookkeeping (MOVEM1/FMOVEM flags, state[0] long counter, fmovem_store
  in the $B frame's padding slots — WinUAE uses the non-state accessors
  there, fpp.c:2810/2875), and FPU (An)± operands arm *plain* mmufixups
  (register restored on a fault, status byte 0 — bit 7 of
  `mmuFixupReg[]`). Solo seeds 11/17/19/23: 700/700, 211/211, 100/100,
  100/100; full ctest 15/15.
- **O5 closed — gate re-pinned at 2 082/2 082** (14 corpus files): duo
  `fpu_off` 99 (seeds 1/7), `fpu_identity` 32 (1/7), `fpu_tt` 11 (13),
  plus WinUAE-solo `fpusolo_off` 600 / `fpusolo_identity` 300 (seeds
  11/17) under **ruling D22** — the slice-1 solo proposals
  (FSAVE/FRESTORE frames, packed decimal, FDBcc/FTRAPcc, FMOVEM-fp)
  promoted to a pinned solo class, same status as the D9 fault family.
  **Why solo**: Musashi's 6888x cannot testify on those classes (~13 %
  duo agreement, D18-D20); every future Musashi convergence patch grows
  the duo side and shrinks the solo residue. `loop.sh` now fuzzes fpu
  in the duo grid (off/identity/tt) + solo cells; the pending dir is
  retired.

## 2026-07-15 — O4 slice 4: integer-family arbitration (O4 complete)

- The last integer disagreements between the two 68030 oracles were
  swept (3 000 duo states → 88 disputes), categorized by opcode, probed
  case-by-case, and arbitrated: **WinUAE won every ruling** (D11-D17 +
  the D6-remainder, `oracle/fuzz/disputes/NOTES.md` § slice 4) — its
  undefined-flag tables in `newcpu_common.c` are hardware-verified.
  Musashi was patched to the rulings (12 BCD bodies, DIV/CHK/CHK2
  tables, PACK/UNPK byte order, reserved I/IS=100, F-line priv windows,
  format-$2 next-PC, `oracle/musashi/VENDOR.md`); Moira's 020 paths now
  route to its existing UAE-derived helpers instead of the SST-68000
  rules — gated `if constexpr (C68020)` like D10, so the 68000 core is
  bit-identical (sst68000 still 1 000 058/1 000 058).
- **Why two non-obvious fixes existed at all**: Moira's imported copy
  of WinUAE's CHK table had silently dropped `SET_NFLG(dst < 0)` —
  only fresh-seed replay caught it; and Musashi needed **`-fwrapv`**
  because the (deliberately overflowing) CHK bound-subtractions were
  being folded by GCC into `bound < val` while the WinUAE oracle
  already built with `-fwrapv` — same C code, different .so truth.
- **D11 — the Musashi address-error gap became a standing ruling**:
  odd control-flow targets raise vector 3 with a real format $B frame
  on a 68030 (SSW $0066, per-instruction stacked-PC conventions,
  access log in the internal words — all probed); Musashi cannot model
  it, so `fuzz030.py` now auto-arbitrates that signature to WinUAE
  (`ruled_for_a`, D9 precedent) and the vectors enter the corpora.
  Moira replays them byte-for-byte: odd-target checks in nine handlers
  + `execAddressError030`, with WinUAE's quirks kept (BSR decrements A7
  without writing, DBcc faults even on an expired counter, JSR defers
  the fault to the next fetch). The M68030 read/write funnel is now
  taken with TC.E off too — WinUAE's `_state` accessors always log,
  and those logs are what the $B frames stack.
- The Musashi 68881 is **disabled on the 030** (FPU-less LC II, like
  the WinUAE `fpu_model = 0` build): the whole $F2xx/$F3xx space is
  duo-agreed as Line-F/priv until the O5 FPU slice re-enables both
  sides; `move16` and 68040-PFLUSH no longer leak onto the 030.
- Result: **the `--mmu off` grid is fully converged** (core/mmu/random
  100 % over fresh seeds; random was ≈94 % before the slice);
  identity/tt at 96-100 % with the residue fully explained (D8/D9
  Musashi fault-model gap, covered by the WinUAE-solo fault corpora).
  Moira replays 100 % of every agreed vector (2 672/2 672 grid +
  2 000/2 000 sweep). Gates re-pinned: `random_off.json` 250,
  `random_identity.json` 121 (fresh seeds 81/91, D11 vectors included)
  → `ctest -R sst68030` = **1 040/1 040**; full ctest 14/14. **O4 is
  complete** — next: O5 (FPU) per TODO § Phase 2.

## 2026-07-15 — O4 slice 3: the 68030 MMU bus layer (Moira translates)

- Moira now translates **every bus access** when `Model::M68030` and
  TC.E=1 (MC68030UM § 9.5, modeled on the primary oracle WinUAE
  `cpummu030.c`): transparent-translation match, 22-entry ATC with
  pseudo-LRU replacement and the *write-to-unmodified-page invalidates*
  rule, a full table search (FCL, short/long descriptors, limits, early
  termination, indirection, U/M history writes), and 68030 bus
  splitting for unaligned accesses — each sub-access translated
  separately, so a long can straddle a good and a bad page and commit
  its first half. PFLUSH/PFLUSHA/PLOAD/PMOVE-with-FD-clear now flush a
  REAL ATC; PTEST level 0 searches it. Hook cost outside the LC II
  path: `if constexpr` — the 68000/68010 template instantiations are
  bit-identical in behaviour (sst68000 still 1 000 058/1 000 058
  cycle-exact, all Mac Plus boot etalons green).
- Translation faults raise vector-2 bus errors with **byte-for-byte
  WinUAE format $A/$B frames** (the fuzzer compares raw RAM): $A on a
  fault at the instruction's last write (next-instruction PC, updated
  CCR, (An)± kept), $B otherwise (instruction PC, CCR + pending-fixup
  restore, access-value log, wb2/wb3 fixup encodings, MOVEM counter,
  SUBACCESS flags, disp-store words; prefetch-phase faults flag bit 31
  of the pipeline-status long). Double fault (odd vector 2 / fault
  while stacking) → HALT.
- **Why the fault corpus is WinUAE-solo (ruling D9)**: Musashi runs
  faulted instructions to completion and pushes zero-filled frames —
  architecturally incapable without a rewrite, so every fault vector
  self-quarantines in the duo differ. Majority rule (WinUAE + manual):
  the new `--family fault` corpora (`fault_{identity,tt}.json`, aimed
  at invalid/WP/remapped pages, unaligned page-straddles, MOVEM, MOVES,
  TAS/CAS locked-RMW) are generated from WinUAE alone. Two oracle
  determinism bugs were fixed on the way (VENDOR patches 5-6): the
  setjmp-clobbered CCR capture (`volatile`) and stale restart globals
  leaking history into frames (`oracle_set_state` zeroes them).
- Also arbitrated: **D10** — the SST-68000 "ASR past width clears C/X"
  rule is 68000/68010-only (both 68030 oracles keep the sign as C/X);
  MOVES joined the `core` fuzz family (SFC/DFC-driven translation).
- Gates: `ctest -R sst68030` = **875/875** across 9 corpora (mmu-off
  520, duo-agreed identity/tt 250, fault-solo 105); the harness replays
  translation-enabled vectors first-class (`--skip-translation` kept
  for debugging). Scratch sweep: 2 342/2 342 duo-agreed vectors across
  3 families × identity/tt × 3 seeds. Full ctest: 14/14.

## 2026-07-15 — Phase 2 live: two 68030 oracles + arbitration turn 1

- The differential loop of TODO § Phase 2 exists end-to-end: WinUAE
  (Hatari e77819f7, `oracle/uae/`) and MAME-Musashi (m68kmmu 0.276 on
  kstenerud 4.60, `oracle/musashi/`) behind one C ABI
  (`oracle/oracle_api.h`), fuzzed by `oracle/fuzz/` with real MMU
  translation trees in RAM, exchanged as SST030 JSON, replayed against
  Moira by `tests/sst68030` (gate 14). `oracle/fuzz/loop.sh` = one turn.
- Arbitration turn 1 (D1-D5 + bonuses): **WinUAE won every ruling** —
  MMU ops are privileged with the S-check *before* the extension fetch;
  PMOVE MMUSR,Dn is an invalid EA → Line-F (the D2 replace-vs-merge
  question was unreachable on real hardware); long-indirect descriptors
  read the second long at +4 (Musashi also had an unmasked-shift bug that
  killed indirection except at TID); DT=0 walks keep accumulated MMUSR
  bits; the vector-56 frame is format $0 with next-PC; MOVEM list,-(An)
  with the base register in the list stores initial−size (020+ PRM).
  Musashi oracle and Moira both fixed; the losing behaviours are
  catalogued in `oracle/musashi/VENDOR.md` and
  `extern/moira/POM68K_VENDOR.md`, rulings in
  `oracle/fuzz/disputes/NOTES.md § Arbitrated`.
- State: oracle agreement 200/200 (core and mmu families, MMU off);
  Moira replays 100 % of every duo-agreed corpus (520 gated vectors +
  fresh-seed 300/300 spot-check). Translation-enabled vectors stay
  mmu-skipped until the bus/ATC slice.

## 2026-07-15 — O4 slice 1: Moira executes the 68030 MMU instructions

- PMOVE/PTEST/PFLUSH/PFLUSHA/PLOAD execute for real (Model::M68030):
  crp/srp/tc/tt0/tt1/mmusr live in `Registers` with getVBR-style
  accessors, PMOVE moves all of them (TC validation → vector-56 MMU
  configuration error, format-2 frame), PTEST performs the § 9.5.3
  translation-table walk into MMUSR (+ descriptor address → An), PLOAD
  walks with U/M history writes to RAM, PFLUSH* are no-ops (no ATC yet).
  Verified by the O3 differential loop against the Musashi oracle:
  100 % on `family=mmu --mmu off` (2 900+ vectors, 8 seeds); gate
  corpus `tests/data/sst68030/mmu_off.json` (200 vectors) wired into
  `ctest -R sst68030`, which now loads/compares the MMU registers and
  only skips vectors whose *initial* TC has E set (bus translation is
  the next slice). Harness parser fix: `"length":-1` (oracle_step < 0
  in translation-enabled corpora) used to livelock the hand-rolled JSON
  scanner; it now accepts negative literals.
- The non-obvious part: the oracle contradicts MC68030UM in several
  places and, per the project rule, wins — most notably **Musashi never
  privilege-checks MMU instructions** (user-mode PMOVE executes; ~30 %
  of the corpus), MMUSR→Dn replaces the whole register, and an
  invalid-descriptor walk erases previously accumulated MMUSR bits. All
  logged as D1-D7 in `oracle/fuzz/disputes/NOTES.md` for re-arbitration
  when the WinUAE oracle lands. Vendor catalogue updated
  (`extern/moira/POM68K_VENDOR.md` § 68030 MMU-instruction convergence).

## 2026-07-15 — M6: the startup chime plays

- Sound: `MacAudio` pulls the 370-sample/frame PWM buffer (ramTop−$300,
  even byte = 8-bit sample, PA3 buffer-select, PB7 enable, PA2-0 volume);
  `MacAudioHost` plays it through miniaudio on a lock-free ring at
  22254 Hz. The turbo-vs-audio pitch problem is solved by pushing only
  non-silent frames — the ring drains while the machine races through the
  silent RAM test, so the chime and beeps keep the right pitch.
- The Mac Plus power-on chime — a ~601 Hz (≈D5) tone held ~0.7 s then
  muted — is audible. Verified headless by capturing it to a WAV and
  checking the fundamental + decay (`sound_test`).

## 2026-07-15 — M7: System 6 boots from a SCSI hard disk

- NCR 5380 controller + SCSI-1 target boot System 6 from HD20SC.vhd; the
  HD20SC volume mounts on the Finder desktop.
- The day-long blocker was NOT the controller (proven correct in isolation
  by ncr5380_test from the start) but the ROM never running its SCSI scan.
  Diagnosis chain: PRAM (ruled out — Plus ignores the default-boot-device,
  that's a 256K-ROM feature) → floppy presence (ruled out) → drive queue
  had only the floppy → SCSI Manager select/read primitives never executed
  → the gate is `HWCfgFlags` ($0B22) bit 7, set by `E_SoftReset`'s
  $420000-vs-$440000 ROM-mirror probe. We mirrored the ROM across the whole
  window so the probe saw no difference and declared "no SCSI". Fixed by
  returning address-dependent open bus above the true 128 KB ROM. (Nailed
  via the bit-exact Plus v3 ROM disassembly, jonathanschilling/mac_rom.)
- Second blocker: WRITE(6/10) is mandatory — the driver writes to the
  volume during mount; a read-only target hung the boot in a VIA interrupt
  storm right after the driver loaded. Added a DATA OUT phase to the
  controller and in-memory writes to the target.
- Also: GUI windows move only from the title bar (Finder drag-and-drop no
  longer drags the host window); floppy/SCSI paths resolve relative to the
  executable; SCSI disk auto-attaches from hdv/.

## 2026-07-14 — M5.5: the Finder is drivable (keyboard + mouse)

- Minimal SCC Z8530 (DCD ext/status interrupts, RR2B modified vector —
  the ROM's actual dispatch mechanism; it never reads RR3), quadrature
  mouse with the exact polarity table, M0110A keyboard with the
  two-SR-interrupt transaction (~3 ms per phase).
- The vicious one: with keyboard AND mouse alive, a naive IPL OR yields
  level 3, whose ROM vector is a bare RTE → instant livelock. The real
  glue disconnects the VIA /IPL0 while the SCC interrupts (GttMFH); the
  suppression formula fixed it in one line. Diagnosed by single-stepping
  at storm onset: the "handler" was just `rte` + the interrupted
  instruction alternating.
- Gate `input_etalon` verifies against System 6's own understanding:
  RawMouse deltas (±2 for inherent quadrature reversal loss), MBState,
  KeyMap bits. Bonus verification: headless click on the File menu —
  it drops with all items rendered.
- GUI: mouse captured over the Mac screen (2x scale compensated),
  55-key M0110 map, Cmd = Super, Option = Alt.

## 2026-07-14 — M5: System 6.0.5 boots to the Finder from floppy

- IWM + Sony 800K drive from the cross-verified research spec (MAME, pce,
  Snow). GCR checksum ported verbatim from MAME and cross-validated
  against pce's independent formulation before use.
- Three bugs found by tracing the ROM's Sony driver instruction by
  instruction (`sony_trace`, new dev tool):
  1. TACH must run on motor time, not data position — the ROM times
     spindle speed against VIA T2 before ever reading data.
  2. The IWM data register clears ~14 clocks AFTER a read, not
     immediately — the ROM's `tst.b`/`move.b` pairs read it twice.
  3. Boot blocks need the bbVersion word ($4418) at +6 and a BRA at
     bbEntry (+2); 'LK' alone is not enough.
- Verification chain, cheapest first: gcr_test (encoder roundtrip vs an
  independently-ported decoder) → disk_boot_etalon (synthetic boot block
  executes our 68000 code through the whole floppy path, no Apple bits
  needed) → system_boot_etalon (real System 6.0.5 to the Finder desktop,
  2.7 s headless).
- GUI probes disks35/ for a floppy image; boot_trace grew --disk.

## 2026-07-14 — M4.5: SingleStepTests/680x0 — 1 000 058 / 1 000 060

- `sst68000` harness (POM2/POMIIGS JSON-scanner pattern) runs the full
  1M-vector corpus in 3.7 s; `--dump` prints complete state diffs (the
  workhorse of the convergence loop); `--only/--skip/--max` for triage.
- Starting point was 81.1% (all non-address-error vectors already passed —
  Moira's core semantics were sound). The 19% gap was almost entirely the
  fine detail of 68000 address-error behavior: exception idle cycles,
  stacked-PC values per instruction class, An update ordering around
  faults, frame FC/IN bits, and MOVE's interim flags. Full patch catalogue
  in `extern/moira/POM68K_VENDOR.md § convergence`.
- Notable finds: branches/jumps/returns all stack PC = target−4 with the
  I/N bit set; BSR pushes its return address before faulting but JSR does
  not; ADDX/SUBX leave a faulting -(An).l at init−2 while plain reads keep
  the full −4; DIV/CHK undefined flags follow the preserve rules verified
  empirically by a research agent against the whole corpus (0 mismatches).
- Two ASL.b vectors are upstream-documented bad data (SST issue #4 — our
  computed values match the proposed corrections); skipped with citation.
- Two SST rules conflict with hardware literature (DIVS late-overflow
  timing vs ijor's paper; ASR over-shift C/X) — applied per the
  oracle-wins rule and flagged for oracle #2 in the LC II phase.
- Methodology note: this was the phase-2 oracle loop run manually — dump
  divergence, derive the rule from the corpus (python analysis over the
  JSON), patch, re-run. The 68030 phase will automate exactly this.

## 2026-07-14 — M4 complete: cycle-accurate boot hardware

- VIA timers T1/T2 count at φ2 = CPU/10, driven from `Cpu68k::sync()` via a
  peripheral catch-up (`lastPeriphClock_`) so contention wait states tick
  them too. One-shot/free-run semantics; ±1-cycle 6522 reload latency
  deferred (TODO M4.1).
- RTC 343-0042 bit-banged serial protocol (PB2 /enable, PB1 clock, PB0
  data), 20-byte PRAM, write-protect register, seconds counter on the CA2
  one-second tick. **Fixed the "2 MB" mystery**: with a working RTC the ROM
  now stores MemTop = $400000 — the earlier $1FFBE0 SSP was a mid-boot
  stack, not a sizing result.
- RAM/video contention (`Cpu68k::contentionDelay`): video owns alternate
  4-cycle slots during the 512 visible dots of lines 0-341, sound/PWM fetch
  steals the last 4 cycles of every line. The delay iterates across
  adjacent busy slots (a wait can land in the next line's video slot — the
  first version under-counted by 4 cycles × 341 lines). Gate reproduces
  GttMFH Table 5-3: 2.56 MB/s average RAM bandwidth, 21 246 accesses/frame.
- cpu_smoke hardened: with contention the 1M-cycle run stops mid-repaint,
  so the diagonal invariant is checked on 4 row pairs (3 must hold) instead
  of assuming a completed frame.
- `MacFrame.h`: shared frame clock — VBL (CA1) at line 342 = cycle 120 384,
  one-second every 60 frames; GUI, boot_trace, etalon all agree on phase.
- New gates: `contention_test` (budget math), `rom_boot_etalon` (real ROM →
  gray desktop 50% ± icon patch white ratio; soft-skips without a ROM).
- GUI: Turbo ×8 checkbox (default on with a real ROM — the 4 MB RAM test
  takes 45 s of machine time).

## 2026-07-14 — M0–M3.5 + first real-ROM boot

- Project scaffolded on the POMIIGS blueprint; Moira vendored from NeoST
  (`extern/moira/POM68K_VENDOR.md`).
- Built-in demo ROM gate: caught that the 6522 port A reads inputs with
  pull-ups (`ora | ~ddra`) — code must set DDRA before ORA to clear the
  overlay, exactly like the real ROM (DDRA=$7F, then ORA with PA4=0).
- Web-research report (MAME/pce/Mini vMac/GttMFH cross-checked) pinned in
  `DEV.md`; fixed SCC read/write split ($8/$9 even vs $A/$B odd), IWM range
  $C-$D with stub reads of `$1F` (required to reach the blinking-?), VIA PA6
  screen-buffer select.
- Level-sensitive IPL: recomputed after **every** VIA access (reads clear
  IFR flags too) — without this the first serviced VBL re-interrupts forever.
- **Mac Plus ROM v3 boots to the blinking-? floppy icon**: RAM test runs
  ~45 s of machine time on 4 MB (real hardware does the same — don't
  mistake the `movem.l`/`eor` loop at `$400E82` for a hang), then gray
  desktop + mouse pointer + ?-icon; VBL IRQ drives the blink counter wait
  at `$402420`. No VIA timers or RTC needed to get here, confirming the
  BMOW Plus Too minimal-hardware list.
