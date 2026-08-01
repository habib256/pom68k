# LLE vs HLE — the deviation inventory

**What this file is.** The list of every place POM68K's behaviour is not the
hardware's, and for each one *why* and *what would close it*. POM68K's
direction is: harden the **LLE** core first (registers/protocol/timing from
silicon references, gate- and oracle-verified), and only later layer an
**opt-in, clearly-flagged HLE accelerator** on top (`HLE_OVERLAY.md`). That
requires knowing exactly where the code deviates today.

Current as of 2026-08-01 (**seventh pass**, counts re-derived — 34 machine
profiles, 136 gates). The two profiles added since the pass (SE/30, IIfx) and
the gated-but-unregistered Duo 230 were not themselves audited here.
Line numbers are indicative — grep before relying on them.

**Read § 1 first.** §§ 2–3 are the smaller live surface (HLE fallbacks, pure-LLE
traps); § 4 is resolved history, compressed to a table; § 5 is the standing
caveat about what the whole inventory is worth.

> ## Principle — a clean LLE **before** the HLE boost
>
> **Order matters and it is not negotiable: the LLE core must be correct and
> complete first; the HLE "boost" (`HLE_OVERLAY.md`) is layered on top only
> afterwards.** The boost trades conformance for speed, and it is only ever
> meaningful, testable and safe when a faithful gate-verified LLE reference
> sits underneath to (a) define correct behaviour, (b) fall back to, and
> (c) diff against.
>
> Building the boost first inverts the dependency: shortcuts calcify into the
> only implementation, "correct" becomes whatever one System image needed, and
> every new image is a guess — the trap the whole § 4.1 hack list documents.
> So:
>
> - Finish a subsystem's LLE (real silicon/firmware, gated) **before** adding
>   any HLE boost path for it.
> - Every HLE shortcut ships **behind a visible non-conformant flag** with the
>   LLE path still present and default.
> - A boost is accepted only once it is shown equivalent to the LLE reference
>   on the gates; where it diverges, that divergence is the flag's whole point
>   and must be documented, not hidden.

## Classification

| Class | Meaning | Policy |
|---|---|---|
| **Pure LLE** | Registers/protocol/timing from MAME/datasheets/ROM traces | Keep; extend accuracy |
| **LLE simplified** | Functional, not cycle-exact (whole-frame video, batched ticks) | Acceptable; tracked in § 1 |
| **HLE replacement** | A whole device replaced at command level (no firmware) | Fallback-only today (§ 2) |
| **HLE hack** | Emulator reaches *into the guest* — patches ROM/RAM, injects events | **Eliminate.** List is empty on every default path (§ 4.1) |
| **Host convenience** | No guest-visible effect (media formats, rendering, audio host) | Keep |

---

# 1. Open gaps — LLE with simplification

Functional but not exact. This is the live list: every entry says what is
simplified, why it was accepted, and **→ what would close it**.

Rough priority order is the § 5 summary at the end.

## 1.1 Video — whole-frame decode everywhere, no beam position

`MacVideo.h` (Plus/compacts), `V8Video.h` (V8 family), `TobyVideo.*` (Mac II
NuBus), `SonoraVideo.h`, `VaspVideo.h` (2048-byte row pitch), `RbvVideo.h`
(IIsi/IIci through the Bt478 CLUT), `Dafb.*` (Quadra), `Valkyrie.*` (Q630/
LC 580).

Registers, CLUT, monitor sense and the guest-programmed modeline are faithful;
the framebuffer is decoded **once per frame** (`V8Video.h:10`, "no beam").
Toby's frame clock is CRTC-derived (htotal×vtotal of the 30.24 MHz crystal,
MAME `nubus_m2video.cpp`); DAFB derives geometry from the Swatch CRTC and the
frame rate from the clockgen — which MAME does *not* model.

*Gaps*: no beam position, so raster-timed effects and mid-frame register
changes are wrong rather than approximate. No VRAM arbitration/timing. DAFB's
VBL line is hard-coded at 480 (`Dafb.cpp:325-346` — as in MAME). Valkyrie's
pixel clock is programmed over I2C by the Cuda (Valkyrie is slave `$28`);
**that bus is not modelled**, so the clock stays at the 31.3344 MHz default and
only the derived refresh rate is affected (`Valkyrie.h:20-23`).

→ **Closing it**: a scanline-granular decode driven by the existing CRTC
geometry, plus a beam-position read for the registers that expose one. The
Valkyrie clock is much smaller — extend `CudaLle`'s I2C master (already there
for the DFAC2, § 3) with the `$28` slave and its M/N/P divisors.

## 1.2 CPUs — cycle-exact only on the Plus

`Cpu68k` (Plus, cycle-exact **with** RAM contention — the exception),
`Cpu020`, `Cpu030`, `Cpu040` and the per-machine derivatives (`SonoraCpu`,
`VaspCpu`, `RbvCpu`, `CentrisCpu`, `Q700Cpu`, `Q630Cpu`).

*Gaps*:

- **Peripheral-tick batching**: peripherals advance in blocks of
  `kPeriphBatch` core cycles — 64 on `Cpu020.h:55`, 128 on the 030s
  (`Cpu030.h:167`, `SonoraCpu.h:76`, `VaspCpu.h:69`, `RbvCpu.h:69`), 256 on
  the 040s (`Cpu040.cpp:11` and siblings). ~4–16 µs of IRQ-latency jitter.
- **VIA E-clock at a fixed 32:1** (real ≈31.91:1) — `V8Memory::viaSync`
  (`V8Memory.cpp:280`) and its per-machine twins align to `viaDiv_`
  (`V8Memory.h:355`, 20 at C15M / 40 on the Mac TV's C32M).
- **i-cache is a throughput overlay, not an architectural cache**:
  `cacheBoost_` scales Moira cycles in `flushTicks` (`Cpu040.h:77`, default
  **4**; `POM68K_Q605_CACHE_BOOST` overrides 1–64). No 040 copyback or
  snooping.

→ **Closing it**: batching is a tick-granularity knob — drop `kPeriphBatch`
toward 1 and re-measure the boot etalons for the cost. The E-clock ratio wants
a fractional accumulator like the ASC drain. Real caches are a Moira-side
project (`extern/moira`), not a wrapper change.

**Pinned lesson — the overlay must not touch bus time** (root-caused
2026-07-25). It used to compress VIA-paced pulses `cacheBoost_`× because
`viaSync` aligned to the E-clock in the **boosted** clock domain and `stall()`
charged the wait in boosted cycles, starving the ROM's host-paced Egret
transport on every 030 machine above ~20 MHz (IIsi wedge; LC III / LC III+ /
IIvx black screen). On real silicon an i-cache accelerates instruction fetch,
never a VIA bus cycle. Bus time is now charged in **machine cycles**
(`machineClock()`, `stall()` scaled) — the convention `Cpu040` already had.
Both workarounds are retired (`RbvCpu` back to the shared default;
`POM68K_Q605_CACHE_BOOST` re-measured green at 2/4/8 across the 040 family).
The audit found one more boosted-clock reader: `AdbVia::syncTo` fed the
PIC1654S co-step the raw core clock — all four call sites now pass
`machineClock()`. **Any new consumer of the CPU clock must ask which domain it
is in.**

**030 + GLUE** (`MacIIMemory`, IIx/IIcx): once the CPU's own PMMU is enabled
(TC bit 31) the address Moira presents is already physical, so the GLUE 24-bit
remap must be skipped — double-translating wedges the boot mid-System. Same
split as `V8Memory`'s 020-HMMU-vs-030-PMMU rule.

## 1.3 Floppy — ideal cells, no flux

`Swim2.*` runs the **real bit engines** (MAME `swim2.cpp`): the MFM
sync-hunting shifter with serial CRC-CCITT (`$CDB4` seed, `M_CRC0` handshake
tag), the GCR high-bit framer, the TSS write serializer in half-cycles.
`SonyDrive` stores the track as **raw cells** (one padded revolution; rotation
angle from the spin counter → real rotational latency at every ACTION start);
MFM writes decode back through the same state machine and only CRC-valid
sectors commit. `Iwm.*` (Plus / LC II) has the real write mode (MAME
`MODE_WRITE`: handshake bit 7/bit 6, underrun halt, 128-cycle byte cadence) and
GCR write-back commits on both mouths through the checksum-verified inverse-6&2
decoder.

*Accepted simplifications*:

- **Discrete cells at the setup-programmed rate** instead of MAME's attotime
  flux + `fdc_pll` — an ideal PLL, no jitter.
- **`Swim1`'s ISM read engine reduces to the SWIM2 shifter**: MAME's LS-pair
  cell state machine + correction factors (`swim1.cpp:965-1140`) exist to
  discriminate real-world flux jitter, which ideal cells do not have
  (`Swim1.h:16-21`). `DAT1BYTE` is not wired — the LC II polls the FIFO.
- **The `Iwm` READ path stays byte-granular** (nibble stream, no cell engine).
- **Committed tracks re-encode canonically** — no exotic-format preservation;
  recovered tag bytes are dropped (flat images have no tag space).
- **Tach is a sampled bit, not a waveform.**

→ **Closing it**: a flux/PLL layer under `SonyDrive`'s cell store would close
the first three at once — it is the same change MAME made, and `Swim1`'s
correction factors then become live rather than dead code.

*Not a gap (corrected 2026-07-31)*: **host-file persistence exists.**
`SonyDrive::flushToFile` (`SonyDrive.cpp:676`) writes committed sectors back on
eject and at exit via temp+rename, regenerating the DiskCopy 4.2 header and
data checksum. It is **on by default in the GUI** (`main.cpp:1205-1206`,
opt-out `POM68K_FLOPPY_RO=1`) and deliberately off in tests. Gate
`floppy_persist_test`.

## 1.4 SCC — byte-granular engines, no bit sampling

`Scc8530.*`. The SDLC/LLAP side is unusually complete: full Rx path (3-deep
FIFO with per-byte RR1 status, hunt exit/re-entry carrier sense, WR1 Rx-int
modes, address search, EOF+FCS tail), Tx frame capture on the underrun edge,
Send Abort, standing-abort re-present, LLAP inter-dialog-gap deferral. The baud
machinery derives each channel's byte pace from the machine clocks (WR4 clock
mode + stop/parity, WR5 data bits, WR11 Tx-clock routing, WR12/13+WR14 BRG;
RTxC 3.6864 MHz everywhere, PCLK per machine), and SDLC derives the legacy
LocalTalk constants (272/544/868) exactly, so `byteCycles_` is only the
pre-programming fallback. The Tx/Rx engine is real: WR5 bit 3 gates the
transmitter, a one-slot Tx buffer feeds a shifter at the derived rate, the SDLC
tail drains CRC+flag in 24 bit times, the receiver verifies the Rx FCS (RR1
bit 6 on the EOF byte), and async bytes carry parity/framing status raised as a
special condition at READ time with WR1 bit 2 routing.

A 2026-07-22 audit against MAME `z80scc.cpp` found **we model more of SDLC than
MAME does** — its Send Abort (`:1602`), CRC resets (`:1635-1643`) and error
reset (`:1592`) are marked "not implemented", and it has no Tx Underrun/EOM
latch or hunt/sync machine at all (MAME is async-serial-centric). MAME is
therefore a weak oracle here.

*Gaps*: no true **bit-serial sampling** (byte-granular engines, unlike MAME's
`device_serial`); **WR5 RTS pin not tracked**; **TRxC-pin and DPLL-async clock
sources unmodelled**.

→ **Closing it**: only worth doing against a real async transport to talk to —
a host serial port or modem emulation would give the bit engine a consumer.
Until then the byte engines are observationally equivalent for LLAP.

Gates: `scc_baud_test`, `scc_engine_test`, `scc_ext_test`, `llap_loop_test`.

## 1.5 SCSI — phase-faithful, with four audited omissions

`Ncr5380.*`, `Ncr53c96.*` — register/phase engines faithful to MAME,
pseudo-DMA handshake per byte. The 53C96 **schedules its delays**: selection =
the `ncr53c90.cpp` arbitrate/assert/settle chain (19×CCF+6 SCSI clocks +
`sync_period` per CDB byte at the 40 MHz chip clock), transfers =
`sync_period`×bytes+2, default ON (`POM68K_SCSI_LAT=0` restores instant, `=N`
flat) — so the guest-programmed `R_CLOCK`/`R_SEQ` registers finally do
something. The **PrimeTime TurboSCSI wait-state cell** is in (`iosb.cpp:482-618`:
3-cycle register stalls, IOSB reg 2 → `times[4]={5,5,4,3}` on the bit-19
waitstated DMA alias). Gate `q605_turboscsi_test`.

*Gaps vs MAME's 120+ sub-state machine — re-audited 2026-07-23, all four
closed as **accepted**, with the verdict recorded so nobody re-opens them
blind*:

- **tcounter↔FIFO staging**: MAME decides phase advance from
  `fifo_pos + tcounter`; our payload short-circuits the physical FIFO through
  `dataIn_`/`dataOut_`. The audit re-derived what a true staging engine would
  change *observably*: with instant staging, R_FLAGS, DRQ, S_TC0/I_BUS event
  order and every byte read are **identical**. A wire-paced engine would
  differ (data starvation under a slow bus) but risks every pinned Q6.3-Q6.6b
  OS 8.1 interaction for a nuance no Mac driver observes — they gate on
  S_TC0/FLAGS, both already honest. → Reopen only on a real divergence.
- **Instant selection timeout on empty IDs — and that IS oracle parity**: MAME
  ships `#define DELAY_HACK` (`ncr53c90.cpp:382`, `delay(1)` instead of
  `delay(8192*select_timeout)`), so the oracle's own bus scan is instant. Not
  a divergence.
- **SDTR not modelled**: no consumer — Quadra-era Mac drivers never negotiate
  sync on this bus (transfers are logically async; the sync registers still
  pace the delay model).
- **BUSMOD 16-bit widths**: not wired on the Q605 (MAME `macquadra605.cpp`
  runs the chip 8-bit/BUSMD_1 through PrimeTime). Only matters for a future
  DAFB-DMA machine profile.
- **Target-mode `CT_*` family and `CT_ABORT_DMA` missing** — initiator-only is
  fine for a Mac; target-side DISCONNECT is approximated by direct BUS FREE
  detection.

**DAFB's own TurboSCSI cell is absent** (real DAFB/DAFB II inserts configurable
wait states per 5394/5396 access and can hold off /DTACK on pseudo-DMA, MAME
`dafb.cpp` `m_scsi_*_cycles`). N/A on the Q605 — its SCSI sits behind
PrimeTime, whose cell IS modelled. → Only matters for a Q950-class profile
where SCSI DMA flows through DAFB itself. Note the **Quadra 700 already routes
SCSI through DAFB's cell** (`Q700Memory`), so this is the machine to measure
against first.

## 1.6 ADB — bit-serial under firmware, command-level in the fallback

Every ADB machine POM68K ships runs **real MCU firmware by default** (§ 2), so
`AdbLine` is bit-serial (MAME `macadb.cpp` lineage) on every conformant path.

*Gaps*: remaining LLE fidelity is **PIC↔device timing under load**, not the
byte state machine. On the HLE fallback only: `AdbVia` assumes 2-byte Listen
payloads (real ADB is 2–8; DingusPPC `adbbus.cpp` validates against 8), and
mouse deltas are clamped.

*Still open — the Quadra modifier symptom.* A genuine device-model divergence
was found and closed 2026-07-31 (§ 2), but **it did not fix the symptom that
exposed it**: Cmd-N still fails to repaint on the Quadra under System 7.6
(measured, `tests/adb_key_probe.cpp`), while the LC II control is unaffected.
→ The Quadra's modifier path has a **second, unidentified cause**. Next step is
to trace where the modifier is lost between `AdbLine` R2 and the guest's
KeyMap on the Cuda path specifically.

## 1.7 Audio — fixed drain rate

`Asc.*`. FIFO semantics are faithful (MODE mask, edge/level IRQ variants);
the drain is a fixed 22 257 Hz via fractional accumulators. Three flavors share
the file: `AscV8` (LC II/VASP), `AscSonora` (the EASC at `$BC` on Spice/Sonora)
and `AscIosb` (Q605 stereo).

**Pinned quirk**: the Sonora/Spice EASC `$804` status read must clear the IRQ
**unconditionally** — MAME's `HALF_B` gate freezes the CC / LC III boot at
"Bienvenue." inside the autovector.

→ **Closing it**: derive the drain from the programmed sample rate rather than
pinning 22 257 Hz. Low value — no known guest depends on it.

## 1.8 NuBus / DeclRom

Functional slot windows; **no arbitration or timeout cycles**.
→ Closing it needs a machine that actually contends for the bus (a second card).

## 1.9 Egret/Cuda HLE fallback wire (fallback path only)

The firmware path (§ 2) closes every wire gap — framing, pacing, MCU RAM and
autopoll are the 68HC05's own. The notes below apply **only** to `Egret.*`
under `POM68K_EGRET_LLE=0` / no dump: real framing + the 61/71/88/13/30 µs
per-byte schedule + `$1B` one-second modes; autopoll obeys `$14`; boot-heartbeat
shapes are pinned against **ROM readers, not firmware traces**; MCU-RAM reads
outside PRAM serve a 256-byte scratch.

## 1.10 In-process AppleTalk — a faithful peer with one synthesized signal

`AtalkStack.*` / `AfpServer.*` / `PapServer.*` / `MacIpGateway.*` attach at the
**`Scc8530` wire**, the same point as the LToUDP cable (`AtalkStack.h:4-8`), so
the guest side is fully LLE: real LLAP frames, real DDP/RTMP/ZIP/NBP/ATP. The
stack is the network's *other end*, not a shortcut in the Mac.

*Gap*: **RTS/CTS never cross the wire.** A directed `lapRTS` is answered by a
locally synthesized `lapCTS` (`main.cpp:174-181`), threaded through the
sender's half-duplex Rx-off window as an `express` injection; broadcast RTS
gets no CTS. Real LLAP arbitrates the line.
→ Closing it means modelling collision/deferral between two real endpoints —
only meaningful with a second live node on the cable.

## 1.11 Save states and the JIT — deliberate non-serialization

Neither is a hardware deviation, but both make choices a reader should know.

- **Save states** (`SaveState.h:20-30`): callbacks and inter-device pointers
  are **re-bound by the machine after restore, never serialized**; pure caches
  (Moira's ATC, JIT blocks) are **flushed, not saved** — they are re-derivable
  from the page tables and RAM; ROM and disk images stay host-backed, with the
  snapshot carrying an identity checksum plus whatever the guest modified.
  Snapshots are same-version artifacts (header pins format version + machine
  profile; mismatch is refused). Coverage is the whole tree: `save`/`load`
  overloads for all ten machine families (`SaveStateMachines.h:75-122`), gated
  by `savestate_test`, `savestate_v8_test`, `savestate_030/040/68k_test`,
  `lcii_savestate_etalon`, `q605_savestate_etalon`.
- **JIT** (`src/jit/`, `POM68K_JIT.md`): a second execution engine, **off by
  default everywhere** — the interpreter is what every accuracy claim rests
  on. Both backends are bit-exact against it (registers, supervisor stacks,
  cycle clock, low 2 KB of RAM, compared at every instruction boundary —
  `jit_lockstep_*`). The five relaxations a classic 68k JIT makes and this one
  refuses (coarse time, coarse interrupts, a soft TLB instead of exact ATC
  semantics, lazy flags, long traces) are catalogued in `POM68K_JIT.md` § 7-8.
  → If a future non-conformant fast mode takes any of them, it belongs in
  `HLE_OVERLAY.md` with a visible flag, and this file gets an entry.

---

# 2. HLE replacements still in the tree — all fallback-only

**Every machine runs its exact factory MCU part by default.** These models have
exactly two consumers left: a no-dump install, and the explicit `*_LLE=0`
escapes.

| Device | Files | Default today | Fallback triggers |
|---|---|---|---|
| **Egret / Cuda** | `Egret.*` (HLE) vs `M68hc05.*` + `CudaLle.*` (LLE) | **Firmware LLE** on every Egret/Cuda machine: CC factory `341s0417` (2.35), Mac TV `341s0789` (2.38), LC III/III+ + IIvx/IIvi `341s0851`, LC 520/550/CC II `341s0060` (2.40 — 2.37 livelocks that ROM on pseudo-cmd `$0E`), LC II `341s0850`, Q605 `341s0788` | `POM68K_EGRET_LLE=0` / `POM68K_CUDA_LLE=0`, or missing dump |
| **ADB modem** (Mac II / IIx / IIcx, IIci, Centris + Quadra 610/650) | `AdbVia.*` (HLE byte SM on VIA SR) vs `Pic1654s.*` + `AdbLine.*` (LLE) | **Firmware LLE** when `roms/adbmodem/342s0440-b.bin` loads (`AdbVia.cpp:34-49`); `Via6522::extShiftCB1` is the wire | `POM68K_ADB_LLE=0`, or missing dump |
| **ADB bus** (Egret/Cuda machines) | `AdbBus.*` | Unused — both machines feed `AdbLine` under the firmware LLE | Retires with the Egret HLE |

Gates: `m68hc05_test`, `cuda_lle_test`, `egret_lle_test`, `egret_test`,
`pic1654s_test`, `adbline_test`, `q605_cudalle_*`, `macii_mouse_etalon`,
`input_etalon`, `family_input_etalon` (which **SKIPs rather than passes** when
a machine fell back to HLE — the gate exists to pin the firmware path).

### Retirement policy (settled 2026-07-29)

The fallbacks are **kept** — MCU dumps are user-provided and not
distributable, and without an Egret/Cuda the V8-class machines cannot boot at
all — but **never silent**: every entry into an HLE ADB path prints a
NON-CONFORMANT-substitute notice naming the missing dump (all eight machine
classes + `AdbVia::attach`). That satisfies the § Principle rule without
orphaning no-dump users. Actually deleting `Egret.*` / `AdbBus` / the
byte-model (and § 4.1's last hack with them) is a **product decision** —
"POM68K requires MCU dumps" — not a code cleanup. Take it deliberately.

**One HLE-only hack survives inside the byte-model fallback.**
`MacIIMemory.cpp:301-307`: on VIA1 ORB writes, if ACR shift-in is armed and
soft-flag bit 5 at `ADBBase($CF8)+$15D` is set, call
`Via6522::armShiftComplete()`. Required for the HLE ADB POST wait (Slot Manager
clocks SR after slot select). **Poisonous under LLE** — `$CF8` is ADBBase and
`$15D` is the ADB driver's own flag, so the hack raised a phantom SHIFT ~320
cycles after every ST write and collapsed the PIC↔VIA handshake. Gated
`!adbVia_.lle()` since 2026-07-22; never fires on a default path; dies with the
fallback.

### Reference hierarchy for the Egret/Cuda protocol

**MAME `cuda.cpp`** (LLE, real 6805 firmware — the *timing* oracle) →
**DingusPPC `viacuda.cpp` + `zdocs/developers/viacuda.md`** (HLE at our
abstraction but with real packet framing and per-byte scheduling — the *design*
oracle) → Linux `via-cuda.c` (host-side driver cross-check). Expand Cuda
commands **only from ROM/driver traces**.

### Device-model gap closed 2026-07-31 — and honestly scoped

Found during the `q605_cudalle_key_etalon` investigation: `AdbLine` answered
keyboard **register 2 with a constant** `FF FF` — "no modifier held" — where
MAME keeps a live modifier bitmap refreshed on the read
(`macadb.cpp:694-700`). A guest polling R2 for modifier state never saw
Command, Shift, Option or Control. `AdbLine` now tracks the same five keys MAME
does (Caps `$39`, Control `$36`, Shift `$38`, Option `$3A`, Command `$37`) and
reports them **active low** — 0 = held, which is what makes MAME's own `$FF`
reset value mean "nothing held". MAME's press path *sets* rather than clears
the bit; that internal inconsistency we did not copy.
`AdbLine.cpp:37,50-58,313`; `AdbLine.h:110-115`; the field travels in the
save-state `visit()`.

**This is a conformance fix, NOT a fix for the symptom that exposed it** — see
§ 1.6. Recording that precisely matters, because the next person will otherwise
re-find R2 and assume it was never done.

---

# 3. Pure LLE — and the fidelity facts that look like bugs

Pure LLE: `Via6522.*` (incl. `extShiftCB1` for Mac II ADB), `PseudoVia.*`,
`MacMemory.*` (overlay), `Rtc` serial protocol, `Ariel.h`, `MacFrame.h`,
`Pic1654s.*` + `AdbLine.*` (dump present).

**Confirmed parity, no action** (second audit): pseudo-VIA register decode +
level-triggered ASC IRQ match MAME `pseudovia.cpp`; the 60.15 Hz CA1 tick is an
independent timer in `Q605Memory::tick` like MAME IOSB's `6015_timer` (it does
**not** depend on DAFB CRTC state); extended monitor sense matches; the MEMCjr
holding protocol lives in `Q605Memory` exactly where MAME's `djmemc.cpp` puts
it. All three DAFB clock generators are modelled since 2026-07-27 (ctor variant
`Dafb::Clockgen`): Gazelle on MEMCjr (`dafb.cpp:1322`), DP8534 on djMEMC
(`:1197`), DP8531 on the Q700's discrete DAFB (`:884`) — one decoder for all
three silently froze the djMEMC pclk at the 31.3344 MHz reset value and fed the
Q700 garbage. Trace with `POM68K_DAFB_CLOCK_TRACE=1`.

Four facts that are **correctness, not shortcuts** — each cost a debugging
round when assumed otherwise:

- **Unmapped I/O reads back as 0 on the Sonora and PrimeTime maps**
  (`SonoraMemory`/`VaspMemory` catch-all, `Q605Memory` PrimeTime window; MAME
  `iosb.cpp:54-65` — no catch-all /BERR). An open-bus `$FF` hard-wedges the
  LC III+ ProductInfo RAM-device poll at `$50F0A000` bit 3 and drops the
  all-in-one ROM into its serial debugger. The Plus/Mac II 24-bit maps keep
  their /BERR on truly unmapped space — this rule is specific to the gate
  arrays MAME models the same way.
- **The Cuda's I2C bus and its DFAC2** (`CudaLle::setI2cDfac`): PB7 = SCL,
  PB6 = SDA (`cuda.cpp` `pb_w:198-199`) with a minimal slave at address `$6F` —
  START/STOP, the 9-pulse byte cadence, the ACK. The payload is discarded, which
  IS oracle parity (MAME's `dfac2_device::write_data` only logs; its registers
  are still being reverse-engineered upstream). Populated exactly where MAME
  wires a DFAC2 — Color Classic (`maclc.cpp:505`), the Cuda AIOs
  (`maclc3.cpp:403`), Quadra 630 / LC 580 (`macquadra630.cpp:196`, bus shared
  with Valkyrie) — and left EMPTY on the Q605 and Mac TV (`device_remove("dfac")`).
  Without the ACK the factory CC Cuda 2.35 takes a DFAC error path and stops
  answering the host VIA session: that is what the old "341S0417 wedges the
  M68hc05" note actually was.
- **RBV: physical low RAM IS the framebuffer, and the guest's low memory is
  elsewhere** (IIsi/IIci). RAM-Based Video displays from the start of physical
  RAM (MAME `rbv.cpp` `update_screen` reads `m_ram_ptr` with no offset), and
  the ROM enables the PMMU (IIsi `TC = $80F84500`) to relocate the System's
  logical low memory. **`peek8()` is physical**, so reading "ADBBase" /
  "Mouse" / "ScrnBase" on these machines returns desktop pixels, not globals.
  That mistake cost three rounds of a nonexistent "IIsi has no ADB" bug;
  `iisi_input_etalon` therefore asserts on screen pixels (cursor motion),
  which the MMU cannot move.
- **Factory PRAM seeding is reset-time only** — `Rtc::factoryDefaults`
  (`Rtc.cpp:15-46`, Mac II) and `Egret::factoryDefaults` (`Egret.cpp:56-100`,
  LC II / Cuda). Borderline but kept: factory PRAM is hardware-plausible and
  prevents the ROM's cold-PRAM re-init loop. Documented *policy*, not just
  code: `Rtc` writes the full Basilisk block only when `'NuMc'` is absent but
  **always** (re)seeds SPConfig XPRAM `$13` to `$22` (both ports async;
  `POM68K_APPLETALK=1` → `$21`, LocalTalk active, for headless LLAP tests).
  `Egret` is more aggressive — even with `'NuMc'` present it rewrites DynWait,
  the classic PRAM block, SPConfig (same policy) and OSDefault; cold /
  bit7-clear video sPRAM seeds `$58=$83` (8 bpp) so first boots match a
  Monitors+Restart color Mac rather than the ROM's B&W `$80`.
  **Do not grow the always-rewrite set without a gate proving the ROM path
  alone is insufficient.**

**Host convenience** (guest-invisible): `MacAudioHost.h`, GUI (`main.cpp`),
trace tools, PRAM file persistence (`<disk>.pram`), LToUDP peer bridging,
`FloppySound.*`.

- **`ScsiDisk` flat-HFS façade** (`ScsiDisk.cpp:30-146`): synthesizes an
  in-memory DDM + partition map + Apple_Driver43 in front of bare HFS `.dsk`
  images. Classified host convenience (a media-format adapter, like supporting
  a new image format), **not** an HLE hack — the guest sees a valid consistent
  SCSI disk and writes round-trip. The synthetic driver partition must stay
  byte-identical to `tools/wrap_hfs.py` output.
- **`DeclRom::buildSynthetic`** (`MacIIMemory.cpp:62-67`): with no Toby DeclROM
  dump, a minimal synthetic card ROM is installed so Slot Manager still
  enumerates video. Missing-asset fallback — prefer a real DeclROM.

---

# 4. Resolved — the history, compressed

## 4.1 HLE hacks: **the list is empty on every default path**

Every guest-state intervention POM68K ever shipped has been eliminated. The
pattern held every time: *the hack pile existed because one LLE device was
subtly wrong.* Kept as a table because the **root causes** are the reusable
part — the prose lives in the CHANGELOG entries named below.

| # | Hack that used to exist | Resolved | Root cause / where the story is |
|---|---|---|---|
| 1 | **Mac II ROM patched at load** — 3 code patches (forced StartBoot `wantType`, retargeted boot-drive matcher, `$B0E` `btst` bypass + checksum repair) | 2026-07-21 | Two wire bugs in `Rtc`: inverted /enable polarity at the Mac II call site + a one-edge-early read phase (every byte read `(v<<1)\|1`), so the ROM saw virgin PRAM every boot. `Rtc` now implements MAME macrtc semantics; the **unmodified ROM boots SCSI unaided**. CHANGELOG "LLE step 1" |
| 2 | **EvQ synthetic Return keypresses** (`postKeyReturn`, `maybeDismissBootAlerts`) | 2026-07-21 | The ADB path could always deliver keystrokes during modals (ST=EVEN wedge covered by `AdbVia::tick`'s dead-timer re-arm). Alert dismissal moved into the *tests* as real host-side ADB presses. CHANGELOG "LLE step 4" |
| 3 | **SPConfig / AppleTalk clamp re-applied every tick** (Q605/V8/MacII memory) | 2026-07-21 | Only the reset-time seed remains (§ 3). The guest may now turn AppleTalk on and the LLE SCC no-peer path handles it. CHANGELOG "LLE step 2" |
| 4 | **LocalTalk LAP watchdogs** (×2) | 2026-07-21 | Three SCC LLE gaps: RR15 reading 0, standing abort presented in async modes, no RR0 bit 4 Sync/Hunt (the LLAP carrier sense). With those + the Tx Underrun/EOM latch the LAP times out by itself. CHANGELOG "LLE step 3" |
| 5 | **Resource patch-on-load** (`ltlk` stub, `RsrcPatcher.*`) | 2026-07-21 | Dead code — never in CMakeLists, its gate never existed. Files deleted |
| 6 | **Quadra UniversalInfo FPU-bit masking** + low-mem scrub | 2026-07-21 | Unreachable since the soft-FPU path landed; the ROM's own fnop probe handles no-FPU detection (HWCfg self-clears to `$EC00`). Bare `FPUModel::NONE` boots — gate `q605_barefpu_boot_etalon` |
| 6b | **Cuda reply framing serving per-reader accommodations** (`$76` echo-pop, GetPram erase, Q8.2 echo-slot duplication, `firstTick_` heuristic) | 2026-07-22 | The wire-model redo. Replies became the real `[type, flags, cmdEcho, data…]`, and **the attention byte a wire event outside the packet buffer** on DingusPPC's measured schedule (close-ack +61 µs, attention +30, command ack +71, response byte +88, TREQ +13). That separation made the accommodations unnecessary — every reader lands on its data naturally. CHANGELOG "LLE step 7" |
| 8 | **Mac II Slot Manager ORB → phantom SHIFT** | de-facto 2026-07-22 | Still present but gated `!adbVia_.lle()` — HLE-fallback only. Full detail in § 2 |
| 10 | **`POM68K_SCC_CLEANLINE`** — machine config standing in for line state | 2026-07-28 | Env deleted from all eight memory classes. The LLE model: LocalTalk is FM0, so a **virgin line reads clean** (no edge → no recovered clock → no sampled 1s → no abort) — exactly what Open Transport's `.MPP` bind spins on. The abort condition begins with the first frame the line carries (`Scc8530::lineDriven_`). Gate `q605_ot_bind_etalon`. Recorded honestly: the OT wedge that motivated the env was already un-reproducible on that tree, so the change makes the bind *guaranteed* rather than timing-dependent |

Related and still true: **`setAbortIdle(true)` is a transport-driven line
state, not a permanent open line.** It marks a machine with no *hardwired*
peer; the moment a real peer transmits (a non-express `injectRxFrame` — an
LToUDP multicast frame, not the cable's own synthesized CTS, which stays
`express`) the SDLC line becomes a live terminated network whose idle is clean
flags, and the abort drops for a `kPeerHold` (~2 s) window refreshed per peer
frame. A solo boot never refreshes it, so the no-peer LAP timeout that lets the
boot etalons proceed is unchanged. `Scc8530::openLine()` (`Scc8530.h:309`) =
`abortIdle_ && lineDriven_ && peerHold_ <= 0`. Gate `llap_loop_test`.

## 4.2 Migration plan — steps 1-16, all closed

| Step | Work | Closed | Landmark |
|---|---|---|---|
| 1 | PRAM-seed instead of ROM-patch on Mac II | 2026-07-21 | Fix was LLE-correcting `Rtc` itself (§ 4.1 #1) |
| 2 | Delete per-tick SPConfig clamps | 2026-07-21 | Gates green clamp-free |
| 3 | SCC/SDLC no-peer timeout completion | 2026-07-21 | Hunt bit + EOM latch + mode-gated abort; watchdogs and `RsrcPatcher` deleted |
| 4 | Fix `AdbVia` ST stuck-EVEN | 2026-07-21 | EvQ machinery deleted; tests press Return over ADB |
| 5 | FPSP for bare no-FPU | 2026-07-21 | Soft-FPU is the supported config; bare NONE reaches the Finder, gated |
| 6 | DAFB → MAME parity | 2026-07-21 | LLE step 6 + `Dafb` extraction |
| 7 | Cuda wire-model redo | 2026-07-22 | § 4.1 #6b |
| 8 | SCC Rx path | 2026-07-22 | Rx FIFO, carrier-driven hunt→sync, special-condition ints, EOF CRC status; plus the mid-frame Enter-Hunt truncation fix (the empty-Chooser bug) and the inter-dialog-gap deferral |
| 9 | 53C96 + TurboSCSI fidelity | 2026-07-23 | Wait-state cell + scheduled selection/bus-service delays; remaining items audited into § 1.5's accepted list |
| 10 | Toby CRTC frame clock + **Egret/Cuda firmware LLE** | 2026-07-23/24 | `M68hc05` + `CudaLle` run the real parts. Silicon discoveries: the customized-E1 PFW input pin, the inverting ADB output stage, the Egret's falling-edge PC3 release. The LC II re-flip's autopoll desync (mouse ~1.5% delivery) was the receive path frozen inside the peripheral-tick batch — `CudaLle::mcu_.onCycles` clocks the MCU per instruction |
| 11 | Mac II ADB → firmware LLE | 2026-07-22 | Blockers fixed: PIC instruction cost, the ORB phantom SHIFT (§ 2), VIA mode-111 first-falling-edge bit7 drop |
| 12 | SCC async-baud machinery | 2026-07-23 | WR4/5/11/12-14 → guest-derived per-channel pace; SDLC derives the legacy LLAP constants exactly. Gate `scc_baud_test` |
| 13 | SWIM2/SonyDrive MFM cell timing + CRC | 2026-07-23 | MAME bit engines over a raw-cell track with real rotational latency; same-day follow-up landed the IWM write engine + GCR write-back |
| 14 | SCC no-peer abort → genuine line state | 2026-07-28 | § 4.1 #10 |
| 15 | **Factory MCU parts everywhere** | 2026-07-29 | The CC's "0417 wedge" was a missing **device** (DFAC2 I2C ACK), not a core bug; Mac TV runs its factory 341S0789. **No machine substitutes another machine's firmware** |
| 16 | HLE-fallback policy | 2026-07-29 | Kept but loud (§ 2) |

---

# 5. What is actually left, and the caveat on all of it

**The remaining LLE distance is § 1, not § 4.** In rough order of how much
correctness it buys:

1. **Whole-frame video on every machine** (§ 1.1) — no beam position. The one
   gap that makes a whole class of software *wrong* rather than approximate.
2. **Peripheral-tick batching** (§ 1.2), 64/128/256 → 4–16 µs of IRQ-latency
   jitter; and the VIA E-clock pinned at 32:1 (real ≈31.91:1).
3. **No 040 copyback/snooping** (§ 1.2); the i-cache overlays are throughput
   models, not architectural caches.
4. **The Quadra Cmd-N modifier symptom** (§ 1.6) — a known-live divergence with
   an unidentified second cause, and the only entry here that is a *bug* rather
   than a simplification.
5. **Floppy flux/PLL** (§ 1.3) — ideal cells; closing it also activates
   `Swim1`'s dead LS-pair correction machinery.
6. **SCC bit-serial sampling**, RTS pin, DPLL (§ 1.4) — only worth it with a
   real async transport to talk to.
7. **NuBus arbitration** (§ 1.8); **DAFB** VRAM arbitration and the VBL line
   hard-coded at 480; **Valkyrie's I2C pixel clock** (§ 1.1).

**Caveat, learned the hard way on 2026-07-29: this inventory is only worth what
its gates are worth.** Only 9 of the 34 profiles have any beyond-boot gate at
all — Plus (`input_etalon`), Mac II (`macii_mouse_etalon`, `macii_post_etalon`),
LC II (`lcii_soak/persist/launch/floppy/savestate_etalon`), Q605 (`q605_asc/
cdrom/dafb/turboscsi/ot_bind/savestate/cudalle_*`), the four machines the
`family_input_etalon` binary serves (`lc3_`/`lc520_`/`iivx_`/`iisi_input_etalon`)
and the IIfx
(`iifx_input_etalon`). The other **25 profiles are pinned only by "it reached
the Finder"**.

In one day the suite produced a **false green** (a positive assertion over a
too-wide window — KeyMap is 8 bytes, the scan was 16, so a dead ADB stack read
as half-working) and a **false bug** (three rounds chasing the physical-vs-
logical memory artifact in § 3). Adding fidelity on top of unverifiable
coverage is work with no way to know it landed — **the test-depth pass in
`TODO.md` outranks every item above.**

**Standing rule for anything new.** Every remaining shortcut must be (a) behind
an env flag or module toggle, (b) logged when it fires, (c) listed here, and
(d) eventually migrated into the `HLE_OVERLAY.md` framework with its visible
non-conformant-mode indicator. Save states must stamp active HLE modules.
