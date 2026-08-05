/*
 * bench.c — noyaux de mesure CPU / FPU. Voir bench.h pour ce que les
 * chiffres signifient (et surtout ce qu'ils ne signifient pas).
 *
 * NB : chaines en ASCII pur (source UTF-8, ecran MacRoman).
 */
#include "bench.h"

#include <Events.h>
#include <Gestalt.h>
#include <Memory.h>
#include <Timer.h>
#include "prober_compat.h"
#include <string.h>
#include <stdio.h>

/* ── cadence de mesure ───────────────────────────────────────────────────
 * TickCount() vaut 1/60,15 s : quantification de ~16,6 ms. On vise donc
 * une duree de passe assez longue pour que le grain soit negligeable
 * (~0,5 s => erreur < 3 %), et on garde la MEILLEURE de plusieurs passes :
 * les perturbations d'un Mac multitache ne peuvent qu'ALLONGER une passe,
 * jamais la raccourcir, donc le minimum est l'estimateur honnete. */
#define BENCH_TARGET_TICKS 30      /* ~0,5 s par passe */
#define BENCH_PASSES        3

/* Volatile partout ou le resultat doit survivre a l'optimiseur : un noyau
 * dont le resultat n'est pas lu se fait supprimer, et on mesure alors la
 * vitesse du neant. */
static volatile long  gSinkL;
static volatile double gSinkD;



/* ── noyaux entiers ─────────────────────────────────────────────────── */

/* Chaine ALU registre-a-registre : dependance portee, donc pas de
 * parallelisme a exploiter — mesure le debit d'instruction pur. */
static long k_alu(long iters)
{
    register long a = 1, b = 3, i;
    for (i = 0; i < iters; i++) {
        a = a + b;
        b = b ^ a;
        a = a << 1;
        b = b - a;
    }
    return a + b;
}

/* Lecture sequentielle de longs : mesure la bande passante memoire, donc
 * la CONTENTION video la ou elle existe (une Mac Plus perd la moitie des
 * cycles bus pendant la zone visible — GttMFH table 5-3, 2,56 Mo/s). */
static long *gBuf;
static long  gBufLongs;
#define BENCH_BUF_LONGS 8192       /* 32 Ko : au-dela des caches 020/030  */

static long k_mem(long iters)
{
    register long s = 0, i, j;
    for (i = 0; i < iters; i++)
        for (j = 0; j < gBufLongs; j += 8) {
            s += gBuf[j];   s += gBuf[j+1]; s += gBuf[j+2]; s += gBuf[j+3];
            s += gBuf[j+4]; s += gBuf[j+5]; s += gBuf[j+6]; s += gBuf[j+7];
        }
    return s;
}

/* Branches dependantes des donnees : ce que ni un cache d'instructions ni
 * une fenetre de prefetch ne sauvent. */
static long k_branch(long iters)
{
    register long a = 12345, s = 0, i;
    for (i = 0; i < iters; i++) {
        a = a * 1103515245L + 12345L;
        if (a & 0x10000L)      s += 3;
        else if (a & 0x2000L)  s -= 1;
        else                   s ^= a;
    }
    return s;
}

/* DIVU : cout dependant des donnees sur 68k, et le seul noyau que le JIT
 * de POM68K refuse explicitement de compiler pour cette raison. C'est donc
 * aussi un discriminant utile entre ses deux moteurs. */
static long k_div(long iters)
{
    register unsigned long a = 0x7FFF0000UL, s = 0;
    register long i;
    for (i = 0; i < iters; i++) {
        s += a / (unsigned long)((i & 0x3FF) + 1);
        a += 0x9E37U;
    }
    return (long)s;
}

/* ── noyaux flottants ────────────────────────────────────────────────────
 * Deux mesures distinctes, et leur ECART est l'information :
 *   k_fpu   : instructions FPU reelles, emises en asm — mesure le 68881/2
 *             ou l'unite integree du 68040.
 *   k_float : arithmetique C double, telle que le systeme la fournit
 *             (SANE, soft-float de la libgcc, ou vraie FPU selon le build).
 *             C'est ce qu'une application ordinaire obtient reellement.
 * Les deux ne tournent QUE si Gestalt annonce une FPU : emettre une
 * instruction F-line sans FPU part dans le trap d'emulation, ce qui
 * mesurerait autre chose sans prevenir. */
static long k_fpu(long iters)
{
    long i;
    for (i = 0; i < iters; i++) {
        /* fp0 = fp0 * 1,0000001 + 1,0  — chaine dependante, 2 flops/tour */
        /* Le binaire cible le 68000 (il doit tourner sur une Plus), donc
         * l'assembleur refuse les instructions FPU par defaut. `.chip`
         * les autorise localement ; elles ne sont EXECUTEES que si Gestalt
         * annonce une FPU, donc leur simple presence dans le binaire est
         * sans danger sur une machine qui n'en a pas. */
        __asm__ __volatile__(
            ".chip 68040\n\t"
            "fmul.x %%fp1,%%fp0\n\t"
            "fadd.x %%fp2,%%fp0\n\t"
            ".chip 68000"
            : : : "cc");
    }
    return 1;
}

static void fpu_setup(void)
{
    /* fp0 = 1,0 ; fp1 = 1,0000001 ; fp2 = 1,0 */
    static const double one = 1.0, eps = 1.0000001;
    __asm__ __volatile__(
        ".chip 68040\n\t"
        "fmove.d %0,%%fp0\n\t"
        "fmove.d %1,%%fp1\n\t"
        "fmove.d %0,%%fp2\n\t"
        ".chip 68000"
        : : "m"(one), "m"(eps) : "cc");
}

static long k_float(long iters)
{
    register double a = 1.0, b = 1.0000001, i;
    register long n;
    for (n = 0; n < iters; n++) {
        a = a * b;
        a = a + 1.0;
        if (a > 1.0e30) a = 1.0;         /* borne, sans changer le cout    */
    }
    i = a;
    gSinkD = i;
    return (long)(a != 0.0);
}

/* ── moteur de mesure ───────────────────────────────────────────────────
 * Calibre d'abord (doublement jusqu'a atteindre la duree cible), puis
 * mesure BENCH_PASSES fois et garde le minimum. */
static BenchProgress gProgress;

void Bench_SetProgress(BenchProgress p) { gProgress = p; }

static void Progress(const char *what, short pct)
{
    if (gProgress) gProgress(what, pct);
}

void Bench_Run(Report *r, const char *name, BenchKernel k,
               long unitsPerIter, const char *unit)
{
    long iters = 64, t0, t1, best = 0x7FFFFFFFL, worst = 0, p;

    Progress(name, 0);

    /* calibration */
    for (;;) {
        t0 = (long)TickCount();
        gSinkL = k(iters);
        t1 = (long)TickCount();
        if (t1 - t0 >= BENCH_TARGET_TICKS) break;
        if (iters > 0x10000000L) break;              /* garde-fou */
        iters <<= 1;
    }

    worst = 0;
    for (p = 0; p < BENCH_PASSES; p++) {
        /* La calibration compte pour un quart du temps percu ; les trois
         * passes pour le reste. La jauge doit avancer de facon plausible,
         * pas exactement — elle sert a rassurer, pas a mesurer. */
        Progress(name, (short)(25 + (p * 75) / BENCH_PASSES));
        t0 = (long)TickCount();
        gSinkL = k(iters);
        t1 = (long)TickCount();
        if (t1 - t0 < best)  best  = t1 - t0;
        if (t1 - t0 > worst) worst = t1 - t0;
    }
    if (best <= 0) best = 1;                          /* jamais 0 tic       */

    {
        char key[40];
        sprintf(key, "%s.iters", name);
        Report_AddDec(r, "bench", key, iters * unitsPerIter, R_INFO);
        sprintf(key, "%s.ticks", name);
        Report_AddDec(r, "bench", key, best, R_INFO);
        /* La DISPERSION, pas seulement le minimum. Trois passes donnent un
         * min et un max ; si l'ecart depasse ~10 %, la machine etait
         * perturbee et le chiffre ne vaut rien. Le taire laisserait croire
         * a une precision qu'on n'a pas — la meme faute que de croire un
         * observable avant qu'il ait montre sa sensibilite. */
        sprintf(key, "%s.ticksWorst", name);
        Report_AddDec(r, "bench", key, worst, R_INFO);
        sprintf(key, "%s.unit", name);
        Report_Add(r, "bench", key, unit, R_INFO);
    }
    Progress(name, 100);
}


/* ── deux horloges independantes ─────────────────────────────────────────
 * Sur une vraie machine, TickCount() (le tic 60,15 Hz du VIA, ligne CA1)
 * et Microseconds() (le Time Manager, cadence par un TIMER du VIA) sont
 * deux chemins distincts vers la meme grandeur physique. Ils doivent donc
 * s'accorder, sans qu'aucun golden soit necessaire : c'est un controle de
 * COHERENCE INTERNE, vrai sur silicium comme en emulation.
 *
 * Dans POM68K ce sont litteralement deux generateurs separes — le tic
 * 60,15 Hz que la carte fabrique d'un cote, les timers du 6522 de l'autre.
 * Rien ne les avait jamais confrontes. Un desaccord ici est un defaut de
 * fidelite qu'aucun etalon d'ecran ne verrait.
 *
 * On rapporte les DEUX comptes bruts ; le rapport se calcule au golden ou
 * a l'affichage, jamais ici. */
static void collect_clocks(Report *r)
{
#if !PROBER_HAVE_MICROSECONDS
    /* Microseconds() absent du jeu multiversal (prober_compat.h) : sans
     * seconde horloge independante, le controle croise n'a pas de sens.
     * On le DIT plutot que de rendre un chiffre bâti sur une seule source. */
    Report_Add(r, "clock", "crossCheck",
               "non mesure : Microseconds sans glue", R_INFO);
#else
    UnsignedWide u0, u1;
    long t0, t1, dt;
    double us;

    /* Attendre un front de tic : sans ca, la quantification de 16,6 ms
     * pollue une mesure de 2 s de 0,8 %. */
    t0 = (long)TickCount();
    while ((long)TickCount() == t0) { }
    t0 = (long)TickCount();
    Microseconds(&u0);

    while ((long)TickCount() - t0 < 120) { }      /* ~2 s */

    t1 = (long)TickCount();
    Microseconds(&u1);
    dt = t1 - t0;

    /* 32 bits bas suffisent : 2 s = 2 000 000 us, tres loin du repli. */
    us = (double)(u1.lo - u0.lo);

    Report_AddDec(r, "clock", "ticks",  dt,          R_INFO);
    Report_AddDec(r, "clock", "micros", (long)us,    R_INFO);
    /* La cadence de tic que ces deux sources impliquent, en milli-Hz pour
     * garder un entier : 60,15 Hz -> 60150. */
    if (us > 0.0)
        Report_AddDec(r, "clock", "tickHz_mHz",
                      (long)((double)dt * 1.0e9 / us), R_INFO);
#endif /* PROBER_HAVE_MICROSECONDS */
}

void Bench_Collect(Report *r)
{
    long fpu = 0;
    Handle h;

    /* Le buffer memoire : alloue une fois, touche une fois pour qu'il soit
     * bien resident avant la mesure. */
    /* Degradation plutot que renoncement : sur une Plus a 1 Mo sous
     * System 6, 32 Ko d'un bloc ne sont pas acquis. On descend jusqu'a ce
     * que ca passe, et on DIT la taille retenue — un debit mesure sur
     * 8 Ko ne se compare pas a un debit mesure sur 32 Ko. */
    gBufLongs = BENCH_BUF_LONGS;
    for (;;) {
        h = NewHandle((Size)(gBufLongs * sizeof(long)));
        if (h) break;
        if (gBufLongs <= 512) break;                /* 2 Ko : plancher */
        gBufLongs >>= 2;
    }
    if (h == NULL) {
        Report_Add(r, "bench", "mem.skip", "memoire insuffisante", R_WARN);
    } else {
        HLock(h);
        gBuf = (long *)*h;
        { long i; for (i = 0; i < gBufLongs; i++) gBuf[i] = i; }
        Report_AddDec(r, "bench", "mem.bufBytes",
                      gBufLongs * (long)sizeof(long), R_INFO);
    }

    /* La barre de menus et le curseur clignotant volent des cycles :
     * on ne les desactive PAS (ce serait mentir sur la machine reelle),
     * on absorbe le bruit par le minimum sur plusieurs passes. */
    Bench_Run(r, "alu",    k_alu,    4L, "op");
    if (h) Bench_Run(r, "mem", k_mem, gBufLongs, "longword");
    Bench_Run(r, "branch", k_branch, 1L, "iter");
    Bench_Run(r, "div",    k_div,    1L, "divu");

    if (h) { HUnlock(h); DisposeHandle(h); gBuf = NULL; }

    collect_clocks(r);

    /* FPU : strictement sur annonce Gestalt. */
    if (Gestalt(gestaltFPUType, &fpu) == noErr && fpu != 0) {
        fpu_setup();
        Bench_Run(r, "fpu",   k_fpu,   2L, "flop");
        Bench_Run(r, "float", k_float, 2L, "flop");
    } else {
        Report_Add(r, "bench", "fpu.skip",
                   "aucune FPU annoncee par Gestalt", R_INFO);
    }
}
