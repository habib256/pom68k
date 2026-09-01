# POM68K — Macintosh 68k emulator

POM68K emulates classic Macintosh computers from the 68000 Macintosh Plus to
the 68040 Quadra 950. The project currently provides **37 machine profiles on
12 hardware platforms**, and every listed profile boots to the Finder with the
matching ROM and system media.

The goal is broader than the current catalogue: **support every 68k
Macintosh**. The compiled source of truth for present coverage is
[`src/MachineCatalog.h`](src/MachineCatalog.h).

> **ROMs and system disks are copyrighted and are never distributed with
> POM68K.** You must provide your own dumps and disk images.

## Contents

- [Highlights](#highlights)
- [Quick start](#quick-start)
- [ROMs and machine profiles](#roms-and-machine-profiles)
- [Media and persistent files](#media-and-persistent-files)
- [Controls and menus](#controls-and-menus)
- [CPU engines and JIT policy](#cpu-engines-and-jit-policy)
- [AppleTalk and host file exchange](#appletalk-and-host-file-exchange)
- [Build and test](#build-and-test)
- [Strict AArch64 LLE mode](#strict-aarch64-lle-mode)
- [Project documentation](#project-documentation)

## Highlights

- Macintosh 68000, 68020, 68030 and 68040 families, including compact Macs,
  desktop systems, towers, all-in-ones and the PowerBook Duo 230.
- Moira interpreter as the accuracy oracle, plus a conformant accelerated
  engine with portable threaded execution and native AArch64/x86-64 code
  generators where their measured policy allows them.
- IWM, SWIM1 and SWIM2 floppy support; SCSI disks and CDs; writable DiskCopy
  4.2 and raw floppy images; persistent PRAM and save states.
- Built-in AppleTalk services: AppleShare, LaserWriter spooling and MacIP
  user-mode networking, with no privileged host setup.
- Firmware-level emulation for supported ADB, Egret and Cuda controllers when
  the required user-provided firmware is present, with an explicit HLE/LLE
  status in the interface.
- Linux x86-64/AArch64, macOS Universal 2 and Windows x64 packages.

POM68K uses the [Moira](https://github.com/dirkwhoffmann/Moira) CPU core,
vendored through NeoST. The local fork and its provenance are documented in
[`extern/moira/POM68K_VENDOR.md`](extern/moira/POM68K_VENDOR.md).

## Quick start

### Use a release package

Download a package from the
[GitHub Releases page](https://github.com/habib256/pom68k/releases), place your
ROMs and media in the data directories described below, then launch POM68K.

| Package | Target |
|---|---|
| `POM68K-<version>-x86_64.AppImage` | Linux x86-64, glibc 2.27 or newer |
| `POM68K-<version>-aarch64.AppImage` | Linux AArch64, including Raspberry Pi 3/4/400/5 with a desktop |
| `POM68K-macOS-v<version>.dmg` | macOS 11 or newer, Universal 2 |
| `POM68K-Windows-v<version>.zip` | Windows x64, self-contained |

Tagged releases build those four packages. The **Raspberry Pi package
(-mcpu)** workflow can additionally create a board-specific AppImage and a
`tar.gz` for Pi OS Lite or kiosk installations. The generic release AppImage
is normally preferable on Pi 4/400; see
[`docs/RASPBERRY_PI.md`](docs/RASPBERRY_PI.md) for the measured comparison.

No package contains a ROM. Data placement depends on the package:

| Host/package | Data directory |
|---|---|
| Linux AppImage | A directory containing `roms/`, `hdv/` or `disks35/` beside the AppImage; otherwise a matching launch directory; otherwise `$XDG_DATA_HOME/POM68K` or `~/.local/share/POM68K`. `POM68K_DATA_DIR=<path>` overrides this search. |
| macOS application | `~/Library/Application Support/POM68K` |
| Windows zip | The working directory, normally the directory containing `POM68K.exe` when opened from Explorer |
| Source build | The repository root, or another working directory containing the data folders |

A typical data tree is:

```text
roms/          Macintosh ROMs and optional controller firmware
hdv/           SCSI hard-disk and CD images
disks35/       floppy images
AppleShare/    host files exported through built-in AFP
```

The selected data directory is printed on stderr by the Linux launcher.

### Build locally

```bash
./setup_imgui.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/POM68K
```

`setup_imgui.sh` fetches the pinned Dear ImGui docking release and creates the
build directory. See [Build and test](#build-and-test) for dependencies,
headless builds and test tiers.

### Launch a specific machine

The general command line is:

```text
POM68K [options] [ROM] [boot medium] [additional media...]
```

Examples:

```bash
./build/POM68K roms/macplus.rom disks35/Disk605.dsk hdv/HD20SC.vhd
./build/POM68K roms/maclcii.rom hdv/boot.vhd hdv/data.vhd
./build/POM68K --machine-profile=q605 roms/quadra605.rom hdv/MacOS-8.1-boot.vhd
./build/POM68K --version
```

For Plus, SE, SE FDHD and Classic, the positional media layout is
`[ROM] [floppy] [SCSI disk]`. Other platforms use `[ROM] [boot volume]
[additional media...]`; additional SCSI disks are attached at IDs 1 through 6.

Without an explicit ROM, POM68K looks for `roms/maclcii.rom`, an LC II CRC in
the `roms/` tree, `roms/macplus.rom`, `roms/macii.rom`,
`roms/quadra605.rom`, then Mac II and Quadra 605 CRCs. If no ROM is found, it
runs a built-in 68000 framebuffer demo instead of a Macintosh ROM.

## ROMs and machine profiles

ROM dispatch uses file size followed by the big-endian checksum stored in the
first four bytes. A ROM shared by several models can be paired with a profile
through the **Machine** menu or `--machine-profile=<slug>`.

POM68K searches the working directory, the executable directory and its
parent. Recursive signature discovery matches the hexadecimal CRC in the
**filename**; it does not hash unnamed files. Use the canonical path below,
include the CRC in the filename, or pass the ROM explicitly.

### ROM to machine table

Bold text is the default profile for a shared ROM.

| ROM size | Header checksum | Machine profiles | Profile slug when shared |
|---|---|---|---|
| 128 KB | any | Macintosh Plus | — |
| 256 KB | `B2E362A8` | Macintosh SE | — |
| 256 KB | `B306E171` | Macintosh SE FDHD | — |
| 512 KB | `A49F9914` | Macintosh Classic | — |
| 256 KB | `9779D2C4`, `97851DB6`, or another non-`97221136` dump | Macintosh II | — |
| 256 KB | `97221136` | **Macintosh IIx**, IIcx, SE/30 | `iix`, `iicx`, `se30` |
| 512 KB | `4147DD77` | Macintosh IIfx | — |
| 512 KB | `368CADFE` | Macintosh IIci | — |
| 512 KB | `36B7FB6C` | Macintosh IIsi | — |
| 512 KB | `350EACF0` | Macintosh LC | — |
| 512 KB | `35C28F5F` or another otherwise unmatched 512 KB dump | Macintosh LC II | — |
| 512 KB | `3193670E` | Macintosh Classic II | — |
| 1 MB | `ECD99DC0` | Macintosh Color Classic | — |
| 1 MB | `EAF1678D` | Macintosh TV | — |
| 1 MB | `ECBBC41C`, `EC904829` | **Macintosh LC III**, LC III+ | `lc3`, `lc3plus` |
| 1 MB | `EDE66CBD` | **Macintosh LC 520**, LC 550, Color Classic II | `lc520`, `lc550`, `cclassic2` |
| 1 MB | `4957EB49` | **Macintosh IIvx**, IIvi | `iivx`, `iivi` |
| 1 MB | `FF7439EE` or another otherwise unmatched 1 MB dump | **Macintosh LC 475**, LC 575, Quadra 605 | `lc475`, `lc575`, `q605` |
| 1 MB | `F1A6F343`, `F1ACAD13` | **Macintosh Centris 650**, Centris 610, Quadra 610, Quadra 650, Quadra 800 | `c650`, `c610`, `q610`, `q650`, `q800` |
| 1 MB | `420DBFF3` | **Macintosh Quadra 700**, Quadra 900 | `q700`, `q900` |
| 1 MB | `3DC27823` | Macintosh Quadra 950 | — |
| 1 MB | `06684214`, `064DC91D` | **Macintosh Quadra 630**, LC/Performa 580 | `q630`, `lc580` |
| 1 MB | `ECFA989B` | PowerBook Duo 230 | — |

The LC 475, LC 575 and Quadra 605 profiles currently share the same emulated
25 MHz clock even though the LC 575 label identifies the real 33 MHz model.
The IIfx ROM is 32-bit dirty and is intended for System 7.6 or earlier.

### Additional firmware

Most machines can fall back to a higher-level controller model, but firmware
LLE requires user-provided dumps in these locations:

| Controller | Principal profiles | Preferred path |
|---|---|---|
| PIC1654S ADB transceiver | SE family, Classic, Mac II family, IIci, Centris/Quadra 610–800, Quadra 700 | `roms/adbmodem/342s0440-b.bin` |
| Egret 341S0850 | LC, LC II, Classic II | `roms/egret/341s0850.bin` |
| Egret 341S0851 | LC III family, IIvx/IIvi, Quadra 900/950 | `roms/egret/341s0851.bin` |
| Egret 344S0100 | IIsi | `roms/egret/344s0100.bin` |
| Cuda 341S0417 | Color Classic | `roms/cuda/341s0417.bin` |
| Cuda 341S0789 | Macintosh TV | `roms/cuda/341s0789.bin` |
| Cuda 341S0060 | LC 520 family, Quadra 630/LC 580 | `roms/cuda/341s0060.bin` |
| Cuda 341S0788 | LC 475/575 and Quadra 605 | `roms/cuda/341s0788.bin` |
| PG&E power manager | PowerBook Duo 230 | `roms/pge/pge_boot.bin` |

The SE/30 additionally uses `roms/se30/se30vrom.uk6` for its video declaration
ROM. The Duo firmware is required for the PMU handshake; without it, that
machine cannot complete its boot. `assets.lock` records the hashes required by
strict product qualification; runtime fallback order and implementation
details are documented in [`DEV.md`](DEV.md) and the corresponding memory
classes.

## Media and persistent files

POM68K accepts SCSI hard disks, writable HFS volumes, floppy images and SCSI
CD images. Common CD extensions are `.iso`, `.cdr`, `.toast`, `.cue` and
`.bin`. Passing the literal `cdbay` as additional media creates an empty,
hot-swappable CD drive.

Bare HFS volumes are given an in-memory partition-map facade when a template
is available as `HD20SC.vhd`, `boot.vhd`, or through
`POM68K_SCSI_DDM_TEMPLATE=<path>`. [`tools/wrap_hfs.py`](tools/wrap_hfs.py)
can make that wrapping permanent.

Floppy changes are persisted on eject and exit using an atomic replacement;
DiskCopy 4.2 checksums are regenerated. Use `POM68K_FLOPPY_RO=1` to prevent
write-back. SCSI images passed explicitly are writable. Repository fixtures
under `hdv/ref/` remain immutable: the GUI creates its working copy under
`hdv/work/`.

PRAM is stored beside the boot volume as `<disk>.<profile>.pram`. Save states
use `<disk>.<profile>.pomss`; incompatible profile, ROM or RAM configurations
are refused without modifying the running machine.

To turn a host directory into one or more writable classic-HFS volumes:

```bash
python3 -m venv .venv-tools
.venv-tools/bin/pip install machfs
.venv-tools/bin/python tools/dir2hfs.py input hdv/INPUT
./build/POM68K roms/quadra605.rom hdv/MacOS-8.1-boot.vhd hdv/INPUT.vhd
```

MacBinary files are decoded into native forks, zip archives are expanded, and
large inputs are split at 1,900 MB by default. Run
`tools/dir2hfs.py --help` for filtering and format options.

## Controls and menus

- **Mouse:** hover the Macintosh screen to control it. Middle click,
  `Ctrl+Alt+G`, or `Delete` toggles full mouse capture; releasing capture does
  not require the pointer to remain over the screen.
- **Machine:** select another profile whose ROM is available, save or restore
  state, and toggle drive sounds. Profile changes relaunch the emulator.
- **CPU:** switch between interpreter and accelerated execution, choose the
  available backend, select display-related CPU options, and view measured
  speed relative to the emulated machine.
- **Disques:** choose the boot volume, secondary SCSI disks, floppies and CDs.
  Changes that affect the boot bus relaunch the machine.
- **Réseau:** configure the built-in AppleTalk services.
- **Périphériques (LLE / HLE):** inspect controller provenance and choose
  firmware or fallback policy.

Fast-forward is intentionally disabled at startup. It can execute up to eight
guest frames per host frame, but it also advances the guest clock between
physical clicks and can make double-clicking unreliable.

## CPU engines and JIT policy

The Moira interpreter remains the reference for conformance. The accelerated
engine is the default for 68030 and 68040 guests; 68000 and 68020 guests start
on the interpreter because their measured gain is small. The accelerated
engine always has the portable `threaded` backend, even when executable memory
or a native generator is unavailable.

`auto` distinguishes **backend correctness** from **measured default policy**:

| Guest | Default engine | AArch64 `auto` | x86-64 Unix-like `auto` | Windows x64 `auto` |
|---|---|---|---|---|
| 68000 / 68020 | interpreter | `threaded` if JIT is forced | `threaded` if JIT is forced | `threaded` if JIT is forced |
| 68030 | JIT | native `a64` | `threaded` | `threaded` |
| 68040 | JIT | native `a64` | native `x64` | `threaded` |

The x64 generator remains explicitly available and conformance-gated for a
68030 on supported non-Windows x86-64 builds, but its automatic 68030
promotion is currently withdrawn pending whole-tier performance and stability
evidence. Windows does not compile the System V x64 emitter in the automatic
configuration.

Useful overrides:

```bash
POM68K_CPU_ENGINE=interp ./build/POM68K
POM68K_CPU_ENGINE=jit POM68K_JIT_BACKEND=auto ./build/POM68K
POM68K_CPU_ENGINE=jit POM68K_JIT_BACKEND=threaded ./build/POM68K
POM68K_CPU_ENGINE=jit POM68K_JIT_BACKEND=a64 ./build/POM68K
POM68K_CPU_ENGINE=jit POM68K_JIT_BACKEND=x64 ./build/POM68K
```

The detailed design, measured speedups, profiles and diagnostic knobs live in
[`src/jit/POM68K_JIT.md`](src/jit/POM68K_JIT.md). The GUI can switch engine
and backend without changing emulated timing.

## AppleTalk and host file exchange

The in-process AppleTalk stack is enabled by default and requires neither root
nor an external router. Open **Réseau → AppleTalk** to inspect the node,
queues and services.

- **AppleShare:** exports `AppleShare/` as an AFP volume. In the guest, use
  Chooser → AppleShare and log in as Guest. Override the host directory with
  `POM68K_SHARE_DIR=/path`.
- **LaserWriter:** sends PostScript to `lp` when CUPS is available, otherwise
  stores `.ps` files under `run/print`.
- **MacIP:** proxies guest TCP/IP through a user-mode NAT. Classic Mac systems
  generally support plain HTTP, not modern TLS.

`POM68K_APPLETALK=0` disables the complete built-in stack.
`POM68K_LTOUDP=1` also joins the SCC printer port to the Mini vMac/TashRouter
UDP multicast cable at `239.192.76.84:1954` for communication with other
emulators or LocalTalk bridges.

Protocol, tracing and external netatalk/TashRouter setup are documented in
[`docs/APPLETALK.md`](docs/APPLETALK.md) and
[`tools/netatalk2/README.md`](tools/netatalk2/README.md).

## Build and test

The source build requires:

- CMake 3.16 or newer;
- a C++20 compiler;
- GLFW 3.3 and OpenGL for the GUI target;
- Git and network access for `setup_imgui.sh` to fetch Dear ImGui.

Without `imgui/`, CMake still builds the headless core and tests but does not
declare the `POM68K` GUI target.

Common verification tiers are:

```bash
ctest --test-dir build -L asset-none --output-on-failure
ctest --test-dir build -L smoke --output-on-failure
ctest --test-dir build -L jit-fast --output-on-failure
ctest --test-dir build -L etalon-core --output-on-failure
```

Some gates require private ROM, firmware or reference-disk assets and
soft-skip when those assets are absent. [`STATUS.md`](STATUS.md) is generated
from the configured manifests and owns the current gate totals, host split and
recorded execution census. `assets.lock` is checked with:

```bash
python3 tools/verify_assets.py
python3 tools/verify_assets.py --strict   # require the complete private set
```

Profile-guided optimization is optional. On the recorded Quadra 605 workload,
it reduced interpreter time by 33% and JIT time by 18% without changing the
emulated state:

```bash
tools/pgo_train.sh build-pgo
cmake --build build-pgo --parallel 4
```

For a native Raspberry Pi build, the helper selects Cortex-A53, A72 or A76
from the board model and sizes parallelism to available memory:

```bash
packaging/raspberry/build_native_pi.sh --pgo
sudo packaging/raspberry/build_native_pi.sh --pgo --install
```

Two small headless utilities are also available:

```bash
./build/demo_screenshot --frames 60 --out shot.ppm
./build/cpu_smoke
```

## Strict AArch64 LLE mode

`--lle-aarch64` requests a qualified all-LLE AArch64 session. It forces the
JIT engine and `a64` backend, validates the required firmware by size and
SHA-256 before opening the session, and refuses any qualified claim as soon as
an HLE fallback activates. Save states record that provenance.

```bash
./build/POM68K --lle-aarch64-check roms/quadra605.rom
./build/POM68K --lle-aarch64 roms/quadra605.rom hdv/MacOS-8.1-boot.vhd
```

`--lle-aarch64-check` performs only the preflight. A missing backend, firmware
dump, qualified 68040 profile, or a forced HLE fallback exits with status 2
and a `REFUSÉ` diagnostic. See
[`docs/LLE_VS_HLE.md`](docs/LLE_VS_HLE.md) for the qualification contract and
supported platform/firmware combinations.

## Project documentation

| Document | Purpose |
|---|---|
| [`README.md`](README.md) | User setup, ROMs, media, controls and build entry points |
| [`STATUS.md`](STATUS.md) | Generated test registry and recorded runs |
| [`DEV.md`](DEV.md) | Architecture, platform internals and the complete environment-knob catalogue |
| [`TODO.md`](TODO.md) | Open work only |
| [`CHANGELOG.md`](CHANGELOG.md) | Dated engineering history, corrections and measurements |
| [`src/jit/POM68K_JIT.md`](src/jit/POM68K_JIT.md) | JIT design, conformance evidence and performance journal |
| [`docs/`](docs/) | Subsystem research and implementation notes |

POM68K is a sibling of POMIIGS and POM2, sharing their architecture and
conventions.

## License

POM68K is distributed under GPLv3; see [`LICENSE`](LICENSE). Moira is MIT
licensed, and Dear ImGui is MIT licensed and fetched rather than vendored.
