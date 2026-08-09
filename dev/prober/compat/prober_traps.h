/*
 * compat/prober_traps.h — les traps a convention de REGISTRES que le jeu
 * multiversal de Retro68 ne declare pas, transcrites depuis la source
 * d'Apple au lieu d'etre devinees.
 *
 * ── Pourquoi ce fichier existe, et pourquoi il n'existait pas avant ─────
 * `prober_compat.h` eteignait quatre familles avec un motif explicite :
 * une convention de registres fausse ne produit pas une erreur de
 * compilation, elle produit un plantage sur la machine de quelqu'un, et
 * rien ne permettait de la verifier avant de l'avoir livree.
 *
 * Ce n'est plus vrai. `dev/Retro68/InterfacesAndLibraries/` porte les
 * Universal Interfaces 3.4 d'Apple (avril 2001) — 431 en-tetes C et le
 * `Interface.o` de MPW. Chaque declaration ci-dessous est RECOPIEE de la
 * source d'Apple, avec son fichier et sa ligne en face. Aucune n'est
 * reconstituee de memoire.
 *
 * ── Pourquoi pas simplement basculer le toolchain ? ─────────────────────
 * `interfaces-and-libraries.sh --universal` remplacerait multiversal pour
 * TOUT l'arbre `dev/` — mac-rogue et bonjour-pomme-one compris. C'est une
 * decision separee, qui se prend avec ses propres essais. Ici on ne veut
 * que quatre declarations, et un en-tete les porte sans rien deplacer.
 *
 * Ce fichier ne s'appelle donc PAS `Slots.h`/`Power.h`/`Timer.h` : il ne
 * doit rien masquer. Si le toolchain passe un jour a universal, chaque
 * bloc s'efface de lui-meme (voir le discriminant plus bas) et les vrais
 * en-tetes d'Apple prennent la main, sans toucher a une ligne du prober.
 *
 * ── L'idiome ────────────────────────────────────────────────────────────
 * Apple ecrit `TWOWORDINLINE(w1,w2)`, qui s'etend en `= {w1,w2}` (son
 * `ConditionalMacros.h` l. 1740). C'est exactement ce que multiversal
 * ecrit `M68K_INLINE(...)` — meme syntaxe, deja exercee par chaque build
 * du prober. Et `#pragma parameter`, qui porte la convention de
 * registres, est implemente dans le gcc de Retro68
 * (`gcc/gcc/config/m68k/m68k-mac-pragmas.c`, consomme en `m68k.cc:1582`)
 * et utilise 255 fois par multiversal. Rien d'exotique n'est introduit.
 *
 * Numeros de trap verifies dans `CIncludes/Traps.h` :
 *   _SlotManager $A06E (l.1005)   _Microseconds $A193 (l.738)
 *   _PowerMgrDispatch $A09E (l.1060)  _IdleState $A485 (l.792)
 *   _PMgrOp $A085 (l.790)
 */
#ifndef PROBER_TRAPS_H
#define PROBER_TRAPS_H

#include <Multiverse.h>

/* ── Comment ce fichier sait s'il doit s'effacer ─────────────────────────
 * PAS avec `__has_include(<Timer.h>)` : multiversal FOURNIT un `Timer.h`,
 * qui n'est qu'un alias d'une ligne vers `Multiverse.h` et ne declare ni
 * `Microseconds` ni `UnsignedWide`. Tester la presence du fichier laisse
 * donc croire que la declaration est la — c'est exactement l'erreur que
 * le premier jet de ce fichier a commise, et que le compilateur a
 * attrapee.
 *
 * Le bon test porte sur le JEU d'interfaces, pas sur un nom de fichier :
 * `UNIVERSAL_INTERFACES_VERSION` est defini par le `ConditionalMacros.h`
 * d'Apple (l. 34, `0x0340` en 3.4) et par lui seul — le
 * `ConditionalMacros.h` de multiversal existe mais ne le definit pas.
 * Un seul discriminant pour les trois blocs. */
#include <ConditionalMacros.h>

#ifdef UNIVERSAL_INTERFACES_VERSION
/* Le toolchain est passe a universal : les vrais en-tetes d'Apple font
 * autorite, ce fichier ne declare plus rien. */
#include <Slots.h>
#include <Timer.h>
#include <Power.h>
#define PROBER_TRAPS_FROM_APPLE 1
#else
#define PROBER_TRAPS_FROM_APPLE 0
#endif

/* ═══════════════════════════════════════════════════════════════════════
 * Slot Manager — UI 3.4 `CIncludes/Slots.h`
 * ═══════════════════════════════════════════════════════════════════════
 * La DeclROM de chaque carte NuBus, telle que le Slot Manager l'enumere.
 * Cote POM68K c'est ce que fabriquent `NuBus.*` / `DeclRom.*`.
 */
#if !PROBER_TRAPS_FROM_APPLE

#pragma pack(push, 2)
/* Slots.h l. 165-191, recopie champ pour champ (56 octets). */
struct SpBlock {
  long                spResult;               /*FUNCTION Result*/
  Ptr                 spsPointer;             /*structure pointer*/
  long                spSize;                 /*size of structure*/
  long                spOffsetData;           /*offset/data field used by sOffsetData*/
  Ptr                 spIOFileName;           /*ptr to IOFile name for sDisDrvrName*/
  Ptr                 spsExecPBlk;            /*pointer to sExec parameter block.*/
  long                spParamData;            /*misc parameter data (formerly spStackPtr).*/
  long                spMisc;                 /*misc field for SDM.*/
  long                spReserved;             /*reserved for future expansion*/
  short               spIOReserved;           /*Reserved field of Slot Resource Table*/
  short               spRefNum;               /*RefNum*/
  short               spCategory;             /*sType: Category*/
  short               spCType;                /*Type*/
  short               spDrvrSW;               /*DrvrSW*/
  short               spDrvrHW;               /*DrvrHW*/
  SInt8               spTBMask;               /*type bit mask bits 0..3 mask words 0..3*/
  SInt8               spSlot;                 /*slot number*/
  SInt8               spID;                   /*structure ID*/
  SInt8               spExtDev;               /*ID of the external device*/
  SInt8               spHwDev;                /*Id of the hardware device.*/
  SInt8               spByteLanes;            /*bytelanes from card ROM format block*/
  SInt8               spFlags;                /*standard flags*/
  SInt8               spKey;                  /*Internal use only*/
};
typedef struct SpBlock                  SpBlock;
typedef SpBlock *                       SpBlockPtr;
#pragma pack(pop)

/* Slots.h l. 963-967 :
 *     #pragma parameter __D0 SGetSRsrc(__A0)
 *     EXTERN_API( OSErr ) SGetSRsrc(SpBlockPtr) TWOWORDINLINE(0x700B, 0xA06E);
 * soit : moveq #11,D0 ; _SlotManager. Bloc en A0, resultat en D0. */
#if TARGET_CPU_68K
#pragma parameter __D0 SGetSRsrc(__A0)
#endif
pascal OSErr SGetSRsrc(SpBlockPtr spBlkPtr) M68K_INLINE(0x700B, 0xA06E);

#endif /* Slot Manager */


/* ═══════════════════════════════════════════════════════════════════════
 * Time Manager — UI 3.4 `CIncludes/Timer.h` + `MacTypes.h`
 * ═══════════════════════════════════════════════════════════════════════
 * La deuxieme horloge du controle croise de `bench.c` : TickCount est le
 * tic 60,15 Hz cable sur CA1, Microseconds sort d'un TIMER du 6522. Deux
 * generateurs distincts dans POM68K, que rien n'avait jamais confrontes.
 */
#if !PROBER_TRAPS_FROM_APPLE

/* MacTypes.h l. 95-99, branche TARGET_RT_BIG_ENDIAN — l'ORDRE COMPTE :
 * en gros-boutien `hi` vient en premier. La branche petit-boutien (l.106)
 * l'inverse ; s'y tromper echangerait poids fort et poids faible. */
struct UnsignedWide {
  UInt32              hi;
  UInt32              lo;
};
typedef struct UnsignedWide             UnsignedWide;

/* Timer.h l. 210-211 :
 *     EXTERN_API( void ) Microseconds(UnsignedWide *)
 *         FOURWORDINLINE(0xA193, 0x225F, 0x22C8, 0x2280);
 * soit : _Microseconds ; move.l (A7)+,A1 ; move.l A0,(A1)+ ; move.l D0,(A1)
 *
 * Pas de `#pragma parameter` ici, et c'est voulu : l'argument arrive par
 * la PILE (convention Pascal) et c'est l'inline lui-meme qui le depile —
 * d'ou `move.l (A7)+,A1`. Le resultat 64 bits revient en A0:D0, ecrit
 * dans cet ordre : A0 -> +0 (hi), D0 -> +4 (lo). Coherent avec la
 * disposition gros-boutien ci-dessus. */
pascal void Microseconds(UnsignedWide *microTickCount)
    M68K_INLINE(0xA193, 0x225F, 0x22C8, 0x2280);

#endif /* Microseconds */


/* ═══════════════════════════════════════════════════════════════════════
 * Power Manager — UI 3.4 `CIncludes/Power.h`
 * ═══════════════════════════════════════════════════════════════════════
 * Sur un Mac de bureau ces traps n'existent pas : `power.c` conditionne
 * TOUT a `gestaltPMgrExists` a l'execution. Ce fichier ne fait que rendre
 * l'appel compilable et sa convention juste.
 */
#if !PROBER_TRAPS_FROM_APPLE

/* Power.h l. 835-838, 865-868, 1259-1262 : selecteur en D0, puis
 * _PowerMgrDispatch ($A09E) ; resultat (un octet) en D0.
 *   GetSleepTimeout    moveq #$02 -> 0x7002
 *   GetHardDiskTimeout moveq #$04 -> 0x7004
 *   GetDimmingTimeout  moveq #$1D -> 0x701D
 * Les deux premieres temporisations sont en secondes, la troisieme aussi
 * — `power.c` les rend brutes, la mise en forme est ailleurs. */
#if TARGET_CPU_68K
#pragma parameter __D0 GetSleepTimeout
#endif
pascal UInt8 GetSleepTimeout(void)      M68K_INLINE(0x7002, 0xA09E);

#if TARGET_CPU_68K
#pragma parameter __D0 GetHardDiskTimeout
#endif
pascal UInt8 GetHardDiskTimeout(void)   M68K_INLINE(0x7004, 0xA09E);

#if TARGET_CPU_68K
#pragma parameter __D0 GetDimmingTimeout
#endif
pascal UInt8 GetDimmingTimeout(void)    M68K_INLINE(0x701D, 0xA09E);

/* Power.h l. 674-677 : GetCPUSpeed() TWOWORDINLINE(0x70FF, 0xA485)
 * soit moveq #-1,D0 ; _IdleState — PAS _PowerMgrDispatch. Une trap
 * differente pour un appel voisin : la recopier est justement le point. */
#if TARGET_CPU_68K
#pragma parameter __D0 GetCPUSpeed
#endif
pascal long GetCPUSpeed(void)           M68K_INLINE(0x70FF, 0xA485);

/* ── BatteryStatus : le cas qui n'est pas une trap inline ────────────────
 * Power.h l. 632-635 declare `EXTERN_API(OSErr) BatteryStatus(Byte*,
 * Byte*)` SANS inline et SANS #pragma parameter : c'est un vrai symbole,
 * resolu depuis la bibliotheque de glue de MPW. Un en-tete seul ne peut
 * donc pas le fournir — il manquerait a l'edition de liens.
 *
 * On ne devine pas pour autant : `Interface.o` est la, et `ConvertObj`
 * (livre avec Retro68) le desassemble. La routine BATTERYSTATUS d'Apple,
 * octet pour octet :
 *
 *     link    A6,#-14
 *     lea     -12(A6),A0        ; A0 = PmgrRec (12 octets)
 *     move.w  #$0068,(A0)       ; pmCommand
 *     clr.w   2(A0)             ; pmLength  = 0
 *     clr.l   4(A0)             ; pmSBuffer = nil
 *     lea     -14(A6),A1        ; tampon de reception (2 octets)
 *     move.l  A1,8(A0)          ; pmRBuffer
 *     moveq   #0,D0
 *     _PMgrOp                   ; $A085
 *     move.w  D0,16(A6)         ; -> OSErr
 *     move.l  8(A0),A0          ; A0 = pmRBuffer
 *     move.l  12(A6),A1 ; move.b (A0),(A1)      ; *status = buf[0]
 *     move.l  8(A6),A1  ; move.b 1(A0),(A1)     ; *power  = buf[1]
 *
 * Transcrit en C ci-dessous : meme bloc, meme commande, meme ordre des
 * deux octets. Le seul ecart assume est le tampon de reception, porte de
 * 2 a 8 octets — marge gratuite sur la pile, qui ne change rien a ce
 * qu'Apple lit (les deux premiers octets) mais evite d'ecraser le cadre
 * si un PMU en rendait davantage. */
#pragma pack(push, 2)
typedef struct ProberPmgrRec {
  short   pmCommand;
  short   pmLength;
  Ptr     pmSBuffer;
  Ptr     pmRBuffer;
} ProberPmgrRec;
#pragma pack(pop)

#if TARGET_CPU_68K
#pragma parameter __D0 ProberPMgrOp(__A0)
#endif
pascal OSErr ProberPMgrOp(ProberPmgrRec *pb) M68K_INLINE(0x7000, 0xA085);

static OSErr BatteryStatus(Byte *status, Byte *power)
{
    ProberPmgrRec pb;
    unsigned char buf[8];
    OSErr         err;

    pb.pmCommand = 0x0068;
    pb.pmLength  = 0;
    pb.pmSBuffer = (Ptr)0;
    pb.pmRBuffer = (Ptr)buf;
    buf[0] = buf[1] = 0;

    err = ProberPMgrOp(&pb);
    /* Relire par pmRBuffer et non par `buf` : c'est ce que fait la glue
     * d'Apple, au cas ou le Power Manager reecrirait le champ. */
    *status = ((unsigned char *)pb.pmRBuffer)[0];
    *power  = ((unsigned char *)pb.pmRBuffer)[1];
    return err;
}

#endif /* Power Manager */

#endif /* PROBER_TRAPS_H */
