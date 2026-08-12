# dev/ — guest-side applications (Retro68)

Macintosh applications that run **inside** POM68K, built with the
[Retro68](https://github.com/autc04/Retro68) cross-toolchain
(`m68k-apple-macos-gcc`). They are the guest half of the conformance
loop: `tools/` probes the emulator from the host, `dev/` probes it from
the inside.

| Project | What it is |
|---|---|
| `prober/` | **POM68K Prober** — tests POM68K *from the inside*: machine/ROM/chip identity (Gestalt + low-mem + a bus-error topology sweep), AppleTalk state (NBP), CPU/FPU/QuickDraw benchmarks, video inventory, Power Manager, ADB/drives/volumes/slots/sound. Writes its raw findings as a TSV file **next to the application** (primary output, needs no network) and as JSON Lines on the mounted AFP volume (secondary, for the host-side loop). Spec: `prober/SPEC.md`. `compat/` carries the `Lists.h`/AppleTalk/Power-Manager glue absent from Retro68's multiversal interfaces. |
| `mac-rogue/` | **TMS_Rogue port** — the Berlin-Interpretation roguelike from POM1's Apple-1 + TMS9918 build (`POM1 sketchs/tms9918/game_rogue`), as a real Mac app: 256×192 TMS frame in a window, colour where colour exists, B&W where it does not — one binary from the Mac Plus to the Quadra 950. `mac-rogue/README.md` has the status and asset pipeline (`tools/extract_rogue_assets.py`). |
| `Retro68/`, `Retro68-build/` | The toolchain — **not committed** (user-built, ~11 GB, gitignored). Bootstrap below. |

## Toolchain bootstrap (once)

```bash
cd dev
git clone https://github.com/autc04/Retro68.git   # + its submodules
mkdir Retro68-build && cd Retro68-build
../Retro68/build-toolchain.bash
```

The toolchain lands in `dev/Retro68-build/toolchain/`; every project's
build recipe points at
`Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake`.

## Building an app

```bash
cd dev/prober          # or mac-rogue
mkdir -p build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=../../Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake
make
```

`add_application()` (from the toolchain file) emits three artifacts per
app: `.APPL` (the file's data fork is often **0 bytes on the host — that
is normal**, the code lives in the resource fork), `.bin` (MacBinary,
the transportable form) and `.dsk` (a mountable 800 K disk image).

## Getting the app into POM68K

- **Floppy**: the `.dsk` mounts directly — GUI *Disques* menu, CLI
  argument, or `POM68K_FLOPPY=<path>`.
- **SCSI volume**: bake the MacBinary into a data-only HFS volume and
  attach it as a secondary disk. `dir2hfs.py` decodes `.bin` to native
  data+resource forks with the embedded Type/Creator, so the app is
  runnable the moment the volume mounts:

  ```bash
  .venv-tools/bin/python tools/dir2hfs.py <dir-holding-the-.bin> hdv/PROBER
  ./build/POM68K <ROM> hdv/boot.vhd hdv/PROBER.vhd
  ```

  (`tools/wrap_hfs.py` is a different job — it wraps an *already
  bootable* bare HFS volume, one carrying `LK` boot blocks, into a
  partitioned Apple SCSI image. It rejects a `dir2hfs.py` volume, whose
  boot blocks are deliberately zero.)
- **AFP share**: with the in-process AppleTalk stack on, drop the `.bin`
  in the `POM68K_SHARE_DIR` and fetch it from the guest — the same
  volume the prober writes its JSONL report back to.

House rules that apply here: artifacts (`build/`, `*.dsk`) are never
committed; the toolchain is user-built like ROMs are user-provided; a
report that only exists inside the guest is not a result until it is
back on the host (the prober's whole design).
