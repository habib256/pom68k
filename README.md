# POM68K — Macintosh 68k emulator

Macintosh **Plus** (68000, cycle-exact) now; Macintosh **LC II** (68030 + MMU)
as phase 2. Sibling of [POMIIGS](../POMIIGS/) (Apple IIgs) and
[POM2](../POM2/) (Apple II), sharing their architecture and conventions.
CPU core: [Moira](https://github.com/dirkwhoffmann/Moira) (vendored via
NeoST — see `extern/moira/POM68K_VENDOR.md`).

## Build

```bash
./setup_imgui.sh                  # one-time: fetch Dear ImGui, create build/
cd build && cmake .. && make -j
ctest                             # run the milestone gates
```

Requires CMake ≥ 3.16, a C++20 compiler, GLFW3 + OpenGL (GUI only).

## Run

```bash
./build/POM68K                          # no ROM → built-in 68000 demo pattern
./build/POM68K roms/macplus.rom         # Mac Plus ROM (128 KB)
./build/POM68K roms/maclcii.rom hdv/disk.vhd   # 512 KB ROM → Mac LC II (68030)
./build/POM68K roms/maclcii.rom hdv/boot.vhd hdv/data.vhd  # + secondary SCSI volumes
```

ROMs are copyrighted and **never** part of the repository: drop your own
`macplus.rom` dump into `roms/`. Without one, POM68K boots a built-in
hand-assembled 68000 demo that clears the boot overlay through the VIA and
animates a pattern in the 512×342 framebuffer — the same code path a real
ROM takes.

A **512 KB ROM selects the Mac LC II machine** (68030 + PMMU, V8 gate
array, Egret ADB, 512×384 video — work in progress, O6): the second
argument is the boot SCSI disk image (default `hdv/GISTPERSO-boot.vhd`,
then `hdv/boot.vhd`, `hdv/HD20SC.vhd`); further arguments attach as
secondary volumes at SCSI IDs 1-6, mounted by the System's boot-time
bus scan. PRAM + clock persist next to the boot image (`<disk>.pram`)
— the first cold boot runs the ROM's full-RAM burn-in, which is long;
later boots skip it, like a real battery-backed machine.

**Controls**: the mouse drives the Mac while hovering the screen; a drag
started on the screen (Finder drag-and-drop) keeps tracking outside it
and never moves the host-side window (its title bar still does). The
**Delete key toggles full mouse capture** (cursor grabbed, raw motion;
press Delete again to release). The **Machine menu** in the top menu bar
switches between the Mac Plus and Mac LC II profiles (needs the matching
ROM in `roms/`; the app relaunches on the other machine). On the LC II
the **Disques menu** picks the boot volume and toggles secondary
volumes from the images found next to the current one (changing the
set relaunches the emulator — the ROM only scans the SCSI bus at
boot), and **Redémarrer** power-cycles the machine.

## Headless tools

```bash
./build/demo_screenshot --frames 60 --out shot.ppm   # PPM screenshot
./build/cpu_smoke                                    # end-to-end CPU gate
```

## License

GPLv3. Moira is MIT (Dirk W. Hoffmann); Dear ImGui is MIT (fetched, not
vendored).
