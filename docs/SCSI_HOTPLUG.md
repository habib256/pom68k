# SCSI hot-plug: re-reading the bus, and mounting from inside Mac OS

*Research note, opened 2026-09-13. Status: all three steps built and gated (§ 5, § 6); the host-side detach is the remaining piece.*

## 1. The problem the Disques window cannot solve alone

The window (`src/DiskBays.*`) stages every change to a bay that was empty
at boot and applies it with a relaunch, because Classic Mac OS enumerates
the SCSI bus **once**, from the ROM's boot code and the SCSI Manager's
driver install. A fixed disk that appears afterwards has no driver, no
drive-queue entry and no volume. Only a *removable* target already probed
at boot can change medium live — the CD row works because `ScsiDisk`
answers CHECK CONDITION / UNIT ATTENTION `$28` on the next TEST UNIT READY
and the CD driver polls (`DiskBays.h`, hot-swap contract).

Two consequences the user sees:

- adding a hard disk means « Appliquer et redémarrer », even on a machine
  whose session was worth keeping;
- the window shows what the **host** attached, not what the **guest**
  mounted. « Le Finder ne le voit qu'après … » is a guess printed as text.
  A volume the user dragged to the Corbeille is still listed as present.

The wish is therefore two mechanisms: **read the SCSI tree as the guest
sees it**, and **mount / unmount from inside Mac OS** on request.

## 2. What is already in the tree

| Building block | Where | State |
|---|---|---|
| Controller target table, rebound by `attach(target, id)` | `Ncr5380.h`, `Ncr53c96.h` | live; called at boot only |
| UNIT ATTENTION on medium change | `ScsiDisk.cpp` | proven on the CD bay |
| Command queue GUI → machine thread (`InsertBay`, `EjectBay`) | `MachineHost.h` | live |
| Guest-RAM reads on the machine thread (low-memory globals) | every etalon under `tests/` | proven technique, not exposed to the GUI |
| Guest-side drive-queue and volume walk (`LMGetDrvQHdr`, `PBHGetVInfoSync`) | `dev/prober/devs.c` | working Retro68 code |
| Vendor INQUIRY identity (`POM68K_SCSI_INQUIRY=pom68k`) | `ScsiDisk.cpp` | live; how a guest tool can recognise our targets |

## 3. Options weighed

**A. Pretend every disk is removable.** Set RMB and raise UNIT ATTENTION
for fixed disks too. Rejected: Apple's HD SC driver never polls a fixed
target, so nothing mounts; and it lies about the hardware.

**B. Rely on a period utility (SCSIProbe).** Its « Mount » walks the bus,
reads the driver partition and installs it. Works today *if* the target
exists on the bus, which is the one thing the host refuses after boot.
Kept as a manual fallback and as the reference for what a guest agent
must do; not a product answer, because the UI still cannot see the result.

**C. Host-side introspection + live attach + a guest agent.** Chosen.
Three steps, each useful on its own:

1. **Bus view read from guest RAM.** On the machine thread, once per
   published frame, walk the drive queue (`DrvQHdr` `$308`) and the VCB
   queue (`VCBQHdr` `$356`). A drive-queue element's `dQRefNum` names the
   SCSI Manager driver, whose reference number is `−(33 + ID)` for SCSI
   ID 0–6; a VCB whose `vcbDrvNum` matches gives the mounted volume's
   name. Publish `{id, driveInstalled, mounted, volumeName}` per bay in
   the status snapshot the GUI already reads. Read-only, no guest code,
   and it turns the window's guesses into facts: « monté : Q700 »,
   « lecteur installé, aucun volume », « inconnu de l'invité ».
   Etalons already peek low memory the same way, so the technique is
   proven; the only new rule is *read after the frame, never mid-instruction*.
2. **Live attach of a fixed disk.** A queue command that opens the image
   and calls `attach()` on the controller between frames. The target
   answers INQUIRY from then on. Nothing mounts yet — that is the
   guest's job — but the relaunch disappears from the path, and SCSIProbe
   or step 3 can finish it.
3. **Guest agent (Retro68, `dev/`).** A faceless background application
   or INIT that (a) recognises POM68K targets by the vendor INQUIRY string,
   (b) on « mount » reads the Apple partition map, loads the driver
   partition, installs it and calls `_MountVol` — the SCSIProbe sequence —
   and (c) on « unmount » calls `_UnmountVol` / `_Eject` after flushing.
   Requests reach it through a **vendor-specific SCSI command on the
   target itself** (the bus is the one channel every board has; a mapped
   mailbox would need twelve memory maps). The host queues the request on
   the target; the agent polls with a Time Manager task and answers with
   the result. Unmount from the Finder needs no agent at all: step 1 sees
   the VCB vanish and the host may then detach safely.

Step 1 is the prerequisite for the other two and pays for itself alone.
Step 2 is small. Step 3 is the real work and is guest software — it stays
optional, and a machine without the agent keeps today's staged behaviour
with a truthful bus view.

## 4. Rules that carry over

- Machine time is guest CPU time: the bus view is sampled on the machine
  thread at frame boundaries, never from the GUI thread.
- The interpreter is the oracle: `attach()` after boot must leave the
  JIT's code window untouched (SCSI space is I/O, already refused).
- Reference media stay immutable: live attach follows the `hdv/work/`
  clone rule the window already applies.
- Every durable milestone has a gate: step 1 gets a machine-etalon that
  boots, reads the bus view and asserts the boot volume's name and ID;
  step 3 gets the same etalon with a second disk attached live.

## 5. What exists (2026-09-13, evening)

| Piece | Where | Gate |
|---|---|---|
| Queue walk: DrvQHdr → refNum −(33+ID) → bay; VCBQHdr → mounted name | `src/GuestScsiView.h` | `guest_scsi_view_test` (synthetic image, asset-none) |
| Sampling on the machine thread, ~4 Hz, logical addresses through `Mmu030Peek.h` on the 030 boards | `MachineHost::sampleGuestScsiView` | — |
| `Cmd::AttachDisk` — a fixed disk joins the bus between two quanta; outcome as text | `MachineHost::requestAttachDisk`, `bayMessage` | `scsi_hotplug_etalon` |
| Window: « invité : monté « X » / lecteur installé / aucun lecteur / inconnu » under SCSI 0 and every fixed bay; an empty bay's pick and « Créer » attach live | `src/DiskBays.cpp`, `bindScsiBays` (`GuiFloppyBays.h`) | — (the GUI has no gate) |

`scsi_hotplug_etalon` is the contract in one run on the Quadra 605: the
guest names its boot volume on SCSI 0; a blank HFS disk attached live on
SCSI 2 answers the bus but gets no driver and no volume for 15 s of guest
time; a power cycle mounts it as « Branche ». That is exactly the sentence
the window prints, and the reason step 3 exists.

`Mmu030Peek.h` moved from `tests/` to `src/` for the sampler; the walk is
unchanged. On the 68040 boards the sampler reads physically, the way the
etalons do.

## 6. Step 3 as built (2026-09-14): `dev/scsiagent`, « POM68K Disques »

The design of § 3 held, with four corrections the guest taught:

| Piece | Where | Gate |
|---|---|---|
| Mailbox: `$C0` POLL / `$C1` REPORT, answered by the controller for any selected POM68K target; one pending request, a sequence number, a heartbeat | `src/ScsiAgentMailbox.h`, `Ncr5380`/`Ncr53c96` `dispatch()` | `scsi_agent_mailbox_test` |
| `Cmd::AgentMount` / `Cmd::AgentUnmount`, presence = a poll within ~2 s, the report's drive number fed to the bus view | `MachineHost` | — |
| Window: « Monter / Démonter » under a fixed bay while the agent polls; « Agent invité : présent / absent » and its last words | `src/DiskBays.cpp` | — |
| The agent: polls twice a second, mounts through its own block driver installed at the target's SCSI unit, unmounts through `PBDTCloseDown` + `UnmountVol` | `dev/scsiagent/` (Retro68) | `scsi_agent_etalon` |

- **The old SCSI Manager API needs a DRQ interrupt.** Mac OS 8.1's SCSI
  Manager 4.3 emulates `SCSIGet`/`SCSISelect`/`SCSICmd` with a DMA « Select
  without ATN » and feeds the CDB when the chip's DRQ interrupts through the
  pseudo-VIA2. The emulator only reflected DRQ on IFR reads; `scsiDrq()`
  now raises it on the Q605, Centris and Q630 boards. Every old-API client
  (SCSIProbe included) was hanging in the XPT before that.
- **The driver goes in the target's own unit.** Finder 8.1 classes a volume
  by its driver's reference number: with the agent's driver in a free unit
  above 47, the Finder's `UnmountVol` patch showed « There is a problem with
  the disk »; at `−(33 + id)` — the slot the ROM would have used — the same
  unmount is silent, and `GuestScsiView` maps the drive without a hint.
- **Close the desktop database first.** `PBDTCloseDown` before `UnmountVol`
  is Drive Setup's sequence; the Finder keeps a desktop database open on
  every mounted volume.
- **A fixed drive, not an ejectable one.** Registered ejectable, the drive
  drew the same Finder alert at mount time. The Finder's own « put away »
  Apple event, tried in between, refuses fixed disks (`errAEEventFailed`).

What the window cannot do yet: detach the target from the bus once the
guest has let go of it — the memory maps have `attachScsi` and no detach.
The bay keeps its image; « Retirer » still stages a relaunch.
