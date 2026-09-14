/* drv.c — see drv.h. */
#include "drv.h"
#include "glue.h"

#include <Memory.h>
#include <Errors.h>

#define kBlock 512UL

OSErr agentScsiTransfer(short id, const unsigned char *cdb, short cdbLen,
                        Ptr buf, unsigned long bytes, Boolean write,
                        short *statOut, short *msgOut)
{
    SCSIInstr tib[2];
    short     stat = 0, msg = 0;
    OSErr     e, c;

    if (statOut) *statOut = -1;
    if (msgOut)  *msgOut = -1;

    e = SCSIGet();
    if (e != noErr) return e;
    e = SCSISelect(id);
    if (e != noErr) return e;
    e = SCSICmd((Ptr)cdb, cdbLen);
    if (e == noErr && bytes != 0) {
        tib[0].scOpcode = scInc;  tib[0].scParam1 = (long)buf; tib[0].scParam2 = (long)bytes;
        tib[1].scOpcode = scStop; tib[1].scParam1 = 0;         tib[1].scParam2 = 0;
        e = write ? SCSIWrite((Ptr)tib) : SCSIRead((Ptr)tib);
    }
    c = SCSIComplete(&stat, &msg, 600);
    if (statOut) *statOut = stat;
    if (msgOut)  *msgOut = msg;
    if (e != noErr) return e;
    if (c != noErr) return c;
    return stat == 0 ? noErr : ioErr;
}

/* ── Driver routines: no globals, no literals (drv.h) ─────────────────── */

static void drvNote(AgentDriverState *st, short kind, short code, short err,
                    short stat, short msg, unsigned long lba);

void agentNote(AgentDriverState *st, short kind, short code, short err)
{
    drvNote(st, kind, code, err, 0, 0, 0);
}

static void drvNote(AgentDriverState *st, short kind, short code, short err,
                    short stat, short msg, unsigned long lba)
{
    short i = (short)(st->ringPos & 7);
    st->ring[i].kind = kind; st->ring[i].code = code; st->ring[i].err = err;
    st->ring[i].stat = stat; st->ring[i].msg = msg;  st->ring[i].lba = lba;
    st->ringPos++;
}

static AgentDrive *driveFor(AgentDriverState *st, short driveNum)
{
    short i;
    for (i = 0; i < kAgentMaxDrives; i++)
        if (st->drives[i].scsiId >= 0 && st->drives[i].driveNum == driveNum)
            return &st->drives[i];
    return 0;
}

short drv_open(ParmBlkPtr pb, DCtlPtr dce)
{
    (void)pb; (void)dce;
    return noErr;
}

short drv_close(ParmBlkPtr pb, DCtlPtr dce)
{
    (void)pb; (void)dce;
    return noErr;
}

short drv_prime(ParmBlkPtr pb, DCtlPtr dce)
{
    AgentDriverState *st = (AgentDriverState *)dce->dCtlStorage;
    AgentDrive       *d;
    unsigned long     lba, left, done = 0;
    unsigned char     cdb[10];
    Boolean           write;
    OSErr             e = noErr;

    pb->ioParam.ioActCount = 0;
    if (!st) return nsDrvErr;
    d = driveFor(st, pb->ioParam.ioVRefNum);
    if (!d) return nsDrvErr;
    if ((pb->ioParam.ioReqCount & (kBlock - 1)) ||
        (pb->ioParam.ioPosOffset & (kBlock - 1)))
        return paramErr;
    write = (pb->ioParam.ioTrap & 0xFF) == aWrCmd;
    lba  = d->partStart + ((unsigned long)pb->ioParam.ioPosOffset >> 9);
    left = (unsigned long)pb->ioParam.ioReqCount >> 9;
    if (lba + left > d->partStart + d->partBlocks) return paramErr;

    while (left != 0 && e == noErr) {
        unsigned long n = left > 128 ? 128 : left;      /* 64 K per command */
        cdb[0] = write ? 0x2A : 0x28;                    /* WRITE(10)/READ(10) */
        cdb[1] = 0;
        cdb[2] = (unsigned char)(lba >> 24); cdb[3] = (unsigned char)(lba >> 16);
        cdb[4] = (unsigned char)(lba >> 8);  cdb[5] = (unsigned char)lba;
        cdb[6] = 0;
        cdb[7] = (unsigned char)(n >> 8);    cdb[8] = (unsigned char)n;
        cdb[9] = 0;
        short stat = 0, msg = 0;
        e = agentScsiTransfer(d->scsiId, cdb, 10,
                              pb->ioParam.ioBuffer + (done << 9), n << 9, write,
                              &stat, &msg);
        drvNote(st, write ? 2 : 1, (short)n, e, stat, msg, lba);
        if (e == noErr) { lba += n; left -= n; done += n; }
    }
    pb->ioParam.ioActCount = (long)(done << 9);
    return e == noErr ? noErr : ioErr;
}

short drv_control(ParmBlkPtr pb, DCtlPtr dce)
{
    AgentDriverState *st = (AgentDriverState *)dce->dCtlStorage;
    short e;
    switch (pb->cntrlParam.csCode) {
        case 7:     e = noErr; break;      /* Eject: the medium "leaves"; the drive stays */
        case 23: {                         /* Drive info (Inside Macintosh: Devices,
                                            * .Sony csCode 23): type 1 = unspecified,
                                            * primary, internal, removable, not SCSI.
                                            * The Finder asks it of every new volume;
                                            * controlErr here preceded its alert. */
            unsigned char *p = (unsigned char *)pb->cntrlParam.csParam;
            p[0] = 0; p[1] = 0; p[2] = 0; p[3] = 1;
            e = noErr;
            break;
        }
        default:    e = controlErr; break;
    }
    if (st) drvNote(st, 3, pb->cntrlParam.csCode, e, 0, 0, 0);
    return e;
}

/* csCode 8: DrvSts (Inside Macintosh: Devices, "Disk driver status"). */
short drv_status(ParmBlkPtr pb, DCtlPtr dce)
{
    AgentDriverState *st = (AgentDriverState *)dce->dCtlStorage;
    AgentDrive       *d;
    unsigned char    *s;

    if (st) drvNote(st, 4, pb->cntrlParam.csCode, 0, 0, 0, 0);
    if (pb->cntrlParam.csCode != 8) return statusErr;
    if (!st) return nsDrvErr;
    d = driveFor(st, pb->cntrlParam.ioVRefNum);
    if (!d) return nsDrvErr;
    s = (unsigned char *)pb->cntrlParam.csParam;
    s[0] = 0; s[1] = 0;               /* track */
    s[2] = 0;                         /* writeProt */
    s[3] = 8;                         /* diskInPlace: nonejectable */
    s[4] = 1;                         /* installed */
    s[5] = 0;                         /* sides */
    BlockMove(&d->q, s + 6, sizeof(DrvQEl));   /* qLink..dQFSID */
    s[6 + sizeof(DrvQEl)] = 0;        /* twoSideFmt */
    s[7 + sizeof(DrvQEl)] = 0;        /* needsFlush */
    s[8 + sizeof(DrvQEl)] = 0; s[9 + sizeof(DrvQEl)] = 0;   /* diskErrs */
    return noErr;
}

/* ── Application context ──────────────────────────────────────────────── */

#define kUTableBase   (*(DCtlHandle **)0x011C)
#define kUnitNtryCnt  (*(short *)0x01D2)

static const unsigned char kDriverName[] = "\p.POM68KHD";

static void putJmp(unsigned char *p, void (*target)(void))
{
    unsigned long a = (unsigned long)target;
    p[0] = 0x4E; p[1] = 0xF9;                  /* JMP abs.L */
    p[2] = (unsigned char)(a >> 24); p[3] = (unsigned char)(a >> 16);
    p[4] = (unsigned char)(a >> 8);  p[5] = (unsigned char)a;
}

OSErr agentDriverInstall(AgentDriverState **stateOut)
{
    AgentDriverState *st;
    unsigned char    *blk;
    short             i;

    *stateOut = 0;
    st = (AgentDriverState *)NewPtrClear(sizeof(AgentDriverState));
    if (!st) return memFullErr;
    st->sig[0] = 'P'; st->sig[1] = 'O'; st->sig[2] = 'M'; st->sig[3] = 'D';
    for (i = 0; i < kAgentMaxDrives; i++) st->drives[i].scsiId = -1;

    blk = (unsigned char *)NewPtrClear(64);
    if (!blk) { DisposePtr((Ptr)st); return memFullErr; }
    blk[0] = 0x4F; blk[1] = 0x00;              /* dNeedLock + R/W/Ctl/Status enable */
    blk[8]  = 0; blk[9]  = 28;                 /* open   */
    blk[10] = 0; blk[11] = 34;                 /* prime  */
    blk[12] = 0; blk[13] = 40;                 /* control*/
    blk[14] = 0; blk[15] = 46;                 /* status */
    blk[16] = 0; blk[17] = 52;                 /* close  */
    BlockMove(kDriverName, blk + 18, kDriverName[0] + 1);
    putJmp(blk + 28, DrvOpenEntry);
    putJmp(blk + 34, DrvPrimeEntry);
    putJmp(blk + 40, DrvCtlEntry);
    putJmp(blk + 46, DrvStatusEntry);
    putJmp(blk + 52, DrvCloseEntry);
    st->block = blk;
    *stateOut = st;
    return noErr;
}

OSErr agentDriverForId(AgentDriverState *st, short id, short *refNumOut)
{
    const short unit   = (short)(32 + id);
    const short refNum = (short)(-(33 + id));
    DCtlHandle  dce;
    OSErr       e;
    short       opened;

    *refNumOut = 0;
    if (id < 0 || id > 6 || unit >= kUnitNtryCnt) return badUnitErr;
    if (st->installed[id]) { *refNumOut = st->installed[id]; return noErr; }
    if (kUTableBase[unit] != 0) return -29;               /* unitTblFullErr: the ROM's driver lives there */
    e = DrvrInstallGlue((Ptr)st->block, refNum);
    if (e != noErr) return e;
    dce = kUTableBase[unit];
    if (!dce) return unitEmptyErr;
    (**dce).dCtlStorage = (Handle)st;
    /* _DrvrInstall records what it was given; make the DCE say exactly
     * what we mean: a POINTER to the block (dRAMBased clear), the header's
     * flags, this unit's reference number. */
    (**dce).dCtlDriver = (void *)st->block;
    (**dce).dCtlFlags  = (short)((st->block[0] << 8) | st->block[1]);
    (**dce).dCtlRefNum = refNum;
    st->openErr = OpenDriver(kDriverName, &opened);
    if (st->openErr != noErr)
        (**dce).dCtlFlags |= 0x0020;                   /* dOpened, by hand (fnfErr on 8.1) */
    st->installed[id] = refNum;
    *refNumOut = refNum;
    return noErr;
}

void agentDriverRemove(AgentDriverState *state)
{
    short id;
    if (!state) return;
    for (id = 0; id < 7; id++) {
        if (!state->installed[id]) continue;
        CloseDriver(state->installed[id]);
        DrvrRemoveGlue(state->installed[id]);
        state->installed[id] = 0;
    }
}
