# POM68K — Macintosh 68k emulator

Macintosh **Plus** (68000, cycle-exact), Macintosh **II** (68020 + Toby NuBus),
Macintosh **LC II** (68030 + PMMU + 68882), and **Quadra 605 / LC 475**
(68040/68LC040 + 040 MMU). Sibling of [POMIIGS](../POMIIGS/) (Apple IIgs) and
[POM2](../POM2/) (Apple II), sharing their architecture and conventions. CPU
core: [Moira](https://github.com/dirkwhoffmann/Moira) (vendored via NeoST — see
`extern/moira/POM68K_VENDOR.md`).

## Build

```bash
./setup_imgui.sh                  # one-time: fetch Dear ImGui, create build/
cd build && cmake .. && make -j
ctest                             # 41 milestone gates (asset-dependent may soft-skip)
```

Requires CMake ≥ 3.16, a C++20 compiler, GLFW3 + OpenGL (GUI only).

## Run

ROM size selects the machine: **128 KB** = Mac Plus, **256 KB** = Mac II,
**512 KB** = LC II, **1 MB** = Quadra 605 / LC 475. Without a ROM argument the
app probes `roms/macplus.rom`, `roms/macii.rom`, `roms/maclcii.rom`,
`roms/quadra605.rom`, then scans `roms/` for CRC signatures (Mac II
`9779D2C4`, LC II `35C28F5F`, Quadra `FF7439EE`). The **Machine** menu
switches profiles the same way.

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

### Mac Plus

Arguments: `[ROM] [floppy] [SCSI]`. Defaults probe `disks35/Disk605.dsk`
then `hdv/HD20SC.vhd`. Boots System 6 from floppy or SCSI to the Finder.

### Mac II

A **256 KB ROM** selects the 68020 + Toby NuBus video machine; it boots
System 6 and 7 to the Finder. ADB (keyboard/mouse) uses an HLE
transceiver by default. An experimental **LLE** ADB path runs the real
PIC1654S transceiver firmware — set `POM68K_ADB_LLE=1` and drop the dump
at `roms/adbmodem/342s0440-b.bin` (1024 B, CRC `cffb33eb`, user-provided).
It is a work in progress (see `DEV.md` "Mac II ADB: PIC1654S LLE") and not
yet the default.

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

### Quadra 605 / LC 475

A **1 MB ROM** selects MEMCjr/PrimeTime + 68040 (MAME `macqd605`
default; `POM68K_Q605_NOFPU=1` → 68LC040 + soft 68882). Boots System
7.5 / 7.5.5 / 7.6 and Mac OS 8.1 to the Finder. Default boot disk
`hdv/MacOS-8.1-boot.vhd`, then `hdv/boot.vhd`. Optional SuperDrive floppy
(SWIM2: 800K GCR **and** 1.44 MB MFM media) via `POM68K_FLOPPY` or
`disks35/`; `.dsk` / `.image` args insert as floppy rather than SCSI.
Cuda XPRAM persists as `<disk>.pram`. Video is 640×480 DAFB (incl.
256-color Finder). Tuning: `POM68K_Q605_CACHE_BOOST` (default 1) scales
the 040 i-cache throughput overlay; `POM68K_MMU040_WALK=1` disables the
ATC fast path (debug).

### Controls

The mouse drives the Mac while hovering the screen; a drag started on the
screen (Finder drag-and-drop) keeps tracking outside it and never moves
the host window (title bar still does). **Delete** toggles full mouse
capture (cursor grabbed, raw motion). The **Machine** menu switches
Plus / Mac II / LC II / Quadra (needs the matching ROM; the app
relaunches). On
LC II and Quadra, **Disques** picks the boot volume and toggles secondary
SCSI images next to the current one (relaunches — the ROM only scans the
bus at boot), and **Redémarrer** power-cycles the machine.

## LocalTalk between instances (experimental)

`POM68K_LTOUDP=1 ./build/POM68K …` plugs the SCC printer port into a
virtual LLAP cable over UDP multicast (`239.192.76.84:1954`, the
Mini vMac / TashRouter LToUDP format) on all four machines. Two
instances on the same LAN share the wire; the guest-visible AppleTalk
stack is the real one. Add `POM68K_APPLETALK=1` to seed AppleTalk
active in PRAM (skips the Chooser toggle; System 7 then opens LocalTalk
at boot — System 6 only opens it from the Chooser). Gated:
`llap_loop_test` (incl. the RTS/CTS directed-frame dialogue),
`ltoudp_test`, `llap_two_system_etalon` (two full Systems acquire
distinct LLAP node IDs over the shared cable).

To reach AppleShare/printers beyond the virtual cable, bridge with
[TashRouter](https://github.com/lampmerchant/tashrouter) (its `LtoudpPort`
speaks the same wire format — interop verified against its address
probes) and a DDP-capable AFP server (netatalk **2.x**; 3.x dropped
AppleTalk):

```python
# python3 router.py — minimal TashRouter joining our cable
from tashrouter.port.localtalk.ltoudp import LtoudpPort
from tashrouter.router.router import Router
router = Router('router', ports=(
    LtoudpPort(seed_network=1, seed_zone_name=b'POM68K Net'),))
router.start()
```

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
