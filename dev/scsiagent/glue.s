| glue.s — « POM68K Disques » : SCSI Manager, _DrvrInstall/_DrvrRemove et
| les entrées du pilote. Syntaxe GNU as m68k (Retro68), symboles C sans
| préfixe « _ » (ELF).
|
| Convention C de Retro68 (GCC m68k, PARM_BOUNDARY 16) : arguments sur la
| pile, un short prototypé est poussé sur DEUX octets (movew ...,-(sp)), un
| pointeur ou un long sur quatre ; résultat dans D0, D0-D1/A0-A1 libres.
| Mesuré au désassemblage de drv.c.obj le 2026-09-14 : lire 6(sp) pour un
| short passé en premier argument, c'est lire le mot qui suit — SCSICmd
| recevait une longueur fausse et DrvrInstall un refNum au hasard (-21).
| Convention Pascal des traps SCSI Manager : réserver le résultat (mot),
| pousser les arguments, pousser le sélecteur (mot), _SCSIDispatch ($A815)
| dépile tout sauf le résultat.

        .text
        .even

        .global SCSIReset
        .global SCSIGet
        .global SCSISelect
        .global SCSICmd
        .global SCSIRead
        .global SCSIWrite
        .global SCSIComplete
        .global DrvrInstallGlue
        .global DrvrRemoveGlue
        .global DrvOpenEntry
        .global DrvPrimeEntry
        .global DrvCtlEntry
        .global DrvStatusEntry
        .global DrvCloseEntry

| OSErr SCSIReset(void)
SCSIReset:
        clr.w   -(%sp)
        move.w  #0,-(%sp)
        .short  0xA815
        move.w  (%sp)+,%d0
        ext.l   %d0
        rts

| OSErr SCSIGet(void)
SCSIGet:
        clr.w   -(%sp)
        move.w  #1,-(%sp)
        .short  0xA815
        move.w  (%sp)+,%d0
        ext.l   %d0
        rts

| OSErr SCSISelect(short targetID)
SCSISelect:
        move.w  4(%sp),%d1
        clr.w   -(%sp)
        move.w  %d1,-(%sp)
        move.w  #2,-(%sp)
        .short  0xA815
        move.w  (%sp)+,%d0
        ext.l   %d0
        rts

| OSErr SCSICmd(Ptr buffer, short count)
SCSICmd:
        move.l  4(%sp),%a0
        move.w  8(%sp),%d1
        clr.w   -(%sp)
        move.l  %a0,-(%sp)
        move.w  %d1,-(%sp)
        move.w  #3,-(%sp)
        .short  0xA815
        move.w  (%sp)+,%d0
        ext.l   %d0
        rts

| OSErr SCSIComplete(short *stat, short *message, unsigned long wait)
SCSIComplete:
        move.l  4(%sp),%a0
        move.l  8(%sp),%a1
        move.l  12(%sp),%d1
        clr.w   -(%sp)
        move.l  %a0,-(%sp)
        move.l  %a1,-(%sp)
        move.l  %d1,-(%sp)
        move.w  #4,-(%sp)
        .short  0xA815
        move.w  (%sp)+,%d0
        ext.l   %d0
        rts

| OSErr SCSIRead(Ptr tibPtr) — SCSIRBlind (sélecteur 8) : le transfert
| aveugle est le chemin du pilote Apple, le mieux rodé sous SCSI Manager 4.3.
SCSIRead:
        move.l  4(%sp),%a0
        clr.w   -(%sp)
        move.l  %a0,-(%sp)
        move.w  #8,-(%sp)
        .short  0xA815
        move.w  (%sp)+,%d0
        ext.l   %d0
        rts

| OSErr SCSIWrite(Ptr tibPtr) — SCSIWBlind (sélecteur 9)
SCSIWrite:
        move.l  4(%sp),%a0
        clr.w   -(%sp)
        move.l  %a0,-(%sp)
        move.w  #9,-(%sp)
        .short  0xA815
        move.w  (%sp)+,%d0
        ext.l   %d0
        rts

| OSErr HFSDispatchGlue(void *pb, short selector) — A0 = pb, D0.W = sélecteur
        .global HFSDispatchGlue
HFSDispatchGlue:
        move.l  4(%sp),%a0
        move.w  8(%sp),%d0
        .short  0xA260
        ext.l   %d0
        rts

| OSErr DrvrInstallGlue(Ptr driver, short refNum) — A0 = pilote, D0.W = refNum
DrvrInstallGlue:
        move.l  4(%sp),%a0
        move.w  8(%sp),%d0
        .short  0xA03D
        ext.l   %d0
        rts

| OSErr DrvrRemoveGlue(short refNum) — D0.W = refNum
DrvrRemoveGlue:
        move.w  4(%sp),%d0
        .short  0xA03E
        ext.l   %d0
        rts

| ── Entrées du pilote ──────────────────────────────────────────────────────
| Le Device Manager appelle avec A0 = bloc paramètre, A1 = DCE. Open et
| Close reviennent par RTS avec D0 = résultat. Prime/Control/Status
| reviennent par RTS pour une requête immédiate (bit noQueue, bit 9 de
| ioTrap à l'offset 6 du bloc), et par jIODone ($08FC ; A1 = DCE, D0 =
| résultat) pour une requête en file. Les routines C (drv.c) prennent
| (pb, dce) et rendent un short dans D0 ; A2-A6/D2-D7 sont préservés par
| la convention C, A0/A1 sont resauvés ici.

DrvOpenEntry:
        movem.l %a0-%a1,-(%sp)
        move.l  %a1,-(%sp)
        move.l  %a0,-(%sp)
        jsr     drv_open
        addq.l  #8,%sp
        movem.l (%sp)+,%a0-%a1
        rts

DrvCloseEntry:
        movem.l %a0-%a1,-(%sp)
        move.l  %a1,-(%sp)
        move.l  %a0,-(%sp)
        jsr     drv_close
        addq.l  #8,%sp
        movem.l (%sp)+,%a0-%a1
        rts

DrvPrimeEntry:
        movem.l %a0-%a1,-(%sp)
        move.l  %a1,-(%sp)
        move.l  %a0,-(%sp)
        jsr     drv_prime
        addq.l  #8,%sp
        movem.l (%sp)+,%a0-%a1
        bra.s   drvFinish

DrvCtlEntry:
        movem.l %a0-%a1,-(%sp)
        move.l  %a1,-(%sp)
        move.l  %a0,-(%sp)
        jsr     drv_control
        addq.l  #8,%sp
        movem.l (%sp)+,%a0-%a1
        bra.s   drvFinish

DrvStatusEntry:
        movem.l %a0-%a1,-(%sp)
        move.l  %a1,-(%sp)
        move.l  %a0,-(%sp)
        jsr     drv_status
        addq.l  #8,%sp
        movem.l (%sp)+,%a0-%a1

drvFinish:
        btst    #1,6(%a0)                | ioTrap bit 9 = noQueueBit (octet haut, bit 1)
        bne.s   drvImmediate
        move.l  0x8FC,-(%sp)             | jIODone
        rts
drvImmediate:
        rts
