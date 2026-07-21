# LLE vs HLE — inventory and migration plan

**Purpose.** POM68K's direction is: harden the **LLE** core first (hardware
modeled at register/protocol level from silicon references, verified by
gates and oracles), and only later layer an **opt-in, clearly-flagged HLE
accelerator** on top (`HLE_OVERLAY.md`). That requires knowing exactly
where the current code already deviates from hardware. This document is
the complete inventory (audited 2026-07-21) and the plan to shrink it.

Line numbers are indicative — verify with grep before relying on them.

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

### 1.2 EvQ synthetic Return keypresses — `MacIIMemory.cpp:600-655`

When a modal alert is up (`CurActivate` bit 31) and SCSI has stalled,
the emulator posts a synthetic Return `keyDown` directly into the
guest's event queue once per frame to dismiss Sys 7 EtherTalk
CautionAlerts (CHANGELOG 2026-07-20). Exists because the ADB modem
(`AdbVia`) often sticks at ST=EVEN so a real synthetic click can't be
delivered. Known fragile: the same trick **double-faults System 7.5.5
on LC II** and is disabled there.
*Proper LLE:* fix the `AdbVia` state machine so host input works during
modals; with SPConfig seeded correctly at PRAM level the alerts should
not appear at all.

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

### 1.6 Quadra UniversalInfo FPU-bit masking — `Cpu040.cpp:98-111` + `Q605Memory::maybePatchRomNoFpu`

Under `POM68K_Q605_NOFPU=1`, ROM reads of UniversalInfo HWCfgFlags are
patched on the fly (bit 28 cleared) and stale low-memory copies
scrubbed, so the System behaves as on a 68LC040 while Moira still
provides a soft 68882 (SoftwareFPU-equivalent). Deliberate, gated
(`q605_nofpu_boot_etalon`), but a double guest-state intervention.
*Proper LLE:* bare `FPUModel::NONE` + a real FPSP F-line path (TODO).

### 1.7 Mac II / LC II RTC + Egret PRAM factory seeding — `Rtc.cpp:13-28`, `Egret.cpp:54-91`

Seeds `'NuMc'` validity signature + Basilisk-style defaults + SPConfig
`$22` on cold boot. **Borderline acceptable**: a device shipping with
factory PRAM contents is hardware-plausible, and it prevents the ROM's
cold-PRAM re-init loop. Keep, but treat the *contents* as a documented
policy, not scattered magic.

## 2. HLE replacements (whole devices at protocol level)

| Device | Files | What is replaced | Proper LLE would be |
|---|---|---|---|
| **Egret / Cuda** | `Egret.*` | 68HC05 MCU firmware → packet-level command emulation (ADB autopoll, RTC heartbeat, XPRAM streams, Cuda polarity flavor) | 68HC05 core + dumped Egret/Cuda firmware (big; MAME has it) |
| **ADB modem (Mac II)** | `AdbVia.*` | PIC1654S firmware → NEW/EVEN/ODD/IDLE byte state machine on the VIA SR | PIC core + firmware dump |
| **ADB bus** | `AdbBus.*` | Bit-serial ADB → command-level transactions, clamped mouse deltas | Bit-level ADB timing (only matters for exotic peripherals) |

These are pragmatic and well-gated (`egret_test`, `input_etalon`,
`macii_boot_etalon`); firmware-level LLE is a separate, large milestone
and **not** a priority — but they must stay protocol-faithful to ROM
traces (TODO: "expand Cuda commands only from ROM/driver traces").

## 3. LLE with simplification (functional, not cycle-exact)

- **CPUs**: `Cpu68k` (Plus, cycle-exact **with** RAM contention — the
  exception), `Cpu020`, `Cpu030`, `Cpu040` — 030/040 add i-cache
  *throughput overlays* (`POM68K_Q605_CACHE_BOOST`, `cacheBoost_`
  scaling in `flushTicks`) rather than architectural caches; no 040
  copyback/snooping.
- **Video**: `MacVideo.h`, `V8Video.h`, `TobyVideo.*` — whole-frame
  decode, no beam timing.
- **DAFB/Antelope** (`Q605Memory.cpp:292-367`): register-level
  (stride/config/CLUT/Swatch IRQs, MEMCjr 6+6-bit holding protocol) but
  hard-coded version, no VRAM arbitration — closest to MAME `dafb.cpp`
  parity work.
- **Floppy**: `Swim2.*` (register file + FIFO real; media transactions
  whole-sector), `SonyDrive.*` (no rotational latency), `Iwm.*` is the
  most faithful (nibble timing, tach).
- **NuBus/DeclRom**: functional slot windows, no arbitration/timeout
  cycles.
- **SCSI**: `Ncr5380.*`, `Ncr53c96.*` — register/phase engines faithful
  to MAME; pseudo-DMA handshake per-byte without bus timing.
- **Audio**: `Asc.*` FIFO semantics faithful (MODE mask, edge/level IRQ
  variants); fixed 22 257 Hz drain via fractional accumulators.

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
4. **Fix `AdbVia` ST stuck-EVEN** (kills 1.2): real ADB input during
   modals; delete `postKeyReturn`/`maybeDismissBootAlerts`.
5. **FPSP for bare no-FPU** (retires 1.6): TODO Quadra follow-up.
6. Longer term: DAFB → MAME parity, 040 copyback/snooping, Egret/Cuda
   firmware LLE (only if a use case demands it).

Every remaining hack must be: (a) behind an env flag or module toggle,
(b) logged when it fires, (c) listed here, and (d) eventually migrated
into the `HLE_OVERLAY.md` framework with its visible non-conformant-mode
indicator. Save states must stamp active HLE modules (TODO
cross-machine).
