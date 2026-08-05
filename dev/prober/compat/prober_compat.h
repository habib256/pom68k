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
 * ── Ce qui manque VRAIMENT, et pourquoi je ne l'invente pas ─────────────
 * Quatre familles sont absentes et ne figurent meme pas dans
 * `needs-glue.txt` : Slot Manager (`SGetSRsrc`), Power Manager
 * (`BatteryStatus`, `GetSleepTimeout`, `GetCPUSpeed`...), `Microseconds`,
 * et `ReadXPRam`. Toutes sont des traps a convention de REGISTRES, pas des
 * appels Pascal ordinaires.
 *
 * Je pourrais les declarer de memoire. Je ne le fais pas : une convention
 * de registres fausse ne produit pas une erreur de compilation, elle
 * produit un plantage sur la machine de quelqu'un — et je n'ai aucun moyen
 * de la verifier avant de l'avoir livree. Le meme raisonnement que pour le
 * reste de ce programme : on ne publie pas un chiffre qu'on n'a pas mesure.
 *
 * Elles sont donc derriere des interrupteurs, ETEINTS par defaut, avec la
 * glue exacte que chacune reclame ecrite en face. Les rallumer demande un
 * cycle compiler-tester, pas un pari.
 */
#ifndef PROBER_COMPAT_H
#define PROBER_COMPAT_H

/* ── Slot Manager ────────────────────────────────────────────────────────
 * `SGetSRsrc` = trap `_SlotManager` ($A06E), selecteur dans D0, SpBlock
 * dans A0, resultat dans D0. Reclame aussi la structure `SpBlock`, absente
 * de Multiverse.h. A ecrire dans compat/ quand une vraie machine a slots
 * sera disponible pour le verifier. */
#define PROBER_HAVE_SLOTS 0

/* ── Power Manager ───────────────────────────────────────────────────────
 * `_PowerMgrDispatch` ($A09E), selecteur dans D0. Sur un Mac de bureau la
 * trap n'existe pas du tout : la garde `gestaltPMgrExists` protege a
 * l'execution, mais il faut d'abord que ca compile ET que la convention
 * soit juste. A verifier sur un Duo/PowerBook reel. */
#define PROBER_HAVE_POWER 0

/* ── Microseconds ────────────────────────────────────────────────────────
 * `_Microseconds` ($A193), resultat 64 bits rendu en registres. C'est la
 * deuxieme horloge du controle croise (TickCount = tic 60,15 Hz sur CA1,
 * Microseconds = Time Manager cadence par un TIMER du VIA) : deux chemins
 * independants qui doivent s'accorder. Le test vaut la peine ; la glue
 * doit etre juste. */
#define PROBER_HAVE_MICROSECONDS 0

/* ── XPRAM ───────────────────────────────────────────────────────────────
 * `_ReadXPRam` ($A051) : A0 = tampon, D0 = (nbOctets << 16) | offset.
 * Convention documentee et simple, mais non verifiee ici. */
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
