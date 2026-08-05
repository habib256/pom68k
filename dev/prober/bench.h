/*
 * bench.h — mesures de performance CPU / FPU du POM68K Prober.
 *
 * ── Ce que ces chiffres sont, et ce qu'ils ne sont pas ──────────────────
 *
 * L'horloge utilisee est TickCount() : le compteur 60,15 Hz du Macintosh,
 * present de la Plus au Quadra. Sur une vraie machine c'est du temps reel.
 * **Sur POM68K, c'est le temps que l'EMULATEUR fabrique** — le tic 60,15 Hz
 * est justement une des choses que l'emulateur genere.
 *
 * C'est deliberе, et c'est meme tout l'interet : on ne mesure pas la vitesse
 * de l'hote, on mesure le **travail par tic vu de l'interieur du guest**.
 * C'est exactement la grandeur qui doit coincider entre une vraie machine et
 * son emulation. Un ecart ici est un ecart de fidelite, pas de puissance.
 *
 * Corollaire a ne jamais oublier en lisant un rapport : ces nombres ne
 * disent RIEN de la vitesse de la machine hote qui fait tourner POM68K.
 *
 * ── Contrat d'enregistrement (voir interp.h) ────────────────────────────
 * Le rapport brut porte les GRANDEURS PRIMITIVES — iterations et tics —
 * jamais un score calcule. Deux raisons :
 *   1. le golden cote host recalcule comme il veut ;
 *   2. changer la formule de score plus tard n'invalide pas les fichiers
 *      deja collectes.
 * Les taux lisibles ("0,42 Mips-equivalent") sont derives a l'affichage.
 */
#ifndef PROBER_BENCH_H
#define PROBER_BENCH_H

#include "report.h"

/*
 * Lance la batterie et ajoute ses constats bruts a r, section "bench" :
 *   <nom>.iters  nombre d'iterations effectuees
 *   <nom>.ticks  tics 60,15 Hz consommes (meilleure des passes)
 *   <nom>.unit   ce qu'une iteration represente ("op", "longword", "flop")
 *
 * Noyaux CPU (entiers, toujours) : alu, mem, branch, div.
 * Noyaux FPU : fpu (instructions FPU reelles) et float (arithmetique C
 * telle que le systeme la fournit — SANE, soft-float ou FPU). Les deux ne
 * sont lances QUE si Gestalt annonce une FPU ; l'ecart entre les deux est
 * lui-meme une information.
 */
void Bench_Collect(Report *r);

/* ── moteur de mesure, expose pour gfx.c ────────────────────────────────
 * Calibre (doublement jusqu'a ~0,5 s), mesure BENCH_PASSES fois, garde le
 * MINIMUM : une perturbation ne peut qu'allonger une passe, jamais la
 * raccourcir. Emet <name>.iters / <name>.ticks / <name>.unit en brut.
 * `unitsPerIter` convertit un tour de boucle en unites comptees (pixels,
 * flops, octets...) — c'est le seul endroit ou une mise a l'echelle entre
 * dans le fichier, et elle est exacte par construction. */
typedef long (*BenchKernel)(long iters);
void Bench_Run(Report *r, const char *name, BenchKernel k,
               long unitsPerIter, const char *unit);

/* ── retour visuel ──────────────────────────────────────────────────────
 * Un banc prend plusieurs secondes ; un sablier ne dit pas si on en est a
 * 10 % ou a 90 %, et une attente muette passe pour un gel. Le point
 * d'accroche est appele au DEBUT de chaque noyau et entre les passes.
 * `pct` va de 0 a 100 pour le noyau en cours ; `what` le nomme.
 *
 * Il est installe par main.c (qui seul connait la fenetre) et vaut NULL
 * par defaut : bench.c n'a aucune dependance envers l'interface. */
typedef void (*BenchProgress)(const char *what, short pct);
void Bench_SetProgress(BenchProgress p);

#endif /* PROBER_BENCH_H */
