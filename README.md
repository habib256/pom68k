# POM68K — Macintosh 68k emulator

Macintosh **Plus** (68000, cycle-exact), Macintosh **II** (68020 + Toby NuBus)
and its 68030 siblings **IIx / IIcx**,
the **V8 family** — **LC** (68020), **LC II** (68030 + PMMU + 68882),
**Classic II** (Eagle), **Color Classic** (Spice + Cuda) and the
**Mac TV** (Tinker Bell + Cuda, 68030 @ 31.3 MHz) — the **IIsi** (RBV
RAM-based video + Egret, 68030 @ 20 MHz) and **IIci** (RBV + PIC ADB
modem + discrete RTC, 68030 @ 25 MHz), the
**LC III / LC III+** (68030 @ 25 / 33 MHz + Sonora), the all-in-one
**LC 520 / LC 550 / Color Classic II** (Sonora + Cuda), the
**Mac IIvx / IIvi** (VASP + Egret), the **Centris 610 / 650** and **Quadra 610 / 650 / 800**
(djMEMC + IOSB), the **Quadra 700** (discrete 040 + DAFB TurboSCSI),
the **Quadra 605**
(68040 + FPU) and **LC 475 / LC 575 / Performa 475-575** (68LC040), both
+ 040 MMU — **32 machine profiles, every one boots the Finder**.
Sibling of [POMIIGS](../POMIIGS/) (Apple IIgs) and
[POM2](../POM2/) (Apple II), sharing their architecture and conventions. CPU
core: [Moira](https://github.com/dirkwhoffmann/Moira) (vendored via NeoST — see
`extern/moira/POM68K_VENDOR.md`).

## Build

```bash
./setup_imgui.sh                  # one-time: fetch Dear ImGui, create build/
cd build && cmake .. && make -j
ctest                             # 92 milestone gates (asset-dependent may soft-skip)
```

Requires CMake ≥ 3.16, a C++20 compiler, GLFW3 + OpenGL (GUI only).

Optional but worth it — a profile-guided build makes the interpreter ~33 %
faster (an interpreter is dispatch and branches, exactly what PGO predicts),
with bit-identical emulation:

```bash
cmake .. -DPOM68K_PGO=generate && make -j jitdev q605_boot_etalon
./q605_boot_etalon && POM68K_CPU_ENGINE=jit ./q605_boot_etalon   # training
cmake .. -DPOM68K_PGO=use && make -j
```

## Run

ROM size selects the machine: **128 KB** = Mac Plus, **256 KB** = Mac II
(`9779D2C4`/`97851DB6`) or the 68030 IIx/IIcx (`97221136`,
`POM68K_MACII_MODEL=iix`/`iicx`/`fdhd`), or — by checksum — the compact
68000 siblings **Mac SE** (`B2E362A8`) and **SE FDHD** (`B306E171`);
**Mac Classic** (`A49F9914`) is a 512 KB compact,
**512 KB** = V8 family (header checksum picks LC `350EACF0` / LC II
`35C28F5F` / Classic II `3193670E` / IIsi `36B7FB6C` / IIci `368CADFE`),
**1 MB** = Color
Classic (`ECD99DC0`), Mac TV (`EAF1678D`), LC III (`ECBBC41C`), Quadra 700
(`420DBFF3`), Quadra 630 / LC 580 (`06684214` / `064DC91D`), else
Quadra 605 / LC 475. Without a
ROM argument the app probes `roms/macplus.rom`, `roms/macii.rom`,
`roms/maclcii.rom`, `roms/quadra605.rom`, then scans `roms/` for CRC
signatures (Mac II `9779D2C4`, LC II `35C28F5F`, Quadra `FF7439EE`).
The **Machine** menu switches profiles the same way. Several ROMs serve
sibling models that differ only by clock / CPU / model ID: the LC III ROM
also boots the **LC III+** (33 MHz, `POM68K_LC3_PLUS=1`), the
FF7439EE ROM boots the **LC 475** (68LC040, default), the
**Quadra 605** (68040 + FPU, `POM68K_Q605_ID=A55A2225`) or the all-in-one
**LC 575** (68LC040 @ 33 MHz, `POM68K_Q605_ID=A55A222E`), and the
all-in-one `EDE66CBD` ROM boots the **LC 520** (25 MHz, default), the
**LC 550** (33 MHz, `POM68K_AIO_ID=A55A0101`) or the **Color Classic II**
(33 MHz, 512×384, `POM68K_AIO_ID=CC2`), and the `4957EB49` ROM boots the
**Mac IIvx** (32 MHz, default) or **IIvi** (16 MHz, `POM68K_IIVI=1`), and the
`F1A6F343`/`F1ACAD13` ROM boots the **Centris 650** (25 MHz, default) or
**Centris 610** (20 MHz), or the **Quadra 650** (33 MHz) / **Quadra 610** /
**Quadra 800** (33 MHz, + SONIC Ethernet and NuBus) — all full 68040 — via
`POM68K_CENTRIS_MODEL=c610/q650/q610/q800`. The Color
Classic runs its Cuda MCU as firmware LLE off `roms/cuda/341s0788.bin`,
the LC 520/550 theirs off `roms/cuda/341s0060.bin` (Cuda 2.40 — 2.37
livelocks this ROM), the LC III / LC III+ their Egret off
`roms/egret/341s0851.bin` (HLE fallback without).

```bash
./build/POM68K                                    # no ROM → built-in 68000 demo
./build/POM68K roms/macplus.rom                   # Mac Plus (128 KB)
./build/POM68K roms/macplus.rom disks35/Disk605.dsk hdv/HD20SC.vhd
./build/POM68K "roms/256KB ROMs/1987-12 - 9779D2C4 - MacII (800k v2).ROM" hdv/HD20SC.vhd
./build/POM68K roms/maclcii.rom hdv/GISTPERSO-boot.vhd
./build/POM68K roms/maclcii.rom hdv/boot.vhd hdv/data.vhd   # + SCSI IDs 1–6
./build/POM68K roms/quadra605.rom hdv/MacOS-8.1-boot.vhd
```

ROMs are copyrighted and **never** part of the repository. Without one,
POM68K boots a built-in hand-assembled 68000 demo that clears the boot
overlay through the VIA and animates a pattern in the 512×342 framebuffer.

Local working copies live under `roms/` (system ROMs by size, plus
`roms/cuda/`, `roms/egret/`, `roms/adbmodem/`, and convenience symlinks
like `roms/macplus.rom`). See [`roms/README.md`](roms/README.md).
`roms/archive/macroms/` is the unpacked
[Mac ROMs](https://archive.org/details/macroms) collection (deduped
against the active trees) — reference inventory only, not shipped with
the repo.

### Mac Plus

Arguments: `[ROM] [floppy] [SCSI]`. Defaults probe `disks35/Disk605.dsk`
then `hdv/HD20SC.vhd`. Boots System 6 from floppy or SCSI to the Finder.
Floppies are writable (real IWM write engine + GCR sector commit) and the
GUI **persists writes back to the image file** on eject and on exit
(raw `.dsk` and DiskCopy 4.2, checksum regenerated; atomic temp+rename;
opt out with `POM68K_FLOPPY_RO=1`). Tests stay in-memory.

### Mac II

A **256 KB ROM** selects the 68020 + Toby NuBus video machine; it boots
System 6 and 7 to the Finder. ADB (keyboard/mouse) runs the real
PIC1654S transceiver firmware as a **firmware LLE** — the **default since
2026-07-22** when the dump is present at `roms/adbmodem/342s0440-b.bin`
(1024 B, CRC `cffb33eb`, user-provided); the mouse moves live on this path
(`macii_mouse_etalon`). `POM68K_ADB_LLE=0`, or a missing dump, falls back
to the HLE byte-model transceiver (see `DEV.md` "Mac II ADB: PIC1654S
LLE").

### Mac LC II

A **512 KB ROM** selects V8 + 68030 (+ 68882 by default; `POM68K_NOFPU=1`
for a bare LC II). Default video is 640×480 13" RGB; `POM68K_MONITOR=512`
forces the 512×384 12" mode (also switchable live in the CPU window).

The second argument is the boot SCSI image (default
`hdv/GISTPERSO-boot.vhd`, then `hdv/boot.vhd`, `hdv/HD20SC.vhd`); further
arguments attach as secondary volumes at SCSI IDs 1–6. Bare HFS `.dsk`
files (Infinite Mac style, `'LK'` at LBA 0) get an in-memory SCSI façade
automatically; otherwise use a DDM-wrapped image (`tools/wrap_hfs.py`).
PRAM + clock persist next to the boot image (`<disk>.pram`) — the first
cold boot runs the ROM's full-RAM burn-in; later boots skip it.

### Quadra 605 / LC 475 / LC 575

A **1 MB ROM** (FF7439EE) selects MEMCjr/PrimeTime. It serves three models:
the **LC 475 / Performa 475** (model ID `$A55A2221`, 68LC040 + soft 68882,
the default and the `lc475_boot_etalon` gate), the **Quadra 605**
(`POM68K_Q605_ID=A55A2225`, full 68040 + FPU, MAME `macqd605`) and the
all-in-one **LC 575 / Performa 575** (`POM68K_Q605_ID=A55A222E`, 68LC040
@ 33 MHz, gate `lc575_boot_etalon`).
`POM68K_Q605_NOFPU=2` forces a bare `FPUModel::NONE`. Boots System
7.5 / 7.5.5 / 7.6 and Mac OS 8.1 to the Finder. Default boot disk
`hdv/MacOS-8.1-boot.vhd`, then `hdv/boot.vhd`. Optional SuperDrive floppy
(SWIM2: 800K GCR **and** 1.44 MB MFM media) via `POM68K_FLOPPY` or
`disks35/`; `.dsk` / `.image` args insert as floppy rather than SCSI.
Cuda XPRAM persists as `<disk>.pram`. Video is 640×480 DAFB (incl.
256-color Finder). Tuning: `POM68K_Q605_CACHE_BOOST` (default 4) scales
the 040 i-cache throughput overlay; `POM68K_MMU040_WALK=1` disables the
ATC fast path (debug).

### Controls

The mouse drives the Mac while hovering the screen; a drag started on the
screen (Finder drag-and-drop) keeps tracking outside it and never moves
the host window (title bar still does). **Delete** toggles full mouse
capture (cursor grabbed, raw motion). The **Machine** menu switches
between the 27 profiles (needs the matching ROM; the app
relaunches). On
LC II and Quadra, **Disques** picks the boot volume and toggles secondary
SCSI images next to the current one (relaunches — the ROM only scans the
bus at boot), and **Redémarrer** power-cycles the machine.

### Mechanical drive sounds

Floppy and hard-disk activity is audible: head steps and seeks, spindle
spin-up/loop/down, insert/eject clicks (MAME's floppy sample set, ported
via POM2 — `assets/floppy_samples/`, BSD-3-Clause). The 3.5" set voices
the Sony drives on every machine; the 5.25" set plays at low gain as
the SCSI hard-disk seek proxy. Toggle with **Machine ▸ Sons des
lecteurs**, or start muted with `POM68K_DRIVE_SFX=0`.

## AppleTalk: file sharing, printing and the internet (built in)

POM68K carries the whole AppleTalk service side **in-process** — no
external router, server or gateway, no root. It is **on by default in
the GUI**; open **Réseau → AppleTalk** to see and toggle it. The window
reports, live, whether each piece works:

- **Réseau (nœud/routeur)** — zone "POM68K", the guest's node, and
  frame / NBP / ATP counters.
- **AppleShare** — a host folder shared as an AFP volume. Pick it in
  the guest's **Chooser → AppleShare** (log in as Guest). The window
  shows the server name, whether the folder is writable, live sessions,
  the last user/command and bytes moved. The shared folder defaults to
  **`AppleShare/` at the repo root** (created if absent — the window's
  "Dossier hôte" line shows the exact path); override with
  `POM68K_SHARE_DIR=/path`. Resource forks + Finder info are kept in
  netatalk-compatible `.AppleDouble` sidecars. The volume takes the
  shared folder's own name. Copies run over a *lossless* virtual LocalTalk
  clocked ~8× above the real 28 KB/s wire — the SCC exerts backpressure
  (a real cable can't), so a frame is held rather than dropped and the
  1-2 s retransmit stalls go away; `POM68K_ATALK_WIRE_BOOST=N` tunes it
  (`=1` = authentic LocalTalk speed). Large files still take a while —
  that era's file sharing was slow — but the transfer stays smooth.
  Backpressure is bounded: a guest that stops listening long enough to
  fill the 64-frame hold queue starts losing frames like it would on a
  real cable, because holding them forever is worse than a retransmit
  (they are already too old to be useful, and the queue would grow
  without limit). The window reports it as "Débordement du fil", next to
  the retransmission count, its lag and the queue depth — all four read
  0 / shallow on a healthy transfer.
- **Imprimante** — a LaserWriter in the Chooser. Print to it and the
  PostScript is spooled to CUPS (`lp`) if present, else a timestamped
  `.ps` file under `run/print`. The window shows idle/busy, the job
  count and where the last job went.
- **Internet (MacIP)** — real TCP/IP for the guest tunneled in DDP. In
  the **TCP/IP** (Open Transport) or **MacTCP** control panel choose
  *AppleTalk (MacIP)*, server zone POM68K. A user-mode NAT proxies it
  onto the host — no `tun`, no `iptables`. The window shows the
  gateway, the DNS it advertises, and each guest's lease (a lease means
  it works). **Plain HTTP only** (1990s TLS can't reach 2026 sites) —
  try <http://frogfind.com> or <http://theoldnet.com>.

`POM68K_APPLETALK=0` disables the whole stack. Gated:
`atalk_stack_test`, `afp_server_test`, `pap_server_test`,
`macip_gw_test`. Full protocol notes: `docs/APPLETALK.md` (§6.5 covers
the in-process stack).

When something goes wrong on the wire, two tracers write to stderr:
`POM68K_ATALK_DEBUG=1` logs DDP/NBP/ATP traffic, and one line per client
retransmission carrying the delay since our own reply — ~1-2 s means the
guest's ATP timer fired (the reply never got through), much less means it
arrived damaged. `POM68K_MACIP_DEBUG=1` logs every IP datagram crossing
the gateway in both directions, with TCP flags/seq/ack; if the guest's
network stack falls over, the last line before it is the datagram that
did it.

### Reaching a real LocalTalk network (LToUDP + external bridge)

To interoperate with **other** machines instead of POM68K's own stack,
add `POM68K_LTOUDP=1`: the SCC printer port joins a virtual LLAP cable
over UDP multicast (`239.192.76.84:1954`, the Mini vMac / TashRouter
LToUDP format). The internal node is multicast alongside, so both
coexist. `POM68K_APPLETALK=1` additionally seeds AppleTalk active in
PRAM (System 7 opens LocalTalk at boot; System 6 opens it from the
Chooser). Gated: `llap_loop_test`, `ltoudp_test`,
`llap_two_system_etalon`.

For AppleShare/printing served by the **real** netatalk over a routed
segment, the repo still vendors the full external bridge: TashRouter
(`extern/tashrouter`) and netatalk **2.4.9** (`extern/netatalk2`) —
build with `tools/netatalk2/build_netatalk2.sh`, then follow
`tools/netatalk2/README.md`; MacIP toward a host `tun` uses
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
decoded to native forks (usable immediately); `.zip` archives are expanded
host-side; `.sit`/`.hqx` keep their StuffIt types (unstuff in the guest);
CD images (`.toast`/`.cdr`/`.iso`) are extracted next to the output and
attach directly as SCSI disks. The volume is writable (write-back); split
past 1.9 GB (`--max-mb`), filter with `--only 'glob'`.

## Headless tools

```bash
./build/demo_screenshot --frames 60 --out shot.ppm   # PPM screenshot
./build/cpu_smoke                                    # end-to-end CPU gate
```

## License

GPLv3. Moira is MIT (Dirk W. Hoffmann); Dear ImGui is MIT (fetched, not
vendored).
