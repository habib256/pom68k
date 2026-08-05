/*
 * power.c — Power Manager : batterie, temporisations, vitesse CPU.
 * Voir power.h. Valeurs brutes ici, lecture humaine dans interp.c.
 *
 * NB : chaines en ASCII pur (source UTF-8, ecran MacRoman).
 */
#include "power.h"

#include <Gestalt.h>
#include "prober_compat.h"
#include <string.h>
#include <stdio.h>

/* Bits de gestaltPowerMgrAttr, nommes ici parce que le jeu multiversal ne
 * les expose pas tous. Source : Inside Macintosh: Devices, ch. Power Mgr. */
#ifndef gestaltPMgrExists
#define gestaltPMgrExists          0
#endif
#ifndef gestaltPMgrCPUIdle
#define gestaltPMgrCPUIdle         1
#endif
#ifndef gestaltPMgrSCC
#define gestaltPMgrSCC             2
#endif
#ifndef gestaltPMgrSound
#define gestaltPMgrSound           3
#endif
#ifndef gestaltPMgrDispatchExists
#define gestaltPMgrDispatchExists  4
#endif

void Power_Collect(Report *r)
{
    long attr = 0;
    char bits[80];

#if !PROBER_HAVE_POWER
    /* La glue Power Manager n'est pas encore verifiee (prober_compat.h).
     * On rapporte quand meme ce que GESTALT sait — c'est deja la moitie de
     * l'information, et elle ne coute aucune trap non verifiee. */
    if (Gestalt(gestaltPowerMgrAttr, &attr) == noErr &&
        (attr & (1L << gestaltPMgrExists))) {
        Report_Add(r, "power", "present", "oui", R_INFO);
        Report_AddHex(r, "power", "pmgrAttr", (unsigned long)attr, 4, R_INFO);
        bits[0] = 0;
        if (attr & (1L << gestaltPMgrCPUIdle))        strcat(bits, "cpuIdle ");
        if (attr & (1L << gestaltPMgrSCC))            strcat(bits, "sccPower ");
        if (attr & (1L << gestaltPMgrSound))          strcat(bits, "soundPower ");
        if (attr & (1L << gestaltPMgrDispatchExists)) strcat(bits, "dispatch ");
        if (!bits[0]) strcpy(bits, "(aucun bit connu)");
        Report_Add(r, "power", "features", bits, R_INFO);
        Report_Add(r, "power", "detail",
                   "batterie/temporisations : glue non verifiee", R_WARN);
    } else {
        Report_Add(r, "power", "present", "non", R_INFO);
    }
    return;
#else
    if (Gestalt(gestaltPowerMgrAttr, &attr) != noErr ||
        !(attr & (1L << gestaltPMgrExists))) {
        /* Le dire explicitement : une section muette ne se distingue pas
         * d'un test qui n'a pas tourne. */
        Report_Add(r, "power", "present", "non", R_INFO);
        return;
    }

    Report_Add(r, "power", "present", "oui", R_INFO);
    Report_AddHex(r, "power", "pmgrAttr", (unsigned long)attr, 4, R_INFO);

    /* Le detail des capacites annoncees, en clair mais toujours factuel :
     * on nomme les bits qu'on connait, on ne devine pas les autres. */
    bits[0] = 0;
    if (attr & (1L << gestaltPMgrCPUIdle))        strcat(bits, "cpuIdle ");
    if (attr & (1L << gestaltPMgrSCC))            strcat(bits, "sccPower ");
    if (attr & (1L << gestaltPMgrSound))          strcat(bits, "soundPower ");
    if (attr & (1L << gestaltPMgrDispatchExists)) strcat(bits, "dispatch ");
    if (!bits[0]) strcpy(bits, "(aucun bit connu)");
    Report_Add(r, "power", "features", bits, R_INFO);

    /* ── batterie ────────────────────────────────────────────────────────
     * BatteryStatus() est l'appel classique, present des le Portable. Le
     * couple (status, power) est rendu BRUT : ses bits varient selon le
     * modele, et c'est au golden cote host — ou a l'humain — d'en juger,
     * pas a la sonde de trancher a la place du materiel. */
    {
        unsigned char st = 0, pw = 0;
        OSErr e = BatteryStatus(&st, &pw);
        if (e == noErr) {
            Report_AddHex(r, "power", "batteryStatus", st, 1, R_INFO);
            Report_AddHex(r, "power", "batteryPower",  pw, 1, R_INFO);
        } else {
            Report_AddDec(r, "power", "batteryErr", e, R_WARN);
        }
    }

    /* ── temporisations ──────────────────────────────────────────────────
     * En tics 60,15 Hz pour la veille, en secondes pour les deux autres
     * (c'est ainsi que le Power Manager les rend ; la conversion en
     * minutes se fait a l'affichage, pas ici). */
    Report_AddDec(r, "power", "sleepTimeout",    (long)GetSleepTimeout(),    R_INFO);
    Report_AddDec(r, "power", "hardDiskTimeout", (long)GetHardDiskTimeout(), R_INFO);
    Report_AddDec(r, "power", "dimmingTimeout",  (long)GetDimmingTimeout(),  R_INFO);

    /* ── vitesse CPU ─────────────────────────────────────────────────────
     * GetCPUSpeed() rend les MHz que le PMU DECLARE. A comparer, cote
     * host, avec ce que le banc CPU MESURE : deux sources independantes
     * pour la meme grandeur, donc un croisement exploitable. */
    Report_AddDec(r, "power", "cpuSpeedMHz", (long)GetCPUSpeed(), R_INFO);
#endif /* PROBER_HAVE_POWER */
}
