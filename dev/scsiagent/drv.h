/* drv.h — the in-memory block-device driver of « POM68K Disques » and the
 * SCSI transfer helper it shares with mount.c.
 *
 * The driver serves a volume the ROM never saw (a disk attached to the bus
 * after the boot): Prime reads and writes its partition through the SCSI
 * Manager, Status answers the Finder's drive-status query, Control accepts
 * Eject. It is installed once in a free unit-table entry (DrvrInstall) and
 * lives in a NewPtrClear block: a 28-byte DRVR header followed by five
 * JMP.L trampolines into glue.s.
 *
 * RULE: nothing below drv_* touches a global, a static or a string
 * literal. The File Manager calls Prime from ANY process — the Finder's,
 * for one — under that process's A5, and Retro68 addresses an
 * application's globals through A5. Every byte of state hangs off the
 * DCE's dCtlStorage instead. */
#pragma once
#include <MacTypes.h>
#include <Devices.h>
#include <Files.h>

#define kAgentMaxDrives 7

typedef struct {
    short          scsiId;        /* -1 = free */
    short          refNum;        /* the driver unit serving it: -(33 + scsiId) */
    short          driveNum;
    unsigned long  partStart;     /* first block of the HFS partition */
    unsigned long  partBlocks;
    unsigned char  flags[4];      /* the four bytes that PRECEDE a DrvQEl */
    DrvQEl         q;             /* must follow flags immediately */
} AgentDrive;

typedef struct {
    char       sig[4];            /* "POMD": findable from the host (etalon) */
    short      refNum;            /* unused since the per-ID units; kept for the host's reader */
    short      openErr;           /* last install's OpenDriver verdict */
    unsigned char *block;         /* the DRVR header + trampolines, shared by every unit */
    short      installed[7];      /* refNum installed for SCSI id, 0 = none */
    /* Ring of the last calls, in heap memory so the driver may write it
     * from any process and the host may read it (the etalon prints it on
     * failure): kind 1 Prime read / 2 Prime write / 3 Control / 4 Status /
     * 5 unmount step; code = csCode, block count or step; err = result;
     * stat/msg = the SCSI status and message of a Prime. */
    unsigned long heartbeat;      /* main-loop turns (main.c) */
    short      ringPos;
    struct { short kind, code, err, stat, msg; unsigned long lba; } ring[8];
    AgentDrive drives[kAgentMaxDrives];
} AgentDriverState;

/* Driver routines, entered from glue.s (A0 = pb, A1 = DCE). */
short drv_open(ParmBlkPtr pb, DCtlPtr dce);
short drv_close(ParmBlkPtr pb, DCtlPtr dce);
short drv_prime(ParmBlkPtr pb, DCtlPtr dce);
short drv_control(ParmBlkPtr pb, DCtlPtr dce);
short drv_status(ParmBlkPtr pb, DCtlPtr dce);

/* One SCSI transaction: Get, Select, Cmd, one polled Read/Write of `bytes`
 * (0 = none), Complete. ioErr when the target answers anything but GOOD.
 * Global-free: usable from the driver. */
OSErr agentScsiTransfer(short id, const unsigned char *cdb, short cdbLen,
                        Ptr buf, unsigned long bytes, Boolean write,
                        short *statOut, short *msgOut);

/* Application context: build the DRVR block and the state. No unit is
 * taken yet: agentDriverForId installs the block at the SCSI Manager's
 * own unit for a target, 32 + id (refNum -(33 + id)) — the slot the ROM
 * would have used had the disk been there at boot. Finder 8.1 classes a
 * volume by that number: a driver elsewhere made its UnmountVol patch
 * show « There is a problem with the disk » (2026-09-14), and the host's
 * bus view maps drives to bays by the same rule (src/GuestScsiView.h). */
OSErr agentDriverInstall(AgentDriverState **stateOut);
OSErr agentDriverForId(AgentDriverState *st, short id, short *refNumOut);
/* Remove every unit — after every drive has been unmounted. */
void  agentDriverRemove(AgentDriverState *state);

/* Ring note from application code (mount.c): kind 5 = unmount step. */
void  agentNote(AgentDriverState *st, short kind, short code, short err);
