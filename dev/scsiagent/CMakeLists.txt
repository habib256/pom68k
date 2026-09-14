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
