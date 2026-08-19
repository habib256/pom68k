# HLE_OVERLAY.md — opt-in HLE accelerator (non-conformant mode)

> **STATUS: DESIGN STUDY. NOTHING HERE HAS BEEN BUILT.** Re-verified
> 2026-08-12: no `HleModule`, `HleRegistry`, `HleContext`, no signature
> scanner, no purity flag, no module UI exists in `src/` or `tests/`. Nothing
> below describes POM68K's behaviour — it describes a feature that has been
> deferred for a year and may never ship. Read it only when deciding whether
> to build it.
>
> One piece of its guardrail set *did* ship, for a different reason: the LLE
> AArch64 product mode's **session module registry and save-state stamp**
> (`src/LleSession.h`, 2026-08-12 — `LLE_VS_HLE.md` § 5). § 3's third
> guardrail and § 7's stamp are therefore no longer hypothetical; anything
> built here extends that mechanism rather than inventing one.

Design study for an **opt-in High-Level-Emulation overlay** layered *on top of*
POM68K's accurate LLE core, in the spirit of Basilisk II's targeted ROM patches:
trade timing fidelity for speed **only where the user explicitly asks**.

| Where it lives elsewhere | |
|---|---|
| backlog entry | `TODO.md` § 8 *Cross-machine architecture* — "Optional HLE acceleration overlay" |
| the *what* and *how* of Basilisk's patches | `docs/BASILISK_ROM_NOTES.md` (EMUL_OP plane §1, patch map §3, trap dispatcher §4, Egret/Cuda/ADB/PRAM stubs §5) — the implementation oracle, not restated here. **Those sections are its SECONDHAND tier** (read from Basilisk's sources, not verified on a ROM); only its §8 is firsthand. Anything here that becomes code must be re-verified against the actual ROM first |
| what POM68K already deviates on, and its policy | `docs/LLE_VS_HLE.md` — §2's "kept but LOUD" retirement policy is the precedent this overlay's visibility guardrail should follow |
| the conformant accelerator that shipped instead | `src/jit/POM68K_JIT.md` |
| the identity this must not undermine | `CLAUDE.md` |

---

## 0. Premises re-dated 2026-07-31, re-checked 2026-08-12 — read this first

This study was written when the JIT was a plan. It has since **shipped and been
measured**, which moves four of the study's load-bearing assumptions. The
design below survives; its motivating arithmetic does not. What changed:

| Written as | Reality | Consequence for this study |
|---|---|---|
| "the **planned** method-JIT" (§2, §9) | Shipped 2026-07-27 → 2026-07-31 (J0–J3 + block linking); conformant 68040 default since 2026-08-10, GUI **CPU** menu, `POM68K_CPU_ENGINE=interp\|jit` override | Every "when the JIT lands" deferral below is **due now**, not pending |
| the JIT would take the CPU-speed job, HLE the wait-elision job | `q605_boot_etalon`: **61.3 s interpreted → 22.9 s, ×2.68** (`POM68K_JIT.md` § 3). It went *through* the "~×2.5 conformant ceiling" `TODO.md` § 8 still quotes | The conformant path is faster than this study assumed. The HLE overlay's remaining value is narrower and must be re-argued, not assumed |
| the residual cost is code size / footprint | It is the **ATC-exactness contract**: 794 M window-lost exits over 12.2 G instructions — one derived-state death per ~15 instructions at the idle Finder, because Mac OS 8.1's VM ages pages by writing descriptor U bits (`POM68K_JIT.md` § 3, § 8) | This is the sharpest thing this study has learned. **It names exactly what a non-conformant mode could buy** that no conformant backend can — see § 2 |
| the JIT accelerates "the CPU", full stop | **Corrected 2026-08-12: the JIT engine is wired into all twelve CPU wrappers**, `Cpu68k` and `Cpu020` included, and there are `jit_classic_`, `jit_macii_` and `jit_lockstep_68000_*` gates. What is 68040-only is the *native* x64/A64 codegen (by declared capability) and the default policy | The "HLE is the only accelerator on the 68000 and Mac II families" argument — § 2 and § 11's second reason — is **dead**. Those families get the `threaded` backend today; measured worth is ×1.03-1.08 on 68000, ×1.0-1.2 on 68020, so the honest form of the argument is "the JIT buys them almost nothing", not "they have nothing" |

Two secondary premises also moved, in the overlay's favour:

- **Save states shipped** 2026-07-30 (`src/SaveState.h/.cpp`,
  `SaveStateMachines.h/.cpp`, **12** machine families / **37** profiles).
  § 3's "stamp the active module set" guardrail is no longer hypothetical —
  and since 2026-08-12 it is no longer even a design: `SaveStateMachines.cpp:163`
  writes `lle::snapshotFlags()` into the header and `:207-210` refuses an
  HLE-tainted restore in strict mode.
- **The "loud non-conformant mode" precedent exists.** Every HLE ADB *fallback*
  prints a `NON-CONFORMANT` notice to stderr on entry **and** registers the
  module in `pom68k::lle` — eight sites, all through the one shared
  `pom68k::fw::select` that prints it (`FirmwareChoice.h:106-110`):
  `AdbVia.cpp:51-61`, `V8Memory.cpp:164-174`, `SonoraMemory.cpp:51-67`,
  `VaspMemory.cpp:26-37`, `RbvMemory.cpp:35-47`, `Q605Memory.cpp:96-106`,
  `Q630Memory.cpp:78-88`, `Q700Memory.cpp:46-58` — per `LLE_VS_HLE.md` § 2's
  policy settled 2026-07-29. The last of those was the last exception: until
  **2026-08-14** the Eclipse Q900/Q950 registered `HleEgretCuda` *without* a
  notice, because its Egret HLE was unconditional rather than a fallback and
  no dump would have silenced it. It now runs the factory `341s0851` on a real
  68HC05 like every other Egret board, so no silent registration is left. § 7's
  visible-cheat requirement extends that mechanism; it does not invent one.

---

## 1. The one non-negotiable

POM68K's entire value proposition is *"LLE, cycle-exact on the Plus,
functionally accurate everywhere else, verified at the differential against an
oracle."* The moment an HLE patch is live, that guarantee is void: you are no
longer running *what the real Mac does*, you are running *what POM68K chose to
short-circuit*.

> **HLE is opt-in, never the default, and never active during any oracle or
> accuracy gate. LLE remains the source of truth. HLE is an assumed
> "fast-forward" — visible, reversible, and with no standing in the conformance
> suite.**

If that frontier blurs, the overlay slowly rots the LLE path (bitrot of hardware
nobody exercises anymore) and dissolves the project's identity. Keeping the
frontier explicit — in code, in the UI, and in the test gates — is what makes the
feature a net win instead of a slow-acting poison.

The JIT was built under the same rule and is the working proof it holds: the
proved conformant engine is now the 68040 default, while the interpreter stays
the declared and explicitly tested reference (`POM68K_JIT.md` § 2,
invariants 1 and 3). Other guest families remain on the interpreter.

---

## 2. What HLE could still buy, after the JIT

HLE does **not** speed up the CPU — the JIT does, and did (§ 0). HLE eliminates
what *accurate* emulation makes the machine **wait on**, and — the post-JIT
insight — what *exactness* makes the engine **re-derive**.

| Module (id) | Nature of the win | Value | Risk |
|---|---|---|---|
| `boot.checksum`, `boot.memtest` | Skip multi-MB cold scans at power-on | Medium (boot) | **Low** |
| `boot.hwprobe` | Elide VIA/ADB/Egret/Cuda detection spins | Medium (boot) | Medium |
| `disk.sony` / SCSI Manager | Replace register polling + pseudo-DMA with a block `pread` | **High** | Medium |
| `time.delay` (Time Manager, `Delay`, VIA spin-waits) | Skip the busy-wait instead of burning cycles | **High** | **High** |
| `sound.mix` | Native mix instead of sample-by-sample ASC/PWM buffer fill | Medium | Medium |
| `serial.host` | Host serial passthrough (only if serial is in use) | Low/situational | Medium |

Three observations that must steer prioritisation:

1. **The two richest targets (disk, timed waits) diverge most from real
   timing** — the § 3 symmetry. Hence per-item opt-in, never a blanket "HLE on."
2. **`disk.sony` is orthogonal to the JIT** — a JIT does not speed up a
   `pread` — and is the one item whose value the JIT's arrival did not touch.
   It should be the first *visible* module for that reason alone.
3. **`time.delay` is no longer just "overlapping with the JIT" — it is the one
   place where relaxing exactness is the whole point.** The JIT's measured
   floor at the idle Finder is derived-state churn forced by ATC exactness
   (§ 0), and it refuses by charter the five relaxations a classic 68k JIT
   makes: coarse time, coarse interrupts, a soft TLB instead of exact ATC
   semantics, lazy flags, long traces (`POM68K_JIT.md:22-25`). A
   **non-conformant mode is where those relaxations are legal.** Whether that
   belongs in *this* overlay (guest-level, patch a Toolbox routine) or as a
   relaxed JIT profile (host-level, keep the ATC-derived state across evictions)
   is the open question — and it is a real fork in the road, not a detail.
   Nothing here should be built before it is settled.

Non-goal worth stating: this overlay is **not** a way to make the JIT faster on
the machines it already covers. It is (a) boot-time and I/O latency everywhere,
and (b) the only *large* win available on the 68000 and Mac II families, where
the JIT is wired but buys ×1.03-1.08 and ×1.0-1.2 respectively.

---

## 3. The frontier with the project's DNA (the real risk)

Three failure modes, each with its mandated guardrail:

1. **Bitrot of the LLE path.** If disk-HLE becomes the comfort mode, nobody
   exercises the NCR5380 pseudo-DMA / SWIM path and its LLE bugs reawaken months
   later. → *Guardrail:* accuracy gates always run in **purity mode** (§ 7) with
   HLE assertively disabled.
2. **HLE hiding an LLE bug.** A title that only works with disk-HLE may be masking
   a genuine IWM/SCSI emulation bug. → *Guardrail:* every module ships an **A/B
   gate** (§ 7) that boots HLE-off *and* HLE-on and compares the functional result;
   a divergence opens an LLE ticket, it is not shrugged off.
3. **Determinism & save-states.** A state saved HLE-on is not replayable HLE-off —
   the short-circuited hardware has no coherent state. → *Guardrail:* save-states
   **stamp the active module set** and refuse/warn on reload mismatch (§ 7). The
   container to stamp exists today (§ 0).

The symmetry behind all three, and the reason per-module opt-in is not
negotiable: **Basilisk's speed does not come from "magic ROM pages."** It comes
from having deleted the hardware emulation beneath those pages. Any speed the
overlay buys is exactly the timing fidelity it gives up.

Treat HLE as an **assumed degraded mode**, like a console fast-forward: powerful,
visible, reversible, and with **zero** standing in the conformance suite.

---

## 4. What Basilisk II actually does (and what we borrow)

Basilisk II is not "LLE plus a few patches" — it is **full native HLE**, and ROM
patching is merely its attach mechanism: an `EMUL_OP` trap plane at `$7100-$71FF`
(illegal `MOVEQ` encodings, chosen to stay *out* of the A-line, which on a Mac is
the Toolbox — § 5.1), installed at ROM load by **byte-signature scan** rather than
at hardcoded addresses, replacing whole subsystems (the patched routine *is* the
driver; the chips underneath are not emulated at all) and neutralising the ROM
checksum and hardware probes because neither would survive. Mechanism, patch map
and per-device stubs: `BASILISK_ROM_NOTES.md` §§ 1, 3.2, 3.3, 5 — the
implementation oracle, not restated here.

**What we borrow:** the trap-plane idea and, above all, the **signature-scan
discipline** — matching by pattern is what lets one build survive many ROM
revisions. **What differs:** in POM68K the hardware *does* exist and is
accurately emulated, so our HLE short-circuits *selected* paths whose faithful
emulation is expensive while everything else stays LLE. That is a **more modest,
safer point on the LLE↔HLE spectrum** than Basilisk's all-native design — a
feature, not a limitation.

---

## 5. How it plugs into *this* codebase

### 5.1 The trap mechanisms that exist — and what each actually costs

Checked against the vendored core on 2026-07-31. The study's original claim
("`willExecute` fires before every instruction") is **wrong for this build**:

| Hook | Where | Reality |
|---|---|---|
| `willExecute(func, Instr, Mode, Size, opcode)` | `Moira.h:872` (virtual) / `:1015`, called at `MoiraExec_cpp.h:12` | Gated by `if constexpr (MOIRA_WILL_EXECUTE)`, and this vendor's macro is `I == Instr::STOP \|\| I == Instr::TAS \|\| I == Instr::BKPT` (`MoiraConfig.h:89`). **Not a per-instruction hook** unless that macro is widened — which puts a test on every instruction, i.e. the very cost § 5.2 tries to avoid |
| `didReachSoftwareTrap(addr)` | `Moira.h:974` (virtual) / `:1066`, fired from `execLineA` (`MoiraExec_cpp.h:47`) | A real trap plane — keyed on opcode in `debugger.swTraps`, restores the original instruction before calling back. But it is **A-line only**, and on a Mac the A-line *is* the Toolbox trap space. Basilisk chose `$71xx` precisely to stay out of it |
| `MoiraDebugger` breakpoints / softstops | `MoiraDebugger.*`, checked at `Moira.cpp:667` | **The zero-cost-when-unused route**: `State::CHECK_BP` is set only while a breakpoint exists (`MoiraDebugger.cpp:190-192`), so an unarmed build pays one already-hot flag test. **Caveat:** the check sits at the `done:` label — *after* the instruction retires, reporting `reg.pc0`. An entry-address hook therefore fires with the routine's first instruction **already executed**; the handler must either account for that or hook the instruction before the entry |

Also corrected: `Cpu68k` derives from `MoiraSnapshot` (`src/MoiraSnapshot.h:32`),
not from `moira::Moira` directly; `MacMemory::loadRom` is at
`src/MacMemory.cpp:39`.

### 5.2 Two attach strategies — support both

| Strategy | Advantage | Cost |
|---|---|---|
| **Address hook** (Moira breakpoint, ROM byte-for-byte intact) | No checksum problem; trivially reversible on toggle; the **v1 choice** | The post-retire firing point (§ 5.1); a PC test per step, amortised by the debugger's table; trickier under the JIT (block invalidation) |
| **ROM byte-patch** (write a trap opcode into `rom_`) | Zero per-instruction cost | Breaks the ROM checksum → must also neutralise the checksum routine; a patched ROM region is self-modifying code to the JIT → **must invalidate its cached blocks** (§ 8) |

Recommendation: **address hook for v1** (no checksum handling, instant toggle).
Move a module to byte-patch only with *patch-before-compile* + *region
invalidation* guaranteed.

A byte-patch pass would run inside `MacMemory::loadRom` (`src/MacMemory.cpp:39`)
and its per-machine equivalents, **after** the signature scan and **before** the
first fetch or JIT compile sees the page.

### 5.3 Module structure — "one concern per file"

Each HLE feature is a single self-describing file (project convention), declaring
its signature, handler and applicability:

```cpp
// HleModule.h — common interface
struct HleModule {
    std::string_view id;           // "disk.sony", "boot.checksum", "time.delay"
    std::string_view label;        // UI label
    std::string_view accuracyNote; // shown in UI ("disables real IWM timing")
    MachineMask      machines;     // Plus | LCII | Quadra…
    RomMatch         match;        // byte signature → attach address
    void (*install)(HleContext&);  // set the hook (patch or breakpoint)
    void (*handler)(HleContext&);  // native code run at the trap
    bool             enabled = false; // ← the checkbox
};
```

A static `HleRegistry` enumerates modules. On ROM load: **scan by signature —
never a hardcoded address** (the Basilisk lesson that survives ROM revisions,
`BASILISK_ROM_NOTES.md` §1, §3.3), then install only those that are *checked*
**and** whose signature *matched the loaded ROM*. On a non-matching signature,
`log()` it and grey the module — **never patch silently at a guessed address**.

### 5.4 Per-machine applicability

The 37 shipping profiles span 12 platform implementations with different ROMs
and different drivers (IWM vs SWIM1 vs SWIM2, NCR 5380 vs 53C96, M0110 vs
PIC1654S vs Egret vs Cuda vs PG&E — roster in `CLAUDE.md`, reachability in
`docs/68K_FAMILY_SCOPE.md`). `machines` gates a module to the families it
understands; `RomMatch` further gates it to the specific ROM the scan
recognises. `SnapMachine` (`src/MachineCatalog.h:35-49`) already enumerates
the 37 profiles and is the natural basis for `MachineMask`.
A module with no signature hit on the loaded ROM is inert and greyed, not a
hazard.

---

## 6. Candidate module notes

Ordered as they should be built (§ 9). Attach addresses/signatures for the LC II
and Plus ROMs are catalogued in `BASILISK_ROM_NOTES.md` §3 (patch map) and §5
(Egret/Cuda) — that document is the implementation oracle.

- **`boot.checksum` / `boot.memtest`** — cheapest, lowest-risk, ideal first proof
  of the whole mechanism (hook + registry + purity mode) on an anodyne case.
  Basilisk's patch points: `BASILISK_ROM_NOTES.md` §3.2.
- **`disk.sony`** (or SCSI Manager) — the first *visible* win, and the one item
  the JIT cannot substitute for (§ 2). Native block access to the mounted image
  instead of IWM/SWIM GCR or NCR 5380 / 53C96 pseudo-DMA polling. Ships with its
  A/B gate from day one.
- **`boot.hwprobe`** — elide VIA/ADB/Egret/Cuda detection spins; speeds cold boot.
  Note this is where the overlay overlaps most with the firmware-LLE MCUs that
  are now the *default* on every ADB machine — a probe elided here is an
  `M68hc05` path not exercised.
- **`time.delay`** — Time Manager / `Delay` / VIA spin-wait elision. Biggest raw
  win, biggest divergence. **Do not build before the § 2.3 fork is settled.**
- **`sound.mix`, `serial.host`** — situational, later.

---

## 7. Guardrails (keeping the oracle discipline)

- **Purity mode.** A global `HLE_FORBIDDEN` flag set by every accuracy gate —
  the boot etalons (48 gate names end in `boot_etalon`), the SST vector suites
  (`sst68000`/`sst68030`/`sst68040`), the JIT locksteps. Any attempt to install
  an HLE hook while the flag is set **`abort()`s**. This makes it *mechanically
  impossible* for an oracle gate to be "helped." The JIT does not need the
  lock: its invariant 3 is "the fastest **proved conformant** engine is the
  default per guest family" (`POM68K_JIT.md` § 2), so switching engines is
  conformance-neutral. The overlay needs the lock precisely because it is not.
- **Per-module A/B gate.** One CTest per module runs the scenario HLE-off then
  HLE-on and compares the **functional** outcome (reaches the Finder, reads the
  correct block) — *not* cycle-exact, since HLE changes timing by construction. A
  divergence fails the gate and opens an LLE bug ticket.
- **Save-state stamp.** The active module set is serialised into the save-state
  header beside the existing format version and `SnapMachine` tag
  (`src/SaveState.h`, `src/SaveStateMachines.h`); reload refuses (or loudly
  warns) on mismatch.
- **Scan logging.** Every expected-but-unmatched signature is `log()`ged and the
  module greyed. No silent patch at a wrong address, ever.
- **Status indicator.** A permanent "cheat" light in the status bar whenever ≥1
  module is active, plus the stderr `NON-CONFORMANT` line the HLE ADB fallbacks
  already print (`LLE_VS_HLE.md` §2) — so no bug is ever reported from an
  unknowingly-HLE state.

---

## 8. Interaction with the JIT (no longer hypothetical)

The JIT shipped, so these are constraints against real code rather than
predictions. Source: `src/jit/POM68K_JIT.md` §§ 4, 8, 9.

- **Orthogonal:** I/O-HLE (`disk.sony`, serial, SCSI Manager) — a JIT does not
  speed up host I/O; these stay valuable, unchanged.
- **Overlapping, and now a fork:** timing-HLE (`time.delay`) — see § 2.3. The
  JIT collapses busy-loops *conformantly*; the question is whether the remaining
  win justifies a guest-level patch or a relaxed engine profile.
- **The invalidation machinery already exists.** `jit::CodeGuard` watches guest
  writes at **256-byte** slice granularity (`JitGuard.h:28-62`) and
  `Engine::serviceGuard()` evicts only the blocks overlapping a written slice
  (`JitEngine.cpp:492`). A byte-patch module writing into `rom_` must go through
  that path, or patch before the first compile.
- **An A-line or illegal-opcode hook exits a JIT block cleanly for free.** The
  classifier marks the **whole A-line** and the `$4Exx` group `Unsafe`
  (`POM68K_JIT.md` § 4), so such an instruction is already a block boundary and
  is handed back to `Moira::execute()`. A hook placed there needs no new JIT
  work. A hook on an *ordinary* instruction does not get that for free.
- **Address-hook modules and the code window.** Breakpoints are checked in
  `Moira::execute()` (`Moira.cpp:667`), which a replayed or generated block
  bypasses by design. An address hook must therefore either force the block
  boundary itself or arm through the engine — this is the one genuinely new
  integration item, and it is why § 5.2 keeps both strategies alive.

---

## 9. Phasing (do not build the framework first)

1. **Settle the § 2.3 fork** — guest-level timing-HLE vs a relaxed JIT profile.
   This is now the *first* step, not the last, because it decides whether the
   overlay's biggest module belongs here at all.
2. **One module, one hidden flag** — `boot.checksum` via **breakpoint** (ROM
   intact), no UI. Validates the dispatch path (including § 5.1's post-retire
   firing point) + registry + purity mode. Small, disposable, proves everything.
3. **Generalise** the `HleModule` interface + `HleRegistry` + signature scan,
   once the mechanism is proven on that first case.
4. **Add `disk.sony`** (the first win people can see) with its A/B gate.
5. **UI** + save-state stamp + status indicator.

---

## 10. UI

A separate ImGui window **"Acceleration (HLE)"**, distinct from normal settings,
with a warning banner at the top ("Enabling an accelerator leaves faithful mode;
conformance is no longer guaranteed"). Checkboxes grouped **Boot / Disk / Timing /
Sound**, each with its `accuracyNote` as a tooltip and **greyed when the signature
does not match the loaded ROM/machine**. A master **"All LLE (faithful)"** switch
clears everything — and is the **first-launch default**. The status indicator of
§ 7 mirrors the active state at all times.

Precedent to follow, not re-invent: the **CPU** menu already switches execution
engines live and names the active backend in the GUI (`main.cpp`, per
`POM68K_JIT.md`). The difference is that switching engines is conformance-neutral
and switching on an HLE module is not — which is exactly what the extra banner,
the greying and the permanent indicator are for.

---

## 11. Bottom line

The mechanism is *cheap* given Moira's breakpoint hooks and POM68K's
one-concern-per-file layout — most of the real work is **disciplinary**, not
technical: purity mode, A/B gates, stamped save-states, and a visible cheat
indicator.

What the JIT changed is the *case*, not the design. The conformant engine
already reaches ×2.68 on the Quadra 605 boot (`POM68K_JIT.md` § 3), so the
overlay can no longer be justified as "the way to make POM68K fast." Its
remaining, honest arguments are three, and the middle one has weakened since
this was written: **host I/O latency the JIT cannot touch** (`disk.sony`); **the
68000 and Mac II families, where the JIT is wired but worth ×1.03-1.08 and
×1.0-1.2 respectively** (§ 0); and
**the exactness contract itself** — the one thing a conformant backend may never
relax, and the measured floor of the JIT at the idle Finder. Anything built here
should be built for one of those three reasons, and for no other.

The genuine risk is unchanged: not writing the code, but letting HLE's
convenience erode the LLE path that gives the project its worth. As long as the
conformance suite runs in locked faithful mode, POM68K keeps both the speed
(when the user asks for it) and the truth (a core that still does *what the real
Mac does*).
