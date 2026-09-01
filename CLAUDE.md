# CLAUDE.md — repository orientation

This is the always-loaded map of POM68K. It tells a contributor where a fact
belongs, which invariants must survive a change, and how to verify the result.
It deliberately does not carry dated investigations, generated counts or
subsystem tutorials.

POM68K aims to support every 68k Macintosh. Current coverage is **37 machine profiles**
across 12 board implementations, from the Macintosh Plus to the
Quadra 950, with every catalogue entry booting to the Finder when its
user-provided ROM and system media are available. The compiled catalogue is
`kMachineProfiles` in `src/MachineCatalog.h`; it is the authority, not this
summary.

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
| Moira provenance and local patch inventory | `extern/moira/POM68K_VENDOR.md` |
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
| `Compact` | Plus, SE, SE FDHD, Classic | `MacMemory.*`, `Cpu68k.*`, `PlatformCompact.cpp` |
| `Glue` | Mac II, IIx, IIcx, SE/30 | `MacIIMemory.*`, `Cpu020.*`, `PlatformToby.cpp` |
| `Oss` | IIfx | `IIfxMemory.*`, `IIfxCpu.*`, `ApplePic.*` |
| `V8` | LC, LC II, Classic II, Color Classic, Mac TV | `V8Memory.*`, `Cpu030.*`, `PlatformV8.cpp` |
| `Rbv` | IIsi, IIci | `RbvMemory.*`, `RbvCpu.*` |
| `Sonora` | LC III/III+, LC 520/550, Color Classic II | `SonoraMemory.*`, `SonoraCpu.*`, `PlatformSonora.cpp` |
| `Vasp` | IIvx, IIvi | `VaspMemory.*`, `VaspCpu.*` |
| `MemcJr` | LC 475/575, Quadra 605 | `Q605Memory.*`, `Cpu040.*`, `PlatformDafb.cpp` |
| `DjMemc` | Centris 610/650, Quadra 610/650/800 | `CentrisMemory.*`, `CentrisCpu.*`, `PlatformDafb.cpp` |
| `Spike` | Quadra 700/900/950 | `Q700Memory.*`, `Q700Cpu.*`, `PlatformDafb.cpp` |
| `F108` | Quadra 630, LC/Performa 580 | `Q630Memory.*`, `Q630Cpu.*`, `PlatformDafb.cpp` |
| `Msc` | PowerBook Duo 230 | `MscMemory.*`, `MscCpu.*`, `PgePmu.*`, `PlatformDuo.cpp` |

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
| `RASPBERRY_PI.md` | Pi tuning, packaging and PGO |
| `SIMPLIFICATIONS_REVIEW.md` | reviewed implementation simplifications |

## Product and architecture invariants

- **ROMs and system media are private inputs.** Never commit them or package
  them. `assets.lock` records identities, not payloads.
- **Machine time is guest CPU time.** Device deadlines and events use
  emulated cycles, never host wall time or boosted core cycles.
- **The interpreter is the accuracy oracle.** An accelerated path must match
  architectural state and timing at the boundary its gate claims.
- **HLE fallback is never silent.** Devices report whether firmware LLE or a
  fallback is active; strict LLE mode refuses a mixed-provenance session.
- **Save and load share one visitor.** Pointers/callbacks are rebound, caches
  are flushed, and disk payloads remain host-owned.
- **Reference media remain immutable.** Repository fixtures under `hdv/ref/`
  are read-only; writable GUI sessions use `hdv/work/` clones.
- **One concern per owner.** Extend a focused class/module instead of growing
  another global registry or composition root.
- **Every durable milestone has a gate.** A test that soft-skips proves only
  asset detection unless its named asset was actually present.

## CPU and JIT policy

The engine and backend are separate decisions:

- 68000 and 68020 guests default to the Moira interpreter.
- 68030 and 68040 guests default to the accelerated engine.
- `threaded` is always compiled and is valid for every guest family.
- AArch64 `auto` selects native `a64` for 68030 and 68040.
- Non-Windows x86-64 `auto` selects native `x64` for 68040; a 68030 uses
  `threaded` until the withdrawn x64 promotion is re-earned.
- Windows x64 automatic builds omit the System V emitter and use `threaded`.
- Explicit `POM68K_CPU_ENGINE=interp|jit` and
  `POM68K_JIT_BACKEND=auto|threaded|x64|a64` remain diagnostic/user overrides.

Backend `guestFamilies` declares correctness; `autoFamilies` declares measured
automatic policy. Never infer one from the other. The declarations in each
backend's `caps()` implementation are the current source of truth.

The optional architectural 68040 I/D-cache model is enabled with
`POM68K_040_DCACHE=1`; it remains outside the product default while its
cache-active native paths are being completed and measured.

## Development workflow

Start with the concise loop in `DEV.md`:

```bash
./setup_imgui.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel
ctest --test-dir build -L asset-none --output-on-failure
git diff --check
```

Select the smallest relevant tier while iterating, then widen in proportion
to the change. `DEV.md` §6 owns tier definitions; `STATUS.md` owns current
registry facts. `docs_test` holds cross-document/code contracts, and
`file_size_budget_test` ratchets architecture-sensitive translation units.

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

- Documentation is written in English; conversation with the user may be in
  French.
- The whole tree is C++20.
- POM68K is GPLv3. Moira and Dear ImGui retain their own licenses.
- Preserve existing user changes in a dirty worktree and avoid destructive
  Git operations.
- Comments explain non-obvious contracts and cite a primary source or a
  regression gate; dated narrative belongs in the changelog.
- Do not add live process-environment reads below the startup boundary.
- Do not make a native backend the default from opcode coverage alone:
  conformance, whole-tier stability and same-process performance evidence are
  independent admissions.

External hardware sources, in order:

1. MAME Apple machine/device models and the Motorola 68k family.
2. *Guide to the Macintosh Family Hardware* and *Inside Macintosh III*.
3. Hatari/WinUAE for CPU timing and differential behaviour.
4. pce-macplus, Mini vMac and Basilisk II as behavioural cross-checks.

On a specification/oracle conflict, preserve the reproducer and record the
ruling in `CHANGELOG.md` or the relevant research note.

## Moira is a permanent fork

`extern/moira/` came through NeoST from Dirk Hoffmann's Moira. POM68K's MMU,
FPU, snapshot, timing and JIT seams make it a maintained fork rather than an
unchanged dependency. Upstream changes are ported or cherry-picked
deliberately; do not merge/revendor over the local tree.

`extern/moira/POM68K_VENDOR.md` owns the decision, patch inventory and resync
procedure. The WinUAE/Hatari oracle under `oracle/` remains the differential
reference for the local 68030/68040 extensions.
