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

# ─── Optional sanitizer build (POM2/POMIIGS convention) ───────────────────
set(POM68K_SANITIZE "" CACHE STRING "Build with -fsanitize=<value>. Empty = off.")
if(POM68K_SANITIZE AND NOT MSVC)
    add_compile_options(-fsanitize=${POM68K_SANITIZE} -fno-omit-frame-pointer -g)
    add_link_options   (-fsanitize=${POM68K_SANITIZE})
endif()

# ─── Moira (vendored from NeoST — see extern/moira/POM68K_VENDOR.md) ──────
set(MOIRA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/extern/moira/Moira)
if(NOT EXISTS "${MOIRA_DIR}/Moira.cpp")
    message(FATAL_ERROR "Moira not found in ${MOIRA_DIR} — restore extern/moira from git.")
endif()
add_library(moira STATIC ${MOIRA_DIR}/Moira.cpp ${MOIRA_DIR}/MoiraDebugger.cpp)
target_include_directories(moira PUBLIC ${MOIRA_DIR})
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
    add_executable(${PROJECT_NAME} src/main.cpp ${IMGUI_SOURCES})
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
    add_executable(boot_trace tests/boot_trace.cpp)
    target_link_libraries(boot_trace PRIVATE pom68k_core)

    # Dev tool: disassemble ROM ranges (Moira dasm).
    add_executable(dasm tests/dasm.cpp)
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

    # M5.5 gate: keyboard/mouse against the System 6 drivers (soft-skips).
    add_executable(input_etalon tests/input_etalon.cpp)
    target_link_libraries(input_etalon PRIVATE pom68k_core)
    add_test(NAME input_etalon COMMAND input_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # M4.5 gate: SingleStepTests/680x0 68000 vectors against bare Moira.
    # Data is user-fetched (~1 GB): tests/fetch_sst_68000.sh <dir>.
    # Soft-skips when absent.
    add_executable(sst68000 tests/sst68000.cpp)
    target_link_libraries(sst68000 PRIVATE moira)
    set(POM68K_SST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/sst68000"
        CACHE PATH "Directory of SingleStepTests/680x0 68000 .json vectors")
    add_test(NAME sst68000 COMMAND sst68000 "${POM68K_SST_DIR}")
endif()

# Dev tool (not a gate): instruction-level Sony-driver trace.
if(POM68K_TESTS AND NOT EMSCRIPTEN)
    add_executable(sony_trace tests/sony_trace.cpp)
    target_link_libraries(sony_trace PRIVATE pom68k_core)
endif()
