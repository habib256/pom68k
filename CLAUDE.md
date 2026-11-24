# CLAUDE.md — repository orientation

This is the always-loaded map of POM68K. It tells a contributor where a fact
belongs, which invariants must survive a change, and how to verify the result.

POM68K aims to support every 68k Macintosh. Current coverage is
**58 machine profiles** across 16 board implementations, from the 128K to the
Quadra 950, each booting to the Finder given its user-provided ROM and system
media. The compiled catalogue, `kMachineProfiles` in `src/MachineCatalog.h`,
is the authority, not this summary.

## Read first

| Need | Authoritative document or source |
|---|---|
| Install, ROM placement, launch, controls and releases | `README.md` |
| Developer setup, architecture, platforms and environment options | `DEV.md` |
| Current gate registry and recorded executions | generated `STATUS.md` |
| Open work only | `TODO.md` |
| Dated decisions, corrections and measurements | `CHANGELOG.md` and generated `CHANGELOG_INDEX.md` |
| Machine roster, ROM identity and stable save-state ids | `src/MachineCatalog.h` |
| Startup-option schema and lifecycle | `src/StartupOptions.h`, `src/RuntimeConfig*` |
| JIT design, conformance and performance evidence | `src/jit/POM68K_JIT.md` |
| Moira base, patch queue and host-side contract | `extern/moira/patches/`, `extern/moira/POM68K_VENDOR.md`, `POM68K_SURFACE.md` |
| Guest-side Retro68 utilities | `dev/README.md` |

Do not duplicate generated gate totals in this file, README or TODO. Read
`STATUS.md`, and regenerate it with `tools/status_md.py` after a registration
change. Add changelog entries newest-first and regenerate their topic index
with `python3 tools/changelog_index.py`.

## Repository map

| Path | Responsibility |
|---|---|
| `src/` | emulator core, devices, platform composition and GUI |
| `src/jit/` | host-neutral JIT engine, IR, policies and backends |
| `tests/` | unit, lockstep and machine-etalon executables |
| `cmake/` | gate registration, labels, manifests and developer targets |
| `docs/` | one focused research or design note per topic |
| `tools/` | asset checks, measurement, media and host integration tools |
| `oracle/` | WinUAE-based 68k differential oracle and fuzz workflow |
| `extern/` | vendored/forked dependencies with provenance records |
| `packaging/` | Linux, macOS and Raspberry Pi packaging logic |
| `share/` | guest-side binaries shipped beside the executable (the Disques agent) |
| `.github/workflows/` | CI, nightly, release and on-demand package jobs |

The main application composition is intentionally one-way:

```text
ProcessEnvironment
        ↓ typed StartupSnapshot
RuntimeConfig
        ↓
MachineFactory → MachineSession → platform core + MachineHost + GuiShell
```

Only `ProcessEnvironment` reads process-global configuration. Domain decoders
consume the immutable startup snapshot; platform/device code receives typed
configuration by injection and must not call `getenv`.

## Platform map

One platform is one memory map, CPU wrapper and device graph. Marketing models
sharing that hardware remain separate catalogue/save-state identities.

| Platform kind | Profiles | Main owners |
|---|---|---|
| `Compact` | 128K, 512K, 512Ke, Plus, SE, SE FDHD, Classic | `MacMemory.*`, `Cpu68k.*`, `PlatformCompact.cpp` |
| `Glue` | Mac II, II FDHD, IIx, IIcx, SE/30 | `MacIIMemory.*`, `Cpu020.*`, `PlatformToby.cpp` |
| `Oss` | IIfx | `IIfxMemory.*`, `IIfxCpu.*`, `ApplePic.*` |
| `V8` | LC, LC II, Classic II, Color Classic, Mac TV | `V8Memory.*`, `Cpu030.*`, `PlatformV8.cpp` |
| `Rbv` | IIsi, IIci | `RbvMemory.*`, `RbvCpu.*` |
| `Sonora` | LC III/III+, LC 520/550, Color Classic II | `SonoraMemory.*`, `SonoraCpu.*`, `PlatformSonora.cpp` |
| `Vasp` | IIvx, IIvi | `VaspMemory.*`, `VaspCpu.*` |
| `MemcJr` | LC 475/575, Quadra 605 | `Q605Memory.*`, `Cpu040.*`, `PlatformDafb.cpp` |
| `DjMemc` | Centris 610/650, Quadra 610/650/800 | `CentrisMemory.*`, `CentrisCpu.*`, `PlatformDafb.cpp` |
| `Spike` | Quadra 700/900/950 | `Q700Memory.*`, `Q700Cpu.*`, `PlatformDafb.cpp` |
| `F108` | Quadra 630, LC/Performa 580 | `Q630Memory.*`, `Q630Cpu.*`, `PlatformDafb.cpp` |
| `Niagra` / `Pangola` | PowerBook 160, 165, 180 / 165c, 180c | `Pb030Memory.*`, `Pb030Cpu.*`, `Pb030Niagra.h`, `PangolaVideo.*`, `PlatformPb030.cpp` |
| `Jaws` / `Portable` | PowerBook 140, 145/145B, 170 / Macintosh Portable, PB100 | `Pb030Memory.*`, `Pb030Cpu.*`, `PlatformPb030.cpp` / `PortableMemory.*`, `PortableCpu.*`, `PlatformPortable.cpp` |
| `Msc` | PowerBook Duo 210, 230, 250, 270c, 280, 280c; PowerBook 150 | `MscMemory.*`, `MscCpu.*`, `PgePmu.*`, `PlatformDuo.cpp` |

Detailed address maps, clocks, device graphs and bring-up constraints belong
in `DEV.md` §2. The catalogue decides identity; the platform implementations
must not grow a second ROM/profile registry.

## Subsystem routing

| Area | Start here |
|---|---|
| CPU integration and bus-time rules | `DEV.md` §1.2–1.3 |
| Save-state serialization contract | `DEV.md` §1.4, `SaveState*`, `MoiraSnapshot.h` |
| Floppy and SCSI | `DEV.md` §3.1–3.4 |
| Keyboard, mouse and ADB | `DEV.md` §3.5–3.6 |
| Egret, Cuda and firmware LLE | `DEV.md` §3.7, `docs/LLE_VS_HLE.md` |
| SCC, LocalTalk and AppleTalk | `DEV.md` §3.8, `docs/APPLETALK.md` |
| Video beam and DAFB | `DEV.md` §3.8bis–3.9 |
| GUI/thread ownership and relaunch | `DEV.md` §6, `MachineHost.h`, `GuiShell*` |
| Debugger; session files | `DEV.md` §5–6, `DebugSession.*`, `GuiDebuggerWindow.*`, `SessionFile.h` |
| Startup and diagnostic knobs | `DEV.md` §5, `config_knobs.tsv` |
| JIT | `src/jit/POM68K_JIT.md` |

Focused research notes under `docs/`:

| File | Topic |
|---|---|
| `68K_FAMILY_SCOPE.md` | reachability of other 68k Macintosh families |
| `APPLETALK.md` | AppleTalk, LocalTalk, bridges and protocol behaviour |
| `BASILISK_ROM_NOTES.md` | ROM behaviour cross-checks |
| `CACHE_040.md` | 68040 cache model and cache-aware JIT contract |
| `DUO_BRINGUP.md` | MSC and PG&E Power Manager |
| `HLE_OVERLAY.md` | optional non-conformant HLE acceleration study |
| `IOP_BRINGUP.md` | IIfx and Eclipse Apple PIC IOPs |
| `JIT_BRINGUP.md` | historical JIT bring-up investigations |
| `LC520_BRINGUP.md` | all-in-one Sonora family bring-up |
| `LCII_HARDWARE.md` | LC II hardware blueprint |
| `LLE_VS_HLE.md` | current LLE/HLE inventory and qualification rules |
| `MAME_PARITY_AUDIT.md` | chip-level POM68K/MAME comparison |
| `MEASURING.md` | rules for publishable performance measurements |
| `MOIRA_CONVERGENCE.md` | the fork's 158 changes sorted against Dirk Hoffmann's rewritten Moira (the base) |
| `RASPBERRY_PI.md` | Pi tuning, packaging and PGO |
| `SCSI_HOTPLUG.md` | live SCSI attach, guest-side mount/unmount and the bus view the GUI needs |
| `SIMPLIFICATIONS_REVIEW.md` | reviewed implementation simplifications |
| `SNOW_COMPARISON.md` | feature comparison with the Snow emulator, pinned to both revisions |
| `SNOW_IMPLEMENTATION_PLAN.md` | the work selected from that comparison and its admission rules |

## Product and architecture invariants

- **ROMs and system media are private inputs.** Never commit them or package
  them. `assets.lock` records identities, not payloads.
- **Machine time is guest CPU time.** Device deadlines and events use
  emulated cycles, never host wall time or boosted core cycles.
- **The interpreter is the accuracy oracle.** An accelerated path must match
  architectural state and timing at the boundary its gate claims.
- **HLE fallback is never silent.** Devices report whether firmware LLE or a
  fallback is active; strict LLE mode refuses a mixed-provenance session.
- **Save and load share one visitor.** Rebind pointers/callbacks, flush derived
  caches, restore architectural 040 cache bytes, and keep disk payloads host-owned.
- **Reference media remain immutable.** Repository fixtures under `hdv/ref/`
  are read-only; writable GUI sessions use `hdv/work/` clones.
- **One concern per owner.** Extend a focused class/module instead of growing
  another global registry or composition root.
- **Every durable milestone has a gate.** A test that soft-skips proves only
  asset detection unless its named asset was actually present.

## Evidence grades — written into the code

**The silicon is the reference.** Every claim about hardware carries **its
grade and its source**, in one bracket at the head of the comment, so a reader
and a `grep` agree on what backs the code:

| Grade | Evidence | For POM68K |
|---|---|---|
| **A** | Measured on **real hardware** | A scope or capture on a real Plus / LC II / Quadra. |
| **B** | **Primary document** | *Guide to the Macintosh Family Hardware*, *Inside Macintosh*, the Motorola 68000/68030/68040 and 68882 manuals, chip datasheets, Apple technical notes. |
| **C** | **ROM disassembly** | Apple's own ROM source (`elliotnunn/mac-rom`), and the ROM images themselves. Proof of the contract the software expects. |
| **D** | **Another emulator** | MAME, Hatari, WinUAE, pce-macplus, Mini vMac, Basilisk II — **all equal**. A reading of the hardware by someone else: useful, fallible. |
| **E** | **Asserted with no source.** | |
| **F** | **Admitted unknown** | guessed, tuned, approximate, unverified. |

Three rules follow. **A grade-D citation names its revision** (tag or commit):
upstream line numbers move on every commit, so a bare `file.cpp:248-253`
designates nothing. **A lower grade never overrules a higher one** — where
POM68K diverges from MAME or WinUAE because a manual, a ROM or a measurement
says otherwise, that is correct, and the comment says so; it protects the code
from a future "alignment" that would be a regression. **An F is kept, not
hidden**: state what would settle it.

```
// [B] MC68030 User's Manual § 7.1.3 — the ATC is flushed on PMOVE to TC.
// [D] MAME m68kcpu.cpp:1204-1219 @ mame0287 — same, as MAME reads it.
// [C] mac-rom Plus 4D1F8172 $400A1C — the trap expects D0 preserved.
// [A] Measured on a real Plus: 783 ns per bus cycle.
// [F] Unknown: tuned until MacPaint stopped tearing. Settled by: a capture.
```

This hierarchy is shared by every emulator from the same author (POM1,
POMIIGS, POM68K, POMPPC, NeoST, RasterSystemGG). On a specification/oracle
conflict, preserve the reproducer and record the ruling in `CHANGELOG.md`.

## CPU core

### Moira: Dirk's core is the base

`extern/moira/Moira` is Dirk Hoffmann's Moira (`master`, the commit in
`extern/moira/patches/BASE`) plus POM68K's patch queue, one commit each in
`extern/moira/patches/`, written in his style for upstream pull requests
(discussion #36); the Macintosh layer lives above it, in `src/MoiraCompat.h`
and the CPU wrappers. Before any patch, fetch his `master`, read his new
commits and drop a patch he covers. Branch `main_old` (`0139930a`) freezes
the previous POM68K (the NeoST fork, JIT on): a reference to compare against
and the source for rebuilding the JIT, never a branch to develop on. `main`
proves the interpreter first, then rebuilds the JIT on it piece by piece
(`TODO.md` § Objectif principal). `extern/moira/POM68K_VENDOR.md` holds the
queue's inventory and the fork's history; `docs/MOIRA_CONVERGENCE.md` sorts
the fork's 158 changes against his rewrite; `tools/dirk_moira/` replays the
oracle vectors and the reproducers on his unmodified tree. The WinUAE/Hatari
oracle (`oracle/`) stays the 030/040 differential reference; the fork's CPU
tests run against classified known lists (`tests/known/`).

### Validating the interpreter

- **Qualify in native mode** (`Compatibility::DEFAULT`), then replay the same
  minimal case under the modes `MOIRA_COMPATIBILITY` offers (`UAE` per CPU
  model, `MAME` for the 68000, `MUSASHI` where it applies). Record the CPU
  model, engine revisions, build options and the first divergent state or cycle.
- **A divergence opens an investigation, not a verdict.** A compatibility mode
  measures agreement with its own reference; MAME and the SingleStepTests
  vectors it generated count as one source.
- **The model's Motorola manual decides** where it specifies the behaviour (B).
  Exact cycle order, an update before a fault or bits the manual leaves
  undefined need a capture on that CPU (A). Record the reference, the minimal
  case and the expected result; keep an uncertain one explicitly open (F).
- **Fix what is demonstrated**, with a targeted native-mode regression test.
  Totals and vector gains measure agreement with a corpus, not correctness;
  keep the criteria intact and name the tests that did not run.

### JIT policy

**Since 2026-11-23 the JIT is compiled out (`POM68K_JIT=OFF`, the default)**
while it is rebuilt on Dirk's core; every machine runs on the interpreter and
the JIT gates are recorded absent (`cmake/pom68k_jit_gates.tsv`). The policy
to restore keeps engine and backend as separate decisions: 68030/68040 default
to the accelerated engine, 68000/68020 run on the interpreter only (the 68000
with its fetch window, `POM68K_JIT.md` § 2.2); `threaded` is always compiled
and correct for every family; `auto` selects native `a64` on AArch64 and `x64`
on non-Windows x86-64 for 68030/68040, `threaded` on Windows x64.
`POM68K_CPU_ENGINE=interp|jit` and `POM68K_JIT_BACKEND=auto|threaded|x64|a64`
remain diagnostic/user overrides. Backend `guestFamilies` declares correctness, `autoFamilies` measured
automatic policy; never infer one from the other (each backend's `caps()` is
the source of truth). Opcode coverage alone never makes a native backend the
default: conformance, whole-tier stability and same-process performance are
independent admissions. The architectural 68040 I/D-cache model
(`POM68K_040_DCACHE=1`) stays outside the product default until its
cache-active native paths are completed and measured.

## Development workflow

Start with the concise loop in `DEV.md`:

```bash
./setup_imgui.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel 4   # bounded: a bare --parallel exhausts memory
ctest --test-dir build -L asset-none --output-on-failure
git diff --check
```

Select the smallest relevant tier while iterating, then widen in proportion
to the change. `DEV.md` §6 owns tier definitions; `STATUS.md` owns current
registry facts. `docs_test` holds cross-document/code contracts, and
`file_size_budget_test` ratchets architecture-sensitive files.

Before considering a change complete:

1. Build every touched production/test target.
2. Run the narrow regression plus the appropriate label tier.
3. Confirm that required-asset gates executed instead of soft-skipping.
4. Run `git diff --check` and review the complete diff.
5. Update `TODO.md` only when open work changed.
6. Add a newest-first `CHANGELOG.md` entry when the reason, evidence or
   historical state is worth preserving; regenerate `CHANGELOG_INDEX.md`.
7. Regenerate `STATUS.md` only when gate registration or recorded execution
   data changed.

## Conventions

- Documentation and product UI are in English (conversation may be in
  French); the whole tree is C++20.
- POM68K is GPLv3; its own code in the Moira release is MIT (`LICENSE-MIT`).
- Preserve existing user changes in a dirty worktree and avoid destructive
  Git operations.
- Comments explain non-obvious contracts and cite a primary source or a
  regression gate; dated narrative belongs in the changelog.
- Do not add live process-environment reads below the startup boundary.
