/*
 * devs.c — ADB, stockage, son. Voir devs.h.
 *
 * NB : chaines en ASCII pur (source UTF-8, ecran MacRoman).
 */
#include "devs.h"

#include <Devices.h>

#include <Files.h>
#include <Gestalt.h>
#include <Sound.h>

#include "prober_compat.h"
#include <string.h>
#include <stdio.h>

/* ── ADB ─────────────────────────────────────────────────────────────────
 * `CountADBs` / `GetIndADB` rendent, pour chaque peripherique enregistre :
 * son adresse COURANTE, son identifiant de GESTIONNAIRE (devType) et son
 * adresse D'ORIGINE. Le triplet est exactement ce que le modele de
 * peripherique de POM68K fabrique — et l'identifiant de gestionnaire est
 * ce qui distingue un clavier standard (1) d'un Extended Keyboard II (2)
 * ou du protocole etendu (3), une souris ordinaire (handler par defaut)
 * d'une souris a deux boutons (protocole etendu, activateur 4).
 *
 * On rapporte les nombres, JAMAIS une conclusion : c'est le golden — ou
 * l'humain — qui sait ce que cette machine devrait porter. */
static void collect_adb(Report *r)
{
    short n, i;
    char  key[40], val[80];

    n = (short)CountADBs();
    Report_AddDec(r, "adb", "count", n, R_INFO);
    if (n <= 0) {
        Report_Add(r, "adb", "note", "aucun peripherique ADB enregistre",
                   R_INFO);
        return;
    }

    for (i = 1; i <= n && i <= 16; i++) {
        ADBDataBlock blk;
        short        addr;

        memset(&blk, 0, sizeof blk);
        /* multiversal declare GetIndADB comme rendant un OSErr ; sur le
         * vrai Toolbox c'est l'ADRESSE ADB du peripherique (un petit
         * entier). Negatif = entree vide, dans les deux lectures. */
        addr = (short)GetIndADB(&blk, i);
        if (addr < 0) continue;

        sprintf(key, "dev%d.addr", i);
        Report_AddDec(r, "adb", key, (long)addr, R_INFO);
        sprintf(key, "dev%d.handler", i);
        Report_AddDec(r, "adb", key, (long)blk.devType, R_INFO);
        sprintf(key, "dev%d.origAddr", i);
        Report_AddDec(r, "adb", key, (long)blk.origADBAddr, R_INFO);
        /* Un gestionnaire installe ou non : la difference entre "le bus
         * connait ce peripherique" et "un pilote s'en occupe". */
        sprintf(key, "dev%d.service", i);
        sprintf(val, "%s", blk.dbServiceRtPtr ? "oui" : "non");
        Report_Add(r, "adb", key, val, R_INFO);
    }
}

/* ── stockage ────────────────────────────────────────────────────────────
 * Deux vues complementaires : la file des LECTEURS (materiel present,
 * media insere ou non) et les VOLUMES montes (ce que l'utilisateur voit).
 * Un lecteur sans volume est une information — disquette absente, ou
 * disque que le systeme n'a pas su monter. */
static void collect_drives(Report *r)
{
    /* GetDrvQHdr() est declare mais sans glue dans ce jeu ; la file des
     * lecteurs EST la globale basse $0308 (Inside Macintosh: Files). */
    QHdrPtr  q = (QHdrPtr)&LMGetDrvQHdr();
    DrvQEl  *d;
    short    i = 0;
    char     key[40];

    if (!q || !q->qHead) {
        Report_Add(r, "drive", "note", "file des lecteurs vide", R_WARN);
    } else {
        for (d = (DrvQEl *)q->qHead; d && i < 16;
             d = (DrvQEl *)d->qLink, i++) {
            sprintf(key, "drv%d.number", i);
            Report_AddDec(r, "drive", key, (long)d->dQDrive, R_INFO);
            sprintf(key, "drv%d.refNum", i);
            Report_AddDec(r, "drive", key, (long)d->dQRefNum, R_INFO);
            sprintf(key, "drv%d.fsid", i);
            Report_AddDec(r, "drive", key, (long)d->dQFSID, R_INFO);
            /* qType = 1 : les champs de taille etendus sont valides. */
            if (d->qType == 1) {
                unsigned long sz = ((unsigned long)d->dQDrvSz2 << 16) |
                                   (unsigned long)d->dQDrvSz;
                sprintf(key, "drv%d.blocks", i);
                Report_AddDec(r, "drive", key, (long)sz, R_INFO);
            }
        }
        Report_AddDec(r, "drive", "count", i, R_INFO);
    }

    /* Volumes montes : nom, taille, libre — la vue utilisateur. */
    {
        HParamBlockRec pb;
        Str63          name;
        short          idx = 1, nv = 0;
        char           val[80];

        for (;;) {
            memset(&pb, 0, sizeof pb);
            pb.volumeParam.ioNamePtr  = name;
            pb.volumeParam.ioVRefNum  = 0;
            pb.volumeParam.ioVolIndex = idx++;
            if (PBHGetVInfoSync(&pb) != noErr) break;
            if (nv >= 12) break;

            memcpy(val, name + 1, name[0]);
            val[name[0]] = 0;
            sprintf(key, "vol%d.name", nv);
            Report_Add(r, "volume", key, val, R_INFO);
            sprintf(key, "vol%d.kbTotal", nv);
            Report_AddDec(r, "volume", key,
                          (long)(((unsigned long)pb.volumeParam.ioVNmAlBlks *
                                  pb.volumeParam.ioVAlBlkSiz) / 1024UL), R_INFO);
            sprintf(key, "vol%d.kbFree", nv);
            Report_AddDec(r, "volume", key,
                          (long)(((unsigned long)pb.volumeParam.ioVFrBlk *
                                  pb.volumeParam.ioVAlBlkSiz) / 1024UL), R_INFO);
            /* drVolAtrb bit 7 : verrouille par le materiel (languette). */
            sprintf(key, "vol%d.locked", nv);
            Report_Add(r, "volume", key,
                       (pb.volumeParam.ioVAtrb & 0x0080) ? "materiel"
                     : (pb.volumeParam.ioVAtrb & 0x8000) ? "logiciel" : "non",
                       R_INFO);
            nv++;
        }
        Report_AddDec(r, "volume", "count", nv, R_INFO);
    }
}

/* ── son ─────────────────────────────────────────────────────────────────
 * Capacites annoncees seulement. Le test AUDIBLE est un choix de menu :
 * un diagnostic qui fait du bruit sans prevenir en est un mauvais. */
static void collect_sound(Report *r)
{
    long v = 0;
    char bits[80];

    if (Gestalt(gestaltSoundAttr, &v) != noErr) {
        Report_Add(r, "sound", "attr", "indisponible", R_WARN);
        return;
    }
    Report_AddHex(r, "sound", "attr", (unsigned long)v, 4, R_INFO);

    bits[0] = 0;
    if (v & (1L << gestaltStereoCapability_)) strcat(bits, "stereo ");
    if (v & (1L << gestaltStereoMixing_))     strcat(bits, "mixage ");
    if (v & (1L << gestaltSoundIOMgrPresent_))strcat(bits, "sndIOMgr ");
    if (v & (1L << gestaltBuiltInSoundInput_))strcat(bits, "entree ");
    if (v & (1L << gestaltHasSoundInputDevice_)) strcat(bits, "micro ");
    if (!bits[0]) strcpy(bits, "(aucun bit connu)");
    Report_Add(r, "sound", "features", bits, R_INFO);
}


/* ── NuBus / Slot Manager ────────────────────────────────────────────────
 * Sur les machines a slots, chaque carte porte une DeclROM decrivant ses
 * ressources : categorie, type, interfaces logicielle et materielle. Le
 * Slot Manager les enumere. C'est ce que POM68K fabrique dans `NuBus.*` /
 * `DeclRom.*`, et — pour la video du Mac II — la DeclROM SYNTHETIQUE que
 * `MacIIMemory` installe faute de dump Toby (§ 3, "repli d'actif
 * manquant"). Un rapport pris sur une vraie machine dirait, enfin, ce
 * qu'une vraie carte declare.
 *
 * Machines sans slots : le Slot Manager repond quand meme (slots 0-8 sont
 * la carte mere sur certaines), donc on enumere large et on rapporte ce
 * qui repond, sans supposer. */
static void collect_slots(Report *r)
{
#if !PROBER_HAVE_SLOTS
    /* SGetSRsrc absent du jeu multiversal (prober_compat.h). */
    Report_Add(r, "slot", "note",
               "Slot Manager non interroge : glue absente", R_INFO);
#else
    SpBlock sp;
    short   slot, found = 0;
    char    key[40], val[80];

    for (slot = 0; slot <= 14; slot++) {
        OSErr e;
        memset(&sp, 0, sizeof sp);
        sp.spSlot     = (char)slot;
        sp.spID       = 0;
        sp.spExtDev   = 0;
        sp.spCategory = 0;
        sp.spCType    = 0;
        sp.spDrvrSW   = 0;
        sp.spDrvrHW   = 0;
        sp.spTBMask   = 3;          /* categorie + type : cherche tout */
        e = SGetSRsrc(&sp);
        if (e != noErr) continue;

        sprintf(key, "slot%d.category", slot);
        Report_AddHex(r, "slot", key, (unsigned long)sp.spCategory, 2, R_INFO);
        sprintf(key, "slot%d.type", slot);
        Report_AddHex(r, "slot", key, (unsigned long)sp.spCType, 2, R_INFO);
        sprintf(key, "slot%d.drvrSW", slot);
        Report_AddHex(r, "slot", key, (unsigned long)sp.spDrvrSW, 2, R_INFO);
        sprintf(key, "slot%d.drvrHW", slot);
        Report_AddHex(r, "slot", key, (unsigned long)sp.spDrvrHW, 2, R_INFO);
        sprintf(key, "slot%d.id", slot);
        Report_AddDec(r, "slot", key, (long)sp.spID, R_INFO);
        found++;
    }

    sprintf(val, "%d", found);
    Report_Add(r, "slot", "count", val, R_INFO);
    if (!found)
        Report_Add(r, "slot", "note",
                   "aucune sResource : machine sans slots", R_INFO);
#endif /* PROBER_HAVE_SLOTS */
}

void Devs_Collect(Report *r)
{
    collect_adb(r);
    collect_drives(r);
    collect_sound(r);
    collect_slots(r);
}

void Devs_PlayTestTone(void)
{
    /* SysBeep passe par le meme chemin que l'alerte systeme : c'est
     * exactement ce qu'un utilisateur veut verifier ("mon haut-parleur
     * fonctionne-t-il ?"), et ca ne demande aucune ressource son. */
    SysBeep(30);
}
