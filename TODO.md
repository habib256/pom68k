# TODO

**Active work only.** Resolved work, investigation trails and design rationale
live in `CHANGELOG.md` (`CHANGELOG_INDEX.md` groups its 254 dated entries by
subsystem), implementation detail in `DEV.md`, vendor notes in
`extern/*/POM68K_VENDOR.md`, LLE inventory in `docs/LLE_VS_HLE.md`, JIT design
in `src/jit/POM68K_JIT.md`, conformant-JIT plan in `docs/JIT_BRINGUP.md`.

**Counts re-verified against `CMakeLists.txt` and the code on 2026-08-17** —
re-verify before quoting them anywhere:

- **The gate registry is host-conditional, so a single number is always wrong
  somewhere.** The documented registry (2026-08-22): **227 gates** — 109
  `unit`, 83 `asset-none`, 9 `smoke`, 40 `jit`, 51 `m040`, 53 `m030`, 118
  `etalon` (12 of them `etalon-core`). Five are host-conditional — the
  AArch64 trio `jit_lockstep_a64_coarse_test` +
  `jit_lockstep_030_a64_experimental_test` +
  `jit_lockstep_030_a64_alignment_test` (the first also joins `smoke`) and
  the x86-64-only `jit_lockstep_030_x64_experimental_test` +
  `jit_lockstep_030_x64_alignment_test` — so an x86-64 configure sees
  **224** (106 `unit`, 8 `smoke`, 37 `jit`) and an AArch64 one 225 (107
  `unit`, 38 `jit`). Eight more exist only under `-DPOM68K_PRODUCT_LLE_GATES=ON`
  (default OFF, `CMakeLists.txt:425`, and it FATAL_ERRORs off AArch64).
  `unit` is *not* "asset-free" — it remains the legacy "name does not end in
  `etalon`" label. `asset-none` is the manifest-declared daily tier.
- **Last FULL suite: 222/222 on 2026-08-18** — the whole registry on this
  host and the first PARALLEL whole-registry run the project has quoted:
  208 gates at `-j16` in 1 h 27 on the calibrated slot budgets, then the two
  families the RAM model cannot see (Q700/Eclipse, which share `Q700Memory`,
  and the UDP-port AppleTalk gates) serialized in 43 min, 14/14 — 2 h 11
  total against the 4 h 30 sequential habit, on relinked binaries with the
  freshness guard green before the tier (`CHANGELOG.md` 2026-08-18 (fourth)).
  The previous claim here was 206/206 on 2026-08-16, 4 h 30 sequential; the
  same day's earlier run of the same tiers found **ten** reds in five
  unrelated causes (`CHANGELOG.md` 2026-08-16) — which is what a suite that
  has not been run whole for nine days looks like.
  **The `make` is part of the claim, not the decor**: an earlier 143/143 in
  August ran over binaries linked at different times and proved nothing —
  `ctest` does not compile. A phantom failure gets investigated; a phantom pass
  gets quoted. The same freshened tree's first tier run then found a red the
  stale one had hidden for a day (`sst68030` 3068/3082, closed by ruling
  D23).
- **37 machine profiles** = 37 `kMachineProfiles` rows in
  `src/MachineCatalog.h`, each carrying its stable `SnapMachine` id = 21
  `MachineKind` values over **12** platform implementations. `docs_test`
  compares the compiled catalogue directly with `CLAUDE.md`. These numbers
  describe current coverage; they do not close the all-68k-Macintosh target.

House rule for this file: an item earns its place by saying **what to do next**,
concretely. When it lands, it moves to `CHANGELOG.md` and leaves at most one
line here.

---

## 0. Séquencement de la consolidation architecturale — ACTIVE

**Décision utilisateur clarifiée, 2026-08-17.** La finalité de POM68K est le
support de **tous les Macintosh 68k** ; ce périmètre produit n'est pas
négociable. Les **37 profils / 12 plateformes** sont l'état courant de la
matrice, jamais son plafond. Pendant ce cycle, la convergence JIT et les gates
qui la prouvent passent avant l'ouverture simultanée de nouveaux fronts
machine ou périphérique : c'est un ordre d'exécution temporaire, pas un gel de
la couverture cible.

Ce qui reste autorisé et prioritaire : corrections de conformité, réduction
du delta entre interpréteur et JIT, consolidation d'une abstraction existante,
tests asset-free, budgets de performance, portabilité A64/x64, documentation
et suppression de code mort. Un nouveau modèle reste légitime dès qu'il ne
dilue pas ces sorties prioritaires et qu'il apporte ses contrats et gates.

La fenêtre se ferme seulement quand les quatre sorties suivantes sont vraies :

- [x] toutes les formes réellement émises par A64/x64 consomment les contrats
  `Instr::semantics` et `Instr::memory` : famille, ALU, taille, direction,
  accès, ordre et restart ne sont plus redécodés par backend ;
- [x] `jit-fast` exécute un lockstep synthétique natif, déterministe et sans
  assets sur les CI A64 et x64, avec `/BERR` restart et last-write ;
- [x] le fork Moira possède une frontière `pom*`/`POM68K` et un inventaire
  recalculés par `docs_test` ;
- [x] les budgets structuraux, de part native et de fallback sont versionnés
  dans `performance_budgets.tsv`, séparés par hôte et consommés par les gates
  `jit-fast` ; A64/x64 publient le même JSON structuré ;
- [x] les extensions indexées 68020 full et les transferts de contrôle ont un
  plan IR commun ; les backends les refusent sans les redécoder tant que leur
  lowering natif n'est pas prouvé ;
- [ ] ajouter les bancs Macintosh à budget de cycles fixe pour 68000 et 68020.
  Les seuils représentatifs versionnés couvrent déjà Q605/68040 et
  LC II/68030 sur l'hôte x86-64 de référence et sur Apple M4 ; aucun seuil
  des deux familles restantes n'est extrapolé ;
- [ ] le palier legacy `unit` n'a plus d'échec inexpliqué sur les deux
  architectures hôtes. **Moitié x86-64 mesurée le 2026-08-17** (GCC 13,
  conteneur neuf, arbre reconstruit from scratch — `make -j4`, exit 0,
  152 binaires, aucun à 0 octet) : `asset-none` **83/83** en 4,49 s sans un
  seul SKIP, `unit` **104/104** en 5,34 s, **zéro échec**. Ce que ce vert ne
  dit pas, et qu'il faut lire avec lui : sur les 21 gates de `unit` hors
  `asset-none`, **20 sont des soft-skips** faute d'assets privés (ROMs, images
  disque, dumps MCU, et les données de fuzz 030/040 qui se génèrent avec
  l'oracle). Un seul tournait vraiment, `savestate_68k_test` — un palier
  « vert » de 104 dont 84 s'exécutent. Depuis, **`sst68000` aussi** : ses
  vecteurs sont le seul asset manquant qui soit public, ils ont été
  récupérés, et le gate donne **1 000 058 / 1 000 058 sur 124 fichiers** en
  6,67 s. Reste à faire pour cocher : rejouer le palier sur un hôte x86-64
  *portant les assets*, et la moitié AArch64.
  **Prérequis levé le même jour** : sur cet hôte l'arbre ne se construisait
  pas du tout (LTO + `ld.lld`, `CHANGELOG.md` 2026-08-17 (later)) — la case
  était inatteignable, pas seulement non mesurée.

`docs_test` vérifie la cohérence dynamique du catalogue compilé, des gates,
des budgets et de la documentation ; il ne doit contenir aucun nombre plafond qui transforme par
accident l'état courant en restriction produit.

---

## 0·A. Direction produit — la vitesse, et l'ordre dans lequel on la paie

**Décision utilisateur, 2026-08-09.** Le problème n°1 de POM68K est sa
**vitesse d'exécution**. Toutes les machines doivent être *utilisables*, quitte
à perdre la conformité sur les plus puissantes. L'échelle voulue :

| Classe | Contrat | État constaté |
|---|---|---|
| **68000 compacts** (Plus & co) | **LLE complet, conformité non négociable.** Un Raspberry Pi 400 suffit et doit suffire | tenu |
| **68020** (Mac II & co) | conformant | la fenêtre ne vaut que ×1,0-1,2 (pas d'ATC à sauter) |
| **68030** (LC II, 15,67 MHz) | utilisable sur Pi 400 | rapporté ~×1,3 en turbo ; **mesuré 2026-08-09 : ×1,98 interprété, ×2,40 moteur allumé** (thread machine, budget fixe). L'écart avec le ×1,3 n'est expliqué ni par le CPU ni par le raster — voir plus bas. **Aucun chiffre sur Pi 400** |
| **68040** (Centris, Performa, Quadra) | utilisable | **mesuré 2026-08-09 : ×0,79 interprété — ×1,73 moteur allumé.** « Ne suit plus » était vrai de la configuration par défaut de l'époque ; depuis le 2026-08-10 le JIT **est** le défaut sur 68040, donc ce n'est plus vrai de la machine livrée |

**L'ordre est fixé et il n'est pas négociable :**

> **1. D'abord épuiser toutes les accélérations possibles en LLE et en JIT
> conformant. 2. Ensuite seulement, ajouter du HLE et du JIT non conformant.**

C'est la règle § *Principle* de `docs/LLE_VS_HLE.md` appliquée à la
performance : le raccourci ne se mesure et ne se valide que contre une
référence conforme qui existe déjà. Concrètement, **§ 3 est le chemin critique
du projet**, et l'item *Optional HLE acceleration overlay* (§ 8) est
explicitement **bloqué derrière lui**.

### Ce que la bascule non conforme rapportera — estimation, à ne pas citer comme mesure

Base **mesurée** (`POM68K_JIT.md` § 3.4, budget fixe de 3 000 frames sur Q605,
empreinte identique sur les trois moteurs) : interpréteur 48,51 s →
`threaded` 28,10 s (**×1,73**) → `x86-64` 9,71 s (**×5,00**). Le ×2,68 du
`q605_boot_etalon` ne sert PAS de base : un etalon s'arrête à la signature
Finder, donc les deux moteurs y sont chronométrés sur des quantités de travail
invité différentes. *(Les ×1,60-1,69 / ×2,09-2,18 que cette section a longtemps
cités sont antérieurs au 2026-07-31 et explicitement périmés par § 3.4 — trois
émetteurs, deux cellules de table de coût et la suppression du flush DTLB à
l'armement les séparent.)*

| Levier | Gain attendu sur le JIT actuel | Statut |
|---|---|---|
| ~~Soft TLB / ATC relâché~~ | **≈3 %** | **MESURÉ et RÉFUTÉ 2026-08-09** (`POM68K_JIT_WINDOW_KILL`, `POM68K_JIT.md` § 3.6). Une sortie de fenêtre coûte **42,9 ns** (LC II/`threaded`, R² = 0,990) et **120,4 ns** (Q605/x64, R² = 0,961) ; aux taux naturels de chaque famille cela fait **3,5 %** et **3,2 %** du run. Le « +10 à 30 % » multipliait un taux réel par une intuition. Ne pas le remettre dans un plan de perf |
| Longues traces, contrat par instruction abandonné | +10 à 20 % | estimé ; le block linking a déjà pris le gros (entrées −53 %, 268 → 566 instr/entrée) |
| Interruptions + temps grossiers | +0 à 10 % | estimé, et **la relaxation à laquelle ce tree est le plus fragile** (voir plus bas) |
| ~~Lazy flags~~ | **≈0,8 %** | **MESURÉ et abandonné** — voir § 3 *Measured and DROPPED*. Ne pas le remettre dans un plan de perf |

Composé : **×1,3 à ×2,0 sur le JIT actuel** — appliqué à la base ×5,00
ci-dessus, de l'ordre de ×6,5 à ×10 face à l'interpréteur, mais le rapport qui
compte ici est le premier, pas l'absolu. Sacrifier toute la conformité CPU rend
donc de l'ordre de **+50 %** — ce n'est pas un changement d'échelle. Raison
structurelle : le
générateur x64 *est déjà* un vrai JIT (code machine émis, DTLB inline,
transferts de contrôle compilés comme terminateurs de bloc) ; les relaxations
d'un JIT 68k classique servent surtout à *atteindre* cet état, et on y est
déjà, en payant l'exactitude. On ne rachète que la taxe.

**Le changement d'échelle, s'il existe, est dans le HLE, pas dans le JIT.**
La liste des instructions non couvertes est menée par les décalages line-$E et
les modes indexés 68020 — *ce dont sont faits les blitters de QuickDraw*
(§ 3, *Coverage tail*). Un JIT exécute ces boucles plus vite ; un
HLE QuickDraw ne les exécute pas du tout. C'est le seul levier dont le gain
n'est pas borné par le taux d'instructions — et le plus invasif.

### Deux garde-fous à poser AVANT la première ligne de code non conforme

1. **Ne jamais relâcher l'horloge périphérique/MCU.** Relâcher l'exactitude
   côté CPU, oui ; le temps vu par le VIA, l'Egret/Cuda, l'IWM/SWIM reste sur
   le compteur machine. Ce tree a trois cicatrices qui disent pourquoi : la
   Mac TV deadlocke sur **2 %** de dérive du taux d'instructions MCU ; le boost
   i-cache comprimait le dénibblage sous le hold de 14 ticks de l'IWM →
   `badDCksum` (d'où le gel du boost pendant que le moteur tourne) ;
   `CudaLle::tick` doit reporter son dépassement en `mcuDebt_` sous peine de
   suroverclocker le MCU de ~37 %.
   Le **temps grossier est la relaxation la plus dangereuse ici**, pas la plus
   rentable — la prendre en dernier, machine par machine.
2. **L'instrument de mesure ne survit pas au mode non conforme.** Chaque
   chiffre de la table § 3 a été pris avec les trois moteurs imprimant la
   **même empreinte** d'état architectural. Un profil relâché imprimera une
   empreinte différente **par construction**. Il lui faut donc son critère
   d'équivalence *fonctionnel* (atteint le Finder, lit le bon bloc) **avant**
   toute mesure, sinon « plus rapide » et « cassé mais rapide » deviennent
   indiscernables — le mode d'échec que ce projet a déjà payé plusieurs fois.

### Forme visée (pas un booléen)

Le mot est « **progressivement** » : un **profil de fidélité gradué** avec des
défauts **par famille de machine** (compacts verrouillés au profil conforme ;
030 et 040 relâchés par cran), pas un interrupteur conforme/non-conforme. Le
*purity mode* de `docs/HLE_OVERLAY.md` § 7 s'applique tel quel : tous les
gates d'accuracy forcent le profil conforme, `abort()` si quelque chose tente
de l'armer.

### Ce qui manque pour transformer tout ceci en plan

- [ ] **Une ligne de base sur le matériel cible.** Aucun chiffre POM68K
  n'existe sur un vrai Pi 400 (§ 3, *Build recipe*) et « inutilisable » n'est
  pas une cible tant qu'on ne sait pas *combien* il manque. Mesurer avec
  `jit_bench` **ou `jit_bench_lcii`** (`POM68K_BENCH_FRAMES`), **jamais** un
  boot etalon. Les deux impriment un **× temps réel**, seule forme du chiffre
  qui se compare d'un hôte à l'autre — et la seule qui se compare à la cible,
  qui est « ×1 ou mieux », pas « n secondes ».
- [ ] **Où passe l'écart ×1,98 (mesuré, thread machine) → ×1,3 (rapporté,
  GUI) ?** Innocentés : le cœur CPU, le raster, et le découpage en tranches du
  quantum GUI (`runQuantumWithWire`, `main.cpp:238`, 64 tranches quand
  AppleTalk est actif — `POM68K_BENCH_SLICES` le chiffre à **2,2 %**, et 64
  tranches ne coûtent pas plus qu'une). Restent, dans cet ordre : le `tick` du
  hub AppleTalk par tranche (il vit dans `main.cpp`, donc aucun bench ne
  l'atteint aujourd'hui), le thread GUI lui-même (`renderFrame` + upload GL +
  ImGui à 60 Hz), le chemin audio — et l'hypothèse que le ×1,3 soit une
  impression. La jauge GUI existe désormais (voir ci-dessous), donc l'écart est
  observable des deux côtés.
- [ ] **Rejouer l'A/B sur un vrai Pi 400** : même instrument que la case 1.

**La jauge de vitesse du GUI a atterri** : le menu CPU affiche le ratio temps
réel sur les douze familles, calculé côté GUI à partir de `machineClock()`
publié séparément du `getClock()` boosté (deux échantillons à 500 ms d'écart,
divisés par `mem.cpuHz()`), sans toucher ni scheduler ni périphériques ;
`machinehost_test` verrouille que la source est bien l'horloge machine.
**Mesure GUI 2026-08-11**, quatre copies jetables du même disque 8.1, moyenne
des dix derniers échantillons : Q605 **×2,596**, Q630 **×2,274**, Centris 650
**×3,448**, Q700 **×3,394** — application complète (thread machine, rendu,
audio, hub), pas le débit isolé de `jit_bench`.

### Passe perf 2026-08-11 — ce qui a été promu, et tout ce qui a été rejeté

> **Ce bloc est le seul enregistrement de cette journée : `CHANGELOG.md` n'a
> aucune entrée datée du 2026-08-11.** Ne pas le supprimer sans l'y verser
> d'abord. Toutes les mesures : Q605/AArch64, 6 000 frames fixes sauf mention,
> empreinte `f8e91527781ede67` et 1 410 142 343 instructions retirées comme
> invariant de non-régression.

**Promu :**

- **Table de chaînage 4 096 → 65 536 destinations** (1 Mio par CPU). Elle
  collisionnait bien avant le plafond de 65 536 blocs résidents : `block end`
  149 265 073 → 72 507 478, 22,62/22,39 s → **21,27/21,41 s** (≈5,2 % répétés).
- **Échéancier événementiel, Q605/SCC.** `Q605Memory` accumule une dette
  explicite en cycles machine, la soustrait de `cyclesToNextEvent()` et la vide
  exactement à l'échéance, avant tout accès MMIO ou toute injection LocalTalk
  externe. La dette fait partie de `visit()` et du hash lockstep ; le format de
  snapshot passe à la **version 2** pour qu'un ancien Q605 sans cette phase ne
  soit jamais accepté comme complet. **21,40-21,49 s** contre 21,94-22,06 s
  (≈2,5-3 %), tous compteurs identiques. Override : `POM68K_Q605_EVENT_SCC=0`.
- **Échéancier événementiel, Q605/53C96.** Même contrat, dette 25 MHz propre,
  consommée avant chaque registre MMIO, fenêtre pseudo-DMA, lecture VIA2 des
  lignes IRQ/DRQ et accès externe. Le gate scheduler prouve que le flush MMIO
  SCSI ne consomme pas la dette SCC. Deux paires A/B de 18 000 frames :
  **91,43 s** contre 92,14 s en moyenne (**−0,77 %**), empreinte
  `7817661fd1097608`. Override : `POM68K_Q605_EVENT_SCSI=0`.
- **`B592` (`EOR.L D2,(A2)`) sort de la garde de store conservatrice AArch64**,
  seul opcode libéré ; **16 776 916** fallbacks supprimés, **20,99/21,01 s**
  contre 21,24/21,43 s. `POM68K_JIT_A64_STORE_GUARD_OPCODE=0` restitue l'oracle
  précédent et permet d'évaluer un candidat à la fois — `2F40`, `42A7` et
  `2F0C` ont déjà été testés : conformes en lockstep et sur 6 000 frames, mais
  **aucun gain reproductible**, ne pas les reprendre comme candidats.
  Le gate SMC AArch64 prouve les deux côtés du contrat : masque nul → chemin RAM direct ;
  destination dans la tranche traduite → mémoire exacte, quatre octets
  modifiés, bloc invalidé une fois, état frontière identique à l'interpréteur.
  *(Seul élément de cette journée qui soit dans `CHANGELOG.md` — 2026-08-12.)*

  **Supplanté le 2026-08-20 :** le fixup exact est maintenant global et le
  sélecteur d'opcode a été supprimé. Les gates RAM/SMC, lockstep 040/030 et le
  budget Q605 gardent l'empreinte, les cycles, SCSI et le PC identiques ; le
  débit passe d'une médiane 5,32 s à 3,61 s sur 3 000 frames. Cette nouvelle
  preuve annule la restriction « B592 seul » sans affaiblir la garde.

**Rejeté — ne pas rouvrir sans données nouvelles :**

- **Échéancier événementiel sur l'ASC** : sept gates verts, mais **régression
  de débit** (21,80/22,35 s contre 21,72/21,86 s) — entièrement retiré, selon
  le critère de promotion. C'est le premier candidat qu'on croit évident quand
  on lit « étendre à un troisième périphérique » plus bas : il a déjà été fait.
- **Index secondaire ATC** (22,66 s, régression), **inlining forcé du lookup**
  (sous le bruit), **hook direct MCU→ADB** (22,00/21,91 s), **retours anticipés
  du scheduler** (21,81/21,95 s), **garde code à 128 octets** (21,89/21,93 s,
  plus de churn).
- **Seuil `codeMask` à 128 octets** : gates verts, empreinte inchangée, mais
  21,71/21,72 s et **exactement 162 738** écritures dans du code traduit,
  comme le masque 256 octets — *aucune collision observable supprimée*.
  Descendre sous 128 imposerait d'agrandir l'entrée DTLB ou d'ajouter un bitmap
  vivant au chemin de store, sans signal de rendement.
- **Dette DAFB sérialisée** : bit-exacte, gates verts, mais deux paires de
  18 000 frames se contredisent (91,35 vs 91,21 s, puis 91,26 vs 91,64 s).
  Gain moyen 0,13 %, sous le bruit. **Ne pas porter à Centris/Q700 sans un
  nouveau profil montrant un coût DAFB significativement plus élevé là-bas.**
- **Cache d'échéance SCC** (deadline mémorisé comme état strictement dérivé,
  invalidé par reset/chargement/MMIO/accès externe mutable, avec mode oracle
  recalculant `Scc8530::cyclesToNextEvent()` à chaque hit) : court neutre
  (21,96 vs 21,93 s), longs non reproductibles et contaminés par le régime
  thermique (91,30 vs 94,36 s, puis 94,24 vs 93,39 s en ordre inversé).
- **Correction globale du coût AArch64 `abs.W`** (3 → 2, comme x86-64 et la
  colonne 68020 de Moira) : les 14,77 M cas `MOVE -> abs.W` (`21DF` + `21CF`)
  passent de « unsupported » à « runtime » sans réduire le total ; A/B court
  19,99 vs 20,38 s mais A/B long strictement neutre (**56,24 vs 56,23 s**).
- **Correction globale du fixup `CBZ/CBNZ`** (il encode w9/x14 quel que soit le
  registre demandé, donc la garde teste l'adresse et refuse les stores même
  avec un masque nul) : 826 556 495 instructions, SCSI = 0, empreinte
  divergente `35cb722024e28325`. **Le défaut cache des stores natifs encore
  non conformes ; il ne peut pas être simplement « réparé ».**
  **Supplanté le 2026-08-20 :** après les corrections ultérieures du cache et
  des écritures, la correction globale passe désormais les oracles longs ;
  elle est promue avec des fixups qui conservent explicitement le registre.

**Attribution, faite et à ne pas refaire :**

- Census exact des fallbacks (`POM68K_JIT_HISTO=1`, qui sépare statique et
  runtime par opcode, affiche les modes source/destination de MOVE, l'EA de
  MOVEM et les couples dominants) : **219 277 316** fallbacks = 46 167 498
  statiques + 173 109 818 runtime. `MOVEM -(A7)` est **déjà natif** et pèse
  17,31 M refus runtime — ce n'est pas une lacune d'opcode.
- Attribution runtime complète, cause + opcode + adresse logique + R/W +
  largeur + masque conservés jusqu'au fallback final (un MMIO servi par le
  thunk exact n'est pas compté) : **140 982 525 / 140 982 525** attribués —
  garde de store conservatrice **128 824 006 (91,38 %)**, vrai `codeMask`
  **9 044 353 (6,42 %)**, non-plain/MMIO **2 972 353 (2,11 %)**, fill/tag
  **96 953 (0,07 %)**, franchissement 4 Kio **44 860 (0,03 %)**. L'ancien
  « 68,84 % de `codeMask` » était l'artefact du fixup `CBZ/CBNZ` ci-dessus.
- Les vraies collisions sont très concentrées : page logique `$01F56`,
  tranche 1, masque `$FFFE`, pile autour de `$01F56100-$01F5618E` — le store
  touche réellement une tranche contenant du code traduit, **ce n'est pas un
  faux voisin à relâcher**. Les franchissements de page sont dominés par la
  pile `$000ADFFC` (`4CDF` lecture 16 o : 16 716 cas ; `48E7` écriture 16 o :
  5 287) puis les accès 4 octets à offset `$FFE` : trop rares pour être le
  prochain levier.
- Profil hôte après SCC + 53C96 événementiels (11 243 échantillons) :
  `mmu040Translate` 1 683, `Q605Memory::tick` 393, `M68hc05::run` 319,
  `Dafb::tick` 77, `Via6522::tick` 27, `AscIosb::tick` 23.
- L'échéance périphérique AArch64 en ligne est le bon défaut : 3,05 s contre
  3,66 s avec `sync` à chaque instruction sur 2 000 frames (**≈20 %**).
  **Ne pas grossir le pas périphérique moyen de 19,11 cycles machine : il est
  observable.**

**La suite conforme, dans l'ordre :** étendre l'échéancier événementiel à un
troisième périphérique Q605 — *seulement* après avoir ajouté son flush MMIO et
son état de dette au même ensemble de gates — puis isoler par opcode les stores
masqués-nuls libérés par le vrai test du masque, sous gate SMC + lockstep +
boot JIT/interpréteur + tier étalon complet. Toute promotion exige les quatre
mêmes preuves : empreinte et compteurs identiques, gain **répété**, gates
ciblés verts, tier `etalon` complet vert.

---

## 1. Red now

*(The `sst68030` 3068/3082 that stood here for a few hours on 2026-08-18 —
14 FPU vectors red under a day-old STALE binary, bisected to `90f5f56` —
is closed the same day by **ruling D23** (`oracle/fuzz/disputes/NOTES.md`):
the oracle replay (`oracle/fuzz/replay030.py`, 2042/2042 pinned finals
reproduced bit-for-bit) proved the 2026-08-17 merge had overruled the
oracle on spec alone, twice — the 030 post-instruction frame is format $3
with EA on the 030 too, and FRESTORE's version-$41 hack is FPU-model-
independent. Both reverted, `fpu_sanity` check 14 re-pinned; 3082/3082,
`sst68040` 7200/7200 untouched.)*

*(The dirty-volume refusal that stood here — "on `GISTPERSO-boot.vhd`,
clearing the volume's clean-unmount bit is enough to stop the IIfx booting
it" — is closed, 2026-08-13 (seventh): `iifx_persist_etalon` is green. It
was never the mount path: 7.6-FR's not-cleanly-unmounted path tears the
video driver down and reinstalls it mid-boot, and `TobyVideo` swallowed the
teardown's VBL disable (a synthetic-decl-ROM-era guard), so the ROM's
level-2 dispatcher recursed on an unserviceable slot 9 until the stack had
eaten 6 MB of heap. "Stopped at pc=$40843B22" was the serial-monitor
tombstone, and "jumps to address 1" was open bus with a wrapped PC. The
controls, the dead leads and the storm anatomy: `CHANGELOG.md` 2026-08-13
(seventh).)*

*(The two beyond-boot reds that stood here — the Duo's frozen System clock
and "Cmd-N never reaches the Finder on the three non-Egret/Cuda input
paths" — are closed, 2026-08-13 (sixth). Neither was what it said it was:
the Duo was not missing a one-second source, it was DEAD (a `power_cycle_w`
stub left the ROM spinning at `bra.b *` with the interrupt mask at 7, 58 s
after boot), and Cmd-N always worked — screen dumps at the gesture's peak
show the folder on the desktop of every one of the three. The KeyMap
evidence in that entry was correct; the conclusion drawn from it was not.
Full account: `CHANGELOG.md` 2026-08-13 (sixth).)*
- *(CLOSED 2026-08-15 — `macii_boot_etalon`, and `iix_`/`se30_` with it,
  which were red for the same reason and were not noticed. Not a
  regression: `hdv/HD20SC.vhd` had its `drVolAtrb` bit 8 SET on
  2026-08-14, and the System skips the scavenge on a volume it finds
  clean — 295 SCSI commands dirty, 178 clean, identical desktop and
  identical `vramWrites`. The floor of 200 sat between them. Lead (a) in
  the old entry — "the image is not the one the floor was calibrated on"
  — was **right**, and had been closed on the wrong argument: it was
  checked for probe-chain membership, never for content. The three gates
  now assert `CurApName == "Finder"` + a menu-bar light run instead of a
  command count — `tests/FinderSignature.h`, `CHANGELOG.md` 2026-08-15.)*
- *(CLOSED 2026-08-15 (third) — `lcii_floppy_etalon`, red since 2026-08-14
  and **found from a GUI field report**, not from the suite: "les disquettes
  ne se montent plus", the System offering to initialize a perfectly good
  800K disk. The IWM's cell window was being counted in a C7M-sized unit on
  boards whose chip is clocked at C15M, so the window was 2.3× the cell and
  nothing framed — `.Sony` gave up with -67 noAdrMkErr, on **nine
  platforms**; the compacts were the only machines that could still read a
  floppy, which is why `disk_boot_etalon` stayed green and hid it.
  `Iwm::clockTick()`, and `iwm_read_test` now carries the C15M + mode-`$17`
  combination that had no coverage. `CHANGELOG.md` 2026-08-15 (third).)*
- *(CLOSED 2026-08-16 — the three CD gates, `q605_cdrom_`/`cdboot_`/
  `cdhot_etalon`. Not the dirty 8.1 volume the old entry pointed at, and not
  a CD-ROM defect at all: `git bisect run` over 136 commits named `a355561`
  (2026-08-08), and what it exposed was **`Ncr53c96` dropping bytes a driver
  had already put in the FIFO** — twice, on the two MODE SELECTs Mac OS 8.1's
  CD driver issues while adopting a disc. A preload before the Transfer Info
  went into `fifo_` and stayed there; and a POLLED Transfer Info sized itself
  from `tcount_`, a DMA register still holding an earlier command's 16. Both
  ended with the driver and the chip waiting on each other — R_STATUS polled
  ~600 times a frame, no further CDB, forever. `CHANGELOG.md` 2026-08-16.)*
- *(CLOSED 2026-08-16 — `se_`/`sefdhd_`/`classic_`/`jit_classic_boot_etalon`,
  `FAIL: ejected`. The Mac SE's IWM is clocked at **C15M**
  (`mac128.cpp:1317`, `IWM(config.replace(), m_iwm, C7M*2)`, inherited by
  macsefd/macclasc) while its CPU stays at C7M, so `Iwm` needed the two
  clocks split — `setTickHz` vs `setChipHz`. Same defect as 2026-08-15
  (third) on the Mac II family, on the three machines nobody re-ran because
  `disk_boot_etalon` is a Plus. Also closed the same day:
  `q605_savestate_etalon` (one byte in 33 MB — Moira's `cp`, a per-instruction
  cycle counter the JIT does not maintain; save-state **v9** drops it),
  `iisi_persist_etalon` (the leg now runs the shared `BeyondBoot.h` engine,
  with Cmd-Shift-3 as the flush stir) and `cclassic2_boot_etalon` (its
  desktop-weave criterion was measuring a window the volume's saved layout
  opens; `FinderSignature.h` terms instead). `CHANGELOG.md` 2026-08-16.)*
- **A hot-inserted disc mounts nothing on a guest with no CD stack** — not a
  defect, and written here so it is not re-investigated: nothing binds a
  driver to that SCSI id at boot. The reporter's System 7.5 volume has
  `Apple CD-ROM` and `ISO 9660` but **no `Foreign File Access`**, and Mac OS
  mounts no disc without the dispatcher. Mac OS 8.1 has it and
  `q605_cdrom_etalon` gates that path. Same for `ER`/512 discs whose driver
  partition is a CD driver (`The_Yukon_Trail.cdr`): they do not mount even
  attached as a plain SCSI disk. `CHANGELOG.md` 2026-08-15 (fifth).
- **`tests/finder_boot_matrix.cpp` still carries the same trap**: its Mac II
  leg asserts `scsi().commands > 500` (`:207`) over whatever image the sweep
  passes it. Not a registered CTest, so nothing is red today, and it was
  left alone rather than fixed blind — the sweep needs every OS image to
  re-calibrate. Next: run the sweep, then give it `FinderSignature.h` too.
- *(CLOSED 2026-08-15 (later) — `duo_persist_etalon`. The gate was typing
  at **Stickies**: System 7.5 launches it from Startup Items and it ends the
  boot in front of the Finder, so Cmd-N opened a New Note and the `stir`
  click landed on empty menu bar right of its four titles. Every term of
  the signature was satisfied by the desktop underneath. Accumulated image
  state — the lead recorded here — was not it. The fix is two opt-in
  `BeyondBoot.h` hooks: `focusFinder` (click the desktop) and `frontApp`
  (`$910 CurApName`, the guest's own answer), with `persist()` refusing to
  gesture at anyone but the Finder; plus a steering loop that measures
  pixels-per-trackball-unit instead of halving in units, which used to pin
  the pointer at y=0 against the screen edge. `CHANGELOG.md` 2026-08-15
  (later).)*
- **System 7.5.5 refuses a hot-inserted GCR floppy on SWIM2 machines**
  — reported in the GUI, and **NOT reproduced headless**: judged on the
  desktop (the mounted volume's icon, screen-diff) rather than on
  `nibblesRead` (an IWM-only counter that reads 0 on SWIM2 and produced
  a night of false negatives — `CHANGELOG.md` § 2026-08-04 (soir),
  retraction), the plain tree mounts the disk under 7.5.5 on the Quadra.
  So the difference lives in the GUI path, not the SWIM2 model: a
  machine-thread insert against a running emulation, the live PRAM/Finder
  state, the actual image on the actual profile. Next: reproduce IN THE
  GUI with `POM68K_FLOPPY` unset, insert from the Disques window, and
  compare that Swim2 dialogue against the headless one. Only then a gate
  — "Q605 + 7.5.5 boot volume + hot GCR insert mounts" — which must fail
  on today's tree before it is worth anything.
- [ ] **"Beeps sound wrong / differ per letter"** (field report): the beep
  itself was the Slow Keys rejection beep — expected, and it stopped when the
  8.1 image was cleaned (2026-08-02). What remains is the unrelated half:
  whether ASC renders audible output *correctly* is still untested
  (`q605_asc_test` covers registers/IRQ, not sound). Low-priority ASC item.

*(Nothing else is red — the items above.
Resolved and worth one line each: `savestate_040_test` (2026-08-12 (later) —
not Valkyrie and not one platform: all five 040 families diverged at CPU-chunk
offset 256, `writeBuffer`, a 68010-frame field the interpreter maintained on
every core and the JIT does not, exposed when `jit/auto` became the 68040
default on 2026-08-10; the buffers are 68010-only in the fork since patch
group 24, both engines now agree by neither touching them); the IIfx etalons
(2026-08-06 — a corrupted `hdv/MacOS-7.6-boot.vhd`, `drVolAtrb = $0000`, not a
code regression; deleted, the gates fall back to `GISTPERSO-boot.vhd`; the
dirty-bit refusal underneath closed 2026-08-13 (seventh) — Toby VBL disable); the
800K-GCR-on-boosted-030 refusal (2026-08-05 — the floppy boost gate);
`jit_q605_boot_etalon` (2026-08-04 — `leaveToDynamic` now reloads the target
from `at(L_.pc)`); `q605_cudalle_key_etalon` (2026-07-31 — Easy Access Slow
Keys inside the image); the Cuda↔VIA phase robustness chantier (2026-08-03 —
`M68hc05::serviceInterrupts` charges the hardware's 11 cycles, gated by
`cuda_lle_test`). Each has its date in `CHANGELOG.md`.)*

### Three method rules those hunts paid for, in full

- **Read `drVolAtrb` bit 8 on a gate's image before theorising about its
  code.** Every gate prints a SHA-256 and `drVolAtrb` in its preamble since
  2026-08-09; a `$0000` there means the volume was never cleanly unmounted.
  Two wrong diagnoses were bought before this rule existed.
- **Believe an observable only after it has demonstrated sensitivity — and
  only after it has demonstrated *silence without stimulus*.** `KeyLast`
  (`$0184`) moves in **no** cell, including working ones; **`KeyTime`
  (`$0186`) is stamped continuously by the Slow Keys periodic task**,
  keystrokes or not (its "lockstep with typing" was a sampling coincidence);
  a VRAM-hash probe reported "keys lost" on a cell where KeyMap proves they
  arrive.
- **Read `$484185` with `adb_key_probe` to check Easy Access, never with the
  hold-Return gesture** — that gesture is a toggle and answers the question by
  changing the answer. Expect **three** outcomes: `$FF` on, `$00` off,
  **anything else = the engine is not loaded**, which is what a properly
  cleaned image looks like.
- **Run `tools/check_binaries_fresh.py` before quoting any tier, and run its
  `--self-test` the first time on a new machine.** The whole method file is
  `docs/MEASURING.md`; the two shapes this guard kills are the phantom pass
  (143/143 over stale binaries) and the phantom failure (four "reds" that
  were missing executables). Its own first version answered STALE to
  everything — it asked the top-level Makefile, whose every rule depends on
  a phony target — which is why the self-test exists (R5: a guard nobody
  has watched say both yes and no is not an instrument).

---

## 2. Test & validation depth — the single biggest gap

The gates prove **boot**, not **use**, and the machine fan-out made the ratio
worse. Of the **37 profiles**, only **14** have any gate past the Finder
signature: LC II (`lcii_soak/persist/launch/floppy_etalon` +
`lcii_savestate_etalon`), Quadra 605 (`q605_soak/persist_etalon`,
`q605_cudalle_mouse/key_etalon`, `q605_cdrom/cdboot/cdhot_etalon`,
`q605_savestate_etalon`, `q605_ot_bind_etalon`), Mac II (`macii_mouse_etalon`),
**Mac Plus** (`input_etalon` — mouse quadrature + M0110 keys), the four input
gates from 2026-07-29 (`lc3_`, `lc520_`, `iivx_`, `iisi_input_etalon`), the
IIfx (`iifx_input_etalon`) and the IIvx (`iivx_soak/persist_etalon`).
Enumerate them with
`ctest -N | grep -E 'input_etalon|soak|persist|mouse_etalon|key_etalon|savestate_etalon'`
rather than trusting this sentence. **The other ~23 profiles are boot-to-Finder
signature only.** A machine can pass its etalon and still be useless for real
work.

**Depth is a second axis.** **All twelve** platforms now carry the
soak+persist pair that proves a machine *keeps* working and *writes*, and as
of 2026-08-14 **thirteen soaks and thirteen persists are registered, all green** (twelve platforms, thirteen profile pairs — the towers count twice) — no SKIP left on
the board. The last one was the Duo's, and it took three findings to close:
a `$91` power flag that stopped the PG&E ever cold-booting a second time, a
trackball that had never been wired to the PMU's own quadrature counters, and
a volume that this machine's System will not flush on its own (its VCB keeps
the File Manager's dirty bit for minutes; the same image on an LC III writes
in the frame of the commit), so the gate ends the session through the Finder —
`Special → Shut Down`, steered with the trackball. `CHANGELOG.md` 2026-08-14.
Counting profiles alone hides this axis: the IIvx was inside the nine before
it could survive three idle minutes or create a folder, and the Quadra 630's
two legs were green for a day while its gate looked for a ROM under a name no
archive uses and SKIPped without ever starting the machine.

The IIsi pair discharged the prerequisite this list used to carry: a
LOGICAL-address read of the Time global. `tests/Mmu030Peek.h` is that read —
a side-effect-free walk of the live 030 page tables through `peek8` (whose
physical-ness is exactly right for descriptor addresses), mirroring
`mmuWalkTables` case for case but never driving the bus and never setting
U/M bits. The first soak validated the instrument by construction: TC read
back `$80F84500` (the documented IIsi value) and the Mac clock advanced
**180 s in 180 s of frames** — a wrong walk reads pixels, not a clock. The
gate FAILS loudly if the Finder is up with the PMMU off, so the
physical-vs-logical trap now has a standing regression test.

Highest-ROI closers, in order:

- [x] **The Eclipse pair** — **done 2026-08-14**: `q900_soak/persist_etalon`,
  the same `q700_beyond_etalon` binary on `POM68K_Q700_MODEL=q900`, both
  green first run. The first second-profile pair in the roster, and it earns
  its place because past the boot screen the tower is a different machine:
  two Apple PIC IOPs, an Egret firmware LLE and a second 53C96 that the
  Spike's legs never keep alive, plus the only Toolbox-level exercise of the
  tower's IOP-bit-banged ADB. Thirteen pairs, 26 legs.
- [ ] **Next beyond-boot machines**: a `launch`/`floppy` pair on the Q605,
  soak on the **IIci** (the other RBV — `Mmu030Peek.h` makes it a rig clone
  now), or the AIO family.
- [ ] **Floppy: a guest-INITIATED write — BLOCKED on the § 1 SWIM1-IWM mount
  bug.** The 2026-07-29 "volume mounts, window auto-opens, Cmd-N dropped"
  evidence is RETRACTED (2026-08-05): it was the System 7.5 INIT DIALOG end to
  end — the changed region x 3..494 / y 2..240 *was the dialog box*, the modal
  dialog ate Cmd-N, and the gesture's Return pressed its default [Eject].
  KeyMap sampling proves the keystrokes arrive at every settle time.
  `lcii_floppy_etalon` now detects the dialog instead of calling it a mount.
  The write gesture becomes trivial the day the volume actually mounts.
  Device-side write→eject→flush stays gated by `floppy_persist_test`.
- [ ] **Widen per-machine System coverage.** Each profile's etalon pins one
  reference image (`GISTPERSO`, Infinite Mac 8.1…). `finder_boot_matrix`
  accepts only four machines (`tests/finder_boot_matrix.cpp:328-333` —
  `plus`/`macii`/`lcii`/`q605`); every profile added since Phase C has no
  matrix cell. This is the calcify-around-one-image trap `LLE_VS_HLE.md` warns
  about. Add cells as images are validated — Classic II, LC, Color Classic,
  LC III and the AIO family are the oldest debts. (The harness is
  `EXCLUDE_FROM_ALL` and deliberately not a registered CTest.)
- [ ] **Plus floppy System 4.1 cell.** `bootPlus`
  (`tests/finder_boot_matrix.cpp:136-155`) only does `attachScsi`; the 4.1 cell
  needs an `insertDisk` path. All HD cells PASS.

**The shared beyond-boot criterion is settled and centralised** — do not
re-invent it per gate. `tests/FolderProbe.h` (gate `folderprobe_test`,
19 checks): **the signal is not the biggest count, it is the count that
changes.** `count()` folds ASCII case, `sample()` prints every candidate at
every sampling point, `grew()` returns the candidate that moved. The three
beyond-gate binaries (`lcii_`, `q605_`, `iivx_beyond_etalon` — which back
`lcii_soak/persist/launch/floppy`, `q605_soak/persist` and
`iivx_soak/persist_etalon`) and `folderprobe_test` all include it since
2026-08-09. The old "most frequent candidate" heuristic held on
English volumes by luck and never could on the French 7.5 one, where the
localization resource `Nouveau dossier` sits at ×51 while the created folder
is `Dossier sans titre` 10 → 12.

**And the gesture belongs to an APPLICATION, not to a machine** (2026-08-15):
`persist()` now brings the Finder to the front (`focusFinder`) and asks the
guest who is there (`frontApp` = `$910 CurApName`) before typing Cmd-N,
refusing with a named failure otherwise. Both hooks are optional — only
`duo_beyond_etalon` binds them, because only its volume launches an
application from Startup Items (Stickies, which answers Cmd-N with a New
Note). Any gate that starts failing with "NO candidate folder name appeared"
should bind them before anything else is suspected.

**Per-machine LLE-completeness estimates** (±5 pts, re-scored 2026-07-25, and
they predate the row-granular raster work): Plus ~85, Q605 ~80, LC II ~80,
LC 475/575 ~79, Mac II ~78, LC / LC III/III+ ~75, Centris/Quadra 610/650 ~74,
Classic II / LC 520 family ~73, IIx/IIcx ~73, IIvx/vi ~72, IIsi / IIci ~70,
Color Classic ~70, Mac TV ~70. Common ceilings: cycle-exact CPU only on the
Plus, and the beyond-boot gap above. Lowest scores are **freshness**
(booted-once, not hardened — RBV / Tinker Bell / VASP / AIO).

---

## 3. JIT — second execution engine

> **Chemin critique du projet depuis le 2026-08-09** (§ 0·A). L'ordre décidé
> est : épuiser d'abord tout le conformant listé ici, **puis** seulement le
> HLE / JIT non conformant (§ 8).

Landed and documented: J0/J1 (engine seam, backends, fetch window, block cache),
J2 (x86-64 code generator), J3 (inline DTLB), block linking, 030 + 020 seams,
GUI engine switch on every family, PGO training per CPU family. Design and
measurements: `src/jit/POM68K_JIT.md`; plan and milestones:
`docs/JIT_BRINGUP.md`; per-item history in `CHANGELOG.md`.

Current state: the engine reaches **every CPU wrapper in the tree** — twelve of
them, 68000 included since 2026-08-06. **`jit/auto` has been the conformant
default on 68040 since 2026-08-10**, and **on 68030 since 2026-08-18** — the
16 profiles behind `Cpu030`/`SonoraCpu`/`VaspCpu`/`RbvCpu`/`IIfxCpu`/`MscCpu`,
worth **×1.18** on the LC II (17.90 → 15.14 s, fixed budget, one fingerprint).
68000 and 68020 still default to the interpreter, which remains the accuracy
oracle and keeps one explicit etalon per platform on both accelerated
families (`interp_*_boot_etalon`: q605, centris650, q630, q700 and now
lcii, lc3, iivx, iisi, iifx, duo230). Both x86-64
and AArch64 code generators **declare the 68030 since 2026-08-18**
(`BackendCaps::guestFamilies`) — correctness is lockstep-gated on both ISAs
and an explicit backend request needs no unsafe override. The separate
`BackendCaps::autoFamilies` SPEED mask now gives 68030s native code on
BOTH ISAs: AArch64 since 2026-08-20 (score 64, −5.3 %), x86-64 since
2026-08-21 (score 0, −12.6 % vs `threaded` at the default budget) — the
C.5 flip fired once the IIsi segfault (§ C.4septies) proved gone under the
hardened native gates. Admission stays per (family, backend), so unproven
native 030 code cannot become a shipping default. Best measured figures — fixed budget, identical fingerprint on every
engine (`POM68K_JIT.md` § 3.4, Q605, 3 000 frames): interpreter 48.51 s,
`threaded` 28.10 s (×1.73), `x86-64` **9.71 s (×5.00)**. The ×2.68 quoted
elsewhere is the `q605_boot_etalon` wall clock (61.3 s → 22.9 s), a different
and flattered instrument — see § 0·A.

Open, in ROI order:

- [x] **~~Fix the IIsi SIGSEGV under the x64 generator~~ — CLEARED
  2026-08-21, and the C.5 flip FIRED.** The crash did not survive the
  2026-08-19→21 hardening window: on a fresh full relink, the host that
  produced the four deterministic SIGSEGVs boots the IIsi green under the
  explicit native generator, and all six IIsi gates pass under `auto`.
  Attribution not bisected (17 commits in the window; a bisect prices at
  that many crash re-runs) — the reproducer and triage order stay in
  `docs/JIT_BRINGUP.md` § C.4septies in case it returns. The flip is the
  one line § C.5 promised (`kGuest68030` into x64's `autoFamilies`),
  landed behind fresh D.1 evidence in its own commit; the `jit_*` 030
  boot gates already pin `POM68K_JIT_BACKEND` + `REQUIRE_NATIVE` since
  the 2026-08-19 hardening, so the false-green class cannot recur.
- [ ] **Port the a64 68030 deltas to x86-64 — the forms the 030's OWN
  census names.** **2026-08-21: the opcode pair the shared oracles demand
  is DONE** — EXG and distinct-register CMPM are native on x64, the Scc
  thunk hole is closed (scoped `restartableWriteRequired`), and the
  synthetic 040 oracle is back to 999 ‰ native / 0 slow on x86-64
  (CHANGELOG 2026-08-21 (third)). Also priced the same day: score 64 on
  x64 measured −0.8 %, inside the 1.0 % floor — REFUSED, x64 ships at
  score 0; the a64 i-cache counter retention is parked (all six x64
  callee-saved registers are taken, `add [mem], imm` is already cheap);
  the exact-source read token was already consumed by x64. Remaining
  named levers below. **2026-08-19: the x86-64 generator now BEATS `threaded`
  on the LC II from ~2500 frames up — −12.0 % at the bench's default
  6000-frame budget (ABBA, quiet host, fp `cfb184b6faddabec`) — after the
  i-cache uncharge hole was fixed, the CACR hint retired on the proven V8
  inventory, the base-cost cross-check made global, and the MMU-generation
  flush made a lazy revalidation (`docs/JIT_BRINGUP.md` § C.4sexies,
  `CHANGELOG.md` 2026-08-19). Remaining named levers: the restartable-write
  family still keeps the total-cost check (~44 % of in-block fallbacks) —
  **run to ground 2026-08-21**: reproducer in-tree
  (`POM68K_JIT_RESTART_BASE=1`), two IRQ delay-loop culprits disassembled,
  class = peripheral-delivery alignment; **BSR.W converged on the SAME
  class the same evening** (`POM68K_JIT_BSRW=1`; 2026-08-21 (fourth) and
  (fifth)). **The class itself is CLOSED on x64 — 2026-08-21 (sixth)**:
  the slip was the forced I/O flush running at a clock missing the
  fetch penalty (the take was already aligned); the access thunks now
  bias the clock for the access alone, both reproducers pass the full
  120k, and `jit_lockstep_030_x64_alignment_test` pins it. **The speed
  evidence is MEASURED and the flip is LANDED per-backend (2026-08-22,
  § C.4nonies)**: restart-base −4.3 %, BSR.W −2.3 %, the pair **−8.0 %**
  and super-additive, at 6000 frames on the LC II, fp identical
  (`cfb184b6faddabec`), floor 1.0 %. The default rides the backend's
  `caps().accessClockBias` declaration — ON under x64, and **ON under
  a64 since 2026-08-22 (third)**: the a64 port landed on the M1 (bias in
  `pom68kA64Read/Write`, the `guardIcacheHits` replay deleted, four 120k
  runs + the 6000-frame gate identical, `jit_lockstep_030_a64_alignment_test`
  registered; the class had never been latent there — the guard had closed
  it by replay; its emitter had the total-cost rule hard-wired and was
  wired to the two knobs the same evening, (sixth)). The
  remaining work is ONE item: **finish the `-L etalon` tier under the
  flip on x86-64** — the 2026-08-22 (second) validation was cut by a host
  shutdown at 47/106 parallel gates, zero failures (`CHANGELOG.md` has the
  exact state). On AArch64 the `-L m030` tier ran the same afternoon
  (52/53 → the `lcii_soak_etalon` red was the slice-index leak, fixed,
  `CHANGELOG.md` 2026-08-22 (fourth)). **Two items that run opened:**
  (a) ~~the a64 generator is 3× slower than `threaded` in the idle Finder~~
  **run to ground the same evening** (`CHANGELOG.md` 2026-08-22 (fifth)):
  21 M guard trips on data writes near code re-recorded whole slices; the
  guard now marks 32-byte sub-slices of each block's own bytes and evicts
  by byte-range intersection — 609 s → 119 s at 30 000 frames (past
  `threaded`'s 154 s), `q630_persist_etalon` 436 → 212 s, fp identical. Still named by the counters, in order: the
  37 694 `PFLUSHA` generation bumps of `SwapMMUMode` (each re-proves
  every block lazily — a per-page path for the 2481 single-page
  `PFLUSH` would only take 25 % of them), `window/interp` at 47 % of
  instructions, and 946 hit-list-overflow flushes; the 30 000-frame
  `threaded,a64` ABBA is the instrument (`POM68K_BENCH_FRAMES=30000`).
  **Owed: the quiet-host ABBA after (sixth)** — the provisional pair reads
  −22.9 % (30k) / −23.8 % (6k), both `HOST BUSY` under macOS indexing
  daemons; re-run with Spotlight paused. Next census: the 25 % of
  instructions still in the window — JSR (`4EBA`/`4E91`/`4EAD`, ~35 M)
  and MOVEM (`48E7`/`4CEE`/`4CDF`, ~45 M) lead the fallback list —
  **named 2026-08-23** by `POM68K_JIT_WATCH_OPCODE` (`CHANGELOG.md`
  2026-08-23): (i) ~~JSR~~ **DONE on a64 the same day** (`CHANGELOG.md`
  2026-08-23 (second)): the target's first word is read at run time through
  `Moira::pomJitReadProg`, push proved-then-stored — native 71 → 83 %;
  the x64 port is the same three pieces (`heldIrc` formula → runtime read,
  `Frame` slot, thunk) and needs the x64 030 lockstep on x86-64; (ii) the x64 `bsrW030` exemption for `$4EBA` tests
  `BranchSubroutine` and is dead code exactly as a64's was — mirror the
  a64 fix and run `jit_lockstep_030_x64_experimental_test` on x86-64;
  (iii) ~~MOVEM on 030~~ **native on a64 since 2026-08-23 (third)** —
  the span is proved before the first access, so the $B resume is never
  observable; locksteps identical; x64 keeps its guard until its 030
  lockstep runs on x86-64. What is left in the window (13 %): JSR
  idx(An) `4EB0` (not one path: base 12-14, 3-4 fetched words), the
  `PFLUSHA` re-proofs, and `arm backoff` 4.3 % — **measured 2026-08-23**
  (`POM68K_JIT_ARM_BACKOFF`, 30 000 frames, single runs, same binary):
  32 → 94.2 s, 8 → 90.5 s, 4 → 92.9 s, 1 → 93.1 s; native share rises
  monotonically (85.6 → 88.2 %) but wall does not — each probe costs.
  ≤ 4 %, not a claim; a streak-growing backoff (1 → 32) was tried and
  **diverged the two 68040 locksteps** (`D1` at the first boundary, D-cache
  model on) while the 030 gates stayed identical — reverted; WHEN the
  window is armed is guest-visible on the 040 and any retry policy needs
  that family's gate first. **The profile is now
  flat** (`sample`, idle Finder): generated code ~20 %, window ~13 %,
  engine ~7 %, and the peripheral LLE ~27 % — the Cuda's 68HC05 alone
  ~13 %, then `V8Memory::tick`, SCC, ASC, VIA, IWM — so the next
  end-to-end lever is outside the JIT; (b) an asset-free gate for the
  slice-index invariant (`unmarkPages` is the inverse of `markPages`;
  the soak is the only tripwire and needs the ROM). The paragraph below is the 2026-08-18
  state this work overturned.**
  Measured 2026-08-18 (`docs/JIT_BRINGUP.md` § C.4bis and
  § C.4ter, `jit_bench_lcii` 2000 frames, one fingerprint throughout): the
  x64 generator is **slower than the interpreter** on a 68030 (21.84 s vs
  17.90 s) and loses to `threaded` (15.14 s) even at its own ceiling.
  Two thirds of the guest never enters a block, always for the same reason:
  202 848 of 277 002 compile attempts fail the 50 % native-coverage bar,
  and **zero** fail `emit()`.
  - > **"The lock is global native residency" — RETRACTED 2026-08-18.**
    > Residency is a symptom. Sweeping the bar with `POM68K_JIT_MIN_NATIVE`
    > shows forcing it up makes the engine **monotonically slower**: at 0 %
    > residency is 2.2× higher (31.3 %) and wall is **37 % worse**
    > (29.99 s), because a fallback *inside* a block pays a call, a frame
    > and a boundary commit where the window pays a plain dispatch.
    > Everything from 50 upward is a flat plateau — the inherited 68040
    > number is already optimal. What raises residency legitimately is
    > emitting more instructions per block, not admitting worse blocks.
  - The 68030's fallback census is **not** the 68040 drawing census's
    answer: `DBcc 56C9` **37.8 %**, the `(A7)+` pops (`205F`/`221F`/`245F`/
    `4A1F`) **28.6 %**, memory-to-memory `MOVE.L (A0),(A2)+` 13.1 %,
    `ADDQ.W #4,A7` + `UNLK` 12.2 %. `idx(An)` is **3 708** against
    1 976 626 for `(An)+` as a MOVE source. Census the guest you are
    optimising.
  - **DBcc is not missing an emitter** — `emitDbcc` exists and refuses
    nothing. The x64 compile loop refused **every multi-word branch**
    while the 030 i-cache charge was armed, because a long branch's two
    paths fetch a different number of words and `chargeIcache()` was
    emitted once, before the condition. *(Superseded 2026-08-19: the x64
    guard is gone — DBcc, two-word Bcc and `JSR d16(PC)` compile, the
    exemptions at `JitBackendX64.cpp:2988-3023`; a64 keeps the guard at
    `JitBackendA64.cpp:2587` with a `!dbcc` carve-out.)* Unlocking it (priced with
    `POM68K_JIT_ICACHE_EMIT=0`, which moves the fingerprint) is worth
    residency 14.4 → 21.4 % and **3 %** of wall. The real obstacle is that
    the i-cache shadow is a compile-time model and its post-branch state is
    run-time dependent.
  - **The honest ceiling: there is no measured path to parity on x86-64.**
    Both non-conformant ceilings at once (no CACR flush, no i-cache charge)
    give 16.49 s against `threaded`'s own ceiling of **14.19 s**. Keep
    C.5's declaration shut on x64 — that is now the measurement, not
    caution.
  - The remaining gaps are C.4's own and are **closed on a64, untouched on
    x64**: the `(An)+` pre-access ordering delta, and the cost cross-check
    refusing instructions whose traced cycles carry an i-cache penalty
    (`Instr::baseCycles`, which a64 consumes for narrow forms). That is
    why a64 sits within 3 % of `threaded` on a 68030 and x64 is 44 % behind.
    Still the obvious work; do not expect parity from it.
  - The cache is dropped **28 816** times per run, **26 544** of them by the
    wrapper's CACR SMC hint (new `jit::Flush` gauge, printed by every
    bench): 74 154 compiles for 29 272 distinct blocks. Gating the hint on
    the i-cache strobes CI/CEI — done, all four 68030 wrappers — is correct
    and buys **15 flushes**: this guest's CACR writes really are all i-cache
    clears. Removing the hint outright is worth **−21.8 %** wall
    (`POM68K_JIT_030_CACR_FLUSH=0`, unsafe instrument), and the gain is
    compile time, not residency (native share *falls* to 11.4 %).
  - **To retire the hint conformantly**, prove the guard sees every write
    into RAM on each 68030 board. Done for the V8: SCSI is CPU-driven
    pseudo-DMA, IWM polled, generated stores cross the DTLB `codeMask`.
    Not done for VASP, RBV, MSC. The four-proof bar below applies.
  - Residual not-native causes once the declined blocks are set aside:
    `arm backoff` 13.5 % (each `armWindow` refusal costs 32 single-stepped
    instructions — 998 655 refusals × 32), `tracing` 5.1 %, `cpu flags`
    1.3 %. The backoff is the only one big enough to be worth a look after
    the emitter work.
- [ ] **Widen the code generators to the 68030 family.** Plan, milestones and
  every measurement: `docs/JIT_BRINGUP.md` Phase C. Prerequisites already
  landed: the `lcii` lockstep gates (`jit_lockstep_030_test` +
  `jit_lockstep_030_blocks_test`, 2026-08-10 — two LC IIs stepped from
  power-up comparing registers, the three stacks, SR, `clock`, 2 KB of low RAM
  **and the three `PomIcache` counters**, calibrated at 8192 cycles per
  comparison so the gate FAILS rather than reports green when the JIT carries
  less than half the instructions); a 68030 branch in `pomJitProbeData` and
  model-correct access thunks (**written, gated only by the boot etalon once
  the family is declared** — the interpreter's `POM68K_DATA_WINDOW` reaches
  `pomJitData` only from `mmu040Read`/`mmu040Write`, and the 030 interpreter
  uses `mmuRead`/`mmuWrite`, so that window is a dead path there).
  - > **The divergence is NOT localized to the memory-access path — REFUTED
    > 2026-08-10.** With `POM68K_JIT_ACCESS_THUNK=0`, which hands every
    > memory-touching instruction back to the interpreter, x64 on the LC II
    > diverges at **exactly the same step (5956)** as with it on. What differs
    > in both runs is the **68030 i-cache accounting** — generated code fetches
    > no instructions and charges no miss penalty.
  - [ ] **The emitted i-cache charge** (`docs/JIT_BRINGUP.md` Phase B) —
    constant-folded per natively emitted instruction; the cold fallback stub
    charges itself already, through `pomJitExecOne` → `mmuExecuteStart` →
    `mmuFetchWord`. **AArch64 half semantically DONE**: split trace metadata
    (base/data-bus, i-cache and post-exception cycles recorded separately),
    native `MOVE SR,Dn` reconstructing T1/T0, S/M, the real `reg.sr.ipl` mask
    and the CCR without clobbering Dn's high word (2026-08-12), and an exact
    poll path where `4A11` still goes through `pomJitReadData` so the MMU and
    the peripheral own the 64 variable cycles while native code owns only the
    6 fixed ones and the flags (2026-08-12). Static fallbacks on the fixed 10k
    census **61 436 → 3 924 (−93,6 %)**; declared coverage 99,1 %; 120k
    lockstep green throughout.
    **At the 2026-08-12 checkpoint the throughput criterion was NOT met**: on
    two fixed 6 000-frame pairs `threaded` does 19,52/19,50 s and AArch64
    20,18/20,16 s, same fingerprint `cfb184b6faddabec` and identical i-cache
    counters. (An earlier pair was 20,37/20,39 s threaded vs 19,43 then
    22,64 s AArch64 — one win out of two is not a gain.) The measured lock was
    global native residency (**18,4 %**) and **64,6 M AArch64 exits against
    50,7 M threaded**. The declaration therefore stayed 68040-only and the
    experimental override remained mandatory at that point.
    **Superseded 2026-08-20:** native i-cache/retirement state now survives
    linked blocks, cold compilation uses the measured score 64, and the
    repeated fixed-budget comparison gives `threaded` 19,93 s versus a64
    18,88 s (−5,3 %, dispersions 0,4/0,3 %, empreinte
    `cfb184b6faddabec`). The long lockstep and native platform tier stayed
    green; AArch64 `autoFamilies` now carries 040+030.
    **The four cumulative proofs required for promotion were**: identical fingerprint
    and counters; IIfx, LC II, IIvx, IIsi, SE/30, Macintosh TV and Duo green;
    a gain repeated across runs; the full `etalon` tier green on both the
    interpreter and the candidate engine.
    *(Fixture trap that already cost a wrong diagnosis, 2026-08-11, and which
    `CHANGELOG.md` does not record: a zero-SCSI IIfx run was blamed on the CPU
    engine and was the **synthetic Toby declaration ROM**. All three IIfx gates
    require the real card ROM `342-0008-a` and SKIP without it
    (`tests/iifx_boot_etalon.cpp:31-42`); `IIfxMemory::installTobyVideo` falls
    back to `DeclRom::buildSynthetic` with only a stderr line
    (`IIfxMemory.cpp:102-104`). Interpreter, `threaded` and experimental
    AArch64 then all give the same etalon — `f=539`, Finder `0,06/0,69`,
    512 SCSI commands.)*
  - [ ] The 030's restartable last write (`MoiraDataflow_cpp.h:355-361`) and
    the exact no-tail-refill queue contract at a block exit. **Landed on A64
    2026-08-10, gated by `jit_restart_write_030_test`** (which compares all 32
    bytes of an injected `$A` frame — SR/next-PC/LASTWRITE/SSW/address/opcode/
    output buffer — and additionally compiles self-looping `JMP (xxx).L` and
    `BRA.L` to prove IRC is the low address/displacement word, not a synthetic
    lookahead): `(An)+` update after a successful probe but before the access
    with exact MMIO-thunk rollback; `-(An)` and brief-indexed destinations
    committed before every access and rolled back before a fault replay;
    register/immediate-source MOVE to `(An)`, `d16(An)` and absolute EAs
    native behind a bidirectional chain barrier. **What is still open**:
    a *broad* last-write shortcut (`MOVE reg/imm → memory` after a writable
    probe) diverged at step 10 455 and was reverted, and indexed destinations
    beyond the brief form remain closed.
    Two oracles built here are worth reusing before anyone re-derives them:
    `POM68K_JIT_LOCKSTEP_PERIPH_TRACE_AT` hashes the save-stated device tree at
    every edge and delivery (it proved 23 trace points identical and isolated
    point 24 — same clock, same device hash, PC `40A09A14` vs `40A09A0A`);
    and the PI success oracle compares the written byte, pre/post A6, all
    16 MiB of backing memory and address/value/width/A6/PC/PC0/IRD/IRC/SR/clock
    inside MMIO. The latter is what named `22D8`: its source probe committed
    `(A0)+`, its destination probe refused, and replay incremented A0 again —
    **memory-to-memory MOVE now probes both mappings before either EA
    mutation.**
- [ ] **Coverage tail** (after MOVEM/DBcc/JMP/Scc/PEA): register-count and
  memory shifts/rotates (nothing decodes line-$E on either backend); **the
  68020 indexed modes are the big block** — a brief extension-word decoder,
  and what QuickDraw's blitters are made of; refused today at
  `JitBackendX64.cpp:36` (the stated exclusion) and `:198-199` (`eaIndex`
  returns −1 for mode 6). MULU/DIVU stay fallback (data-dependent cycles —
  the cross-check refuses them honestly).
  - **The drawing census asked for by `POM68K_JIT.md` § 3.5 has been taken**
    (2026-08-18, § 3.5bis, `tests/q605_rogue_census.cpp` — the Q605 playing
    `dev/mac-rogue` off the floppy). It **promotes** this item rather than
    closing it: the indexed modes are **29 % of all block fallbacks** while
    drawing and **31 % on the idle Finder** — the largest single category
    either way. The `~167 k` in § 3.5 was the top-60 *opcode* view, which
    fragments a mode across hundreds of opcodes; the all-opcode rollup that
    landed two days later says 11.2 M for the same idle workload.
    What drawing changes is **magnitude** (2.84 fallbacks per retired
    instruction against 0.47 idle — an idle Finder understates the pressure
    6×) and **form**: the full 68020 extension is 36.1 % of indexed
    fallbacks at idle and **4.1 %** while drawing. The brief share is a
    bracket — 36 % (opcodes brief at every compiled site) to 96 % (all that
    is not full-only), the both-ways mass apportioned by slot ratio
    estimating **71.5 %**, which the dump labels an ESTIMATE. Every point in
    it decides the same way: **scope the work brief-first**; the full format
    is a second, smaller question and not on the drawing path.
  *(Two items left this list on x86-64, 2026-08-10: `Emitter::emitScc`,
  `JitBackendX64.cpp:2425`, emits both the register and the memory forms on
  the 68020 cycle column, and `PEA` is native at `:2278-2291` with its own
  `kPea` cost row. **Neither is emitted by the AArch64 backend** — that
  asymmetry is the honest remainder here.)*
- [ ] **Compact `mmu040InstrStart`.** Eight per-instruction field resets + a
  `getCCR()` pack; adjacent fields could collapse into one or two wide stores.
  Small, but it sits on every single 040 instruction.
- [ ] **Generated-code density** — deprioritized 2026-07-30. The stale
  150 B/instr figure predates boundary-deferral + cold emission; the
  re-baseline shows x64 already beating threaded on both regimes (−10 %). The
  residual idle-Finder gap is dominated by the ATC-eviction exactness contract,
  which density cannot touch. Micro-wins (local rel8, shared stubs) are
  third-order.

### Build recipe — Raspberry Pi (landed 2026-08-08, `docs/RASPBERRY_PI.md`)

`POM68K_NATIVE` / `POM68K_TUNE` / `POM68K_LTO` are three CMake knobs now, not
one; the aarch64 AppImage gets LTO + `-mtune=cortex-a72`;
`packaging/raspberry/build_native_pi.sh` builds `-mcpu=<exact core>` + PGO on
the board; `tools/pgo_train_run.sh` is the shared training load and **fails
loudly** where PGO used to fail silently. Left open:

- [ ] **Measure POM68K on an actual Pi, before and after.** Every ARM number
  in `docs/RASPBERRY_PI.md` is NeoST's, borrowed from the same Moira
  interpreter on a Cortex-A72 (−20 % PGO, −34 % PGO+LTO, ~10-20 % `-mcpu`).
  POM68K's own PGO figure is x86-64 only. Use `jit_bench`
  (`POM68K_BENCH_FRAMES`), never a boot etalon: an etalon stops when it
  recognises the Finder, so two builds get timed over different amounts of
  guest work. The fingerprints it prints must match across builds.
- [ ] **Dispatch `pi400.yml` with `cortex-a76` once.** A Pi 5 is armv8.2-a —
  LSE atomics, fp16, dotprod — the one case where the raised floor might
  actually change codegen. **The a72 case is already settled and refuted**
  (2026-08-08, run 31264225875): `-mcpu=cortex-a72` produced a binary
  byte-identical to the release `-mtune=cortex-a72` (27 bytes differ out of
  8 698 128 — build-id + version string), because `-mcpu=X` is
  `-march=<X's arch> -mtune=X` and the ISA delta (crc, crypto) is code GCC
  never emits by itself. The workflow builds both ways and reports the verdict
  per run; nobody should re-derive the a72 result to find out.
- [ ] **LTO for the macOS `.dmg` and the Windows `.zip`.** Stopped at Linux
  deliberately: the universal-2 `lipo` path was not exercised, and MSVC needs
  `/GL` + `/LTCG`, which the CMake block does not emit.
  **Re-stated 2026-08-17**, because the old wording ("both set
  `POM68K_NATIVE=OFF` and so still get no LTO") stopped being true when
  `POM68K_LTO` started defaulting ON. The two halves are now different:
  - **macOS** — `package_macos_release.sh` and `macos.yml` would have picked
    LTO up as a *side effect* of that flip, on a path nobody has exercised.
    Both now pass `-DPOM68K_LTO=OFF` with the reason written next to it. This
    item is one line away from done: delete that flag and exercise `lipo`.
  - **Windows** — nothing to flip. The whole optimization block is guarded
    `if(NOT MSVC AND NOT EMSCRIPTEN)`, so `POM68K_LTO` is **inert** on MSVC
    at any default; the job passes no LTO flag on purpose, and says so. The
    work is emitting `/GL` + `/LTCG`, not changing a knob.

### Measured and DROPPED — do not re-open without new data

These cost real time to measure. The numbers, not the conclusions, are the
value here. (The 2026-08-11 rejections are in § 0·A, where they are the only
record.)

- **Lazy condition codes in x64: ceiling ≈0.8 %.** Method: duplicate the flag
  emission (storing the same byte twice is a semantic no-op, so the delta is the
  marginal cost of one full materialisation set). Q605 boot on the x64 backend
  26.13/26.15 s → 26.37/26.30 s. Lazy CC can only remove the *dead* subset of
  that. **Measurement trap**: the first published figure (2.5 %) used
  `POM68K_CPU_ENGINE=jit` alone, which selects **threaded** — the modified x64
  code never ran. Any x64 measurement must set `POM68K_JIT_BACKEND=x64`.
- **Page-granular dispatch tables for the memory maps: premise was wrong.**
  Accesses by destination over an LC II boot (1 475 M): RAM 69.6 %, ROM 29.3 %,
  I/O 0.33 %, other 0.80 %. 99 % land on RAM/ROM, already 2-4 perfectly
  predicted compares; a 4 KB table would put a **dependent load** in front of
  the 99 % case to save branches that cost nothing. Re-open only with a profile
  showing decode as a real share; the honest lever for I/O-heavy code is
  per-device caching.
- **O(1) ATC lookup for `mmu040Translate`: flat.** callgrind put it at 38.6 % of
  the interpreter, so a 256-entry direct-mapped hint table went in front of the
  32-entry scan (bit-identical by construction). Interpreter 213.2 vs 213.2 s
  (5 G), 906.7 vs 903.9 s (20 G), x64 98.9 vs 97.6 s. The single-entry
  `last[write]` memo already catches the hot case. **The 38.6 % is real but it
  is the WALK and per-access bookkeeping, not the lookup** — re-open only with a
  profile that separates them.
- **The interpreter's data window (J1c) is opt-in, not open work.** It exists
  (`JitEngine.cpp:87`, `POM68K_DATA_WINDOW=1`, `POM68K_JIT.md § 8`); capping it
  at ATC coverage for bit-exactness made it a net loss on the interpreter. The
  x64 keeps its inline TLB.

Invariant worth restating: **derived state dies with the ATC entry it came
from** (`pomJitAtcEvict`). A window hit must imply the interpreter would have
ATC-hit too, or the engines walk different subsets of the page tables — and
walks write the descriptor U bit, which Mac OS VM reads for page aging. That
class of divergence mimics memory corruption; see CHANGELOG 2026-07-28.

---

## 4. LLE fidelity — replace HLE shortcuts

Inventory and migration plan: `docs/LLE_VS_HLE.md`, which **carries no live
BUG any more** (2026-08-02): everything remaining in its § 1 is a
*simplification*, each with its reason and its reopening condition. Policy
settled 2026-07-29 (§ 2): HLE fallbacks are **kept but LOUD** (stderr
NON-CONFORMANT notice at every HLE ADB entry) because MCU dumps are
non-distributable; deletion would be a deliberate "POM68K requires MCU dumps"
product decision, not a cleanup.

- [x] **The Egret LLE on the Eclipse towers** — **done 2026-08-14**. The
  Quadra 900/950 were the last board running the command-level `Egret`
  model, and the only HLE registration no dump could retire. They now run
  the real `341s0851` on a real 68HC05 (`CudaLle`, `Flavor::Egret` — the
  part MAME names, `macquadra700.cpp:887`), which brings them the wire, the
  MCU RAM, the autopoll, the PC3 restart seam and product-mode
  qualification. `q900_/q950_boot_etalon` green, new `q900_input_etalon`
  (the tower's ADB now runs through the firmware, not `AdbBus`),
  `lle_a64_q900_preflight` replaces `lle_a64_q900_refused`.
- [x] **The Finder's "Restart"** — **done 2026-08-13** on the six platforms
  that carried an Egret/Cuda LLE (V8, Sonora, VASP, RBV, Q605, Q630; the old
  "eight" counted Centris, which has no Egret, and the Eclipse, which ran
  the HLE one — it took the seam on 2026-08-14 with its own LLE, so the
  count is now seven). Deferred latch + per-platform binding +
  `cuda_restart_test` (30 checks, both flavours). Detail:
  `docs/LLE_VS_HLE.md` § 1.9.
  *Still open here*: a **guest-level** etalon that boots a System, picks
  Finder → Redémarrer and asserts the machine comes back up. The gate that
  landed proves the seam end to end from the firmware's own action; it does
  not prove the Toolbox path that leads to it.
- [ ] **Quadra 605 / LC 475**: expand Cuda commands only from ROM/driver
  traces. The on-chip FPU/FPSP and M0-M3 cache work are closed: opt-in
  I/D contents, copyback, snooping and bus-transaction timing. What remains
  is pin-level 040 timing, not an architectural cache gap
  (`docs/CACHE_040.md`).
  *Product-performance follow-up*: the cache-active null-DTLB memo removed
  99.77 % of redundant fill probes but only 2.4 % of Q605 wall time; a
  last-line tag memo measured worse and was removed. Native physical
  D-cache-line **reads** have now landed and are lockstep-gated on A64/x64:
  18.6 M exercised hits and **61.94 -> 48.78 s** (-21.2 %) on the paired
  Q605 control. Native sole-access **copyback write hits** now follow them:
  a separate write-authorized line table, emitted dirty-longword publication
  on A64/x64, and `jit_copyback_write_040_test`, which pins both the last-write
  and restart halves of the format-$7 fault contract; its independently
  registered OFF control proves the escape hatch returns to the exact cache
  path. The first paired 5 G-cycle A/B found the MOVE-only write path below
  noise (49.87 -> 49.81 s average), so broadening arbitrary stores is closed.
  The cache-on census instead priced the BSR return-address push; routing that
  sole write through the same proof removed 3,933,940 more fallbacks. Across
  two order-reversed pairs the whole write path now averages **49.93 ->
  49.49 s** (-0.88 %), with identical fingerprint/SCSI/PC. The full A64
  cache-on census then exposed the next bounded pair: `MOVE.L abs.W,-(A7)`
  and `MOVE.L (A7)+,abs.W`. Proving both lines before either access removes
  **7,523,969** more fallbacks and measures **46.92 -> 46.63 s** (-0.62 %)
  over two order-reversed pairs. Its dedicated ON/OFF gate pins dirty bytes,
  flags/EA and whole-instruction /BERR replay. The repeated full lockstep
  exercises **21,303,835 reads + 5,896,026 writes** over 131.8 M JIT
  instructions without divergence. The mode still misses the
  Finder guest budget and remains far behind cacheless 11.03 s; more C++ tag
  lookup and unmeasured RMW work are closed.
- [ ] **SCC LLE, Low tier** (2026-07-22 MAME `z80scc.cpp` audit,
  `docs/LLE_VS_HLE.md` § 1.4). The two items that needed no new transport
  landed 2026-08-02 (WR5 `/RTS` **and** `/DTR` as real pins with the
  Auto-Enables deferral released from `tick()` as the shifter drains; SDLC Rx
  residue codes, byte-aligned code 011), both gated in `scc_engine_test`.
  What is left is deliberately deferred, each for a stated reason: true
  bit-serial Tx/Rx sampling (only worth it with a real async transport to talk
  to); chip-variant gating (NMOS 8530 / 85C30 / ESCC — FIFO depth 3/8, WR7',
  status FIFO `:1363`, needed the day a machine wants the ESCC); WR9 VIS/NV
  options (hardcoded VIS=1, correct for every Mac target); DPLL (MAME stubs it
  too, `:305-318`). **Also missing: a consumer** — nothing on the emulated side
  of the wire reads `rtsAsserted()` yet.
  **Caveat that applies to the whole SCC backlog**: MAME's SDLC side is partial
  (Send Abort / CRC resets `:1602/:1635` "not implemented", no EOM latch, no
  hunt/sync) — for LLAP behaviours **we are the more complete model**. Use MAME
  as oracle for the ASYNC side only; do not regress LLAP chasing parity.
- [x] **Floppy flux + PLL layer — the plan is FINISHED, 2026-08-14.**
  Steps **5 and 6** landed the same day as 2-4b. Step 5: `flux_` is the
  medium (transition times, one revolution) and `cells_` is what a
  `FluxPll` recovers from it; both SWIMs hand `commitFlux()` their TSS
  half-cycle times directly, so a controller writing at its own rate —
  SWIM2 setup bit 3 doubles the spacing, the SWIM1 ISM takes it from
  P_TIME0/1 — writes that rate onto the disk. A successful commit no
  longer re-lays the track canonically and a failed one no longer heals
  itself. The write-back decode is clocked by the controller that wrote,
  which is the honest stand-in for a drive that has no reader of its own.
  Step 6: the `Iwm` READ path is MAME's `sync()` MODE_READ window machine
  (`src/devices/machine/iwm.cpp:398-455`) over the store, plus the `:249-250` shifter clear —
  paid for exactly as this list said it would be, `disk_boot_etalon` +
  `lcii_floppy_etalon` both green. The gap4 became written self-sync,
  which is the `encodeTrackGcr` geometry note's reopening condition coming
  due (the filler is MAME's kind now; the pregap LENGTHS stay put).
  Snapshot format **v8**; new gate `iwm_read_test`. Also closed that day:
  the **two-revolution READY spin-up** counter with the gate that was its
  reopening condition. Left in § 1.3, none symptom-backed: the store holds
  the LIVE track (a seek re-lays canonically — a whole-image flux store is
  what MAME has and what MAME pays for), committed tracks re-encode, tach
  is a sampled bit.
- [x] ~~**Floppy flux + PLL layer, steps 2-4b**~~ — **done 2026-08-14**
  (`docs/LLE_VS_HLE.md` § 1.3 owns the full state). `SonyDrive` exposes the
  track as a flux view (`nextFluxAfter` = MAME's `get_next_transition`,
  opt-in deterministic jitter `POM68K_FLUX_JITTER`); `Swim2` reads through
  a real `FluxPll` separator (snapshot **v6**); `Swim1`-ISM runs **MAME's
  real LS-pair/CSM/TSM engine** (`src/devices/machine/swim1.cpp:885-1233` verbatim, snapshot
  **v7**) — the 64-min-cell calibration and its per-pair-side correction
  factors are live, the parameter RAM became load-bearing, and the bite
  test is IN the suite: a +20 % off-rate track reads only because the CSM
  recalibrates, `P_MULT=0` starves the calibration and the same track
  fails with error `$08`. `nextCell()` retired. Gates: `swim2_media_test`
  +9 checks, `swim1_test` +7; every pre-existing floppy gate re-proves the
  ideal-edge bit stream unchanged; the 2026-08-02 test-first note held on
  both engines (jitter alone never leaves its own fixed window — off-rate
  is what bites). **Still open, neither symptom-backed**: a first-class
  flux track *store* (the view derives from the canonical cell ring, so
  off-rate written flux does not survive a commit — same change that would
  let `encodeTrackGcr` adopt MAME's zone arithmetic), and the `Iwm` READ
  path (hand-timed denibble stream — off limits without
  `disk_boot_etalon` + the LC II floppy gates in the loop).

### Peripheral event deadlines — eight of twelve platforms, and why the other four are not

Landed 2026-08-03 (Q605 + V8) and 2026-08-04 (Sonora, VASP, RBV, Centris,
Q700/Eclipse, Q630; 27 serial gates green). Fixed batching is replaced by
`min(binding MCU-LLE bound, historical batch)`: `catchUp()` returns until that
absolute clock, device-space accesses still force `flushTicks()`, a too-small
bound is merely slow, and **no bound may be larger than the next observable
transition**. The explicit HLE fallback keeps its historical batch.
`POM68K_PERIPH_STATS=1` (Cpu040 only) counts the path.

The contract, worth keeping because the next platform will need it: **only a
device that can raise an interrupt or flip an externally visible line
*spontaneously* needs a bound**; pure state (SonyDrive spin, rotation angle) is
covered by the access-forced `flushTicks()`. Mechanical wrapper change per CPU:
add `periphDeadline_`, early-return in `catchUp`, schedule after `tick` — **and
add the new member to the wrapper's `visit()`** (the `savestate_030/040` gates
catch an omission).

Why it was worth doing at all, measured on `q605_boot_etalon`: batch 256 →
25.6 M `mem.tick()` calls / 60.1 s; batch 1 → **833.2 M calls** / 103.3 s, with
the **same 1.650 G machine cycles delivered either way** and `catchUp()` called
879 M times in both — the cost is neither the devices nor the hook, it is
entering the ~15-device fan-out **32.5× more often**. With deadlines: **86.65 M
calls for 1.675 G machine cycles, 19.34 cycles/call**, exact event timing
preserved. Every device in `Q605Memory::tick` can state a bound and **none has
to fall back to 1**:

| device | bound | value @ 25 MHz |
|---|---|---|
| `CudaLle` 6805 | `ceil(((mcuDebt_+1)*cpuHz - mcuAcc_) / kMcuHz)` | **~12 — binding, gated** |
| VIA E clock | `ceil((cpuHz - viaEClock_.acc) / 783360)` | ~32 |
| `AscIosb` drain | `ceil((kCpuHz - drainAcc_) / drainHz())`, scaled C15M→CPU | ~1123 |
| `Scc8530` | min of the live countdowns (`peerHold_`, `txShiftIn`, …) | **∞ when idle** |
| `Swim*` / `SonyDrive` | cell/spin clock — only while the motor runs | ∞ when parked |
| `Ncr53c96` | service-latency countdown — only during a transfer | ∞ when idle |
| 60.15 Hz CA1 | `ceil((kCpuHz*100 - tickAcc_) / 6015)` | ~416 000 |
| `Dafb` VBL | from `framePos_` | ~416 000 |

**Extended 2026-08-13, and the extension is OPT-IN because it was measured.**
`Cpu020` (Mac II family) and `MscCpu` (Duo) now carry the deadline behind
`POM68K_MACII_EVENT=1` / `POM68K_DUO_EVENT=1`; the default stays the
historical batch. One binary, knob flipped between runs: Mac II
65.28/66.37 s against 57.18/56.49 s (**+14.2 %/+17.5 %**, repeated), Duo
109.49 s against 100.50 s (**+9.0 %**) — every run reaching the Finder with
the Mac II etalon's three observables identical either way. The deadline is
strictly more correct (jitter → 0) and its correctness is invisible to every
gate we have, so defaulting it on would be the ASC-scheduler mistake again
(§ 0·A, withdrawn on a throughput regression despite seven green gates).
Full reasoning and the per-board bounds: `docs/LLE_VS_HLE.md` § 1.2.
→ Flip either default once a **jitter-sensitive gate** exists to justify it.

Deliberately NOT converted, each for a stated reason: the **compacts**
(cycle-exact by construction; a deadline there risks the Plus's whole
accuracy claim for the cheapest machine to emulate) and the **IIfx** —
whose reason is now **verified, not assumed**: `ApplePic::tick` steps one
65C02 instruction per iteration with no idle state, so
`ApplePic::cyclesToNextEvent()` is 1 whenever the PIC is not already in debt (`ApplePic.h:89`) and a deadline
would collapse to a flush per cycle, replacing a 64-cycle batch with the
worst case. The IIci is the delicate one of the
converted set — `adbVia_.tick` + `syncTo(machineClock())` every tick is a
host-paced PIC transport, so derive its bound from the live countdowns before
touching anything (`pom68k-mcu-lle-clock-drift` applies).

**Permanent guard rail:** `Q700Memory` serves three machines. After any change
there, re-run `q700` **and** `q900`, never in parallel
(`pom68k-no-concurrent-ctest`).

### Closed by measurement, kept as one line each

`AdbLine` device-model gaps + the host-side two-button/modifier routes
(2026-08-02/03, `adbline_test`); the Valkyrie I2C pixel clock (2026-08-02,
`valkyrie_i2c_test` — M/N/P = `$0E`/`$1B`/`$02` → 30.752 MHz, 640×480 at
67.80 Hz; **left open on purpose**: that is 1.7 % above Apple's nominal
66.67 Hz and a `31.3344/8` reference would be exact, which suggests MAME's
`3986400` is off — needs a real observable before anyone touches it);
`Swim1` DAT1BYTE as a level line (2026-08-02); the VIA E clock as exact
rational arithmetic for both the rate and `viaSync()`'s phase grid, in one
header `src/ViaEClock.h` because the two must not drift apart (2026-08-02);
the ASC drain rate following `$807` CLOCK RATE (2026-08-02, `asc_test`);
the raster beam — `src/VideoBeam.h` + row-granular decode on **all nine**
decoders, wired into every GUI loop, **the beam owns no clock, it adopts the
platform's own `framePos_`** so the VBL edges are untouched (2026-08-02,
`video_beam_test`, `v8_raster_test`, `raster_equiv_test`); one scan position
per machine exposed to the guest — Valkyrie's `$14` blanking bit reads the LIVE
line, DAFB has no position register at all (not in `Dafb.cpp`, not in MAME's),
and the Plus's VIA PB6 already read the same `cpu.getClock() % 130240`.

Two findings from that batch that are still load-bearing:

- **Classic II Eagle `$F18000` is the analogue CRT brightness/contrast DAC**
  (MAME's `spice_device::bright_contrast_w`), named from three independent
  pieces of evidence: `rominfo --universal` shows the Classic II record's own
  DecoderInfo with `decoder[6] = $50F18000`, zero on every LC / LC II / IIci /
  IIfx / IIsi record in the same table; the ROM routine at `$A51350`
  disassembles as a DAC feed (0-255 scaled `×$2B >> 8`, de-scrambled through a
  43-entry table, shifted out 370 times at stride 2); same address and purpose
  in MAME's Spice. Behaviour is unchanged — our CRT has no analogue stage — but
  a named device deliberately ignored is not an unexplained hole. **Deliberately
  NOT done**: the ROM's presence test fails here because reads return `$FF` and
  never echo; that is probably right for a write-only DAC, but "probably" is not
  a measurement, so no read-back value was invented.
  Also settled: **the hole value does not matter to this guest** —
  `POM68K_V8_HOLEVAL=00` and `$FF` both reach the Finder with 9619 SCSI
  commands, identical. MAME's 0 here is its `address_space` DEFAULT, not a
  modelled decision (unlike the Sonora's 0, which `iosb.cpp:54-65` states in a
  comment). **Do NOT align to 0 "for parity"**: there is no oracle statement to
  be parallel to.
- **The ADB report rate is right, and is not a suspect any more.** Trace of a
  full LC III run (`POM68K_ADB_LLE_TRACE=1 ./build/family_input_etalon lc3`,
  199 s emulated, 17 850 ADB commands): the aggregate autopoll interval is
  **11.18 ms — p10 = p90 = median**, dead steady, against the Egret's nominal
  11.1 ms / 90 Hz. That is **89.5 Hz vs 90**, a 0.6 % deficit, exact by
  construction because the cadence is the firmware's own timer. The mouse is
  polled at ~67 Hz *while it has data* (SRQ-driven bursts). So the ~1.6×
  amplification of a sustained stream is System 7's mouse scaling acting on
  coalesced deltas. **Anyone re-opening this needs a *new* observable.**

---

## 5. Per-machine backlogs

### LC II / V8

- [ ] **1.44 MB guest-level mount/boot etalon.** The SWIM1 controller is done
  (IWM+ISM personalities, 1-0-1-1 switch, param RAM, MFM read/write through the
  cell engines — CHANGELOG 2026-07-23, gate `swim1_test`); what is missing is a
  guest-level gate. Asset on hand: `disks35/Stuffit_Expander_5.5.dsk`.
- [ ] Finish DFAC / sound-out behaviour and host-clock resampling; verify
  long-running audio tempo under GUI load.
- [ ] Bus and timing gaps: compare interrupt, VBL, VIA and memory timings with
  real hardware; diagnose the idle screen dim seen after very long runs.
- [ ] **Confirm the GISTPERSO / SimCity 2000 startup race is closed.** CHANGELOG
  2026-07-18 records only the root-cause analysis (CPU spins in the ROM Memory
  Manager heap-walk at `$40A0E148` = guest heap corruption during Finder
  startup; PRAM and HFS structure exonerated) and the Shift/Option workaround.
  Re-run the deterministic headless repro (`LCII_HOLD_KEYS` in `lcii_trace`) and
  either land a fix entry in CHANGELOG or reopen the differential hunt. Backup
  image: `hdv/GISTPERSO-boot.vhd.avant-reparation`.
- [ ] **No-FPU SANE.** Solved on the 040 side (CHANGELOG 2026-07-21 "Bare no-FPU
  solved: _FP68K binds the integer PACK 4"; gate `q605_barefpu_boot_etalon`
  reaches the Finder under a true `FPUModel::NONE`). **Unverified whether the
  030/LC II path still needs the same UniversalInfo / defaultRSRCs selection**
  (`POM68K_NOFPU` at `main.cpp:1874,1887`; the FPU model itself is set in
  `Cpu030.cpp:46`) — re-test before spending effort here; the original O6.13
  diagnosis may already be obsolete.

### 68030 / MMU / FPU oracle gaps — CLOSED 2026-08-12

All four closed the same day and gated in `berr030_test`, which is their only
record (no CHANGELOG entry): RTE format `$A`/`$B` instruction restart (`$A`
pending data-output replay, `$B` `(An)±` inverse fixups, both with transient
faults); PMOVE-through-translation fault frames (WinUAE-interpreted read/write
frames, SSW `$0345`/`$0305`, logical fault address and PMOVE PC —
`berr030_test:366-401`); instruction-stream fetches across page boundaries
(non-contiguous translated page plus an invalid-page format-`$B` oracle);
FMOVEM indirect-EA read order (full-extension pointer read before three
ascending operand longs, raw FP image matched to WinUAE, `:403-447`).

### Mac Plus

- [ ] **VIA/RTC accuracy**: model 6522 T1/T2 ±1-cycle reload/IFR latency; VIA
  E-clock access alignment and IACK E-cycles; seed the GUI RTC from the host
  while keeping tests deterministic. *(PRAM file persistence is NOT a gap here
  any more: `MacMemory::loadPram`/`savePram` exist — `MacMemory.h:123-124` —
  and `runXxx` wires them on all twelve platforms, `main.cpp` 12 `loadPram` /
  12 `savePram` call sites. What differs per platform is only the STORE:
  discrete `Rtc` on the compacts, Mac II family, IIfx and IIci; Egret/Cuda
  XPRAM on V8/Sonora/VASP/Q605/Q630/Centris/Q700/IIsi; PG&E internal RAM +
  SRAM on the Duo.)*
- [ ] **Floppy**: external-drive selection. `Iwm::attachDrive` takes a second
  `SonyDrive*` (`Iwm.h:23`) but every 800K machine passes `nullptr` for it —
  `MacMemory.cpp:59`, `MacIIMemory.cpp:129`, `IIfxMemory.cpp:129`,
  `RbvMemory.cpp:161`, `SonoraMemory.cpp:120`, `VaspMemory.cpp:91`,
  `V8Memory.cpp:268/271`. The SWIM2 boards already pass two
  (`Q605Memory.cpp:45`, `CentrisMemory.cpp:29`, `Q630Memory.cpp:41`,
  `Q700Memory.cpp:140`), so this is a per-machine wiring gap, not a device
  one. *(Write support, GCR write-back and host file persistence are DONE —
  CHANGELOG 2026-07-23 / 2026-07-24, `SonyDrive::setWriteBack`/`flushToFile`,
  gates `iwm_write_test`, `floppy_persist_test`; hot eject/insert is the shared
  Disques window, `src/DiskBays.*`.)*
- [ ] Keypad/arrow `$79`-prefix handling where required by M0110 input.
- [ ] **Sound accuracy**: fetch the sound buffer per scanline instead of once
  per frame; model the disk-PWM byte and the analog volume curve.
- [ ] **SCSI/serial**: multiple targets/LUNs and correct REQUEST SENSE after
  CHECK CONDITION; a host-side serial transport (PTY/TCP) to make the SCC ports
  *usable*, not just correctly timed. *(WR4 clock mode, WR12/13 BRG, WR11
  routing, WR5 Tx gating, parity/framing, Rx CRC are all DONE — CHANGELOG
  2026-07-23, gates `scc_baud_test`, `scc_engine_test`.)*
- [ ] Pixel-accurate etalons and a WASM build: a screenshot regression runner for
  the Plus boot/Finder paths, keeping asset-dependent tests soft-skippable.

### CD-ROM

Base support DONE 2026-07-29 (`ScsiDisk::openCdrom` — INQUIRY type $05 +
removable, 2048-byte blocks, READ TOC, START/STOP eject, read-only, the Apple
magic MODE SENSE page $30; `attachCdrom(path, id = 3)` on the eleven
multi-target platforms — every `*Memory` except the compacts' `MacMemory`;
CLI routes `.iso`/`.cdr`/`.toast`). Gates `scsi_cdrom_test`,
`q605_cdrom_etalon` (8.1 boots from HD, an 8.6 CD mounts as data),
`q605_cdboot_etalon` (no HD → the ROM's 6→0 scan boots the disc, 3913 blocks).
**Layout matters**: the ROM scans 6→0, so the boot volume goes to ID 6 and the
CD to 3, or a bootable disc wins the scan. **8.5/8.6 are PowerPC-only** (8.1 is
the last 68k release) — a black screen on them is correct behaviour.

- [ ] **Install from CD**: the disc boots and the Installer is reachable, but
  driving it to completion (mouse through the Installer UI, then a reboot onto
  the freshly written volume) is not automated. The natural next "real work"
  gate.
- [ ] **Only 2048-byte-DDM discs mount.** `MacOS_86.iso` (`sbBlkSize = 2048`)
  mounts; the hybrid `TIM_3.iso` (`sbBlkSize = 512`) and bare-HFS `.toast`
  images are read (4 blocks of probes) then ignored. Observed, cause not
  established — **it may well be correct** (a real Apple CD driver may require
  its own `Apple_Driver43_CD` partition, which only the 8.6 disc has). Verify
  against MAME or a real drive before "fixing" anything.
- [ ] **CD audio**: READ TOC already reports the data track; CDDA playback,
  PLAY AUDIO/PAUSE and the audio-through-ASC path are absent — no consumer yet.
- [ ] 2352-byte raw rips + `.cue`/`.bin` multi-track (refused today rather than
  mis-read).

---

## 6. Networking

### AppleTalk in-process stack — DONE 2026-07-24, polish backlog

`AtalkStack` (DDP/RTMP/ZIP/NBP/AEP/ATP), `AfpServer` (ASP + AFP 2.1 +
`.AppleDouble`), `PapServer` (PAP → CUPS/`.ps`), `MacIpGateway` (DDP-22 +
user-mode NAT), tied by `AtalkHub` with a **Réseau → AppleTalk** window; GUI
default (`POM68K_APPLETALK=0` disables it). Gates `atalk_stack_test`,
`afp_server_test`, `pap_server_test`, `macip_gw_test`. Reference:
`docs/APPLETALK.md` § 6.5.

- [ ] **The lapENQ address-defence ACK can lose the prober's race** (the
  comment at `src/AtalkStack.cpp:94-98` now states this plainly — it used
  to promise an express path that never existed). The
  only production binding of `stack_.sendFrame` appends to `pending_`
  (`AtalkHub.h:80-90`) and the flush at `AtalkHub.h:69` calls
  `injectRxFrame(0, d, n, /*express=*/false)` — one tick later, then further
  delayed by LLAP's 400 µs inter-dialog gap. `express=true` exists but is used
  only for the CTS synth (`main.cpp:210`). Consequence: a guest probing node 128
  can time out and take the internal node's address, after which `onGuestFrame`'s
  `src != node_` guard stops recording it and every DDP the stack sends to 128 is
  also the guest's own address. Fix: either a `sendControlFrame` hook bound to
  `injectRxFrame(..., true)`, or correct the comment. Found by the 2026-07-27
  bug hunt, still open (`.bughunt/INDEX.md`); no gate exercises address defence —
  `llap_two_system_etalon` comes closest and does not.
- [ ] Persist CNIDs across runs (per-session today; a `.AppleDB` or an id in the
  `.AppleDouble` sidecar would survive a reboot, matching netatalk's catalog).
- [ ] AFP corners the subset skips: a proper Desktop database (icons / comments /
  APPL mapping, not the current "no item" stubs), CopyFile, CatSearch,
  DID-relative variable paths, AFP ≥ 3.0 / UTF-8 names.
- [ ] More UAMs than guest/cleartext (DHX / random-number) if a guest refuses
  cleartext.
- [ ] PAP status polling + per-queue `papd.conf`-style config; expose the CUPS
  queue choice in the GUI.
- [ ] MacIP: outbound ICMP (needs raw sockets or a ping helper), IP fragment
  reassembly guest→host, TCP window scaling for big transfers. In-order-only,
  MSS 536 today.
- [ ] GUI: editable fields (share dir / server + printer name / gateway subnet +
  DNS) instead of env-only; a "reveal spool folder" button.
- [ ] A live-boot etalon that mounts the internal AppleShare from a real guest
  Chooser. `q605_ot_bind_etalon` already proves the OT `.MPP` bind under 8.1;
  what is missing is the scripted Chooser drive (same need as the external
  bridge below, and a Sys 7 image).

### LocalTalk between two POM68K instances

Milestone 1 DONE 2026-07-22: bidirectional SCC LLAP wire (`Scc8530` SDLC Tx
capture, paced 3-deep Rx FIFO, Hunt carrier sense, WR1 modes, WR3/WR6 address
search + `$FF` broadcast, EOF + FCS in RR1) and the `LtoUdp` cable (Mini vMac /
TashRouter format, multicast 239.192.76.84:1954 — `LtoUdp.cpp:24-25`, enabled
by `POM68K_LTOUDP`). Gates `llap_loop_test`, `ltoudp_test`,
`llap_two_system_etalon`. TashRouter interop verified. Note: System 6 only
opens `.MPP` lazily from the Chooser — headless LLAP tests need Sys 7.

- [~] **Full AppleShare session over the real bridge**: infrastructure DONE
  (netatalk **2.4.9** + TashRouter vendored, `tools/netatalk2/build_netatalk2.sh`
  builds hermetically, `tools/netatalk2/appleshare.sh` + `router.py` serve
  `input/` as "Input" in zone "POM68K"). **Remaining: run the bridge with a GUI
  guest and mount the volume from the Chooser.**
- [ ] Interop check against Mini vMac's LToUDP (same multicast group).

### Ethernet over SCSI — DaynaPort SCSI/Link, opt-in since 2026-08-07

`DaynaPort` (the SCSI target) + `EtherLink` (framing + proxy ARP) onto the
NAT already in `MacIpGateway`. `POM68K_DAYNAPORT=<id>` on the Quadra 605 only
(`Q605Memory.cpp:73-81`); gate `daynaport_test`; design in `DEV.md` § 3.3bis,
rationale in `CHANGELOG.md` 2026-08-07 (later). Every machine here has a SCSI
bus, so this is the one Ethernet path that can reach all of them.

- [ ] **Run a real SCSI/Link driver against it.** This is the only test that
  settles whether the command set is right; everything gated so far is our
  own reading of SLINKCMD.TXT. Needs the DaynaPort driver on a boot image
  and a scripted MacTCP config (address in the gateway's subnet, gateway as
  router) — the same "scripted control panel" need as the Chooser drive
  above.
- [ ] GUI: a **Réseau** entry to attach/detach the card and pick its SCSI ID,
  instead of an env knob at startup only.
- [ ] Save states: the card appears nowhere in `SaveStateMachines.*`, so a
  restore comes back with an empty Rx ring. Cheap to add, but it changes the
  on-disk format for every existing `.pomss` — do it with the next format bump.
- [ ] **EtherTalk** (AARP + 802.3/SNAP DDP) so AppleTalk can leave the SCC
  too. Today the card carries IPv4 and ARP only, and AFP/PAP still go over
  LocalTalk at 230.4 kbit/s through the most timing-fragile device in the
  tree. This is the item with the most to gain.
- [ ] Decouple the uplink from `AtalkHub`: with `POM68K_APPLETALK=0` the
  guest sees the card and it carries nothing, because the NAT lives in the
  hub.
- [ ] Carry the card to the other eleven platforms (a member + an accessor
  each — `AtalkHub::attach` already detects it with a `requires` clause).

---

## 7. New machine profiles

Phase A/B/C are done — **37 profiles**, all booting the Finder (per-machine
detail in `CLAUDE.md` § Status and `CHANGELOG.md` 2026-07-21 → 2026-08-06).
Effort tiers and the full family map: `docs/68K_FAMILY_SCOPE.md`. Rule kept:
**each new profile gets at least one Finder cell before the next.**

Explicitly **out of scope** for now: AV DSP, all 4 MB PPC ROMs.

### Independent majors — the only things left that are not just a ROM dump

- [ ] **Power Manager, Duo line.** The MSC + PG&E platform is finished
  through milestone 3b of `docs/DUO_BRINGUP.md`: `MscMemory`/`MscCpu`,
  `M68hc05Pge`/`PgePmu` LLE including the mid-boot BORG v2 upload and the
  /PMU_INT level, `MscMemory::decodeScreen` (fixed 640×400 LCD, grayscale by
  GSC reg 4 bits 0-1), gate `duo230_boot_etalon` (System 7.5.5, SCSI 3448
  cmds), and since 2026-08-06 **the Duo 230 is the 37th GUI profile** —
  `runDuo` (`main.cpp:4356`), `MachineKind::Duo`, `SnapMachine::Duo230 = 37`,
  PRAM through the PG&E's own RAM + 32 KB SRAM, save states in
  `savestate_030_test`. `docs/DUO_BRINGUP.md` § 3b lists what is deliberately
  NOT wired (floppy, drive sounds, live CD-bay swap, right mouse button — all
  machine-side API absences, not shell gaps).
  **Input through the PMU is done** (2026-08-14): the matrix keyboard landed
  2026-08-13, and the trackball is wired to the PG&E's own quadrature counters
  ($14-$16, latched at 60 Hz — the register must NOT be drained on read, or
  half the directions vanish into a double-read race). `duo_persist_etalon`
  drives the Finder's Special menu with it. A dedicated `duo230_input_etalon`
  would still be worth having: today the pointer's only coverage is inside the
  persist leg.
  Remaining, in milestone order: variants (210/250 trivial — `MscMemory.h`
  already carries `kIdDuo210`/`kIdDuo250` and they share the `$ECFA989B` ROM,
  so they need a variant tag and a `kMachineProfiles` row; then 270c CSC, 280 040, then
  PB150 as the no-oracle MSC variant), and **the actual point — a sleep/wake
  gate** (`duo230_sleep_etalon`), which no other machine can test. That one has
  its first measurement now: `PgePmu::setClamshell(false)` (port F bit 3, the
  lid) holds the 68030 within 5 s — the firmware does act on the switch — but
  the System runs no sleep procs first (not one write reaches the disk, the
  volume's dirty bit is untouched) and re-opening the lid does not wake the
  machine. Both halves are the milestone.
  The 140-180 line is a different PMU (Mitsubishi M50753, 6502-class —
  POMIIGS `CPU65816` candidate) — same brick as Portable/PB100.
- [ ] **NuBus + slot video** beyond Mac II Toby: IIx / IIcx / IIci and the NuBus
  Quadras. VASP/IIvx currently reads its three slots as empty; real cards would
  reuse the Mac II NuBus/DeclRom port.
- [ ] **ATA/IDE target** on the Quadra 630 / LC 580. The port is mapped but has
  no drive, so boot goes through SCSI — the remaining gap on that board.
- [ ] **AV DSP (DSP3210)** → 660AV/840AV. Not planned.

*(The Apple PIC IOP + OSS brick is DONE and closed: `src/R65c02.*` +
`src/ApplePic.*` + platform `IIfxMemory.*`/`IIfxCpu.*`, gates `r65c02_test`,
`applepic_test`, `iifx_post/boot/input_etalon`, and the same front end on the
Quadra 700 board carrying the Eclipse towers — `q900_boot_etalon` 640×480×1,
`q950_boot_etalon` 640×480×8 at 33.333 MHz on its own `$3DC27823` ROM.
Milestones and the four "inherited Q700 rule" bugs it cost:
`docs/IOP_BRINGUP.md` § 5b / § M7, `CHANGELOG.md` 2026-08-01 → 2026-08-02.
**M4 stays deferred and LOUD**: SCSIDMA true DMA + restartable handshake is
A/UX-only per `scsidma.cpp:12`, nothing in the Mac OS path needs it. The
multi-ID SCSI mirror it left behind is deduped — the bug that corrupted
`MacOS-7.6-boot.vhd` (`CHANGELOG.md` 2026-08-13 (seventh) for where that
volume's story ended).)*

### Remaining machines with the ROM already in `roms/`

| Machine | ROM on hand | New brick |
|---|---|---|
| **PowerBook Duo 210/250/270c/280** | `0024D346` / `015621D7` | none left — `MscMemory` + `PgePmu` ship; identity/clock variants of the gated Duo 230 |
| **PowerBook 150** | `FDA22562` | LCD framebuffer + 68HC05 PM — the `M68hc05` core already ships |
| **PowerBook 140-180** | `E33B2724` | LCD framebuffer + the M50753 Power Manager (a different MCU from the Duos') |
| **Portable / PowerBook 100** | `96CA3846` / `96645F9C` | LCD framebuffer + M50753 (740/6502) Power Manager |

### Assets for new profiles

Local, never committed (`hdv/` is gitignored): Infinite Mac copies of System 4.1
(floppy), 5.1 / 6.0 / 6.0.8 / 7.0 / 7.1 / 7.5 / 7.5.5 HD `.dsk`, plus
`HD20SC.vhd`, `boot.vhd` / `GISTPERSO-boot.vhd`, `MacOS-8.1-boot.vhd`.
(`MacOS-7.6-boot.vhd` was deleted as corrupt — the refusal it triggered is
closed, `CHANGELOG.md` 2026-08-13 (seventh); `runIIfx` still probes for it
first and falls through to `GISTPERSO-boot.vhd`, `main.cpp:1479-1481`.)
Full tree also at `../refs/infinite-mac/Images`. Missing files: fetch with
**Scrapling** (not raw `curl` through the sandbox proxy) — `Fetcher.get` /
`scrapling extract get` on
`https://raw.githubusercontent.com/mihaip/infinite-mac/main/Images/…`.
Flat HFS → SCSI façade: `ScsiDisk::open` (gate `scsi_hfs_facade_test`; offline
bake `tools/wrap_hfs.py`).

---

## 8. Cross-machine architecture

**Items marked [AR] come from the architecture review of the tree at
`d3bbd81` (2026-08-09)**, which measured the repository rather than reading
its docs. Landed since and recorded in `CHANGELOG.md` 2026-08-09: the gate
asset preamble, the Moira fork decision (`extern/moira/POM68K_VENDOR.md`
§ *Status*), the `MachineHost` CRTP extraction (six `*Machine` structs, 1 671 l.
→ `src/MachineHost.h` + 681 l. of genuinely per-platform code; `machinehost_test`
gates the queue ordering, framebuffer double buffer, both pacing branches and
the thread teardown that no test could link before, because `main.cpp` is the
only TU outside `pom68k_core`), the `etalon-core` tier (12 gates, one profile
per platform, 12/12 in 31 min 41 s; a name in `POM68K_ETALON_CORE` that stops
being a registered gate is a configure-time `FATAL_ERROR`), `docs_test`, and
`CHANGELOG_INDEX.md` (`tools/changelog_index.py`, 13 subsystem groups;
`docs_test` fails when it stops covering every dated entry — a generated index
that silently falls behind is worse than none, because it looks complete).

- [ ] **[AR] The `run*()` bodies: four done, seven to go.** The hosting was
  unified in 2026-08-09; the `run*()` functions that wire it up were not, and
  the diff between any two was almost entirely a *descriptor*. **The four
  `DafbMachine` runners collapsed on 2026-08-23** (`CHANGELOG.md`): one
  `runDafbGui<MachineT>` (`main.cpp:3756`) driven by a six-field
  `DafbRunnerSpec` (`main.cpp:3731`), plus a wrapper per platform that decodes
  its own model and constructs `mem`/`cpu` — 1433 lines → 568, and the three
  drift bugs the copies had accumulated (two window titles, one banner) died
  with them. **Seven remain** (`main.cpp:1075-4627`, one per platform bar the
  compacts, which run inline in `main()`): `runMacII`, `runIIfx`, `runLcII`,
  `runLc3`, `runVasp`, `runIIsi`, `runDuo`. The next natural group is the
  `SonoraStyleMachine` trio — `runLc3` (`:2355`), `runVasp` (`:2707`) and
  `runIIsi` (`:3008`) — which already share a machine template the same way
  the four just did. Then the harder half: `runDafbGui` still lives in
  `main.cpp` because it reads ~18 of its statics (`ScreenInput`, the key
  table, `machineMenu`, the `g*` engine hooks, `findPath`, `initDriveSfx`…);
  lifting those into a `GuiRunner.h` is what actually removes the concern from
  the file, and it is what the ratchet (`tools/check_file_sizes.sh`) exists to
  keep pressure on.
- [ ] **[AR] Separate fixture roles, then version them.** The gates never write
  their images (`ScsiDisk::open()` defaults `writeBack = false` at
  `ScsiDisk.h:67`, and **no test anywhere passes `true`**; `q605_persist_etalon`
  replays its reboot against the in-memory image) — but the GUI attaches the
  *same* `hdv/*.vhd` with `attachScsi(path, true)`, **twelve boot-volume sites**
  plus eleven secondary-disk loops. A mutable, unversioned file is not a
  fixture: that is how `MacOS-7.6-boot.vhd` was corrupted and the IIfx gates
  went red. **And it happened again, cheaper and quieter, on 2026-08-14**:
  `HD20SC.vhd` came back from something with `drVolAtrb` bit 8 SET, the System
  stopped scavenging it, its boot went from 295 SCSI commands to 178, and three
  boot gates that asserted on that count went red for two days
  (`CHANGELOG.md` 2026-08-15). Nothing was corrupted — a fixture merely
  *changed*, which is enough. Two steps left now that the preamble prints the
  digest: a read-only
  `hdv/ref/` distinct from the volumes the GUI mounts, then **extending the
  existing `assets.lock`** — which today pins only the three firmware dumps
  `--lle-aarch64` qualifies (label, size, SHA-256, path, profiles) — to the ROMs
  and disk images, plus a `tools/verify_assets.py` to check it. It distributes
  no copyrighted content and makes the drift *nameable*.
- [ ] **[AR] Env knobs: the gate is sound again, the *classification* is what
  is left.** `config_test` (`unit`, asset-free) checks `DEV.md` § 5 +
  `src/jit/POM68K_JIT.md` against the tree in both directions, and re-derived
  by hand on 2026-08-12 both directions are clean over the real surface of
  **148** distinct `POM68K_*` string literals in `src/`, `tests/` and the Moira
  fork. What was fixed that day and must not be undone:
  `tests/config_test.cpp:72-101` turns any backticked token ending in `*` or
  `_` into a *prefix*, and § 5's own "How this list stays true" paragraph used
  to spell the harvest target as a backticked POM68K wildcard — inside the
  section, since `section()` cuts at the next `\n## ` — which registered the
  empty prefix and made direction 1 unable to fail. **Write meta-references to
  the namespace without backticks**, and keep real wildcards family-scoped
  (`POM68K_JIT_*`, `POM68K_JIT_LOCKSTEP_*`, `POM68K_BENCH_*`, `POM68K_PROBE*`,
  `POM68K_LLE_AARCH64_*`): one broad one re-opens the hole. The two knobs the
  wildcard had been hiding are no longer both active :
  `POM68K_JIT_A64_STORE_GUARD_OPCODE` was removed when the exact guard became
  global on 2026-08-20; `POM68K_JIT_ICACHE_EMIT` remains documented in
  `src/jit/POM68K_JIT.md` § 6.
  **Expiry: still not done.** The bring-up probes declare their chantier; the
  other ~140 entries still do not say whether they are a permanent product
  option (which earns a gate) or a chantier leftover. That is a decision per
  knob — the mechanism is in place, the classification is not.
  (The `KNOB=0` trap — many knobs test only for existence, so zero *activates*
  them — is documented once, in `DEV.md` § 5's preamble. Do not re-list it
  here.)
- [x] **`quadra_event_scheduler_test` silently drops out of `ctest -L m040`
  — FIXED 2026-08-12.** The derivation loop now MERGES explicit labels
  instead of overwriting (`CMakeLists.txt:2174-2188`), and the registry
  confirms the gate carries `unit,m040`. Residual to verify once with
  `POM68K_PRODUCT_LLE_GATES=ON`: that the inline `a64` label on the LLE
  preflight gates survives too (the merge fix should cover it). While
  there: the etalon-count comment now at `CMakeLists.txt:2106-2119` says
  91; the registry says **118** — update it on the next pass through that
  file.
- [x] **The Machine menu makes "Macintosh II" unclickable once IIx is picked
  — FIXED 2026-08-12** (`src/main.cpp:840-853` matches EXACT
  case-insensitive, not `strstr`). Secondary observations that still hold:
  the `ii` token is dead — the runtime dispatch (`:5688-5691`) only
  recognises `iicx`/`se30`/`fdhd` and reaches the plain Mac II by ROM
  checksum (`$9779D2C4`) — and `fdhd` is a runtime-only token with no
  catalogue row.
- [ ] **Save states — one residual.** The feature ships across all **12**
  machine families and **37** profiles (archive core `src/SaveState.h/.cpp`,
  container `SaveStateMachines.h/.cpp`, `MoiraSnapshot.h`, GUI/CLI wiring in
  `main.cpp` `SaveStateSlot`). Gates: `savestate_test`, `savestate_v8_test`,
  `savestate_030/040/68k_test` (all `unit`), plus the real-OS
  `lcii_savestate_etalon` and `q605_savestate_etalon`.
  **Remaining: a hands-on GUI pass** — click « Sauver l'état » / « Restaurer
  l'état » on a booted machine. The machine-level save/load is gated; the GUI
  layer is compile-verified only. (More generally: **there is no automated GUI
  gate at all.** The 2026-08-09 pass under a dedicated Xvfb proved a screenshot
  harness is possible — and found a bug no gate could see, the 040 loops
  auto-inserting `disks35/Disk605.dsk` so the Quadra booted System 6.0.5 off
  it — but it created no gate.)
  Conventions the chunks follow, worth keeping: callbacks and cross-device
  pointers are **re-bound, never serialized** (a pointer becomes an index —
  `Ncr5380::disk_`); pure caches are **flushed** on restore (ATC via the one
  vendored line `Moira::pomFlushAtcs()`, the 030 i-cache, the JIT guard) rather
  than carried; host-backed bulk data stays on the host (`ScsiDisk` ships a
  copy-on-first-write log of what the guest changed, not the image).
- [ ] **Optional HLE acceleration overlay** — **BLOQUÉ derrière § 3** par la
  décision du 2026-08-09 (§ 0·A) : on épuise le conformant d'abord, le HLE et
  le JIT non conformant ensuite, jamais l'inverse. Ce que cette décision fixe
  aussi, et que le doc laissait ouvert : la bifurcation § 2.3 de
  `HLE_OVERLAY.md` (HLE invité vs profil JIT relâché) se tranche en faveur du
  **profil JIT relâché côté hôte**, parce que le besoin réel est du *débit CPU
  soutenu sur 030/040*, pas de la latence d'I/O — patcher `.Sony` ne fait pas
  tourner le Finder d'une LC II plus vite. Le HLE au niveau invité redevient
  secondaire, **sauf QuickDraw**, qui est le seul endroit où le gain n'est pas
  borné par le taux d'instructions (§ 0·A).
  Reste vrai par ailleurs (`docs/HLE_OVERLAY.md`) : commencer par un seul hook
  caché sur `boot.checksum` et un mode de test d'accuracy HLE-interdit ; puis
  des modules appariés par signature, des gates A/B par module et un indicateur
  visible de mode non conformant.
  **The premise moved 2026-07-31**: the conformant JIT measures **×2.68** on
  `q605_boot_etalon` (61.3 s → 22.9 s), i.e. THROUGH the "~×2.5-3 conformant
  ceiling" this item used to invoke as its justification — so the overlay can no
  longer be sold as the way to make POM68K fast. Its surviving argument is
  narrower and sharper: the residual idle-Finder cost is the ATC-exactness
  contract itself (794 M window-lost exits over 12.2 G instructions), and a
  non-conformant mode is exactly where the five relaxations the JIT refuses
  become legal. `docs/HLE_OVERLAY.md` § 0 dates every premise; read it before
  building anything. Note that the JIT reaches **every** CPU wrapper
  (2026-08-06), so HLE is no longer the only accelerator anywhere — but on the
  68000 and 68020 guests the window is worth ×1.0-1.1, because there is no ATC
  walk to skip, which is precisely where an HLE overlay would have room.
- [ ] **Retro68 as a guest-level differential oracle**: build small Toolbox /
  Device Manager / XPRAM probes, run identical binaries under MAME and POM68K,
  compare. Known friction: no `Lists.h`/`AppleTalk.h` shims in multiversal
  (hardcoded list in `cincludes.rb`); the prober's `compat/` carries the
  PBControl glue; a 0-byte `.APPL` is normal.
- [ ] **Refactor the remaining GUI globals**: move compile-unit state such as
  `demoMode` into a machine/UI status object; keep machine threads, command
  queues and Emscripten's single-thread path behaviourally aligned. *This is
  the visible tip of the `run*()` item above — "keep the paths behaviourally
  aligned" is exactly the obligation a single descriptor-driven runner would
  discharge structurally instead of by hand.*

---

## 9. One lesson kept out of the closed list

Everything closed before 2026-08-10 now lives in `CHANGELOG.md` by date and in
`CHANGELOG_INDEX.md` by subsystem; this file no longer duplicates it. One line
survives because it is *about* this file:

**Block linking** (`POM68K_JIT.md § 9`) had a stale one-line summary here that
misdirected a whole planning round on 2026-07-30. It is a link *table* (O(1)
slot invalidation, no patched jumps); loading-phase block entries −53 %,
268 → 566 instr/entry, 18.4 → 16.75 s; at the idle Finder it helps less because
the hot exits are `JSR`/`RTS` (still `Unsafe`). **That is why the house rule at
the top of this file exists**: a closed item leaves at most one line, and that
line has to be true.
