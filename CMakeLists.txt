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

# COFF caps an object file at 65 279 sections; Moira.cpp's dispatch-table
# instantiation blows past it (C1128, first Windows release build). /bigobj
# lifts the cap to 2^32 and costs nothing elsewhere — tree-wide, because
# every big translation unit here is one template pass away from the same
# wall.
if(MSVC)
    add_compile_options(/bigobj)
endif()

# ─── Host-tuned optimization (perf pass 2026-07-17) ──────────────────────
# The LC II at cache-boost 4 needs every cycle the host can give; native
# tuning + LTO buy ~15-25% on the Moira hot loop. ON by default (POM68K is
# built from source on the machine that runs it); disable for a portable
# binary with -DPOM68K_NATIVE=OFF.
# EMSCRIPTEN is excluded, not merely untested: `-march=` does not exist for
# wasm32 ("unsupported option '-march=' for target 'wasm32-unknown-emscripten'")
# and the option is ON by default, so the DEFAULT configuration could not build
# the target this file already has an EMSCRIPTEN branch for. There is no host
# CPU to tune for when the host is a browser.
#
# THREE knobs, deliberately separate (they were one until 2026-08-08, and the
# coupling cost every released binary its LTO — see below):
#
#   POM68K_NATIVE  raise the ISA floor AND the cost model to this host.
#                  Non-distributable by construction.
#   POM68K_TUNE    schedule for a named core WITHOUT raising the ISA floor
#                  (`-mtune=`). Distributable: the binary still runs on every
#                  CPU of the family, it is merely ordered for that one. This
#                  is how the release aarch64 AppImage targets the Pi 4/400's
#                  Cortex-A72 while staying loadable on a Pi 3.
#   POM68K_LTO     link-time optimization. Independent of both: a portable
#                  binary has every reason to want it.
option(POM68K_NATIVE "Optimize for the build host CPU (native ISA + tuning)" ON)
set(POM68K_TUNE "" CACHE STRING
    "Tune for a named core without raising the ISA floor (e.g. cortex-a72)")
option(POM68K_LTO "Link-time optimization" ${POM68K_NATIVE})

if(NOT MSVC AND NOT EMSCRIPTEN)
    include(CheckCXXCompilerFlag)
    if(POM68K_NATIVE)
        # aarch64 wants `-mcpu=native`, not `-march=native`: on this target
        # `-march=` sets the architecture features and leaves the cost model
        # GENERIC, and the cost model is the whole point for an interpreter
        # dispatch loop. x86-64 has no `-mcpu=`, so it keeps `-march=native`
        # (which does carry tuning there).
        # Both are probed rather than assumed: on some 64-bit Pi kernels the
        # MIDR that GCC reads is incomplete and the flag is rejected outright.
        # Falling back beats failing twenty minutes later on a random .cpp.
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
            set(pom68k_native_flag "-mcpu=native")
        else()
            set(pom68k_native_flag "-march=native")
        endif()
        check_cxx_compiler_flag("${pom68k_native_flag}" pom68k_native_ok)
        if(pom68k_native_ok)
            add_compile_options(${pom68k_native_flag})
        else()
            message(STATUS "POM68K: ${pom68k_native_flag} rejected by "
                           "${CMAKE_CXX_COMPILER_ID} — building generic")
        endif()
    endif()
    if(POM68K_TUNE)
        check_cxx_compiler_flag("-mtune=${POM68K_TUNE}" pom68k_tune_ok)
        if(pom68k_tune_ok)
            add_compile_options(-mtune=${POM68K_TUNE})
            message(STATUS "POM68K: tuned for ${POM68K_TUNE} (ISA floor unchanged)")
        else()
            message(WARNING "POM68K: -mtune=${POM68K_TUNE} rejected — ignored")
        endif()
    endif()
    if(POM68K_LTO)
        include(CheckIPOSupported)
        check_ipo_supported(RESULT pom68k_ipo OUTPUT pom68k_ipo_why)
        if(pom68k_ipo)
            set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
        else()
            message(STATUS "POM68K: LTO requested but unsupported — ${pom68k_ipo_why}")
        endif()
    endif()
    # ─── Link speed ──────────────────────────────────────────────────────
    # This tree links ~130 executables against one static core, every one of
    # them under tree-wide LTO, so a core change costs ~130 full relinks and
    # the linker is a first-order term in the working loop. mold and lld are
    # drop-in and several times faster than BFD ld at exactly this shape of
    # work. Neither is a dependency: absent, the build is unchanged.
    # Not tied to POM68K_LTO — a non-LTO build links the same 130 binaries.
    option(POM68K_FAST_LINK "Use mold or lld when one is installed" ON)
    if(POM68K_FAST_LINK)
        foreach(pom68k_ld mold lld)
            find_program(pom68k_ld_${pom68k_ld} ld.${pom68k_ld})
            if(pom68k_ld_${pom68k_ld})
                # Probed, not assumed: the driver has to know the flag, and
                # a GCC older than 12 does not know -fuse-ld=mold at all.
                set(CMAKE_REQUIRED_LINK_OPTIONS "-fuse-ld=${pom68k_ld}")
                check_cxx_compiler_flag("" pom68k_ld_${pom68k_ld}_ok)
                unset(CMAKE_REQUIRED_LINK_OPTIONS)
                if(pom68k_ld_${pom68k_ld}_ok)
                    add_link_options(-fuse-ld=${pom68k_ld})
                    message(STATUS "POM68K: linking with ${pom68k_ld}")
                    break()
                endif()
            endif()
        endforeach()
    endif()
endif()

# ─── Profile-guided optimization (perf pass 2026-07-28) ──────────────────
# An interpreter is dispatch and branches, which is exactly what PGO is
# for — measured on the Quadra 605 boot: interpreter −33 %, JIT −18 %, with
# bit-identical emulation (the state fingerprints match the plain build).
# Three steps, in ONE build directory so the profile paths line up. The
# helper handles both GCC's gcda files and Clang/AppleClang's profraw ->
# profdata merge:
#
#   tools/pgo_train.sh build-pgo
#
# GCC writes *.gcda next to the objects. Clang writes *.profraw at runtime;
# the helper merges those with llvm-profdata and passes the resulting file
# back through POM68K_PGO_PROFILE. Keep the compiler-specific flags separate:
# AppleClang silently ignores GCC's correction flag and does not understand
# its missing-profile warning switch.
set(POM68K_PGO "off" CACHE STRING "Profile-guided optimization: off | generate | use")
set_property(CACHE POM68K_PGO PROPERTY STRINGS off generate use)
set(POM68K_PGO_PROFILE "${CMAKE_BINARY_DIR}/pom68k.profdata" CACHE FILEPATH
    "Merged LLVM profile used when POM68K_PGO=use with Clang/AppleClang")
if(NOT POM68K_PGO STREQUAL "off" AND NOT MSVC)
    if(CMAKE_CXX_COMPILER_ID MATCHES "^(AppleClang|Clang)$")
        if(POM68K_PGO STREQUAL "generate")
            add_compile_options(-fprofile-instr-generate)
            add_link_options(-fprofile-instr-generate)
        elseif(POM68K_PGO STREQUAL "use")
            if(NOT EXISTS "${POM68K_PGO_PROFILE}")
                message(FATAL_ERROR
                    "LLVM PGO profile not found: ${POM68K_PGO_PROFILE}. "
                    "Run tools/pgo_train.sh <build-dir> first.")
            endif()
            # Most machine/device translation units are intentionally cold in
            # a four-machine training set. Clang diagnoses each one by
            # default; that is expected coverage information, not a broken
            # profile (llvm-profdata already rejects malformed input).
            add_compile_options(-fprofile-instr-use=${POM68K_PGO_PROFILE}
                                -Wno-profile-instr-unprofiled
                                -Wno-profile-instr-out-of-date)
            add_link_options(-fprofile-instr-use=${POM68K_PGO_PROFILE})
        else()
            message(FATAL_ERROR "POM68K_PGO must be off, generate or use")
        endif()
    else()
        if(POM68K_PGO STREQUAL "generate")
            add_compile_options(-fprofile-generate)
            add_link_options(-fprofile-generate)
        elseif(POM68K_PGO STREQUAL "use")
            add_compile_options(-fprofile-use -fprofile-correction -Wno-missing-profile)
            add_link_options(-fprofile-use)
            # Without this, GCC optimizes every function the training set never
            # entered for SIZE. A four-machine training set leaves ~30 machine
            # and device translation units cold, so the untrained profiles would
            # come out of a PGO build SLOWER than out of a plain -O3 one. GCC 10+.
            include(CheckCXXCompilerFlag)
            check_cxx_compiler_flag(-fprofile-partial-training pom68k_partial_ok)
            if(pom68k_partial_ok)
                add_compile_options(-fprofile-partial-training)
            endif()
        else()
            message(FATAL_ERROR "POM68K_PGO must be off, generate or use")
        endif()
    endif()
    message(STATUS "POM68K PGO: ${POM68K_PGO} (${CMAKE_CXX_COMPILER_ID})")
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

# ─── JIT (second execution engine — see src/jit/POM68K_JIT.md) ───────────
# POM68K is multiplatform, so the JIT is multi-target by construction: the
# `threaded` backend generates no host code and is ALWAYS built, which is
# what makes the engine available on every host the emulator compiles for
# (Emscripten included). Native code generators are added on top, per host
# architecture, and never replace that floor.
#
# POM68K_JIT_BACKENDS: `auto` follows CMAKE_SYSTEM_PROCESSOR; set it to
# `threaded` to build the portable-only configuration on purpose — that is
# the control build proving a host without a code generator still works.
set(POM68K_JIT_BACKENDS "auto" CACHE STRING
    "JIT backends to compile: auto | threaded")
set(POM68K_JIT_SOURCES
    src/jit/JitEngine.cpp        # layer 1 — blocks, code window, invalidation
    src/jit/JitBackend.cpp       # layer 2 — backend registry and selection
    src/jit/JitCodeBuffer.cpp    # portable W^X memory for code generators
    src/jit/backends/JitBackendThreaded.cpp   # the portable floor
)
set(POM68K_JIT_DEFS "")
if(POM68K_JIT_BACKENDS STREQUAL "auto" AND NOT EMSCRIPTEN)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
        list(APPEND POM68K_JIT_SOURCES src/jit/backends/JitBackendX64.cpp)
        list(APPEND POM68K_JIT_DEFS POM68K_JIT_BACKEND_X64)
        set(POM68K_JIT_HOST "x86-64 code generator")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
        list(APPEND POM68K_JIT_SOURCES src/jit/backends/JitBackendA64.cpp)
        list(APPEND POM68K_JIT_DEFS POM68K_JIT_BACKEND_A64)
        set(POM68K_JIT_HOST "aarch64 code generator")
    else()
        set(POM68K_JIT_HOST "${CMAKE_SYSTEM_PROCESSOR} (portable only)")
    endif()
else()
    set(POM68K_JIT_HOST "portable only (forced)")
endif()
message(STATUS "POM68K JIT: threaded backend + ${POM68K_JIT_HOST}")

# ─── Core library (headless — shared by GUI, tests, future oracles) ──────
add_library(pom68k_core STATIC
    ${POM68K_JIT_SOURCES}
    src/SaveState.cpp     # save states — archive core (visitor, chunks, RLE)
    src/SaveStateMachines.cpp  # save states — machine assembly + instantiations
    src/Cpu68k.cpp        # M1 — Moira wrapper + contention
    src/MacMemory.cpp     # M2 — Mac Plus memory map + overlay
    src/Via6522.cpp       # M2/M4 — VIA (ports, timers)
    src/Rtc.cpp           # M4 — clock/PRAM serial chip
    src/Iwm.cpp           # M5 — floppy controller
    src/SonyDrive.cpp     # M5 — 800K GCR drive
    src/Scc8530.cpp       # M5.5 — SCC (mouse DCD interrupts)
    src/M68hc05.cpp       # step 10 — 68HC05E1 core (Egret/Cuda firmware LLE)
    src/R65c02.cpp        # IOP M1 — R65C02 core (Apple PIC, docs/IOP_BRINGUP.md)
    src/ApplePic.cpp      # IOP M2 — Apple 343S1021 PIC (host window/DMA/timer)
    src/IIfxMemory.cpp    # IOP M3 — Mac IIfx map (OSS + 2 IOPs + SCSIDMA)
    src/IIfxCpu.cpp       # IOP M3 — Moira 68030 wrapper (IIfx, 40 MHz)
    src/CudaLle.cpp       # step 10 — Cuda firmware ↔ VIA/ADB glue (opt-in)
    src/LtoUdp.cpp        # LLAP — LocalTalk-over-UDP multicast cable
    src/AtalkStack.cpp    # AppleTalk in-process node (DDP/RTMP/ZIP/NBP/ATP)
    src/AfpServer.cpp     # in-process AppleShare (ASP + AFP 2.1 subset)
    src/PapServer.cpp     # in-process LaserWriter (PAP → CUPS/fichier)
    src/MacIpGateway.cpp  # in-process MacIP gateway + user-mode NAT
    src/MacInput.cpp      # M5.5 — M0110 keyboard + quadrature mouse
    src/ScsiDisk.cpp      # M7 — SCSI direct-access target
    src/DaynaPort.cpp     # DaynaPort SCSI/Link — Ethernet as a SCSI target
    src/EtherLink.cpp     # Ethernet framing + ARP: DaynaPort ↔ MacIpGateway
    src/Ncr5380.cpp       # M7 — SCSI controller (phase engine)
    src/Ncr53c96.cpp      # Q6 — NCR 53C96 SCSI controller (Quadra 605)
    src/PseudoVia.cpp     # O6 — LC II pseudo-VIA (V8 interrupt core)
    src/V8Memory.cpp      # O6 — LC II memory map + V8 gate array
    src/Cpu030.cpp        # O6 — Moira 68030 wrapper (LC II)
    src/Egret.cpp         # O6 — Egret ADB/RTC/PRAM MCU (HLE)
    src/AdbBus.cpp        # O6 — ADB bus + HLE keyboard/mouse
    src/Asc.cpp           # O6 — ASC-V8 sound (FIFO A, 22 257 Hz)
    src/Swim1.cpp         # O6.7 — LC II SWIM1 (IWM personality + ISM/MFM)
    src/Swim2.cpp         # Q8 — PrimeTime/IOSB SWIM2 register + FIFO core
    src/SonoraMemory.cpp  # Phase C — LC III map + Sonora gate array
    src/SonoraCpu.cpp     # Phase C — Moira 68030 wrapper (LC III, 25 MHz)
    src/VaspMemory.cpp    # Phase C — Mac IIvx/IIvi map + VASP gate array
    src/VaspCpu.cpp       # Phase C — Moira 68030 wrapper (IIvx 31.3 MHz)
    src/MscMemory.cpp     # Platform #11 — PowerBook Duo MSC (docs/DUO_BRINGUP.md)
    src/MscCpu.cpp        # Platform #11 — Moira 68030 wrapper (Duo 230 33 MHz)
    src/M68hc05Pge.cpp    # Platform #11 — PG&E power manager 68HC05 core
    src/PgePmu.cpp        # Platform #11 — PG&E board integration (SPI on VIA1)
    src/RbvMemory.cpp     # Phase C — Mac IIsi map + RBV (RAM-based video)
    src/RbvCpu.cpp        # Phase C — Moira 68030 wrapper (IIsi, 20 MHz)
    src/CentrisMemory.cpp # Phase C — Centris 610/650 map (djMEMC + IOSB)
    src/CentrisCpu.cpp    # Phase C — Moira 68LC040 wrapper (Centris)
    src/Q630Memory.cpp    # Quadra 630 / LC 580 — F108 + PrimeTime II + Valkyrie
    src/Q630Cpu.cpp       # Quadra 630 — Moira 68040 wrapper (33 MHz)
    src/Valkyrie.cpp      # Quadra 630 video cell (fixed-mode framebuffer)
    src/Q700Memory.cpp    # Quadra 700 — discrete "Spike" map (DAFB TurboSCSI)
    src/Q700Cpu.cpp       # Quadra 700 — Moira 68040 wrapper (25 MHz)
    src/Q605Memory.cpp    # Q5 — LC 475/Quadra 605 map (MEMCjr + PrimeTime)
    src/Dafb.cpp          # Q8 — DAFB II video cell (Swatch CRTC/Gazelle/RAMDAC)
    src/Cpu040.cpp        # Q5 — Moira 68LC040 wrapper
    src/DeclRom.cpp       # Mac II — NuBus declaration ROM builder
    src/NuBus.cpp         # Mac II — NuBus slot decode + IRQ
    src/TobyVideo.cpp     # Mac II — Toby Frame Buffer HLE
    src/MacIIMemory.cpp   # Mac II — 32-bit GLUE memory map
    src/Cpu020.cpp        # Mac II — Moira 68020 wrapper
    src/AdbVia.cpp        # Mac II — ADB HLE on VIA1
    src/Pic1654s.cpp      # Mac II — PIC1654S ADB transceiver core (LLE)
    src/AdbLine.cpp       # Mac II — bit-serial ADB devices (LLE)
)
target_include_directories(pom68k_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(pom68k_core PUBLIC moira)
# Which JIT backends this build actually contains (the registry in
# src/jit/JitBackend.cpp keys its candidate list off these).
target_compile_definitions(pom68k_core PUBLIC ${POM68K_JIT_DEFS})

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
    add_executable(${PROJECT_NAME} src/main.cpp src/miniaudio_impl.cpp
        src/FloppySound.cpp src/DiskBays.cpp src/PeripheralWindow.cpp
        src/DockLayout.cpp ${IMGUI_SOURCES})
    target_include_directories(${PROJECT_NAME} PRIVATE ${IMGUI_DIR} ${IMGUI_DIR}/backends)
    target_link_libraries(${PROJECT_NAME} PRIVATE pom68k_core)
    # Version stamp for `POM68K --version` (release smoke gates). Single
    # source of truth: the repo-root VERSION file; packagers override with
    # -DPOM68K_VERSION=<tag>. Target-scoped so bumping it recompiles one TU.
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" POM68K_VERSION_DEFAULT LIMIT_COUNT 1)
    set(POM68K_VERSION "${POM68K_VERSION_DEFAULT}" CACHE STRING "Package version string")
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        POM68K_VERSION_STRING="${POM68K_VERSION}")
    # OpenGL remains the intentional portable GUI backend. Apple deprecated
    # the API at the SDK level in 10.14, which otherwise emits the same warning
    # for every GL call and hides actionable AppleClang diagnostics.
    if(APPLE)
        target_compile_definitions(${PROJECT_NAME} PRIVATE GL_SILENCE_DEPRECATION)
    endif()
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
option(POM68K_PRODUCT_LLE_GATES
       "Enable no-skip AArch64 LLE product gates (requires private ROM/firmware assets)"
       OFF)
if(POM68K_TESTS AND NOT EMSCRIPTEN)
    enable_testing()

    # Gate-side headers (tests/AssetFingerprint.h, tests/atalk_test_util.h).
    # Directory-scoped so every target below sees them without 130 edits;
    # the GUI target above is deliberately outside it.
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/tests)

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

    # LLE floppy-write gate: IWM write engine (handshake/underrun) + GCR
    # write-back — a harvested data field replayed through the write path
    # must commit as the exact inverse of the read path.
    add_executable(iwm_write_test tests/iwm_write_test.cpp)
    target_link_libraries(iwm_write_test PRIVATE pom68k_core)
    add_test(NAME iwm_write_test COMMAND iwm_write_test)

    add_executable(iwm_read_test tests/iwm_read_test.cpp)
    target_link_libraries(iwm_read_test PRIVATE pom68k_core)
    add_test(NAME iwm_read_test COMMAND iwm_read_test)

    # O6.7 gate: SWIM1 IWM/ISM personalities — 1-0-1-1 switch magic, param
    # RAM, and ISM MFM read+write of 1.44 MB media through the cell engine.
    add_executable(swim1_test tests/swim1_test.cpp)
    target_link_libraries(swim1_test PRIVATE pom68k_core)
    add_test(NAME swim1_test COMMAND swim1_test)

    # Save states — the archive core every device chunk is built on:
    # visit() round-trip, the pinned little-endian/LEB128 wire format, the
    # zero-run codec (exactness AND the compression that keeps a 10 MB
    # snapshot small), chunk walking, and clean refusal of truncated or
    # corrupt input. No ROM or disk image needed.
    add_executable(savestate_test tests/savestate_test.cpp)
    target_link_libraries(savestate_test PRIVATE pom68k_core)
    add_test(NAME savestate_test COMMAND savestate_test)

    # Save states end to end on a live LC II (synthetic ROM, no assets): the
    # re-save byte-identity AND the determinism property — same start, same
    # cycle count, same snapshot — which is what detects state a visit()
    # forgot. Also pins that a refused snapshot leaves the machine untouched.
    add_executable(savestate_v8_test tests/savestate_v8_test.cpp)
    target_link_libraries(savestate_v8_test PRIVATE pom68k_core)
    add_test(NAME savestate_v8_test COMMAND savestate_v8_test)

    # Save-state fan-out: the three 030 families (Sonora / VASP / RBV,
    # incl. the IIci's AdbVia + Pic1654s + Rtc chunks) — re-save
    # byte-identity + determinism across a restore, synthetic ROM.
    add_executable(savestate_030_test tests/savestate_030_test.cpp)
    target_link_libraries(savestate_030_test PRIVATE pom68k_core)
    add_test(NAME savestate_030_test COMMAND savestate_030_test)

    # Save-state fan-out: the four 040 families (Q605 / Centris / Q700 /
    # Q630 — Dafb, Valkyrie, AscIosb, Ncr53c96 chunks), same properties.
    add_executable(savestate_040_test tests/savestate_040_test.cpp)
    target_link_libraries(savestate_040_test PRIVATE pom68k_core)
    add_test(NAME savestate_040_test COMMAND savestate_040_test)

    # Save-state fan-out: Mac II (NuBus/Toby/Cpu020) + compact 68000
    # (Plus M0110 engine, SE ADB, Cpu68k), same properties.
    add_executable(savestate_68k_test tests/savestate_68k_test.cpp)
    target_link_libraries(savestate_68k_test PRIVATE pom68k_core)
    add_test(NAME savestate_68k_test COMMAND savestate_68k_test
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # Keyboard through the real Cuda firmware (SRQ path — the 2026-07-23
    # freeze repro). Soft-skips without assets.
    add_executable(q605_cudalle_key_etalon tests/q605_cudalle_key_etalon.cpp)
    target_link_libraries(q605_cudalle_key_etalon PRIVATE pom68k_core)
    add_test(NAME q605_cudalle_key_etalon COMMAND q605_cudalle_key_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

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

    # The target's SCSI-2 surface: mode pages, the Apple page $30 signature
    # and the formatter command set. Builds its own image — no asset, no skip.
    add_executable(scsi_target_test tests/scsi_target_test.cpp)
    target_link_libraries(scsi_target_test PRIVATE pom68k_core)
    add_test(NAME scsi_target_test COMMAND scsi_target_test)

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

    # Gate: tools/dir2hfs.py fixture bake (MacBinary decode, data-only bare
    # volume for the flat-HFS façade, machfs round-trip). Soft-skips if machfs
    # is missing — prefers the repo venv (.venv-tools), else system python3.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.venv-tools/bin/python")
        set(POM68K_TOOLS_PYTHON "${CMAKE_CURRENT_SOURCE_DIR}/.venv-tools/bin/python")
    else()
        set(POM68K_TOOLS_PYTHON "python3")
    endif()
    add_test(NAME dir2hfs_selftest
             COMMAND ${POM68K_TOOLS_PYTHON} tools/dir2hfs.py --selftest
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

    # LLAP milestone gate: two SCCs on a virtual LocalTalk cable — SDLC Tx
    # frame capture, paced Rx FIFO, address search, EOF/FCS, carrier sense.
    add_executable(llap_loop_test tests/llap_loop_test.cpp)
    target_link_libraries(llap_loop_test PRIVATE pom68k_core)
    add_test(NAME llap_loop_test COMMAND llap_loop_test)

    # LToUDP multicast cable (Mini vMac / TashRouter wire format). Soft-skips
    # where multicast is unavailable.
    add_executable(ltoudp_test tests/ltoudp_test.cpp)
    target_link_libraries(ltoudp_test PRIVATE pom68k_core)
    add_test(NAME ltoudp_test COMMAND ltoudp_test)

    # In-process AppleTalk stack gates (AtalkStack + AFP/PAP/MacIP services)
    add_executable(atalk_stack_test tests/atalk_stack_test.cpp)
    target_link_libraries(atalk_stack_test PRIVATE pom68k_core)
    add_test(NAME atalk_stack_test COMMAND atalk_stack_test)

    add_executable(afp_server_test tests/afp_server_test.cpp)
    target_link_libraries(afp_server_test PRIVATE pom68k_core)
    add_test(NAME afp_server_test COMMAND afp_server_test)

    add_executable(pap_server_test tests/pap_server_test.cpp)
    target_link_libraries(pap_server_test PRIVATE pom68k_core)
    add_test(NAME pap_server_test COMMAND pap_server_test)

    add_executable(macip_gw_test tests/macip_gw_test.cpp)
    target_link_libraries(macip_gw_test PRIVATE pom68k_core)
    add_test(NAME macip_gw_test COMMAND macip_gw_test)

    # DaynaPort SCSI/Link (Ethernet as a SCSI target) + the EtherLink bridge
    # onto the same NAT the MacIP gateway uses.
    add_executable(daynaport_test tests/daynaport_test.cpp)
    target_include_directories(daynaport_test PRIVATE tests)
    target_link_libraries(daynaport_test PRIVATE pom68k_core)
    add_test(NAME daynaport_test COMMAND daynaport_test)

    # Gate for the gate preamble: SHA-256 against FIPS 180-4 (padding
    # boundaries included) and the drVolAtrb probe against both image
    # layouts, synthesised in the test. Asset-free — it links nothing but
    # the header, so it stays in `unit`.
    add_executable(asset_fingerprint_test tests/asset_fingerprint_test.cpp)
    add_test(NAME asset_fingerprint_test COMMAND asset_fingerprint_test)

    # Reference media under ref/ are never opened for write-back: the first
    # GUI session gets a persistent sibling under work/. No Apple asset needed.
    add_executable(fixture_store_test tests/fixture_store_test.cpp)
    target_include_directories(fixture_store_test PRIVATE
                               ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME fixture_store_test COMMAND fixture_store_test)

    # assets.lock is useful in a clean clone too: validate its schema, and any
    # present user-provided file. Private runners add --strict to require all.
    add_test(NAME asset_lock_test
             COMMAND ${POM68K_TOOLS_PYTHON} tools/verify_assets.py
                     --manifest assets.lock --root .
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # The GUI ↔ machine-thread contract (src/MachineHost.h): queue ordering,
    # the framebuffer double buffer, the engine-swap round trip, the pacing
    # branches and the thread teardown. `main.cpp` is the one translation unit
    # outside pom68k_core, so this contract had six copies and no gate until
    # it was lifted into a header (2026-08-09). No ROM, no image.
    add_executable(machinehost_test tests/machinehost_test.cpp)
    target_link_libraries(machinehost_test PRIVATE pom68k_core)
    add_test(NAME machinehost_test COMMAND machinehost_test)

    # The docs' citable invariants, checked against the code: compiled profile
    # catalogue == what CLAUDE.md says, every gate CLAUDE.md
    # names is registered, every registered gate carries a label, and the gate
    # totals it quotes. Reads pom68k_gates.tsv, which the tier block below
    # writes at configure time. Links nothing.
    add_executable(docs_test tests/docs_test.cpp)
    target_include_directories(docs_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(docs_test PRIVATE
        POM68K_GATE_ROSTER="${CMAKE_CURRENT_BINARY_DIR}/pom68k_gates.tsv"
        POM68K_GATE_ROSTER_ABSENT="${CMAKE_CURRENT_BINARY_DIR}/pom68k_gates_absent.tsv"
        POM68K_GATE_MANIFEST="${CMAKE_CURRENT_BINARY_DIR}/pom68k_gate_manifest.tsv"
        POM68K_PERF_BUDGET_FILE="${CMAKE_CURRENT_SOURCE_DIR}/performance_budgets.tsv")
    add_test(NAME docs_test COMMAND docs_test
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # The env-knob surface against DEV.md § 5, both directions. Found twelve
    # undocumented knobs and one documented ghost the first time it ran.
    # Links nothing; walks the source tree.
    add_executable(config_test tests/config_test.cpp)
    add_test(NAME config_test COMMAND config_test)

    # tests/FolderProbe.h — "did the guest create a folder?", the observable
    # the three beyond-boot persist gates judge PASS/FAIL on. Gated here
    # rather than through three six-minute machine runs, and because the
    # thing it replaced was wrong twice over (case-sensitive, and reporting
    # the most FREQUENT candidate instead of the one that moves).
    add_executable(folderprobe_test tests/folderprobe_test.cpp)
    add_test(NAME folderprobe_test COMMAND folderprobe_test)

    # The peripheral LLE/HLE registry behind the "Périphériques" window. The
    # window itself is compile-verified only (no gate opens an ImGui window),
    # so everything that could be wrong lives below it and is gated here.
    add_executable(peripheral_lle_test tests/peripheral_lle_test.cpp)
    target_link_libraries(peripheral_lle_test PRIVATE pom68k_core)
    add_test(NAME peripheral_lle_test COMMAND peripheral_lle_test)

    # LLAP two-System etalon: two Mac II machines boot Sys 6 with AppleTalk
    # seeded active on a shared virtual cable — real LAP address acquisition
    # end-to-end (soft-skips without ROM+disk).
    add_executable(llap_two_system_etalon tests/llap_two_system_etalon.cpp)
    target_link_libraries(llap_two_system_etalon PRIVATE pom68k_core)
    add_test(NAME llap_two_system_etalon COMMAND llap_two_system_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

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

    # The Valkyrie's pixel clock, programmed by the Cuda over I2C (slave
    # $28) — the last "bus not modelled" entry of LLE_VS_HLE.md § 1.1,
    # closed 2026-08-02. Asserts through the frame cadence, not the setter.
    # The floppy data-separator PLL (src/FluxPll.h, MAME fdc_pll_t) — the
    # first step of the flux layer, LLE_VS_HLE.md § 1.3. Header-only, so it
    # links against nothing but itself.
    add_executable(flux_pll_test tests/flux_pll_test.cpp)
    target_link_libraries(flux_pll_test PRIVATE pom68k_core)
    add_test(NAME flux_pll_test COMMAND flux_pll_test)

    add_executable(valkyrie_i2c_test tests/valkyrie_i2c_test.cpp)
    target_link_libraries(valkyrie_i2c_test PRIVATE pom68k_core)
    add_test(NAME valkyrie_i2c_test COMMAND valkyrie_i2c_test)

    # MAME-parity gates (2026-08-05 audit, medium wave): free-running Valkyrie
    # frame clock, SCC interrupt trio, 53C96 command queue, MSC SOUND_BUSY +
    # PMU-driven CPU reset, EASC flavour, DAFB TurboSCSI hold-off.
    add_executable(valkyrie_test tests/valkyrie_test.cpp)
    target_link_libraries(valkyrie_test PRIVATE pom68k_core)
    add_test(NAME valkyrie_test COMMAND valkyrie_test)

    add_executable(scc_int_test tests/scc_int_test.cpp)
    target_link_libraries(scc_int_test PRIVATE pom68k_core)
    add_test(NAME scc_int_test COMMAND scc_int_test)

    # MAME-parity gate (low wave): T2 PB6 pulse counting, SR recirculation,
    # T1-driven PB7 square wave.
    add_executable(via6522_parity_test tests/via6522_parity_test.cpp)
    target_link_libraries(via6522_parity_test PRIVATE pom68k_core)
    add_test(NAME via6522_parity_test COMMAND via6522_parity_test)

    # The RTC battery file on the four platforms that gained it 2026-08-06
    # (compacts, Mac II family, IIfx; the Duo's is in msc_parity_test).
    add_executable(rtc_pram_test tests/rtc_pram_test.cpp)
    target_link_libraries(rtc_pram_test PRIVATE pom68k_core)
    add_test(NAME rtc_pram_test COMMAND rtc_pram_test)

    add_executable(ncr53c96_queue_test tests/ncr53c96_queue_test.cpp)
    target_link_libraries(ncr53c96_queue_test PRIVATE pom68k_core)
    add_test(NAME ncr53c96_queue_test COMMAND ncr53c96_queue_test
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    add_executable(msc_parity_test tests/msc_parity_test.cpp)
    target_link_libraries(msc_parity_test PRIVATE pom68k_core)
    add_test(NAME msc_parity_test COMMAND msc_parity_test)

    add_executable(asc_easc_test tests/asc_easc_test.cpp)
    target_link_libraries(asc_easc_test PRIVATE pom68k_core)
    add_test(NAME asc_easc_test COMMAND asc_easc_test)

    add_executable(q700_turboscsi_test tests/q700_turboscsi_test.cpp)
    target_link_libraries(q700_turboscsi_test PRIVATE pom68k_core)
    add_test(NAME q700_turboscsi_test COMMAND q700_turboscsi_test
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

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

    # Test-depth gates: the firmware-LLE ADB wire delivers mouse+keyboard
    # on the Phase C 030 families (one binary, one machine per test —
    # Sonora Egret, Sonora AIO Cuda, VASP, RBV). Soft-skip without assets.
    # `iisi` asserts on SCREEN PIXELS (cursor motion) rather than the
    # low-memory globals: on a RAM-based-video machine physical low RAM is
    # the framebuffer, so peeking the globals reads the desktop pattern.
    add_executable(family_input_etalon tests/family_input_etalon.cpp)
    target_link_libraries(family_input_etalon PRIVATE pom68k_core)
    # q900 = the Eclipse tower's Egret 341S0851 firmware LLE (2026-08-14):
    # the one board where ADB could hang off either the Egret or the SWIM
    # IOP's bit-banged wire, so "which transport does the ROM drive" needed a
    # gate of its own. It boots Mac OS 8.1 and runs 16000 frames, hence the
    # longer timeout its 030 siblings do not need.
    foreach(fam lc3 lc520 iivx iisi q900)
        add_test(NAME ${fam}_input_etalon COMMAND family_input_etalon ${fam}
                 WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    endforeach()
    set_tests_properties(q900_input_etalon PROPERTIES TIMEOUT 2400)

    # LLE step 7 gate (LLE_VS_HLE §1.10): Open Transport binds .MPP on a
    # virgin (never-driven) LocalTalk line — the guest's own lapENQ probes
    # on the wire are the proof. Same OS 8.1 assets as the boot etalon.
    add_executable(q605_ot_bind_etalon tests/q605_ot_bind_etalon.cpp)
    target_link_libraries(q605_ot_bind_etalon PRIVATE pom68k_core)
    add_test(NAME q605_ot_bind_etalon COMMAND q605_ot_bind_etalon
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

    # Phase C gate: the LC 475 identity ($A55A2221, 68LC040) on the Q605
    # machine boots Mac OS 8.1 to the 640x480x8 Finder (soft-skip).
    add_executable(lc475_boot_etalon tests/lc475_boot_etalon.cpp)
    target_link_libraries(lc475_boot_etalon PRIVATE pom68k_core)
    add_test(NAME lc475_boot_etalon COMMAND lc475_boot_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # Phase C gate: the LC 575 / Performa 575 identity ($A55A222E, 68LC040 @
    # 33 MHz) on the Q605 machine boots Mac OS 8.1 to the 640x480x8 Finder.
    add_executable(lc575_boot_etalon tests/lc575_boot_etalon.cpp)
    target_link_libraries(lc575_boot_etalon PRIVATE pom68k_core)
    add_test(NAME lc575_boot_etalon COMMAND lc575_boot_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # Q8 gate: PrimeTime SWIM2 register/FIFO core and 16-bit bus mapping.
    add_executable(swim2_test tests/swim2_test.cpp)
    target_link_libraries(swim2_test PRIVATE pom68k_core)
    add_test(NAME swim2_test COMMAND swim2_test)

    # Q8 gate: SWIM2 + SonyDrive MFM 1.44MB / GCR 800K media + sector R/W.
    add_executable(swim2_media_test tests/swim2_media_test.cpp)
    target_link_libraries(swim2_media_test PRIVATE pom68k_core)
    add_test(NAME swim2_media_test COMMAND swim2_media_test)

    # Mechanical drive sounds (FloppySound, MAME sample playback). The
    # miniaudio TU lives outside pom68k_core so headless tests stay slim;
    # this gate links it explicitly. Soft-skips without the sample WAVs.
    add_executable(floppy_sound_test tests/floppy_sound_test.cpp
        src/FloppySound.cpp src/miniaudio_impl.cpp)
    target_link_libraries(floppy_sound_test PRIVATE pom68k_core
        ${CMAKE_DL_LIBS} pthread m)
    add_test(NAME floppy_sound_test COMMAND floppy_sound_test
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/floppy_samples)

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

    # LLE gate: PIC1654S core (ADB transceiver microcontroller) instruction set.
    add_executable(pic1654s_test tests/pic1654s_test.cpp)
    target_link_libraries(pic1654s_test PRIVATE pom68k_core)
    add_test(NAME pic1654s_test COMMAND pic1654s_test)

    # LLE gate: bit-serial ADB device model (keyboard + mouse) decode/response.
    add_executable(adbline_test tests/adbline_test.cpp)
    target_link_libraries(adbline_test PRIVATE pom68k_core)
    add_test(NAME adbline_test COMMAND adbline_test)

    # LLE_VS_HLE §1.1 gate, step 1: the raster beam — scan position + the
    # row schedule the decoders render against (every visible row exactly
    # once per frame, in order, tail flushed on the wrap).
    add_executable(video_beam_test tests/video_beam_test.cpp)
    target_link_libraries(video_beam_test PRIVATE pom68k_core)
    add_test(NAME video_beam_test COMMAND video_beam_test)

    # O6.4 gate: V8 video decode + Ariel palette + VBL cadence.
    add_executable(v8_video_test tests/v8_video_test.cpp)
    target_link_libraries(v8_video_test PRIVATE pom68k_core)
    add_test(NAME v8_video_test COMMAND v8_video_test)

    # SIMPLIFICATIONS_REVIEW F4: the Finder's "Restart" — the firmware's
    # RESET_SYSTEM reaching the 68k. Needs an MCU dump; SKIPs without one.
    add_executable(cuda_restart_test tests/cuda_restart_test.cpp)
    target_link_libraries(cuda_restart_test PRIVATE pom68k_core)
    add_test(NAME cuda_restart_test COMMAND cuda_restart_test)

    # SIMPLIFICATIONS_REVIEW F2: the Sonora CLUT's monochrome blue gun —
    # applied at the DAC write on the active modeline, as MAME does, so no
    # boot etalon (both LC III monitors are RGB) ever walks it.
    add_executable(sonora_video_test tests/sonora_video_test.cpp)
    target_link_libraries(sonora_video_test PRIVATE pom68k_core)
    add_test(NAME sonora_video_test COMMAND sonora_video_test)

    # LLE_VS_HLE §1.1 gate, step 3: the row-range invariant — decoding a
    # frame in arbitrary row chunks must be bit-identical to one pass, on
    # every decoder and at every depth. Catches the row-offset arithmetic
    # the whole-frame → decodeRows conversion introduced (fixed 1024/2048
    # pitches, modeline-derived ones), which a boot etalon cannot see.
    add_executable(raster_equiv_test tests/raster_equiv_test.cpp)
    target_link_libraries(raster_equiv_test PRIVATE pom68k_core)
    add_test(NAME raster_equiv_test COMMAND raster_equiv_test)

    # LLE_VS_HLE §1.1 gate, step 2: beam-placed decode on the V8 — a
    # mid-frame palette change splits the picture AT the beam, and VRAM
    # written behind the beam does not reach rows already scanned out.
    add_executable(v8_raster_test tests/v8_raster_test.cpp)
    target_link_libraries(v8_raster_test PRIVATE pom68k_core)
    add_test(NAME v8_raster_test COMMAND v8_raster_test)

    # Q8 gate: MEMCjr DAFB stride register + Antelope RAMDAC depth modes.
    add_executable(q605_dafb_test tests/q605_dafb_test.cpp)
    target_link_libraries(q605_dafb_test PRIVATE pom68k_core)
    add_test(NAME q605_dafb_test COMMAND q605_dafb_test)

    add_executable(q605_event_scheduler_test tests/q605_event_scheduler_test.cpp)
    target_link_libraries(q605_event_scheduler_test PRIVATE pom68k_core)
    add_test(NAME q605_event_scheduler_test COMMAND q605_event_scheduler_test)
    set_tests_properties(q605_event_scheduler_test PROPERTIES LABELS "m040;unit")

    add_executable(quadra_event_scheduler_test tests/quadra_event_scheduler_test.cpp)
    target_link_libraries(quadra_event_scheduler_test PRIVATE pom68k_core)
    add_test(NAME quadra_event_scheduler_test COMMAND quadra_event_scheduler_test)
    set_tests_properties(quadra_event_scheduler_test PROPERTIES LABELS "m040;unit")

    # LLE step 9 gate: PrimeTime TurboSCSI wait-state cell (IOSB reg 2)
    # + the 53C96 MAME-derived selection/transfer delay model.
    add_executable(q605_turboscsi_test tests/q605_turboscsi_test.cpp)
    target_link_libraries(q605_turboscsi_test PRIVATE pom68k_core)
    add_test(NAME q605_turboscsi_test COMMAND q605_turboscsi_test)

    # SCC async-baud LLE gate: WR4/WR11/WR12-14 → derived byte pace
    # (MAME z80scc get_clock_mode/get_brg_rate/update_serial parity).
    add_executable(scc_baud_test tests/scc_baud_test.cpp)
    target_link_libraries(scc_baud_test PRIVATE pom68k_core)
    add_test(NAME scc_baud_test COMMAND scc_baud_test)

    # SCC Tx/Rx-engine LLE gate (backlog Medium tier): WR5 Tx-Enable
    # gating, paced Tx buffer/shifter + TxIP on the buffer-empty edge,
    # SDLC tail (CRC+flag) at the programmed pace, Rx FCS verification,
    # async parity/framing error bits + WR1 bit 2 special routing.
    add_executable(scc_engine_test tests/scc_engine_test.cpp)
    target_link_libraries(scc_engine_test PRIVATE pom68k_core)
    add_test(NAME scc_engine_test COMMAND scc_engine_test)

    # R65C02 core gate (docs/IOP_BRINGUP.md M1): Klaus Dormann's 6502
    # functional + 65C02 extended-opcodes images (committed under
    # tests/assets/) run to their success traps on the vendored core.
    add_executable(r65c02_test tests/r65c02_test.cpp)
    target_link_libraries(r65c02_test PRIVATE pom68k_core)
    add_test(NAME r65c02_test COMMAND r65c02_test
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # Apple PIC IOP gate (docs/IOP_BRINGUP.md M2): a 65C02 program uploaded
    # through the host window proves reset-release, both mailbox interrupt
    # directions, timer one-shot + continuous cadence, 2-channel DMA and
    # the bypass path.
    add_executable(applepic_test tests/applepic_test.cpp)
    target_link_libraries(applepic_test PRIVATE pom68k_core)
    add_test(NAME applepic_test COMMAND applepic_test
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # Egret/Cuda firmware-LLE gate (blueprint step 1): the M68HC05E1 core
    # runs the real Cuda 2.37 dump from its reset vector (soft-skips).
    add_executable(m68hc05_test tests/m68hc05_test.cpp)
    target_link_libraries(m68hc05_test PRIVATE pom68k_core)
    add_test(NAME m68hc05_test COMMAND m68hc05_test
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # Blueprint step 2: the firmware behind the Quadra VIA (POM68K_CUDA_LLE;
    # reset release + PRAM install + /TREQ idle; soft-skips).
    add_executable(cuda_lle_test tests/cuda_lle_test.cpp)
    target_link_libraries(cuda_lle_test PRIVATE pom68k_core)
    add_test(NAME cuda_lle_test COMMAND cuda_lle_test
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # Blueprint step 3: Mac OS 8.1 boots to the Finder with the ADB/PRAM/
    # clock side on the REAL Cuda firmware (soft-skips).
    add_executable(q605_cudalle_boot_etalon tests/q605_cudalle_boot_etalon.cpp)
    target_link_libraries(q605_cudalle_boot_etalon PRIVATE pom68k_core)
    add_test(NAME q605_cudalle_boot_etalon COMMAND q605_cudalle_boot_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # Blueprint step 4: mouse motion through the real firmware's ADB
    # autopoll (AdbLine → 341S0788 → VIA SR → mouse driver; soft-skips).
    add_executable(q605_cudalle_mouse_etalon tests/q605_cudalle_mouse_etalon.cpp)
    target_link_libraries(q605_cudalle_mouse_etalon PRIVATE pom68k_core)
    add_test(NAME q605_cudalle_mouse_etalon COMMAND q605_cudalle_mouse_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # Egret flavor: the real 341S0850 behind the LC II's V8 VIA (falling-
    # edge PC3 release; soft-skips without the dump).
    add_executable(egret_lle_test tests/egret_lle_test.cpp)
    target_link_libraries(egret_lle_test PRIVATE pom68k_core)
    add_test(NAME egret_lle_test COMMAND egret_lle_test
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

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

    # Save-state gate under a real OS: boot the LC II to the Finder,
    # snapshot, run N frames, hash; restore, run the same N frames,
    # require the identical hash (soft-skips without ROM + image).
    add_executable(lcii_savestate_etalon tests/lcii_savestate_etalon.cpp)
    target_link_libraries(lcii_savestate_etalon PRIVATE pom68k_core)
    add_test(NAME lcii_savestate_etalon COMMAND lcii_savestate_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # The 040-side twin: Mac OS 8.1 on the Q605 machine — real-OS restore
    # determinism over the DAFB/AscIosb/53C96/Cuda-LLE chunks.
    add_executable(q605_savestate_etalon tests/q605_savestate_etalon.cpp)
    target_link_libraries(q605_savestate_etalon PRIVATE pom68k_core)
    add_test(NAME q605_savestate_etalon COMMAND q605_savestate_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # Beyond-boot gates on the Quadra 605 (second machine after the LC II,
    # TODO §2): idle soak (Mac clock keeps time — catches the MCU-overclock
    # class), and Finder file creation surviving a reboot — the one gate
    # that drives the 53C96 WRITE path end to end from a real guest
    # (soft-skips without assets).
    add_executable(q605_beyond_etalon tests/q605_beyond_etalon.cpp)
    target_link_libraries(q605_beyond_etalon PRIVATE pom68k_core)
    foreach(scenario soak persist)
        add_test(NAME q605_${scenario}_etalon COMMAND q605_beyond_etalon
                 WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
        set_tests_properties(q605_${scenario}_etalon PROPERTIES
                             ENVIRONMENT "POM68K_BEYOND=${scenario}"
                             TIMEOUT 1800)
    endforeach()

    # Beyond-boot on the THIRD machine (TODO §2, the freshness tail): the
    # Macintosh IIvx — VASP + Egret 341S0851 + 68030, 640×480×8. Same two
    # scenarios. Chosen over the RBV siblings because physical low RAM IS
    # the framebuffer there, so the low-memory Time global is not readable
    # through peek8 — see the header of iivx_beyond_etalon.cpp.
    add_executable(iivx_beyond_etalon tests/iivx_beyond_etalon.cpp)
    target_link_libraries(iivx_beyond_etalon PRIVATE pom68k_core)
    foreach(scenario soak persist)
        add_test(NAME iivx_${scenario}_etalon COMMAND iivx_beyond_etalon
                 WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
        set_tests_properties(iivx_${scenario}_etalon PROPERTIES
                             ENVIRONMENT "POM68K_BEYOND=${scenario}"
                             TIMEOUT 1800)
    endforeach()

    # Beyond-boot on the FOURTH machine, and the first RAM-based-video one:
    # the IIsi. Blocked until 2026-08-13 on the logical-address read of the
    # Time global (peek8 is physical and low physical RAM is the framebuffer
    # there) — tests/Mmu030Peek.h is that read, a side-effect-free walk of
    # the live page tables.
    add_executable(rbv_beyond_etalon tests/rbv_beyond_etalon.cpp)
    target_link_libraries(rbv_beyond_etalon PRIVATE pom68k_core)
    foreach(scenario soak persist)
        add_test(NAME iisi_${scenario}_etalon COMMAND rbv_beyond_etalon
                 WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
        set_tests_properties(iisi_${scenario}_etalon PROPERTIES
                             ENVIRONMENT "POM68K_BEYOND=${scenario}"
                             TIMEOUT 1800)
    endforeach()

    # Beyond-boot for the whole roster (2026-08-13): every platform gets the
    # soak+persist pair on the shared engine (tests/BeyondBoot.h). One thin
    # gate binary per platform; POM68K_BEYOND picks the scenario.
    foreach(machine sonora centris q700 q630 duo macii iifx compact)
        add_executable(${machine}_beyond_etalon tests/${machine}_beyond_etalon.cpp)
        target_link_libraries(${machine}_beyond_etalon PRIVATE pom68k_core)
        foreach(scenario soak persist)
            add_test(NAME ${machine}_${scenario}_etalon
                     COMMAND ${machine}_beyond_etalon
                     WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
            set_tests_properties(${machine}_${scenario}_etalon PROPERTIES
                                 ENVIRONMENT "POM68K_BEYOND=${scenario}"
                                 TIMEOUT 1800)
        endforeach()
    endforeach()

    # The Eclipse pair (2026-08-14): the same binary on the tower profile,
    # which is a different MACHINE past the boot screen — two Apple PIC IOPs,
    # an Egret firmware LLE and a second 53C96, none of which the Spike's
    # legs keep alive. The soak is what would notice an IOP that stops
    # answering three minutes in; the persist leg is the only gate that drives
    # the tower's ADB (the SWIM IOP's bit-banged wire) from the Toolbox. It
    # boots twice at 16000 frames, so it gets a longer budget than the pairs
    # above.
    foreach(scenario soak persist)
        add_test(NAME q900_${scenario}_etalon COMMAND q700_beyond_etalon
                 WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
        set_tests_properties(q900_${scenario}_etalon PROPERTIES
                             ENVIRONMENT "POM68K_BEYOND=${scenario};POM68K_Q700_MODEL=q900"
                             TIMEOUT 2400)
    endforeach()

    # Phase C gate: Macintosh LC (68020 on the same V8 board, 2 MB
    # soldered) boots to the Finder (soft-skips without ROM + image).
    add_executable(lc_boot_etalon tests/lc_boot_etalon.cpp)
    target_link_libraries(lc_boot_etalon PRIVATE pom68k_core)
    add_test(NAME lc_boot_etalon COMMAND lc_boot_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # Phase C gate: Macintosh Classic II (Eagle — mono 512×342 out of
    # main RAM, ROM boxflag patch) boots to the Finder (soft-skips).
    add_executable(classic2_boot_etalon tests/classic2_boot_etalon.cpp)
    target_link_libraries(classic2_boot_etalon PRIVATE pom68k_core)
    add_test(NAME classic2_boot_etalon COMMAND classic2_boot_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # Phase C gate: Macintosh Color Classic (Spice — built-in 512×384
    # color, SWIM2, 1 MB ROM, Cuda firmware LLE) boots to the Finder
    # (soft-skips without the ROM + a bootable image).
    add_executable(cclassic_boot_etalon tests/cclassic_boot_etalon.cpp)
    target_link_libraries(cclassic_boot_etalon PRIVATE pom68k_core)
    add_test(NAME cclassic_boot_etalon COMMAND cclassic_boot_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # Phase C gate: Macintosh LC III (Sonora — 68030 @ 25 MHz, Sonora
    # video, SWIM2, Egret 341S0851 firmware LLE) boots to the Finder
    # (soft-skips without the ROM + a bootable image).
    add_executable(lc3_boot_etalon tests/lc3_boot_etalon.cpp)
    target_link_libraries(lc3_boot_etalon PRIVATE pom68k_core)
    add_test(NAME lc3_boot_etalon COMMAND lc3_boot_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # Phase C gate: the LC III+ (33.33 MHz, model $A55A0003) on the SAME
    # Sonora machine boots to the Finder (soft-skips without assets).
    add_executable(lc3plus_boot_etalon tests/lc3plus_boot_etalon.cpp)
    target_link_libraries(lc3plus_boot_etalon PRIVATE pom68k_core)
    add_test(NAME lc3plus_boot_etalon COMMAND lc3plus_boot_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # Phase C gate: the all-in-one LC 520 (EDE66CBD universal ROM, model
    # $A55A0100, Cuda 341S0060 firmware LLE, built-in 640×480 sense 6) boots
    # System 7.5 to the 8-bpp color Finder — bring-up story in
    # docs/LC520_BRINGUP.md. Debug env knobs: POM68K_BOXID / POM68K_SENSE /
    # POM68K_PROBE / POM68K_HALT / POM68K_DIAG / POM68K_FRAMES / POM68K_DUMP.
    add_executable(lc520_boot_etalon tests/lc520_boot_etalon.cpp)
    target_link_libraries(lc520_boot_etalon PRIVATE pom68k_core)
    add_test(NAME lc520_boot_etalon COMMAND lc520_boot_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # Phase C gate: the LC 550 / Performa 550 — the LC 520 board at
    # 33.33 MHz with the $A55A0101 model longword (maclc550_map).
    add_executable(lc550_boot_etalon tests/lc550_boot_etalon.cpp)
    target_link_libraries(lc550_boot_etalon PRIVATE pom68k_core)
    add_test(NAME lc550_boot_etalon COMMAND lc550_boot_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # Phase C gate: the Color Classic II / Performa 275 — the LC 550 board
    # in the CC case; monitor sense 2 selects the ROM's 512×384 table entry.
    add_executable(cclassic2_boot_etalon tests/cclassic2_boot_etalon.cpp)
    target_link_libraries(cclassic2_boot_etalon PRIVATE pom68k_core)
    add_test(NAME cclassic2_boot_etalon COMMAND cclassic2_boot_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # Phase C gates: the Mac IIvx (VASP, 68030+68882 @ 31.3344 MHz, Egret
    # 341S0851 LLE) and the IIvi (same board @ 15.6672 MHz, POM68K_IIVI=1)
    # boot to the Finder off the shared 4957EB49 universal ROM.
    add_executable(iivx_boot_etalon tests/iivx_boot_etalon.cpp)
    target_link_libraries(iivx_boot_etalon PRIVATE pom68k_core)
    add_test(NAME iivx_boot_etalon COMMAND iivx_boot_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME iivi_boot_etalon COMMAND iivx_boot_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(iivi_boot_etalon PROPERTIES ENVIRONMENT "POM68K_IIVI=1")

    # Phase C gate: the Mac IIsi (68030 @ 20 MHz, RBV RAM-based video,
    # Egret 344S0100 firmware LLE, SWIM1, discrete ASC) boots to the
    # Finder off the 512 KB $36B7FB6C ROM (soft-skips without assets).
    add_executable(iisi_boot_etalon tests/iisi_boot_etalon.cpp)
    target_link_libraries(iisi_boot_etalon PRIVATE pom68k_core)
    add_test(NAME iisi_boot_etalon COMMAND iisi_boot_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # Phase C gate: the Mac IIx / IIcx (68030 on the Mac II GLUE board, Toby
    # NuBus video, mac2fdhd ROM) boot to the Finder (soft-skips without assets).
    add_executable(iix_boot_etalon tests/iix_boot_etalon.cpp)
    target_link_libraries(iix_boot_etalon PRIVATE pom68k_core)
    add_test(NAME iix_boot_etalon COMMAND iix_boot_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME iicx_boot_etalon COMMAND iix_boot_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(iicx_boot_etalon PROPERTIES ENVIRONMENT "POM68K_IICX=1")

    # The SE/30 — the compact IIx: same GLUE board and mac2fdhd ROM, internal
    # 512×342 video on pseudo-slot $E (se30vrom.uk6 decl ROM), VIA1 PB6-gated
    # slot VBL (soft-skips without assets).
    add_executable(se30_boot_etalon tests/se30_boot_etalon.cpp)
    target_link_libraries(se30_boot_etalon PRIVATE pom68k_core)
    add_test(NAME se30_boot_etalon COMMAND se30_boot_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # Phase C gate: the Mac IIci (RBV in its IIci flavor — 68030 @ 25 MHz,
    # PIC1654S ADB modem LLE + discrete 343-0042 RTC, empty NuBus) boots to
    # the Finder off the 512 KB $368CADFE ROM (soft-skips without assets).
    add_executable(iici_boot_etalon tests/iici_boot_etalon.cpp)
    target_link_libraries(iici_boot_etalon PRIVATE pom68k_core)
    add_test(NAME iici_boot_etalon COMMAND iici_boot_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # Phase C gate: the Macintosh TV (Tinker Bell V8 derivative, 68030 @
    # 31.3344 MHz no-FPU, Cuda LLE, built-in 640×480) boots to the Finder
    # off its own 1 MB $EAF1678D ROM (soft-skips without assets).
    add_executable(mactv_boot_etalon tests/mactv_boot_etalon.cpp)
    target_link_libraries(mactv_boot_etalon PRIVATE pom68k_core)
    add_test(NAME mactv_boot_etalon COMMAND mactv_boot_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # Phase C gates: the Mac Centris 650 (68LC040 @ 25 MHz, djMEMC + IOSB,
    # PIC1654S firmware LLE) and Centris 610 (20 MHz, POM68K_CENTRIS610=1)
    # boot to the DAFB Finder off the shared F1A6F343/F1ACAD13 ROM.
    add_executable(centris650_boot_etalon tests/centris650_boot_etalon.cpp)
    target_link_libraries(centris650_boot_etalon PRIVATE pom68k_core)
    add_test(NAME centris650_boot_etalon COMMAND centris650_boot_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME centris610_boot_etalon COMMAND centris650_boot_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(centris610_boot_etalon PROPERTIES
                         ENVIRONMENT "POM68K_CENTRIS610=1" TIMEOUT 1800)
    set_tests_properties(centris650_boot_etalon PROPERTIES TIMEOUT 1800)
    # Same djMEMC+IOSB machine with a full 68040 (POM68K_CENTRIS_FPU) and the
    # Quadra ID pins: Quadra 650 (33 MHz, $52) and Quadra 610 (25 MHz, $44).
    add_test(NAME quadra650_boot_etalon COMMAND centris650_boot_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME quadra610_boot_etalon COMMAND centris650_boot_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(quadra650_boot_etalon PROPERTIES
                         ENVIRONMENT "POM68K_CENTRIS_MODEL=q650" TIMEOUT 1800)
    set_tests_properties(quadra610_boot_etalon PROPERTIES
                         ENVIRONMENT "POM68K_CENTRIS_MODEL=q610" TIMEOUT 1800)
    # Quadra 630 / LC 580: F108 + PrimeTime II + Valkyrie, 68040 @ 33 MHz.
    add_executable(q630_boot_etalon tests/q630_boot_etalon.cpp)
    target_link_libraries(q630_boot_etalon PRIVATE pom68k_core)
    add_test(NAME q630_boot_etalon COMMAND q630_boot_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME lc580_boot_etalon COMMAND q630_boot_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(q630_boot_etalon PROPERTIES TIMEOUT 3600)
    set_tests_properties(lc580_boot_etalon PROPERTIES
                         ENVIRONMENT "POM68K_Q630_ID=A55A225A;POM68K_Q630_ROM=lc580"
                         TIMEOUT 3600)

    # Compact 68000 family (Mac SE / SE FDHD / Classic): the Plus machine with
    # a bigger ROM and the PIC1654S ADB transceiver instead of the M0110.
    # One binary, POM68K_COMPACT_MODEL picks the sibling.
    add_executable(compact_boot_etalon tests/compact_boot_etalon.cpp)
    target_link_libraries(compact_boot_etalon PRIVATE pom68k_core)
    add_test(NAME se_boot_etalon COMMAND compact_boot_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME sefdhd_boot_etalon COMMAND compact_boot_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME classic_boot_etalon COMMAND compact_boot_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(se_boot_etalon PROPERTIES TIMEOUT 1800)
    set_tests_properties(sefdhd_boot_etalon PROPERTIES
                         ENVIRONMENT "POM68K_COMPACT_MODEL=sefdhd" TIMEOUT 1800)
    set_tests_properties(classic_boot_etalon PROPERTIES
                         ENVIRONMENT "POM68K_COMPACT_MODEL=classic" TIMEOUT 1800)

    # Quadra 700 ("Spike"): the first Quadra — a full 68040 on discrete
    # chips (Mac II VIA1/VIA2 + RTC + PIC ADB, Quadra DAFB/53C96/SWIM1/EASC),
    # SCSI behind DAFB's own TurboSCSI cell. $420DBFF3 ROM.
    add_executable(q700_boot_etalon tests/q700_boot_etalon.cpp)
    target_link_libraries(q700_boot_etalon PRIVATE pom68k_core)
    add_test(NAME q700_boot_etalon COMMAND q700_boot_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(q700_boot_etalon PROPERTIES TIMEOUT 1800)

    # Quadra 900 ("Eclipse"): the same board with the Mac IIfx's front end —
    # two Apple PIC IOPs (SCC + SWIM/ADB) running host-uploaded 65C02
    # firmware, the Egret in place of the discrete RTC, a second 53C96 bus.
    # Same binary, same $420DBFF3 ROM, selected by argv (docs/IOP_BRINGUP.md
    # § M7). Gated the day it reached the Finder — 2026-08-02.
    add_test(NAME q900_boot_etalon COMMAND q700_boot_etalon q900
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(q900_boot_etalon PROPERTIES TIMEOUT 1800)

    # Quadra 950 ("Zydeco"): the Eclipse at 33.333 MHz with its own
    # $3DC27823 ROM, `via_in_a_q950` identity and the DAFB_Q950 flavour. It
    # came up on the same day as the Q900 and off the same fix — and it
    # lands at 640×480×**8**, so it is also the tower gate that exercises a
    # colour DAFB path rather than the Q900's 1 bpp.
    add_test(NAME q950_boot_etalon COMMAND q700_boot_etalon q950
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(q950_boot_etalon PROPERTIES TIMEOUT 1800)

    # Quadra 800: same board, ID pins $12 (the only one with pa6 clear), full
    # 68040 @ 33 MHz, plus SONIC Ethernet (address ROM at $50008000; the SONIC
    # registers stay unmapped-0) and three NuBus slots.
    add_test(NAME quadra800_boot_etalon COMMAND centris650_boot_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(quadra800_boot_etalon PROPERTIES
                         ENVIRONMENT "POM68K_CENTRIS_MODEL=q800" TIMEOUT 1800)

    # Floppy write persistence: committed sectors reach the host .dsk /
    # DC42 file on eject/exit when write-back is opted in (GUI default).
    # Gate: the SCSI CD-ROM personality (type $05, 2048-byte blocks, READ
    # TOC, and the Apple magic MODE SENSE page $30). Self-contained.
    # Gate: a CD mounts IN THE GUEST (boot volume at SCSI 6 so the 6→0
    # scan does not boot off the disc). Soft-skips without a CD image.
    add_executable(q605_cdrom_etalon tests/q605_cdrom_etalon.cpp)
    target_link_libraries(q605_cdrom_etalon PRIVATE pom68k_core)
    add_test(NAME q605_cdrom_etalon COMMAND q605_cdrom_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    # The other half: no hard disk, so the ROM scan reaches the CD and
    # boots it. Needs a 68k-bootable disc (Mac OS 8.1 — 8.5/8.6 are
    # PowerPC-only).
    add_test(NAME q605_cdboot_etalon COMMAND q605_cdrom_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(q605_cdboot_etalon PROPERTIES
                         ENVIRONMENT "POM68K_CD_BOOT=1" TIMEOUT 1800)
    # The third half :-) — the drive is empty through the boot and the disc
    # is hot-inserted at the Finder (UNIT ATTENTION $28 + READ SUB-CHANNEL).
    add_test(NAME q605_cdhot_etalon COMMAND q605_cdrom_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(q605_cdhot_etalon PROPERTIES
                         ENVIRONMENT "POM68K_CD_HOT=1" TIMEOUT 1800)

    add_executable(scsi_cdrom_test tests/scsi_cdrom_test.cpp)
    target_link_libraries(scsi_cdrom_test PRIVATE pom68k_core)
    add_test(NAME scsi_cdrom_test COMMAND scsi_cdrom_test)

    add_executable(floppy_persist_test tests/floppy_persist_test.cpp)
    target_link_libraries(floppy_persist_test PRIVATE pom68k_core)
    add_test(NAME floppy_persist_test COMMAND floppy_persist_test)

    # Beyond-boot gates on the reference LC II: idle soak (Mac clock keeps
    # time, no hang), Finder file creation surviving a reboot, and an app
    # launched by mouse from the desktop (soft-skip without assets).
    add_executable(lcii_beyond_etalon tests/lcii_beyond_etalon.cpp)
    target_link_libraries(lcii_beyond_etalon PRIVATE pom68k_core)
    foreach(scenario soak persist launch floppy)
        add_test(NAME lcii_${scenario}_etalon COMMAND lcii_beyond_etalon
                 WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
        set_tests_properties(lcii_${scenario}_etalon PROPERTIES
                             ENVIRONMENT "POM68K_BEYOND=${scenario}"
                             TIMEOUT 1800)
    endforeach()

    # O6 slice 1 gate: external /BERR + RTE $A/$B on the 68030 (the LC II
    # ROM's address-map probe and the SCSI pseudo-DMA timeout rely on it).
    add_executable(berr030_test tests/berr030_test.cpp)
    target_link_libraries(berr030_test PRIVATE moira)
    add_test(NAME berr030_test COMMAND berr030_test)

    # AArch64 68030 gate: train a native MOVE.B-to-memory block, inject an
    # external /BERR only after it is cached, and compare the complete short
    # restartable-write frame with a pure-interpreter oracle. Soft-skips on
    # hosts where the AArch64 backend is not available.
    add_executable(jit_restart_write_030_test tests/jit_restart_write_030_test.cpp)
    target_link_libraries(jit_restart_write_030_test PRIVATE pom68k_core)
    add_test(NAME jit_restart_write_030_test COMMAND jit_restart_write_030_test)
    set_tests_properties(jit_restart_write_030_test PROPERTIES LABELS "jit;unit")

    # Versioned performance budgets. CMake validates the manifest and turns
    # each consumed value into a compile-time constant; tests therefore fail
    # against reviewed policy rather than private literals in their source.
    set(POM68K_PERF_BUDGET_FILE
        "${CMAKE_CURRENT_SOURCE_DIR}/performance_budgets.tsv")
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" POM68K_PERF_HOST)
    if(POM68K_PERF_HOST MATCHES "^(arm64|aarch64)$")
        set(POM68K_PERF_HOST "aarch64")
    elseif(POM68K_PERF_HOST MATCHES "^(x86_64|amd64)$")
        set(POM68K_PERF_HOST "x86_64")
    else()
        set(POM68K_PERF_HOST "other")
    endif()
    file(STRINGS "${POM68K_PERF_BUDGET_FILE}" pom68k_perf_budget_lines)
    foreach(line IN LISTS pom68k_perf_budget_lines)
        if(line MATCHES "^[ \t]*#" OR line STREQUAL "")
            continue()
        endif()
        string(REPLACE "\t" ";" fields "${line}")
        list(LENGTH fields field_count)
        if(NOT field_count EQUAL 6)
            message(FATAL_ERROR
                "performance_budgets.tsv: expected six columns: ${line}")
        endif()
        list(GET fields 0 workload)
        list(GET fields 1 cpu_family)
        list(GET fields 2 perf_host)
        list(GET fields 3 metric)
        list(GET fields 4 value)
        if(NOT value MATCHES "^[0-9]+$")
            message(FATAL_ERROR
                "performance_budgets.tsv: non-integer value: ${line}")
        endif()
        set("POM68K_PERF_${workload}_${cpu_family}_${perf_host}_${metric}"
            "${value}")
    endforeach()
    function(pom68k_perf_budget target workload cpu_family metric macro)
        set(key "POM68K_PERF_${workload}_${cpu_family}_${POM68K_PERF_HOST}_${metric}")
        set(fallback "POM68K_PERF_${workload}_${cpu_family}_any_${metric}")
        if(DEFINED ${key})
            set(value "${${key}}")
        elseif(DEFINED ${fallback})
            set(value "${${fallback}}")
        else()
            message(FATAL_ERROR
                "performance_budgets.tsv: missing ${workload}/${cpu_family}/"
                "${POM68K_PERF_HOST}/${metric}")
        endif()
        target_compile_definitions(${target} PRIVATE
            "${macro}=${value}")
    endfunction()

    # Daily native lockstep with no Apple assets: a deterministic flat-RAM
    # 68040 program compares complete architectural boundaries and RAM, then
    # injects both restart and last-write bus faults. `jit-fast` makes native
    # host execution mandatory on the A64 and x64 CI runners.
    add_executable(jit_asset_free_lockstep_test
                   tests/jit_asset_free_lockstep_test.cpp)
    target_link_libraries(jit_asset_free_lockstep_test PRIVATE pom68k_core)
    pom68k_perf_budget(jit_asset_free_lockstep_test
        synthetic_68040_lockstep 68040 min_blocks_compiled
        POM68K_PERF_MIN_BLOCKS_COMPILED)
    pom68k_perf_budget(jit_asset_free_lockstep_test
        synthetic_68040_lockstep 68040 min_blocks_run
        POM68K_PERF_MIN_BLOCKS_RUN)
    pom68k_perf_budget(jit_asset_free_lockstep_test
        synthetic_68040_lockstep 68040 min_native_share_permille
        POM68K_PERF_MIN_NATIVE_SHARE_PERMILLE)
    pom68k_perf_budget(jit_asset_free_lockstep_test
        synthetic_68040_lockstep 68040 max_slow_instrs
        POM68K_PERF_MAX_SLOW_INSTRUCTIONS)
    add_test(NAME jit_asset_free_lockstep_test COMMAND jit_asset_free_lockstep_test)
    set_tests_properties(jit_asset_free_lockstep_test PROPERTIES LABELS "jit;unit")

    # J4 write half: one generated copyback hit must publish the precise
    # dirty-longword mask, while misses retain the 68040 format-$7
    # last-write/restart dichotomy. Runs on whichever native host backend is
    # available and soft-skips on hosts that only have the threaded engine.
    add_executable(jit_copyback_write_040_test
                   tests/jit_copyback_write_040_test.cpp)
    target_link_libraries(jit_copyback_write_040_test PRIVATE pom68k_core)
    pom68k_perf_budget(jit_copyback_write_040_test
        synthetic_68040_copyback 68040 max_slow_instrs
        POM68K_PERF_COPYBACK_MAX_SLOW_INSTRUCTIONS)
    pom68k_perf_budget(jit_copyback_write_040_test
        synthetic_68040_copyback 68040 max_native_ratio_permille
        POM68K_PERF_COPYBACK_MAX_RATIO_PERMILLE)
    pom68k_perf_budget(jit_copyback_write_040_test
        synthetic_68040_copyback 68040 native_slack_microseconds
        POM68K_PERF_COPYBACK_SLACK_US)
    add_test(NAME jit_copyback_write_040_test COMMAND jit_copyback_write_040_test)
    set_tests_properties(jit_copyback_write_040_test PROPERTIES LABELS "jit;unit")
    # The attribution switch is itself contractual: OFF must replay the exact
    # cache-aware write rather than exposing stale backing RAM through DTLB.
    add_test(NAME jit_copyback_write_040_control_test
             COMMAND jit_copyback_write_040_test --disabled)
    set_tests_properties(jit_copyback_write_040_control_test PROPERTIES
                         LABELS "jit;unit")
    # Highest-yield real consumer after the paired census: BSR's sole
    # return-address push, including dirty publication and /BERR stack switch.
    add_test(NAME jit_copyback_bsr_040_test
             COMMAND jit_copyback_write_040_test --bsr)
    set_tests_properties(jit_copyback_bsr_040_test PROPERTIES
                         LABELS "jit;unit")
    # The remaining hot MOVE pair needs two cache proofs before either
    # access becomes visible. The OFF twin pins whole-instruction replay.
    add_test(NAME jit_copyback_pair_040_test
             COMMAND jit_copyback_write_040_test --pair)
    set_tests_properties(jit_copyback_pair_040_test PROPERTIES
                         LABELS "jit;unit")
    add_test(NAME jit_copyback_pair_040_control_test
             COMMAND jit_copyback_write_040_test --pair-disabled)
    set_tests_properties(jit_copyback_pair_040_control_test PROPERTIES
                         LABELS "jit;unit")

    # AArch64 68040 gate for opcode-local removal of the conservative store
    # fallback. It proves both halves of the contract: mask-null ordinary RAM
    # is direct, while a true overlap with translated code is observed by the
    # memory map and precisely invalidates the cached block. Soft-skips away
    # from AArch64.
    add_executable(jit_store_guard_a64_test tests/jit_store_guard_a64_test.cpp)
    target_link_libraries(jit_store_guard_a64_test PRIVATE pom68k_core)
    add_test(NAME jit_store_guard_a64_test COMMAND jit_store_guard_a64_test)
    set_tests_properties(jit_store_guard_a64_test PROPERTIES LABELS "jit;unit")

    # M1-M3 gate (docs/CACHE_040.md): 68040 cache geometry and line data,
    # CACR/CM/TTR policy, copyback/CPUSH/CINV, snooping and hit/fill timing.
    add_executable(cache040_test tests/cache040_test.cpp)
    target_link_libraries(cache040_test PRIVATE moira)
    add_test(NAME cache040_test COMMAND cache040_test)

    # O5 gate: hand-computed 68882 smoke cases (FMOVECR/FADD/DZ/OPERR/
    # FCMP/FMOVEM, format-$9 IRQ + BUSY resume, detached F-line). The corpus
    # through sst68030 (tests/data/sst68030_fpu, generated by a parallel
    # fuzz run) once available.
    add_executable(fpu_sanity tests/fpu_sanity.cpp)
    target_link_libraries(fpu_sanity PRIVATE moira)
    add_test(NAME fpu_sanity COMMAND fpu_sanity)

    add_executable(fpu040_test tests/fpu040_test.cpp)
    target_link_libraries(fpu040_test PRIVATE moira)
    add_test(NAME fpu040_test COMMAND fpu040_test)

    add_executable(callm_rtm_test tests/callm_rtm_test.cpp)
    target_link_libraries(callm_rtm_test PRIVATE moira)
    add_test(NAME callm_rtm_test COMMAND callm_rtm_test)

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

    # Gate: LLE ADB (PIC1654S) delivers mouse motion to the Finder — boots,
    # injects deltas, requires RawMouse/Mouse to move (soft-skips without
    # ROM+disk). Also pins the VIA2 CA1 slot-VBL → jCrsrTask coupling.
    add_executable(macii_mouse_trace tests/macii_mouse_trace.cpp)
    target_link_libraries(macii_mouse_trace PRIVATE pom68k_core)
    add_test(NAME macii_mouse_etalon COMMAND macii_mouse_trace
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # Dev harness: ROM×disk Finder matrix (not a CTest gate).
    add_executable(finder_boot_matrix EXCLUDE_FROM_ALL tests/finder_boot_matrix.cpp)
    target_link_libraries(finder_boot_matrix PRIVATE pom68k_core)

    # Dev harness: fixed guest-cycle stopwatch for the two engines (and a
    # screen dump when a boot etalon's signature needs eyes on it). Not a
    # gate — it measures, it does not judge. See src/jit/POM68K_JIT.md § 3.
    add_executable(jit_bench EXCLUDE_FROM_ALL tests/jit_bench.cpp)
    target_link_libraries(jit_bench PRIVATE pom68k_core)

    # The same stopwatch on the LC II — the machine `TODO.md` § 0·A names as
    # the speed objective ("~×1,3 temps réel en turbo"), and the one a
    # Quadra-only bench could say nothing about. Body shared with jit_bench:
    # tests/BenchHarness.h.
    add_executable(jit_bench_lcii EXCLUDE_FROM_ALL tests/jit_bench_lcii.cpp)
    target_link_libraries(jit_bench_lcii PRIVATE pom68k_core)

    # ── JIT gates (src/jit/POM68K_JIT.md) ────────────────────────────────
    # The portability seam: backend selection, W^X executable memory, and
    # the block classifier's safety rules. No assets, runs everywhere.
    add_executable(jit_backend_test tests/jit_backend_test.cpp)
    target_link_libraries(jit_backend_test PRIVATE pom68k_core)
    add_test(NAME jit_backend_test COMMAND jit_backend_test)

    # The decisive one: two Quadra 605 machines from the same ROM, one
    # interpreted and one JIT-driven, compared register by register at every
    # instruction boundary. Fails if the JIT never actually ran a block, so
    # a silent fallback cannot masquerade as a green gate.
    add_executable(jit_lockstep_test tests/jit_lockstep_test.cpp)
    target_link_libraries(jit_lockstep_test PRIVATE pom68k_core)
    add_test(NAME jit_lockstep_test COMMAND jit_lockstep_test
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(jit_lockstep_test PROPERTIES TIMEOUT 1800)

    # The portable backend with its block path forced on: `auto` runs it
    # WITHOUT blocks (measured faster that way), so this is the only gate
    # that exercises block discovery on the floor every platform has.
    add_test(NAME jit_lockstep_blocks_test COMMAND jit_lockstep_test
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(jit_lockstep_blocks_test PROPERTIES
                         ENVIRONMENT "POM68K_JIT_BACKEND=threaded;POM68K_JIT_BLOCKS=1"
                         TIMEOUT 1800)

    # ── the cycle-exact lockstep (2026-08-06) ────────────────────────────
    # The 68000 twin, and it asks a different question. On Core::C68020 —
    # every other guest the engine reaches — SYNC(x) expands to nothing, so
    # a windowed fetch cannot move the clock and the gates above compare
    # architectural state. On Core::C68000 SYNC is real, and
    # Moira::pomJitFetch000 has to re-charge two SYNC(2)s, POLL_IPL and the
    # Mac Plus bus contention by hand. `clock` is therefore compared at
    # every instruction boundary, alongside the VIA/IWM/drive/SCC state a
    # cycle drift would move first.
    #
    # Coarse-then-fine, because the two halves cover different things. At
    # one cycle per comparison the machine only reaches 26 M guest cycles in
    # a couple of seconds — still inside the ROM's RAM test, where the
    # window arms FOUR times and never dies. A 256-cycle budget covers 666 M
    # cycles in eleven seconds: the whole POST, the GCR floppy read, the
    # System load and the Finder, with 83 000 window arms. FINE_AT then
    # drops the last 100 000 checkpoints to one cycle each, so the sharpest
    # comparison happens in live Finder code rather than in a boot loop.
    add_executable(jit_lockstep_68000_test tests/jit_lockstep_68000_test.cpp)
    target_link_libraries(jit_lockstep_68000_test PRIVATE pom68k_core)
    add_test(NAME jit_lockstep_68000_test COMMAND jit_lockstep_68000_test 2500000
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(jit_lockstep_68000_test PROPERTIES
                         ENVIRONMENT "POM68K_JIT_LOCKSTEP_BUDGET=256;POM68K_JIT_LOCKSTEP_FINE_AT=2400000"
                         TIMEOUT 1800)

    # …and with the block path forced on. Recorded blocks are safe on a
    # cycle-exact core only because the `threaded` backend replays through
    # pomJitExecOne() — Moira's own handlers, charging Moira's own cycles —
    # and never replays the recorded Instr::cycles. That is a property worth
    # a gate, because a code generator doing the arithmetic itself would be
    # wrong here in a way no architectural comparison would catch.
    add_test(NAME jit_lockstep_68000_blocks_test COMMAND jit_lockstep_68000_test 2500000
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(jit_lockstep_68000_blocks_test PROPERTIES
                         ENVIRONMENT "POM68K_JIT_BACKEND=threaded;POM68K_JIT_BLOCKS=1;POM68K_JIT_HOT=1;POM68K_JIT_LOCKSTEP_BUDGET=256;POM68K_JIT_LOCKSTEP_FINE_AT=2400000"
                         TIMEOUT 1800)

    # ── the 68030 lockstep (docs/JIT_BRINGUP.md § C.1) ───────────────────
    # Every gate above runs two Quadra 605s, so until this one the 68030
    # side of the engine had no instruction-level differential coverage at
    # all — and that gap has already been paid for: on 2026-07-30 `auto`
    # handed the x86-64 generator the 030 machines and jit_lcii_boot_etalon
    # timed out after an HOUR, with nothing to say beyond "it did not
    # arrive". A boot etalon cannot name an instruction; this can.
    #
    # It also compares the three PomIcache counters, which is the half a
    # Quadra gate cannot have: Moira charges the 030 i-cache overlay inside
    # mmuFetchWord, before the JIT window hook, so today both engines charge
    # it identically — and a code generator emitting 030 blocks fetches no
    # instructions and would charge nothing at all.
    add_executable(jit_lockstep_030_test tests/jit_lockstep_030_test.cpp)
    target_link_libraries(jit_lockstep_030_test PRIVATE pom68k_core)
    add_test(NAME jit_lockstep_030_test COMMAND jit_lockstep_030_test 120000
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(jit_lockstep_030_test PROPERTIES
                         ENVIRONMENT "POM68K_JIT_LOCKSTEP_BUDGET=8192;POM68K_JIT_LOCKSTEP_FINE_AT=110000"
                         TIMEOUT 1800)

    # …and with the block path forced on, same argument as the 68000 pair
    # above: `threaded` replays through pomJitExecOne(), so it charges
    # Moira's own cycles and the i-cache along with them. A future 030 code
    # generator will not, which is exactly what this variant exists to
    # notice.
    add_test(NAME jit_lockstep_030_blocks_test COMMAND jit_lockstep_030_test 120000
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(jit_lockstep_030_blocks_test PROPERTIES
                         ENVIRONMENT "POM68K_JIT_BACKEND=threaded;POM68K_JIT_BLOCKS=1;POM68K_JIT_HOT=1;POM68K_JIT_LOCKSTEP_BUDGET=8192;POM68K_JIT_LOCKSTEP_FINE_AT=110000"
                         TIMEOUT 1800)

    # Apple-Silicon release gate. A coarse budget exercises complete native
    # blocks, linked exits and peripheral synchronization; five million
    # comparisons cover every late-boot A64 regression found during bring-up.
    # This is also the permanent architectural-cache/J4 gate: the diagnostic
    # counter makes a green run with zero native physical-line hits fail.
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$" AND
       POM68K_JIT_BACKENDS STREQUAL "auto")
        add_test(NAME jit_lockstep_a64_coarse_test COMMAND jit_lockstep_test 5000000
                 WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
        set_tests_properties(jit_lockstep_a64_coarse_test PROPERTIES
                             ENVIRONMENT "POM68K_JIT_BACKEND=a64;POM68K_JIT_HOT=1;POM68K_JIT_LOCKSTEP_BUDGET=50;POM68K_040_DCACHE=1;POM68K_JIT_040_LINE_STATS=1"
                             TIMEOUT 1800)

        # Apple-Silicon 68030 gate: unlike the portable pair above, this one
        # exercises generated arm64 code, including the emitted 030 i-cache
        # model and its exact runtime-bail uncharge. The final 10k checks use
        # a fine budget to localise any future instruction-boundary drift.
        add_test(NAME jit_lockstep_030_a64_experimental_test COMMAND jit_lockstep_030_test 120000
                 WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
        set_tests_properties(jit_lockstep_030_a64_experimental_test PROPERTIES
                             ENVIRONMENT "POM68K_JIT_BACKEND=a64;POM68K_JIT_UNSAFE_BACKEND=1;POM68K_JIT_BLOCKS=1;POM68K_JIT_HOT=128;POM68K_JIT_LOCKSTEP_BUDGET=8192;POM68K_JIT_LOCKSTEP_FINE_AT=110000"
                             TIMEOUT 1800)
    else()
        # These two are the ONLY host-conditional gates, so the registry has
        # two sizes and the docs cannot state one number. Record what this
        # host is missing, with the labels the derivation loop below would
        # have given them (both: `unit` + `jit`; the coarse one also takes
        # `smoke` explicitly), so `docs_test` can hold the documented
        # AArch64 totals to account on any host instead of failing everywhere
        # else. Adding a third conditional gate means adding it here — and
        # docs_test says so in its own failure text. (2026-08-12)
        set(pom68k_absent_gates
            "jit_lockstep_a64_coarse_test\tunit,jit,smoke\njit_lockstep_030_a64_experimental_test\tunit,jit\n")
    endif()

    # ── the x86-64 code generator ────────────────────────────────────────
    # Every gate below names x64 so the generator remains covered even when
    # the build or host changes its automatic backend choice.
    # A WIDE cycle budget between checkpoints is deliberate: at one cycle a
    # compiled block can never be more than one instruction long, and the
    # interesting half of a code generator — long blocks, and a loop closing
    # on itself entirely inside generated code — would go unexercised behind
    # a green light.
    # The wide x64 variant carries the same cache-on native-line proof; the
    # fine and no-access variants below retain the default cacheless paths.
    add_test(NAME jit_lockstep_x64_test COMMAND jit_lockstep_test 1000000
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(jit_lockstep_x64_test PROPERTIES
                         ENVIRONMENT "POM68K_JIT_BACKEND=x64;POM68K_JIT_LOCKSTEP_BUDGET=256;POM68K_040_DCACHE=1;POM68K_JIT_040_LINE_STATS=1"
                         TIMEOUT 1800)

    # The same, one cycle at a time: the sharpest check there is, and the
    # one that catches a wrong flag or a wrong cycle count immediately.
    add_test(NAME jit_lockstep_x64_fine_test COMMAND jit_lockstep_test 200000
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(jit_lockstep_x64_fine_test PROPERTIES
                         ENVIRONMENT "POM68K_JIT_BACKEND=x64" TIMEOUT 1800)

    # The conservative data path: no per-access thunk, so every address the
    # inline TLB cannot serve hands the whole instruction back to Moira.
    # One environment variable away from being the shipping behaviour, so it
    # has to keep working.
    add_test(NAME jit_lockstep_noaccess_test COMMAND jit_lockstep_test 400000
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(jit_lockstep_noaccess_test PROPERTIES
                         ENVIRONMENT "POM68K_JIT_BACKEND=x64;POM68K_JIT_LOCKSTEP_BUDGET=256;POM68K_JIT_ACCESS_THUNK=0"
                         TIMEOUT 1800)

    # The same boot etalons, same executables, driven by the JIT instead of
    # the interpreter — the established "one binary, another environment"
    # idiom used by iivi/iicx/centris610/lc580/quadra800 above. Green means
    # the machine reached the identical Finder signature under the JIT.
    foreach(gate q605_boot_etalon centris650_boot_etalon
                 q630_boot_etalon q700_boot_etalon
                 # 030 extension (2026-07-28): the V8 family through the same
                 # engine — mmuFetchWord is the single 030 fetch choke point.
                 # mactv is here because it is the phase-fragile one: if the
                 # window ever perturbed guest cycles, Tinker Bell's Cuda
                 # transport would deadlock long before a signature failed.
                 lcii_boot_etalon mactv_boot_etalon
                 # …and one gate per additional 030 family (Sonora / VASP /
                 # RBV): same engine, each map's own span/guard plumbing.
                 lc3_boot_etalon iivx_boot_etalon iisi_boot_etalon
                 # The 020 seam's guards. The JIT is measurably NOT worth
                 # turning on for a plain 68020 (nothing to save: no MMU,
                 # one cheap fetch per instruction) — the LC (V8 map +
                 # as020) and the Mac II (GLUE map) are here so the seam
                 # stays CORRECT for the day a code generator gives it
                 # something to win with.
                 lc_boot_etalon macii_boot_etalon
                 # …and the Mac II family's 030 half, which is the case
                 # that DOES pay: same ATC probe and mmuFetchWord choke
                 # point as the V8/Sonora/VASP/RBV 030s, on the GLUE map.
                 # se30 rather than iix because it also exercises the
                 # pseudo-slot video path while the window is armed.
                 se30_boot_etalon
                 # The compacts (2026-08-06). These are the gates that
                 # matter most of the whole list, because this is the one
                 # family where the window is NOT free of cycle accounting:
                 # Moira::pomJitFetch000 has to charge what read<> would,
                 # contention included. A Mac Plus whose fetch cycles drifted
                 # would not fail with a wrong pixel, it would fail with a
                 # sound tempo or an IWM hold — hence both models: the Plus
                 # (VIA PA4 overlay, M0110) and — just below, because it is
                 # the one gate whose binary is shared — the Classic (ADB,
                 # and the overlay drops on the first low-RAM WRITE).
                 system_boot_etalon
                 # The IIfx (2026-08-06), which completes the set: every CPU
                 # wrapper in the tree now carries an engine. Its map is the
                 # easiest the window has met — 32-bit clean, no HMMU, no
                 # GLUE remap — but its overlay drops on a ROM-region READ,
                 # which is the one map move a write guard cannot see, so
                 # this gate is really about that.
                 iifx_boot_etalon)
        add_test(NAME jit_${gate} COMMAND ${gate}
                 WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
        set_tests_properties(jit_${gate} PROPERTIES
                             ENVIRONMENT "POM68K_CPU_ENGINE=jit" TIMEOUT 3600)
    endforeach()

    # The plain 68040 etalons now exercise the accelerated default. Preserve
    # one explicit interpreter oracle per 68040 platform: a default is only
    # conformant while the old reference remains independently runnable and
    # green on the exact same executable and assets.
    foreach(gate q605_boot_etalon centris650_boot_etalon
                 q630_boot_etalon q700_boot_etalon)
        add_test(NAME interp_${gate} COMMAND ${gate}
                 WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
        set_tests_properties(interp_${gate} PROPERTIES
                             ENVIRONMENT "POM68K_CPU_ENGINE=interp" TIMEOUT 3600)
    endforeach()

    # Product qualification is intentionally separate from the public
    # soft-skip etalons: enabling it promises that CI provisioned every
    # private ROM and firmware dump. Missing assets are hard failures.
    if(POM68K_PRODUCT_LLE_GATES)
        if(NOT TARGET POM68K)
            message(FATAL_ERROR "POM68K_PRODUCT_LLE_GATES requires the GUI executable")
        endif()
        if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$" OR
           NOT POM68K_JIT_BACKENDS STREQUAL "auto")
            message(FATAL_ERROR "POM68K_PRODUCT_LLE_GATES requires the native AArch64 backend")
        endif()
        set(lle_q605_rom "${CMAKE_CURRENT_SOURCE_DIR}/roms/1MB ROMs/1993-10 - FF7439EE - LC475,575,Quadra 605,Performa 475,476,575,577,578.ROM")
        set(lle_centris_rom "${CMAKE_CURRENT_SOURCE_DIR}/roms/1MB ROMs/1993-02 - F1A6F343 - Quadra, Centris 610,650.ROM")
        set(lle_q700_rom "${CMAKE_CURRENT_SOURCE_DIR}/roms/1MB ROMs/1991-10 - 420DBFF3 - Quadra 700&900 & PB140&170.ROM")
        set(lle_q630_rom "${CMAKE_CURRENT_SOURCE_DIR}/roms/1MB ROMs/1994-07 - 06684214 - LC,Quadra,Performa 630.ROM")
        set(lle_boot_disk "${CMAKE_CURRENT_SOURCE_DIR}/hdv/MacOS-8.1-boot.vhd")
        foreach(asset "${lle_q605_rom}" "${lle_centris_rom}" "${lle_q700_rom}" "${lle_q630_rom}"
                      "${lle_boot_disk}"
                      "${CMAKE_CURRENT_SOURCE_DIR}/roms/cuda/341s0788.bin"
                      "${CMAKE_CURRENT_SOURCE_DIR}/roms/cuda/341s0060.bin"
                      "${CMAKE_CURRENT_SOURCE_DIR}/roms/adbmodem/342s0440-b.bin")
            if(NOT EXISTS "${asset}")
                message(FATAL_ERROR "LLE product asset missing: ${asset}")
            endif()
        endforeach()
        # q900 joined this list on 2026-08-14, when the Eclipse gained its
        # Egret 341S0851 firmware LLE: until then it was the one profile
        # product mode refused on principle (`lle_a64_q900_refused`), because
        # its ADB/RTC MCU was a command-level model no dump could replace.
        foreach(pair
                "q605|${lle_q605_rom}|"
                "centris|${lle_centris_rom}|POM68K_CENTRIS_MODEL=c650"
                "q700|${lle_q700_rom}|POM68K_Q700_MODEL=q700"
                "q900|${lle_q700_rom}|POM68K_Q700_MODEL=q900"
                "q630|${lle_q630_rom}|POM68K_Q630_ID=A55A2252")
            string(REPLACE "|" ";" fields "${pair}")
            list(GET fields 0 profile)
            list(GET fields 1 rom_path)
            add_test(NAME lle_a64_${profile}_preflight
                     COMMAND POM68K --lle-aarch64-check "${rom_path}")
            list(LENGTH fields field_count)
            if(field_count GREATER 2)
                list(GET fields 2 profile_env)
                if(profile_env)
                    set_tests_properties(lle_a64_${profile}_preflight PROPERTIES
                                         ENVIRONMENT "${profile_env}")
                endif()
            endif()
            set_tests_properties(lle_a64_${profile}_preflight PROPERTIES
                                 WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                                 LABELS "product;lle;a64" TIMEOUT 60)
        endforeach()
        add_test(NAME lle_a64_forced_hle_refused
                 COMMAND POM68K --lle-aarch64-check "${lle_q605_rom}")
        set_tests_properties(lle_a64_forced_hle_refused PROPERTIES
                             ENVIRONMENT "POM68K_CUDA_LLE=0" WILL_FAIL TRUE
                             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                             LABELS "product;lle;a64" TIMEOUT 60)
        add_test(NAME lle_a64_missing_firmware_refused
                 COMMAND POM68K --lle-aarch64-check "${lle_q605_rom}")
        set_tests_properties(lle_a64_missing_firmware_refused PROPERTIES
                             ENVIRONMENT "POM68K_FIRMWARE_ROOT=${CMAKE_CURRENT_BINARY_DIR}/no-firmware"
                             WILL_FAIL TRUE WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                             LABELS "product;lle;a64" TIMEOUT 60)
        # The Eclipse's own forced-HLE refusal — the successor to
        # `lle_a64_q900_refused`, which asserted the whole profile was
        # unqualifiable. Now that its Egret runs real firmware, what must
        # stay refused is the FALLBACK: POM68K_EGRET_LLE=0 on the tower.
        add_test(NAME lle_a64_q900_forced_hle_refused
                 COMMAND POM68K --lle-aarch64-check "${lle_q700_rom}")
        set_tests_properties(lle_a64_q900_forced_hle_refused PROPERTIES
                             ENVIRONMENT "POM68K_Q700_MODEL=q900;POM68K_EGRET_LLE=0"
                             WILL_FAIL TRUE
                             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                             LABELS "product;lle;a64" TIMEOUT 60)

        # Full interpreted and native boots consume the same executable and
        # assert the same Finder signature. The A64 variants name the backend
        # explicitly; `auto` is not accepted as product evidence.
        foreach(gate q605_boot_etalon centris650_boot_etalon
                     q630_boot_etalon q700_boot_etalon)
            set_tests_properties(jit_${gate} PROPERTIES
                                 ENVIRONMENT "POM68K_CPU_ENGINE=jit;POM68K_JIT_BACKEND=a64")
            set_property(TEST jit_${gate} interp_${gate}
                         APPEND PROPERTY LABELS product lle a64-oracle)
        endforeach()
        set_property(TEST jit_lockstep_a64_coarse_test savestate_040_test
                     APPEND PROPERTY LABELS product lle a64-oracle)
    endif()

    # The ADB compact under the JIT. Out of the loop above because the
    # compact family is "one binary, POM68K_COMPACT_MODEL picks the
    # sibling" — the test NAME is not an executable name here, and the
    # model variable has to ride along with the engine one.
    add_test(NAME jit_classic_boot_etalon COMMAND compact_boot_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(jit_classic_boot_etalon PROPERTIES
                         ENVIRONMENT "POM68K_COMPACT_MODEL=classic;POM68K_CPU_ENGINE=jit"
                         TIMEOUT 3600)

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

    # Dev tool (not a gate): .Sony driver give-up trace on the LC II —
    # the sony_trace pattern aimed at the TODO §1 boosted-030 mount bug.
    add_executable(lcii_sony_trace EXCLUDE_FROM_ALL tests/lcii_sony_trace.cpp)
    target_link_libraries(lcii_sony_trace PRIVATE pom68k_core)

    # Dev tool (not a gate): LC 475 / Quadra 605 ROM boot trace (Q5).
    add_executable(q605_trace EXCLUDE_FROM_ALL tests/q605_trace.cpp)
    target_link_libraries(q605_trace PRIVATE pom68k_core)

    # Dev tool (not a gate): PowerBook Duo ROM boot trace (platform #11
    # milestone 1, docs/DUO_BRINGUP.md).
    add_executable(duo_trace EXCLUDE_FROM_ALL tests/duo_trace.cpp)
    target_link_libraries(duo_trace PRIVATE pom68k_core)

    # Dev tool (not a gate): Mac IIfx ROM POST trace (platform #12
    # milestone 3, docs/IOP_BRINGUP.md).
    add_executable(iifx_trace EXCLUDE_FROM_ALL tests/iifx_trace.cpp)
    target_link_libraries(iifx_trace PRIVATE pom68k_core)

    # Platform #12 gate (docs/IOP_BRINGUP.md milestone 3): the IIfx ROM's
    # POST on the OSS + dual-IOP board — both IOP firmwares uploaded
    # byte-perfect and running, OSS priorities programmed, SCSI boot scan
    # reached (soft-skips without the IIfx ROM).
    add_executable(iifx_post_etalon tests/iifx_post_etalon.cpp)
    target_link_libraries(iifx_post_etalon PRIVATE pom68k_core)
    add_test(NAME iifx_post_etalon COMMAND iifx_post_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # Platform #12 gate (docs/IOP_BRINGUP.md milestone 5): the IIfx boots
    # a real System to the Finder — ADB served by the SWIM IOP's own
    # firmware bit-banging the AdbLine devices (soft-skips without the
    # ROM or a bootable image).
    add_executable(iifx_boot_etalon tests/iifx_boot_etalon.cpp)
    target_link_libraries(iifx_boot_etalon PRIVATE pom68k_core)
    add_test(NAME iifx_boot_etalon COMMAND iifx_boot_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(iifx_boot_etalon PROPERTIES TIMEOUT 1800)

    # Platform #12 gate (docs/IOP_BRINGUP.md milestone 6): ADB input is
    # DELIVERED — mouse motion repaints the cursor and a held key reaches
    # KeyMap, through the SWIM IOP's firmware and the AdbLine devices.
    add_executable(iifx_input_etalon tests/iifx_input_etalon.cpp)
    target_link_libraries(iifx_input_etalon PRIVATE pom68k_core)
    add_test(NAME iifx_input_etalon COMMAND iifx_input_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(iifx_input_etalon PROPERTIES TIMEOUT 1800)

    # Platform #11 gate (docs/DUO_BRINGUP.md milestone 3): the Duo 230 —
    # MSC + PG&E LLE (BORG v2 mid-boot upload, /PMU_INT level) — boots
    # System 7.5.5 to the Finder on the GSC 640×400 grayscale panel
    # (soft-skips without the ROM, the PG&E dump or the 7.5.5 image).
    add_executable(duo230_boot_etalon tests/duo230_boot_etalon.cpp)
    target_link_libraries(duo230_boot_etalon PRIVATE pom68k_core)
    add_test(NAME duo230_boot_etalon COMMAND duo230_boot_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(lcii_trace PRIVATE pom68k_core)

    add_executable(macii_trace EXCLUDE_FROM_ALL tests/macii_trace.cpp)
    target_link_libraries(macii_trace PRIVATE pom68k_core)

    add_executable(macii_mm_probe EXCLUDE_FROM_ALL tests/macii_mm_probe.cpp)
    target_link_libraries(macii_mm_probe PRIVATE pom68k_core)

    add_executable(lcii_mouse_trace EXCLUDE_FROM_ALL tests/lcii_mouse_trace.cpp)
    target_link_libraries(lcii_mouse_trace PRIVATE pom68k_core)

    # Dev tool (not a gate): where an ADB keystroke dies, on any
    # (machine, image) pair — POM68K_PROBE_MACHINE/_IMG/_FRAMES.
    add_executable(adb_key_probe EXCLUDE_FROM_ALL tests/adb_key_probe.cpp)
    target_link_libraries(adb_key_probe PRIVATE pom68k_core)

    add_executable(macii_hmmu_trace EXCLUDE_FROM_ALL tests/macii_hmmu_trace.cpp)
    target_link_libraries(macii_hmmu_trace PRIVATE pom68k_core)

    # Dev tool (not a gate): offline Mac ROM introspection (header,
    # resources, trap table, UniversalInfo) — Basilisk II-derived parsers,
    # see docs/BASILISK_ROM_NOTES.md. Standalone, no emulator core.
    add_executable(rominfo EXCLUDE_FROM_ALL tools/rominfo.cpp)

    # ─── Test tiers (`ctest -L <label>`) ─────────────────────────────────
    # A bare `ctest` runs every gate and takes hours on a busy host — far too
    # slow to develop against, and the boot etalons are contention-sensitive
    # so `-j` is not an option either. Every gate therefore carries labels,
    # so a working loop can pick the smallest set that actually proves the
    # change at hand:
    #
    #   ctest -L smoke        ~35 s   ONE machine, BOTH engines — the JIT loop
    #   ctest -L unit         ~1 min  everything needing no ROM or disk image
    #   ctest -L jit          ~8 min  every JIT gate, all four 68040 machines
    #   ctest -L m040         ~25 min the 68040 family on both engines
    #   ctest -L etalon-core          ONE profile per platform — 12 of the 91
    #   ctest                         everything (the release gate)
    #
    # `etalon` on its own is 91 gates / ~3 h 35, which is a release gate and
    # not a pre-commit check — so the temptation to skip it grows with every
    # machine added. `etalon-core` is the answer to "does a change break a
    # PLATFORM": one representative profile from each of the 12 memory-map
    # implementations, chosen as the profile that platform was brought up on.
    # A change that survives it can still break a sibling PROFILE (a clock, a
    # model ID, an MCU), which is what the full label is for.
    # `-L etalon` still matches all 91: CTest matches labels by regex, and the
    # core gates carry both.
    set(POM68K_ETALON_CORE
        system_boot_etalon        # 68000 compacts   — MacMemory
        macii_boot_etalon         # GLUE + NuBus     — MacIIMemory
        iifx_boot_etalon          # OSS + 2 IOPs     — IIfxMemory
        iisi_boot_etalon          # RBV              — RbvMemory
        lcii_boot_etalon          # V8               — V8Memory
        lc3_boot_etalon           # Sonora           — SonoraMemory
        iivx_boot_etalon          # VASP             — VaspMemory
        q605_boot_etalon          # MEMCjr+PrimeTime — Q605Memory
        centris650_boot_etalon    # djMEMC + IOSB    — CentrisMemory
        q700_boot_etalon          # discrete 040     — Q700Memory
        q630_boot_etalon          # F108 + Valkyrie  — Q630Memory
        duo230_boot_etalon)       # MSC + PG&E       — MscMemory
    #
    # Labels are DERIVED from test names rather than declared per gate, so a
    # gate registered tomorrow is classified the moment it is added.
    get_property(pom68k_tests DIRECTORY PROPERTY TESTS)
    foreach(t IN LISTS pom68k_tests)
        set(lbl "")
        if(t MATCHES "etalon$")
            list(APPEND lbl etalon)
            if(t IN_LIST POM68K_ETALON_CORE)
                list(APPEND lbl etalon-core)
            endif()
        else()
            list(APPEND lbl unit)
        endif()
        if(t MATCHES "^jit_")
            list(APPEND lbl jit)
        endif()
        # The 68040 family — the JIT's entire blast radius today.
        if(t MATCHES "q605|lc475|lc575|centris|quadra6|quadra8|q630|lc580|q700|q900|q950|turboscsi|swim2")
            list(APPEND lbl m040)
        endif()
        if(POM68K_PRODUCT_LLE_GATES AND
           (t MATCHES "^lle_a64_" OR
            t MATCHES "^(jit_|interp_)(q605|centris650|q630|q700)_boot_etalon$" OR
            t STREQUAL "jit_lockstep_a64_coarse_test" OR
            t STREQUAL "savestate_040_test"))
            list(APPEND lbl product lle a64-oracle)
        endif()
        # MERGE, never overwrite. A gate that set LABELS explicitly at its
        # registration did so because the derivation cannot see what it knows:
        # `quadra_event_scheduler_test` asked for m040 at :880 and silently lost
        # it here for two weeks, because this line used to assign — and the
        # m040 regex below matches `quadra6|quadra8`, not `quadra_`. Nothing
        # reported it: `ctest -L m040` simply ran one gate fewer than the docs
        # claimed. Its sibling `q605_event_scheduler_test` survived only by the
        # accident of re-deriving the same pair. (2026-08-12)
        get_test_property(${t} LABELS pom68k_prior_lbl)
        if(pom68k_prior_lbl)
            list(APPEND lbl ${pom68k_prior_lbl})
            list(REMOVE_DUPLICATES lbl)
        endif()
        set_tests_properties(${t} PROPERTIES LABELS "${lbl}")
    endforeach()

    # A name in POM68K_ETALON_CORE that is not a registered gate would drop a
    # PLATFORM out of the pre-commit tier silently — and a tier that quietly
    # covers eleven platforms while claiming twelve is worse than no tier.
    # Renaming a gate must therefore break the configure, not the coverage.
    foreach(core IN LISTS POM68K_ETALON_CORE)
        if(NOT core IN_LIST pom68k_tests)
            message(FATAL_ERROR
                "POM68K_ETALON_CORE lists '${core}', which is not a registered "
                "test. Update the list in CMakeLists.txt: it is the one place "
                "that names a representative profile per platform.")
        endif()
    endforeach()


    # The working loop. jit_lockstep_test is the real proof — it compares the
    # JIT against the interpreter register by register at every instruction
    # boundary — and the two q605 etalons show the machine still reaches the
    # Finder on the accelerated DEFAULT and the explicit interpreter oracle.
    # Anything a JIT/default-policy change can break shows up here.
    set_property(TEST jit_backend_test jit_lockstep_test jit_lockstep_blocks_test
                      jit_lockstep_x64_test jit_lockstep_x64_fine_test
                      jit_lockstep_noaccess_test
                      q605_boot_etalon interp_q605_boot_etalon
                 APPEND PROPERTY LABELS smoke)
    if(TEST jit_lockstep_a64_coarse_test)
        set_property(TEST jit_lockstep_a64_coarse_test APPEND PROPERTY LABELS smoke)
    endif()

    # Fast, asset-free native-JIT tier. Every registration here must fail,
    # never soft-skip, on the A64/x64 CI hosts: jit_backend_test requires the
    # compiled host generator, the copyback trio executes the IR-derived
    # single/pair proof protocols, and the two standalone checkers keep the
    # environment/document surfaces coherent. TIMEOUT is the deliberately
    # coarse CI performance budget; semantic budgets (zero fallback, native
    # cache-hit/fallback counters, bounded IR size) live inside the gates.
    set_property(TEST jit_backend_test
                      jit_asset_free_lockstep_test
                      jit_copyback_write_040_test
                      jit_copyback_bsr_040_test
                      jit_copyback_pair_040_test
                      docs_test config_test
                 APPEND PROPERTY LABELS jit-fast)
    set_tests_properties(jit_backend_test
                         jit_asset_free_lockstep_test
                         jit_copyback_write_040_test
                         jit_copyback_bsr_040_test
                         jit_copyback_pair_040_test
                         docs_test config_test
                         PROPERTIES TIMEOUT 45)

    # Gate contract manifest. `unit` predates the asset model and only means
    # "not named *_etalon"; it is intentionally retained for compatibility,
    # but CI and tooling now consume these orthogonal, exhaustive dimensions.
    # The short exception list names component gates whose useful path needs a
    # user-provided corpus, firmware or disk despite not being an etalon.
    set(POM68K_OPTIONAL_ASSET_GATES
        sst68000 sst68030 sst68040
        cuda_lle_test egret_lle_test
        scsi_disk_test ncr53c96_test scsi_pdma_test scsi_hfs_facade_test sound_test
        savestate_030_test savestate_68k_test)
    set(pom68k_gate_manifest
        "name\tassets\thost\tscope\ttier\n")
    foreach(t IN LISTS pom68k_tests)
        if(t MATCHES "etalon$" OR
           (t MATCHES "^jit_lockstep" AND
            NOT t STREQUAL "jit_asset_free_lockstep_test"))
            set(gate_assets required)
        elseif(t IN_LIST POM68K_OPTIONAL_ASSET_GATES)
            set(gate_assets optional)
        else()
            set(gate_assets none)
        endif()

        if(t MATCHES "(^|_)a64(_|$)" OR t MATCHES "^lle_a64_")
            set(gate_host a64)
        elseif(t MATCHES "(^|_)x64(_|$)")
            set(gate_host x64)
        elseif(t MATCHES "^jit_(asset_free|copyback)")
            set(gate_host native)
        else()
            set(gate_host any)
        endif()

        get_test_property(${t} LABELS gate_contract_labels)
        if(t MATCHES "etalon$")
            set(gate_scope profile)
        elseif(t STREQUAL "docs_test" OR t STREQUAL "config_test")
            set(gate_scope repository)
        elseif(t MATCHES "^jit_")
            set(gate_scope engine)
        else()
            set(gate_scope component)
        endif()
        if("jit-fast" IN_LIST gate_contract_labels OR gate_assets STREQUAL "none")
            set(gate_tier daily)
        elseif("etalon-core" IN_LIST gate_contract_labels)
            set(gate_tier platform)
        else()
            set(gate_tier full)
        endif()

        set_property(TEST ${t} APPEND PROPERTY
                     LABELS "asset-${gate_assets}" "host-${gate_host}"
                            "scope-${gate_scope}" "tier-${gate_tier}")
        set_property(TEST ${t} PROPERTY POM68K_ASSETS ${gate_assets})
        set_property(TEST ${t} PROPERTY POM68K_HOST ${gate_host})
        set_property(TEST ${t} PROPERTY POM68K_SCOPE ${gate_scope})
        set_property(TEST ${t} PROPERTY POM68K_TIER ${gate_tier})
        string(APPEND pom68k_gate_manifest
               "${t}\t${gate_assets}\t${gate_host}\t${gate_scope}\t${gate_tier}\n")
    endforeach()
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/pom68k_gate_manifest.tsv
         "${pom68k_gate_manifest}")

    # The registry, as a file `docs_test` can read: one line per gate,
    # "<name>\t<label>,<label>". CTest has no way to hand a test the roster,
    # and a documentation gate that cannot see the roster can only check the
    # docs against themselves.
    set(pom68k_gate_list "")
    foreach(t IN LISTS pom68k_tests)
        get_test_property(${t} LABELS tlabels)
        if(NOT tlabels)
            set(tlabels "")
        endif()
        string(REPLACE ";" "," tlabels "${tlabels}")
        string(APPEND pom68k_gate_list "${t}\t${tlabels}\n")
    endforeach()
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/pom68k_gates.tsv "${pom68k_gate_list}")
    # Empty on the AArch64 dev host, two rows anywhere else — see the guard
    # above. docs_test adds these back before comparing with the docs.
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/pom68k_gates_absent.tsv
         "${pom68k_absent_gates}")

    # `make jitdev` builds ONLY what `ctest -L smoke` needs. The tree-wide LTO
    # makes a full `make` relink ~90 binaries after any core change; this is
    # three. Pair them: `make -j4 jitdev && ctest -L smoke`.
    add_custom_target(jitdev DEPENDS
                      jit_backend_test jit_lockstep_test q605_boot_etalon)

    # Pair with `ctest -L jit-fast`: seven registrations, five small binaries,
    # no ROM/disk/SST corpus and no GUI dependency.
    add_custom_target(jitfast DEPENDS
                      jit_backend_test jit_asset_free_lockstep_test
                      jit_copyback_write_040_test
                      docs_test config_test)

endif()
