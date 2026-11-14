cmake_minimum_required(VERSION 3.5)
project(POM68KDisques C ASM)

# « POM68K Disques » — the guest-side agent of docs/SCSI_HOTPLUG.md § 3:
# polls the emulator through two vendor SCSI commands and mounts/unmounts
# volumes on request with the File Manager. glue.s carries the SCSI Manager
# traps, DrvrInstall/DrvrRemove and the driver entry trampolines.
add_application(POM68KDisques
    main.c
    mount.c
    drv.c
    glue.s
    scsiagent.r
)

# The toolchain identity beside the output (share/README.md): the same
# sources give different code under another Retro68 or GCC, so
# agent_binary_test compares the build with share/ byte for byte only when
# this stamp equals share/POM68KDisques.toolchain. The Retro68 commit is read
# from its source tree, two levels above the installed toolchain (dev/README.md).
set(POM68K_RETRO68_COMMIT "unknown")
find_package(Git QUIET)
if(GIT_FOUND AND DEFINED RETRO68_ROOT AND EXISTS "${RETRO68_ROOT}/../../Retro68/.git")
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${RETRO68_ROOT}/../../Retro68"
                            rev-parse --short=7 HEAD
                    OUTPUT_VARIABLE POM68K_RETRO68_COMMIT
                    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()
file(WRITE "${CMAKE_BINARY_DIR}/POM68KDisques.toolchain"
     "Retro68 ${POM68K_RETRO68_COMMIT} GCC ${CMAKE_C_COMPILER_VERSION}\n")
