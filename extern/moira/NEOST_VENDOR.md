# Moira — copie vendorisée (fork NeoST)

> **POM68K note (English, 2026-08-12).** This file is **NeoST's** provenance
> record, kept verbatim (in French, as NeoST wrote it) because POM68K's copy of
> Moira inherits its four patches. It describes NeoST, not this tree, so:
>
> - **All four patches below are still present here** — deferred IPL
>   (`iplPrev`/`iplChangeClock`/`iplDelay4`/`iplDelay2`, `setIplDelay`,
>   `pollIpl`, `POLL_IPL` in `MoiraMacros.h:67`), the STOP level-sensitive
>   IRQ re-check (`Moira.cpp:612` — `if (reg.ipl > reg.sr.ipl || reg.ipl == 7)`
>   in the `STOPPED` branch opened at `:587`), the guarded `reset<C>()` whose
>   double fault ends in `halt()` rather than an escaping C++ exception
>   (`Moira.cpp:284-309`), and the 24-bit watchpoint mask
>   (`MoiraDataflow_cpp.h:547`/`:637`). Beyond the four, NeoST also wrapped
>   three `execException` calls on `execute()`'s own paths in `try` →
>   `processException`, so an odd TRACE/PRIVILEGE vector raises an address error
>   instead of escaping the emulator (`Moira.cpp:527`, `:590`, `:635`) —
>   undocumented below but present. Everything NeoST touched carries `NEOST`
>   markers, never `POM68K` ones — `grep -rn NEOST Moira/` finds them, and the
>   two greps are disjoint.
> - **Deferred IPL is dormant in POM68K.** Nothing in `src/` or `tests/` calls
>   `setIplDelay`, so `iplDelay4 == 0` and `pollIpl()` is the plain
>   assignment on every profile. `NEOST_IPLFETCH` was NeoST's env knob and is
>   not read by anything here; `Moira.h:602-604` (`pomJitSimpleIpl`) depends
>   on the feature staying off, since generated code models `POLL_IPL` as the
>   assignment.
> - **`NEOST_EXC_DIAG`** (presence-only, stderr) survives and is the only
>   environment variable the vendored core reads besides the `POM68K_040_*`
>   pair — `Moira.cpp:1257`, `MoiraExceptions_cpp.h:830`,
>   `MoiraDataflow_cpp.h:1040`.
> - Everything else it names — `docs/MOIRA_WINUAE_CONVERGENCE.md`,
>   `tools/make_trace_odd_test.py`, `roms/tos106us.img`, the `Cpu68k::run` STOP
>   guard, the 19/19 Atari etalons, the `trace_odd` gate — is **NeoST-side and
>   does not exist in POM68K**. The rationale is kept because the *why* of each
>   patch is not recoverable from the code; the file paths are not usable.
> - **Its closing instruction does not apply.** POM68K's copy is a fork
>   (`POM68K_VENDOR.md` § *Status*): changes are committed here and documented
>   there, not pushed back to NeoST.

Ce dossier n'est **plus un sous-module Git**. C'est une copie versionnée
directement dans le dépôt NeoST (« vendorisée ») du cœur 68000 cycle-exact
[Moira](https://github.com/dirkwhoffmann/Moira) de Dirk W. Hoffmann (licence MIT,
cf. `LICENSE`).

## Pourquoi vendorisé

NeoST **modifie** des fichiers internes de Moira (cf. patch ci-dessous). Avec un
sous-module, ces edits ne sont pas dans le dépôt parent et sont **écrasés au
premier `git submodule update`** — ce qui s'est déjà produit une fois (fork local
perdu, commits `e4da365`/`a1e52ec` introuvables). La vendorisation fige le code et
le patch dans l'historique NeoST, à l'épreuve du clobber.

## Contenu conservé

Seul `Moira/` (les sources compilées par NeoST), plus `LICENSE`, `README.md` et
`CMakeLists.txt` upstream (référence). L'arbre upstream complet — `Cputester/`
(~713 Mo de corpus ADF), `Documentation/`, `docs/`, `Runner/`, `Moira.xcodeproj/`
— a été **élagué** (inutile au build NeoST, qui ne compile que `Moira/*`).

## Patch local : `NEOST_IPLFETCH` (reconnaissance IPL différée)

Port fidèle de WinUAE `ipl_fetch_next` (mécanisme B). **OFF par défaut**
(`iplDelay4 == 0` ⇒ `pollIpl()` ≡ `reg.ipl = ipl`, byte-identique à l'upstream).
Activé via l'env `NEOST_IPLFETCH=1` côté NeoST. Détails et mesures :
`docs/MOIRA_WINUAE_CONVERGENCE.md` (mécanisme B).

Fichiers touchés vs upstream :
- `Moira/MoiraMacros.h` — `POLL_IPL` → `pollIpl()`
- `Moira/Moira.h` — membres `iplPrev`/`iplChangeClock`/`iplDelay4`/`iplDelay2`,
  méthodes `setIplDelay()` + `pollIpl()`
- `Moira/Moira.cpp` — historisation de la broche dans `setIPL`, règle 3-cas dans
  `pollIpl()`
- `Moira/Moira.cpp` — **gardes d'exception** (2026-07-03) : les chemins TRACE,
  PRIVILEGE-depuis-STOP et LOOPING de `execute()` passent par `processException`
  (un vecteur impair y jette `AddressError` → address error 68000, pas un abort) ;
  `processException` corrige le `throw df` (pointeur jamais rattrapé) et traite
  AddressError/BusError imbriquées comme **double faute → HALT**. Étalon :
  `trace_odd` (`tools/make_trace_odd_test.py`).

## Patch local : STOP niveau-sensible (2026-07-03)

Le 68000 compare IPL/masque **en continu** pendant un STOP (broches niveau-
sensibles). L'upstream ne re-teste `checkForIrq()` que sur `CHECK_IRQ`, posé au
CHANGEMENT de broche — une IRQ levée AVANT le `stop` (masquée par le SR d'alors,
démasquée par l'opérande du stop) n'était jamais re-testée : le CPU dormait
jusqu'au prochain changement de broche. Fix : dans la branche STOPPED
d'`execute()` (Moira.cpp), après `POLL_IPL`, re-armer `CHECK_IRQ` si
`reg.ipl > reg.sr.ipl`. Pendant : garde `!irqDeliverable()` sur le saut
d'attente STOP de `Cpu68k::run` (NeoST) — sans elle l'horloge était téléportée
au prochain événement et l'IRQ déjà prenable partait ~350 cyc trop tard. Cas
mesuré : raster « Timer B + stop #$2100 + HBL » de Super Hang-On (bande blanche
à l'horizon, 3 écritures palette par activation au lieu de 2, dérive +1 ligne
par segment). Étalons 19/19 + EL + Cuddly re-validés après fix.

## Patch local : reset gardé (2026-07-08)

Une bus/address error pendant le fetch des vecteurs de reset (SSP/PC à $0-$7,
ex. image ROM tronquée — `roms/tos106us.img` fait 192 Ko au lieu de 256) fuyait
hors de l'émulateur (`terminate called after throwing moira::BusError`), le
`processException` ne couvrant que les chemins d'`execute()`. Fidèle 68000 :
double faute au reset = **HALT** (cpu_halt(CPU_HALT_DOUBLE_FAULT) chez
WinUAE/Hatari). Fix : `Moira::reset<C>()` (Moira.cpp) enveloppe les
`read16OnReset` + prefetch dans un try → `halt()`. Étalons 19/19 re-validés.

## Patch local : watchpoints masqués au bus 24 bits (2026-07-11)

Le débogueur NeoST pose des watchpoints mémoire via `debugger.watchpoints`. Moira
teste l'accès dans `peekM`/`pokeM` (`MoiraDataflow_cpp.h`) avec l'adresse EA **non
masquée**, alors que l'accès réel juste en dessous utilise `addr & addrMask<C>()`
(24 bits). Un accès I/O en adressage court absolu (`$8001.w` → EA `$FFFF8001`) ne
matchait donc jamais un watchpoint posé sur `$FF8001`. Fix : masquer l'`addr` par
`addrMask<C>()` **avant** `watchpointMatches`/`didReachWatchpoint` (les 2 sites, lecture
et écriture). Court-circuité hors watchpoints (`flags & CHECK_WP &&`) → zéro impact en
marche normale. Vérifié : `--watch FF8001` (via `$8001.w`) et `--watch 10` (RAM directe)
se déclenchent ; self-tests + glue-selftest 31/31 intacts.

## DIVS : valeurs absolues sans comportement indéfini (NeoST 2026-10-20, porté ici le 2026-11-06)

`cyclesDiv<C68000|C68010, DIVS>` et `setUndefinedDIVS` calculaient `abs(dividend)` sur un
`i32` : pour `0x80000000`, `abs(INT32_MIN)` est indéfini. clang et GCC prenaient des branches
différentes pour `0x80000000 / 0x8000` (banc `cpu_div_ram` de NeoST : vert sous macOS,
Δ = +136 cycles sous Linux). Les valeurs absolues sont désormais calculées en non signé ;
|−2³¹| >> 16 = `0x8000` ≥ `0x8000` → débordement, comme `getDivs68kCycles` d'Hatari.

*(Texte de NeoST, commit `74b2f2d`. C'est le premier correctif NeoST postérieur à la copie
du 2026-07-11 à être rétro-porté ; il est marqué `POM68K` dans le source et porte le rang 38
de `POM68K_VENDOR.md`. Les autres sont listés dans `TODO.md § Moteur › Convergence`.)*

## Préfetch après écriture de SR/CCR, et cycles internes de DIV (NeoST 2026-10-19/20, portés ici le 2026-11-06)

Trois groupes de NeoST, rang 40 de `POM68K_VENDOR.md`, marqués `POM68K` dans le source :

- **MOVE to SR** (`execMoveToSr`, NeoST `ce1cc52`) : `fullPrefetch<POLL>` remplace la
  lecture fictive de `pc+2` suivie de `prefetch`.
- **ANDI/ORI/EORI #,SR et #,CCR, MOVE to CCR** (`execAndisr`, `execAndiccr`,
  `execMoveToCcr`, NeoST `d8d33bb`) : même correctif. La [transcription du
  microcode](https://og.kervella.org/m68k/all.txt) donne la queue `1cc stiw1` →
  `321 stiw4` → `1e1 malw3` → `363 b` → `34c mmrw3` : accès pc0+4, **pc0+4**, pc0+6 au
  lieu de pc0+4, pc0+6, pc0+6 ; IRD reçoit le mot relu APRÈS l'écriture de SR/CCR, en
  espace PROGRAMME. Hatari CE fait de même. Nombre de cycles inchangé.
- **DIVU/DIVS** (`execDivuMoira`, `execDivsMoira`, NeoST `262f63a`, amont `f90fc50`) :
  sur 68000 les cycles internes précèdent `writeD` et le préfetch. Chez POM68K la
  sortie anticipée du débordement de DIVS (16/18 cycles, SingleStepTests) est gardée :
  le quotient est calculé d'abord.

*(Ce que POM68K ajoute à la preuve de NeoST : les listes de transactions du corpus
SingleStepTests/680x0 montrent exactement ces séquences sur tous les vecteurs sans
exception, et `sst68000` compare désormais les cycles bus. Restent à porter : les
quatre hooks — `illegalOpcodeHook`, `willProcessFault`, bornes de cycle bus,
`setBusLock` — et STOP+8 ; voir `TODO.md § Moteur › Convergence`.)*

## Hooks d'hôte : opcode illégal, faute, verrou de bus (NeoST 2026-10-01 et 2026-10-19, portés ici le 2026-11-06)

Rang 42 de `POM68K_VENDOR.md`, marqués `POM68K` dans le source, inertes par défaut :

- `illegalOpcodeHook(u16)` (NeoST `fdbce2e`) : appelé par `execIllegal` avant
  l'exception ; `true` = opcode consommé comme un NOP. NeoST y sert ses appels hôte
  GEMDOS (`$0008`-`$000A`).
- `willProcessFault(const StackFrame&)` (NeoST `ce1cc52`) : la faute en cours de
  traitement, y compris celle qui interrompt une exception (double faute → HALT).
- `setBusLock(bool)` (NeoST `ce1cc52`) : /RMC du TAS — `true` avant la lecture,
  `false` après l'écriture, et aussi si l'une des deux lève une exception.

*(Non portées : les bornes de cycle bus `willAccessBus`/`didAccessBus`, deux appels
virtuels par accès — `TODO.md § Moteur › Convergence`. POM68K ajoute
`extBusError000(addr, write)`, qui lève la `moira::BusError` de groupe 0 avec la
recette de `Cpu68k::raiseBusError` de NeoST : un hôte 68000 n'a plus à construire
la trame lui-même.)*

*(NeoST's original closing line — "toute modif future d'un fichier `Moira/` se
commit normalement dans NeoST" — is superseded in this repo: see the POM68K
note at the top.)*
