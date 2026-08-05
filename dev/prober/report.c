/*
 * report.c — modèle de rapport + écriture JSON Lines sur volume AFP.
 * Voir dev/prober/SPEC.md §5.
 */
#include "report.h"

#include <Files.h>
#include <Devices.h>
#include <OSUtils.h>
#include <TextUtils.h>
#include <string.h>
#include <stdio.h>

/* ── copie bornée d'une C-string (toujours terminée) ── */
static void copyz(char *dst, const char *src, size_t cap)
{
    size_t i = 0;
    if (cap == 0) return;
    while (src[i] && i < cap - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

void Report_Init(Report *r) { r->count = 0; }

void Report_Add(Report *r, const char *sec, const char *key,
                const char *val, RStatus st)
{
    RFinding *f;
    if (r->count >= REPORT_MAX) return;
    f = &r->items[r->count++];
    copyz(f->section, sec, sizeof f->section);
    copyz(f->key,     key, sizeof f->key);
    copyz(f->value,   val, sizeof f->value);
    f->status = st;
}

void Report_AddHex(Report *r, const char *sec, const char *key,
                   unsigned long val, short nBytes, RStatus st)
{
    char buf[24];
    sprintf(buf, "0x%0*lX", (int)(nBytes * 2), val);
    Report_Add(r, sec, key, buf, st);
}

void Report_AddDec(Report *r, const char *sec, const char *key,
                   long val, RStatus st)
{
    char buf[24];
    sprintf(buf, "%ld", val);
    Report_Add(r, sec, key, buf, st);
}

/* ── JSON : échappe " \ et les caractères de contrôle ── */
static void json_escape(char *dst, size_t cap, const char *src)
{
    size_t o = 0;
    for (; *src && o + 6 < cap; src++) {
        unsigned char c = (unsigned char)*src;
        if (c == '"' || c == '\\') { dst[o++] = '\\'; dst[o++] = (char)c; }
        else if (c < 0x20) {
            sprintf(dst + o, "\\u%04X", (unsigned)c);
            o += 6;
        } else {
            dst[o++] = (char)c;
        }
    }
    dst[o] = '\0';
}

static const char *status_name(RStatus st)
{
    switch (st) {
        case R_OK:   return "OK";
        case R_WARN: return "WARN";
        case R_FAIL: return "FAIL";
        default:     return "INFO";
    }
}

/* ── localise un volume monté par son nom (§5.1) ── */
static OSErr find_volume(ConstStr255Param want, short *vRefOut)
{
    HParamBlockRec pb;
    Str63          name;
    short          idx = 1;

    for (;;) {
        memset(&pb, 0, sizeof pb);
        pb.volumeParam.ioNamePtr  = name;
        pb.volumeParam.ioVRefNum  = 0;
        pb.volumeParam.ioVolIndex = idx++;
        if (PBHGetVInfoSync(&pb) != noErr) return fnfErr;   /* fin de liste */
        if (EqualString(name, want, false, true)) {
            *vRefOut = pb.volumeParam.ioVRefNum;
            return noErr;
        }
    }
}

/*
 * Sérialise tout le rapport dans un buffer JSONL (lignes LF), puis l'écrit.
 * Buffer statique dimensionné pour REPORT_MAX lignes ~ 180 o chacune.
 */
static char gOut[REPORT_MAX * 200 + 512];

OSErr Report_WriteAFP(const Report *r, ConstStr255Param volName)
{
    short   vRef;
    OSErr   err;
    FSSpec  spec;
    short   refNum;
    long    len, secs;
    long    machv = 0;
    size_t  o = 0;
    short   i;
    char    ek[80], ev[160];
    Str255  fname;
    char    cname[64];

    err = find_volume(volName, &vRef);
    if (err != noErr) return err;

    /* En-tête _meta : machine + horodatage. */
    GetDateTime((unsigned long *)&secs);
    /* machineType best-effort ; 0 si Gestalt indisponible. */
    /* (l'appelant a déjà tout dans le Report ; ici on redonde l'horodatage) */
    o += sprintf(gOut + o,
        "{\"sec\":\"_meta\",\"key\":\"header\",\"val\":\"POM68K-Prober v1\","
        "\"time\":%ld,\"st\":\"INFO\"}\n", secs);

    for (i = 0; i < r->count; i++) {
        const RFinding *f = &r->items[i];
        json_escape(ek, sizeof ek, f->key);
        json_escape(ev, sizeof ev, f->value);
        if (o + 220 >= sizeof gOut) break;      /* garde-fou */
        o += sprintf(gOut + o,
            "{\"sec\":\"%s\",\"key\":\"%s\",\"val\":\"%s\",\"st\":\"%s\"}\n",
            f->section, ek, ev, status_name(f->status));
    }
    (void)machv;

    /* Nom de fichier horodaté : Probe-<secs>.log */
    sprintf(cname, "Probe-%lu.log", (unsigned long)secs);
    len = (long)strlen(cname);
    fname[0] = (unsigned char)len;
    memcpy(fname + 1, cname, len);

    err = FSMakeFSSpec(vRef, 0, fname, &spec);
    if (err != noErr && err != fnfErr) return err;   /* fnfErr = à créer */
    FSpDelete(&spec);                                 /* écrase un run précédent */
    err = FSpCreate(&spec, 'ttxt', 'TEXT', smSystemScript);
    if (err != noErr) return err;
    err = FSpOpenDF(&spec, fsWrPerm, &refNum);
    if (err != noErr) return err;

    len = (long)o;
    err = FSWrite(refNum, &len, gOut);
    FSClose(refNum);
    FlushVol(NULL, vRef);
    return err;
}

/*
 * ── sortie locale : un fichier texte a cote de l'application ────────────
 * Volume/dossier par defaut = celui de l'app lancee, donc un Create/Open
 * par nom simple suffit et evite toute dependance au File Manager
 * hierarchique. Le format est deliberement pauvre — tabulations — pour
 * etre lisible par SimpleText sur la machine ET par le golden cote host
 * sans parseur.
 */
OSErr Report_WriteLocal(const Report *r)
{
    OSErr  err;
    short  refNum;
    long   len, secs;
    size_t o = 0;
    short  i;
    Str255 fname;
    const char *cname = "POM68K Prober.txt";

    GetDateTime((unsigned long *)&secs);
    o += sprintf(gOut + o, "POM68K-Prober v1\ttime=%ld\tfindings=%d\n",
                 secs, (int)r->count);
    o += sprintf(gOut + o, "# valeurs BRUTES telles que la machine les rend.\n"
                           "# l'interpretation est a l'ecran, jamais ici.\n");

    for (i = 0; i < r->count; i++) {
        const RFinding *f = &r->items[i];
        if (o + 220 >= sizeof gOut) break;              /* garde-fou */
        o += sprintf(gOut + o, "%s\t%s\t%s\t%s\n",
                     f->section, f->key, f->value, status_name(f->status));
    }

    len = (long)strlen(cname);
    fname[0] = (unsigned char)len;
    memcpy(fname + 1, cname, len);

    /* Volume/dossier par defaut : 0/0 = celui de l'application. */
    err = Create(fname, 0, 'ttxt', 'TEXT');
    if (err != noErr && err != dupFNErr) return err;
    err = FSOpen(fname, 0, &refNum);
    if (err != noErr) return err;
    SetEOF(refNum, 0);                                  /* ecrase le run precedent */
    len = (long)o;
    err = FSWrite(refNum, &len, gOut);
    FSClose(refNum);
    FlushVol(NULL, 0);
    return err;
}
