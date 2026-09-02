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
#                  binary has every reason to want it — so it defaults ON,
#                  not to POM68K_NATIVE. It followed NATIVE until 2026-08-17,
#                  which made this comment a lie in the one case it was
#                  written for: a distributable build sets NATIVE=OFF, and so
#                  got no LTO unless it asked twice. That is the coupling the
#                  2026-08-08 split was supposed to remove and only half did
#                  — every distributable script already carries an explicit
#                  `-DPOM68K_LTO=ON` for exactly this reason.
#                  It is expensive: LTO roughly doubles the link time of the
#                  ~130 test binaries. A build that does not want it must now
#                  say `-DPOM68K_LTO=OFF` and say why (see .github/workflows).
option(POM68K_NATIVE "Optimize for the build host CPU (native ISA + tuning)" ON)
set(POM68K_TUNE "" CACHE STRING
    "Tune for a named core without raising the ISA floor (e.g. cortex-a72)")
option(POM68K_LTO "Link-time optimization" ON)

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
    # ─── LTO, and the linker it has to survive ───────────────────────────
    # LTO is resolved to a REQUEST first, and only committed at the end of
    # this block, because the linker probe below has to carry the LTO flags:
    # the two features are independent, but the BUILD uses them at the same
    # time and that combination is a third thing that neither probe covered.
    # See the linker block for the failure it let through.
    set(pom68k_lto_wanted OFF)
    if(POM68K_LTO)
        include(CheckIPOSupported)
        check_ipo_supported(RESULT pom68k_ipo OUTPUT pom68k_ipo_why)
        if(pom68k_ipo)
            set(pom68k_lto_wanted ON)
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
    #
    # The probe carries the LTO flags because THE COMBINATION IS THE BUG.
    # GCC's IPO objects are GIMPLE, not machine code (`-fno-fat-lto-objects`
    # is what CMake's IPO adds), and a linker the driver does not hand the
    # LTO plugin to reads them as objects with no symbols in them. GCC 13 +
    # `ld.lld` + LTO is exactly that case, and it is not a corner: on a plain
    # x86-64 Linux host with lld installed, EVERY executable in this tree
    # failed to link with `undefined symbol: main`, at 4 % of the build.
    # Probing the two halves separately is what let it through, and each
    # half was individually right: `check_ipo_supported()` ran BEFORE
    # `-fuse-ld=` was added, so it answered for the default linker, and the
    # linker probe compiled a source with no LTO on it. Both said yes; the
    # build said no.
    # On conflict the fast linker loses, not LTO: LTO is a shipped
    # optimization that release binaries have already silently lost once
    # (`docs/RASPBERRY_PI.md`), a faster link is a working-loop convenience.
    option(POM68K_FAST_LINK "Use mold or lld when one is installed" ON)
    if(POM68K_FAST_LINK)
        include(CheckCXXSourceCompiles)
        if(pom68k_lto_wanted)
            set(pom68k_ld_probe_flags "-flto=auto -fno-fat-lto-objects")
        else()
            set(pom68k_ld_probe_flags "")
        endif()
        foreach(pom68k_ld mold lld)
            find_program(pom68k_ld_${pom68k_ld} ld.${pom68k_ld})
            if(pom68k_ld_${pom68k_ld})
                # Probed, not assumed: the driver has to know the flag (a GCC
                # older than 12 does not know -fuse-ld=mold at all), AND it
                # has to produce a linkable program under the LTO mode this
                # build will really use. The cache variable is named
                # `_usable`, not `_ok`, so a tree configured before this
                # check existed re-probes instead of trusting the old answer.
                set(CMAKE_REQUIRED_FLAGS "${pom68k_ld_probe_flags}")
                set(CMAKE_REQUIRED_LINK_OPTIONS "-fuse-ld=${pom68k_ld}")
                check_cxx_source_compiles("int main() { return 0; }"
                                          pom68k_ld_${pom68k_ld}_usable)
                unset(CMAKE_REQUIRED_FLAGS)
                unset(CMAKE_REQUIRED_LINK_OPTIONS)
                if(pom68k_ld_${pom68k_ld}_usable)
                    add_link_options(-fuse-ld=${pom68k_ld})
                    message(STATUS "POM68K: linking with ${pom68k_ld}")
                    break()
                elseif(pom68k_lto_wanted)
                    message(STATUS "POM68K: ld.${pom68k_ld} found but cannot "
                                   "link this compiler's LTO objects — keeping "
                                   "the default linker (LTO stays ON)")
                endif()
            endif()
        endforeach()
    endif()
    if(pom68k_lto_wanted)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
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

# ─── Optional coverage build ──────────────────────────────────────────────
# Third item of the 2026-08-26 review (TODO.md § 0·B): nothing in this tree
# measured which code the gates actually reach. The number worth having is NOT
# a global percentage -- it is the list of src/ files that the asset-free tier
# never executes at all, because that list IS the inventory of what only the
# private ROM/disk assets prove. `tools/coverage_report.sh` produces it.
#
# Toolchain-split on purpose: Clang wants instrumented profiles plus a coverage
# map and is read back with llvm-cov; GCC's --coverage drops .gcno/.gcda next
# to the objects and is read with gcovr. The CI leg is GCC, the dev host is
# Clang, and neither can read the other's data.
set(POM68K_COVERAGE OFF CACHE BOOL "Instrument for coverage (see tools/coverage_report.sh)")
if(POM68K_COVERAGE AND NOT MSVC)
    if(CMAKE_CXX_COMPILER_ID MATCHES "^(AppleClang|Clang)$")
        add_compile_options(-fprofile-instr-generate -fcoverage-mapping -g -O1)
        add_link_options(-fprofile-instr-generate)
    else()
        add_compile_options(--coverage -g -O1)
        add_link_options(--coverage)
    endif()
    message(STATUS "POM68K coverage: ON (${CMAKE_CXX_COMPILER_ID})")
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
# SYSTEM: our own TUs include Moira.h, and 756 of the 798 warnings the first
# -Wall -Wextra pass produced came from its inlines. The fork is edited
# deliberately, patch group by patch group (extern/moira/POM68K_VENDOR.md);
# rewriting upstream's unused-parameter style is not one of those edits.
target_include_directories(moira SYSTEM PUBLIC ${MOIRA_DIR})
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
# POM68K_JIT_BACKENDS: `auto` follows CMAKE_SYSTEM_PROCESSOR; `x64` / `a64`
# select a generator explicitly (notably for a macOS cross-architecture build),
# and `threaded` builds the portable-only control configuration.
set(POM68K_JIT_BACKENDS "auto" CACHE STRING
    "JIT backends to compile: auto | threaded | x64 | a64")
set_property(CACHE POM68K_JIT_BACKENDS PROPERTY STRINGS auto threaded x64 a64)
set(POM68K_JIT_SOURCES
    src/jit/JitEngine.cpp src/jit/JitEngineCensus.cpp src/jit/JitBlockCache.cpp src/jit/JitShiftVersions.cpp # layer 1
    src/jit/JitBackend.cpp       # layer 2 — backend registry and selection
    src/jit/JitCodeBuffer.cpp    # portable W^X memory for code generators
    src/jit/backends/JitBackendThreaded.cpp   # the portable floor
)
set(POM68K_JIT_DEFS "")
set(POM68K_JIT_NATIVE_BACKEND "")
if(NOT EMSCRIPTEN)
    # NOT on Windows via `auto`: the x64 generator emits and follows the
    # System V AMD64 ABI (args in RDI/RSI, RSI/RDI scratch, no shadow space),
    # and Win64 disagrees on all three — a property of the TARGET OS shared
    # by MSVC and MinGW, so the guard is WIN32, not MSVC. The first compiled
    # block would dereference an arbitrary pointer; nothing fails at build
    # time because the file contains no GNU-ism. Windows falls back to the
    # `threaded` floor, valid for every guest. An explicit `x64` request
    # still compiles (that is how a Win64 ABI port would iterate), but
    # `X64Backend::usable()` refuses it at runtime for the same reason.
    if(POM68K_JIT_BACKENDS STREQUAL "x64" OR
       (POM68K_JIT_BACKENDS STREQUAL "auto" AND NOT WIN32 AND
        CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$"))
        list(APPEND POM68K_JIT_SOURCES src/jit/backends/JitBackendX64.cpp)
        list(APPEND POM68K_JIT_DEFS POM68K_JIT_BACKEND_X64)
        set(POM68K_JIT_HOST "x86-64 code generator")
        set(POM68K_JIT_NATIVE_BACKEND "x64")
    elseif(POM68K_JIT_BACKENDS STREQUAL "a64" OR
           (POM68K_JIT_BACKENDS STREQUAL "auto" AND
            CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$"))
        list(APPEND POM68K_JIT_SOURCES src/jit/backends/JitBackendA64.cpp)
        list(APPEND POM68K_JIT_DEFS POM68K_JIT_BACKEND_A64)
        set(POM68K_JIT_HOST "aarch64 code generator")
        set(POM68K_JIT_NATIVE_BACKEND "a64")
    elseif(POM68K_JIT_BACKENDS STREQUAL "auto" OR
           POM68K_JIT_BACKENDS STREQUAL "threaded")
        set(POM68K_JIT_HOST "${CMAKE_SYSTEM_PROCESSOR} (portable only)")
    else()
        message(FATAL_ERROR
            "Unknown POM68K_JIT_BACKENDS='${POM68K_JIT_BACKENDS}'; "
            "expected auto, threaded, x64 or a64")
    endif()
else()
    if(NOT POM68K_JIT_BACKENDS STREQUAL "auto" AND
       NOT POM68K_JIT_BACKENDS STREQUAL "threaded")
        message(FATAL_ERROR "Emscripten supports only the threaded JIT backend")
    endif()
    set(POM68K_JIT_HOST "portable only")
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

# ─── Application architecture (headless composition) ────────────────────
# ProcessEnvironment captures process policy; typed decoders and RuntimeConfig
# compose it; MachineFactory/MachineSession own profile routing and runtime.
# This headless layer is exercised by docs_test without GLFW/ImGui.
add_library(pom68k_app STATIC
    src/ProcessEnvironment.cpp
    src/RuntimeConfig.cpp src/RuntimeConfigProduct.cpp
    src/RuntimeConfigCore.cpp src/RuntimeConfigMachine.cpp
    src/MachineFactory.cpp src/MachineSession.cpp)
target_include_directories(pom68k_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

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
    add_executable(${PROJECT_NAME}
        src/main.cpp
        src/GuiMachineRuntime.cpp
        src/GuiHostServices.cpp
        src/GuiShell.cpp
        src/GuiSmokeScenario.cpp
        src/PlatformComposers.cpp
        src/PlatformCompact.cpp
        src/PlatformToby.cpp
        src/PlatformV8.cpp
        src/PlatformSonora.cpp
        src/PlatformDafb.cpp
        src/PlatformDuo.cpp
        src/miniaudio_impl.cpp
        src/FloppySound.cpp src/DiskBays.cpp src/PeripheralWindow.cpp
        src/DockLayout.cpp ${IMGUI_SOURCES})
    target_include_directories(${PROJECT_NAME} PRIVATE ${IMGUI_DIR} ${IMGUI_DIR}/backends)
    target_link_libraries(${PROJECT_NAME} PRIVATE pom68k_core pom68k_app)
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

    # Test modules execute in this directory scope. Their order is contractual:
    # machine targets precede JIT oracle variants; policy runs after every gate.
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/tests
                        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    include(cmake/Pom68kComponentGates.cmake)
    include(cmake/Pom68kMachineGates.cmake)
    include(cmake/Pom68kJitGates.cmake)
    include(cmake/Pom68kDevTools.cmake)
    include(cmake/Pom68kGatePolicy.cmake)
endif()

# ─── Warning policy — POM68K's own targets, and nothing else ──────────────
# Until 2026-08-27 this tree compiled with NO -Wall/-Wextra anywhere: the only
# -W* flag in it was the -Wno-unused-* silencing Moira. That is a strange gap
# in a codebase full of raw memory, generated code, threads and serialization,
# and it was the first item of the 2026-08-26 review (TODO.md § 0·B).
#
# The flags go on POM68K's targets only. `extern/` noise is not ours to fix,
# and Dear ImGui's sources compile INSIDE our GUI target, so they are silenced
# per source file rather than by dropping the policy for the whole target.
#
# POM68K_WERROR is CI's switch, not a developer's: a wall of warnings in a
# local build stops the work, the same wall in CI closes the door. It stays
# OFF here until a GCC 13 run has been read — this host has clang only, and
# the two compilers do not warn about the same things.
option(POM68K_WARNINGS "Compile POM68K's own targets with -Wall -Wextra" ON)
option(POM68K_WERROR   "Treat those warnings as errors (CI only)" OFF)
if(POM68K_WARNINGS AND NOT MSVC)
    set(pom68k_warning_flags -Wall -Wextra)
    if(POM68K_WERROR)
        list(APPEND pom68k_warning_flags -Werror)
    endif()
    # Every target defined in THIS directory scope is ours: the five cmake/
    # modules are include()d, not add_subdirectory()d, so their gates are here
    # too. softfloat lives in its own scope and never sees these flags; moira
    # is ours by name and excluded on purpose (see its SYSTEM include above).
    get_property(pom68k_targets DIRECTORY . PROPERTY BUILDSYSTEM_TARGETS)
    foreach(pom68k_target IN LISTS pom68k_targets)
        get_target_property(pom68k_target_type ${pom68k_target} TYPE)
        if(pom68k_target STREQUAL "moira"
           OR pom68k_target_type STREQUAL "INTERFACE_LIBRARY"
           OR pom68k_target_type STREQUAL "UTILITY")
            continue()
        endif()
        target_compile_options(${pom68k_target} PRIVATE ${pom68k_warning_flags})
    endforeach()
    if(IMGUI_SOURCES)
        set_source_files_properties(${IMGUI_SOURCES}
                                    PROPERTIES COMPILE_OPTIONS -w)
    endif()
    message(STATUS "POM68K warnings: -Wall -Wextra (-Werror: ${POM68K_WERROR})")
endif()
