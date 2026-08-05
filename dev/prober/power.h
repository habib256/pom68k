/*
 * power.h — gestion d'energie (Power Manager), si la machine en a une.
 *
 * ── Pourquoi c'est ici ──────────────────────────────────────────────────
 * Sur un portable (Portable, PowerBook 1xx, Duo), un micro-controleur
 * dedie — le PMU — tient la batterie, les temporisations de veille, la
 * vitesse CPU et l'extinction de l'ecran. Le systeme lui parle par le
 * Power Manager. C'est un sous-systeme entier que rien, dans un rapport
 * d'identite ordinaire, ne fait apparaitre.
 *
 * Cote POM68K, c'est la plate-forme #11 (MSC + PG&E, `MscMemory` +
 * `PgePmu`/`M68hc05Pge`) : le Duo 230 est la seule machine du parc a
 * porter un PMU, et son etat d'energie n'a aujourd'hui AUCUN observable
 * vu du guest. Ces lignes sont ce premier observable.
 *
 * ── Prudence ────────────────────────────────────────────────────────────
 * Sur un Mac de bureau, les appels du Power Manager n'existent pas : les
 * invoquer part dans le vide. TOUT est donc conditionne, a l'execution, au
 * bit `gestaltPMgrExists` — et quand il est absent, on le DIT (une section
 * silencieuse ne se distingue pas d'un test qui n'a pas tourne).
 *
 * Comme partout : valeurs BRUTES ici, lecture humaine dans interp.c.
 */
#ifndef PROBER_POWER_H
#define PROBER_POWER_H

#include "report.h"

/* Remplit r, section "power". Ne fait rien de risque si la machine n'a
 * pas de Power Manager — elle emet alors une seule ligne le disant. */
void Power_Collect(Report *r);

#endif /* PROBER_POWER_H */
