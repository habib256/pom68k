# POM68K — Macintosh 68k emulator

**Project mission: support every 68k Macintosh.** The roster below is current
coverage, not a closed product list.

**37 machine profiles, every one boots the Finder** — from the Macintosh
Plus (68000, cycle-exact) to the Quadra 950 tower (68040), by way of the
PowerBook Duo 230 laptop. Sibling of
[POMIIGS](../POMIIGS/) (Apple IIgs) and [POM2](../POM2/) (Apple II),
sharing their architecture and conventions. CPU core:
[Moira](https://github.com/dirkwhoffmann/Moira) (vendored via NeoST —
`extern/moira/POM68K_VENDOR.md`).

The full machine list is the **ROM → machine** table below. The compiled
source of truth is `src/MachineCatalog.h` (`kMachineProfiles`): the Machine
menu, ROM identity and stable save-state id all consume each row. The 37 rows
are present coverage, never a ceiling on the project mission.

| Document | For |
|---|---|
| this file | build, ROMs, launch, keys, CLI |
| `DEV.md` | internals, pinned tests, **the complete environment-knob list** |
| `TODO.md` / `CHANGELOG.md` | backlog / resolved items and the *why* |
| `docs/` | per-subsystem deep dives (`APPLETALK.md`, `LLE_VS_HLE.md`, …) |
| the **ROM → machine** table below | what goes where under `roms/` (the directory itself is gitignored) |

## Build

```bash
./setup_imgui.sh                  # one-time: fetch Dear ImGui, create build/
cd build && cmake .. && make -j
ctest -j64                        # 238 gates, 20 min here (4 h 46 sequential).
                                  # `-j` is a RAM budget in 256 MiB slots, not a
                                  # core count. 4 of 228 soft-skip without assets
ctest -L unit                     # 113 legacy non-etalon gates
ctest -L asset-none               # 88 manifest-declared asset-free gates
ctest -L smoke                    # 9 gates, one machine (Q605), both CPU engines
ctest -L jit-fast                 # asset-free native A64/x64 proof + doc/config checks
ctest -L etalon-core              # 12 gates, one profile per platform, ~32 min
```

The counts are the documented registry's (238 total); five lockstep gates
are host-conditional — three AArch64-only, two x86-64-only
(`jit_lockstep_030_x64_experimental_test` + `_alignment_test`) — so an
x86-64 configure sees 235 / 110 unit / 8 smoke and an AArch64 one 236
(`CMakeLists.txt`;
`docs_test` asserts the numbers against the configured registry). `unit` means "name does
not end in `_etalon`", not "needs no assets" — several unit gates want a ROM or
an SST vector set and soft-skip without one.

CTest's generated `pom68k_gate_manifest.tsv` gives every gate independent
asset/host/scope/tier dimensions. Reference media under an exact `ref/` path
are never opened writable: a normal `hdv/<name>` probe prefers
`hdv/ref/<name>` when present, and `ScsiDisk` clones the first writable GUI
open once into sibling `hdv/work/`. Without the reference twin, the historical
direct-write behaviour remains available.
`assets.lock` names the 37 private identities qualified by the green gate
corpus: 5 MCU firmwares, 24 machine ROMs, 2 declaration ROMs and 6 immutable
reference disks. `python3 tools/verify_assets.py` verifies every present entry;
`--strict` requires the complete set, and the schema refuses a reference disk
outside `hdv/ref/`. It also requires every catalogue profile to map to exactly
one `machine-rom` row.
`performance_budgets.tsv` keys policy by workload, guest CPU family and host
profile (architecture for deterministic daily gates, named reference host for
wall-clock baselines). The asset-free A64/x64 jobs emit the same
`pom68k.jit.metrics.v1` JSON schema and archive it as
`pom68k-jit-metrics-{aarch64,x86_64}`; `tools/check_jit_performance.py`
checks those artifacts. Fixed-cycle Plus/68000, Mac II/68020, LC II/68030 and
Q605/68040 baselines are kept separately from the synthetic daily tripwires.

Requires CMake ≥ 3.16, a C++20 compiler, and — for the GUI target only — GLFW
≥ 3.3 + OpenGL. Without `imgui/` the GUI target is simply not declared
(`CMakeLists.txt:372`, `if(EXISTS "${IMGUI_DIR}/imgui.cpp")`) and the core
library plus the tests still build, which
is what headless CI does.

Optional: a profile-guided build measured **−33 % on the interpreter** and
−18 % on the JIT (Quadra 605 boot), bit-identical emulation. The helper keeps
the three steps in one directory and, on Clang/AppleClang, merges the runtime
profiles with `llvm-profdata` (rationale + flags: `CMakeLists.txt:173-234`):

```bash
tools/pgo_train.sh build-pgo
cmake --build build-pgo -j4
```

### On a Raspberry Pi

Compiling on the Pi itself beats the generic AppImage: the ISA floor rises to
the exact core instead of plain armv8-a, and `--pgo` adds the profile on top.
The script reads `/proc/device-tree/model` for the core (Pi 5 → `cortex-a76`,
Pi 4/400 → `cortex-a72`, Pi 3 → `cortex-a53`), sizes `-j` against the board's
RAM, and installs without ever overwriting a disk image:

```bash
packaging/raspberry/build_native_pi.sh --pgo          # → build-pi/POM68K
sudo packaging/raspberry/build_native_pi.sh --pgo --install   # → /opt/pom68k
```

Recipe, measurements and their provenance, and the way a mis-set-up PGO build
fails **silently**: `docs/RASPBERRY_PI.md`.

### Picking the execution engine on the command line

`auto` selects native code generation for 68040 and 68030 guests on both
x86-64 and AArch64. The portable `threaded` window serves the remaining
JIT-enabled families and hosts without executable memory. Speedups, the
lockstep gates that pin them and the PGO numbers live in one place:
`src/jit/POM68K_JIT.md`.

```bash
POM68K_CPU_ENGINE=jit POM68K_JIT_BACKEND=auto     ./build/POM68K
POM68K_CPU_ENGINE=jit POM68K_JIT_BACKEND=threaded ./build/POM68K   # portable fallback
POM68K_CPU_ENGINE=jit POM68K_JIT_BACKEND=a64      ./build/POM68K   # explicit native
POM68K_JIT_PROFILE=conservative                   ./build/POM68K   # proof/replay policy
POM68K_JIT_PROFILE=instrumented                   ./build/POM68K   # counters + validation
```

### Strict LLE product mode (`--lle-aarch64`)

`--lle-aarch64` forces both of those selections and, before the session opens,
checks the firmware the profile needs against the compiled manifest
(`src/FirmwareManifest.h`, mirrored by the four product-firmware rows in the
broader `assets.lock`, which additionally records each dump's qualified
profiles) by size **and** SHA-256, not merely
by "it loaded": Cuda 341s0788 for the Q605 family, Cuda 341s0060 for the
Q630 family, PIC1654S 342s0440-b for Centris/Quadra, Egret 341s0851 for
the Eclipse towers (Q900/Q950). It exits with status
**2** and a `REFUSÉ` diagnostic when the native backend or a dump is missing,
when a `*_LLE=0` forces the HLE fallback, or when the profile is not a
qualified 68040 platform — an HLE-fallback session is never announced as full
LLE.

Qualification covers the whole session: `src/LleSession.h` holds a central
register that invalidates it the moment any HLE module activates, and save
states (format v4) stamp that provenance, so a strict session refuses a state
taken under HLE. `--lle-aarch64-check` runs the preflight and exits before
GLFW, for CI. The Eclipse towers (Q900/Q950) joined qualification with
their Egret firmware LLE on 2026-08-14. Design notes: `docs/LLE_VS_HLE.md`.

Private CI that holds the assets configures `-DPOM68K_PRODUCT_LLE_GATES=ON`
(default OFF; AArch64 + the GUI target, or configuration fails) and runs
`ctest -L product`: missing assets become configure-time errors, five
preflights (Q605/Centris/Q700/Q900/Q630) plus the four boots pinned to the
AArch64 backend with their interpreted oracles kept alongside, and the
three negative refusals (missing firmware, forced fallback, Q900
forced-HLE) are gated too.

## Releases (prebuilt packages)

Tagged releases build four artifacts in CI (`.github/workflows/release.yml`,
adapted from POM1's battle-tested workflows):

| Package | Target |
|---|---|
| `POM68K-<v>-x86_64.AppImage` | any Linux with glibc ≥ 2.27 (Mint 19.x and newer) |
| `POM68K-<v>-aarch64.AppImage` | aarch64 Linux, **Raspberry Pi 400/4/5 included** (Pi OS bookworm) — LTO, scheduled for the Cortex-A72, generic armv8-a floor |
| `POM68K-macOS-v<v>.dmg` | macOS 11+, Universal 2 (Apple Silicon + Intel) |
| `POM68K-Windows-v<v>.zip` | Windows x64, self-contained (no DLL beside the exe) |

A fifth package is built **on demand**, not per tag — *Raspberry Pi package
(-mcpu)* in the Actions tab, one core per run:

| Package | Target |
|---|---|
| `POM68K-<v>-pi400-aarch64.AppImage` | Pi 4/400 (or Pi 5 / Pi 3 — you pick the core). ISA floor **raised** to that core, so it will not run on a lesser one |
| `pom68k-<v>-pi400-aarch64.tar.gz` | the same build for Pi OS Lite / a kiosk: Pi OS bookworm does not install `libfuse2`, and an unpacked tree needs none |

On a **Pi 4/400, take the tar.gz but prefer the release AppImage** to the
`pi400` one: raising the ISA floor to `cortex-a72` was measured to produce
**byte-identical code** to the release build's `-mtune=cortex-a72` (27 bytes
differ out of 8 698 128 — build-id and version string, nothing else), and the
release artifact also runs everywhere else. The workflow re-measures this on
every run and reports the verdict. `docs/RASPBERRY_PI.md` § 1bis.

No ROM ships in any package — drop your own dumps into `roms/`, `hdv/`,
`disks35/`. The **AppImage looks for those beside itself first**: create
`roms/` next to the `.AppImage` file and everything, writes included (PRAM,
save states, screenshots), stays there — a portable install on a USB stick
works. Failing that it uses the directory you launched from if it holds one of
those folders, and failing that `~/.local/share/POM68K`, which it seeds on
first run with a README saying so. `POM68K_DATA_DIR=<path>` overrides all
three, and the chosen directory is printed on stderr at every launch
(`packaging/linux/AppRun`). The macOS `.app` always uses
`~/Library/Application Support/POM68K` — its launcher `cd`s there
(`package_macos_release.sh:76`, at the repo root); the Windows zip has no
launcher and resolves everything against the working directory, which is the
`.exe`'s own folder when you double-click it. `POM68K --version` prints the
banner and exits before any window — that is the CI smoke and a quick
install check.
Maintainers: run the *Build bionic builder image* workflow once, pin the
digests it prints into `release.yml`, then push a version tag.

A `disks35/` image is listed in the **Disques** window and can be inserted at
any time, but it goes into the drive at power-on **only when there is no SCSI
disk to boot from**. A Mac boots whatever medium is in the drive, so inserting
a System 6 floppy automatically used to make the 040 machines come up on "This
startup disk will not work on this Macintosh model" instead of the Finder.
`POM68K_FLOPPY=<path>` is an explicit instruction and inserts one regardless.

## Run

```bash
./build/POM68K [ROM] [boot disk] [extra disks…]
```

ROMs are copyrighted and **never** part of the repository. Put yours under
`roms/` (names and checksums in the **ROM → machine** table below; the
directory is gitignored); with none at all, POM68K runs a built-in
hand-assembled 68000 demo that clears the boot overlay through the VIA and
animates the 512×342 framebuffer.

**Without a ROM argument** the app looks for, in order: `roms/maclcii.rom`,
a `35C28F5F` scan of `roms/`, `roms/macplus.rom`, `roms/macii.rom`,
`roms/quadra605.rom`, then the `9779D2C4` and `FF7439EE` scans
(`src/MachineFactory.cpp:194-206`). The default machine is therefore the **Mac LC II**.
Each path is tried against the working directory, the executable's directory
and its parent (`findPath`, `src/MachineFactory.cpp:84-92`); the signature scan walks
`roms/` recursively and matches the CRC32 hex **in the file name**
(`findRomBySignature`, `src/MachineFactory.cpp:95-120`), which is how Apple dumps are
normally named — rename yours to something else and only the exact short name
or an explicit ROM argument will find it.

### ROM → machine

Dispatch is by ROM **size**, then by the header checksum (the first four
bytes, big-endian), then by a typed `MachineSelectionConfig` for models that
share a ROM and differ only by clock / CPU / model ID. `RuntimeConfig` creates
that value once from the documented environment selectors. Its internal
machine decoder is separate from the product and core-policy decoders; the
public composition root contains no environment-key spelling and never reads
the live process. A single typed startup-option schema, including the JIT,
supplies both these decoders and the process-capture boundary. Every row also
declares its value policy and numeric bounds; domain- and value-constrained
views make a cross-domain read or an incompatible decode a compile-time error.
`ProcessEnvironment` seals the captured strings into a `StartupSnapshot` whose
map is private and whose raw names are validated. `RuntimeConfig` and the JIT
receive only a const reference to that snapshot and can read it solely through
typed `StartupOption` symbols.
`src/MachineFactory.cpp:123-167` owns this selection; the **Machine** menu
relaunches with the typed `--machine-profile=<slug>` option and never mutates
the process environment.
The **Périphériques (LLE / HLE)** window follows the same rule: it relaunches
with typed `--firmware-override=<adb|egret|cuda>:<lle|hle>:<path>` arguments.
An empty path explicitly restores automatic firmware discovery. The historic
`POM68K_*_LLE` and `POM68K_*_FW` variables remain accepted startup syntax.

| Size | Checksum | Machine(s) | Selector |
|---|---|---|---|
| 128 KB | — | Macintosh Plus | |
| 256 KB | `B2E362A8` | Macintosh SE | |
| 256 KB | `B306E171` | Macintosh SE FDHD | |
| 512 KB | `A49F9914` | Macintosh Classic | |
| 256 KB | `9779D2C4` `97851DB6` *(and any other 256 KB dump)* | Macintosh II (68020) | |
| 256 KB | `97221136` | **IIx** (default) / IIcx / SE/30 / II FDHD | `POM68K_MACII_MODEL=iix\|iicx\|se30\|fdhd` — the SE/30 also needs `roms/se30/se30vrom.uk6` (video decl ROM) |
| 512 KB | `4147DD77` | **Macintosh IIfx** (68030 @ 40 MHz) | OSS + two Apple PIC IOPs; no built-in video — boots on the slot-9 Toby card. System ≤ 7.6 (32-bit-dirty ROM) |
| 512 KB | `368CADFE` | Macintosh IIci | |
| 512 KB | `36B7FB6C` | Macintosh IIsi | |
| 512 KB | `350EACF0` | Macintosh LC | |
| 512 KB | `35C28F5F` | Macintosh LC II *(also the fallback for any other 512 KB dump)* | |
| 512 KB | `3193670E` | Macintosh Classic II | |
| 1 MB | `ECD99DC0` | Macintosh Color Classic | |
| 1 MB | `EAF1678D` | Macintosh TV | |
| 1 MB | `ECBBC41C` `EC904829` | **LC III** (default) / LC III+ (33 MHz) | `POM68K_LC3_PLUS=1` |
| 1 MB | `EDE66CBD` | **LC 520** (default) / LC 550 / Color Classic II | `POM68K_AIO_ID=A55A0101\|CC2` |
| 1 MB | `4957EB49` | **IIvx** (default) / IIvi (16 MHz) | `POM68K_IIVI=1` |
| 1 MB | `FF7439EE` *(and any other 1 MB dump)* | **LC 475** (default) / LC 575 / Quadra 605 — all three emulated at **25 MHz**: `Q605Memory::kCpuHz` is a constant and the LC 575's "33 MHz" is only a banner/menu label | `POM68K_Q605_ID=A55A222E\|A55A2225` |
| 1 MB | `F1A6F343` `F1ACAD13` | **Centris 650** (default) / Centris 610 / Quadra 610 / 650 / 800 | `POM68K_CENTRIS_MODEL=c610\|c650\|q610\|q650\|q800` |
| 1 MB | `420DBFF3` | **Quadra 700** (default) / Quadra 900 — the Eclipse tower: same board + the IIfx's two Apple PIC IOPs, Egret, 2nd 53C96 | `POM68K_Q700_MODEL=q900` |
| 1 MB | `3DC27823` | Macintosh Quadra 950 (33.3 MHz, Eclipse) — pins its own model | |
| 1 MB | `06684214` `064DC91D` | **Quadra 630** (default) / LC 580 | `POM68K_Q630_ID=A55A225A` |
| 1 MB | `ECFA989B` | **PowerBook Duo 230** (33 MHz, MSC + PG&E) — the LCD laptop; needs `roms/pge/pge_boot.bin` | |

Several profiles run their MCU as **firmware LLE** off a user-provided dump
and fall back to a non-conformant HLE model without it (which they say on
stderr). First choice per family, with the fallbacks the code then tries:
`roms/egret/341s0850.bin` (LC, LC II, Classic II), `roms/cuda/341s0417.bin`
(Color Classic, then `341s0788.bin`), `roms/cuda/341s0789.bin` (Mac TV, then
`341s0060`/`341s0788`), `roms/cuda/341s0060.bin` (LC 520/550/CC II, Quadra
630, LC 580 — Cuda 2.40; 2.37 livelocks these ROMs),
`roms/cuda/341s0788.bin` (Quadra 605 family), `roms/egret/341s0851.bin`
(LC III, LC III+, IIvx, IIvi, and the Eclipse towers Q900/Q950 since
2026-08-14 `Q700Memory.cpp:52-53` — then the LC II's `341s0850`),
`roms/egret/344s0100.bin` (IIsi), `roms/adbmodem/342s0440-b.bin` — the
PIC1654S ADB transceiver, used by every `AdbVia` platform: SE / SE FDHD /
Classic, the Mac II family, the IIci, Centris/Quadra 610/650/800 and the
Quadra 700. The Duo additionally needs
`roms/pge/pge_boot.bin` — without it there is no PMU at all and the boot
stalls on the handshake.

### Command line

The typed positional contract is `[ROM] [boot volume] [additional media…]`;
additional SCSI volumes attach at IDs 1–6 (six entries max, a repeat of the
boot volume skipped). On the **four compact 68000 machines** — Plus, SE,
SE FDHD, Classic — the layout is `[ROM] [floppy] [SCSI]` instead, and there
is no additional-media loop: later values are ignored.

There are only three flags, all consumed before the positional parsing:
`--version`, `--lle-aarch64`, `--lle-aarch64-check`. Everything else is
positional; there is no `--help`.

| Machine | Default boot volume (probed in order) |
|---|---|
| Plus / SE / SE FDHD / Classic | floppy `disks35/Disk605.dsk`, SCSI `hdv/HD20SC.vhd` |
| Mac II family | `hdv/System 6.0.8 HD.dsk`, `HD20SC.vhd`, `GISTPERSO-boot.vhd`, `boot.vhd` |
| IIfx | `hdv/MacOS-7.6-boot.vhd`, `GISTPERSO-boot.vhd`, `boot.vhd` (32-bit-dirty ROM: 7.6 is the ceiling) |
| V8 family | `hdv/<profile>-boot.vhd` (e.g. `lcii-`, `lc-`, `classic2-`), then `GISTPERSO-boot.vhd`, `boot.vhd`, `HD20SC.vhd` |
| RBV | `hdv/iici-boot.vhd` or `hdv/iisi-boot.vhd`, then `lc3-boot.vhd` and the V8 tail |
| Sonora / VASP | `hdv/lc3-boot.vhd`, then the V8 tail |
| 040 machines | `hdv/MacOS-8.1-boot.vhd`, `boot.vhd` |
| Duo 230 | `hdv/System 7.5.5 HD.dsk`, `boot.vhd` |

Each `hdv/<name>` in that table is a logical name. If
`hdv/ref/<name>` exists, both the GUI and gates select that immutable fixture;
the GUI actually writes its persistent clone at `hdv/work/<name>`. An explicit
path outside `hdv/ref/` remains directly writable for ordinary user volumes.

Bare HFS images — Infinite Mac style `'LK'` boot blocks at LBA 0, or the
zero-boot-block data volumes `dir2hfs.py` bakes, recognised by the MDB `'BD'`
at `$400` — get an in-memory DDM/partition-map façade at attach time. That
needs a **template**: `HD20SC.vhd` or `boot.vhd` beside the image or under
`hdv/`, else `POM68K_SCSI_DDM_TEMPLATE=<path>`; without one the image is left
raw and a line says so on stderr. Otherwise wrap them with
`tools/wrap_hfs.py`. `.iso`/`.cdr`/`.toast`/`.cue`/`.bin` attach as a SCSI CD
on all eleven non-compact platforms, and the literal token `cdbay` among the
additional media puts an **empty** CD drive on the bus (hot-swappable from
the **Disques** window forever after). On the four 68040 machines an
additional `.dsk`/`.image` is **inserted as a floppy** instead of going on
the bus (SWIM2: 800K GCR and 1.44 MB MFM), as is
`POM68K_FLOPPY=<path>`; the first media argument stays the SCSI boot volume.

```bash
./build/POM68K roms/macplus.rom disks35/Disk605.dsk hdv/HD20SC.vhd
./build/POM68K roms/maclcii.rom hdv/boot.vhd hdv/data.vhd     # + SCSI ID 1
./build/POM68K roms/quadra605.rom hdv/MacOS-8.1-boot.vhd
POM68K_Q605_ID=A55A2225 ./build/POM68K roms/quadra605.rom     # Quadra 605
```

PRAM persists next to the boot volume as `<disk>.<profile>.pram`, on all
twelve platforms — the first cold boot runs the ROM's full-RAM burn-in, later
boots skip it. The **clock is not in the file**: the GUI seeds host local wall
time at every launch (tests start at 0, so etalons stay deterministic).

### Environment variables

`DEV.md` § *Environment knobs — the complete list* is the single list, and
is re-derived from the code rather than maintained by hand. The ones that
select a machine are in the table above; the rest cover CPU options,
firmware-LLE toggles, the JIT (`src/jit/POM68K_JIT.md` § 6) and stderr
tracers. Frequently useful:

| Knob | Effect |
|---|---|
| `POM68K_MONITOR=512` | 512×384 12" mode instead of 640×480 — any value < 640 selects it. Read on Sonora, VASP, RBV and the V8 machines with an external connector (LC, LC II); the built-in displays (Classic II, Color Classic, Mac TV) ignore it. Also live in the CPU window |
| `POM68K_NOFPU=1` | drop the FPU — Mac II family, IIfx, V8, Sonora, VASP, RBV. The 68040 platforms have their own (`POM68K_Q605_NOFPU`, `POM68K_CENTRIS_FPU`, `POM68K_Q700_LC040`, `POM68K_Q630_LC040`) |
| `POM68K_FLOPPY_RO=1` | never write floppy changes back to the image file |
| `POM68K_DRIVE_SFX=0` | start with drive sounds muted |
| `POM68K_AUDIO=0` | disable host audio output and audio-clocked pacing (measurement/debugging) |
| `POM68K_APPLETALK=0` | disable the whole built-in AppleTalk stack |

Careful: many knobs are **presence-only** — `POM68K_NOFPU=0` still drops the
FPU. The ones that honour `=0` are the toggles that name a default-on feature
(`POM68K_APPLETALK`, `POM68K_AUDIO`, `POM68K_DRIVE_SFX`, `POM68K_*_LLE`, `POM68K_DAYNAPORT`,
`POM68K_FLOPPY_BOOST_GATE`, and the `POM68K_JIT_*` booleans). `DEV.md` § 5
says which is which.

## Using the machine

**Mouse** drives the Mac while hovering the screen; a drag started on the
screen (Finder drag-and-drop) keeps tracking outside it and never moves the
host window — the title bar still does. A **middle click** (the mouse
wheel), **Ctrl+Alt+G**, or **Delete**, toggles full capture (cursor grabbed,
raw motion); any of the three releases it again. Hovering the screen is
needed to capture, never to release. The host keyboard maps to M0110 codes on the
Plus and to raw ADB codes (= M0110 code >> 1) elsewhere (M0110 table:
`src/GuiShellCommon.h:323-341`; the shared DAFB/Sonora/V8/Toby/Duo ADB table is in
`src/GuiShellCommon.h:241-270`, with the Toby pair and Duo's Escape/no-keypad variant
at `GuiShellCommon.h:209-218` — notes in
`DEV.md` § *Input: M0110 keyboard + quadrature mouse*).

Menus:

- **Machine** — switch profile (needs the matching ROM present; the app
  relaunches itself), and **Sauver / Restaurer l'état** (save states). A
  state file sits next to the boot volume as `<disk>.<profile>.pomss`,
  written atomically; a snapshot that does not match the running profile,
  ROM or RAM size is refused and the machine is left untouched. All 37
  profiles are wired (`SnapMachine`, `src/MachineCatalog.h`). Also
  **Sons des lecteurs** (drive sounds).
- **CPU** — pick the execution engine. The conformant accelerated engine is
  the default on 68040 and 68030; other families start on the Moira
  **interpreter**, which remains the reference for every accuracy claim.
  Both are available and switchable live on every machine. `auto` selects
  native x86-64/AArch64 generation for a 68040 and a 68030 alike; the
  portable `threaded` window covers the remaining JIT-enabled families.
  `POM68K_CPU_ENGINE=interp|jit`
  overrides the family default explicitly; design, gates and measurements in
  `src/jit/POM68K_JIT.md`. The CPU menu also reports **× real time**, measured
  from the machine clock without changing emulated timing. The separate GUI
  **Avance rapide** scheduler starts **disabled**: accelerating up to eight guest
  frames per host frame makes two physical clicks too far apart on the Mac's
  clock for reliable double-clicks. Normal mode targets the model's nominal
  speed; enable fast-forward manually only when desired.
- **Disques** — pick the boot volume and toggle secondary SCSI images next
  to the current one (relaunches: the ROM only scans the bus at boot).
  All twelve platforms have it, the compacts included
  (`pom68k::diskBaysMenuItem`, one call per machine loop).
- **Réseau → AppleTalk** — see below.
- Per-machine window: **Redémarrer** power-cycles.

Floppies are writable (real IWM/SWIM write engines) and the GUI **persists
writes back to the image file** on eject and on exit — raw `.dsk` and
DiskCopy 4.2, checksum regenerated, atomic temp+rename. Tests stay
in-memory.

Drive activity is audible: head steps and seeks, spindle spin-up/loop/down,
insert/eject clicks (MAME's sample set ported via POM2 —
`assets/floppy_samples/`, BSD-3-Clause). The 3.5" set voices the Sony
drives; the 5.25" set plays at low gain as the SCSI hard-disk seek proxy.

## AppleTalk: file sharing, printing and the internet (built in)

POM68K carries the whole AppleTalk service side **in-process** — no
external router, server or gateway, no root. **On by default in the GUI**;
open **Réseau → AppleTalk**, which reports live whether each piece works.

- **Réseau (nœud/routeur)** — zone "POM68K", the guest's node, frame / NBP
  / ATP counters.
- **AppleShare** — a host folder as an AFP volume, picked in the guest's
  **Chooser → AppleShare** (log in as Guest). Defaults to `AppleShare/` at
  the repo root, created if absent; override with `POM68K_SHARE_DIR=/path`.
  Resource forks and Finder info live in netatalk-compatible
  `.AppleDouble` sidecars. Transfers run over a lossless virtual LocalTalk
  clocked ~8× the real 28 KB/s wire (`POM68K_ATALK_WIRE_BOOST=N`, `=1` is
  authentic speed): the SCC exerts backpressure, so a frame is held rather
  than dropped and the 1–2 s retransmit stalls go away. The hold queue is
  bounded at 64 frames — past that, frames are dropped like a real cable
  would, and the window reports it as "Débordement du fil" beside the
  retransmission count, its lag and the queue depth. All four read
  0 / shallow on a healthy transfer.
- **Imprimante** — a LaserWriter in the Chooser. PostScript is spooled to
  CUPS (`lp`) if present, else a timestamped `.ps` under `run/print`.
- **Internet (MacIP)** — real TCP/IP tunneled in DDP. In the **TCP/IP**
  (Open Transport) or **MacTCP** control panel choose *AppleTalk (MacIP)*,
  server zone POM68K; a user-mode NAT proxies it onto the host (no `tun`,
  no `iptables`). **Plain HTTP only** — 1990s TLS cannot reach 2026 sites;
  try <http://frogfind.com> or <http://theoldnet.com>.

Tracers when the wire misbehaves: `POM68K_ATALK_DEBUG=1` logs DDP/NBP/ATP
plus one line per client retransmission with the delay since our own reply
(~1–2 s means the guest's ATP timer fired, much less means the reply
arrived damaged); `POM68K_MACIP_DEBUG=1` logs every IP datagram both ways
with TCP flags/seq/ack, so the last line before a guest stack collapse is
the datagram that did it. Protocol notes: `docs/APPLETALK.md` (§ 6.5 = the
in-process stack).

### Reaching a real LocalTalk network

`POM68K_LTOUDP=1` joins the SCC printer port to a virtual LLAP cable over
UDP multicast (`239.192.76.84:1954`, the Mini vMac / TashRouter format), so
POM68K can talk to **other** machines; the internal node is multicast
alongside, and both coexist. Setting it at all (any value) turns the cable
on — and turns the lossless wire boost off, since the cable is shared with
peers that expect real LocalTalk pacing. `POM68K_APPLETALK=1` additionally seeds
AppleTalk active in PRAM (System 7 opens LocalTalk at boot; System 6 opens
it from the Chooser).

For AppleShare/printing served by the **real** netatalk over a routed
segment, the repo vendors the external bridge: TashRouter
(`extern/tashrouter`) and netatalk 2.4.9 (`extern/netatalk2`) — build with
`tools/netatalk2/build_netatalk2.sh`, then follow
`tools/netatalk2/README.md`. MacIP toward a host `tun` uses
`extern/macipgw` + `tools/macip/`.

## Sharing host files with the Mac (baked HFS volume)

`tools/dir2hfs.py` bakes a host folder into classic-HFS volume(s) that
mount on the emulated desktop as a second SCSI disk:

```bash
python3 -m venv .venv-tools && .venv-tools/bin/pip install machfs   # once
.venv-tools/bin/python tools/dir2hfs.py input hdv/INPUT             # bake
./build/POM68K <ROM> hdv/MacOS-8.1-boot.vhd hdv/INPUT.vhd           # mount
```

(or pick the volume from the **Disques** menu). MacBinary `.bin` files are
decoded to native forks; `.zip` archives are expanded host-side;
`.sit`/`.hqx` keep their StuffIt types (unstuff in the guest);
`.iso`/`.cdr`/`.toast` are dropped from the volume and copied next to the
output instead, to be attached directly as SCSI CDs (`--all` bakes them in).
The volume is writable and carries **zero boot blocks** on purpose, so the
ROM's 6→0 bus scan never tries to boot it. It is split automatically past
`--max-mb` (default 1900) into `INPUT-1.vhd`, `INPUT-2.vhd`, …; one volume
keeps the plain name. Filter with `--only 'glob'`.

## Headless tools

```bash
./build/demo_screenshot --frames 60 --out shot.ppm   # PPM screenshot
./build/cpu_smoke                                    # end-to-end CPU gate
```

## License

GPLv3. Moira is MIT (Dirk W. Hoffmann); Dear ImGui is MIT (fetched, not
vendored). ROMs are user-provided and never committed.
