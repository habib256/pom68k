/*
 * ident.c — identité machine/ROM/chips (paliers 0-2). Voir SPEC.md §3.
 *
 * Cible = images System 7 ; Gestalt est donc toujours présent. Le palier 1
 * (ROM/low-mem) est collecté EN PLUS (donnée complémentaire, pas seulement un
 * repli) : il lit la vraie ROM que POM68K mappe, indépendamment du System.
 */
#include "ident.h"

#include <Gestalt.h>
#include <LowMem.h>
#include <OSUtils.h>
#include "prober_compat.h"
#include <setjmp.h>
#include <string.h>
#include <stdio.h>

/* Sélecteurs absents du jeu multiversal. Codes 4-cc : 'adbv' / 'mtcp'
 * (MacTCP dev notes). Sélecteur inconnu => Gestalt échoue, ligne omise. */
#ifndef gestaltADBVersion
#define gestaltADBVersion    0x61646276L /* 'adbv' */
#endif
#ifndef gestaltMacTCPVersion
#define gestaltMacTCPVersion 0x6D746370L /* 'mtcp' */
#endif

/* ── contrat du handler bus-error (probe.s) ─────────────────────────────
 * Le C possède le jmp_buf et le drapeau ; l'asm les référence en extern et
 * appelle longjmp() depuis le handler. */
extern void ProbeInstall(void);   /* sauve + installe vecteurs $08/$0C */
extern void ProbeRestore(void);   /* restaure                          */

jmp_buf       gProbeEnv;          /* rempli par setjmp ci-dessous       */
volatile long gProbeFaulted;      /* posé à 1 par le handler            */

/* Tente une lecture longue à *a ; false si l'accès a bus-erroré. */
static Boolean ProbeReadable(volatile unsigned long *a)
{
    gProbeFaulted = 0;
    if (setjmp(gProbeEnv) == 0) {
        volatile unsigned long x = *a;      /* peut déclencher le handler */
        (void)x;
        return true;
    }
    return false;                            /* le handler a longjmp ici   */
}

/* ── palier 0 : Gestalt ── */
static void collect_gestalt(Report *r)
{
    long v;
    if (Gestalt(gestaltMachineType,     &v) == noErr) Report_AddHex(r,"ident","machineType", v,4,R_INFO);
    if (Gestalt(gestaltSystemVersion,   &v) == noErr) Report_AddHex(r,"ident","systemVersion",v,4,R_INFO);
    if (Gestalt(gestaltROMVersion,      &v) == noErr) Report_AddHex(r,"ident","romVersion",  v,4,R_INFO);
    if (Gestalt(gestaltROMSize,         &v) == noErr) Report_AddHex(r,"ident","romSize",     v,4,R_INFO);
    if (Gestalt(gestaltProcessorType,   &v) == noErr) Report_AddHex(r,"ident","cpu",         v,4,R_INFO);
    if (Gestalt(gestaltFPUType,         &v) == noErr) Report_AddHex(r,"ident","fpu",         v,4,R_INFO);
    if (Gestalt(gestaltMMUType,         &v) == noErr) Report_AddHex(r,"ident","mmu",         v,4,R_INFO);
    if (Gestalt(gestaltHardwareAttr,    &v) == noErr) Report_AddHex(r,"ident","hwAttr",      v,4,R_INFO);
    if (Gestalt(gestaltADBVersion,      &v) == noErr) Report_AddHex(r,"ident","adb",         v,4,R_INFO);
    if (Gestalt(gestaltAppleTalkVersion,&v) == noErr) Report_AddHex(r,"ident","atalkVers",   v,4,R_INFO);
    if (Gestalt(gestaltMacTCPVersion,   &v) == noErr) Report_AddHex(r,"ident","macTCP",      v,4,R_INFO);
    if (Gestalt(gestaltQuickdrawVersion,&v) == noErr) Report_AddHex(r,"ident","qdVers",      v,4,R_INFO);
}

/* ── palier 1 : ROM + low-mem ── */
static void collect_rom_lowmem(Report *r)
{
    Ptr romBase = LMGetROMBase();                       /* $2AE            */
    Report_AddHex(r,"ident","romBase",  (unsigned long)romBase, 4, R_INFO);
    Report_AddHex(r,"ident","romChksum",*(unsigned long  *)(romBase + 0), 4, R_INFO);
    Report_AddHex(r,"ident","romIdWord",*(unsigned short *)(romBase + 8), 2, R_INFO);
    Report_AddHex(r,"ident","rom85",    *(unsigned short *)0x028E, 2, R_INFO);
    Report_AddHex(r,"ident","hwCfg",    *(unsigned short *)0x0B22, 2, R_INFO);
    Report_AddHex(r,"ident","memTop",   (unsigned long)LMGetMemTop(), 4, R_INFO);
}

/* ── palier 2 : sondage bus-error ──
 * Surensemble d'adresses documentées (SPEC.md §3.3). Le golden host sait
 * lesquelles doivent répondre par machine. present = l'accès n'a pas fauté. */
typedef struct { const char *name; unsigned long addr; } ProbeSite;

static const ProbeSite kSites[] = {
    { "VIA1@Plus",     0x00EFE1FEUL },
    { "SCC@Plus",      0x009FFFF8UL },
    { "SCSI@Plus",     0x00580000UL },
    { "IWM@Plus",      0x00DFE1FFUL },
    { "VIA1@V8",       0x00F00000UL },
    { "SCC@V8",        0x00F04000UL },
    { "SCSI@V8",       0x00F10000UL },
    { "ASC@V8",        0x00F14000UL },
    { "SWIM@V8",       0x00F16000UL },
    { "pVIA2@V8",      0x00F26000UL },
    { "VIA1@II",       0x50F00000UL },
    { "VIA2@II",       0x50F02000UL },
    { "SCC@II",        0x50F04000UL },
    { "SCSI@II",       0x50F10000UL },
    { "ASC@II",        0x50F14000UL },
    { "SWIM@II",       0x50F16000UL },
    { "DAFB@Q605",     0x50F40000UL },
};

static void collect_probe(Report *r)
{
    short i;
    char  key[40];
    ProbeInstall();
    for (i = 0; i < (short)(sizeof kSites / sizeof kSites[0]); i++) {
        Boolean ok = ProbeReadable((volatile unsigned long *)kSites[i].addr);
        /* key = "VIA2@II=0x50F02000" pour que le host ait l'adresse brute */
        sprintf(key, "%s", kSites[i].name);
        {
            char val[24];
            sprintf(val, "%s@0x%08lX", ok ? "present" : "absent", kSites[i].addr);
            Report_Add(r, "probe", key, val, R_INFO);
        }
    }
    ProbeRestore();
}


/* ── palier 3 : PRAM + horloge temps reel ───────────────────────────────
 * La PRAM etendue (XPRAM) porte la configuration que la machine garde
 * sous pile : port serie, video, son, disque de demarrage. POM68K
 * l'AMORCE lui-meme au reset (`Rtc::factoryDefaults` / `Egret`), et
 * `docs/LLE_VS_HLE.md` § 3 classe cet amorcage "politique documentee,
 * pas seulement du code". Rien, jusqu'ici, ne le verifiait vu du guest.
 *
 * On lit des octets, on ne les interprete pas : la signification d'un
 * octet de XPRAM depend du modele, et c'est au golden — ou a l'humain —
 * de savoir ce que cette machine devrait porter.
 *
 * Adresses relevees : $00-$0F (bloc classique), $13 (SPConfig, les deux
 * ports serie), $8A-$8B (le marqueur 'NuMc' de validite), $58 (video
 * sPRAM). Ce sont celles que le CHANGELOG de POM68K nomme. */
static void collect_pram(Report *r)
{
#if PROBER_HAVE_XPRAM
    unsigned char buf[16];
    char hex[80];
    short i;

    /* Le bloc classique, en un seul appel. */
    ReadXPRam((Ptr)buf, 0x00, 16);
    hex[0] = 0;
    for (i = 0; i < 16; i++)
        sprintf(hex + i * 3, "%02X%s", buf[i], (i == 15) ? "" : " ");
    Report_Add(r, "pram", "block00", hex, R_INFO);

    ReadXPRam((Ptr)buf, 0x13, 1);
    Report_AddHex(r, "pram", "spConfig", buf[0], 1, R_INFO);
    ReadXPRam((Ptr)buf, 0x58, 1);
    Report_AddHex(r, "pram", "videoSPRam", buf[0], 1, R_INFO);
    ReadXPRam((Ptr)buf, 0x8A, 4);
    hex[0] = 0;
    for (i = 0; i < 4; i++) sprintf(hex + i * 3, "%02X%s", buf[i], i == 3 ? "" : " ");
    Report_Add(r, "pram", "marker8A", hex, R_INFO);

#endif /* PROBER_HAVE_XPRAM */
    /* L'horloge : une seconde source pour la meme grandeur que le tic
     * systeme. Une pile morte ou un RTC arrete se voit ici, et nulle
     * part ailleurs. */
    {
        unsigned long secs = 0;
        DateTimeRec dt;
        char v[40];
        GetDateTime(&secs);
        Report_AddDec(r, "clock", "macSeconds", (long)secs, R_INFO);
        SecondsToDate(secs, &dt);
        sprintf(v, "%04d-%02d-%02d %02d:%02d:%02d",
                dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
        Report_Add(r, "clock", "dateTime", v, R_INFO);
    }
}

void Ident_Collect(Report *r)
{
    collect_gestalt(r);
    collect_rom_lowmem(r);
    collect_pram(r);
    collect_probe(r);
}
