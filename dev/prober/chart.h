/*
 * chart.h — vue graphique des mesures.
 *
 * Troisieme vue du Prober, apres "Identification" et "Donnees brutes".
 * PURE PRESENTATION : elle ne lit que le rapport brut et n'ecrit rien.
 * La regle des deux couches (interp.h) vaut ici comme ailleurs.
 *
 * ── Une honnetete de conception a ne pas perdre ─────────────────────────
 * Les barres sont normalisees PAR GROUPE (les op/s d'un ALU et les
 * pixels/s de QuickDraw n'ont pas le meme ordre de grandeur). Une barre
 * pleine ne veut donc PAS dire "rapide dans l'absolu" — elle veut dire
 * "le plus grand de ce groupe". C'est exactement le genre de graphique
 * qui ment tout seul, alors **la valeur chiffree est toujours ecrite a
 * cote de la barre**. Le dessin sert a comparer d'un coup d'oeil ; le
 * nombre reste la reference.
 *
 * ── Du 1 bit au 8 bits ──────────────────────────────────────────────────
 * En couleur, chaque groupe a sa teinte. En noir et blanc on retombe sur
 * les TRAMES (`qd.black`, `qd.dkGray`, `qd.gray`, `qd.ltGray`), qui
 * distinguent quatre niveaux sans gris invente — le vocabulaire graphique
 * natif de ces machines.
 */
#ifndef PROBER_CHART_H
#define PROBER_CHART_H

#include "report.h"
#include <Windows.h>

/* Dessine dans `area` (deja clippee par l'appelant). `page` choisit le
 * jeu de mesures ; rend le nombre total de pages disponibles, de sorte
 * que l'appelant sache s'il doit proposer "suivante". */
short Chart_Draw(WindowPtr win, const Rect *area, const Report *raw,
                 short page);

#endif /* PROBER_CHART_H */
