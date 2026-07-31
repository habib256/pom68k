# LLE vs HLE — inventory and migration plan

**Purpose.** POM68K's direction is: harden the **LLE** core first (hardware
modeled at register/protocol level from silicon references, verified by
gates and oracles), and only later layer an **opt-in, clearly-flagged HLE
accelerator** on top (`HLE_OVERLAY.md`). That requires knowing exactly
where the current code already deviates from hardware. This document is
the complete inventory and the plan to shrink it. Current as of
2026-07-31 (**seventh pass** — save states across the whole tree, the
JIT re-measure, and one genuine ADB divergence found: `AdbLine`'s
keyboard register 2 is hardcoded where MAME keeps a live modifier
bitmap — see § 3 ADB and TODO. **32 machine profiles, 129 gates**).
Earlier: 2026-07-29 (sixth pass — SCC line-state / factory-MCU /
test-depth round);
2026-07-25 (fifth pass — RBV / Tinker Bell / 68030-Mac II);
2026-07-24 (fourth pass — the **Phase C** machine fan-out);
2026-07-22 (third pass against the live tree); 2026-07-21
MAME (`refs/mame-apple`, `refs/mame`) + DingusPPC (`refs/dingusppc`)
cross-check → §3 gaps and migration steps 7–10.

> **Sixth-pass headline (2026-07-29): the hack list is empty on every
> default path, and every machine runs its EXACT factory MCU part.** Three
> things closed this round. (a) **§1.10 is resolved**: the standing
> no-peer SDLC abort became a genuine line state — a virgin line that has
> never carried a frame reads clean (FM0 gives the DPLL no edge), which
> is what Open Transport's `.MPP` bind waits for — and
> `POM68K_SCC_CLEANLINE`, the last env that let machine configuration
> decide a wire condition, is deleted from all eight memory classes.
> (b) **The last two substitute firmwares fell**: the Color Classic's
> "341S0417 wedges the M68hc05" was never a core bug but a *missing
> device* — the CC carries a DFAC2 on the Cuda's I2C at address `$6F`
> (`maclc.cpp:505`) and the 2.35 firmware requires its ACK; with a
> minimal I2C slave (`CudaLle::setI2cDfac`) the factory part boots, and
> the Mac TV's factory 341S0789 landed the same day. (c) **The HLE
> fallbacks are now LOUD** — see the retirement policy in §2: they stay
> (dumps are user-provided and non-distributable) but every entry
> announces itself as a non-conformant substitute, which is what the
> §Principle rule actually demands. New pure-LLE facts in §4: the Cuda
> I2C bus and its per-machine DFAC2 population, and the RBV
> physical-vs-logical low-memory split.

> **Phase C headline (2026-07-24): every machine now boots on a
> firmware-LLE MCU by default.** The LC II Egret flavor flipped from
> opt-in to DEFAULT (migration step 10 / TODO step 6 CLOSED — the
> instruction-slaved ADB wire `CudaLle::mcu_.onCycles` fixed the
> autopoll-load desync that had starved the mouse), and the eight new
> Phase C machines each ride a real 68HC05 MCU image: the **LC** (020),
> **Classic II** (Eagle) and **Color Classic** (Spice, Cuda 341S0788),
> the **LC III / LC III+** (Sonora, Egret 341S0851), the all-in-one
> **LC 520 / LC 550 / Color Classic II** (Sonora, Cuda 341S0060 — 2.40,
> the factory AIO part) and the **Mac IIvx / IIvi** (VASP, Egret
> 341S0851). The reused subsystems (V8/Sonora/VASP video, `AscSonora`,
> SWIM1/SWIM2) inherit their §3 LLE-simplified classification unchanged;
> the new pure-LLE fidelity fact is the **unmapped-I/O-reads-as-0**
> MAME-parity rule on the Sonora and PrimeTime maps (§4).

> **Fifth-pass headline (2026-07-25): the *second* firmware-LLE MCU family
> spread.** The **PIC1654S ADB modem** — until now a Mac II curiosity — is
> the default transceiver on three families: Mac II / **IIx** / **IIcx**,
> the **Mac IIci** (`RbvMemory` `iici`) and the **Centris/Quadra 610/650**
> (`CentrisMemory`). Together with the Egret/Cuda 68HC05 machines that means
> **every ADB machine POM68K ships runs real MCU firmware by default**, and
> the HLE `AdbVia` byte-model / `AdbBus` are no-dump fallbacks on every
> path. Two new pure-LLE fidelity facts came out of the round: the **030
> PMMU must not double-translate against the GLUE 24-bit remap** (skip
> `MacIIMemory::physAddr` when TC bit 31 is set — the
> 020-HMMU-vs-030-PMMU split, §4), and the **i-cache throughput overlay is
> not free** — it used to accelerate BUS time too, which starved the ROM's
> host-paced Egret transport (IIsi wedge, LC III/IIvx black screen). Bus
> time is now charged in machine cycles, both workarounds (`RbvCpu` boost 1,
> `POM68K_Q605_CACHE_BOOST` 1) are retired, and the PIC1654S co-step no
> longer runs on the boosted clock either (§3 CPUs).

Line numbers are indicative — verify with grep before relying on them.

> ## Principle — a clean LLE **before** the HLE boost
>
> **Order matters, and it is not negotiable: the LLE core must be correct
> and complete first; the HLE "boost" accelerator (`HLE_OVERLAY.md`) is
> layered on top only afterwards.** The boost is an *optimization/shortcut*
> mode — it trades conformance for speed or convenience — and it is only
> ever meaningful, testable, and safe when there is a faithful,
> gate-verified LLE reference underneath to (a) define correct behaviour,
> (b) fall back to when the shortcut does not apply, and (c) diff against
> to prove the shortcut is equivalent where it claims to be.
>
> Building the boost first inverts the dependency: shortcuts calcify into
> the only implementation, "correct" becomes whatever one System image
> happened to need, and every new image is a guess (exactly the trap the
> **HLE-hack** class below documents). So:
>
> - Finish the LLE for a subsystem (real silicon/firmware, gated) **before**
>   adding any HLE boost path for it. The Mac II ADB PIC1654S work
>   (2026-07-22, §2 / step 11) is the template: the real transceiver is
>   now the **default** whenever `roms/adbmodem/342s0440-b.bin` is
>   present; HLE remains only as the no-dump / `POM68K_ADB_LLE=0`
>   fallback — never as the substitute for a working LLE.
> - Every HLE shortcut ships **behind a visible non-conformant flag** and
>   with the LLE path still present and default.
> - A boost is accepted only once it is shown equivalent to the LLE
>   reference on the gates; where it diverges, that divergence is the
>   flag's whole point and must be documented, not hidden.

## Classification

| Class | Meaning | Long-term policy |
|---|---|---|
| **Pure LLE** | Registers/protocol/timing modeled from MAME/datasheets/ROM traces | Keep; extend accuracy |
| **LLE simplified** | Functional, not cycle-exact (whole-frame video, batched ticks) | Acceptable; gaps tracked in TODO |
| **HLE replacement** | A whole device replaced at command/protocol level (no firmware) | Acceptable medium-term; document + gate |
| **HLE hack** | Emulator reaches *into the guest* — patches ROM/RAM, injects events, watchdogs guest state | **Eliminate**, or move behind the future HLE overlay with a visible non-conformant flag |
| **Host convenience** | No guest-visible effect (media formats, rendering, audio host) | Keep |

The dangerous category is **HLE hack**: each one encodes an assumption
about a specific System version's memory layout and can silently break
another image — the opposite of the oracle-gated discipline used on the
CPU side.

## 1. HLE hacks (guest-state interventions) — the elimination list

Ordered by severity (guest-visibility × fragility).

### 1.1 Mac II ROM patched at load — **RESOLVED 2026-07-21**

Three code patches used to be applied to the 256 KB ROM at `loadRom`
(forced StartBoot `wantType`, retargeted boot-drive matcher, `$B0E`
`btst` bypass + checksum repair). Root cause was two wire-level bugs in
the `Rtc` model (inverted /enable polarity at the Mac II call site, and
a one-edge-early read bit phase → every read byte = `(v<<1)|1`), which
made the ROM see virgin PRAM on every boot. `Rtc` now implements the
MAME macrtc semantics (falling-edge shift, 256-byte extended XPRAM
protocol, unified classic mapping) and the **unmodified ROM boots SCSI
by itself** (CHANGELOG 2026-07-21 "LLE step 1"). This was the model
outcome for the whole list: the hack pile existed only because one LLE
device was subtly wrong.

### 1.2 EvQ synthetic Return keypresses — **RESOLVED 2026-07-21**

`postKeyReturn`/`maybeDismissBootAlerts` are deleted. The ADB path
proved able to deliver keystrokes during the modals (the ST=EVEN wedge
is covered by `AdbVia::tick`'s dead-timer re-arm), so the Sys 7 alert
dismissal moved into the tests as real host-side ADB Return presses —
what a user would do (CHANGELOG 2026-07-21 "LLE step 4").

### 1.3 SPConfig / AppleTalk clamp, re-applied every tick — **RESOLVED 2026-07-21**

The tick-time clamps in `Q605Memory` / `V8Memory` / `MacIIMemory` are
deleted; only the reset-time `factoryDefaults` seed remains (§1.7 —
factory PRAM contents, hardware-plausible). The guest may now turn
AppleTalk on (Chooser or on-disk prefs — the Infinite Mac OS 8.1 image
does exactly that) and the LLE SCC no-peer path handles it; the Q605
etalon needed a real-Finder early-exit and a bigger cycle budget to
absorb the LAP timeouts (CHANGELOG 2026-07-21 "LLE step 2").

### 1.4 LocalTalk LAP watchdogs — **RESOLVED 2026-07-21**

Both watchdogs are deleted. The wedge was three SCC LLE gaps (RR15
reading 0, standing abort presented in async modes, and no RR0 bit 4
Sync/Hunt — the LLAP carrier sense). With those fixed plus the Tx
Underrun/EOM latch, the LAP transmits its real ENQ probes and times out
on its own (CHANGELOG 2026-07-21 "LLE step 3").

### 1.5 Resource patch-on-load (`ltlk` stub) — **RESOLVED 2026-07-21**

`RsrcPatcher.*` turned out to be dead code (never compiled — absent from
CMakeLists, and its `rsrc_patch_test` gate never existed); the files are
removed. LocalTalk-active boots now ride the real SCC path (see 1.4).

### 1.6 Quadra UniversalInfo FPU-bit masking — **RESOLVED 2026-07-21**

The read-mask + low-mem scrub machinery is deleted: it had been
unreachable since the soft-FPU path landed, and the ROM's own fnop
probe handles no-FPU detection unaided (HWCfg self-clears to `$EC00`).
`POM68K_Q605_NOFPU=1` (68LC040 + soft 68882 — a SoftwareFPU-equivalent
FPU-model choice, not a guest-state intervention) is the supported
no-FPU config. TRUE bare `FPUModel::NONE` (`POM68K_Q605_NOFPU=2`)
**boots to the Finder since 2026-07-21** (gate
`q605_barefpu_boot_etalon`): the `_FP68K` FPU-flavored binding turned
out to be a Cuda HLE reply-framing bug — Mac OS 8.1's own Cuda reader
took our READ_XPRAM command echo for the XPRAM `$AE` ROM-resource
combo (CHANGELOG "Bare no-FPU solved").

### 1.6b Cuda reply framing serves per-reader accommodations — **RESOLVED 2026-07-22**

The wire-model redo landed (CHANGELOG "LLE step 7"): replies are the
real `[type, flags, cmdEcho, data…]`, errors `[$02, err, pktType,
cmd]`, and the **attention byte is a wire event outside the packet
buffer** (dummy SHIFT, stale SR) on DingusPPC's measured schedule
(close-ack +61 µs, attention +30 µs, command ack +71 µs, response
byte +88 µs, TREQ +13 µs). That separation is what made the hacks
unnecessary: the ROM device-manager ISR counts the close-ack as its
discarded sync (4 header bytes), the direct pollers and the Mac OS 8.1
System reader consume it in their send ritual (3 header bytes) — every
reader lands on its data naturally. The `$76` echo-pop, the GetPram
erase, the Q8.2 echo-slot duplication and the `firstTick_` long/short
heuristic are deleted; one-second packets obey the real `$1B` command
(captured from the LC II ROM, `$1B 00`, and Sys 7.5 / OS 8.1,
`$1B 03`). Bonus fidelity from the same pass: BYTEACK edges are
session-gated (the `ori #$30` close no longer injects a duplicate
final command byte — WriteXPram was writing one extra adjacent byte),
$02/$08 decode as READ/WRITE_MCU_MEM with 16-bit addressing (PRAM at
$0100-$01FF, `mcuRam_` scratch below — the System's $B3 parameter
block round-trips instead of corrupting PRAM), PRAM reads are
genuinely open-ended streams, and the Quadra's seconds heartbeat runs
at the real 25 MHz rate (was 1.6× fast; `q605_barefpu_boot_etalon`
budget re-pinned accordingly). The Egret flavor keeps its pinned LC II
wire mechanics (buffer byte 0 = attention) over the same real-framed
header.

### 1.7 Mac II / LC II / Quadra RTC + Egret PRAM factory seeding — `Rtc.cpp:15-44`, `Egret.cpp:56-97`

Reset-time seed only (no tick-time rewrite). **Borderline acceptable**:
factory PRAM is hardware-plausible and prevents the ROM's cold-PRAM
re-init loop. Keep, but treat the *contents* as documented policy:

- **`Rtc::factoryDefaults`** (Mac II): full Basilisk block only when
  `'NuMc'` is absent; **always** (re)seeds SPConfig XPRAM `$13` to `$22`
  (both ports async). `POM68K_APPLETALK=1` → `$21` (LocalTalk active)
  for headless LLAP tests.
- **`Egret::factoryDefaults`** (LC II / Cuda on Q605): more aggressive —
  even with `'NuMc'` present it rewrites DynWait, the classic PRAM
  block, SPConfig (same `$22`/`$21` policy), and OSDefault; cold / bit7-
  clear video sPRAM seeds `$58=$83` (8 bpp) so first boots match a
  Monitors+Restart color Mac rather than the ROM's B&W `$80`.

Do not grow the always-rewrite set without a gate that proves the ROM
path alone is insufficient.

### 1.8 SCC `abortIdle` — now a transport-driven line state — **RESOLVED 2026-07-22**

`setAbortIdle(true)` (still at `V8Memory.cpp:49` / `Q605Memory.cpp:83`)
no longer means "permanently open line" — it now marks a machine with
**no *hardwired* peer**, and the standing Break/Abort it presents is a
LINE STATE driven by the transport. The moment a REAL peer transmits (a
non-express `injectRxFrame` — an LToUDP multicast frame, not the cable's
own synthesized CTS, which stays `express`) the SDLC line becomes a
live, terminated network whose idle is clean flags, and the abort drops
for a `kPeerHold` (~2 s) window refreshed on each peer frame; it returns
only once the peer goes quiet (`Scc8530::openLine()`,
`peerHold_`). A solo boot with no cable never refreshes it, so the
no-peer LAP timeout that lets `lcii_boot_etalon` / `q605_boot_etalon`
proceed is unchanged. Gate: `llap_loop_test` (a real peer's frame drops
the abort, a synthesized CTS does not, the abort returns after the
hold); the two-System and boot etalons stay green. The `setAbortIdle`
comments in `V8Memory.cpp` / `Q605Memory.cpp` now read "no *hardwired*
peer" and point at `openLine`.

### 1.9 Mac II Slot Manager ORB → phantom SHIFT — HLE-path only

`MacIIMemory.cpp:273-290`: on VIA1 ORB writes, if ACR shift-in is armed
and soft-flag bit5 at `ADBBase($CF8)+$15D` is set, call
`Via6522::armShiftComplete()`. Required for the HLE ADB POST wait
(Slot Manager clocks SR after slot select). **Poisonous under LLE**:
`$CF8` is ADBBase and `$15D` is the ADB driver's own flag — the hack
raised a phantom SHIFT ~320 cycles after every ST write, collapsing the
PIC↔VIA handshake. Gated with `!adbVia_.lle()` since 2026-07-22; never
fires on the default path. **Policy settled 2026-07-29** (with the
loud-fallback pass): the hack lives ONLY inside the HLE byte-model
fallback, and every entry into that fallback now announces itself as a
NON-CONFORMANT substitute on stderr — so §1.9 is de-facto eliminated on
every conformant path and is deleted the day the fallback itself is
(see the §2 retirement policy).

### 1.10 `POM68K_SCC_CLEANLINE` — machine config standing in for line state — **RESOLVED 2026-07-28**

The env is deleted; all eight memory classes call `setAbortIdle(true)`
unconditionally and the abort is presented only under a **genuine abort
condition** (CHANGELOG "LLE step 7"). The LLE model: LocalTalk is FM0 —
the SCC recovers its receive clock from the line's own transitions, so a
**virgin line (never driven since reset) reads clean**: no edge, no
recovered clock, no sampled 1s, no abort. That clean line is exactly
what Open Transport's `.MPP` bind spins on (RR0 bit 7, `$D1F04` —
SCCDBG capture 2026-07-24; System 7's LAP never waited on it). The
abort condition begins with the first frame the line carries — an LLAP
trailer ends in a real abort sequence and the driver then releases the
line mid-mark, so the receiver's last recovered state IS a standing
abort: `Scc8530::lineDriven_` latches at local SDLC EOM / Send Abort /
any `injectRxFrame`, `openLine()` requires it alongside the §1.8
peer-hold, and the EOM path presents the trailer's abort as the
ext/status event when only WR15 bit 7 is armed (the guest's own first
ENQ probe starts the Sys 7 LAP abort stream — no-peer timeout
mechanics and etalon timings unchanged). Note: the OT wedge itself was
no longer reproducible on the 2026-07-28 tree even before the fix
(most plausibly unwedged by the 2026-07-25 bus-time pass); the
virgin-line semantics guarantees the bind by construction. Gates:
`q605_ot_bind_etalon` (OS 8.1 + the in-process hub in the exact
`main.cpp` wiring; bind proven by the guest's post-ENQ DDP
conversation with the stack), `scc_ext_test` / `llap_loop_test`
re-pinned.

## 2. HLE replacements (whole devices at protocol level)

| Device | Files | What is replaced | Proper LLE would be |
|---|---|---|---|
| **Egret / Cuda** | `Egret.*` / `CudaLle.*` | **Firmware LLE is now the DEFAULT on every Egret/Cuda machine** (gates `m68hc05_test`, `cuda_lle_test`, `egret_lle_test`, `q605_cudalle_*`). Q605 flipped 2026-07-23 (`341s0788`); **the LC II Egret flipped 2026-07-24** (`341s0850`) once the **instruction-slaved ADB wire** (`CudaLle::mcu_.onCycles`) fixed the autopoll-load desync (was mouse ~1.5% delivery — now saturates like the HLE, CHANGELOG "instruction-slaved ADB wire"). Phase C's new machines are all firmware-LLE by default: Color Classic **factory `341s0417`** (2.35 — since 2026-07-29; the old "wedge" was the missing DFAC2 I2C ACK, `CudaLle::setI2cDfac`), Mac TV **factory `341s0789`** (2.38), LC III/III+ + IIvx/IIvi `341s0851`, LC 520/550/CC II `341s0060` (2.40 — 2.37 livelocks that ROM on pseudo-cmd `$0E`). `POM68K_EGRET_LLE=0` / `POM68K_CUDA_LLE=0` force the HLE; a missing dump falls back LOUDLY (stderr NON-CONFORMANT notice, 2026-07-29) | LC II re-flip **DONE**. Retirement policy settled — see "HLE-fallback retirement policy" below: fallbacks kept (dumps are non-distributable) but never silent; deletion is a deliberate product decision |
| **ADB modem (Mac II / IIx / IIcx, IIci, Centris + Quadra 610/650)** | `AdbVia.*` + `Pic1654s.*` + `AdbLine.*` | **LLE default** since 2026-07-22 when `roms/adbmodem/342s0440-b.bin` loads (`AdbVia.cpp:34-49`); since 2026-07-25 the same firmware path serves the **IIci** (`RbvMemory` `iici`) and the **Centris/Quadra** djMEMC machines. HLE = NEW/EVEN/ODD/IDLE byte SM on VIA SR — only if dump missing or `POM68K_ADB_LLE=0` | Done: PIC runs real firmware; `AdbLine` is bit-serial; `Via6522::extShiftCB1` is the wire |
| **ADB bus (Egret/Cuda machines)** | `AdbBus.*` | Bit-serial ADB → command-level Talk/Listen with clamped mouse deltas — **fallback-only since 2026-07-23** (both machines feed `AdbLine` under the firmware LLE) | Retire with the Egret HLE |

These are pragmatic and well-gated (`egret_test`, `input_etalon`,
`macii_boot_etalon`, `macii_mouse_etalon`); they must stay
protocol-faithful to ROM traces (TODO: "expand Cuda commands only from
ROM/driver traces").

### Mac II ADB modem — LLE default (2026-07-22)

The first HLE-replacement migrated to true firmware LLE — the pattern
the rest should follow:

- **Default**: `Pic1654s` (gate `pic1654s_test`) + `AdbLine` (gate
  `adbline_test`) + `Via6522::extShiftCB1` load `342s0440-b.bin`
  (`AdbVia::attach`). Self-test → `ADBReInit` → mouse-at-addr-3 on the
  wire; mouse moves (`macii_mouse_etalon` / `macii_mouse_trace`).
- **Force HLE**: `POM68K_ADB_LLE=0`, or missing dump — both LOUD since
  2026-07-29 (stderr "NON-CONFORMANT HLE ADB byte-model").
- **HLE-only leftover**: §1.9 ORB→SHIFT re-arm.
- Blockers that delayed default (PIC instruction cost, phantom SHIFT,
  VIA mode-111 first-falling-edge bit7 drop) are fixed — CHANGELOG
  2026-07-22 "Mac II LLE ADB default".

**Open device-model gap, found 2026-07-31** (the `q605_cudalle_key_etalon`
investigation): the shared `AdbLine` device answers keyboard **register 2
with a constant** `FF FF` — "no modifier held" — where MAME maintains a
live modifier bitmap and returns it, refreshing it on the read
(`macadb.cpp:694-700`, `m_buffer[0] = m_modifiers` after
`adb_pollkbd(1)`). A guest that reads R2 for modifier state therefore
never sees Command, Shift, Option or Control. Symptom that exposed it:
Cmd-N repaints on the LC II but not on the Quadra, even under System 7.6
where plain keystrokes demonstrably reach KeyMap. This is a genuine
divergence from the oracle, small and independently testable — the next
ADB item, ahead of the `AdbLine` handler-ID work already listed in TODO.

### HLE-fallback retirement policy (settled 2026-07-29)

Every machine now runs its exact factory MCU firmware by default, so the
HLE models (`Egret.*`, `AdbBus.*`, the `AdbVia` byte-model) have exactly
two consumers left: a no-dump install and the `POM68K_*_LLE=0` /
`POM68K_ADB_LLE=0` escapes. The fallbacks are **kept** — MCU dumps are
user-provided and not distributable, and without an Egret/Cuda the
V8-class machines cannot boot at all — but **never silent**: every entry
into an HLE ADB path prints a NON-CONFORMANT-substitute notice naming
the missing dump (all eight machine classes + `AdbVia::attach`). That
satisfies the §"Principle" rule (every HLE shortcut behind a visible
non-conformant flag) without orphaning no-dump users. Actually deleting
`Egret.*`/`AdbBus`/the byte-model (and §1.9 with them) is a product
decision — "POM68K requires MCU dumps" — not a code cleanup; take it
deliberately, not as a side effect.

Reference hierarchy for the Egret/Cuda protocol, established by the
second audit: **MAME `cuda.cpp`** (LLE, real 6805 firmware — the
timing oracle), **DingusPPC `viacuda.cpp` + `zdocs/developers/
viacuda.md`** (HLE at the same abstraction as ours, but with the real
packet framing and per-byte scheduling — the *design* oracle), Linux
`via-cuda.c` (host-side driver cross-check).

## 3. LLE with simplification (functional, not cycle-exact)

Second-audit format: status, then **Gaps** with the reference that
documents the real behavior.

- **CPUs**: `Cpu68k` (Plus, cycle-exact **with** RAM contention — the
  exception), `Cpu020`, `Cpu030`, `Cpu040` — 030/040 add i-cache
  *throughput overlays* (`POM68K_Q605_CACHE_BOOST`, `cacheBoost_`
  scaling in `flushTicks`) rather than architectural caches; no 040
  copyback/snooping.
  *Gaps*: peripheral-tick batching (`Cpu020::kPeriphBatch=64`,
  `Cpu030::kPeriphBatch=128`, `Cpu040` `kPeriphBatch=256` — ~4–16 µs
  IRQ-latency jitter); VIA E-clock synced at a fixed 32:1 ratio
  (real ≈31.91:1).
  *The overlay must not touch bus time* (root-caused 2026-07-25): it used
  to compress VIA-paced pulses `cacheBoost_`× because `viaSync` aligned to
  the E-clock in the **boosted** clock domain and `stall()` charged the wait
  in boosted cycles — which starved the ROM's host-paced Egret transport on
  every 030 machine above ~20 MHz (LC III, LC III+, IIvx red; IIsi pinned to
  boost 1 as a workaround). On real silicon an i-cache accelerates
  instruction fetch, never a VIA bus cycle, so bus time is now charged in
  **machine cycles** (`machineClock()`, `stall()` scaled) — the convention
  `Cpu040` already had. Both workarounds are retired: `RbvCpu` runs at the
  shared default, and `POM68K_Q605_CACHE_BOOST` — pinned to 1 since Q8.8 on
  a "boost 2+ fails SCSI" note — was re-measured green at 2/4/8 across the
  whole 040 family, so `Cpu040`/`CentrisCpu` default to 4 as well. The audit
  it prompted found one more boosted-clock reader: `AdbVia::syncTo` fed the
  PIC1654S co-step the raw core clock (the transceiver firmware ran
  `cacheBoost_`× fast); all four call sites now pass `machineClock()`.
  *030 + GLUE* (`MacIIMemory`, IIx/IIcx): once the CPU's own PMMU is
  enabled (TC bit 31) the address Moira presents is already physical, so
  the GLUE 24-bit remap must be skipped — double-translating wedges the
  boot mid-System. Same split as `V8Memory`'s 020-HMMU-vs-030-PMMU rule.
- **Egret/Cuda wire**: **every Egret/Cuda machine now DEFAULTS to the
  real firmware** (§2 — `M68hc05` + `CudaLle`; Q605 since 2026-07-23, the
  LC II Egret and all Phase C machines since 2026-07-24), which closes
  every wire gap on those paths: framing, pacing, MCU RAM and autopoll
  are the 68HC05's own. The notes below apply only to the `Egret.*` HLE
  **fallback** (`POM68K_EGRET_LLE=0` / no dump): real framing +
  61/71/88/13/30 µs per-byte schedule + `$1B` one-second modes since
  the §1.6b redo; autopoll obeys `$14`; boot-heartbeat shapes pinned
  against ROM readers, not firmware traces; MCU-RAM reads outside
  PRAM serve a 256-byte scratch.
- **Video**: `MacVideo.h`, `V8Video.h`, `TobyVideo.*`, and (Phase C)
  `SonoraVideo.h` (mv_sonora modelines/CLUT/sense in vctrl) and
  `VaspVideo.h` (the V8 framebuffer decode at VASP's 2048-byte row
  pitch, vasp.cpp screen_update) and `RbvVideo.h` (the IIsi/IIci
  RAM-based framebuffer through the Bt478 CLUT, MAME `rbv.cpp`) —
  whole-frame decode, no beam timing.
  Same classification as V8Video: register/CLUT/sense faithful, geometry
  from the guest-programmed modeline, but decoded once per frame.
  Toby's frame clock is **CRTC-derived since 2026-07-23** (the Q8.1
  DAFB treatment: htotal×vtotal ticks of the 30.24 MHz crystal, MAME
  `nubus_m2video.cpp` in `refs/mame/src/devices/bus/nubus/`), and the
  same pass fixed the TFB register file being silently write-dropped
  on the byte path the machine actually uses (gate `toby_test`).
  *Remaining gaps*: whole-frame decode only, no beam position.
- **DAFB/Antelope** (`Dafb.*`; MEMCjr 6+6-bit holding split stays in
  `Q605Memory`): register-level and close to MAME `dafb.cpp` parity
  since 2026-07-21 (Swatch CRTC timing → derived geometry, clockgen →
  guest-programmed frame rate — which MAME does *not* model —
  extended monitor sense, display-disable bit).
  **All three clock generators** are modelled since 2026-07-27 (ctor
  variant `Dafb::Clockgen`, gate `q605_dafb_test`): Apple's Gazelle on
  MEMCjr (`dafb.cpp:1322`), the DP8534 on djMEMC (`:1197`, Centris/
  Quadra 6x0/800 + LC 575) and the DP8531 on the discrete DAFB of the
  Quadra 700 (`:884`). Until then the Gazelle decoder served all three,
  so the djMEMC machines silently kept the 31.3344 MHz reset clock and
  the Q700's DP8531 nibbles for register 12 landed on the Gazelle's
  $3C3 serial port. Both now latch the real values (Centris 650 →
  30.26 MHz; Q700 → 25.175 then 30.24 MHz — trace with
  `POM68K_DAFB_CLOCK_TRACE=1`).
  *Gaps*: no VRAM arbitration/timing; VBL line hard-coded at 480 (as
  in MAME); the DAFB **TurboSCSI cell is absent** — real DAFB/DAFB II
  inserts configurable wait states per 5394/5396 access and can hold
  off /DTACK on pseudo-DMA (MAME `dafb.cpp` `m_scsi_*_cycles`). N/A on
  the Q605 (audit 2026-07-23): its SCSI sits behind PrimeTime, whose
  IOSB TurboSCSI wait-state cell IS modelled (`q605_turboscsi_test`);
  the DAFB cell only matters for a future Q700/Q950-class profile
  where SCSI DMA flows through DAFB itself.
- **Floppy**: `Swim2.*` since 2026-07-23 runs the **real bit engines**
  (step 13): the MAME `swim2.cpp` MFM sync-hunting shifter with the
  serial CRC-CCITT ($CDB4 seed, `M_CRC0` handshake tag), the GCR
  high-bit framer, and the TSS write serializer in half-cycles.
  `SonyDrive` stores the track as **raw cells** (one padded revolution;
  rotation angle from the spin counter → real rotational latency at
  every ACTION start; MFM writes are decoded back through the same
  state machine and only CRC-valid sectors commit to the image).
  `Iwm.*` (Plus / LC II SWIM1) still serves the byte-level nibble
  stream on the read side, but since 2026-07-23 it has the **real write
  mode** (MAME `MODE_WRITE`: handshake bit 7/bit 6, underrun halt,
  128-cycle byte cadence — gate `iwm_write_test`) and **GCR write-back
  commits** on both mouths (IWM nibble buffer and SWIM2 cell splice)
  through the checksum-verified inverse-6&2 decoder; the only logged
  drop left is an encoding/media mismatch. *Accepted simplifications*:
  discrete cells at the setup-programmed rate instead of MAME's attotime
  flux + `fdc_pll` (ideal PLL, no jitter); committed tracks re-encode
  canonically (no exotic-format preservation, recovered tag bytes
  dropped — flat images have no tag space); the Iwm read path stays
  byte-granular (no cell engine); floppy writes are in-memory only (no
  host-file persistence yet); tach is a sampled bit, not a waveform.
- **NuBus/DeclRom**: functional slot windows, no arbitration/timeout
  cycles.
- **SCSI**: `Ncr5380.*`, `Ncr53c96.*` — register/phase engines faithful
  to MAME; pseudo-DMA handshake per-byte. Since 2026-07-23 (step 9,
  partial) the 53C96 **schedules its delays**: selection = the
  `ncr53c90.cpp` arbitrate/assert/settle chain (19×CCF+6 SCSI clocks +
  `sync_period` per CDB byte at the 40 MHz chip clock), transfers =
  `sync_period`×bytes+2, default ON in `Q605Memory`
  (`POM68K_SCSI_LAT=0` restores instant, `=N` flat) — so the
  guest-programmed `R_CLOCK`/`R_SEQ` sync registers finally *do*
  something; and the **PrimeTime TurboSCSI wait-state cell** is in
  (`iosb.cpp:482-618`: 3-cycle register stalls, IOSB reg 2 →
  `times[4]={5,5,4,3}` on the bit-19 waitstated DMA alias). Gate:
  `q605_turboscsi_test`.
  *Gaps vs MAME `ncr53c90.cpp`* (120+ sub-state machine) — re-audited
  2026-07-23, step 9 CLOSED with these as accepted simplifications:
  - **tcounter↔FIFO staging**: MAME decides phase advance from
    `fifo_pos + tcounter`; our payload short-circuits the physical
    FIFO through `dataIn_`/`dataOut_`. The audit re-derived what a
    true staging engine would change observably: with instant
    (non-wire-paced) staging, R_FLAGS, DRQ, S_TC0/I_BUS event order
    and every byte read are IDENTICAL to the current model — the only
    difference is which internal array holds the bytes. A wire-paced
    engine WOULD differ (data starvation under a slow bus), but it
    risks every pinned Q6.3-Q6.6b OS 8.1 interaction for a timing
    nuance no Mac driver observes (they gate on S_TC0/FLAGS, both
    already honest). Rewrite dropped unless a real-world divergence
    shows up.
  - **Selection timeout on empty IDs is instant — and that IS oracle
    parity**: MAME ships `#define DELAY_HACK` (`ncr53c90.cpp:382`,
    `delay(1)` instead of `delay(8192*select_timeout)`), so the
    oracle's own bus scan is instant too. Not a divergence.
  - **SDTR** not modelled: no consumer — Quadra-era Mac drivers never
    negotiate sync on this bus (transfers are logically async; the
    sync registers still pace the step 9 delay model).
  - **BUSMOD 16-bit widths**: not wired on the Q605 (MAME
    `macquadra605.cpp` runs the chip in the 8-bit/BUSMD_1
    configuration through PrimeTime); would only matter for a future
    DAFB-DMA machine profile.
  - target-mode `CT_*` family and `CT_ABORT_DMA` missing
    (initiator-only is fine for a Mac; target-side DISCONNECT
    sequencing is approximated by direct BUS FREE detection).
- **SCC** (`Scc8530.*`): the SDLC/LLAP side is real since LLE step 3 +
  LLAP milestone 1 (2026-07-22): full Rx path (3-deep FIFO with
  per-byte RR1 status, hunt exit/re-entry carrier sense, WR1 Rx-int
  modes, address search, EOF+FCS tail), Tx frame capture on the
  underrun edge, Send Abort, standing-abort re-present, and LLAP
  inter-dialog-gap deferral on injected frames. A 2026-07-22 audit
  against MAME `z80scc.cpp` (fetched to `refs/mame/src/devices/
  machine/`) found we model MORE of SDLC than MAME does — its Send
  Abort (z80scc.cpp:1602), CRC resets (:1635-1643) and error reset
  (:1592) are marked "not implemented", and it has no Tx Underrun/EOM
  latch or hunt/sync machine at all (MAME is async-serial-centric).
  The **baud machinery is in since 2026-07-23** (gate `scc_baud_test`):
  WR4 clock mode + stop/parity bits, WR5 data bits, WR11 Tx-clock
  routing, WR12/13+WR14 BRG all derive each channel's byte pace from
  the machine clocks (RTxC 3.6864 MHz everywhere; PCLK per machine);
  SDLC derives the exact legacy LocalTalk constants (272/544/868), so
  `byteCycles_` is now only the pre-programming fallback.
  The **Tx/Rx engine is real since 2026-07-23** (gate `scc_engine_test`,
  backlog Medium tier): WR5 bit 3 gates the transmitter, a one-slot Tx
  buffer feeds a shifter paced at the derived rate (TxIP on the
  buffer-empty transition, RR0 TBE and RR1 All Sent live), the SDLC
  tail drains CRC+flag in 24 bit times at the programmed pace, the
  receiver verifies the Rx FCS (RR1 bit 6 on the EOF byte), and async
  bytes carry parity/framing error status raised as a special condition
  at READ time with WR1 bit 2 routing (z80scc data_read :2130).
  *Remaining gaps (MAME is the oracle here)*: no true bit-serial
  sampling (byte-granular engines, like MAME's device_serial), WR5 RTS
  pin not tracked, TRxC-pin/DPLL-async clock sources unmodelled.
- **ADB**: Mac II default path is firmware LLE (§2 / step 11).
  `AdbBus.*` (LC II / Q605 via Egret/Cuda) and the Mac II HLE fallback
  remain command-level.
  *Gaps (HLE paths only)*: `AdbVia` HLE assumes 2-byte Listen payloads
  (real ADB is 2–8 bytes; DingusPPC `adbbus.cpp` validates against 8);
  mouse deltas clamped. LLE `AdbLine` is bit-serial (MAME `macadb.cpp`
  lineage) — remaining fidelity is PIC↔device timing under load, not
  the byte SM.
- **Audio**: `Asc.*` FIFO semantics faithful (MODE mask, edge/level IRQ
  variants); fixed 22 257 Hz drain via fractional accumulators. Three
  flavors share the file — `AscV8` (LC II/VASP), `AscSonora` (the EASC at
  `$BC` on the Spice/Sonora machines) and `AscIosb` (Q605 stereo). Note
  the Sonora/Spice EASC `$804` status read must clear the IRQ
  unconditionally — MAME's `HALF_B` gate freezes the CC/LC III boot at
  "Bienvenue." in the autovector (pinned quirk, see CHANGELOG).
- **Confirmed parity** (second audit, no action): pseudo-VIA register
  decode + level-triggered ASC IRQ matches MAME `pseudovia.cpp`; the
  60.15 Hz CA1 tick is an independent timer in `Q605Memory::tick` like
  MAME IOSB's `6015_timer` (it does NOT depend on DAFB CRTC state);
  extended monitor sense matches; MEMCjr holding protocol lives in
  `Q605Memory` exactly where MAME's `djmemc.cpp` puts it.

## 4. Pure LLE / host convenience

- Pure LLE: `Via6522.*` (incl. `extShiftCB1` for Mac II ADB),
  `PseudoVia.*`, `MacMemory.*` (overlay), `Rtc` serial protocol,
  `Ariel.h`, `MacFrame.h`, `Pic1654s.*` + `AdbLine.*` (when dump
  present).
- **Bus fidelity — unmapped I/O reads back as 0 on the Sonora and
  PrimeTime maps** (`SonoraMemory`/`VaspMemory` catch-all, `Q605Memory`
  PrimeTime window; MAME `iosb.cpp:54-65` — no catch-all /BERR). This is
  a correctness fact, not a shortcut: an open-bus `$FF` there hard-wedges
  the LC III+ ProductInfo RAM-device poll at `$50F0A000` bit 3 and drops
  the all-in-one ROM into its serial debugger. The Plus/Mac II 24-bit
  maps keep their /BERR on truly unmapped space — this rule is specific
  to the Sonora/PrimeTime gate arrays MAME models the same way.
- **The Cuda's I2C bus and its DFAC2** (`CudaLle::setI2cDfac`, added
  2026-07-29): PB7 = SCL, PB6 = SDA (`cuda.cpp` `pb_w` :198-199) with a
  minimal slave answering at address `$6F` — START/STOP, the 9-pulse byte
  cadence, and the ACK. The payload is discarded, which IS oracle parity:
  MAME's `dfac2_device::write_data` only logs (its registers are still
  being reverse-engineered upstream). Populated exactly where MAME wires
  a DFAC2 — Color Classic (`maclc.cpp:505`), the Cuda AIOs LC 520/550/
  CC II (`maclc3.cpp:403`), Quadra 630 / LC 580 (`macquadra630.cpp:196`,
  bus shared with Valkyrie) — and left EMPTY on the Q605 and the Mac TV
  (`device_remove("dfac")`, nothing re-added). This is a fidelity fact,
  not a shortcut: without the ACK the factory Color Classic Cuda 2.35
  takes a DFAC error path and stops answering the host VIA session, which
  is what the old "341S0417 wedges the M68hc05" note actually was.
- **RBV: physical low RAM is the FRAMEBUFFER, and the guest's low memory
  is elsewhere** (IIsi / IIci). RAM-Based Video displays from the start of
  physical RAM (MAME `rbv.cpp` `update_screen` reads `m_ram_ptr` with no
  offset), and the ROM enables the PMMU (IIsi `TC = $80F84500`) to
  relocate the System's logical low memory. Consequence for anyone
  writing a test or a probe: **`peek8()` is physical**, so reading
  "ADBBase" / "Mouse" / "ScrnBase" on these machines returns desktop
  pixels, not globals. That mistake cost three rounds of a nonexistent
  "IIsi has no ADB" bug (2026-07-29); `iisi_input_etalon` therefore
  asserts on screen pixels (cursor motion), which the MMU cannot move.
- Host convenience (guest-invisible): `MacAudioHost.h`, GUI (`main.cpp`),
  trace tools, PRAM file persistence (`<disk>.pram`), LToUDP peer
  bridging.
- **`ScsiDisk` flat-HFS façade** (`ScsiDisk.cpp:70-132`): synthesizes an
  in-memory DDM + partition map + Apple_Driver43 in front of bare HFS
  `.dsk` images. Classified **host convenience** (media-format adapter,
  like supporting a disk image format), *not* an HLE hack: the guest
  sees a valid, consistent SCSI disk and writes round-trip. Keep; the
  synthetic driver partition must stay byte-identical to
  `tools/wrap_hfs.py` output.
- **`DeclRom::buildSynthetic`** (`MacIIMemory.cpp:61-65`): if no Toby
  DeclROM dump is found, a minimal synthetic card ROM is installed so
  Slot Manager still enumerates video. Host convenience / missing-asset
  fallback — prefer a real DeclROM when available.

## 5. Migration plan (LLE-first)

Priority order — each step removes a guest intervention and is gated by
the existing etalons (`finder_boot_matrix` must stay green):

1. ~~**PRAM-seed instead of ROM-patch on Mac II**~~ **DONE 2026-07-21**
   (see 1.1): the fix turned out to be LLE-correcting the `Rtc` itself —
   `macii_boot_etalon` is green with the `loadRom` patches deleted.
2. ~~**Delete the per-tick SPConfig clamps**~~ **DONE 2026-07-21**
   (see 1.3): 41/41 gates green clamp-free; AppleTalk-active boots go
   through the SCC no-peer timeouts instead of being fought.
3. ~~**SCC/SDLC no-peer timeout completion**~~ **DONE 2026-07-21**
   (see 1.4/1.5): hunt bit + EOM latch + mode-gated abort; watchdogs and
   `RsrcPatcher` deleted, LLAP ENQ probes observed on the wire.
4. ~~**Fix `AdbVia` ST stuck-EVEN**~~ **DONE 2026-07-21** (see 1.2):
   ADB delivers input during modals; EvQ machinery deleted, tests press
   Return over ADB.
5. ~~**FPSP for bare no-FPU**~~ **DONE 2026-07-21** (see 1.6): the 1.6
   guest-state machinery is deleted; soft-FPU stays as the supported
   config, and the bare-NONE `_FP68K` binding was solved the same day
   (see 1.6b) — bare `FPUModel::NONE` reaches the Finder, gated.
6. ~~DAFB → MAME parity~~ **DONE 2026-07-21** (LLE step 6 + Dafb
   extraction; remaining DAFB gaps folded into step 9 below).

Steps 7-10 come from the second audit (MAME + DingusPPC cross-check):

7. ~~**Cuda wire-model redo**~~ **DONE 2026-07-22** (see 1.6b): real
   framing + wire-event attention byte + 61/71/88/13/30 µs schedule;
   the `$76` echo-pop, GetPram erase, Q8.2 duplication and tick
   heuristic are deleted, 49/49 gates + Finder matrix green (CHANGELOG
   "LLE step 7"). The last per-reader wire hacks are retired.
8. ~~**SCC Rx path**~~ **DONE 2026-07-22.** Rx FIFO, carrier-driven
   hunt→sync, Rx character + special-condition interrupts, end-of-frame
   CRC status all landed with LLAP milestone 1; this pass added the two
   fidelity bugs a real AppleTalk flow exposed — the mid-frame Enter-Hunt
   no longer truncates a long directed frame (the empty-Chooser bug), the
   inter-dialog-gap deferral, and `abortIdle` is now a **transport-driven
   line state** (§1.8): a real peer drops the standing abort,
   `Scc8530::openLine`/`peerHold_`. The MAME `z80scc.cpp` audit (§3)
   found we model more SDLC than MAME; the remaining SCC work is the
   **async-serial** baud machinery (WR4/WR11/WR12-13), tracked separately
   under the Plus serial-ports TODO, not part of the LLAP path.
9. ~~**53C96 + TurboSCSI fidelity**~~ **CLOSED 2026-07-23** (landed +
   audit): the PrimeTime TurboSCSI wait-state cell (IOSB reg 2 →
   `times[4]={5,5,4,3}`, 3-cycle register stalls, bit-19 waitstated
   DMA alias — on the Q605 the cell is in **IOSB/PrimeTime**, so
   `iosb.cpp` is the oracle, not `dafb.cpp`) and the scheduled
   selection/bus-service delay model (MAME's arbitrate/settle chain +
   `sync_period` per byte at the 40 MHz chip clock, **default ON**;
   `POM68K_SCSI_LAT=0` → instant) are in, with the sync-register
   plumbing riding the same model. Gate: `q605_turboscsi_test` pinned
   against MAME's cycle counts. The remaining items were re-audited
   and closed as **accepted simplifications** (full verdicts in §3
   SCSI): the tcounter↔FIFO staging rewrite has no observable payoff
   against the pinned Q6.3-Q6.6b interactions; the instant selection
   timeout IS oracle parity (MAME `DELAY_HACK`); SDTR has no consumer;
   BUSMOD 16-bit is not wired on a Q605.
10. Longer term: ~~Toby CRTC-derived frame clock~~ **DONE 2026-07-23**
    (CHANGELOG "Toby: CRTC-derived frame clock" — plus the discovery
    that the TFB register file was write-dropped on the byte path);
    ~~Egret/Cuda **firmware** LLE~~ **DONE — DEFAULT on every Egret/Cuda
    machine** (Q605 2026-07-23, LC II Egret + all Phase C machines
    2026-07-24; CHANGELOG "The real Cuda firmware is the Quadra's
    DEFAULT" + "instruction-slaved ADB wire"): `M68hc05` 68HC05E1 core +
    `CudaLle` glue run the real 341S0788 / 341S0850 / 341S0851 / 341S0060
    — Finder boots, PRAM persists; silicon discoveries en route: the
    customized-E1 PFW input pin, the inverting ADB output stage, the
    Egret's falling-edge PC3 release. **The LC II re-flip is CLOSED**: the
    autopoll-load desync (mouse ~1.5% delivery) was the receive path
    frozen inside the peripheral-tick batch — the instruction-slaved wire
    `CudaLle::mcu_.onCycles` clocks the MCU per instruction, and the mouse
    now saturates (`lcii_mouse_trace`). §2 tracks the HLE retirement.
    Follow-up CLOSED 2026-07-29: the Color Classic "0417 wedge" was a
    missing DEVICE, not a core bug — the CC carries a DFAC2 on the Cuda's
    I2C and the 2.35 requires its ACK (`CudaLle::setI2cDfac`, minimal $6F
    slave). Factory 341S0417 is the CC default; the Mac TV runs its
    factory 341S0789 (2.38).
    Still longer-term: NuBus arbitration, 040 copyback/snooping
    (~~SWIM2/SonyDrive MFM cell timing~~ → step 13).
12. ~~**SCC async-baud machinery**~~ **DONE 2026-07-23** (CHANGELOG
    "SCC async-baud machinery"; §3 SCC entry): WR4/WR5/WR11/WR12-14 →
    guest-derived per-channel byte pace, machine-wired clocks, SDLC
    deriving the legacy LLAP constants exactly. Gate `scc_baud_test`.
    The remaining SCC fidelity items (bit-serial engine, Tx-Enable
    gating, Rx CRC → RR1 bit 6, counted underrun delay) stay in the
    TODO backlog's Medium/Low tiers.
13. ~~**SWIM2/SonyDrive MFM cell timing + CRC**~~ **DONE 2026-07-23**
    (CHANGELOG "SWIM2: the real cell engines"; §3 Floppy entry): the
    MAME `swim2.cpp` read/write bit engines ported verbatim (MFM sync
    hunter, serial CRC $CDB4 + `M_CRC0`, TSS half-cycle serializer)
    over a raw-cell `SonyDrive` track with real rotational latency;
    MFM write-back decodes through the same machine and drops
    CRC-invalid fields. Gates `swim2_test` / `swim2_media_test`
    re-pinned to oracle behavior (no-media FIFO stays empty, CRC
    tokens required to commit); `q605_floppy_boot_etalon` green on
    the first run. Follow-up landed same day: **GCR write-back + the
    IWM write engine** (CHANGELOG "IWM write engine + GCR write-back";
    gate `iwm_write_test`) — floppies are writable on all four
    machines. Remaining floppy items (Iwm read cell engine,
    flux-level jitter, host-file persistence) are §3 accepted
    simplifications.
11. ~~**Mac II ADB → firmware LLE**~~ **DONE, default since 2026-07-22**
    (§2). `Pic1654s` + `AdbLine` + `Via6522::extShiftCB1` run the real
    `342s0440-b.bin`; self-test → `ADBReInit` → mouse-at-addr-3 on the
    wire; mouse moves (`macii_mouse_etalon`). Blockers fixed: PIC
    instruction cost, Slot-Manager ORB phantom SHIFT (§1.9, gated
    `!lle()`), VIA mode-111 first-falling-edge bit7 drop (CHANGELOG
    2026-07-22 "Mac II LLE ADB default"). HLE `AdbVia` remains only as
    the no-dump / `POM68K_ADB_LLE=0` fallback — retire it once more
    machines run LLE ADB. Precedent for Egret/Cuda 68HC05 LLE (step 10).

Steps added by the sixth pass (2026-07-29):

14. ~~**SCC no-peer abort → a genuine line state**~~ **DONE 2026-07-28**
    (§1.10): a virgin line reads clean, the standing abort begins with the
    first frame the line carries (`Scc8530::lineDriven_`), and
    `POM68K_SCC_CLEANLINE` is deleted from all eight memory classes. Gate
    `q605_ot_bind_etalon`. Note recorded honestly: the OT wedge that
    motivated the env was already un-reproducible on the 2026-07-28 tree,
    so the change makes the `.MPP` bind *guaranteed* rather than
    timing-dependent.
15. ~~**Factory MCU parts everywhere**~~ **DONE 2026-07-29**: the Color
    Classic runs its factory Cuda 341S0417 once the DFAC2 I2C slave
    answers (§4), and the Mac TV its factory 341S0789. No machine now
    substitutes another machine's firmware.
16. ~~**HLE-fallback policy**~~ **SETTLED 2026-07-29** (§2 "HLE-fallback
    retirement policy"): kept but loud; deletion is a deliberate product
    decision, not a cleanup.

**The remaining LLE distance is now §3, not §1.** With the hack list
empty on default paths, what is left is the *accepted simplifications*
list, in rough order of how much correctness it buys:

- **Whole-frame video on every machine** (no beam position). The one gap
  that makes a whole class of software wrong rather than approximate.
- **Peripheral-tick batching** (`kPeriphBatch` 64/128/256) — 4-16 µs of
  IRQ-latency jitter; and the VIA E-clock at a fixed 32:1 (real ≈31.91:1).
- **No 040 copyback/snooping**; the i-cache overlays are throughput
  models, not architectural caches.
- **SCC**: no true bit-serial sampling, WR5 RTS pin untracked, DPLL
  unmodelled (MAME stubs it too) — only worth it with a real async
  transport to talk to.
- **Floppy**: the `Iwm` READ path stays byte-granular, no flux-level
  jitter or PLL, and floppy writes are in-memory only (no host-file
  persistence).
- **NuBus** arbitration/timeout cycles; **DAFB** VRAM arbitration and a
  VBL line hard-coded at 480 (as in MAME).

**Caveat on all of the above, learned the hard way on 2026-07-29**: this
inventory is only worth what its gates are worth, and 22 of 32 profiles
still have no beyond-boot gate at all. In one day the test suite produced
a false green (a positive assertion over a too-wide window) and a false
bug (three rounds chasing a physical-vs-logical memory artifact). Adding
fidelity on top of unverifiable coverage is work with no way to know it
landed — the test-depth pass in `TODO.md` outranks every item above.

Every remaining hack must be: (a) behind an env flag or module toggle,
(b) logged when it fires, (c) listed here, and (d) eventually migrated
into the `HLE_OVERLAY.md` framework with its visible non-conformant-mode
indicator. Save states must stamp active HLE modules (TODO
cross-machine).
