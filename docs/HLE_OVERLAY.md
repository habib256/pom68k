# HLE_OVERLAY.md — Accélérateur HLE (mode non conforme)

Design study for an **opt-in High-Level-Emulation overlay** layered *on top of*
POM68K's accurate LLE core, in the spirit of Basilisk II's targeted ROM patches:
trade timing fidelity for speed **only where the user explicitly asks**. This is
the long-form companion to the `TODO.md` § *Idea — Accélérateur HLE (mode non
conforme)* entry.

Companion docs:
- `BASILISK_ROM_NOTES.md` — the concrete oracle for *what* Basilisk patches and
  *how* (EMUL_OP plane, fixed-offset patch map §3, trap dispatcher §4,
  Egret/Cuda/ADB/PRAM stubs §5). This document reuses its findings rather than
  restating them; section references below point into it.
- `CLAUDE.md` — the project's LLE-first, oracle-wins identity that this overlay
  must not undermine.

**Status: idea / not started.** Nothing here is built. This file exists so the
design is pinned before any code lands.

---

## 0. The one non-negotiable

POM68K's entire value proposition is *"LLE, cycle-exact on the Plus, functionally
accurate on the LC II / Quadra, verified at the differential against two
oracles."* The moment an HLE patch is live, that guarantee is void: you are no
longer running *what the real Mac does*, you are running *what POM68K chose to
short-circuit*.

Therefore the overlay is governed by a single hard rule, from which every design
decision below descends:

> **HLE is opt-in, never the default, and never active during any oracle or
> accuracy gate. LLE remains the source of truth. HLE is an assumed
> "fast-forward" — visible, reversible, and with no standing in the conformance
> suite.**

If that frontier blurs, the overlay slowly rots the LLE path (bitrot of hardware
nobody exercises anymore) and dissolves the project's identity. Keeping the
frontier explicit — in code, in the UI, and in the test gates — is what makes the
feature a net win instead of a slow-acting poison.

---

## 1. What Basilisk II actually does (and what we borrow)

Basilisk II is not "LLE plus a few patches" — it is **full native HLE**, and ROM
patching is merely its attach mechanism. The relevant pieces (detail in
`BASILISK_ROM_NOTES.md`):

- **The `EMUL_OP` trap plane** — Basilisk reserves opcodes `$7100–$71FF`. These
  are *illegal* encodings of `MOVEQ` (`MOVEQ` is `0111 rrr 0 dddddddd`; bit 8
  must be 0, and at `$71xx` it is 1). The CPU takes them as illegal instructions;
  the handler decodes the low byte as an index and jumps into native C++.
- **Signature-based patching** — at ROM load, Basilisk scans for *byte patterns*
  (checksum entry, memory test, `.Sony`, `SCSIDispatch`, `CudaDispatch`, …) and
  writes an `EMUL_OP` at each entry. Matching by pattern, not by hardcoded
  address, is what lets one build survive many ROM revisions
  (`BASILISK_ROM_NOTES.md` §1, §3.3).
- **Whole-subsystem replacement** — the patched routine doesn't drive emulated
  hardware; it *is* the driver. `.Sony` reads a block from an image file with a
  host `pread`; the SCSI Manager, serial, Ethernet, Time Manager, sound and ADB
  are all native. The chips underneath are **not emulated at all**.
- **Boot neutralisation** — Basilisk patches out the ROM checksum and the
  hardware probes (VIA/ADB/memory sizing) because the patched ROM no longer
  checksums and the expected chips don't exist (`BASILISK_ROM_NOTES.md` §3.2).

**What we borrow:** the `EMUL_OP` trap-plane idea and, above all, the
**signature-scan discipline**. **What differs:** in POM68K the hardware *does*
exist and is accurately emulated. Our HLE therefore short-circuits *selected*
paths whose faithful emulation is expensive, while everything else stays LLE.
That is a **more modest, safer point on the LLE↔HLE spectrum** than Basilisk's
all-native design — a feature, not a limitation.

Key corollary, easy to get wrong: **Basilisk's speed does not come from "magic
ROM pages."** It comes from having deleted the hardware emulation beneath those
pages. Any speed the overlay buys is exactly the timing fidelity it gives up.
That symmetry is why every module must be individually opt-in.

---

## 2. Where HLE actually buys speed in POM68K

HLE does **not** speed up the CPU — the planned method-JIT (see `TODO.md`
§ Performance) does that. HLE eliminates what cycle-exact emulation makes the
machine **wait on**. Candidates, ranked by value/risk:

| Module (id)        | Nature of the win                                             | Value  | Risk   |
|--------------------|--------------------------------------------------------------|--------|--------|
| `boot.checksum`, `boot.memtest` | Skip multi-MB cold scans at power-on           | Medium (boot) | **Low** |
| `boot.hwprobe`     | Elide VIA/ADB/Egret/Cuda detection spins                     | Medium (boot) | Medium |
| `disk.sony` / SCSI Manager | Replace register polling + pseudo-DMA with a block `pread` | **High** | Medium |
| `time.delay` (Time Manager, `Delay`, VIA spin-waits) | Skip the busy-wait instead of burning cycles | **High** | **High** |
| `sound.mix`        | Native mix instead of sample-by-sample ASC/PWM buffer fill   | Medium | Medium |
| `serial.host`      | Host serial passthrough (only if serial is in use)           | Low/situational | Medium |

Two observations that must steer prioritisation:

1. The two richest targets (disk, timed waits) are also the two that **diverge
   most from real timing** — consistent with §1's symmetry. Hence per-item opt-in,
   never a blanket "HLE on."
2. **JIT overlap.** `time.delay`-style busy-wait elision competes with what the
   JIT already accelerates, so **defer timing-HLE until the JIT lands and re-decide
   then.** Conversely, `disk.sony` is **orthogonal to the JIT** (a JIT does not
   speed up a `pread`) and stays worthwhile regardless. When the JIT arrives, keep
   I/O-HLE, reconsider timing-HLE.

---

## 3. The frontier with the project's DNA (the real risk)

Three failure modes, each with its mandated guardrail:

1. **Bitrot of the LLE path.** If disk-HLE becomes the comfort mode, nobody
   exercises the NCR5380 pseudo-DMA / SWIM path and its LLE bugs reawaken months
   later. → *Guardrail:* accuracy gates always run in **purity mode** (§6) with
   HLE assertively disabled.
2. **HLE hiding an LLE bug.** A title that only works with disk-HLE may be masking
   a genuine IWM/SCSI emulation bug. → *Guardrail:* every module ships an **A/B
   gate** (§6) that boots HLE-off *and* HLE-on and compares the functional result;
   a divergence opens an LLE ticket, it is not shrugged off.
3. **Determinism & save-states.** A state saved HLE-on is not replayable HLE-off —
   the short-circuited hardware has no coherent state. → *Guardrail:* save-states
   **stamp the active module set** and refuse/warn on reload mismatch (§6).

Treat HLE as an **assumed degraded mode**, like a console fast-forward: powerful,
visible, reversible, and with **zero** standing in the conformance suite.

---

## 4. How it plugs into *this* codebase

POM68K's architecture already provides both anchor points.

### 4.1 The trap mechanism already exists

`Cpu68k : public moira::Moira` (`src/Cpu68k.h`), and Moira exposes exactly the
hooks needed (`extern/moira/Moira/Moira.h`):

- `virtual void willExecute(const char *func, Instr, Mode, Size, u16 opcode)` —
  fired **before every instruction**. This is the EMUL_OP dispatch point: if
  `opcode` is in our reserved trap plane, dispatch native and advance PC;
  otherwise fall through.
- `virtual void willExecute(M68kException exc, u16 vector)` and
  `didReachSoftwareTrap(u32 addr)` — the "illegal exception" route, if we prefer
  not to touch the hot `willExecute` path.
- The `MoiraDebugger` breakpoint / softstop / catchpoint machinery (already
  vendored, `extern/moira/Moira/MoiraDebugger.*`) — a route that patches **no ROM
  bytes at all**: set a native breakpoint on a routine's entry address and replace
  it live.

### 4.2 Two attach strategies — support both

| Strategy | Advantage | Cost |
|---|---|---|
| **Address hook** (Moira breakpoint, ROM byte-for-byte intact) | No checksum problem; trivially reversible on toggle; the **v1 choice** | A PC test per step, amortised by the debugger's table; trickier vs a JIT (block invalidation) |
| **ROM byte-patch** (write a trap opcode into `rom_`) | Zero per-instruction cost; the right choice **once the JIT exists** | Breaks the ROM checksum → must also neutralise the checksum routine; a patched ROM region behaves like self-modifying code to a method-JIT → **must invalidate its cached blocks** (document this in the future JIT's `POM68K_VENDOR.md`) |

Recommendation: **address hook for v1** (no checksum handling, instant toggle).
Move a module to byte-patch only when the JIT makes the per-step test measurable,
and only with *patch-before-compile* + *region invalidation* guaranteed.

`MacMemory::loadRom` (`src/MacMemory.cpp:9`) mirrors the ROM into `rom_`; a
byte-patch pass would run there, **after** the signature scan and **before** the
first fetch/JIT sees the page.

### 4.3 Module structure — "one concern per file"

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
    void (*install)(HleContext&);  // set the accroche (patch or breakpoint)
    void (*handler)(HleContext&);  // native code run at the trap
    bool             enabled = false; // ← the checkbox
};
```

A static `HleRegistry` enumerates modules. On ROM load: **scan by signature —
never a hardcoded address** (the Basilisk lesson that survives Plus / LC II /
Quadra ROM revisions, `BASILISK_ROM_NOTES.md` §1, §3.3), then install only those
that are *checked* **and** whose signature *matched the loaded ROM*. On a
non-matching signature, `log()` it and grey the module — **never patch silently at
a guessed address**.

### 4.4 Per-machine applicability

Plus / LC II / Quadra have different ROMs and different drivers (IWM vs SWIM,
NCR5380 vs 53C96, VIA vs Egret vs Cuda). `machines` gates a module to the
machines it understands; `RomMatch` further gates it to the specific ROM the scan
recognises. A module with no signature hit on the loaded ROM is inert and greyed,
not a hazard.

---

## 5. Candidate module notes

Ordered as they should be built (§7). Attach addresses/signatures for the LC II
and Plus ROMs are catalogued in `BASILISK_ROM_NOTES.md` §3 (patch map) and §5
(Egret/Cuda) — that document is the implementation oracle.

- **`boot.checksum` / `boot.memtest`** — cheapest, lowest-risk, ideal first proof
  of the whole mechanism (trap + registry + purity mode) on an anodyne case.
  Basilisk's checksum/memtest patch points: `BASILISK_ROM_NOTES.md` §3.2.
- **`disk.sony`** (or SCSI Manager) — the first *visible* win. Native block access
  to the mounted image instead of IWM/SWIM GCR or NCR5380/53C96 pseudo-DMA
  polling. Ships with its A/B gate from day one.
- **`boot.hwprobe`** — elide VIA/ADB/Egret/Cuda detection spins; speeds cold boot.
- **`time.delay`** — Time Manager / `Delay` / VIA spin-wait elision. Biggest raw
  win, biggest divergence. **Do not build before the JIT decision** (§2).
- **`sound.mix`, `serial.host`** — situational, later.

---

## 6. Guardrails (keeping the oracle discipline)

- **Purity mode.** A global `HLE_FORBIDDEN` flag set by every accuracy gate
  (`lcii_boot_etalon`, `q605_boot_etalon` when it lands, `macqd605` runs, the SST
  vector suites). Any attempt to install an HLE accroche while the flag is set
  **`abort()`s**. This makes it *mechanically impossible* for an oracle gate to be
  "helped."
- **Per-module A/B gate.** One CTest per module runs the scenario HLE-off then
  HLE-on and compares the **functional** outcome (reaches the Finder, reads the
  correct block) — *not* cycle-exact, since HLE changes timing by construction. A
  divergence fails the gate and opens an LLE bug ticket.
- **Save-state stamp.** The active module set is serialised into the save-state;
  reload refuses (or loudly warns) on mismatch.
- **Scan logging.** Every expected-but-unmatched signature is `log()`ged and the
  module greyed. No silent patch at a wrong address, ever.
- **Status indicator.** A permanent "cheat" light in the status bar whenever ≥1
  module is active, so no bug is ever reported from an unknowingly-HLE state.

---

## 7. UI

A separate ImGui window **"Acceleration (HLE)"**, distinct from normal settings,
with a warning banner at the top ("Enabling an accelerator leaves faithful mode;
conformance is no longer guaranteed"). Checkboxes grouped **Boot / Disk / Timing /
Sound**, each with its `accuracyNote` as a tooltip and **greyed when the signature
does not match the loaded ROM/machine**. A master **"All LLE (faithful)"** switch
clears everything — and is the **first-launch default**. The status-bar voyant of
§6 mirrors the active state at all times.

---

## 8. Phasing (do not build the framework first)

1. **One module, one hidden flag** — `boot.checksum` via **breakpoint** (ROM
   intact), no UI. Validates `willExecute` dispatch + registry + purity mode.
   Small, disposable, proves everything.
2. **Generalise** the `HleModule` interface + `HleRegistry` + signature scan, once
   the mechanism is proven on that first case.
3. **Add `disk.sony`** (the first win people can see) with its A/B gate.
4. **UI** + save-state stamp + status voyant.
5. **JIT decision** — settle timing-HLE vs JIT, and rule on
   patch-before-compile / block invalidation for byte-patch modules; record the
   ruling in the JIT's `POM68K_VENDOR.md`.

---

## 9. Interaction with the planned JIT

- **Orthogonal:** I/O-HLE (`disk.sony`, serial, SCSI Manager) — a JIT does not
  speed up host I/O; these stay valuable.
- **Overlapping:** timing-HLE (`time.delay`) — the JIT already collapses
  busy-loops; re-evaluate whether the HLE variant still earns its accuracy cost.
- **Structural constraint:** byte-patch modules turn a ROM region into
  effectively self-modifying code from the JIT's viewpoint. The ROM **must** be
  patched before the JIT compiles the page, and any later toggle **must** invalidate
  the affected code blocks. Address-hook modules avoid this but pay a per-step
  check. This trade-off is the reason both attach strategies (§4.2) exist and must
  survive into the JIT era.

---

## 10. Bottom line

The mechanism is *cheap* given Moira's existing `willExecute` / breakpoint hooks
and POM68K's one-concern-per-file layout — most of the real work is
**disciplinary**, not technical: purity mode, A/B gates, stamped save-states, and
a visible cheat indicator. The genuine risk is not writing the code; it is letting
HLE's convenience erode the LLE path that gives the project its worth. As long as
the conformance suite runs in locked faithful mode, POM68K keeps both the speed
(when the user asks for it) and the truth (a core that still does *what the real
Mac does*).
