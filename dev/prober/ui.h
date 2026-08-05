/*
 * ui.h — presentation : fenetre, bandeau d'en-tete, icones, couleurs.
 *
 * ── La contrainte de conception ─────────────────────────────────────────
 * Le meme binaire doit etre PRESENTABLE d'une Macintosh Plus a un Quadra :
 *
 *   Plus / SE / Classic   512 x 342, 1 bit, pas de Color QuickDraw
 *   LC / II / Duo         640 x 480, 1 a 8 bits
 *   Quadra                640 x 480 et au-dela, 8 bits et plus
 *
 * Deux consequences qui ne se negocient pas :
 *   1. la fenetre se DIMENSIONNE sur l'ecran trouve, jamais sur une taille
 *      ecrite en dur (520 px de large ne tiennent pas sur une Plus) ;
 *   2. tout appel couleur est conditionne a l'execution ; en noir et blanc
 *      on retombe sur les trames (`qd.gray`, `qd.ltGray`), qui etaient
 *      *le* vocabulaire graphique de ces machines — pas un pis-aller.
 *
 * Aucune ressource : tout est construit a l'execution (SPEC.md §1), donc
 * les icones sont DESSINEES en QuickDraw plutot que stockees en bitmap.
 * Elles restent nettes a toute profondeur et ne coutent rien en taille.
 */
#ifndef PROBER_UI_H
#define PROBER_UI_H

#include <Quickdraw.h>
#include <Windows.h>

/* Silhouette a dessiner. Derivee du modele detecte — c'est de
 * l'AFFICHAGE, donc couche 2 (interp.h) : rien de tout ceci n'entre dans
 * le fichier brut. */
typedef enum {
    SHAPE_UNKNOWN = 0,
    SHAPE_COMPACT,     /* Plus, SE, Classic, Color Classic, SE/30      */
    SHAPE_DESKTOP,     /* LC, IIsi, IIvx, Quadra 605/630 — boitier plat */
    SHAPE_TOWER,       /* II, IIx, IIfx, Quadra 700/900/950            */
    SHAPE_PORTABLE     /* Portable, PowerBook, Duo                     */
} MachineShape;

/* Fenetre dimensionnee sur l'ecran courant, centree, avec une marge.
 * Rend aussi la hauteur du bandeau reservee a l'en-tete. */
WindowPtr UI_MakeWindow(short *headerHeight);

/* Le bandeau : icone + nom de machine + ligne d'etat. `title` et `sub`
 * sont des chaines C ASCII ; NULL = ligne omise. */
void UI_DrawHeader(WindowPtr win, short headerHeight,
                   MachineShape shape, const char *title, const char *sub);

/* Icone seule, dans le rectangle donne (au moins 24x24 pour rester lisible). */
void UI_DrawMachineIcon(const Rect *box, MachineShape shape);

/* Vrai si Color QuickDraw est utilisable. Evalue une fois. */
Boolean UI_HasColor(void);

/* Jauge de progression, tracee DANS le bandeau (donc sans voler de place
 * a la liste, et sans fenetre modale a gerer). `what` nomme l'etape ;
 * `pct` va de 0 a 100. Un appel avec pct >= 100 efface la jauge. */
void UI_Progress(WindowPtr win, short headerHeight,
                 const char *what, short pct);

/* Boite "A propos", construite a l'execution. */
void UI_About(MachineShape shape, const char *machineName);

#endif /* PROBER_UI_H */
