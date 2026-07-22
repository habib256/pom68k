# LLE vs HLE — inventory and migration plan

**Purpose.** POM68K's direction is: harden the **LLE** core first (hardware
modeled at register/protocol level from silicon references, verified by
gates and oracles), and only later layer an **opt-in, clearly-flagged HLE
accelerator** on top (`HLE_OVERLAY.md`). That requires knowing exactly
where the current code already deviates from hardware. This document is
the complete inventory and the plan to shrink it. Audited 2026-07-21;
**second pass the same day** cross-checked every device against MAME
(`refs/mame-apple`, `refs/mame`) and DingusPPC (`refs/dingusppc`) — the
per-device gap lists in §3 and the migration steps 7-10 come from that
pass.

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
>   (2026-07-22, §2) is the template: the real transceiver goes in first,
>   opt-in, until it is the default — *then* an HLE fast-path could be
>   offered as a boost, never as the substitute.
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

### 1.7 Mac II / LC II RTC + Egret PRAM factory seeding — `Rtc.cpp:13-28`, `Egret.cpp:54-91`

Seeds `'NuMc'` validity signature + Basilisk-style defaults + SPConfig
`$22` on cold boot. **Borderline acceptable**: a device shipping with
factory PRAM contents is hardware-plausible, and it prevents the ROM's
cold-PRAM re-init loop. Keep, but treat the *contents* as a documented
policy, not scattered magic.

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

## 2. HLE replacements (whole devices at protocol level)

| Device | Files | What is replaced | Proper LLE would be |
|---|---|---|---|
| **Egret / Cuda** | `Egret.*` | 68HC05 MCU firmware → packet-level command emulation (ADB autopoll, RTC heartbeat, XPRAM streams, Cuda polarity flavor) | 68HC05 core + dumped Egret/Cuda firmware (big; MAME has it) |
| **ADB modem (Mac II)** | `AdbVia.*` (LLE **default** since 2026-07-22; HLE = no-dump / `POM68K_ADB_LLE=0` fallback) | PIC1654S firmware → NEW/EVEN/ODD/IDLE byte state machine on the VIA SR (HLE fallback only) | **LLE is the default**: `Pic1654s` runs the real `342s0440-b.bin`, `AdbLine` is the bit-serial device, `Via6522::extShiftCB1` the wire — see below |
| **ADB bus** | `AdbBus.*` (HLE fallback) / `AdbLine.*` (LLE default on Mac II) | HLE: bit-serial ADB → command-level, clamped deltas. LLE: real bit-serial line (`AdbLine`, MAME `macadb.cpp` port) | LLE `AdbLine` is bit-level and default (Mac II) |

These are pragmatic and well-gated (`egret_test`, `input_etalon`,
`macii_boot_etalon`); they must stay protocol-faithful to ROM traces
(TODO: "expand Cuda commands only from ROM/driver traces").

The **Mac II ADB modem is the first HLE-replacement being migrated to
true firmware LLE** (2026-07-22) — the pattern the rest should follow.
`Pic1654s` (PIC16C5x core, gate `pic1654s_test`) + `AdbLine` (gate
`adbline_test`) + `Via6522::extShiftCB1` run the *real* transceiver ROM
behind an opt-in flag; it already reaches the point where the PIC
receives the ROM's commands, drives the ADB bus, and `AdbLine` decodes
them. It is **not the default yet** — the remaining blocker (the PIC
mis-routing the ROM's self-test under burst interleaving) needs
cycle-exact PIC↔CPU co-stepping (TODO ★). This ordering is deliberate:
see the principle below.
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
  *Gaps*: peripheral-tick batching (128 cycles LC II / 256 Q605 —
  ~8-16 µs IRQ-latency jitter); VIA E-clock synced at a fixed 32:1
  ratio (real ≈31.91:1).
- **Egret/Cuda wire** (`Egret.*`): real framing + 61/71/88/13/30 µs
  per-byte schedule + `$1B` one-second modes + open-ended streams
  since the §1.6b redo (2026-07-22). Autopoll rate obeys `$14`
  (default 11 ms, DingusPPC's `poll_rate`). *Remaining gaps*: still a
  packet-level HLE of the 68HC05 firmware (§2 / step 10); the Egret
  flavor's boot-heartbeat packet shapes are pinned against the LC II
  ROM readers, not firmware traces; MCU-RAM reads outside PRAM serve a
  256-byte scratch, not the real register file.
- **Video**: `MacVideo.h`, `V8Video.h`, `TobyVideo.*` — whole-frame
  decode, no beam timing.
  *Gaps*: `TobyVideo.cpp:179` bakes a 60 Hz / 261 120-cycle frame
  instead of deriving it from the Toby CRTC registers (the DAFB got
  this treatment in Q8.1; Toby never did).
- **DAFB/Antelope** (`Dafb.*`; MEMCjr 6+6-bit holding split stays in
  `Q605Memory`): register-level and close to MAME `dafb.cpp` parity
  since 2026-07-21 (Swatch CRTC timing → derived geometry, Gazelle
  clockgen → guest-programmed frame rate — which MAME does *not*
  model — extended monitor sense, display-disable bit).
  *Gaps*: no VRAM arbitration/timing; VBL line hard-coded at 480 (as
  in MAME); the DAFB **TurboSCSI cell is absent** — real DAFB/DAFB II
  inserts configurable wait states per 5394/5396 access and can hold
  off /DTACK on pseudo-DMA (MAME `dafb.cpp` `m_scsi_*_cycles`); our
  53C96 pseudo-DMA path bypasses it entirely.
- **Floppy**: `Swim2.*` (register file + FIFO real; media transactions
  whole-sector), `SonyDrive.*` (no rotational latency), `Iwm.*` is the
  most faithful (nibble timing, tach).
  *Gaps*: no MFM cell timing or CRC verification (MAME `floppy.cpp`
  models bit cells); tach is a sampled bit, not a waveform; SWIM2 FIFO
  drain synced via `syncSwimFromCpu()` batches, not per-access
  `sync()` as MAME.
- **NuBus/DeclRom**: functional slot windows, no arbitration/timeout
  cycles.
- **SCSI**: `Ncr5380.*`, `Ncr53c96.*` — register/phase engines faithful
  to MAME; pseudo-DMA handshake per-byte without bus timing.
  *Gaps vs MAME `ncr53c90.cpp`* (120+ sub-state machine): no
  tcounter↔FIFO interplay (MAME decides phase advance from
  `fifo_pos + tcounter`); sync-negotiation registers stubbed (all
  transfers async); zero-latency selection/arbitration
  (`POM68K_SCSI_LAT` default 0 — MAME schedules per-step
  `delay_cycles`); target-mode `CT_*` command family and
  `CT_ABORT_DMA` missing (initiator-only is fine for a Mac, but
  target-side DISCONNECT sequencing is approximated by direct BUS FREE
  detection); 8-bit pseudo-DMA only (no BUSMOD 16-bit widths).
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
  *Gaps (MAME is the oracle here)*: **no baud machinery** — WR12/13
  BRG constant (z80scc.cpp:2476 `get_brg_rate`), WR4 clock mode X1/16/
  32/64 (:1157 `get_clock_mode`) and WR11 clock-source routing (:2565
  `update_serial`) are ignored; `byteCycles_` is a fixed LocalTalk
  rate. That is *the* blocker for usable async serial ports (Plus
  milestone, TODO). Also: no bit-serial engine (parity/framing error
  generation), WR5 Tx-Enable not gating, no Rx CRC verification (RR1
  bit 6 never set), Tx Underrun uses a flat 1200-cycle delay instead
  of counting CRC+flag bit times.
- **ADB** (`AdbVia.*`, `AdbBus.*`): command-level (§2).
  *Gaps*: `AdbVia` assumes 2-byte Listen payloads (real ADB is 2-8
  bytes; DingusPPC `adbbus.cpp` validates against 8).
- **Audio**: `Asc.*` FIFO semantics faithful (MODE mask, edge/level IRQ
  variants); fixed 22 257 Hz drain via fractional accumulators.
- **Confirmed parity** (second audit, no action): pseudo-VIA register
  decode + level-triggered ASC IRQ matches MAME `pseudovia.cpp`; the
  60.15 Hz CA1 tick is an independent timer in `Q605Memory::tick` like
  MAME IOSB's `6015_timer` (it does NOT depend on DAFB CRTC state);
  extended monitor sense matches; MEMCjr holding protocol lives in
  `Q605Memory` exactly where MAME's `djmemc.cpp` puts it.

## 4. Pure LLE / host convenience

- Pure LLE: `Via6522.*`, `PseudoVia.*`, `MacMemory.*` (overlay), `Rtc`
  serial protocol, `Ariel.h`, `MacFrame.h`.
- Host convenience (guest-invisible): `MacAudioHost.h`, GUI (`main.cpp`),
  trace tools, PRAM file persistence (`<disk>.pram`).
- **`ScsiDisk` flat-HFS façade** (`ScsiDisk.cpp:70-132`): synthesizes an
  in-memory DDM + partition map + Apple_Driver43 in front of bare HFS
  `.dsk` images. Classified **host convenience** (media-format adapter,
  like supporting a disk image format), *not* an HLE hack: the guest
  sees a valid, consistent SCSI disk and writes round-trip. Keep; the
  synthetic driver partition must stay byte-identical to
  `tools/wrap_hfs.py` output.

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
9. **53C96 + DAFB TurboSCSI fidelity** — tcounter↔FIFO phase engine,
   scheduled selection/arbitration latency (non-zero default for
   `POM68K_SCSI_LAT`), sync-negotiation plumbing, and the DAFB
   TurboSCSI wait-state/DTACK-holdoff cell (MAME `ncr53c90.cpp` +
   `dafb.cpp` as oracles). Gate: existing SCSI etalons + a timing
   probe pinned against MAME's cycle counts.
10. Longer term: Toby CRTC-derived frame clock, SWIM2/SonyDrive MFM
    cell timing + CRC, NuBus arbitration, 040 copyback/snooping,
    Egret/Cuda **firmware** LLE (68HC05 core + dump, only if a use
    case demands it — MAME proves it works).
11. **Mac II ADB → firmware LLE: DONE, default since 2026-07-22** (§2).
    `Pic1654s` + `AdbLine` + `Via6522::extShiftCB1` run the real
    `342s0440-b.bin`; self-test → `ADBReInit` → mouse-at-addr-3 all over
    the wire, the mouse moves (`macii_mouse_trace`). The blocker was
    three bugs — PIC instruction cost, a phantom SHIFT from the
    Slot-Manager ORB hack (now gated off in LLE), and the VIA mode-111
    first-falling-edge bit7 drop (CHANGELOG 2026-07-22 "Mac II LLE ADB
    default"). HLE `AdbVia` remains only as the no-dump fallback —
    retire it once more machines run LLE ADB. This is the concrete
    precedent for the Egret/Cuda 68HC05 firmware LLE (step 10) later.

Every remaining hack must be: (a) behind an env flag or module toggle,
(b) logged when it fires, (c) listed here, and (d) eventually migrated
into the `HLE_OVERLAY.md` framework with its visible non-conformant-mode
indicator. Save states must stamp active HLE modules (TODO
cross-machine).
