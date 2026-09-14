/* glue.h — assembly glue of « POM68K Disques » (glue.s).
 * The SCSI Manager (Inside Macintosh: Devices, ch. 3) is absent from the
 * multiversal interfaces; these are its Pascal entries through
 * _SCSIDispatch ($A815), plus _DrvrInstall/_DrvrRemove and the five driver
 * entry trampolines the in-memory DRVR jumps to. */
#pragma once
#include <MacTypes.h>

/* SCSI Manager. */
OSErr SCSIGet(void);
OSErr SCSISelect(short targetID);
OSErr SCSICmd(Ptr buffer, short count);
OSErr SCSIRead(Ptr tibPtr);
OSErr SCSIWrite(Ptr tibPtr);
OSErr SCSIComplete(short *stat, short *message, unsigned long wait);
OSErr SCSIReset(void);

/* Transfer instruction block: (opcode, param1, param2) × n, ends on scStop. */
enum { scInc = 1, scNoInc = 2, scAdd = 3, scMove = 4, scLoop = 5, scNop = 6,
       scStop = 7, scComp = 8 };
typedef struct { short scOpcode; long scParam1; long scParam2; } SCSIInstr;

/* _HFSDispatch ($A260) with a selector: the Desktop Manager (DTGetPath
 * $20, DTOpenInform $2E ...) is absent from the multiversal interfaces. */
OSErr HFSDispatchGlue(void *pb, short selector);

/* Device Manager traps without glue in this interface set. */
OSErr DrvrInstallGlue(Ptr driver, short refNum);   /* _DrvrInstall $A03D */
OSErr DrvrRemoveGlue(short refNum);                /* _DrvrRemove  $A03E */

/* Driver entry trampolines (glue.s): called by the Device Manager with
 * A0 = parameter block, A1 = DCE; they call the C routines of drv.c and
 * finish through jIODone for queued Prime/Control/Status requests. */
void DrvOpenEntry(void);
void DrvPrimeEntry(void);
void DrvCtlEntry(void);
void DrvStatusEntry(void);
void DrvCloseEntry(void);
