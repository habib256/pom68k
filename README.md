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
./build/POM68K                    # no ROM → built-in 68000 demo pattern
./build/POM68K roms/macplus.rom   # user-provided Mac Plus ROM (128 KB)
```

ROMs are copyrighted and **never** part of the repository: drop your own
`macplus.rom` dump into `roms/`. Without one, POM68K boots a built-in
hand-assembled 68000 demo that clears the boot overlay through the VIA and
animates a pattern in the 512×342 framebuffer — the same code path a real
ROM takes.

## Headless tools

```bash
./build/demo_screenshot --frames 60 --out shot.ppm   # PPM screenshot
./build/cpu_smoke                                    # end-to-end CPU gate
```

## License

GPLv3. Moira is MIT (Dirk W. Hoffmann); Dear ImGui is MIT (fetched, not
vendored).
