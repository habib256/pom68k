# POM68K Disques — the guest-side mount agent

The guest half of `docs/SCSI_HOTPLUG.md` § 3. A small Mac OS application
(System 7 – Mac OS 8.1 tested on the Quadra 605) that polls the emulator
twice a second through two vendor SCSI commands and mounts or unmounts a
bay's volume on request, so the emulator's Disques window can offer
« Monter / Démonter » for a disk attached with the machine running.

| File | Role |
|---|---|
| `main.c` | event loop, the poll (`$C0`) and the report (`$C1`), the status window |
| `mount.c` | mount: partition map → drive-queue entry → `PBMountVol`; unmount: `PBDTCloseDown` → `UnmountVol` |
| `drv.c` | the in-memory block driver (Prime over the SCSI Manager, Status, Control), one unit per mounted target |
| `glue.s` | SCSI Manager entries (`_SCSIDispatch`), `_DrvrInstall`/`_DrvrRemove`, `_HFSDispatch`, the driver trampolines |
| `scsiagent.r` | the `SIZE` resource (background-capable) |

## Build

```bash
cd dev/scsiagent && mkdir -p build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=../../Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake
make            # → POM68KDisques.APPL / .bin / .dsk
```

`POM68KDisques.dsk` is an 800 K floppy image: insert it from the Disques
window, open it and launch the application. `scsi_agent_etalon` does
exactly that on the 8.1 volume and is the contract of this directory.

## Protocol

`src/ScsiAgentMailbox.h` is the host side. A `$C0` POLL on any POM68K
target answers 64 bytes: `"POMA"`, version, kind (0 none / 1 mount /
2 unmount), SCSI id, sequence. The agent acts and answers with a `$C1`
REPORT: `"POMR"`, kind, id, sequence, OSErr, drive number, a Pascal string
(the volume name, or its own words). Every POLL is the heartbeat the
Disques window shows as « agent présent ».

## What was learned on the way (2026-09-14)

- Retro68's GCC pushes a prototyped `short` on two bytes: a glue that
  reads `6(sp)` for a first `short` argument reads the next word.
  `SCSICmd` got a random length, `DrvrInstall` a random unit (`badUnitErr`).
- The Mac OS 8.1 SCSI Manager 4.3 emulates the old API with a DMA
  « Select without ATN » (`$C1`) and feeds the CDB at DRQ-interrupt time;
  the emulator's pseudo-VIA2 had DRQ as a readable flag only. Fixed on
  the three boards that have it (`scsiDrq`).
- Finder 8.1 classes a volume by its driver's reference number. A driver
  in a free unit above 47 made its `UnmountVol` patch show « There is a
  problem with the disk »; the same driver at the target's own SCSI unit
  (`-(33 + id)`) unmounts silently, after `PBDTCloseDown` closes the
  desktop database the way Drive Setup does. An *ejectable* drive drew the
  same alert at mount time; the drive is registered fixed.
- A modal alert raised by the Finder's trap patch runs in the caller's
  time slice: the agent's main loop stops, which the etalon reads as
  « the request was never answered ».
