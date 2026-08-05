/*
 * interp.c — traduction humaine du rapport brut. Voir interp.h pour la
 * regle d'architecture (couche 1 = fichier brut, couche 2 = ecran).
 *
 * NB : chaines en ASCII pur — le source est UTF-8, l'ecran Mac est MacRoman.
 */
#include "interp.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ── acces au rapport brut (lecture seule) ───────────────────────────── */

static const RFinding *Find(const Report *r, const char *sec, const char *key)
{
    short i;
    for (i = 0; i < r->count; i++)
        if (!strcmp(r->items[i].section, sec) && !strcmp(r->items[i].key, key))
            return &r->items[i];
    return NULL;
}

/* Les valeurs hex sont ecrites "0x...." par Report_AddHex. Retourne false
 * si la cle est absente — l'appelant doit alors se taire plutot que
 * d'inventer un zero. */
static Boolean FindHex(const Report *r, const char *sec, const char *key,
                       unsigned long *out)
{
    const RFinding *f = Find(r, sec, key);
    if (!f) return false;
    *out = strtoul(f->value, NULL, 0);
    return true;
}

static void Emit(Interp *o, const char *label, RStatus st, const char *text)
{
    IFinding *it;
    if (o->count >= INTERP_MAX) return;
    it = &o->items[o->count++];
    strncpy(it->label, label, sizeof it->label - 1);
    it->label[sizeof it->label - 1] = 0;
    strncpy(it->text, text, sizeof it->text - 1);
    it->text[sizeof it->text - 1] = 0;
    it->status = st;
}

static void EmitF(Interp *o, const char *label, RStatus st, const char *fmt,
                  long a, long b)
{
    char buf[96];
    sprintf(buf, fmt, a, b);
    Emit(o, label, st, buf);
}

/* ── tables : l'enumeration d'APPLE, pas la liste de profils de POM68K ──
 * Inside Macintosh / Gestalt.h, recoupee avec tools/rominfo.cpp
 * modelName(). Un ID inconnu se DIT inconnu et affiche sa valeur : c'est
 * une information exploitable (machine reelle non encore repertoriee),
 * pas une erreur a cacher. */
const char *Interp_MachineName(long v)
{
    switch (v) {
    case 1:  return "Macintosh 128K";
    case 2:  return "Macintosh 512K";
    case 3:  return "Macintosh 512Ke";
    case 4:  return "Macintosh Plus";
    case 5:  return "Macintosh SE";
    case 6:  return "Macintosh II";
    case 7:  return "Macintosh IIx";
    case 8:  return "Macintosh IIcx";
    case 9:  return "Macintosh SE/30";
    case 10: return "Macintosh Portable";
    case 11: return "Macintosh IIci";
    case 13: return "Macintosh IIfx";
    case 17: return "Macintosh Classic";
    case 18: return "Macintosh IIsi";
    case 19: return "Macintosh LC";
    case 20: return "Macintosh Quadra 900";
    case 21: return "PowerBook 170";
    case 22: return "Macintosh Quadra 700";
    case 23: return "Macintosh Classic II";
    case 24: return "PowerBook 100";
    case 25: return "PowerBook 140";
    case 26: return "Macintosh Quadra 950";
    case 27: return "Macintosh LC III / Performa 450";
    case 29: return "PowerBook Duo 210";
    case 30: return "Macintosh Centris 650";
    case 32: return "PowerBook Duo 230";
    case 33: return "PowerBook 180";
    case 34: return "PowerBook 160";
    case 35: return "Macintosh Quadra 800";
    case 36: return "Macintosh Quadra 650";
    case 37: return "Macintosh LC II";
    case 38: return "PowerBook Duo 250";
    case 44: return "Macintosh IIvi";
    case 45: return "Macintosh Performa 600";
    case 47: return "Macintosh Color Classic";
    case 48: return "Macintosh IIvx";
    case 50: return "Macintosh Centris 610";
    case 52: return "PowerBook 145";
    case 53: return "Macintosh LC 520";
    case 62: return "Macintosh Quadra/Centris 610 (DOS)";
    case 75: return "Macintosh Quadra 605 / LC 475";
    case 77: return "Macintosh LC 575";
    case 84: return "Macintosh Quadra 630 / LC 630";
    case 89: return "Macintosh LC 580";
    case 94: return "Macintosh Quadra 605";
    default: return NULL;
    }
}

const char *Interp_CpuName(long v)
{
    switch (v) {
    case 1:  return "MC68000";
    case 2:  return "MC68010";
    case 3:  return "MC68020";
    case 4:  return "MC68030";
    case 5:  return "MC68040";
    default: return NULL;
    }
}

static const char *FpuName(long v)
{
    switch (v) {
    case 0:  return "aucune";
    case 1:  return "MC68881";
    case 2:  return "MC68882";
    case 3:  return "FPU integree (68040)";
    default: return NULL;
    }
}

static const char *MmuName(long v)
{
    switch (v) {
    case 0:  return "aucune";
    case 1:  return "MC68851 (PMMU)";
    case 2:  return "MMU integree (68030)";
    case 3:  return "MMU integree (68040)";
    case 4:  return "HMMU (Mac II d'origine)";
    default: return NULL;
    }
}

/* ── sections ─────────────────────────────────────────────────────────── */

static void interp_machine(Interp *o, const Report *r)
{
    unsigned long v, sys;
    char buf[96];

    if (FindHex(r, "ident", "machineType", &v)) {
        const char *n = Interp_MachineName((long)v);
        if (n) {
            sprintf(buf, "%s", n);
            Emit(o, "Machine", R_OK, buf);
        } else {
            /* Inconnue : on le DIT, avec la valeur, plutot que de deviner. */
            sprintf(buf, "modele non repertorie (Gestalt %lu)", v);
            Emit(o, "Machine", R_WARN, buf);
        }
    } else {
        Emit(o, "Machine", R_WARN, "Gestalt indisponible (System < 6.0.4 ?)");
    }

    if (FindHex(r, "ident", "systemVersion", &sys)) {
        /* BCD : 0x0710 = 7.1.0 */
        sprintf(buf, "Systeme %lu.%lu.%lu",
                (sys >> 8) & 0xF, (sys >> 4) & 0xF, sys & 0xF);
        Emit(o, "Systeme", R_INFO, buf);
    }
    if (FindHex(r, "ident", "romSize", &v))
        EmitF(o, "ROM", R_INFO, "%ld Ko", (long)(v / 1024), 0);
    if (FindHex(r, "ident", "romChksum", &v)) {
        sprintf(buf, "somme $%08lX", v);
        Emit(o, "ROM", R_INFO, buf);
    }
}

static void interp_parts(Interp *o, const Report *r)
{
    unsigned long v;
    char buf[96];
    const char *n;

    if (FindHex(r, "ident", "cpu", &v)) {
        n = Interp_CpuName((long)v);
        sprintf(buf, "%s", n ? n : "inconnu");
        Emit(o, "Processeur", n ? R_INFO : R_WARN, buf);
    }
    if (FindHex(r, "ident", "fpu", &v)) {
        n = FpuName((long)v);
        sprintf(buf, "%s", n ? n : "inconnue");
        Emit(o, "Virgule flottante", R_INFO, buf);
    }
    if (FindHex(r, "ident", "mmu", &v)) {
        n = MmuName((long)v);
        sprintf(buf, "%s", n ? n : "inconnue");
        Emit(o, "MMU", R_INFO, buf);
    }
    if (FindHex(r, "ident", "memTop", &v))
        EmitF(o, "Memoire", R_INFO, "%ld Mo (MemTop)", (long)(v >> 20), 0);
    if (FindHex(r, "ident", "hwAttr", &v)) {
        /* gestaltHardwareAttr : bit 0 = VIA1, 1 = VIA2, 3 = ASC, 4 = SCC,
         * 5 = SCSI ; on nomme ce qu'on sait, on ne devine pas le reste. */
        buf[0] = 0;
        if (v & (1UL << 0)) strcat(buf, "VIA1 ");
        if (v & (1UL << 1)) strcat(buf, "VIA2 ");
        if (v & (1UL << 3)) strcat(buf, "ASC ");
        if (v & (1UL << 4)) strcat(buf, "SCC ");
        if (v & (1UL << 5)) strcat(buf, "SCSI ");
        if (!buf[0]) strcpy(buf, "(aucun bit connu)");
        Emit(o, "Materiel declare", R_INFO, buf);
    }
}

static void interp_net(Interp *o, const Report *r)
{
    const RFinding *f;
    char buf[96];

    f = Find(r, "net", "node");
    if (f) {
        const RFinding *z = Find(r, "net", "myZone");
        sprintf(buf, "noeud %s%s%s", f->value,
                z ? ", zone " : "", z ? z->value : "");
        Emit(o, "AppleTalk", R_OK, buf);
    } else if (Find(r, "net", "mppOpen")) {
        Emit(o, "AppleTalk", R_WARN, "pilote ouvert mais pas d'adresse");
    }
}

/* ── ANOMALIES ────────────────────────────────────────────────────────────
 * Croisements entre sources INDEPENDANTES. Une anomalie n'est jamais un
 * verdict sur POM68K — c'est un signalement pour l'humain devant la
 * machine ("ce boitier dit une chose et sa ROM en dit une autre").
 * Cote conformite, c'est le golden host qui juge, sur le fichier brut. */
static void interp_anomalies(Interp *o, const Report *r)
{
    unsigned long mt, cpu, fpu, romsz, chk;
    Boolean hasMt  = FindHex(r, "ident", "machineType", &mt);
    Boolean hasCpu = FindHex(r, "ident", "cpu", &cpu);
    short before = o->count;

    /* 1. Le System dit 68040 mais aucune FPU : legitime sur 68LC040 —
     *    on le NOTE sans le condamner, c'est exactement le genre de nuance
     *    qu'un utilisateur veut voir. */
    if (hasCpu && cpu == 5 && FindHex(r, "ident", "fpu", &fpu) && fpu == 0)
        Emit(o, "A noter", R_INFO,
             "68040 sans FPU : 68LC040 (normal sur LC 475/575)");

    /* 2. ROM annoncee vs ROM lisible : deux sources independantes. */
    if (FindHex(r, "ident", "romSize", &romsz) &&
        FindHex(r, "ident", "romChksum", &chk) && chk == 0)
        Emit(o, "Anomalie", R_WARN,
             "ROM annoncee mais somme de controle nulle");

    /* 3. Topologie : une adresse a repondu alors que le modele declare ne
     *    devrait pas la porter. On ne teste que les cas ou les familles
     *    s'excluent VRAIMENT (24 bits contre 32 bits). */
    if (hasMt) {
        const RFinding *plus = Find(r, "probe", "VIA1@Plus");
        const RFinding *mII  = Find(r, "probe", "VIA1@II");
        Boolean isCompact = (mt == 4 || mt == 5 || mt == 17);
        if (isCompact && mII && !strncmp(mII->value, "present", 7))
            Emit(o, "Anomalie", R_WARN,
                 "compact 24 bits, mais la carte 32 bits repond aussi");
        if (!isCompact && plus && !strncmp(plus->value, "present", 7) &&
            mt != 0)
            Emit(o, "A noter", R_INFO,
                 "la carte 24 bits repond encore (miroir ou mode 24 bits)");
    }

    /* 4. Rien de suspect : le dire explicitement. Un ecran muet ne se
     *    distingue pas d'un test qui n'a pas tourne — la lecon coute assez
     *    cher cote emulateur pour valoir aussi ici. */
    if (o->count == before)
        Emit(o, "Anomalies", R_OK, "aucune incoherence detectee");
}


/* ── performance ─────────────────────────────────────────────────────────
 * Le brut porte iterations + tics ; le taux se calcule ICI, a l'affichage.
 * Rappel du contrat (bench.h) : sur POM68K ces nombres mesurent le travail
 * par tic 60,15 Hz VU DU GUEST, pas la vitesse de la machine hote. */

Boolean Interp_Rate(const Report *r, const char *name, double *rate)
{
    char k[40];
    const RFinding *fi, *ft;
    unsigned long iters, ticks;

    sprintf(k, "%s.iters", name);  fi = Find(r, "bench", k);
    sprintf(k, "%s.ticks", name);  ft = Find(r, "bench", k);
    if (!fi || !ft) return false;
    iters = strtoul(fi->value, NULL, 10);
    ticks = strtoul(ft->value, NULL, 10);
    if (!ticks) return false;
    /* 60,15 tics par seconde : le vrai tic Macintosh, pas 60. */
    *rate = ((double)iters * 60.15) / (double)ticks;
    return true;
}

/* Vrai si les passes ont trop diverge pour que le chiffre veuille dire
 * quelque chose (> 10 % entre la meilleure et la pire). */
static Boolean RateNoisy(const Report *r, const char *name)
{
    char k[40];
    const RFinding *fb, *fw;
    long b, w;
    sprintf(k, "%s.ticks", name);      fb = Find(r, "bench", k);
    sprintf(k, "%s.ticksWorst", name); fw = Find(r, "bench", k);
    if (!fb || !fw) return false;
    b = strtol(fb->value, NULL, 10);
    w = strtol(fw->value, NULL, 10);
    return (Boolean)(b > 0 && w > b + b / 10);
}

static void EmitRate(Interp *o, const char *label, const char *name,
                     const Report *r, const char *unit)
{
    double v;
    char buf[96];
    if (!Interp_Rate(r, name, &v)) return;
    if (v >= 1.0e6)      sprintf(buf, "%.2f M%s/s", v / 1.0e6, unit);
    else if (v >= 1.0e3) sprintf(buf, "%.1f k%s/s", v / 1.0e3, unit);
    else                 sprintf(buf, "%.0f %s/s", v, unit);
    /* Dire quand la mesure a bouge : mieux vaut un chiffre marque douteux
     * qu'un chiffre net auquel on croit a tort. */
    if (RateNoisy(r, name)) strcat(buf, "  (mesure instable)");
    Emit(o, label, RateNoisy(r, name) ? R_WARN : R_INFO, buf);
}

static void interp_bench(Interp *o, const Report *r)
{
    double mem, fpu, flt;
    char buf[96];

    if (!Find(r, "bench", "alu.ticks")) return;      /* banc pas lance */

    EmitRate(o, "Entiers (ALU)", "alu",    r, "op");
    EmitRate(o, "Branchements",  "branch", r, "iter");
    EmitRate(o, "Divisions",     "div",    r, "divu");

    /* La memoire en octets/s : plus parlant que "longwords/s", et c'est la
     * grandeur que documente le Guide (2,56 Mo/s sur une Mac Plus, ou la
     * contention video prend la moitie des cycles bus pendant la zone
     * visible). Une Plus qui rendrait le double n'a pas de contention. */
    if (Interp_Rate(r, "mem", &mem)) {
        sprintf(buf, "%.2f Mo/s en lecture", (mem * 4.0) / 1.0e6);
        Emit(o, "Bande passante", R_INFO, buf);
    }

    if (Interp_Rate(r, "fpu", &fpu)) {
        sprintf(buf, "%.0f kflops (instructions FPU)", fpu / 1.0e3);
        Emit(o, "Virgule flottante", R_INFO, buf);
    }
    if (Interp_Rate(r, "float", &flt)) {
        sprintf(buf, "%.0f kflops (arithmetique du systeme)", flt / 1.0e3);
        Emit(o, "Calcul applicatif", R_INFO, buf);
    }
    /* L'ECART entre les deux est l'information utile : une arithmetique C
     * bien plus lente que les instructions FPU veut dire que le systeme
     * passe par SANE au lieu du coprocesseur pourtant present. */
    if (Interp_Rate(r, "fpu", &fpu) && Interp_Rate(r, "float", &flt) &&
        flt > 0.0 && fpu / flt >= 4.0)
        Emit(o, "A noter", R_WARN,
             "FPU presente mais le calcul passe par SANE (x4 ou plus)");
}


/* ── video : inventaire + banc graphique ─────────────────────────────── */

static void interp_video(Interp *o, const Report *r)
{
    const RFinding *f;
    unsigned long w, h, d;
    char buf[96], key[40];
    short n;

    f = Find(r, "video", "colorQD");
    if (!f) return;

    for (n = 0; n < 8; n++) {
        const RFinding *fw, *fh, *fd, *fl;
        sprintf(key, "screen%d.w", n);      fw = Find(r, "video", key);
        if (!fw) break;
        sprintf(key, "screen%d.h", n);      fh = Find(r, "video", key);
        sprintf(key, "screen%d.depth", n);  fd = Find(r, "video", key);
        sprintf(key, "screen%d.depths", n); fl = Find(r, "video", key);
        w = strtoul(fw->value, NULL, 10);
        h = fh ? strtoul(fh->value, NULL, 10) : 0;
        d = fd ? strtoul(fd->value, NULL, 10) : 1;
        sprintf(buf, "%lux%lu en %s%s%s", w, h,
                d == 1 ? "noir et blanc" : "couleur",
                fl ? ", modes " : "", fl ? fl->value : "");
        if (d > 1) sprintf(buf + strlen(buf), " (%lu bits)", d);
        Emit(o, n == 0 ? "Ecran" : "Ecran secondaire", R_INFO, buf);
    }
    if (!strcmp(f->value, "non"))
        Emit(o, "QuickDraw", R_INFO, "noir et blanc seulement (pas de Color QD)");

    /* Debits de dessin : on montre la profondeur courante en priorite, et
     * on annonce les autres si elles ont ete mesurees. */
    for (n = 1; n <= 32; n <<= 1) {
        double v;
        sprintf(key, "fill@%d", n);
        if (!Interp_Rate(r, key, &v)) continue;
        sprintf(buf, "%.2f Mpixels/s en %d bit%s",
                v / 1.0e6, n, n > 1 ? "s" : "");
        Emit(o, "Remplissage", R_INFO, buf);

        sprintf(key, "blit@%d", n);
        if (Interp_Rate(r, key, &v)) {
            sprintf(buf, "%.2f Mpixels/s (CopyBits)", v / 1.0e6);
            Emit(o, "Recopie", R_INFO, buf);
        }
        sprintf(key, "text@%d", n);
        if (Interp_Rate(r, key, &v)) {
            sprintf(buf, "%.0f caracteres/s", v);
            Emit(o, "Texte", R_INFO, buf);
        }

        /* L'ecart ecran / hors ecran = le prix de la memoire video. C'est
         * la seule mesure d'arbitrage VRAM que ce projet possede, et elle
         * ne vaut vraiment que prise sur du silicium reel. */
        {
            double on, off;
            char k2[40];
            sprintf(key,  "fill@%d", n);
            sprintf(k2, "fillOff@%d", n);
            if (Interp_Rate(r, key, &on) && Interp_Rate(r, k2, &off) && on > 0.0) {
                double ratio = off / on;
                if (ratio >= 1.05) {
                    sprintf(buf, "hors ecran %.2fx plus rapide "
                                 "(cout de la memoire video)", ratio);
                    Emit(o, "Memoire video", R_INFO, buf);
                } else {
                    Emit(o, "Memoire video", R_INFO,
                         "ecran et hors ecran au meme debit");
                }
            }
        }
    }

    f = Find(r, "gfx", "depth.restore");
    if (f && f->status == R_FAIL)
        Emit(o, "Anomalie", R_WARN,
             "profondeur d'ecran NON restauree apres le test");
}



/* ── peripheriques ADB ──────────────────────────────────────────────────
 * L'identifiant de gestionnaire est ce qui distingue un clavier standard
 * (1) d'un Extended Keyboard II (2) ou du protocole etendu (3), et une
 * souris ordinaire d'une souris a deux boutons (protocole etendu). On
 * NOMME ce que l'enumeration d'Apple documente, on ne juge pas. */
static const char *AdbKind(long addr, long handler)
{
    switch (addr) {
    case 2:
        switch (handler) {
        case 1:  return "clavier standard";
        case 2:  return "Extended Keyboard II";
        case 3:  return "clavier, protocole etendu";
        case 4:  return "clavier ISO";
        default: return "clavier";
        }
    case 3:
        switch (handler) {
        case 1:  return "souris 100 dpi";
        case 2:  return "souris 200 dpi";
        case 4:  return "souris, protocole etendu";
        default: return "peripherique de pointage";
        }
    case 1:  return "tablette / crayon";
    case 7:  return "peripherique reserve";
    default: return NULL;
    }
}

static void interp_adb(Interp *o, const Report *r)
{
    const RFinding *f;
    char key[40], buf[96];
    long n, i;

    f = Find(r, "adb", "count");
    if (!f) return;
    n = strtol(f->value, NULL, 10);
    if (n <= 0) {
        Emit(o, "ADB", R_WARN, "aucun peripherique enregistre");
        return;
    }

    for (i = 1; i <= n && i <= 16; i++) {
        const RFinding *fa, *fh;
        long addr, hnd;
        const char *kind;
        sprintf(key, "dev%ld.addr", i);    fa = Find(r, "adb", key);
        if (!fa) continue;
        sprintf(key, "dev%ld.handler", i); fh = Find(r, "adb", key);
        addr = strtol(fa->value, NULL, 10);
        hnd  = fh ? strtol(fh->value, NULL, 10) : -1;
        kind = AdbKind(addr, hnd);
        if (kind) sprintf(buf, "%s (adr %ld, gestionnaire %ld)", kind, addr, hnd);
        else      sprintf(buf, "adresse %ld, gestionnaire %ld", addr, hnd);
        Emit(o, i == 1 ? "ADB" : "", R_INFO, buf);
    }
}

/* ── stockage ─────────────────────────────────────────────────────────── */
static void interp_storage(Interp *o, const Report *r)
{
    const RFinding *f;
    char key[40], buf[96];
    long n, i;

    f = Find(r, "drive", "count");
    if (f) {
        n = strtol(f->value, NULL, 10);
        sprintf(buf, "%ld lecteur%s dans la file", n, n > 1 ? "s" : "");
        Emit(o, "Lecteurs", R_INFO, buf);
    }

    f = Find(r, "volume", "count");
    if (!f) return;
    n = strtol(f->value, NULL, 10);
    for (i = 0; i < n && i < 12; i++) {
        const RFinding *fn, *ft, *ff, *fl;
        sprintf(key, "vol%ld.name", i);    fn = Find(r, "volume", key);
        if (!fn) continue;
        sprintf(key, "vol%ld.kbTotal", i); ft = Find(r, "volume", key);
        sprintf(key, "vol%ld.kbFree", i);  ff = Find(r, "volume", key);
        sprintf(key, "vol%ld.locked", i);  fl = Find(r, "volume", key);
        sprintf(buf, "%s : %ld Mo, %ld Mo libres%s", fn->value,
                ft ? strtol(ft->value, NULL, 10) / 1024 : 0,
                ff ? strtol(ff->value, NULL, 10) / 1024 : 0,
                (fl && strcmp(fl->value, "non")) ? " (protege)" : "");
        Emit(o, i == 0 ? "Volumes" : "", R_INFO, buf);
    }
}

/* ── son ──────────────────────────────────────────────────────────────── */
static void interp_sound(Interp *o, const Report *r)
{
    const RFinding *f = Find(r, "sound", "features");
    if (!f) return;
    Emit(o, "Son", R_INFO, f->value);
}

/* ── energie ──────────────────────────────────────────────────────────── */
static void interp_power(Interp *o, const Report *r)
{
    const RFinding *f;
    unsigned long v;
    char buf[96];

    f = Find(r, "power", "present");
    if (!f) return;
    if (strcmp(f->value, "oui")) {
        /* Pas de PMU : c'est un fait, pas une lacune. Un Mac de bureau
         * n'en a pas, et le taire laisserait croire a un test manque. */
        Emit(o, "Energie", R_INFO, "pas de gestionnaire (machine de bureau)");
        return;
    }
    Emit(o, "Energie", R_OK, "gestionnaire present (portable)");

    if (FindHex(r, "power", "batteryStatus", &v)) {
        /* Les bits varient selon le modele : on montre le brut et le seul
         * bit universellement documente (chargeur branche). */
        sprintf(buf, "etat $%02lX%s", v,
                (v & 0x01) ? " (chargeur branche)" : "");
        Emit(o, "Batterie", R_INFO, buf);
    }
    f = Find(r, "power", "sleepTimeout");
    if (f) {
        long t = strtol(f->value, NULL, 10);
        sprintf(buf, "veille apres %ld s", t);
        Emit(o, "Temporisation", R_INFO, buf);
    }
    f = Find(r, "power", "cpuSpeedMHz");
    if (f) {
        sprintf(buf, "%s MHz annonces par le PMU", f->value);
        Emit(o, "Vitesse CPU", R_INFO, buf);
    }
}

/* ── coherence des horloges ─────────────────────────────────────────────
 * TickCount (tic 60,15 Hz sur CA1) contre Microseconds (Time Manager,
 * cadence par un TIMER du VIA) : deux chemins independants vers la meme
 * grandeur. Ils doivent s'accorder — aucun golden n'est necessaire pour
 * le dire, c'est de la coherence interne. */
static void interp_clock(Interp *o, const Report *r)
{
    const RFinding *f;
    char buf[96];

    f = Find(r, "clock", "dateTime");
    if (f) {
        const RFinding *sec = Find(r, "clock", "macSeconds");
        long s = sec ? strtol(sec->value, NULL, 10) : 0;
        /* L'epoque Mac part de 1904. Une date anterieure a 1990 sur une
         * machine qui a servi veut presque toujours dire pile morte ou
         * PRAM effacee — un diagnostic que son proprietaire veut lire. */
        sprintf(buf, "%s", f->value);
        Emit(o, "Horloge", (s < 2713910400L) ? R_WARN : R_INFO, buf);
        if (s < 2713910400L)
            Emit(o, "A noter", R_WARN,
                 "date anterieure a 1990 : pile de sauvegarde probablement morte");
    }

    f = Find(r, "clock", "tickHz_mHz");
    {
    long mhz;
    double hz, err;

    if (!f) return;
    mhz = strtol(f->value, NULL, 10);
    hz = (double)mhz / 1000.0;
    err = (hz - 60.15) / 60.15 * 100.0;
    if (err < 0) err = -err;

    sprintf(buf, "%.2f Hz mesures contre 60,15 attendus", hz);
    Emit(o, "Tic systeme", err < 1.0 ? R_OK : R_WARN, buf);
    if (err >= 1.0) {
        sprintf(buf, "les deux horloges divergent de %.1f %%", err);
        Emit(o, "Anomalie", R_WARN, buf);
    }
    }
}


/* ── slots ────────────────────────────────────────────────────────────── */
static const char *SlotCategory(long c)
{
    switch (c) {
    case 1:  return "carte de test";
    case 2:  return "carte d'affichage";
    case 3:  return "carte reseau";
    case 4:  return "communication";
    case 6:  return "controleur d'entree/sortie";
    case 9:  return "processeur";
    case 10: return "carte d'intelligence";
    case 11: return "controleur de bus";
    default: return NULL;
    }
}

static void interp_slots(Interp *o, const Report *r)
{
    const RFinding *f;
    char key[40], buf[96];
    short slot, shown = 0;

    f = Find(r, "slot", "count");
    if (!f) return;
    if (!strcmp(f->value, "0")) {
        Emit(o, "Slots", R_INFO, "machine sans slots d'extension");
        return;
    }
    for (slot = 0; slot <= 14; slot++) {
        unsigned long cat = 0, typ = 0;
        const char *n;
        sprintf(key, "slot%d.category", slot);
        if (!FindHex(r, "slot", key, &cat)) continue;
        sprintf(key, "slot%d.type", slot);
        FindHex(r, "slot", key, &typ);
        n = SlotCategory((long)cat);
        sprintf(buf, "slot %X : %s (type $%02lX)", slot,
                n ? n : "categorie inconnue", typ);
        Emit(o, shown++ ? "" : "Slots", R_INFO, buf);
    }
}

/* ── derives de presentation (couche 2 stricte) ─────────────────────── */

short Interp_MachineShape(long v)
{
    switch (v) {
    /* compacts a tube : Plus, SE, Classic, SE/30, Classic II, CC */
    case 4: case 5: case 9: case 17: case 23: case 47:
        return 1;   /* SHAPE_COMPACT */
    /* portables : Portable, PowerBook, Duo */
    case 10: case 21: case 24: case 25: case 29: case 32:
    case 33: case 34: case 38: case 52:
        return 4;   /* SHAPE_PORTABLE */
    /* boitiers verticaux : II, IIx, IIcx, IIfx, Quadra 700/900/950 */
    case 6: case 7: case 8: case 13: case 20: case 22: case 26:
        return 3;   /* SHAPE_TOWER */
    /* tout le reste des modeles connus : boitier plat */
    case 11: case 18: case 19: case 27: case 30: case 35: case 36:
    case 37: case 44: case 45: case 48: case 50: case 53: case 62:
    case 75: case 77: case 84: case 89: case 94:
        return 2;   /* SHAPE_DESKTOP */
    default:
        return 0;   /* SHAPE_UNKNOWN */
    }
}

const char *Interp_TitleFor(const Report *raw)
{
    static char t[80];
    unsigned long v;
    const char *n;
    if (FindHex(raw, "ident", "machineType", &v)) {
        n = Interp_MachineName((long)v);
        if (n) { strncpy(t, n, sizeof t - 1); t[sizeof t - 1] = 0; return t; }
        sprintf(t, "Macintosh (Gestalt %lu)", v);
        return t;
    }
    return "Macintosh 68k";
}

const char *Interp_SubtitleFor(const Report *raw)
{
    static char t[80];
    unsigned long cpu = 0, fpu = 0, sys = 0;
    const char *c;
    t[0] = 0;
    if (FindHex(raw, "ident", "cpu", &cpu)) {
        c = Interp_CpuName((long)cpu);
        sprintf(t, "%s", c ? c : "68k");
    } else strcpy(t, "68k");
    if (FindHex(raw, "ident", "fpu", &fpu) && fpu != 0)
        strcat(t, " + FPU");
    if (FindHex(raw, "ident", "systemVersion", &sys))
        sprintf(t + strlen(t), "  -  Systeme %lu.%lu",
                (sys >> 8) & 0xF, (sys >> 4) & 0xF);
    return t;
}

/* ── entree ───────────────────────────────────────────────────────────── */

void Interp_Build(Interp *out, const Report *raw)
{
    out->count = 0;
    interp_machine(out, raw);
    interp_parts(out, raw);
    interp_net(out, raw);
    interp_adb(out, raw);
    interp_storage(out, raw);
    interp_sound(out, raw);
    interp_slots(out, raw);
    interp_power(out, raw);
    interp_clock(out, raw);
    interp_bench(out, raw);
    interp_video(out, raw);
    interp_anomalies(out, raw);
}
