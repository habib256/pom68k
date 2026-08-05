/*
 * report.h — modèle de rapport du POM68K Prober + sorties (écran, AFP JSONL).
 * Voir dev/prober/SPEC.md §2 et §5.
 */
#ifndef PROBER_REPORT_H
#define PROBER_REPORT_H

#include <MacTypes.h>

typedef enum { R_INFO = 0, R_OK, R_WARN, R_FAIL } RStatus;

typedef struct {
    char    section[16];   /* "ident" | "net" | "probe" | "serial"    */
    char    key[40];       /* "cpu", "myZone", "VIA2@0x50F02000", ...  */
    char    value[80];     /* valeur textuelle                        */
    RStatus status;
} RFinding;

#define REPORT_MAX 256

typedef struct {
    RFinding items[REPORT_MAX];
    short    count;
} Report;

void Report_Init(Report *r);

/* Ajouts (tronqués silencieusement si dépassement des tailles de champ). */
void Report_Add   (Report *r, const char *sec, const char *key,
                    const char *val, RStatus st);
void Report_AddHex(Report *r, const char *sec, const char *key,
                   unsigned long val, short nBytes, RStatus st);
void Report_AddDec(Report *r, const char *sec, const char *key,
                   long val, RStatus st);

/*
 * Écrit le rapport en JSON Lines sur le volume AFP nommé volName
 * (ex. "\pPOM68K Logs"). Retourne noErr, ou l'erreur File Manager, ou
 * fnfErr si le volume est introuvable. Force un FlushVol.
 */
OSErr Report_WriteAFP(const Report *r, ConstStr255Param volName);

/*
 * Ecrit le meme rapport brut en TEXTE, dans le dossier de l'application
 * elle-meme (le repertoire par defaut d'une app lancee EST celui qui la
 * contient). C'est la sortie principale pour un vrai Macintosh : pas de
 * reseau, pas de volume a monter, le fichier se retrouve a cote du
 * programme. Nom fixe "POM68K Prober.txt" : ecrase le run precedent, ce
 * qui est ce qu'on veut quand on repasse le test sur la meme machine.
 *
 * Le contenu est BRUT — une ligne "section<TAB>cle<TAB>valeur<TAB>statut"
 * par constat. L'interpretation ne va JAMAIS dans ce fichier (interp.h).
 */
OSErr Report_WriteLocal(const Report *r);

#endif /* PROBER_REPORT_H */
