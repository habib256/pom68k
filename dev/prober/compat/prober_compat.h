/*
 * compat/prober_compat.h — ce que le jeu multiversal de Retro68 ne declare
 * pas, et ce qu'on choisit de ne PAS declarer a l'aveugle.
 *
 * ── L'etat des lieux, verifie ───────────────────────────────────────────
 * `CIncludes` est mince : ni `Scrap.h`, ni `ADB.h`, ni `Slots.h`, ni
 * `Displays.h`, ni `Power.h`, ni `Retrace.h`. Mais `Multiverse.h` — le gros
 * en-tete genere — declare quand meme la plupart des appels, avec l'idiome
 * `M68K_INLINE(0xA0xx)` et un `#pragma parameter` pour le registre de
 * retour. Le probleme n'est donc pas "l'appel manque", c'est "l'en-tete par
 * sujet manque".
 *
 * Presents dans Multiverse.h (verifie) : PutScrap, ZeroScrap, CountADBs,
 * GetIndADB, GetDrvQHdr, PBHGetVInfoSync, NewGWorld, SetGWorld,
 * GetGWorldPixMap, HasDepth, SetDepth, CopyMask, GetDeviceList,
 * GetMainDevice, SecondsToDate, InsTime/PrimeTime/RmvTime.
 *
 * ── Ce qui manque VRAIMENT ──────────────────────────────────────────────
 * Quatre familles sont absentes et ne figurent meme pas dans
 * `needs-glue.txt` : Slot Manager (`SGetSRsrc`), Power Manager
 * (`BatteryStatus`, `GetSleepTimeout`, `GetCPUSpeed`...), `Microseconds`,
 * et `ReadXPRam`. Toutes sont des traps a convention de REGISTRES, pas des
 * appels Pascal ordinaires.
 *
 * Elles etaient toutes eteintes, et pour une raison qui tenait : une
 * convention de registres fausse ne produit pas une erreur de
 * compilation, elle produit un plantage sur la machine de quelqu'un — et
 * rien ne permettait de la verifier avant de l'avoir livree. On ne publie
 * pas un chiffre qu'on n'a pas mesure ; on ne declare pas une trap qu'on
 * n'a pas vue declaree.
 *
 * ── Ce qui a change (2026-08-09) ────────────────────────────────────────
 * `dev/Retro68/InterfacesAndLibraries/` porte desormais les Universal
 * Interfaces 3.4 d'Apple, plus le `Interface.o` de MPW. Trois des quatre
 * familles cessent d'etre des paris : leur declaration se RECOPIE de la
 * source d'Apple, fichier et ligne cites. C'est `compat/prober_traps.h`,
 * et c'est la seule raison pour laquelle les interrupteurs ci-dessous
 * changent de valeur.
 *
 * La quatrieme reste eteinte, et le motif est le meme qu'avant : verifie,
 * cette fois, plutot que suppose.
 */
#ifndef PROBER_COMPAT_H
#define PROBER_COMPAT_H

/* Les declarations elles-memes, avec leur provenance. */
#include "prober_traps.h"

/* ── Slot Manager ───────────────────────────────────────────── ALLUME ───
 * `SGetSRsrc` = moveq #11,D0 ; `_SlotManager` ($A06E) ; SpBlock en A0,
 * resultat en D0 — UI 3.4 `Slots.h` l. 963-967, structure l. 165-191.
 * Reste a confronter a une vraie machine a slots : le code n'a plus a
 * etre devine, mais ses SORTIES n'ont pas encore d'etalon. */
#define PROBER_HAVE_SLOTS 1

/* ── Power Manager ──────────────────────────────────────────── ALLUME ───
 * Temporisations et vitesse CPU : selecteur en D0 puis `_PowerMgrDispatch`
 * ($A09E) — sauf `GetCPUSpeed`, qui passe par `_IdleState` ($A485). Une
 * trap differente pour un appel voisin, exactement le genre de detail
 * qu'on aurait rate en devinant (UI 3.4 `Power.h`).
 *
 * `BatteryStatus` n'est pas une trap inline mais un vrai symbole de
 * bibliotheque : sa glue est transcrite depuis le desassemblage du
 * `Interface.o` d'Apple (`ConvertObj`). Voir prober_traps.h.
 *
 * Sur un Mac de bureau ces traps n'existent pas ; `power.c` conditionne
 * tout a `gestaltPMgrExists` a l'execution. Le seul parc a PMU est le
 * Duo 230 (plate-forme MSC + PG&E) : c'est la que ces lignes se verifient
 * pour de bon. */
#define PROBER_HAVE_POWER 1

/* ── Microseconds ───────────────────────────────────────────── ALLUME ───
 * `_Microseconds` ($A193) + trois mots qui rangent A0:D0 dans un
 * `UnsignedWide` — UI 3.4 `Timer.h` l. 210-211. L'argument passe par la
 * PILE et c'est l'inline qui le depile : aucun `#pragma parameter`, ce
 * qu'on n'aurait pas invente non plus.
 *
 * C'est la deuxieme horloge du controle croise (TickCount = tic 60,15 Hz
 * sur CA1, Microseconds = Time Manager cadence par un TIMER du VIA) :
 * deux generateurs distincts dans POM68K, que rien n'avait jamais
 * confrontes. */
#define PROBER_HAVE_MICROSECONDS 1

/* ── XPRAM ────────────────────────────────────────────────────── ETEINT ─
 * `_ReadXPRam` ($A051) : A0 = tampon, D0 = (nbOctets << 16) | offset.
 * Toujours eteint, mais desormais pour une raison VERIFIEE plutot que par
 * prudence : Apple ne le declare pas non plus. Sur les 431 en-tetes des
 * Universal Interfaces 3.4, `ReadXPRam` n'apparait QUE dans `Traps.h`, et
 * seulement comme numero de trap (`_ReadXPRam = 0xA051`, l. 732) — aucun
 * prototype C, aucune glue dans `Interface.o`. Les Universal Interfaces
 * ne rapprochent donc de rien ici : l'ecrire resterait un pari, au meme
 * prix qu'avant. */
#define PROBER_HAVE_XPRAM 0

/* ── petits manques sans risque ─────────────────────────────────────────
 * `BitMapPtr` et les indices de bits de `gdFlags` ne sont pas declares par
 * multiversal. Contrairement aux traps ci-dessus, ce sont des CONSTANTES
 * documentees (Inside Macintosh: Imaging With QuickDraw, Graphics Devices)
 * et le risque d'erreur est d'une autre nature : un indice faux fait
 * inclure ou sauter un ecran dans l'enumeration, il ne corrompt rien et ne
 * plante pas. On les declare donc, en citant leur provenance. */
#ifndef BitMapPtr
typedef BitMap *BitMapPtr;
#endif

enum {
    gdDevType_    = 0,    /* gdFlags bit 0  : 0 = N&B, 1 = couleur       */
    mainScreen_   = 11,   /* gdFlags bit 11 : l'ecran principal          */
    screenDevice_ = 13,   /* gdFlags bit 13 : c'est un ecran             */
    screenActive_ = 15    /* gdFlags bit 15 : allume                     */
};

/* Bits de `gestaltSoundAttr` (Inside Macintosh: Sound). Meme categorie de
 * risque que ci-dessus : un bit faux affiche une capacite en trop ou en
 * moins, il ne casse rien. */
enum {
    gestaltStereoCapability_    = 0,
    gestaltStereoMixing_        = 1,
    gestaltSoundIOMgrPresent_   = 3,
    gestaltBuiltInSoundInput_   = 4,
    gestaltHasSoundInputDevice_ = 5
};

#endif /* PROBER_COMPAT_H */
