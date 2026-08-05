/*
 * interp.h — couche d'INTERPRETATION du POM68K Prober.
 *
 * ── La regle d'architecture, et elle n'est pas negociable ───────────────
 * Le Prober sert DEUX publics avec le meme binaire :
 *
 *   1. le proprietaire d'un vrai Macintosh, qui veut savoir QUELLE machine
 *      il a devant lui et si quelque chose cloche dedans ;
 *   2. la boucle de conformite de POM68K, qui veut des FAITS BRUTS a
 *      comparer a un golden cote host.
 *
 * Ces deux publics sont servis par DEUX COUCHES, jamais par deux verites :
 *
 *   COUCHE 1 — l'ENREGISTREMENT (report.c) : les valeurs brutes, telles
 *     que la machine les a rendues. C'est ce qui part dans le fichier.
 *     Elle n'interprete RIEN.
 *   COUCHE 2 — l'AFFICHAGE (ce fichier) : traduit ces memes valeurs pour
 *     un humain. Elle ne fabrique aucune donnee : tout ce qu'elle montre
 *     est DERIVE de la couche 1, jamais substitue a elle.
 *
 * Pourquoi ca compte : un instrument qui partage les prejuges de ce qu'il
 * mesure ne mesure plus rien. Si un jour le golden host se met a lire
 * l'interpretation plutot que le fichier brut, le Prober cesse d'etre un
 * oracle et devient une seconde implementation de nos propres croyances.
 *
 *   >> Le guest CONSTATE, le host JUGE, la vraie machine ARBITRE. <<
 *
 * Corollaire pratique : la table d'identification ci-dessous vient de
 * l'enumeration d'Apple (Inside Macintosh / Gestalt.h, recoupee avec
 * tools/rominfo.cpp modelName()), PAS de la liste de profils de POM68K.
 * Provenances differentes = elles peuvent diverger, et cette divergence
 * est une information, pas un bug a masquer.
 */
#ifndef PROBER_INTERP_H
#define PROBER_INTERP_H

#include "report.h"

/* Une ligne d'interpretation destinee a l'ecran (jamais au fichier). */
typedef struct {
    char    label[32];     /* "Machine", "Processeur", "Anomalie"      */
    char    text[96];      /* "Quadra 605", "68040 + FPU integree"     */
    RStatus status;        /* R_INFO / R_OK / R_WARN                   */
} IFinding;

#define INTERP_MAX 64

typedef struct {
    IFinding items[INTERP_MAX];
    short    count;
} Interp;

/*
 * Derive l'interpretation du rapport brut. `raw` n'est jamais modifie.
 * Emet, dans l'ordre : l'identification de la machine, les composants,
 * l'etat reseau, puis les ANOMALIES (croisements incoherents).
 */
void Interp_Build(Interp *out, const Report *raw);

/* Noms seuls, exposes pour les tests et pour "A propos". */
const char *Interp_MachineName(long gestaltMachineType);
const char *Interp_CpuName(long gestaltProcessorType);

/* Silhouette a dessiner pour ce modele (ui.h). Pure PRESENTATION : rien de
 * ceci n'entre dans le fichier brut. Rend SHAPE_UNKNOWN si le modele n'est
 * pas repertorie — auquel cas l'icone le dit au lieu de faire semblant. */
short Interp_MachineShape(long gestaltMachineType);

/* Le nom lisible tire du rapport, pour l'en-tete et "A propos". Rend une
 * chaine statique ; jamais NULL. */
/* Debit derive d'un noyau de banc : (iters x 60,15) / ticks. Rend false
 * si le noyau n'a pas ete mesure — l'appelant doit alors se taire. Expose
 * pour chart.c, qui dessine les memes nombres que la vue texte. */
Boolean Interp_Rate(const Report *raw, const char *kernel, double *rate);

const char *Interp_TitleFor(const Report *raw);
const char *Interp_SubtitleFor(const Report *raw);

#endif /* PROBER_INTERP_H */
