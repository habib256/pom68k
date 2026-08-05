/*
 * net.h — collecte de l'état AppleTalk (node/net, zone, lookups NBP, MacIP).
 * Voir dev/prober/SPEC.md §4.
 */
#ifndef PROBER_NET_H
#define PROBER_NET_H

#include "report.h"

void Net_Collect(Report *r);

#endif /* PROBER_NET_H */
