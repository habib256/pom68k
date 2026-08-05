/*
 * ident.h — collecte d'identité machine/ROM/chips + sondage bus-error.
 * Voir dev/prober/SPEC.md §3.
 */
#ifndef PROBER_IDENT_H
#define PROBER_IDENT_H

#include "report.h"

/* Remplit r avec : Gestalt (palier 0), ROM/low-mem (palier 1),
 * puis le sondage bus-error de topologie (palier 2). */
void Ident_Collect(Report *r);

#endif /* PROBER_IDENT_H */
