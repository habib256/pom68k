# Whole-machine, profile and beyond-boot CTest registrations.
# All machine targets exist before the JIT oracle variants are declared.

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
# `launch` (2026-08-27) is the third leg TODO section 2 asked for: create a
# folder, then OPEN it -- the Finder reading its own new catalog entry back
# and putting a window on screen. Keyboard-only, so unlike the LC II's
# mouse-steered launch it is not calibrated to one volume's icon layout.
foreach(scenario soak persist launch)
    add_test(NAME q605_${scenario}_etalon COMMAND q605_beyond_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(q605_${scenario}_etalon PROPERTIES
                         ENVIRONMENT "POM68K_BEYOND=${scenario}"
                         TIMEOUT 1800)
endforeach()

# The section 1 report "System 7.5.5 refuses a hot-inserted GCR floppy on
# SWIM2", run as the experiment it always needed (2026-08-27): Quadra 605,
# the 7.5.5 volume the report names rather than the gates' 8.1 one, and an
# 800K GCR image inserted MID-RUN. It mounts. What the report saw was the
# modal alert this volume opens at every boot -- a machine sitting in a
# dialog polls nothing -- so the gate dismisses the alert first, then proves
# the mount from BOTH sides: the guest stepped the head off track 0, and the
# desktop gained an icon. Either half alone lies.
add_executable(q605_hotfloppy_etalon tests/q605_hotfloppy_probe.cpp)
target_link_libraries(q605_hotfloppy_etalon PRIVATE pom68k_core)
add_test(NAME q605_hotfloppy_etalon COMMAND q605_hotfloppy_etalon
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(q605_hotfloppy_etalon PROPERTIES TIMEOUT 1800)

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
# The sibling's front end is materially different past boot: PIC1654S
# ADB modem + discrete RTC at 25 MHz instead of Egret at 20 MHz. The soak
# reuses the PMMU-safe Time probe and keeps that path alive for 3 minutes.
add_test(NAME iici_soak_etalon COMMAND rbv_beyond_etalon iici
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(iici_soak_etalon PROPERTIES
                     ENVIRONMENT "POM68K_BEYOND=soak"
                     TIMEOUT 1800)

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

# The LIVE AppleShare exchange (2026-08-28, TODO § 6's named missing gate,
# ordered by the user): a real 8.1 guest drives the Chooser, mounts the
# in-process AFP share and creates a folder — the pass criterion is the
# directory appearing in the HOST filesystem. The whole wire, no protocol
# shortcut. Mouse-calibrated to the pinned MacOS-8.1 image; soft-skips
# without the assets.
add_executable(q605_afp_live_etalon tests/q605_afp_live_etalon.cpp)
target_link_libraries(q605_afp_live_etalon PRIVATE pom68k_core)
add_test(NAME q605_afp_live_etalon COMMAND q605_afp_live_etalon
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(q605_afp_live_etalon PROPERTIES TIMEOUT 1800)

# The AIO pair (2026-08-28, TODO § 2's named next beyond-boot target):
# the LC 520 — the Sonora roster's OTHER half, which the LC III legs never
# touch: the $EDE66CBD universal ROM, a Cuda transport instead of the
# Egret, and the built-in 640×480 8-bpp color display, so the signature is
# lc520_boot_etalon's luminance one. Soak is what would notice the Cuda
# MCU drifting three minutes in; persist drives the Toolbox through it.
add_executable(aio_beyond_etalon tests/aio_beyond_etalon.cpp)
target_link_libraries(aio_beyond_etalon PRIVATE pom68k_core)
foreach(scenario soak persist)
    add_test(NAME lc520_${scenario}_etalon COMMAND aio_beyond_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(lc520_${scenario}_etalon PROPERTIES
                         ENVIRONMENT "POM68K_BEYOND=${scenario}"
                         TIMEOUT 1800)
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

