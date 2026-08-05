/*
 * gfx.h — inventaire video + banc graphique QuickDraw.
 *
 * ── Deux choses distinctes, volontairement dans le meme fichier ─────────
 *
 * 1. L'INVENTAIRE (section "video") : ce que la machine DECLARE — nombre
 *    d'ecrans, resolution de chacun, profondeur courante, profondeurs
 *    disponibles, presence de Color QuickDraw. Des faits, comme le reste.
 *
 * 2. Le BANC (section "gfx") : combien de pixels par seconde QuickDraw
 *    delivre reellement, DANS UNE FENETRE — c'est-a-dire par le chemin
 *    qu'emprunte une vraie application, clipping et port compris, et non
 *    par un acces direct au framebuffer que personne n'utilise.
 *
 * ── La mesure qui vaut le detour ────────────────────────────────────────
 * Le meme remplissage est chronometre A L'ECRAN puis HORS ECRAN (GWorld).
 * Leur ECART est le cout d'aller toucher la memoire video plutot que la
 * RAM ordinaire — autrement dit une mesure, prise de l'interieur, de
 * l'arbitrage VRAM. C'est exactement la grandeur que
 * `docs/LLE_VS_HLE.md` § 1.1 classe « acceptee, faute d'oracle » : aucune
 * des quatre puces video de MAME ne la modelise, et aucun chiffre n'existe
 * dans le Guide pour ces cartes. Sur une VRAIE machine, ce banc la produit.
 *
 * ── Prudence obligatoire ────────────────────────────────────────────────
 * Tout le chemin couleur est conditionne A L'EXECUTION (Color QuickDraw,
 * GWorlds, changement de profondeur). Une Mac Plus n'a que du 1 bit et pas
 * de GDevice : elle doit produire un rapport utile quand meme, pas une
 * ligne d'erreur. Et si la profondeur est changee, elle est RESTAUREE —
 * un outil de diagnostic qui laisse l'ecran dans un autre etat que celui
 * ou il l'a trouve n'est pas un outil de diagnostic.
 */
#ifndef PROBER_GFX_H
#define PROBER_GFX_H

#include "report.h"
#include <Windows.h>

/*
 * Remplit r. `win` est la fenetre dans laquelle dessiner (le banc s'y
 * restreint : il ne salit jamais le bureau ni les fenetres des autres).
 * Si allDepths est vrai, le banc est rejoue a chaque profondeur que
 * l'ecran principal accepte, puis la profondeur d'origine est remise.
 */
void Gfx_Collect(Report *r, WindowPtr win, Boolean allDepths);

#endif /* PROBER_GFX_H */
