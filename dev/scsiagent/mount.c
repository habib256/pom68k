/* mount.c — see mount.h. */
#include "mount.h"
#include "glue.h"

#include <Files.h>
#include <Errors.h>
#include <Memory.h>
#include <OSUtils.h>
#include <string.h>

#define kDrvQHdr ((QHdrPtr)0x0308)

static short driveOfRomDriver(short id)
{
    DrvQEl *d;
    short   want = (short)(-(33 + id));
    for (d = (DrvQEl *)kDrvQHdr->qHead; d; d = (DrvQEl *)d->qLink)
        if (d->dQRefNum == want) return d->dQDrive;
    return 0;
}

static short freeDriveNumber(void)
{
    DrvQEl *d;
    short   n = 8;
    for (d = (DrvQEl *)kDrvQHdr->qHead; d; d = (DrvQEl *)d->qLink)
        if (d->dQDrive >= n) n = (short)(d->dQDrive + 1);
    return n;
}

static AgentDrive *ourDrive(AgentDriverState *st, short id)
{
    short i;
    for (i = 0; i < kAgentMaxDrives; i++)
        if (st->drives[i].scsiId == id) return &st->drives[i];
    return 0;
}

/* The HFS partition of the target: Apple partition map first, else the
 * whole disk (READ CAPACITY). */
static OSErr findHfsPartition(short id, unsigned long *start, unsigned long *blocks)
{
    unsigned char *blk = (unsigned char *)NewPtr(512);
    unsigned char  cdb[10];
    OSErr          e;
    unsigned long  mapBlocks = 1, i;

    if (!blk) return memFullErr;
    memset(cdb, 0, sizeof cdb);
    cdb[0] = 0x28; cdb[8] = 1;                       /* READ(10) block 0 */
    e = agentScsiTransfer(id, cdb, 10, (Ptr)blk, 512, false, 0, 0);
    if (e != noErr) { DisposePtr((Ptr)blk); return e; }
    if (blk[0] == 'E' && blk[1] == 'R') {
        for (i = 1; i <= mapBlocks && i < 64; i++) {
            cdb[5] = (unsigned char)i;
            e = agentScsiTransfer(id, cdb, 10, (Ptr)blk, 512, false, 0, 0);
            if (e != noErr) break;
            if (blk[0] != 'P' || blk[1] != 'M') break;
            if (i == 1)
                mapBlocks = ((unsigned long)blk[4] << 24) | ((unsigned long)blk[5] << 16) |
                            ((unsigned long)blk[6] << 8) | blk[7];
            if (strncmp((const char *)blk + 48, "Apple_HFS", 9) == 0) {
                *start  = ((unsigned long)blk[8] << 24) | ((unsigned long)blk[9] << 16) |
                          ((unsigned long)blk[10] << 8) | blk[11];
                *blocks = ((unsigned long)blk[12] << 24) | ((unsigned long)blk[13] << 16) |
                          ((unsigned long)blk[14] << 8) | blk[15];
                DisposePtr((Ptr)blk);
                return noErr;
            }
        }
        DisposePtr((Ptr)blk);
        return e == noErr ? nsvErr : e;
    }
    /* No map: the whole target is the volume. */
    memset(cdb, 0, sizeof cdb);
    cdb[0] = 0x25;                                   /* READ CAPACITY */
    e = agentScsiTransfer(id, cdb, 10, (Ptr)blk, 8, false, 0, 0);
    if (e == noErr) {
        *start  = 0;
        *blocks = (((unsigned long)blk[0] << 24) | ((unsigned long)blk[1] << 16) |
                   ((unsigned long)blk[2] << 8) | blk[3]) + 1;
    }
    DisposePtr((Ptr)blk);
    return e;
}

static OSErr mountDrive(AgentDriverState *st, short drive, unsigned char *volName)
{
    ParamBlockRec  pb;
    HParamBlockRec vp;
    OSErr          e;

    memset(&pb, 0, sizeof pb);
    pb.ioParam.ioVRefNum = drive;
    e = PBMountVol(&pb);
    if (e != noErr) return e;
    memset(&vp, 0, sizeof vp);
    vp.volumeParam.ioNamePtr  = volName;
    vp.volumeParam.ioVRefNum  = pb.ioParam.ioVRefNum;
    vp.volumeParam.ioVolIndex = 0;
    if (PBHGetVInfoSync(&vp) != noErr) volName[0] = 0;
    return noErr;
}

OSErr agentMount(AgentDriverState *st, short id, short *driveOut,
                 unsigned char *volName)
{
    short         drive;
    AgentDrive   *d;
    unsigned long start = 0, blocks = 0;
    OSErr         e;
    short         i;

    volName[0] = 0;
    *driveOut = 0;
    drive = driveOfRomDriver(id);
    if (drive == 0 && (d = ourDrive(st, id)) != 0) {
        drive = d->driveNum;
        d->flags[1] = 8;                      /* disk back in the drive */
    }
    if (drive != 0) {
        *driveOut = drive;
        return mountDrive(st, drive, volName);
    }
    e = findHfsPartition(id, &start, &blocks);
    if (e != noErr) return e;
    for (i = 0; i < kAgentMaxDrives && st->drives[i].scsiId >= 0; i++) {}
    if (i == kAgentMaxDrives) return nsDrvErr;
    d = &st->drives[i];
    memset(d, 0, sizeof *d);
    e = agentDriverForId(st, id, &d->refNum);
    if (e != noErr) return e;
    d->scsiId     = id;
    d->driveNum   = freeDriveNumber();
    d->partStart  = start;
    d->partBlocks = blocks;
    /* Nonejectable disk in place (Inside Macintosh: Files, the four bytes
     * before a DrvQEl). Measured 2026-09-14: an EJECTABLE drive makes
     * Finder 8.1 raise « There is a problem with the disk » the moment the
     * volume appears; a fixed one mounts silently, and leaves silently
     * once its desktop database is closed first (agentUnmount). */
    d->flags[0] = 0; d->flags[1] = 8; d->flags[2] = 0; d->flags[3] = 0;
    d->q.qType    = 1;                               /* dQDrvSz/dQDrvSz2 valid */
    d->q.dQDrive  = d->driveNum;
    d->q.dQRefNum = d->refNum;
    d->q.dQFSID   = 0;
    d->q.dQDrvSz  = (unsigned short)(blocks & 0xFFFF);
    d->q.dQDrvSz2 = (unsigned short)(blocks >> 16);
    Enqueue((QElemPtr)&d->q, kDrvQHdr);
    *driveOut = d->driveNum;
    e = mountDrive(st, d->driveNum, volName);
    if (e != noErr) {
        Dequeue((QElemPtr)&d->q, kDrvQHdr);
        d->scsiId = -1;
    }
    return e;
}

static short vRefNumOnDrive(short drive)
{
    HParamBlockRec vp;
    short          idx;
    for (idx = 1; idx < 32; idx++) {
        memset(&vp, 0, sizeof vp);
        vp.volumeParam.ioVolIndex = idx;
        if (PBHGetVInfoSync(&vp) != noErr) break;
        if (vp.volumeParam.ioVDrvInfo == drive) return vp.volumeParam.ioVRefNum;
    }
    return 0;
}

OSErr agentUnmount(AgentDriverState *st, short id)
{
    AgentDrive *d = ourDrive(st, id);
    short       drive = d ? d->driveNum : driveOfRomDriver(id);
    short       vRef;
    OSErr       e;

    agentNote(st, 5, 1, drive);
    if (drive == 0) return nsDrvErr;
    vRef = vRefNumOnDrive(drive);
    agentNote(st, 5, 2, vRef);
    if (vRef == 0) return nsvErr;
    /* Drive Setup's sequence for a fixed disk: close the volume's desktop
     * database (PBDTCloseDown, _HFSDispatch $21 — the Finder holds it open
     * on every mounted volume), then UnmountVol. Without the first step
     * the Finder's UnmountVol patch shows « There is a problem with the
     * disk » before letting the unmount through (2026-09-14). */
    {
        /* DTPBRec: ioNamePtr +18, ioVRefNum +22, ioDTRefNum +24. GetPath
         * ($20) names the volume's database; CloseDown ($21) takes THAT
         * reference number — a vRefNum there is rfNumErr (-51). */
        unsigned char dt[128];
        OSErr g;
        memset(dt, 0, sizeof dt);
        *(short *)(dt + 22) = vRef;
        g = HFSDispatchGlue(dt, 0x20);
        agentNote(st, 5, 3, g);
        if (g == noErr) {
            const short dtRef = *(short *)(dt + 24);
            memset(dt, 0, sizeof dt);
            *(short *)(dt + 24) = dtRef;
            e = HFSDispatchGlue(dt, 0x21);
            agentNote(st, 5, 5, e);
        }
    }
    e = UnmountVol(0, vRef);
    agentNote(st, 5, 4, e);
    if (e != noErr) return e;
    return noErr;
}

void agentUnmountAll(AgentDriverState *st)
{
    short i;
    for (i = 0; i < kAgentMaxDrives; i++) {
        if (st->drives[i].scsiId < 0) continue;
        agentUnmount(st, st->drives[i].scsiId);
        Dequeue((QElemPtr)&st->drives[i].q, kDrvQHdr);
        st->drives[i].scsiId = -1;
    }
}
