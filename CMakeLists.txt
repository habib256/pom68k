cmake_minimum_required(VERSION 3.16)
project(POM68K VERSION 0.1 LANGUAGES CXX)

# Whole tree is C++20: Moira requires it (std::rotr, map::contains) and
# Cpu68k.h includes Moira.h. (POMIIGS is C++17; divergence documented in
# CLAUDE.md.)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ─── Host-tuned optimization (perf pass 2026-07-17) ──────────────────────
# The LC II at cache-boost 4 needs every cycle the host can give; -march=
# native + LTO buy ~15-25% on the Moira hot loop. ON by default (POM68K is
# built from source on the machine that runs it); disable for a portable
# binary with -DPOM68K_NATIVE=OFF.
option(POM68K_NATIVE "Optimize for the build host CPU (-march=native + LTO)" ON)
if(POM68K_NATIVE AND NOT MSVC)
    add_compile_options(-march=native)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT pom68k_ipo)
    if(pom68k_ipo)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
endif()

# ─── Optional sanitizer build (POM2/POMIIGS convention) ───────────────────
set(POM68K_SANITIZE "" CACHE STRING "Build with -fsanitize=<value>. Empty = off.")
if(POM68K_SANITIZE AND NOT MSVC)
    add_compile_options(-fsanitize=${POM68K_SANITIZE} -fno-omit-frame-pointer -g)
    add_link_options   (-fsanitize=${POM68K_SANITIZE})
endif()

# ─── softfloat (vendored 80-bit softfloat — see extern/softfloat/VENDOR.md) ─
# O5: backs Moira's 68882 FPU. Plain C, kept as a separate static lib
# (GPLv2+; Moira stays MIT and only links against it).
enable_language(C)
add_subdirectory(extern/softfloat)

# ─── Moira (vendored from NeoST — see extern/moira/POM68K_VENDOR.md) ──────
set(MOIRA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/extern/moira/Moira)
if(NOT EXISTS "${MOIRA_DIR}/Moira.cpp")
    message(FATAL_ERROR "Moira not found in ${MOIRA_DIR} — restore extern/moira from git.")
endif()
add_library(moira STATIC ${MOIRA_DIR}/Moira.cpp ${MOIRA_DIR}/MoiraDebugger.cpp)
target_include_directories(moira PUBLIC ${MOIRA_DIR})
target_link_libraries(moira PUBLIC softfloat68k)
if(NOT MSVC)
    target_compile_options(moira PRIVATE -Wno-unused-parameter -Wno-unused-variable)
endif()

# ─── Core library (headless — shared by GUI, tests, future oracles) ──────
add_library(pom68k_core STATIC
    src/Cpu68k.cpp        # M1 — Moira wrapper + contention
    src/MacMemory.cpp     # M2 — Mac Plus memory map + overlay
    src/Via6522.cpp       # M2/M4 — VIA (ports, timers)
    src/Rtc.cpp           # M4 — clock/PRAM serial chip
    src/Iwm.cpp           # M5 — floppy controller
    src/SonyDrive.cpp     # M5 — 800K GCR drive
    src/Scc8530.cpp       # M5.5 — SCC (mouse DCD interrupts)
    src/MacInput.cpp      # M5.5 — M0110 keyboard + quadrature mouse
    src/ScsiDisk.cpp      # M7 — SCSI direct-access target
    src/Ncr5380.cpp       # M7 — SCSI controller (phase engine)
    src/Ncr53c96.cpp      # Q6 — NCR 53C96 SCSI controller (Quadra 605)
    src/PseudoVia.cpp     # O6 — LC II pseudo-VIA (V8 interrupt core)
    src/V8Memory.cpp      # O6 — LC II memory map + V8 gate array
    src/Cpu030.cpp        # O6 — Moira 68030 wrapper (LC II)
    src/Egret.cpp         # O6 — Egret ADB/RTC/PRAM MCU (HLE)
    src/AdbBus.cpp        # O6 — ADB bus + HLE keyboard/mouse
    src/Asc.cpp           # O6 — ASC-V8 sound (FIFO A, 22 257 Hz)
    src/Swim2.cpp         # Q8 — PrimeTime/IOSB SWIM2 register + FIFO core
    src/Q605Memory.cpp    # Q5 — LC 475/Quadra 605 map (MEMCjr + PrimeTime)
    src/Dafb.cpp          # Q8 — DAFB II video cell (Swatch CRTC/Gazelle/RAMDAC)
    src/Cpu040.cpp        # Q5 — Moira 68LC040 wrapper
    src/DeclRom.cpp       # Mac II — NuBus declaration ROM builder
    src/NuBus.cpp         # Mac II — NuBus slot decode + IRQ
    src/TobyVideo.cpp     # Mac II — Toby Frame Buffer HLE
    src/MacIIMemory.cpp   # Mac II — 32-bit GLUE memory map
    src/Cpu020.cpp        # Mac II — Moira 68020 wrapper
    src/AdbVia.cpp        # Mac II — ADB HLE on VIA1
)
target_include_directories(pom68k_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(pom68k_core PUBLIC moira)

# ─── GUI executable (Dear ImGui, fetched by setup_imgui.sh) ──────────────
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/imgui)
if(EXISTS "${IMGUI_DIR}/imgui.cpp")
    set(IMGUI_SOURCES
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_demo.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
        ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp)
    add_executable(${PROJECT_NAME} src/main.cpp src/miniaudio_impl.cpp ${IMGUI_SOURCES})
    target_include_directories(${PROJECT_NAME} PRIVATE ${IMGUI_DIR} ${IMGUI_DIR}/backends)
    target_link_libraries(${PROJECT_NAME} PRIVATE pom68k_core)
    if(EMSCRIPTEN)
        set(EM_LINK "-sUSE_GLFW=3 -sUSE_WEBGL2=1 -sFULL_ES3=1 \
-sINITIAL_MEMORY=134217728 -sALLOW_MEMORY_GROWTH=1")
        set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS "${EM_LINK}" SUFFIX ".html")
    else()
        find_package(OpenGL REQUIRED)
        find_package(glfw3 3.3 QUIET CONFIG)
        if(glfw3_FOUND)
            set(GLFW_TARGET glfw)
        else()
            find_package(PkgConfig REQUIRED)
            pkg_check_modules(GLFW3 REQUIRED IMPORTED_TARGET glfw3)
            set(GLFW_TARGET PkgConfig::GLFW3)
        endif()
        target_link_libraries(${PROJECT_NAME} PRIVATE ${GLFW_TARGET} OpenGL::GL)
        if(UNIX AND NOT APPLE)
            find_package(Threads REQUIRED)
            target_link_libraries(${PROJECT_NAME} PRIVATE ${CMAKE_DL_LIBS} Threads::Threads m)
        endif()
    endif()
else()
    message(STATUS "Dear ImGui absent (${IMGUI_DIR}) — GUI target skipped. Run ./setup_imgui.sh.")
endif()

# ─── Tests (CTest) — every milestone is gated (POMIIGS discipline) ────────
option(POM68K_TESTS "Build the test targets" ON)
if(POM68K_TESTS AND NOT EMSCRIPTEN)
    enable_testing()

    # M1 gate: Moira boots the demo ROM through the real machine path.
    add_executable(cpu_smoke tests/cpu_smoke.cpp)
    target_link_libraries(cpu_smoke PRIVATE pom68k_core)
    add_test(NAME cpu_smoke COMMAND cpu_smoke)

    # M3 gate + dev tool: headless run → PPM screenshot.
    add_executable(demo_screenshot tests/demo_screenshot.cpp)
    target_link_libraries(demo_screenshot PRIVATE pom68k_core)
    add_test(NAME demo_screenshot COMMAND demo_screenshot --frames 30)

    # Dev tool (not a gate): trace a real ROM boot — hot PCs, overlay, screen.
    # EXCLUDE_FROM_ALL: dev tools are not built by `make`/`ctest` (they'd relink
    # the whole core with LTO each time — slow); build on demand: `make dasm`.
    add_executable(boot_trace EXCLUDE_FROM_ALL tests/boot_trace.cpp)
    target_link_libraries(boot_trace PRIVATE pom68k_core)

    # Dev tool: disassemble ROM ranges (Moira dasm).
    add_executable(dasm EXCLUDE_FROM_ALL tests/dasm.cpp)
    target_link_libraries(dasm PRIVATE pom68k_core)

    # M4 gate: RAM/video contention budget vs GttMFH numbers.
    add_executable(contention_test tests/contention_test.cpp)
    target_link_libraries(contention_test PRIVATE pom68k_core)
    add_test(NAME contention_test COMMAND contention_test)

    # M4 gate: real-ROM boot to the blinking-? (soft-skips without a ROM).
    add_executable(rom_boot_etalon tests/rom_boot_etalon.cpp)
    target_link_libraries(rom_boot_etalon PRIVATE pom68k_core)
    add_test(NAME rom_boot_etalon COMMAND rom_boot_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # M5 gate: GCR encode/decode roundtrip (independent MAME-ported decoder).
    add_executable(gcr_test tests/gcr_test.cpp)
    target_link_libraries(gcr_test PRIVATE pom68k_core)
    add_test(NAME gcr_test COMMAND gcr_test)

    # M5 gate: real ROM boots a synthetic floppy through the full IWM/GCR
    # chain and executes our boot-block code (soft-skips without a ROM).
    add_executable(disk_boot_etalon tests/disk_boot_etalon.cpp)
    target_link_libraries(disk_boot_etalon PRIVATE pom68k_core)
    add_test(NAME disk_boot_etalon COMMAND disk_boot_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # M5 gate: boot a real System 6 floppy to the Finder (soft-skips
    # without user-provided roms/macplus.rom + disks35/Disk605.dsk).
    add_executable(system_boot_etalon tests/system_boot_etalon.cpp)
    target_link_libraries(system_boot_etalon PRIVATE pom68k_core)
    add_test(NAME system_boot_etalon COMMAND system_boot_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # M7 gate: SCSI target device in isolation (soft-skips without image).
    add_executable(scsi_disk_test tests/scsi_disk_test.cpp)
    target_link_libraries(scsi_disk_test PRIVATE pom68k_core)
    add_test(NAME scsi_disk_test COMMAND scsi_disk_test
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # M6 gate: the startup chime is a real decaying tone (soft-skips).
    add_executable(sound_test tests/sound_test.cpp)
    target_link_libraries(sound_test PRIVATE pom68k_core)
    add_test(NAME sound_test COMMAND sound_test
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # M7 gate: the NCR 5380 phase engine driven like the ROM's SCSI Manager.
    add_executable(ncr5380_test tests/ncr5380_test.cpp)
    target_link_libraries(ncr5380_test PRIVATE pom68k_core)
    add_test(NAME ncr5380_test COMMAND ncr5380_test
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # Q6 gate: the NCR 53C96 command-driven phase engine driven like the
    # Mac OS 8.1 SCSI Manager on the Quadra 605 (soft-skips without an image).
    add_executable(ncr53c96_test tests/ncr53c96_test.cpp)
    target_link_libraries(ncr53c96_test PRIVATE pom68k_core)
    add_test(NAME ncr53c96_test COMMAND ncr53c96_test
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # M7 gate: full ROM boot from SCSI hard disk to the Finder (soft-skips).
    add_executable(scsi_boot_etalon tests/scsi_boot_etalon.cpp)
    target_link_libraries(scsi_boot_etalon PRIVATE pom68k_core)
    add_test(NAME scsi_boot_etalon COMMAND scsi_boot_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    add_executable(scsi_hfs_facade_test tests/scsi_hfs_facade_test.cpp)
    target_link_libraries(scsi_hfs_facade_test PRIVATE pom68k_core)
    add_test(NAME scsi_hfs_facade_test COMMAND scsi_hfs_facade_test
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # M5.5 gate: keyboard/mouse against the System 6 drivers (soft-skips).
    add_executable(input_etalon tests/input_etalon.cpp)
    target_link_libraries(input_etalon PRIVATE pom68k_core)
    add_test(NAME input_etalon COMMAND input_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # O6.10 gate: SCC external/status interrupt for AppleTalk carrier
    # sense (LC II open-line Break/Abort → LAP manager unblocks).
    add_executable(scc_ext_test tests/scc_ext_test.cpp)
    target_link_libraries(scc_ext_test PRIVATE pom68k_core)
    add_test(NAME scc_ext_test COMMAND scc_ext_test)

    # M4.5 gate: SingleStepTests/680x0 68000 vectors against bare Moira.
    # Data is user-fetched (~1 GB): tests/fetch_sst_68000.sh <dir>.
    # Soft-skips when absent.
    add_executable(sst68000 tests/sst68000.cpp)
    target_link_libraries(sst68000 PRIVATE moira)
    set(POM68K_SST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/sst68000"
        CACHE PATH "Directory of SingleStepTests/680x0 68000 .json vectors")
    add_test(NAME sst68000 COMMAND sst68000 "${POM68K_SST_DIR}")

    # Phase-2 gate (O4): oracle-agreed SST030 vectors against Moira M68030.
    # Corpus is generated locally: oracle/fuzz/fuzz030.py (see oracle/README.md).
    # Soft-skips when absent.
    add_executable(sst68030 tests/sst68030.cpp)
    target_link_libraries(sst68030 PRIVATE moira)
    set(POM68K_SST030_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/sst68030"
        CACHE PATH "Directory of oracle-generated SST030 .json vectors")
    add_test(NAME sst68030 COMMAND sst68030 "${POM68K_SST030_DIR}")

    # Phase-3 gate (Q2): WinUAE-solo SST040 vectors against Moira M68LC040.
    # Corpus is generated locally: oracle/fuzz/fuzz040.py. Soft-skips when
    # absent.
    add_executable(sst68040 tests/sst68040.cpp)
    target_link_libraries(sst68040 PRIVATE moira)
    set(POM68K_SST040_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/sst68040"
        CACHE PATH "Directory of oracle-generated SST040 .json vectors")
    add_test(NAME sst68040 COMMAND sst68040 "${POM68K_SST040_DIR}")

    # O6.2 gates: LC II pseudo-VIA semantics + V8 memory controller.
    add_executable(pseudovia_test tests/pseudovia_test.cpp)
    target_link_libraries(pseudovia_test PRIVATE pom68k_core)
    add_test(NAME pseudovia_test COMMAND pseudovia_test)

    add_executable(v8_ramsize tests/v8_ramsize.cpp)
    target_link_libraries(v8_ramsize PRIVATE pom68k_core)
    add_test(NAME v8_ramsize COMMAND v8_ramsize)

    # O6.6 gate: ASC-V8 FIFO/level-IRQ semantics (boot beep depends on it).
    add_executable(asc_test tests/asc_test.cpp)
    target_link_libraries(asc_test PRIVATE pom68k_core)
    add_test(NAME asc_test COMMAND asc_test)

    # Q8 gate: PrimeTime/IOSB $BB ASC stereo FIFOs + level IRQ semantics.
    add_executable(q605_asc_test tests/q605_asc_test.cpp)
    target_link_libraries(q605_asc_test PRIVATE pom68k_core)
    add_test(NAME q605_asc_test COMMAND q605_asc_test)

    # Q8 gate: FF7439EE + Mac OS 8.1 boot to 640x480x8 Finder (soft-skip).
    add_executable(q605_boot_etalon tests/q605_boot_etalon.cpp)
    target_link_libraries(q605_boot_etalon PRIVATE pom68k_core)
    add_test(NAME q605_boot_etalon COMMAND q605_boot_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # Q8 gate: same Finder boot under POM68K_Q605_NOFPU (real 68LC040, no
    # 68882) — UniversalInfo FPU bit cleared so System installs PACK 4.
    add_executable(q605_nofpu_boot_etalon tests/q605_nofpu_boot_etalon.cpp)
    target_link_libraries(q605_nofpu_boot_etalon PRIVATE pom68k_core)
    add_test(NAME q605_nofpu_boot_etalon COMMAND q605_nofpu_boot_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # Q8.2 gate: TRUE bare no-FPU (NOFPU=2, FPUModel::NONE) Finder boot —
    # guards the _FP68K integer-PACK-4 binding (XPRAM $AE combo read).
    add_executable(q605_barefpu_boot_etalon tests/q605_barefpu_boot_etalon.cpp)
    target_link_libraries(q605_barefpu_boot_etalon PRIVATE pom68k_core)
    add_test(NAME q605_barefpu_boot_etalon COMMAND q605_barefpu_boot_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # Q8 gate: PrimeTime SWIM2 register/FIFO core and 16-bit bus mapping.
    add_executable(swim2_test tests/swim2_test.cpp)
    target_link_libraries(swim2_test PRIVATE pom68k_core)
    add_test(NAME swim2_test COMMAND swim2_test)

    # Q8 gate: SWIM2 + SonyDrive MFM 1.44MB / GCR 800K media + sector R/W.
    add_executable(swim2_media_test tests/swim2_media_test.cpp)
    target_link_libraries(swim2_media_test PRIVATE pom68k_core)
    add_test(NAME swim2_media_test COMMAND swim2_media_test)

    # Q8 gate: Quadra SuperDrive stack (synthetic HD image via SWIM2).
    add_executable(q605_floppy_boot_etalon tests/q605_floppy_boot_etalon.cpp)
    target_link_libraries(q605_floppy_boot_etalon PRIVATE pom68k_core)
    add_test(NAME q605_floppy_boot_etalon COMMAND q605_floppy_boot_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # O6.3 gate: Egret HLE transport + command set (protocol pinned from
    # the LC II ROM's own drivers — see src/Egret.cpp).
    add_executable(egret_test tests/egret_test.cpp)
    target_link_libraries(egret_test PRIVATE pom68k_core)
    add_test(NAME egret_test COMMAND egret_test)

    # O6.4 gate: V8 video decode + Ariel palette + VBL cadence.
    add_executable(v8_video_test tests/v8_video_test.cpp)
    target_link_libraries(v8_video_test PRIVATE pom68k_core)
    add_test(NAME v8_video_test COMMAND v8_video_test)

    # Q8 gate: MEMCjr DAFB stride register + Antelope RAMDAC depth modes.
    add_executable(q605_dafb_test tests/q605_dafb_test.cpp)
    target_link_libraries(q605_dafb_test PRIVATE pom68k_core)
    add_test(NAME q605_dafb_test COMMAND q605_dafb_test)

    # O6.5 gate: SCSI pseudo-DMA windows + DRQ-gated BERR timeout
    # (soft-skips without the disk image).
    add_executable(scsi_pdma_test tests/scsi_pdma_test.cpp)
    target_link_libraries(scsi_pdma_test PRIVATE pom68k_core)
    add_test(NAME scsi_pdma_test COMMAND scsi_pdma_test
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # O6.8 gate: the real LC II ROM boots a bootable image to the Finder
    # (whole-machine etalon; soft-skips without ROM + hdv/ image).
    add_executable(lcii_boot_etalon tests/lcii_boot_etalon.cpp)
    target_link_libraries(lcii_boot_etalon PRIVATE pom68k_core)
    add_test(NAME lcii_boot_etalon COMMAND lcii_boot_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # LC II System 7 Finder (SPConfig $22 clamp — no EvQ dismiss).
    add_executable(lcii_sys7_boot_etalon tests/lcii_sys7_boot_etalon.cpp)
    target_link_libraries(lcii_sys7_boot_etalon PRIVATE pom68k_core)
    add_test(NAME lcii_sys7_boot_etalon COMMAND lcii_sys7_boot_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # O6 slice 1 gate: external /BERR + RTE $A/$B on the 68030 (the LC II
    # ROM's address-map probe and the SCSI pseudo-DMA timeout rely on it).
    add_executable(berr030_test tests/berr030_test.cpp)
    target_link_libraries(berr030_test PRIVATE moira)
    add_test(NAME berr030_test COMMAND berr030_test)

    # O5 gate: hand-computed 68882 smoke cases (FMOVECR/FADD/DZ/OPERR/
    # FCMP/FMOVEM + detached F-line). The differential FPU corpus replays
    # through sst68030 (tests/data/sst68030_fpu, generated by a parallel
    # fuzz run) once available.
    add_executable(fpu_sanity tests/fpu_sanity.cpp)
    target_link_libraries(fpu_sanity PRIVATE moira)
    add_test(NAME fpu_sanity COMMAND fpu_sanity)

    # Mac II gates: declaration ROM, NuBus, Toby video HLE.
    add_executable(declrom_test tests/declrom_test.cpp)
    target_link_libraries(declrom_test PRIVATE pom68k_core)
    add_test(NAME declrom_test COMMAND declrom_test
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    add_executable(nubus_test tests/nubus_test.cpp)
    target_link_libraries(nubus_test PRIVATE pom68k_core)
    add_test(NAME nubus_test COMMAND nubus_test)

    add_executable(toby_test tests/toby_test.cpp)
    target_link_libraries(toby_test PRIVATE pom68k_core)
    add_test(NAME toby_test COMMAND toby_test)

    add_executable(macii_post_etalon tests/macii_post_etalon.cpp)
    target_link_libraries(macii_post_etalon PRIVATE pom68k_core)
    add_test(NAME macii_post_etalon COMMAND macii_post_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    add_executable(macii_boot_etalon tests/macii_boot_etalon.cpp)
    target_link_libraries(macii_boot_etalon PRIVATE pom68k_core)
    add_test(NAME macii_boot_etalon COMMAND macii_boot_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # Mac II System 7 Finder (SPConfig + EvQ dismiss of EtherTalk alerts).
    add_executable(macii_sys7_boot_etalon tests/macii_sys7_boot_etalon.cpp)
    target_link_libraries(macii_sys7_boot_etalon PRIVATE pom68k_core)
    add_test(NAME macii_sys7_boot_etalon COMMAND macii_sys7_boot_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # Dev harness: ROM×disk Finder matrix (not a CTest gate).
    add_executable(finder_boot_matrix EXCLUDE_FROM_ALL tests/finder_boot_matrix.cpp)
    target_link_libraries(finder_boot_matrix PRIVATE pom68k_core)
endif()

# Dev tool (not a gate): instruction-level Sony-driver trace.
if(POM68K_TESTS AND NOT EMSCRIPTEN)
    # All dev tools below are EXCLUDE_FROM_ALL — not built by `make`/`ctest`
    # (each relinks the core with LTO = slow); build on demand by name,
    # e.g. `make q605_trace`.
    add_executable(sony_trace EXCLUDE_FROM_ALL tests/sony_trace.cpp)
    target_link_libraries(sony_trace PRIVATE pom68k_core)

    # Dev tool (not a gate): real LC II ROM boot trace on the O6 machine.
    add_executable(lcii_trace EXCLUDE_FROM_ALL tests/lcii_trace.cpp)

    # Dev tool (not a gate): LC 475 / Quadra 605 ROM boot trace (Q5).
    add_executable(q605_trace EXCLUDE_FROM_ALL tests/q605_trace.cpp)
    target_link_libraries(q605_trace PRIVATE pom68k_core)
    target_link_libraries(lcii_trace PRIVATE pom68k_core)

    add_executable(macii_trace EXCLUDE_FROM_ALL tests/macii_trace.cpp)
    target_link_libraries(macii_trace PRIVATE pom68k_core)

    add_executable(macii_mm_probe EXCLUDE_FROM_ALL tests/macii_mm_probe.cpp)
    target_link_libraries(macii_mm_probe PRIVATE pom68k_core)

    add_executable(macii_hmmu_trace EXCLUDE_FROM_ALL tests/macii_hmmu_trace.cpp)
    target_link_libraries(macii_hmmu_trace PRIVATE pom68k_core)

    # Dev tool (not a gate): offline Mac ROM introspection (header,
    # resources, trap table, UniversalInfo) — Basilisk II-derived parsers,
    # see docs/BASILISK_ROM_NOTES.md. Standalone, no emulator core.
    add_executable(rominfo EXCLUDE_FROM_ALL tools/rominfo.cpp)
endif()
