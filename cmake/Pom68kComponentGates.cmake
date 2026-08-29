# Component, repository and shared-subsystem CTest registrations.
# Included by the repository root while POM68K_TESTS is enabled.

# Behavioural GUI gate: a real hidden GLFW/ImGui window renders three
# frames, switches CPU engine, writes a save state, closes through RAII
# and reaches the relaunch boundary. The wrapper uses Xvfb on headless
# Linux and reports an explicit CTest SKIP when the optional GUI target is
# absent; a missing window system can therefore never look like a pass.
if(TARGET POM68K)
    set(POM68K_GUI_SMOKE_EXE "$<TARGET_FILE:POM68K>")
else()
    set(POM68K_GUI_SMOKE_EXE
        "${CMAKE_CURRENT_BINARY_DIR}/POM68K-gui-not-built")
endif()
add_test(NAME gui_smoke_test
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_gui_smoke.sh"
                 "${POM68K_GUI_SMOKE_EXE}"
                 "${CMAKE_CURRENT_BINARY_DIR}/gui_smoke_report.txt"
                 "${CMAKE_CURRENT_BINARY_DIR}/gui-smoke-missing.rom")
set_tests_properties(gui_smoke_test PROPERTIES
                     WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
                     SKIP_RETURN_CODE 77 TIMEOUT 60 RUN_SERIAL TRUE
                     LABELS "gui")

# M1 gate: Moira boots the demo ROM through the real machine path.
add_executable(cpu_smoke tests/cpu_smoke.cpp)
target_link_libraries(cpu_smoke PRIVATE pom68k_core)
add_test(NAME cpu_smoke COMMAND cpu_smoke)

# M3 gate + dev tool: headless run → PPM screenshot.
add_executable(demo_screenshot tests/demo_screenshot.cpp)
target_link_libraries(demo_screenshot PRIVATE pom68k_core)
add_test(NAME demo_screenshot COMMAND demo_screenshot --frames 30)

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
# volume for the flat-HFS façade, machfs round-trip). Soft-skips if machfs is
# missing. The interpreter is chosen by the wrapper at RUN time: binding it
# here with if(EXISTS .venv-tools/...) meant a tree configured before the venv
# existed kept calling the system python3, so the gate skipped and counted
# green until somebody happened to reconfigure (2026-08-27).
add_test(NAME dir2hfs_selftest
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_dir2hfs_selftest.sh"
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

# assets.lock is useful in a clean clone too: validate its role-aware
# schema and every present user-provided file. In particular, a declared
# reference disk must live below hdv/ref/. Private runners add --strict to
# require the complete qualified set.
# `python3` explicitly: this script hashes files and needs no third-party
# module, so it has no business following the dir2hfs venv. It used to share
# that gate's ${POM68K_TOOLS_PYTHON} variable, and removing the variable when
# dir2hfs moved to a wrapper left this COMMAND starting with a non-executable
# .py path — "Process not started ... permission denied", caught the same
# minute by the tier run (2026-08-27).
add_test(NAME asset_lock_test
         COMMAND python3 tools/verify_assets.py
                 --manifest assets.lock --root .
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

# The GUI ↔ machine-thread contract (src/MachineHost.h): queue ordering,
# the framebuffer double buffer, the engine-swap round trip, the pacing
# branches and the thread teardown. Before the GUI runtime split, this
# contract had six copies in the platform composition unit; it was lifted
# into a header on 2026-08-09. No ROM, no image.
add_executable(machinehost_test tests/machinehost_test.cpp)
target_link_libraries(machinehost_test PRIVATE pom68k_core)
add_test(NAME machinehost_test COMMAND machinehost_test)

# The input journal end to end (src/InputJournal.h + tests/InputReplay.h):
# format round-trip through a real file, then REPLAY DETERMINISM on a live
# synthetic-ROM LC II — restore one snapshot, replay one journal twice,
# byte-identical machines; replaying at all visibly changes the machine so
# the identity is not vacuous. The recording tap itself (applyCmds side,
# enum pairing, initial snapshot) is pinned in machinehost_test.
add_executable(input_journal_test tests/input_journal_test.cpp)
target_link_libraries(input_journal_test PRIVATE pom68k_core)
add_test(NAME input_journal_test COMMAND input_journal_test)

# The docs' citable invariants, checked against the code: compiled profile
# catalogue == what CLAUDE.md says, every gate CLAUDE.md
# names is registered, every registered gate carries a label, and the gate
# totals it quotes. Reads pom68k_gates.tsv, which the tier block below
# writes at configure time. Links nothing.
add_executable(docs_test tests/docs_test.cpp)
target_include_directories(docs_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(docs_test PRIVATE pom68k_app)
target_compile_definitions(docs_test PRIVATE
    POM68K_GATE_ROSTER="${CMAKE_CURRENT_BINARY_DIR}/pom68k_gates.tsv"
    POM68K_GATE_ROSTER_ABSENT="${CMAKE_CURRENT_BINARY_DIR}/pom68k_gates_absent.tsv"
    POM68K_GATE_MANIFEST="${CMAKE_CURRENT_BINARY_DIR}/pom68k_gate_manifest.tsv"
    POM68K_PERF_BUDGET_FILE="${CMAKE_CURRENT_SOURCE_DIR}/performance_budgets.tsv")
add_test(NAME docs_test COMMAND docs_test
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

# The env-knob surface against DEV.md § 5 and config_knobs.tsv. The exact
# registry classifies every name and checks product evidence against the
# configured gate roster. Links nothing; walks the source tree.
# The absent roster too: a product knob's evidence may be a host-conditional
# gate, and elsewhere that citation is not stale, only unregistered.
# POM68K_JIT_030_MEMBF cites the a64 alignment lockstep and turned this gate
# red on every x86-64 tree until this line existed (CHANGELOG 2026-08-28).
add_executable(config_test tests/config_test.cpp)
target_compile_definitions(config_test PRIVATE
    POM68K_GATE_ROSTER="${CMAKE_CURRENT_BINARY_DIR}/pom68k_gates.tsv"
    POM68K_GATE_ROSTER_ABSENT="${CMAKE_CURRENT_BINARY_DIR}/pom68k_gates_absent.tsv")
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
# The six CPU wrappers that coverage measured at 0.00 % in the asset-free tier
# (2026-08-27): only the machine etalons ran them, so a clone with no private
# ROM compiled them and never executed one. A synthetic ROM and 160k cycles is
# not a conformance check -- the etalons stay that -- but it means a wrapper
# that stops constructing, resetting, fetching or advancing its clock fails on
# every host instead of only on one with assets.
add_executable(cpu_wrapper_smoke tests/cpu_wrapper_smoke.cpp)
target_link_libraries(cpu_wrapper_smoke PRIVATE pom68k_core)
add_test(NAME cpu_wrapper_smoke COMMAND cpu_wrapper_smoke)

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


