# POM68K — Macintosh 68k emulator

**37 machine profiles, every one boots the Finder** — from the Macintosh
Plus (68000, cycle-exact) to the Quadra 950 tower (68040), by way of the
PowerBook Duo 230 laptop. Sibling of
[POMIIGS](../POMIIGS/) (Apple IIgs) and [POM2](../POM2/) (Apple II),
sharing their architecture and conventions. CPU core:
[Moira](https://github.com/dirkwhoffmann/Moira) (vendored via NeoST —
`extern/moira/POM68K_VENDOR.md`).

The full machine list is the **ROM → machine** table below, which is also
the Machine menu, in the same order and the same grouping
(`src/main.cpp:777` `kProfiles[]` — one row per profile, no exceptions).

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
ctest                             # 164 gates (asset-dependent ones soft-skip)
ctest -L unit                     # 79 gates, no ROM or disk image needed
ctest -L smoke                    # 8 gates, one machine, both CPU engines
```

Requires CMake ≥ 3.16, a C++20 compiler, GLFW3 + OpenGL (GUI only).

Optional: a profile-guided build measured **−33 % on the interpreter** and
−18 % on the JIT (Quadra 605 boot), bit-identical emulation. The helper keeps
the three steps in one directory and, on Clang/AppleClang, merges the runtime
profiles with `llvm-profdata` (rationale + flags: `CMakeLists.txt:30`):

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

On Apple Silicon, `auto` selects the native AArch64 generator. Its linked
exits, per-access MMIO thunk and expanded opcode coverage reach 97.3 % native
retirement in the fixed 1,000-frame Q605 benchmark. Five-million-step fine and
coarse locksteps and the complete Q605 Finder boot pass. On an Apple M-class
host, Release/native/LTO measured 1.22 s versus 4.55 s for `threaded` (3.73x);
the existing LLVM PGO profile measured 1.01 s versus 3.41 s (3.38x), with the
same guest fingerprint. The complete Finder gate takes 9.19 s natively versus
21.14 s with `threaded` (2.30x); under PGO it takes 7.86 s versus 15.28 s
(1.94x), with identical video and SCSI signatures:

```bash
POM68K_CPU_ENGINE=jit POM68K_JIT_BACKEND=auto ./build-pgo/POM68K
# Portable comparison/fallback:
POM68K_CPU_ENGINE=jit POM68K_JIT_BACKEND=threaded ./build-pgo/POM68K
# Explicit native selection remains accepted:
POM68K_CPU_ENGINE=jit POM68K_JIT_BACKEND=a64 ./build-pgo/POM68K
```

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

No ROM ships in any package — drop your own dumps into `roms/`, `hdv/`,
`disks35/`. The **AppImage looks for those beside itself first**: create
`roms/` next to the `.AppImage` file and everything, writes included (PRAM,
save states, screenshots), stays there — a portable install on a USB stick
works. Failing that it uses the directory you launched from if it holds one of
those folders, and failing that `~/.local/share/POM68K`, which it seeds on
first run with a README saying so. `POM68K_DATA_DIR=<path>` overrides all
three, and the chosen directory is printed on stderr at every launch. The
macOS `.app` still always uses `~/Library/Application Support/POM68K`; the
Windows zip finds its data beside the `.exe`. `POM68K --version` prints the banner and
exits before any window — that is the CI smoke and a quick install check.
Maintainers: run the *Build bionic builder image* workflow once, pin the
digests it prints into `release.yml`, then push a version tag.

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
a `35C28F5F` CRC scan of `roms/`, `roms/macplus.rom`, `roms/macii.rom`,
`roms/quadra605.rom`, then CRC `9779D2C4` and `FF7439EE`
(`src/main.cpp:5169`). The default machine is therefore the **Mac LC II**.

### ROM → machine

Dispatch is by ROM **size**, then by the header checksum (the first four
bytes, big-endian), then by an environment variable for models that share a
ROM and differ only by clock / CPU / model ID. `src/main.cpp:5187` is the
code; the **Machine** menu sets the same variables and relaunches.

| Size | Checksum | Machine(s) | Selector |
|---|---|---|---|
| 128 KB | — | Macintosh Plus | |
| 256 KB | `B2E362A8` | Macintosh SE | |
| 256 KB | `B306E171` | Macintosh SE FDHD | |
| 512 KB | `A49F9914` | Macintosh Classic | |
| 256 KB | `9779D2C4` `97851DB6` | Macintosh II (68020) | |
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
| 1 MB | `FF7439EE` *(and any other 1 MB dump)* | **LC 475** (default) / LC 575 / Quadra 605 | `POM68K_Q605_ID=A55A222E\|A55A2225` |
| 1 MB | `F1A6F343` `F1ACAD13` | **Centris 650** (default) / Centris 610 / Quadra 610 / 650 / 800 | `POM68K_CENTRIS_MODEL=c610\|c650\|q610\|q650\|q800` |
| 1 MB | `420DBFF3` | **Quadra 700** (default) / Quadra 900 — the Eclipse tower: same board + the IIfx's two Apple PIC IOPs, Egret, 2nd 53C96 | `POM68K_Q700_MODEL=q900` |
| 1 MB | `3DC27823` | Macintosh Quadra 950 (33.3 MHz, Eclipse) — pins its own model | |
| 1 MB | `06684214` `064DC91D` | **Quadra 630** (default) / LC 580 | `POM68K_Q630_ID=A55A225A` |
| 1 MB | `ECFA989B` | **PowerBook Duo 230** (33 MHz, MSC + PG&E) — the LCD laptop; needs `roms/pge/pge_boot.bin` | |

Several profiles run their MCU as **firmware LLE** off a user-provided dump
and fall back to an HLE model without it: `roms/cuda/341s0417.bin` (Color
Classic, then `341s0788.bin`), `roms/cuda/341s0060.bin` (LC 520/550/CC II,
Quadra 630 — Cuda 2.40; 2.37 livelocks these ROMs),
`roms/cuda/341s0788.bin` (Quadra 605 family), `roms/egret/341s0851.bin`
(LC III, IIvx), `roms/egret/344s0100.bin` (IIsi),
`roms/adbmodem/342s0440-b.bin` (Mac II, IIci, Centris/Quadra ADB).

### Command line

`argv[1]` = ROM. `argv[2]` = the boot volume; `argv[3..8]` attach as
secondary SCSI volumes at IDs 1–6. On the Mac Plus only, the layout is
`[ROM] [floppy] [SCSI]`.

| Machine | Default boot volume (probed in order) |
|---|---|
| Plus / SE / Classic | floppy `disks35/Disk605.dsk`, SCSI `hdv/HD20SC.vhd` |
| Mac II family | `hdv/System 6.0.8 HD.dsk`, `HD20SC.vhd`, `GISTPERSO-boot.vhd`, `boot.vhd` |
| V8 / RBV family | `hdv/GISTPERSO-boot.vhd`, `boot.vhd`, `HD20SC.vhd` |
| Sonora / VASP | `hdv/lc3-boot.vhd`, then the V8 list |
| 040 machines | `hdv/MacOS-8.1-boot.vhd`, `boot.vhd` |

Bare HFS `.dsk` images (Infinite Mac style, `'LK'` at LBA 0) get an
in-memory SCSI façade automatically; otherwise wrap them with
`tools/wrap_hfs.py`. `.toast`/`.cdr`/`.iso` attach as a SCSI CD on the Mac
II. On the four 68040 machines a `.dsk`/`.image` argument is **inserted as
a floppy** instead (SWIM2: 800K GCR and 1.44 MB MFM), as is
`POM68K_FLOPPY=<path>`.

```bash
./build/POM68K roms/macplus.rom disks35/Disk605.dsk hdv/HD20SC.vhd
./build/POM68K roms/maclcii.rom hdv/boot.vhd hdv/data.vhd     # + SCSI ID 1
./build/POM68K roms/quadra605.rom hdv/MacOS-8.1-boot.vhd
POM68K_Q605_ID=A55A2225 ./build/POM68K roms/quadra605.rom     # Quadra 605
```

PRAM + clock persist next to the boot volume as
`<disk>.<profile>.pram` — the first cold boot runs the ROM's full-RAM
burn-in, later boots skip it.

### Environment variables

`DEV.md` § *Environment knobs — the complete list* is the single list, and
is re-derived from the code rather than maintained by hand. The ones that
select a machine are in the table above; the rest cover CPU options,
firmware-LLE toggles, the JIT (`src/jit/POM68K_JIT.md` § 6) and stderr
tracers. Frequently useful:

| Knob | Effect |
|---|---|
| `POM68K_MONITOR=512` | LC II: 512×384 12" mode instead of 640×480 (also live in the CPU window) |
| `POM68K_NOFPU=1` | drop the FPU (Mac II, LC II, Sonora) |
| `POM68K_FLOPPY_RO=1` | never write floppy changes back to the image file |
| `POM68K_DRIVE_SFX=0` | start with drive sounds muted |
| `POM68K_APPLETALK=0` | disable the whole built-in AppleTalk stack |

## Using the machine

**Mouse** drives the Mac while hovering the screen; a drag started on the
screen (Finder drag-and-drop) keeps tracking outside it and never moves the
host window — the title bar still does. **Delete** toggles full capture
(cursor grabbed, raw motion). The host keyboard maps to M0110 codes on the
Plus and to raw ADB codes elsewhere (M0110 table: `src/main.cpp:5446`; the
ADB tables are one per machine loop, e.g. `src/main.cpp:1301` — notes in
`DEV.md` § *Input: M0110 keyboard + quadrature mouse*).

Menus:

- **Machine** — switch profile (needs the matching ROM present; the app
  relaunches itself), and **Sauver / Restaurer l'état** (save states). A
  state file sits next to the boot volume as `<disk>.<profile>.pomss`,
  written atomically; a snapshot that does not match the running profile,
  ROM or RAM size is refused and the machine is left untouched. All 34
  profiles are wired (`src/SaveStateMachines.h:49`). Also **Sons des
  lecteurs** (drive sounds).
- **CPU** — pick the execution engine. The Moira **interpreter** is the
  default and the reference for every accuracy claim. Beside it sits an
  accelerated engine, switchable live between two instructions, available
  on the 68030/68040 machines (the Mac II family, the compacts and the IIfx
  are interpreter-only). It is honestly labelled: with the portable `threaded`
  backend it is the interpreter behind a fetch window, and only the x86-64
  backend actually emits host code. `POM68K_CPU_ENGINE=jit` starts on it;
  design and measurements in `src/jit/POM68K_JIT.md`.
- **Disques** — pick the boot volume and toggle secondary SCSI images next
  to the current one (relaunches: the ROM only scans the bus at boot).
  Every machine but the compact 68000 family has it.
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
alongside, and both coexist. `POM68K_APPLETALK=1` additionally seeds
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
`.sit`/`.hqx` keep their StuffIt types (unstuff in the guest); CD images
are extracted next to the output and attach directly as SCSI disks. The
volume is writable; split past 1.9 GB with `--max-mb`, filter with
`--only 'glob'`.

## Headless tools

```bash
./build/demo_screenshot --frames 60 --out shot.ppm   # PPM screenshot
./build/cpu_smoke                                    # end-to-end CPU gate
```

## License

GPLv3. Moira is MIT (Dirk W. Hoffmann); Dear ImGui is MIT (fetched, not
vendored). ROMs are user-provided and never committed.
