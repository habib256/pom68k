# TODO

**Active work only.** Resolved work, investigation trails and design rationale
live in `CHANGELOG.md` (`CHANGELOG_INDEX.md` groups its dated entries by
subsystem), implementation detail in `DEV.md`, vendor notes in
`extern/*/POM68K_VENDOR.md`, LLE inventory in `docs/LLE_VS_HLE.md`, JIT design
in `src/jit/POM68K_JIT.md`, conformant-JIT plan in `docs/JIT_BRINGUP.md`.

**Counts re-verified against `CMakeLists.txt` and the code on 2026-08-25** —
re-verify before quoting them anywhere. **Since 2026-08-29 the registry's
numbers are GENERATED into `STATUS.md`** (`tools/status_md.py`, from the
configure-time roster + manifest; `docs_test` re-derives the artifact and
fails on drift) — read them there instead of re-deriving them here:

- **The gate registry is host-conditional, so a single number is always wrong
  somewhere.** The documented registry (2026-08-29): **238 gates** — 113
  `unit`, 88 `asset-none`, 9 `smoke`, 41 `jit`, 54 `m040`, 56 `m030`, 124
  `etalon`, 12 `etalon-core` (`input_journal_test` joined on 2026-08-29;
  `jit_backend_parity_test` and the LC 520
  beyond-boot pair on 2026-08-28). Five are host-conditional — the
  AArch64 trio `jit_lockstep_a64_coarse_test` +
  `jit_lockstep_030_a64_experimental_test` +
  `jit_lockstep_030_a64_alignment_test` (the first also joins `smoke`) and
  the x86-64-only `jit_lockstep_030_x64_experimental_test` +
  `jit_lockstep_030_x64_alignment_test` — so an x86-64 configure sees
  **235** (110 `unit`, 8 `smoke`, 38 `jit`) and an AArch64 one 236 (111
  `unit`, 39 `jit`) — union minus the three, resp. the two, that host
  cannot register (previous totals measured `ctest -N` 2026-08-28, x86-64). Eight more
  exist only under `-DPOM68K_PRODUCT_LLE_GATES=ON`
  (default OFF, `CMakeLists.txt:466`, and it FATAL_ERRORs off AArch64 in
  `cmake/Pom68kJitGates.cmake:458-464`).
  `unit` is *not* "asset-free" — it remains the legacy "name does not end in
  `etalon`" label. `asset-none` is the manifest-declared daily tier.
- **Last FULL suite: 228/228 on AArch64, 2026-08-27 — and 224 of them
  actually EXECUTED.** One `ctest -j16`, **1 089.79 s wall (18 min 10 s)**,
  exit 0, zero red, after a full build with `check_binaries_fresh.py
  --self-test` green and 152/152 gate executables current. The census
  (`tools/gate_execution_census.py`) says 224 executed, 4 soft-skipped for
  named missing assets (§ 1). **Quote the pair, never the first number
  alone.** `-j` is a RAM budget in 256 MiB units, not a core count: the same
  registry that day ran 18 min 02 s / 18 min 10 s under `assumed` budgets,
  **30 min 04 s** at `-j16` once measured rows landed, and 20 min 12 s at
  `-j64`. Summed over its own gates the run is 17 162 s (4 h 46), so the
  parallel schedule is worth **x15.9** — the "4 h 30" this file used to quote
  was an invocation habit, not a property of the suite. Full protocol and the
  four runs that priced it: `CHANGELOG.md` 2026-08-27 (sixth), `docs/MEASURING.md`.
  **The `make` is part of the claim**: `ctest` does not build, a phantom
  failure gets investigated and a phantom pass gets quoted.
- **37 machine profiles** = 37 `kMachineProfiles` rows in
  `src/MachineCatalog.h`, each carrying its stable `SnapMachine` id = 21
  `MachineKind` values over **12** platform implementations. `docs_test`
  compares the compiled catalogue directly with `CLAUDE.md`. These numbers
  describe current coverage; they do not close the all-68k-Macintosh target.

House rule for this file: an item earns its place by saying **what to do next**,
concretely. When it lands, it moves to `CHANGELOG.md` and leaves at most one
line here.

Second house rule, adopted with the reorganisation of 2026-08-26 and enforced
again on 2026-08-28: **open items come before closed ones inside a section**,
and a closed item leaves a single line plus its date in `CHANGELOG.md`. A
section that reads like a story has stopped being a backlog.

Third house rule, adopted 2026-08-28 with the same pass: **a block that says it
is the only record of something must be verified against `CHANGELOG.md` before
it is shortened, and verified into it if it is.** Three such blocks were found
and moved on that date (the 2026-08-11 performance pass, the synthetic-Toby
fixture trap, the four 68030/MMU/FPU oracle closures) — `CHANGELOG.md`
2026-08-11 and 2026-08-12 (third).

**Section numbers are stable on purpose** — ten other documents cite them
(`CLAUDE.md` § 7, `DEV.md` § 8, `docs/HLE_OVERLAY.md` § 8, `docs/APPLETALK.md`
§ 6…). Reorganising means moving items *inside* the numbering, never renumbering
it. § 10 was appended on 2026-08-28 for exactly that reason.

| § | What it holds | The next concrete action |
|---|---|---|
| **0** | Cap produit, séquencement, fenêtre de consolidation | Les deux moitiés `unit` sont mesurées et exécutées ; décider la fermeture de la fenêtre — la dernière case n'attend plus de run |
| **0·A** | Direction produit : la vitesse et l'ordre dans lequel on la paie | Le mode nominal tient enfin ×1 (paceur à échéance absolue, 2026-08-28 — il plafonnait à ~×0,75 partout) ; reste : `POM68K_TURBO` scriptable pour re-mesurer AppleTalk à bras égaux, et la ligne de base Pi 400 |
| **0·B** | Les six réserves de la revue externe du 2026-08-26 | Recensement g++ FAIT le 2026-08-28 (17 lignes, 16 sites) : reste à corriger les 16 puis armer `-Werror` ; et le premier rapport de fuites Linux |
| **1** | Aucun rouge JIT ouvert ; rouges de fixture/timeout, ouverts non rouges, règles de méthode | Réparer les fixtures `macii_persist` / `q605_afp_live`, puis la marge IIvx ; le mode-2 x64 reste une ABBA séparée |
| **2** | Profondeur de test au-delà du boot — le plus gros manque | Prochaine paire beyond-boot : la famille AIO ; et la charge applicative de § 3 |
| **3** | JIT, second moteur d'exécution | Bitfields et couverture division clos sur les deux générateurs ; reste la promotion applicative, puis un PROFIL TEMPOREL pour le prochain levier mural |
| **4** | Fidélité LLE — remplacer les raccourcis HLE | Étendre les commandes Cuda du Q605/LC 475 seulement sur preuve ROM/pilote |
| **5** | Backlogs par machine | Étalon de montage/boot 1,44 Mo au niveau invité |
| **6** | Réseau — AppleTalk, LocalTalk, MacIP, Ethernet-sur-SCSI | Fermer la course de l'ACK de défense d'adresse lapENQ |
| **7** | Nouveaux profils machine | Les majeurs indépendants : ligne PowerBook, NuBus + vidéo sur slot |
| **8** | Architecture transverse | Passe GUI à la main sur ROM réelle pour les save states |
| **9** | La leçon gardée hors de la liste des clos | — (elle est *sur* ce fichier : un item clos laisse une ligne, et cette ligne doit être vraie) |
| **10** | **Revue d'architecture du 2026-08-28** — quatre constats, un plan par vagues | Vague 0 : le JIT x86-64 est compilé dans le binaire Windows et suit l'ABI System V |

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

**Le chantier `run*()` + injection de configuration est CLOS depuis le
2026-08-26** : plus aucun corps `run*()` autonome, une seule lecture
d'environnement dans le produit, relance et overrides typés, et un
`CMakeLists.txt` racine qui ne compose que cinq modules `cmake/`. Récit daté
des onze passes : `CHANGELOG.md` 2026-08-23 → 2026-08-26. Ce qui le tient :
`file_size_budget_test` borne chaque unité issue du découpage et `docs_test`
interdit le retour d'une lecture de processus ou d'un `CoreConfig` complet
dans un composant feuille. **Ce que ces gardes ne prouvent pas, c'est la
cohésion : § 0·B, puis § 10.**

La fenêtre se ferme quand cette dernière case est vraie — les six autres le
sont depuis le 2026-08-25 (`CHANGELOG.md` à leurs dates) :

- [ ] **Le palier legacy `unit` n'a plus d'échec inexpliqué sur les deux
  architectures hôtes.** *Moitié x86-64 mesurée le 2026-08-17* (GCC 13,
  conteneur neuf, arbre reconstruit from scratch) : `asset-none` **83/83**
  sans un seul SKIP, `unit` **104/104**, zéro échec. Ce que ce vert ne dit
  pas, et qu'il faut lire avec lui : sur les 21 gates de `unit` hors
  `asset-none`, **20 étaient des soft-skips** faute d'assets privés — un
  palier « vert » de 104 dont 84 s'exécutaient. `sst68000` a depuis rejoint
  les exécutants (1 000 058 / 1 000 058 sur 124 fichiers, 6,67 s), ses
  vecteurs étant le seul asset manquant qui soit public.
  **La moitié AArch64 est mesurée le 2026-08-28** — sur CET hôte, qui porte
  les assets : `make` complet 0 avertissement, `check_binaries_fresh.py`
  156/156, puis `ctest -L unit -j16` **110/110 en 52 s** et le recensement
  dit **110 exécutés, 0 soft-skip, 0 échec** — le palier entier s'exécute
  ici, là où le vert x86-64 du 2026-08-17 cachait 20 soft-skips.
  **Reste à faire pour cocher** : rejouer le palier sur un hôte x86-64
  *portant les assets* — le seul hôte de ce type n'existe pas encore ; c'est
  la même machine-unique que § 10 constat 3 veut doubler d'un runner.
  **La mesure est faite le 2026-08-30** : les deux premiers runs COMPLETS
  du registre sur l'hôte x86-64 portant les assets (233/235 puis 232/235,
  `STATUS.md`) contiennent le palier `unit` entier **vert et exécuté** les
  deux fois — les échecs sont trois etalons, deux sur l'état des fixtures
  de cet hôte (§ 1) et un sur une marge de TIMEOUT. La case attend la
  décision de séquencement, pas une mesure.

`docs_test` vérifie la cohérence dynamique du catalogue compilé, des gates,
des budgets et de la documentation ; il ne doit contenir aucun nombre plafond
qui transforme par accident l'état courant en restriction produit.

---

## 0·A. Direction produit — la vitesse, et l'ordre dans lequel on la paie

**Décision utilisateur, 2026-08-09.** Le problème n°1 de POM68K est sa
**vitesse d'exécution**. Toutes les machines doivent être *utilisables*, quitte
à perdre la conformité sur les plus puissantes.

**L'ordre est fixé et il n'est pas négociable :**

> **1. D'abord épuiser toutes les accélérations possibles en LLE et en JIT
> conformant. 2. Ensuite seulement, ajouter du HLE et du JIT non conformant.**

C'est la règle § *Principle* de `docs/LLE_VS_HLE.md` appliquée à la
performance : le raccourci ne se mesure et ne se valide que contre une
référence conforme qui existe déjà. Concrètement, **§ 3 est le chemin critique
du projet**, et l'item *Optional HLE acceleration overlay* (§ 8) est
explicitement **bloqué derrière lui**.

**Ce que la bascule non conforme rapportera — estimation, à ne pas citer comme
mesure.** Base mesurée (`POM68K_JIT.md` § 3.4, budget fixe de 3 000 frames sur
Q605, empreinte identique sur les trois moteurs) : interpréteur 48,51 s →
`threaded` 28,10 s (**×1,73**) → `x86-64` 9,71 s (**×5,00**). Les leviers non
conformants restants composent à **×1,3 à ×2,0 sur le JIT actuel**, soit de
l'ordre de **+50 %** — ce n'est pas un changement d'échelle. Raison
structurelle : le générateur x64 *est déjà* un vrai JIT ; les relaxations d'un
JIT 68k classique servent surtout à *atteindre* cet état. On ne rachète que la
taxe. Deux leviers ont été **mesurés et réfutés** et ne doivent pas revenir
dans un plan de perf : le soft-TLB / ATC relâché (≈3 %, pas « +10 à 30 % ») et
les lazy flags (≈0,8 %) — détail dans *Measured and DROPPED*, § 3.

**Le changement d'échelle par HLE reste une hypothèse, pas le prochain
chantier.** Le recensement Rogue refait le 2026-08-23 a fait tomber les replis
de 102,85 M à 0,534 M sans démontrer aucun chemin copie/remplissage VRAM
dominant. Un HLE QuickDraw resterait le levier le plus invasif et ne
profiterait pas aux moteurs qui écrivent directement dans la framebuffer ; ne
l'ouvrir qu'après une fréquence et un gain A/B mesurés.

### Deux garde-fous à poser AVANT la première ligne de code non conforme

1. **Ne jamais relâcher l'horloge périphérique/MCU.** Relâcher l'exactitude
   côté CPU, oui ; le temps vu par le VIA, l'Egret/Cuda, l'IWM/SWIM reste sur
   le compteur machine. Ce tree a trois cicatrices qui disent pourquoi : la
   Mac TV deadlocke sur **2 %** de dérive du taux d'instructions MCU ; le boost
   i-cache comprimait le dénibblage sous le hold de 14 ticks de l'IWM →
   `badDCksum` ; `CudaLle::tick` doit reporter son dépassement en `mcuDebt_`
   sous peine de suroverclocker le MCU de ~37 %.
   Le **temps grossier est la relaxation la plus dangereuse ici**, pas la plus
   rentable — la prendre en dernier, machine par machine.
2. **L'instrument de mesure ne survit pas au mode non conforme.** Chaque
   chiffre de la table § 3 a été pris avec les trois moteurs imprimant la
   **même empreinte** d'état architectural. Un profil relâché imprimera une
   empreinte différente **par construction**. Il lui faut donc son critère
   d'équivalence *fonctionnel* **avant** toute mesure, sinon « plus rapide » et
   « cassé mais rapide » deviennent indiscernables.

**Forme visée (pas un booléen).** Un **profil de fidélité gradué** avec des
défauts **par famille de machine** — compacts verrouillés au profil conforme,
030 et 040 relâchés par cran. Le *purity mode* de `docs/HLE_OVERLAY.md` § 7
s'applique tel quel : tous les gates d'accuracy forcent le profil conforme,
`abort()` si quelque chose tente de l'armer.

### Ce qui manque pour transformer tout ceci en plan

- [ ] **Une ligne de base sur le matériel cible.** Aucun chiffre POM68K
  n'existe sur un vrai Pi 400 (§ 3, *Build recipe*) et « inutilisable » n'est
  pas une cible tant qu'on ne sait pas *combien* il manque. Mesurer avec
  `jit_bench` **ou `jit_bench_lcii`** (`POM68K_BENCH_FRAMES`), **jamais** un
  boot etalon. Les deux impriment un **× temps réel**, seule forme du chiffre
  qui se compare d'un hôte à l'autre — et la seule qui se compare à la cible,
  qui est « ×1 ou mieux », pas « n secondes ».
- [ ] **Rejouer l'A/B sur un vrai Pi 400** : même instrument que la case
  ci-dessus.
- **L'écart GUI est CLOS le 2026-08-28, et l'attribution AppleTalk du
  2026-08-25 est RÉTRACTÉE** (`CHANGELOG.md` 2026-08-28 (ninth)). Le voleur
  était le paceur : `stepTick` dormait `16 625 µs − coût d'émulation`, en
  laissant `publish()`, `drainAudio()` et le grain de réveil de `nanosleep`
  hors budget — période ~21 ms, **tout mode nominal plafonné à ~×0,75 sur
  les douze familles**, AppleTalk on OU off (×0,76-0,80 contre ×0,70-0,73,
  déjà le mauvais signe pour l'histoire AppleTalk ; profil `sample` : 86 %
  du thread machine dans `sleep_for`). Échéance ABSOLUE 60,15 Hz calculée
  après `publish()` + resync à 3 frames : **×0,997-1,03 sur les deux
  bras**. Les ×9,235/×0,845 de l'attribution correspondent à turbo-vs-pacé,
  pas à AppleTalk-vs-rien. L'hypothèse « 64 tranches cassent les chaînes
  JIT » a été chiffrée et réfutée en chemin (`POM68K_BENCH_SLICES` : −2 %
  JIT, −0,7 % interp).
- [ ] **Le coût AppleTalk en régime TURBO reste non mesuré à bras égaux** —
  la seule question qui survit à la rétractation. Il faut d'abord un
  `POM68K_TURBO` scriptable à travers la chaîne de configuration injectée
  (le turbo est un clic de menu sans bouton d'environnement, ce qui est
  précisément comment les deux bras du 2026-08-25 ont divergé) ; puis les
  deux bras SPEED_LOG en turbo, hub on/off, même lancement.

**La jauge de vitesse du GUI a atterri** : le menu CPU affiche le ratio temps
réel sur les douze familles, calculé à partir de `machineClock()` publié
séparément du `getClock()` boosté ; `machinehost_test` verrouille que la source
est bien l'horloge machine. `POM68K_SPEED_LOG=1`, `_SKIP` et `_COUNT` exposent
le même calcul à un protocole scriptable. **Mesure GUI 2026-08-11**, quatre
copies jetables du même disque 8.1 : Q605 **×2,596**, Q630 **×2,274**,
Centris 650 **×3,448**, Q700 **×3,394** — application complète, pas le débit
isolé de `jit_bench`.

**La passe perf du 2026-08-11 — promotions, rejets et attribution — est
maintenant dans `CHANGELOG.md` 2026-08-11**, versée le 2026-08-28 depuis ce
fichier, qui en était le seul enregistrement. Les rejets qu'elle contient
(échéancier ASC, index secondaire ATC, seuil `codeMask` 128 o, dette DAFB
sérialisée, cache d'échéance SCC) **ne se rouvrent pas sans données
nouvelles**.

**La suite conforme, dans l'ordre :** étendre l'échéancier événementiel à un
troisième périphérique Q605 — *seulement* après avoir ajouté son flush MMIO et
son état de dette au même ensemble de gates — puis isoler par opcode les stores
masqués-nuls libérés par le vrai test du masque. Toute promotion exige les
quatre mêmes preuves : empreinte et compteurs identiques, gain **répété**,
gates ciblés verts, tier `etalon` complet vert.

---

## 0·B. Revue externe du 2026-08-26 — les six réserves

**Les six sont posées depuis le 2026-08-27** (récit et chiffres :
`CHANGELOG.md` de ce jour, entrées 7 à 12) : politique d'avertissements,
sanitizers, couverture, recensement d'exécution, registre LLE possédé par la
session, fan-in de composition. Ce qui reste ouvert est deux lectures de CI et
rien d'autre :

- [ ] **Lire le premier recensement g++ 13 que `ci.yml` publie**, corriger ce
  que GCC ajoute, puis basculer `-DPOM68K_WERROR=ON` dans ce job. Les deux
  compilateurs n'avertissent pas des mêmes choses et cet hôte n'a que clang,
  qui compile **0 avertissement** sur tout l'arbre.
  **Lu le 2026-08-28, et la lecture était vide** : la CI x86-64 était rouge
  depuis le 2026-08-24 (voir § 1), donc le job échouait AVANT le build et le
  recensement lisait un `build.log` inexistant — en imprimant « 0 warnings »,
  la forme exacte du phantom pass. Deux correctifs le même jour : le rouge
  amont est fermé, et le recensement dit désormais « NO CENSUS » sur log
  manquant au lieu de compter zéro.
  **La première lecture RÉELLE est faite le 2026-08-28**, pas depuis la CI
  mais avec son instrument exact (g++ 13.3.0, `POM68K_NATIVE=OFF`,
  `POM68K_LTO=OFF`, la recette de comptage de `ci.yml`) sur l'hôte x86-64 :
  **17 lignes d'avertissement dans nos sources, 16 sites distincts**, zéro
  hors `extern/`/`imgui/`. Répartition et sites : `CHANGELOG.md` 2026-08-28
  (twelfth). Aucun n'est un bug avéré, un seul mérite lecture
  (`-Warray-bounds=` sur le `memcpy` d'`EtherLink`). **Le comptage de la CI
  a lui-même un angle mort trouvé en s'en servant** : son
  `grep -o "\[-W[a-z0-9-]*\]"` ne matche pas `[-Warray-bounds=]`, donc son
  tableau par option perd exactement le seul avertissement non cosmétique.
  Reste à faire pour cocher : corriger les 16 sites, corriger le motif du
  recensement, puis basculer `-DPOM68K_WERROR=ON` dans ce job.
- [ ] **Lire le premier rapport de fuites Linux** de la nightly pour rendre
  son étape `detect_leaks=1` bloquante. Elle est non bloquante parce qu'elle
  interroge un *autre* runtime, pas parce que notre code est suspect :
  `leaks --atExit` sur les **78 binaires** du palier sans assets donne **0
  fuite, 0 octet** ici, et les 288 du binaire GUI sont toutes des cycles
  `NSXPCConnection` d'Apple, sans une seule trame POM68K dans une pile.
  **Vérifié le 2026-08-28 : aucun rapport n'existe encore** — la dernière
  nightly planifiée (2026-08-27 13:58) précède la fusion du step, et sa
  jambe x86-64 échouait de toute façon sur le rouge de § 1, maintenant
  fermé. La prochaine nightly est la première qui peut en produire un.

**Résidu légitime, écrit pour ne pas être reclassé en dette** : le
`thread_local` du JIT (`src/jit/JitConfig.h:241`) et le cache d'options par
thread de `JitBackendA64.cpp`. Un thread = une machine reste vrai ; cette ligne
est leur condition de réouverture.

**Réserve transverse, sans case à cocher : `docs_test` teste la forme.** Ses
2 075 lignes vérifient des noms, des chaînes et des ordres de construction, et
le ratchet de taille empêche surtout le retour d'un très gros fichier. Ces
gardes sont utiles — chacune est née d'un bug réel — mais elles ne prouvent pas
la cohésion et elles cassent au renommage. **Règle adoptée le 2026-08-26** :
un nouveau contrôle structurel n'entre dans `docs_test` que s'il **nomme le bug
qu'il aurait attrapé** ; sinon la réponse est un gate comportemental —
`gui_smoke_test` est le modèle : vraie fenêtre, trois frames, bascule moteur,
sauvegarde, fermeture RAII.

---

## 1. Red now

**AUCUN ROUGE JIT OUVERT au 2026-08-30.** Le lockstep 68030 x64 ci-dessous
est le dossier historique qui a mené aux correctifs du 2026-08-29 ; les
trois locksteps x64 sont verts et la moitié AArch64 vient d'être rejouée.
Découvert au premier `ctest -L unit` jamais lancé
sur un hôte x86-64 *portant les assets* : 104 exécutés / 1 soft-skip /
**4 échecs**. Trois sont `jit_lockstep_030_test`,
`jit_lockstep_030_x64_experimental_test` et
`jit_lockstep_030_x64_alignment_test`, en SIGSEGV déterministe ; le
quatrième gate 030, `jit_lockstep_030_blocks_test`, passe en 81,91 s et sa
seule différence est `POM68K_JIT_BACKEND=threaded`.

- **Le crash est corrigé** (Moira patch 30 : compteurs de journal MMU
  saturants ; `mmuIdx`/`mmuIdxDone` sont un état par instruction que seul
  `mmuExecuteStart()` remet à zéro, et `pomJitWriteData` ne passe jamais
  par là — au débordement signé la garde `mmuIdxDone < 10` redevient vraie
  et l'écriture part en `mmuAd[-2147483648]`).
- **Le défaut de fond ne l'est pas**, et après le correctif le même palier
  donne **106/109 — 105 exécutés, 1 soft-skip, 3 échecs — et les trois sont
  désormais `Timeout` (1800 s pleines), plus `SEGFAULT`** : la forme
  attendue.
- **Le refus de fenêtre n'était PAS le générateur x64** — mesuré le
  2026-08-28 (fourteenth) avec les compteurs `jit::ArmFail` posés aux quatre
  sorties de `armWindow()`. À 4 000 pas, `x64` et `threaded` sont identiques
  au compteur près (630 766 armements, 630 766 refus, 0 instruction native,
  8,36 s contre 8,29 s), et la sortie qui les prend est
  `degenerate 630 755` : `pageLen` vaut 1, parce que la branche 030 de
  `pomJitProbeCode` dérive la taille de page du champ PS de TC, **nul tant
  que l'OS n'a pas programmé TC**. Borné à la phase pré-MMU : les 965 013
  refus dégénérés d'un boot complet sont tous là avant ~8 000 pas et ne
  reviennent jamais — 95,6 % des refus du boot, et 32 pas de backoff chacun,
  soit ~31 M des 31,9 M d'instructions interprétées de cette phase.
  Concerne **tous les 030 et les deux ISA hôtes**, pas x86-64. Correctif
  proposé et NON appliqué (il bouge la résidence native des sept
  plateformes 030, donc il veut ses preuves, ses gates et son entrée
  `POM68K_VENDOR.md`) : n'utiliser `mmuPageMask()` que si le champ PS est
  légal (≥ 8), sinon la borne identité 4 Kio que la branche 020 emploie
  déjà — patch Moira 31.
- **Le tier m030 du 2026-08-29 est le fait neuf.** Build vert (157 binaires
  frais), puis `ctest -L m030` : **10 verts sur 33 rendus, 23 `Timeout`, et
  20 gates encore en vol après 11 h** — arrêtés à la main. Les 10 verts sont
  les six `interp_*_boot_etalon`, deux tests unitaires sans CPU et les deux
  gates du Mac LC, **un 68020** : aucune machine 68030 sous le générateur
  natif n'est passée.
- **Et le blocage NE VIENT PAS du patch 31 — mesuré, pas déduit.** Deux bras
  bâtis sur `d4a18b6` (HEAD, sans le patch) sur `lcii_boot_etalon` : x64
  épinglé **900,08 s exit 124**, et sans aucune variable — le défaut `auto`
  de HEAD — **900,06 s exit 124**. C'est donc une **régression produit** des
  huit jours écoulés depuis le 2026-08-21 : sur un hôte x86-64, au sommet de
  `main`, **aucune machine 68030 ne boote sous le moteur par défaut**, parce
  qu'`auto` sert le générateur x64 sur 030 depuis cette date et qu'aucun
  étalon 030 n'a tourné sur x86-64 depuis. Le code qui fige est du
  post-MMU ordinaire : à HEAD la fenêtre ne s'arme pas avant la MMU.
  Mitigation appliquée : `autoFamilies` x64 repasse à `kGuest68040` (§ 3) —
  elle répare la régression, elle ne compense pas le patch. **Le bisect a rendu son nom le
  2026-08-29 : `661a784` (2026-08-22), le garde de code par sous-tranches de
  32 octets.** Six marches testables, aucun saut : `015bea7` GOOD 104 s,
  `ac0f963` GOOD 101 s, `661a784` **BAD SIGSEGV 251 s**, puis tout BAD
  jusqu'à `d4a18b6` (qui fige 900 s au lieu de crasher, depuis que le patch
  Moira 30 a saturé le compteur qui débordait). Ce commit a bien fait
  tourner `-L jit|m030|m040` 131/131 — **sur l'hôte AArch64**, où `auto`
  sert le générateur a64 : la moitié x64 du changement n'a jamais tourné.
  Suite le 2026-08-29 (later) : le déclencheur est cerné aux thunks
  d'écriture exacte (mode 2) et un défaut par-(famille, backend) le
  plafonne à 1 — voir le bullet d'isolation ci-dessus.** Reste dû aussi : le même tier sur
  l'hôte AArch64, où `auto` sert toujours le générateur a64 sur 030.
- **Vingt gates de ce tier ne portent AUCUN `TIMEOUT`** — `lc3plus_`,
  `lc550_`, `cclassic2_`, `lc520_`, `iici_`, `duo230_`, `iisi_`, `iivx_`,
  `lc3_`, `mactv_`, `classic2_`, `cclassic_`, `lcii_sys7_`, `lcii_savestate_`,
  `iifx_post_`… — donc un blocage y mange l'hôte indéfiniment (11 h ici, load
  20 sur 16 cœurs). Un gate sans borne n'est pas un gate, c'est un pari :
  leur en donner une, alignée sur les 1800 s des autres etalons.
- **Le wedge est ISOLÉ à un interrupteur, 2026-08-29 (later) : les thunks
  d'écriture exacte** (`POM68K_JIT_ACCESS_THUNK=2`, le défaut). Au banc
  250 frames : mode 2 **>600 s coupé**, mode 1 **1,27 s** avec l'empreinte
  exacte de `threaded`, et `lcii_boot_etalon` épinglé x64 mode 1 atteint le
  Finder en **53 s** (chemin produit `threaded` : 119 s). Le core SIGABRT
  pris en plein wedge est dans `serviceGuard()` — le moteur, pas le code
  généré ; le lockstep x64 est identique pas à pas jusqu'à 5 500. Mitigation
  livrée : `BackendCaps::maxAccessThunk030 = 1` déclaré par x64, consommé
  dans le ctor de l'Engine (motif `profitScore68030`), l'override env
  explicite gagne — le mode 2 reste atteignable pour la chasse, le 040 garde
  son mode 2 (gates verts). **Le tier m030 est repassé sous le clamp le
  soir même : 56/56 en 29 min 35 s** — et le gate de base
  `jit_lockstep_030_test`, dont le harnais passait par `auto`, tournait
  `threaded` en silence depuis le retrait du flip ; il épingle désormais
  `${POM68K_JIT_NATIVE_BACKEND}`. Les trois locksteps, rouges sur le ±2 à
  cet instant-là, sont verts depuis la fermeture du (night).
  `CHANGELOG.md` 2026-08-29 (later) et (evening).
- **Le ±2 est CLOS le soir même** (`CHANGELOG.md` 2026-08-29 (night)) :
  c'était le pipe de fetch post-PMOVE (patch 11), servi AVANT les compteurs
  i-cache — l'interpréteur ne compte rien dans l'ombre d'un PMOVE, un bloc
  natif y chargeait tout. Fix structurel : `pomMmuPipeLive()` (patch Moira
  32) + refus d'armement dans l'ombre (`ArmFail::Pipe`). **Les trois
  locksteps épinglés x64 : 120 000 pas identiques chacun — plus aucun gate
  030 rouge sur cet hôte.** Boot épinglé inchangé (52,84 s), banc à
  l'empreinte de `threaded` à ×5,03 temps réel.
- **Le storm mode 2 est MÉCANISÉ et CASSÉ dans la nuit** (`CHANGELOG.md`
  2026-08-29 (late night)) : une boucle moteur sans retirement — trip sur
  masque 32 octets, éviction exacte à l'octet (rien ne tombe : le flag
  `$4FC4` du handshake Egret vit à 4 octets du code traduit à `$4FC8`), et
  la porte guard qui re-exécute l'écriture pour toujours. Née dans
  `661a784` (avant lui, un trip évinçait sa tranche entière). Fix : après
  un `WindowLost` trippé, UNE instruction retirée par l'interpréteur
  (`Miss::GuardReplay`). Banc mode 2 : >600 s → **1,25 s**, empreinte de
  `threaded`, ×7,34 ; lockstep 120k mode 2 + admissions : identique.
  **Le plafond `maxAccessThunk030=1` RESTE** — la re-promotion du mode 2
  est un item D.1 nommé (un ABBA mode 1 vs 2 + tier), pas un réflexe.
- **Le tier 030 AArch64 est REJOUÉ le 2026-08-30** : les 56 gates `m030`
  sont tous verts dans la sélection fraîche `jit|m030|m040`; les deux
  locksteps natifs et celui d'alignement sont verts. Reste l'ABBA de
  re-promotion du mode 2 si ses +3 % (un run) survivent au protocole.
- **C'est le chemin 030, pas le générateur x64.** Dans le même run,
  `jit_lockstep_x64_test` (26,30 s) et `jit_lockstep_x64_fine_test` (3,95 s)
  — les locksteps 68040 contre ce même générateur — passent, ainsi que
  `sst68000` (1 000 058 vecteurs), `sst68030` et `sst68040`, ce qui prouve
  au passage que le patch Moira 30 laisse l'interpréteur bit-identique.
- **Ni le crash ni la lenteur ne datent du lot du 2026-08-28** : un worktree
  bâti sur `715efca` meurt à l'identique (exit 139, 289,43 s sur 8 000 pas).
- **Ces trois gates n'existent que sur x86-64**, et l'hôte de développement
  est AArch64 : c'est § 10 constat 3 en acte, et la raison pour laquelle la
  ligne ci-dessous a pu s'écrire. `CHANGELOG.md` 2026-08-28 (thirteenth).

**Deux autres rouges du même run, fermés le soir même** — l'arbre **ne
compilait pas** sous g++ 13 (`std::sqrt` sans `<cmath>` dans
`tests/q605_hotfloppy_probe.cpp`, qui est le gate `q605_hotfloppy_etalon` et
non le « dev harness » que son en-tête décrivait), et `config_test` était
rouge sur **tout** arbre x86-64 depuis le matin (le knob
`POM68K_JIT_030_MEMBF` cite comme preuve un gate AArch64-only ; `config_test`
lit désormais `pom68k_gates_absent.tsv` comme `docs_test`).
`CHANGELOG.md` 2026-08-28 (twelfth).

**Les deux rouges du 2026-08-28 (jour) sont fermés**, chacun avec son entrée
`CHANGELOG.md` :

- **Le rouge Windows de la revue § 10 est FERMÉ le jour même** (vague 0
  posée) : la branche `auto` x64 exclut `WIN32`, `X64Backend::usable()`
  refuse sous `_WIN32` avec le commentaire d'ABI, et les jobs de release
  Windows **et** macOS construisent les tests et lancent `ctest -L
  asset-none` — la première exécution Windows de ce palier est le vrai test,
  à lire, pas à faire taire. La question ouverte (Windows garde `threaded`
  ou le backend apprend l'ABI Win64 derrière un gate) reste § 10 vague 0,
  indécidable sans hôte Windows. `CHANGELOG.md` 2026-08-28 (later).
- **Un second rouge, découvert en LISANT la CI (§ 0·B) : la jambe x86-64 de
  `ci.yml` était rouge depuis le 2026-08-24** — 26 runs consécutifs en
  échec, jamais lus. Cause : le commit Rogue du 2026-08-24 a ajouté des
  abaissements a64-only (LEA full-index direct/indirect, JSR indirect,
  l'ordre `(An)+`) et `jit_asset_free_lockstep_test` affirmait `slow == 0`
  sans condition — vrai sur a64, faux sur x64, exactement l'écart que le
  constat 2 de § 10 nomme. Fermé le 2026-08-28 : les quatre affirmations de
  résidence portent le prédicat `a64Production` que le test avait déjà pour
  ses jambes bitfield/shift ; l'égalité lockstep reste exigée des deux
  backends. Pendant ces quatre jours le recensement g++ de § 0·B imprimait
  « 0 warnings » depuis un log inexistant — corrigé en « NO CENSUS » loud.
  La phrase « le reste de la suite est vert » d'ici était donc fausse d'une
  jambe : elle ne couvrait que l'hôte AArch64 qui l'écrivait.

**Dernière passe registre complet : 228/228 sur AArch64, 2026-08-27, dont
224 réellement exécutés** (en-tête de ce fichier). Cette phrase ne vaut que
deux choses : la fraîcheur des binaires derrière elle —
`tools/check_binaries_fresh.py` avant de citer un palier, et son
`--self-test` la première fois sur une machine neuve — et le recensement
derrière elle, `tools/gate_execution_census.py`, parce qu'un gate qui
soft-skippe sort 0 et est compté vert par `ctest`. Et depuis le 2026-08-28,
une troisième : **elle ne parle que de l'hôte qui l'a produite** — l'autre
jambe se lit dans la CI, pas dans ce fichier.

- [ ] **TWO MORE REDS, x86-64 host only, found by the FIRST full-registry
  runs on the host carrying the assets (2026-08-30, twice, plus a serial
  re-run — deterministic, not load).** `macii_persist_etalon` (KeyMap shows
  Cmd-N live, no folder ever appears; boots `hdv/System 7.5.5 HD.dsk`,
  which `tools/check_volume_state.py` marks DRIFTED from `assets.lock`)
  and `q605_afp_live_etalon` (login dialog reached, `AFP sessions=0`
  through every phase; its boot volume `hdv/MacOS-8.1-boot.vhd` is DIRTY
  **and** DRIFTED). Both land on this host's documented fixture state
  (8 dirty volumes / 4 drifted references) — read the volumes before the
  code, per the method rule below. Next, in order: restore the two
  reference images to their `assets.lock` identities (or re-record the
  identities deliberately), then re-run the pair; only a failure on a
  clean, matching image earns a code hunt. Runs recorded in `STATUS.md`.
- [ ] **`iivx_persist_etalon` sits ON its 1800 s bound on this host**:
  1795.94 s green in the first full run, `Timeout` at 1800.05 s in the
  second — the slowest gate of the registry here (next: `lc520_persist`
  1523 s, `sonora_persist` 1491 s). Not a wedge; a margin of 4 seconds.
  Raise its TIMEOUT alongside the other persists, or measure why the IIvx
  persist costs 1.2× the LC 520's on this host.

### Open here, and not red

- [ ] **38 gates prefer a 1.37 GiB unversioned image over the 250 MiB
  versioned one — and it is not cleanly unmounted.** The RAM sweep of
  2026-08-27 found it: `lcii_boot_etalon` peaks at 1.43 GiB, and
  `malloc_history` attributes 1 468 088 320 bytes of it to a single
  `ScsiDisk::open()` — the size of `hdv/boot.vhd`, whose `drVolAtrb` bit 8 is
  CLEAR. The gates list `hdv/boot.vhd` among their candidates ahead of
  `hdv/GISTPERSO-boot.vhd`, so on a host that has both, the mutable dirty one
  wins and the fixture-role work of 2026-08-24 never gets a chance: the
  preference for `hdv/ref/` applies per NAME, and this name is not in `ref/`.
  Next, in this order: (a) decide per gate whether its signature is calibrated
  on `boot.vhd`'s "MacPack" volume or merely tolerant of it — the boot floors
  and SCSI counts are image-specific, which is why this was not simply
  reordered; (b) for the tolerant ones, put the versioned reference first;
  (c) for the rest, clean a copy of `boot.vhd` into `hdv/ref/` and version it
  in `assets.lock`. The prize is not tidiness: it is 5.5x less RAM per gate
  and a fixture that cannot drift under the suite.
- [ ] **Deux assets manquent sur cet hôte, et le recensement les nomme** :
  `cd/MacOS_86.iso` (trois gates CD du Q605) et le module Python `machfs`
  dans `.venv-tools` (`dir2hfs_selftest`). Ni l'un ni l'autre n'est un défaut
  du code ; ils sont écrits ici pour que « 228 verts » ne se relise pas comme
  « 228 comportements prouvés ».
- [ ] **`gui_smoke_test` flaked once, and only once** (2026-08-27): it failed
  inside a 228-gate `-j16` run with `État NON sauvé: rename impossible` on
  `gui_smoke_report.txt.pomss`, having rendered its frames normally, and the
  whole run before it was 228/228 green. **Not reproduced in 50 further runs**
  — 25 idle, 25 against a loaded machine. `std::rename` replaces an existing
  destination on POSIX, so the interesting value was `errno`, which the
  message threw away; `SaveStateSlot.h` now prints `strerror(errno)` with both
  refusals. Next occurrence is therefore evidence instead of a shrug. Do NOT
  add a retry before that value has been read once: a retry would hide the
  only observation left.
- [ ] **`tests/finder_boot_matrix.cpp` still carries the calibration trap**:
  its Mac II leg asserts `scsi().commands > 500`
  (`tests/finder_boot_matrix.cpp:207`) over whatever image the sweep passes
  it. Not a registered CTest, so nothing is red today, and it was left alone
  rather than fixed blind — the sweep needs every OS image to re-calibrate.
  Next: run the sweep, then give it `FinderSignature.h` too.
- [ ] **"Beeps sound wrong / differ per letter"** (field report): the beep
  itself was the Slow Keys rejection beep — expected, and it stopped when the
  8.1 image was cleaned (2026-08-02). What remains is the unrelated half:
  whether ASC renders audible output *correctly* is still untested
  (`q605_asc_test` covers registers and IRQ, not sound). Low-priority ASC item.
- **Not a defect, written here so it is not re-investigated**: a hot-inserted
  disc mounts nothing on a guest with no CD stack — nothing binds a driver to
  that SCSI id at boot. The reporter's System 7.5 volume has `Apple CD-ROM` and
  `ISO 9660` but no `Foreign File Access`, and Mac OS mounts no disc without
  the dispatcher; Mac OS 8.1 has it, and `q605_cdrom_etalon` gates that path.
  Same for `ER`/512 discs whose driver partition is a CD driver
  (`The_Yukon_Trail.cdr`). `CHANGELOG.md` 2026-08-15 (fifth).

### Four method rules those hunts paid for, in full

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

**A fifth rule, added 2026-08-28 by § 10** — *a configuration nobody executes
is not supported, it is only compiled.* The Windows release job builds with
`-DPOM68K_TESTS=OFF` (`.github/workflows/release.yml:289`) and is the one
artifact the suite never touches; that is where the red above was able to live.

---

## 2. Test & validation depth — the single biggest gap

The gates prove **boot**, not **use**, and the machine fan-out made the ratio
worse. Of the **37 profiles**, only **15** have any gate past the Finder
signature. Enumerate them with
`ctest -N | grep -E 'input_etalon|soak|persist|mouse_etalon|key_etalon|savestate_etalon'`
rather than trusting a sentence in a document. **The other ~22 profiles are
boot-to-Finder signature only.** A machine can pass its etalon and still be
useless for real work.

**Depth is a second axis, and it is now covered on all twelve platforms.**
Fourteen soaks and thirteen persists are registered, green **and executing**
as of 2026-08-27 — the second half of that claim is what
`tools/gate_execution_census.py` added, and re-checking the roster means
running that census, not re-reading this line. Until then the Mac II pair
SKIPped on a ROM filename no archive uses, exited 0 and counted green.

**Le critère beyond-boot est arrêté et centralisé** — ne pas le réinventer par
gate. `tests/FolderProbe.h` (gate `folderprobe_test`, 19 checks) : **le signal
n'est pas le plus gros compte, c'est le compte qui change.** Et **le geste
appartient à une APPLICATION, pas à une machine** : `persist()` ramène le
Finder au premier plan et demande à l'invité qui est là (`$910 CurApName`)
avant de taper Cmd-N. Tout gate qui se met à échouer sur « NO candidate folder
name appeared » doit lier ces deux crochets avant qu'on suspecte autre chose.

Highest-ROI closers, in order:

- [ ] **A beyond-boot leg that RUNS AN APPLICATION under load** — SimCity 2000,
  named 2026-08-27. It belongs to both sections and the plan lives in § 3:
  the point is not one more machine proved, it is the only workload that
  would show what the JIT costs and misses under sustained real use.
- [ ] **Next beyond-boot machines**: the Q605 `floppy` leg is unblocked since
  the hot-insert report closed as a modal alert (2026-08-27), so it is
  writable now. **The AIO family landed 2026-08-28**
  (`lc520_soak/persist_etalon`, `CHANGELOG.md` 2026-08-28 (sixth) — the
  French-layout ADB trap and the auto-opened-windows trap are in the gate's
  comments); next open target after the floppy leg: `duo230_input_etalon`.
- [ ] **Floppy: a guest-INITIATED write.** The 2026-07-29 "volume mounts,
  window auto-opens, Cmd-N dropped" evidence is RETRACTED (2026-08-05): it was
  the System 7.5 INIT DIALOG end to end — the modal dialog ate Cmd-N and the
  gesture's Return pressed its default [Eject]. `lcii_floppy_etalon` now
  detects the dialog instead of calling it a mount. The write gesture becomes
  trivial the day the volume actually mounts. Device-side
  write→eject→flush stays gated by `floppy_persist_test`.
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
- [ ] **`duo230_input_etalon`.** The trackball's only coverage today is inside
  the persist leg (§ 7).

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

**État courant.** Le moteur atteint **les douze enveloppes CPU de l'arbre**.
`jit/auto` est le défaut conformant **sur 68040 depuis le 2026-08-10 et sur
68030 depuis le 2026-08-18**. Un 030 résout vers le générateur NATIF sur
**AArch64 seulement** : la promotion x86-64 du 2026-08-21 (score 0, −12,6 %
face à `threaded`) est **RETIRÉE le 2026-08-29** — le premier `ctest -L m030`
lancé sur cet hôte a figé **tous** les gates 030 qui atteignent le
générateur (23 `Timeout`, 20 encore en vol après 11 h), pendant que les six
`interp_*`, les deux gates Mac LC (68020) et les tests unitaires passaient —
et l'A/B sur `d4a18b6` prouve que le blocage **précède** le patch 31 : c'est
une régression produit des huit jours écoulés depuis le flip. `caps().autoFamilies` x64 = `kGuest68040`, `guestFamilies`
inchangé. **La promotion a64 du 2026-08-20 n'a jamais tourné sous le patch
31** : à revérifier sur l'hôte AArch64 avant de s'y fier. 68000 et 68020
restent à l'interpréteur, qui demeure l'oracle d'exactitude, avec un
`interp_*_boot_etalon` par plateforme 040 et 030.
Meilleurs chiffres mesurés — budget fixe, empreinte identique sur chaque moteur
(`POM68K_JIT.md` § 3.4, Q605, 3 000 frames) : interpréteur 48,51 s,
`threaded` 28,10 s (×1,73), `x86-64` **9,71 s (×5,00)**. Le ×2,68 cité ailleurs
est l'horloge murale de `q605_boot_etalon`, un instrument différent et flatté.

**Chantier utilisateur du 2026-08-27 — mesurer le JIT sous une VRAIE charge
applicative.** Tous les chiffres JIT du projet viennent de bancs à budget de
cycles fixe, d'un boot d'étalon et du recensement Rogue. Aucun ne ressemble à
ce qu'un utilisateur fait de la machine. **SimCity 2000 est la cible nommée** :
simulation entière soutenue, QuickDraw dense, pagination disque, des minutes au
lieu de secondes.

**Première mesure faite le 2026-08-27** — `tests/lcii_simcity_census.cpp`
(outil de dev, `EXCLUDE_FROM_ALL`, aucun seuil) : LC II, GISTPERSO, navigation
clavier en trois sauts, puis deux minutes de jeu. **55,4 M instructions,
98,2 % natives**, profil `move` 38,5 %, `alu` 29,1 %, `branch` 22,6 %.

| repli (80,5 M au total) | part | famille |
|---|---|---|
| `D1F0` | **53,2 %** | `ADDA.L` en EA indexée |
| `E9D0` + `EFD1` | 14,8 % | champs de bits 68020 |
| `81FC`, `8DFC`, `8FFC`, `4C40` | 11,5 % | divisions mot et long |
| `6000`, `4EB0` | 6,8 % | branchements |
| `24D8`, `28C0`, `2499` | 5,1 % | `(An)+` → `(An)+`, replis d'accès runtime |

**Le contraste avec le repos est le résultat, pas une décoration** : au Finder
inactif, `4EB0` (JSR) fait **95,1 %** des replis à lui seul. Un générateur
optimisé sur le Finder au repos travaille sur le mauvais opcode.

**Trois pièges payés en montant l'instrument**, écrits pour ne pas l'être deux
fois : l'Egret **tient le CPU au démarrage** (`while (mem.cpuHeld())
mem.tick(1000);`) ; `jit::defaultResolvedConfig()` **ignore `POM68K_JIT_HISTO`**
depuis que la configuration JIT est injectée, donc le recensement tournait
parfaitement sans rien imprimer ; et le type-select du Finder a une fenêtre
d'une seconde, donc taper une lettre toutes les 30 frames sélectionne trois
fois le premier item au lieu d'accumuler le préfixe.

Open, in ROI order:

- **Le rouge x64 030 est CLOS le 2026-08-29** : page pré-MMU, pipe
  post-PMOVE et boucle de replay du store-guard sont mécanisés et corrigés ;
  les trois locksteps épinglés passent. `auto` reste volontairement retiré
  sur x64/030 et le mode 2 plafonné à 1 jusqu'à son ABBA de re-promotion.
  `CHANGELOG.md` 2026-08-29 (later/evening/night/late night).
- **`D1F0` — CLOS le 2026-08-28**, extraction d'abord puis opt-in sur les
  deux backends, replis de jeu 46,5 % → 0, lockstep/`m030` verts — et
  l'ABBA murale a rendu **+0,02 % sur un plancher de 0,6 % : aucun gain de
  temps, dans aucun sens**. `CHANGELOG.md` 2026-08-28 (fourth) et (seventh).

  > **Leçon qui restructure cette liste : l'histogramme de replis n'est pas
  > un profil temporel.** Éliminer la plus grosse famille de replis de la
  > charge applicative n'a pas bougé l'horloge murale — le repli-fenêtre
  > est trop bon marché pour que sa part prédise du temps. Les items
  > ci-dessous restent du travail de COUVERTURE/parité légitime (les lignes
  > d'exception du gate de parité) ; **le prochain levier mural exige un
  > profil temporel** — l'hypothèse des 64 tranches AppleTalk de § 0·A est
  > le premier candidat nommé.
- **Champs de bits (`E9D0`/`EFD1`) — CLOS le 2026-08-30** : lectures et
  écritures TAILLESS ainsi que les huit actions registre, offset/largeur
  statiques ou dynamiques, sont natives sur A64 et x64 ; seules les queues
  mémoire cinq-octets gardent leur frontière transactionnelle documentée.
  `CHANGELOG.md` 2026-08-28 (eighth) et 2026-08-30 (second à fifth).
- **Promotion applicative de la division SimCity — CLOSE le 2026-08-30** :
  le même exécutable, admission OFF/ON injectée, mesure **−1,453 %** en ABBA
  avec empreintes CPU/écran identiques, 2,55× le plancher nul. Les deux
  comparaisons inter-binaires qui donnaient +2,883 % et +3,691 % étaient du
  bruit de layout et sont rétractées. `CHANGELOG.md` 2026-08-30 (ninth).
- **Division — CLOS le 2026-08-30** : `DIVU.W`/`DIVS.W` et les quatre actions
  `DIVL` sont natives sur A64 et x64 pour `Dn`, immédiat et sources mémoire
  ordinaires. La RAM est lue spéculativement après prévalidation ; les effets
  d'EA et de D-cache 040 attendent les gardes, tandis que MMIO/repli lit une
  seule fois. Zéro, overflow, `Dh==Dl` et `INT_MIN/-1` restent à Moira sans
  mutation invitée. `CHANGELOG.md` 2026-08-30 (sixth à eighth).
- [ ] **Finir le tier `-L etalon` sous la bascule `accessClockBias` sur
  x86-64.** La validation du 2026-08-22 a été coupée par un arrêt de l'hôte à
  47/106 gates parallèles, zéro échec (`CHANGELOG.md` a l'état exact). Sur
  AArch64 le tier `-L m030` est passé le même après-midi. C'est le seul reste
  d'un chantier par ailleurs clos : restart-base −4,3 %, BSR.W −2,3 %, la paire
  **−8,0 %** et super-additive à 6 000 frames sur la LC II, empreinte identique.
- [ ] **L'ABBA sur hôte silencieux après la correction de garde de slices
  a64.** La paire provisoire lit −22,9 % (30k) / −23,8 % (6k), toutes deux
  `HOST BUSY` sous les démons d'indexation macOS. Rejouer Spotlight en pause.
- [ ] **Les prochains leviers au-delà des opcodes nommés** : les re-preuves
  `PFLUSHA` (37 694 bumps de génération par `SwapMMUMode`,
  dont un chemin par page ne prendrait que 25 %), et `arm backoff` 4,3 %.
  **Mesuré 2026-08-23** (`POM68K_JIT_ARM_BACKOFF`, 30 000 frames) : 32 → 94,2 s,
  8 → 90,5 s, 4 → 92,9 s, 1 → 93,1 s ; la part native monte
  monotonement (85,6 → 88,2 %) mais pas l'horloge murale — chaque sonde coûte.
  ≤ 4 %, pas une promesse. Un backoff croissant par séries (1 → 32) a
  **fait diverger les deux locksteps 68040** (`D1` à la première frontière,
  modèle D-cache actif) pendant que les gates 030 restaient identiques —
  annulé ; **QUAND la fenêtre est armée est visible par l'invité sur le 040**,
  et toute politique de retry a besoin du gate de cette famille d'abord.
  **Le profil est maintenant plat** (Finder au repos) : code généré ~20 %,
  fenêtre ~13 %, moteur ~7 %, et le LLE périphérique ~27 % — le 68HC05 du Cuda
  à lui seul ~13 %. Le prochain levier bout-en-bout est **hors du JIT**.
- [ ] **Retirer conformément le hint CACR SMC sur VASP, RBV et MSC.** Le cache
  est lâché 28 816 fois par run, dont 26 544 par ce hint. Le supprimer vaut
  **−21,8 %** d'horloge murale (`POM68K_JIT_030_CACR_FLUSH=0`, instrument non
  sûr), et le gain est du temps de compilation, pas de la résidence. Pour le
  retirer proprement il faut prouver que la garde voit chaque écriture en RAM
  sur chaque carte 68030 : **fait pour le V8** (SCSI en pseudo-DMA piloté CPU,
  IWM polled, stores générés traversant le `codeMask` du DTLB), **pas fait**
  pour VASP, RBV, MSC.
- [ ] **Le dernier écart 030 de a64 non porté sur x64** : le contrôle de coût
  qui refuse les instructions dont les cycles tracés portent une pénalité
  i-cache (`Instr::baseCycles`, qu'a64 consomme pour les formes étroites), et
  l'ordre pré-accès de `(An)+`. C'est ce qui plaçait a64 à 3 % de `threaded` sur
  un 68030 quand x64 était à 44 %. **Voir § 10 vague 2** : cet item est le
  symptôme, la cause est que les deux backends ne partagent rien au-dessus de
  l'IR.
- **Décalages logiques Speedometer — CLOS le 2026-08-30** : le nouveau
  `lcii_speedometer_census` isole `Performance Rating / CPU` en 270 frames,
  avec fin structurellement détectée. `E8A8`, `E4AC`, `E2AD` portaient tous
  un compteur tracé de 16, seule raison de leur refus. La spécialisation
  gardée `Dn&63` couvre maintenant 16 pour les décalages logiques sur A64 et
  x64, huit pour AS/RO ; le lockstep compteur-16 passe sans repli. Sur la
  fenêtre historique de 600 frames, 330 241 → 317 894 replis unsupported,
  empreintes inchangées. Le couple mural −1,99 % reste un signal non promu,
  faute de répétitions au-dessus du plancher. `CHANGELOG.md` (tenth).
- **Multiplications mot Speedometer — CLOS le 2026-08-31** : l'audit a
  corrigé le diagnostic précédent : `cyclesMul()` est variable seulement sur
  68000/010 ; le 68020/030 a une table fixe par EA. L'IR et le coût partagés,
  puis les lowerings A64/x64, couvrent `MULU.W`/`MULS.W`, avec X préservé et
  lecture MMIO exactement unique. Le recensement exact passe de 113 652 à
  96 956 replis (**−14,69 %**) ; les trois opcodes chauds disparaissent, les
  empreintes restent exactes. L'ABBA même binaire donne un signal médian de
  −0,35 %, sous l'étendue contrôle de 2,18 % : couverture promue, pas de gain
  de vitesse revendiqué. `CHANGELOG.md` (2026-08-31).
- **Diagnostic `24D0` + `JSR abs.l` — CLOS le 2026-08-31** : les 25 792
  gardes `24D0` observées lisent toutes `$50F06060`, le pseudo-DMA SCSI du V8.
  Son BERR terminal peut suivre un long partiellement consommé : thunk puis
  replay doublerait des octets FIFO, donc Moira reste volontairement
  propriétaire. `4EB9`, lui, est le JSR absolu long mono-chemin prouvé
  (`words=fetchWords=3`, base 4) ; A64/x64 l'admettent maintenant, avec garde
  d'alias pile/cible et lecture live du premier mot cible. Il disparaît du
  census, unsupported 55 174 → 49 476 dans l'échantillon, empreintes exactes.
  `CHANGELOG.md` (2026-08-31 second).
- **JSR indexé indirect Speedometer — CLOS le 2026-08-31** : les 13 948
  replis `4EB0` étaient tous des EA complets préindexés (`I/IS=001`). A64 et
  x64 prévalident maintenant lecture du pointeur + écriture de pile, ne lisent
  le pointeur qu'en RAM prouvée, gardent cible impaire/alias pile, lisent le
  mot programme live puis poussent par le pointeur prouvé. MMIO, trou et faute
  rejouent l'instruction intacte ; le gate compare aussi la frame 030 de 32
  octets. Le census réel passe 49 476 → 37 290 unsupported et 89 908 → 79 380
  replis totaux ; `4EB0` disparaît du statique, avec 87 seuls fills/tags MMU
  dynamiques observés. `CHANGELOG.md` (2026-08-31 third).
- **Lectures périphériques Speedometer `0829` / `1029` / `1429` — CLOS le
  2026-08-31** : leurs coûts de base variables (59–129 cycles) venaient bien
  des lectures `$50F01000/$1200/$1400/$1600/$1A00`, pas d'une table JIT
  fausse. L'IR les marque `exactRequired` sur 030 ; A64/x64 délèguent la
  lecture vivante au thunk et ne facturent que le coût fixe. Un oracle injecte
  23 cycles par lecture MMIO et reste exact avec zéro repli. Le census réel
  passe 37 290 → 27 751 unsupported et 79 380 → 69 874 replis totaux ; les
  trois lignes disparaissent, empreintes inchangées. `CHANGELOG.md`
  (2026-08-31 fourth).
- **Arithmétique étendue Speedometer — CLOS le 2026-08-31** : `D981` et
  `9381` étaient des `ADDX.L`/`SUBX.L` registre d'un mot, coût fixe 2 ; seul
  l'IR `Unknown` les refusait. Le nouveau contrat partagé couvre B/W/L,
  consomme X, produit C=X et garde `Z'=Z&&result==0`; x64 emploie ADC/SBB et
  A64 un intermédiaire 64 bits. L'oracle exécute 4 915 formes natives sans
  repli, y compris packed CCR et le bord source+X=2^32. Le census réel passe
  27 751 → 20 065 unsupported et 69 874 → 62 188 replis totaux ; toutes les
  formes registre rencontrées disparaissent, empreintes inchangées. Les
  formes mémoire prédecrémentées restent un contrat de faute séparé.
  `CHANGELOG.md` (2026-08-31 fifth).
- **MOVE full-indirects Speedometer — CLOS le 2026-08-31** : `2470` / `2070`
  sont deux lectures séquentielles — pointeur long puis opérande long — avant
  l'unique commit An. A64 et x64 consomment maintenant le contrat
  `PreflightAll` sur le 030 sans thunk/cache intermédiaire ; un pointeur ou
  opérande non plain rejoue l'instruction intacte. L'oracle exact `81E1`
  passe 256 checkpoints sans repli sur A64 et x64, puis remplace seulement la
  valeur du pointeur par du MMIO retardé et retrouve exactement les deux
  demi-lectures de Moira. Le census 270 frames fait aussi disparaître les
  formes sœurs `2270`/`2272`/`2230`/`2032`/`2075` : 9 408 → 5 286 unsupported
  (**−43,82 %**), 52 303 → 48 982 replis totaux (**−6,35 %**), empreintes,
  SCSI et fin inchangés. `CHANGELOG.md` (2026-08-31 ninth).
- **Destinations MOVE indexées Speedometer — CLOS le 2026-08-31** : le traceur
  corrige l'étiquette initiale : `2191` / `31A9` sont des destinations
  **brèves** `1000`, pas full-format, à coût 5. Le chemin deux-mémoires
  prévalide déjà source et destination avant la lecture ; A64/x64 ouvrent
  donc cette cellule seulement pour la paire mémoire `PreflightAll` sur 030.
  L'oracle exact reste natif 256 checkpoints, puis déplace la destination sur
  MMIO et retrouve callbacks/état/horloge de Moira. Les formes sœurs
  `319F`/`31B2`/`2D9F` disparaissent aussi : 5 286 → 3 910 unsupported
  (**−26,03 %**) et 48 982 → 47 768 replis totaux (**−2,48 %**), toutes les
  empreintes restant exactes. `CHANGELOG.md` (2026-08-31 tenth).
- [ ] **Queue de couverture Speedometer après les MOVE indexés** : `C029`
  reste premier mais son admission exact-thunk a échoué le vrai oracle
  (270 → 450 frames malgré le synthétique vert), donc ne pas la réintroduire
  sans preuve de phase périphérique. Les prochains candidats structurels
  sont `4C00`, puis ROX/décalages hors tranche, MOVEM indexé complet et
  ADDX/SUBX mémoire. `6000`, `4EF9`, `E9D4`, les LEA `41F6`/`43F0`, les MOVE
  source full-indirects et les destinations brèves prouvées sont clos ;
  traiter la suite par contrat, pas par largeur d'opcode.
- [ ] **Compact `mmu040InstrStart`.** Huit remises à zéro de champs par
  instruction + un `getCCR()` packé ; des champs adjacents pourraient
  s'effondrer en un ou deux stores larges. Petit, mais sur *chaque* instruction
  040.
- [ ] **Densité du code généré** — dépriorisé 2026-07-30. Le chiffre périmé de
  150 o/instr précède la différée de frontière et l'émission à froid ; la
  re-référence montre x64 déjà devant `threaded` sur les deux régimes (−10 %).
  L'écart résiduel au Finder au repos est dominé par le contrat d'exactitude
  d'éviction ATC, que la densité ne touche pas.

**Deux rétractations à ne pas re-dériver** :

> **« Le verrou est la résidence native globale » — RETIRÉ 2026-08-18.**
> La résidence est un *symptôme*. Balayer la barre avec `POM68K_JIT_MIN_NATIVE`
> montre que la forcer vers le haut rend le moteur **monotonement plus lent** :
> à 0 %, la résidence est 2,2× plus haute (31,3 %) et l'horloge murale **37 %
> pire** (29,99 s), parce qu'un repli *dans* un bloc paie un appel, une frame et
> un commit de frontière là où la fenêtre paie un dispatch simple. Tout ce qui
> est ≥ 50 est un plateau plat. Ce qui monte légitimement la résidence, c'est
> **émettre plus d'instructions par bloc**, pas admettre de plus mauvais blocs.

> **« La divergence 030 est localisée dans le chemin d'accès mémoire » —
> RÉFUTÉ 2026-08-10.** Avec `POM68K_JIT_ACCESS_THUNK=0`, qui rend chaque
> instruction touchant la mémoire à l'interpréteur, x64 sur la LC II diverge au
> **même pas exactement (5956)** qu'avec le thunk actif. Ce qui diffère dans les
> deux cas est la **comptabilité de l'i-cache 68030**.

### Build recipe — Raspberry Pi (`docs/RASPBERRY_PI.md`)

`POM68K_NATIVE` / `POM68K_TUNE` / `POM68K_LTO` sont trois boutons CMake
indépendants depuis le 2026-08-08 ; l'AppImage aarch64 reçoit LTO +
`-mtune=cortex-a72` ; `packaging/raspberry/build_native_pi.sh` construit
`-mcpu=<cœur exact>` + PGO sur la carte. Restent ouverts :

- [ ] **Mesurer POM68K sur un vrai Pi, avant et après.** Chaque chiffre ARM de
  `docs/RASPBERRY_PI.md` est celui de NeoST, emprunté au même interpréteur
  Moira sur Cortex-A72 (−20 % PGO, −34 % PGO+LTO, ~10-20 % `-mcpu`). Le chiffre
  PGO propre à POM68K est x86-64 seulement. Utiliser `jit_bench`
  (`POM68K_BENCH_FRAMES`), jamais un boot etalon : un etalon s'arrête quand il
  reconnaît le Finder, donc deux builds y sont chronométrés sur des quantités
  de travail invité différentes. Les empreintes imprimées doivent coïncider.
- [ ] **Dispatcher `pi400.yml` avec `cortex-a76` une fois.** Un Pi 5 est
  armv8.2-a — atomiques LSE, fp16, dotprod — le seul cas où le plancher relevé
  pourrait changer la génération de code. **Le cas a72 est réglé et réfuté**
  (2026-08-08, run 31264225875) : `-mcpu=cortex-a72` produit un binaire
  identique octet pour octet au `-mtune=cortex-a72` de release (27 octets de
  différence sur 8 698 128 — build-id + chaîne de version). Personne ne doit
  re-dériver ce résultat.
- [ ] **LTO pour le `.dmg` macOS et le `.zip` Windows.** Deux moitiés
  différentes : **macOS** est à une ligne de la fin — `package_macos_release.sh`
  et `macos.yml` passent `-DPOM68K_LTO=OFF` avec la raison écrite à côté ;
  supprimer ce drapeau et exercer `lipo`. **Windows** n'a rien à basculer : tout
  le bloc d'optimisation est gardé `if(NOT MSVC AND NOT EMSCRIPTEN)`, donc
  `POM68K_LTO` y est **inerte** à n'importe quel défaut ; le travail est
  d'émettre `/GL` + `/LTCG`, pas de changer un bouton. *(Voir § 10 : c'est le
  même angle mort qui a laissé passer le générateur x64 sous MSVC.)*

### Measured and DROPPED — do not re-open without new data

Ces mesures ont coûté du temps réel. **Les nombres, pas les conclusions, sont
la valeur ici.** (Les rejets du 2026-08-11 sont dans `CHANGELOG.md` à cette
date, versés depuis ce fichier le 2026-08-28.)

- **Lazy condition codes en x64 : plafond ≈0,8 %.** Méthode : dupliquer
  l'émission des flags (stocker deux fois le même octet est un no-op
  sémantique, donc le delta est le coût marginal d'un jeu complet de
  matérialisation). Boot Q605 sur le backend x64 : 26,13/26,15 s → 26,37/26,30 s.
  **Piège de mesure** : le premier chiffre publié (2,5 %) utilisait
  `POM68K_CPU_ENGINE=jit` seul, qui sélectionne **threaded** — le code x64
  modifié n'a jamais tourné. Toute mesure x64 doit poser `POM68K_JIT_BACKEND=x64`.
- **Tables de dispatch par page pour les cartes mémoire : la prémisse était
  fausse.** Accès par destination sur un boot LC II (1 475 M) : RAM 69,6 %,
  ROM 29,3 %, I/O 0,33 %, autre 0,80 %. 99 % tombent sur RAM/ROM, déjà 2-4
  comparaisons parfaitement prédites ; une table de 4 Kio mettrait un **load
  dépendant** devant le cas à 99 % pour économiser des branchements qui ne
  coûtent rien. Ne rouvrir qu'avec un profil montrant le décodage comme part
  réelle.
- **Recherche ATC en O(1) pour `mmu040Translate` : plat.** callgrind le donnait
  à 38,6 % de l'interpréteur, d'où une table indicative directe de 256 entrées
  devant le balayage à 32 entrées (bit-identique par construction).
  Interpréteur 213,2 contre 213,2 s (5 G), 906,7 contre 903,9 s (20 G), x64
  98,9 contre 97,6 s. **Les 38,6 % sont réels mais c'est la MARCHE et la
  comptabilité par accès, pas la recherche** — ne rouvrir qu'avec un profil qui
  les sépare.
- **La fenêtre de données de l'interpréteur (J1c) est opt-in, pas du travail
  ouvert.** Elle existe (`POM68K_DATA_WINDOW=1`, `POM68K_JIT.md` § 8) ; la
  plafonner à la couverture ATC pour l'exactitude bit-à-bit en a fait une perte
  nette sur l'interpréteur.

**Invariant à re-énoncer : l'état dérivé meurt avec l'entrée ATC dont il vient**
(`pomJitAtcEvict`). Un hit de fenêtre doit impliquer que l'interpréteur aurait
aussi eu un hit ATC, sinon les deux moteurs parcourent des sous-ensembles
différents des tables de pages — et une marche écrit le bit U du descripteur,
que la VM de Mac OS lit pour le vieillissement des pages. Cette classe de
divergence imite une corruption mémoire ; voir `CHANGELOG.md` 2026-07-28.

---

## 4. LLE fidelity — replace HLE shortcuts

Inventory and migration plan: `docs/LLE_VS_HLE.md`, which **carries no live
BUG any more** (2026-08-02) and, since **2026-08-14, no unconditional HLE
either** — the Eclipse towers were the last board on the command-level `Egret`
and now run the factory `341s0851` on a real 68HC05 like every other
Egret/Cuda machine. Everything remaining in its § 1 is a *simplification*, each
with its reason and its reopening condition; every remaining HLE entry is a
*fallback* — a missing dump or an explicit `*_LLE=0`, and never silent.
Policy settled 2026-07-29 (§ 2): HLE fallbacks are **kept but LOUD** (stderr
NON-CONFORMANT notice at every HLE ADB entry) because MCU dumps are
non-distributable; deletion would be a deliberate "POM68K requires MCU dumps"
product decision, not a cleanup.

- [ ] **Quadra 605 / LC 475**: expand Cuda commands only from ROM/driver
  traces. The on-chip FPU/FPSP and M0-M3 cache work are closed: opt-in
  I/D contents, copyback, snooping and bus-transaction timing. What remains
  is pin-level 040 timing, not an architectural cache gap
  (`docs/CACHE_040.md`). *Product-performance state, so nobody re-measures it*:
  native physical D-cache-line reads landed lockstep-gated on A64/x64
  (**61,94 → 48,78 s**, −21,2 %); the sole-access copyback write path followed
  (whole write path **49,93 → 49,49 s**, −0,88 %); the `MOVE.L abs.W,-(A7)` /
  `MOVE.L (A7)+,abs.W` pair removed 7 523 969 further fallbacks
  (**46,92 → 46,63 s**, −0,62 %). The repeated full lockstep exercises
  21 303 835 reads + 5 896 026 writes over 131,8 M JIT instructions without
  divergence. **The mode still misses the Finder guest budget and remains far
  behind cacheless 11,03 s**; broadening arbitrary stores, more C++ tag lookup
  and unmeasured RMW work are all closed as measured-and-refused.
- [ ] **SCC LLE, Low tier** (2026-07-22 MAME `z80scc.cpp` audit,
  `docs/LLE_VS_HLE.md` § 1.4). What is left is deliberately deferred, each for
  a stated reason: true bit-serial Tx/Rx sampling (only worth it with a real
  async transport to talk to); chip-variant gating (NMOS 8530 / 85C30 / ESCC —
  FIFO depth 3/8, WR7', status FIFO, needed the day a machine wants the ESCC);
  WR9 VIS/NV options (hardcoded VIS=1, correct for every Mac target); DPLL
  (MAME stubs it too). **Also missing: a consumer** — nothing on the emulated
  side of the wire reads `rtsAsserted()` yet.
  **Caveat that applies to the whole SCC backlog**: MAME's SDLC side is partial
  (Send Abort / CRC resets "not implemented", no EOM latch, no hunt/sync) — for
  LLAP behaviours **we are the more complete model**. Use MAME as oracle for
  the ASYNC side only; do not regress LLAP chasing parity.
- [ ] **A guest-level "Redémarrer" etalon** — boot a System, pick Finder →
  Redémarrer, assert the machine comes back up. `cuda_restart_test` proves the
  seam from the firmware's own action; nothing proves the Toolbox path that
  leads to it.
- [ ] **A first-class flux track *store***. The view still derives from the
  canonical cell ring, so off-rate written flux does not survive a commit; the
  same change would let `encodeTrackGcr` adopt MAME's zone arithmetic. Neither
  is symptom-backed today — `docs/LLE_VS_HLE.md` § 1.3 owns the live state and
  the reopening conditions (committed tracks re-encode, tach is a sampled bit).

### Peripheral event deadlines — huit plateformes sur douze, et pourquoi les quatre autres non

Le contrat, à garder parce que la prochaine plateforme en aura besoin : **seul
un périphérique capable de lever une interruption ou de basculer une ligne
visible de l'extérieur *spontanément* a besoin d'une borne** ; l'état pur
(rotation de `SonyDrive`, angle) est couvert par le `flushTicks()` forcé à
l'accès. Changement mécanique par enveloppe CPU : ajouter `periphDeadline_`,
retour anticipé dans `catchUp`, planifier après `tick` — **et ajouter le
nouveau membre au `visit()` de l'enveloppe** (les gates `savestate_030/040`
attrapent l'oubli). Aucune borne ne peut être plus grande que la prochaine
transition observable.

Pourquoi ça valait le coup, mesuré sur `q605_boot_etalon` : batch 256 →
25,6 M appels `mem.tick()` / 60,1 s ; batch 1 → **833,2 M appels** / 103,3 s,
avec **les mêmes 1,650 G cycles machine livrés dans les deux cas** — le coût
n'est ni les périphériques ni le crochet, c'est d'entrer dans l'éventail de
~15 périphériques **32,5× plus souvent**. Avec les échéances : **86,65 M appels
pour 1,675 G cycles machine, 19,34 cycles/appel**, timing d'événement exact.
**Ne pas grossir le pas périphérique moyen de 19,11 cycles machine : il est
observable.**

- [ ] **Basculer les défauts Mac II et Duo le jour où un gate sensible à la
  gigue existe pour le justifier.** L'extension du 2026-08-13 est **opt-in
  parce qu'elle a été mesurée** : `POM68K_MACII_EVENT=1` / `POM68K_DUO_EVENT=1`,
  un binaire, bouton basculé entre les runs — Mac II 65,28/66,37 s contre
  57,18/56,49 s (**+14,2 %/+17,5 %**, répété), Duo 109,49 s contre 100,50 s
  (**+9,0 %**), chaque run atteignant le Finder avec les trois observables de
  l'étalon Mac II identiques dans les deux sens. L'échéance est strictement
  plus correcte (gigue → 0) et sa correction est invisible à tous nos gates ;
  la mettre par défaut serait refaire l'erreur de l'échéancier ASC (§ 0·A,
  retiré sur une régression de débit malgré sept gates verts).

**Délibérément NON convertis, chacun pour une raison énoncée** : les
**compacts** (cycle-exacts par construction ; une échéance y risquerait toute
la revendication d'exactitude de la machine la moins chère à émuler) et le
**IIfx** — dont la raison est **vérifiée, pas supposée** : `ApplePic::tick`
avance d'une instruction 65C02 par itération sans état d'inactivité, donc
`ApplePic::cyclesToNextEvent()` vaut 1 dès que le PIC n'est pas déjà en dette
(`ApplePic.h:89`) et une échéance s'effondrerait en un flush par cycle. Le IIci
est le délicat de l'ensemble converti — `adbVia_.tick` + `syncTo(machineClock())`
à chaque tick est un transport PIC cadencé par l'hôte, donc dériver sa borne des
décomptes vivants avant de toucher quoi que ce soit.

**Garde-fou permanent :** `Q700Memory` sert trois machines. Après tout
changement là, rejouer `q700` **et** `q900`, jamais en parallèle.

**Deux constats de la passe close qui restent porteurs :**

- **Le `$F18000` de l'Eagle Classic II est le DAC analogique de
  luminosité/contraste du CRT** (le `spice_device::bright_contrast_w` de MAME),
  nommé sur trois preuves indépendantes. Comportement inchangé — notre CRT n'a
  pas d'étage analogique — mais un périphérique nommé et délibérément ignoré
  n'est pas un trou inexpliqué. **Délibérément NON fait** : le test de présence
  de la ROM échoue ici parce que les lectures rendent `$FF` sans écho ; c'est
  probablement juste pour un DAC en écriture seule, mais « probablement » n'est
  pas une mesure, donc aucune valeur de relecture n'a été inventée.
  Également réglé : **la valeur du trou est indifférente à cet invité** —
  `POM68K_V8_HOLEVAL=00` et `$FF` atteignent tous deux le Finder avec 9 619
  commandes SCSI. **Ne PAS s'aligner sur 0 « pour la parité »** : le 0 de MAME
  ici est le DÉFAUT de son `address_space`, pas une décision modélisée.
- **Le taux de report ADB est correct et n'est plus un suspect.** Trace d'un run
  LC III complet (199 s émulées, 17 850 commandes ADB) : intervalle d'autopoll
  agrégé **11,18 ms — p10 = p90 = médiane**, contre les 11,1 ms / 90 Hz
  nominaux de l'Egret. Soit **89,5 Hz contre 90**, 0,6 % de déficit, exact par
  construction. L'amplification ~1,6× d'un flux soutenu est la mise à l'échelle
  souris de System 7 agissant sur des deltas fusionnés. **Quiconque rouvre ceci
  a besoin d'un observable *neuf*.**

---

## 5. Per-machine backlogs

### LC II / V8

- [ ] **1.44 MB guest-level mount/boot etalon.** The SWIM1 controller is done
  (IWM+ISM personalities, 1-0-1-1 switch, param RAM, MFM read/write through the
  cell engines — gate `swim1_test`); what is missing is a guest-level gate.
  Asset on hand: `disks35/Stuffit_Expander_5.5.dsk`.
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
  **Le banc SimCity de § 3 traverse forcément ce chemin** : soit la mesure
  ferme la dette en passant, soit elle la reproduit de façon déterministe — ce
  qu'aucune passe n'a encore fait.
- [ ] **No-FPU SANE.** Solved on the 040 side (gate `q605_barefpu_boot_etalon`
  reaches the Finder under a true `FPUModel::NONE`). **Unverified whether the
  030/LC II path still needs the same UniversalInfo / defaultRSRCs selection**
  (`POM68K_NOFPU` captured by `ProcessEnvironment`, parsed by `RuntimeConfig`
  and consumed by `PlatformV8.cpp`) — re-test before spending effort here; the
  original O6.13 diagnosis may already be obsolete.

### Mac Plus

- [ ] **VIA/RTC accuracy**: model 6522 T1/T2 ±1-cycle reload/IFR latency; VIA
  E-clock access alignment and IACK E-cycles; seed the GUI RTC from the host
  while keeping tests deterministic. *(PRAM file persistence is NOT a gap here
  any more: `MacMemory::loadPram`/`savePram` exist and the six family
  lifecycles in `GuiRunner*.h` wire them on all twelve platforms. What differs
  per platform is only the STORE: discrete `Rtc` on the compacts, Mac II
  family, IIfx and IIci; Egret/Cuda XPRAM on
  V8/Sonora/VASP/Q605/Q630/Centris/Q700/IIsi; PG&E internal RAM + SRAM on the
  Duo.)*
- [ ] **Floppy: external-drive selection.** `Iwm::attachDrive` takes a second
  `SonyDrive*` (`Iwm.h:23`) but every 800K machine passes `nullptr` for it. The
  SWIM2 boards already pass two, so this is a per-machine wiring gap, not a
  device one. *(Write support, GCR write-back and host file persistence are
  DONE — `SonyDrive::setWriteBack`/`flushToFile`, gates `iwm_write_test`,
  `floppy_persist_test`; hot eject/insert is the shared Disques window,
  `src/DiskBays.*`.)*
- [ ] Keypad/arrow `$79`-prefix handling where required by M0110 input.
- [ ] **Sound accuracy**: fetch the sound buffer per scanline instead of once
  per frame; model the disk-PWM byte and the analog volume curve.
- [ ] **SCSI/serial**: multiple targets/LUNs and correct REQUEST SENSE after
  CHECK CONDITION; a host-side serial transport (PTY/TCP) to make the SCC ports
  *usable*, not just correctly timed. *(WR4 clock mode, WR12/13 BRG, WR11
  routing, WR5 Tx gating, parity/framing, Rx CRC are all DONE — gates
  `scc_baud_test`, `scc_engine_test`.)*
- [ ] Pixel-accurate etalons and a WASM build: a screenshot regression runner for
  the Plus boot/Finder paths, keeping asset-dependent tests soft-skippable.

### CD-ROM

Base support DONE 2026-07-29; gates `scsi_cdrom_test`, `q605_cdrom_etalon`,
`q605_cdboot_etalon`. **Layout matters**: the ROM scans 6→0, so the boot volume
goes to ID 6 and the CD to 3, or a bootable disc wins the scan. **8.5/8.6 are
PowerPC-only** (8.1 is the last 68k release) — a black screen on them is
correct behaviour.

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

### AppleTalk in-process stack — polish backlog

`AtalkStack` (DDP/RTMP/ZIP/NBP/AEP/ATP), `AfpServer` (ASP + AFP 2.1 +
`.AppleDouble`), `PapServer` (PAP → CUPS/`.ps`), `MacIpGateway` (DDP-22 +
user-mode NAT), tied by `AtalkHub` with a **Réseau → AppleTalk** window; GUI
default (`POM68K_APPLETALK=0` disables it). Gates `atalk_stack_test`,
`afp_server_test`, `pap_server_test`, `macip_gw_test`. Reference:
`docs/APPLETALK.md` § 6.5. **Performance caveat: this stack is where the GUI
speed gap lives** — § 0·A, third open item.

- [ ] **The lapENQ address-defence ACK can lose the prober's race** (the
  comment at `src/AtalkStack.cpp:94-98` now states this plainly — it used
  to promise an express path that never existed). The only production binding
  of `stack_.sendFrame` appends to `pending_` and the flush calls
  `injectRxFrame(0, d, n, /*express=*/false)` — one tick later, then further
  delayed by LLAP's 400 µs inter-dialog gap. `express=true` exists but is used
  only for the CTS synth (`GuiHostServices.h:77-91`). Consequence: a guest
  probing node 128 can time out and take the internal node's address, after
  which `onGuestFrame`'s `src != node_` guard stops recording it and every DDP
  the stack sends to 128 is also the guest's own address. Fix: either a
  `sendControlFrame` hook bound to `injectRxFrame(..., true)`, or correct the
  comment. No gate exercises address defence — `llap_two_system_etalon` comes
  closest and does not.
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

Milestone 1 DONE 2026-07-22 (bidirectional SCC LLAP wire + the `LtoUdp` cable,
Mini vMac / TashRouter format, multicast 239.192.76.84:1954). Gates
`llap_loop_test`, `ltoudp_test`, `llap_two_system_etalon`. TashRouter interop
verified. Note: System 6 only opens `.MPP` lazily from the Chooser — headless
LLAP tests need Sys 7.

- [~] **Full AppleShare session over the real bridge**: infrastructure DONE
  (netatalk **2.4.9** + TashRouter vendored, `tools/netatalk2/build_netatalk2.sh`
  builds hermetically, `tools/netatalk2/appleshare.sh` + `router.py` serve
  `input/` as "Input" in zone "POM68K"). **Remaining: run the bridge with a GUI
  guest and mount the volume from the Chooser.**
- [ ] Interop check against Mini vMac's LToUDP (same multicast group).

### Ethernet over SCSI — DaynaPort SCSI/Link, opt-in since 2026-08-07

`DaynaPort` (the SCSI target) + `EtherLink` (framing + proxy ARP) onto the
NAT already in `MacIpGateway`. `POM68K_DAYNAPORT=<id>` on the Quadra 605 only;
gate `daynaport_test`; design in `DEV.md` § 3.3bis. Every machine here has a
SCSI bus, so this is the one Ethernet path that can reach all of them.

- [ ] **Run a real SCSI/Link driver against it.** This is the only test that
  settles whether the command set is right; everything gated so far is our
  own reading of SLINKCMD.TXT. Needs the DaynaPort driver on a boot image
  and a scripted MacTCP config — the same "scripted control panel" need as the
  Chooser drive above.
- [ ] GUI: a **Réseau** entry to attach/detach the card and pick its SCSI ID,
  instead of an env knob at startup only.
- [ ] Save states: the card appears nowhere in `SaveStateMachines.*`, so a
  restore comes back with an empty Rx ring. Cheap to add, but it changes the
  on-disk format for every existing `.pomss` — do it with the next format bump.
- [ ] **EtherTalk** (AARP + 802.3/SNAP DDP) so AppleTalk can leave the SCC
  too. Today the card carries IPv4 and ARP only, and AFP/PAP still go over
  LocalTalk at 230.4 kbit/s through the most timing-fragile device in the
  tree. **This is the item with the most to gain**, and § 0·A's AppleTalk
  attribution is a second argument for it.
- [ ] Decouple the uplink from `AtalkHub`: with `POM68K_APPLETALK=0` the
  guest sees the card and it carries nothing, because the NAT lives in the
  hub.
- [ ] Carry the card to the other eleven platforms (a member + an accessor
  each — `AtalkHub::attach` already detects it with a `requires` clause).

---

## 7. New machine profiles

Phase A/B/C are done — **37 profiles**, all booting the Finder. Effort tiers and
the full family map: `docs/68K_FAMILY_SCOPE.md`. **The cost of a new platform is
enumerated in `DEV.md` § 2.12**, derived from what the Duo actually touched, with
the three traps that were each paid once (the ROM filename that makes a gate skip
in silence, the declaration ROM without which System 7 draws no Finder, the
`drVolAtrb` bit 8). Rule kept: **each new profile gets at least one Finder cell
before the next.**

Explicitly **out of scope** for now: AV DSP, all 4 MB PPC ROMs.

### Independent majors — the only things left that are not just a ROM dump

- [ ] **Power Manager, Duo line.** The MSC + PG&E platform is finished through
  milestone 3b of `docs/DUO_BRINGUP.md`, and input through the PMU is done
  (matrix keyboard 2026-08-13, trackball 2026-08-14 on the PG&E's own
  quadrature counters `$14`-`$16`, latched at 60 Hz — **the register must NOT
  be drained on read, or half the directions vanish into a double-read race**).
  Remaining, in milestone order:
  - **variants** (210/250 trivial — `MscMemory.h` already carries
    `kIdDuo210`/`kIdDuo250` and they share the `$ECFA989B` ROM, so they need a
    variant tag and a `kMachineProfiles` row; then 270c CSC, 280 040, then
    PB150 as the no-oracle MSC variant);
  - **the actual point — a sleep/wake gate** (`duo230_sleep_etalon`), which no
    other machine can test. First measurement: `PgePmu::setClamshell(false)`
    (port F bit 3, the lid) holds the 68030 within 5 s — the firmware does act
    on the switch — but the System runs no sleep procs first (not one write
    reaches the disk, the volume's dirty bit is untouched) and re-opening the
    lid does not wake the machine. **Both halves are the milestone.**
  - `docs/DUO_BRINGUP.md` § 3b lists what is deliberately NOT wired (floppy,
    drive sounds, live CD-bay swap, right mouse button — machine-side API
    absences, not shell gaps).
  The 140-180 line is a different PMU (Mitsubishi M50753, 6502-class —
  POMIIGS `CPU65816` candidate) — same brick as Portable/PB100.
- [ ] **NuBus + slot video** beyond Mac II Toby: IIx / IIcx / IIci and the NuBus
  Quadras. VASP/IIvx currently reads its three slots as empty; real cards would
  reuse the Mac II NuBus/DeclRom port.
- [ ] **ATA/IDE target** on the Quadra 630 / LC 580. The port is mapped but has
  no drive, so boot goes through SCSI — the remaining gap on that board.
- [ ] **AV DSP (DSP3210)** → 660AV/840AV. Not planned.

*(The Apple PIC IOP + OSS brick is DONE and closed, on the IIfx and on the same
front end carrying the Eclipse towers. **M4 stays deferred and LOUD**: SCSIDMA
true DMA + restartable handshake is A/UX-only per `scsidma.cpp:12`, nothing in
the Mac OS path needs it.)*

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
(`MacOS-7.6-boot.vhd` was deleted as corrupt; `runIIfx` still probes for it
first and falls through to `GISTPERSO-boot.vhd`.)
Full tree also at `../refs/infinite-mac/Images`. Missing files: fetch with
**Scrapling** (not raw `curl` through the sandbox proxy).
Flat HFS → SCSI façade: `ScsiDisk::open` (gate `scsi_hfs_facade_test`; offline
bake `tools/wrap_hfs.py`).

---

## 8. Cross-machine architecture

- [ ] **Save states — one residual.** The feature ships across all **12**
  machine families and **37** profiles. Gates: `savestate_test`,
  `savestate_v8_test`, `savestate_030/040/68k_test` (all `unit`), plus the
  real-OS `lcii_savestate_etalon` and `q605_savestate_etalon`.
  **Remaining: a hands-on real-ROM GUI pass** — click « Sauver l'état » /
  « Restaurer l'état » on a booted machine. `gui_smoke_test` opens a real
  hidden GLFW/ImGui window, renders three frames, switches engine, writes a
  compact-demo state, closes through RAII and reaches the relaunch boundary;
  it deliberately does not claim every family-specific panel is covered.
  The 2026-08-09 Xvfb pass found a bug no gate could see — the 040 loops
  auto-inserting `disks35/Disk605.dsk` so the Quadra booted System 6.0.5 off it.
  Conventions the chunks follow, worth keeping: callbacks and cross-device
  pointers are **re-bound, never serialized** (a pointer becomes an index);
  pure caches are **flushed** on restore (ATC via the one vendored line
  `Moira::pomFlushAtcs()`, the 030 i-cache, the JIT guard) rather than carried;
  host-backed bulk data stays on the host (`ScsiDisk` ships a copy-on-first-write
  log of what the guest changed, not the image).
- [ ] **Optional HLE acceleration overlay** — **BLOQUÉ derrière § 3** par la
  décision du 2026-08-09 (§ 0·A). Ce que cette décision fixe aussi : la
  bifurcation § 2.3 de `HLE_OVERLAY.md` (HLE invité vs profil JIT relâché) se
  tranche en faveur du **profil JIT relâché côté hôte**, parce que le besoin
  réel est du *débit CPU soutenu sur 030/040*, pas de la latence d'I/O —
  patcher `.Sony` ne fait pas tourner le Finder d'une LC II plus vite.
  **QuickDraw n'est plus une exception présumée** : le recensement Rogue n'a
  pas démontré de blitter VRAM résiduel dominant.
  **La prémisse a bougé le 2026-07-31** : le JIT conformant mesure ×2,68 sur
  `q605_boot_etalon`, c'est-à-dire À TRAVERS le « plafond conformant ~×2,5-3 »
  que cet item invoquait comme justification. Son argument survivant est plus
  étroit et plus net : le coût résiduel au Finder au repos **est** le contrat
  d'exactitude ATC (794 M sorties « window lost » sur 12,2 G instructions), et
  un mode non conformant est exactement là où les cinq relaxations que le JIT
  refuse deviennent légales. Sur les invités 68000 et 68020 la fenêtre ne vaut
  que ×1,0-1,1, faute de marche ATC à sauter — ce qui est précisément où un
  overlay HLE aurait de la place.
- [ ] **Retro68 as a guest-level differential oracle**: build small Toolbox /
  Device Manager / XPRAM probes, run identical binaries under MAME and POM68K,
  compare. Known friction: no `Lists.h`/`AppleTalk.h` shims in multiversal
  (hardcoded list in `cincludes.rb`); the prober's `compat/` carries the
  PBControl glue; a 0-byte `.APPL` is normal.
- [ ] **Résidu d'état à l'échelle du processus** — voir § 0·B : le registre LLE
  appartient à la session depuis le 2026-08-27, ce qui reste est le
  `thread_local` du JIT, légitime tant qu'un thread = une machine.
- [ ] **Un jour sous `-DPOM68K_PRODUCT_LLE_GATES=ON`** : confirmer que le label
  `a64` en ligne des gates de préflight LLE survit à la fusion de labels
  (`cmake/Pom68kGatePolicy.cmake:96-110`).

---

## 9. One lesson kept out of the closed list

Everything closed before 2026-08-10 lives in `CHANGELOG.md` by date and in
`CHANGELOG_INDEX.md` by subsystem; this file no longer duplicates it. One line
survives because it is *about* this file:

**Block linking** (`POM68K_JIT.md § 9`) had a stale one-line summary here that
misdirected a whole planning round on 2026-07-30. It is a link *table* (O(1)
slot invalidation, no patched jumps); loading-phase block entries −53 %,
268 → 566 instr/entry, 18.4 → 16.75 s; at the idle Finder it helps less because
the hot exits are `JSR`/`RTS` (still `Unsafe`). **That is why the house rule at
the top of this file exists**: a closed item leaves at most one line, and that
line has to be true.

---

## 10. Revue d'architecture du 2026-08-28 — quatre constats, un plan par vagues

Cette section suit la forme de § 0·B : une revue est recopiée comme **travail**,
pas comme compliment ni comme grief. Différence de méthode avec celle du
2026-08-26 : celle-ci a lu le dépôt plutôt que sa documentation — 231 lignes du
manifeste `build/pom68k_gate_manifest.tsv`, les deux backends JIT comparés
symbole par symbole, les cinq workflows CI en entier. **Les quatre constats
ci-dessous ne sont pas dans les autres sections parce qu'aucune ne les
regardait.**

Ce que la revue ne remet pas en cause, et qu'il faut lire avec les constats :
la discipline de mesure de ce projet — le refus de citer un nombre sans son
protocole, la correction publique du « 4 h 30 », la séparation
`guestFamilies` / `autoFamilies`, la règle qu'un `ctest` vert ne vaut que la
fraîcheur de ses binaires. **Les quatre constats sont des angles morts d'une
méthode saine, et deux d'entre eux existent exactement là où cette méthode n'a
pas été appliquée à elle-même** : le JIT n'a pas eu droit à la règle « un
concern par fichier », et les jobs de release n'ont pas eu droit à la règle
« chaque jalon est gaté ».

### Constat 1 — le générateur x86-64 est compilé dans le binaire Windows et suit l'ABI System V

C'est le rouge de § 1. `CMakeLists.txt:311-317` ajoute
`src/jit/backends/JitBackendX64.cpp` et définit `POM68K_JIT_BACKEND_X64` dès
que `CMAKE_SYSTEM_PROCESSOR` correspond à `^(x86_64|AMD64|amd64)$` — et `AMD64`
est exactement ce que MSVC rapporte. Rien dans `src/jit/` ne mentionne
`_MSC_VER`. Quatre incompatibilités indépendantes, chacune fatale :

1. **Les arguments sont lus dans les mauvais registres.** Le prologue fait
   `movRR(Sz::Q, kCpu, RDI)` puis `movRR(Sz::Q, kFrm, RSI)`
   (`src/jit/backends/JitBackendX64.cpp:3384-3385`) ; Win64 passe les deux
   premiers arguments entiers dans `RCX` et `RDX`. Le premier bloc compilé
   déréférence un pointeur arbitraire.
2. **`RSI`/`RDI` sont sauvegardés par l'appelé sous Win64**, alors que le
   commentaire des rôles de registres (`JitBackendX64.cpp:211`) les déclare
   scratch et que le prologue ne pousse que `RBX`, `RBP`, `R14`, `R15`, `R12`,
   `R13` — corruption de l'appelant MSVC.
3. **Les appels aux thunks passent aussi leurs arguments dans `RDI`/`RSI`.**
4. **Pas d'espace d'ombre.** Win64 exige 32 octets réservés par l'appelant
   avant chaque appel ; le prologue ne réserve que 8 octets d'alignement.

`X64Backend::usable()` (`JitBackendX64.cpp:3673`) ne teste que la disponibilité
de mémoire W^X — jamais l'ABI. Et comme `jit/auto` est le défaut conformant sur
68040 **et** 68030, tout utilisateur Windows d'une Quadra, d'un Centris, d'une
LC II ou d'un IIvx prend ce chemin. Le fichier ne contient aucun GNU-isme, donc
MSVC le compile sans broncher : l'échec est à l'exécution, pas à la
construction.

**Ce qui a permis à ce défaut de vivre** est un fait de processus, pas de code :
le job `windows` de `release.yml` configure `-DPOM68K_TESTS=OFF`
(`.github/workflows/release.yml:289`) et ne lance aucun gate. C'est le seul
artefact publié que la suite ne touche jamais. D'où la cinquième règle de
méthode ajoutée en § 1 : **une configuration que personne n'exécute n'est pas
supportée, elle est seulement compilée.**

### Constat 2 — les deux backends JIT sont deux projets distincts

`JitBackendA64.cpp` (4 374 l.) et `JitBackendX64.cpp` (3 919 l.) **ne partagent
aucun nom de fonction**. Chacun porte son propre décodeur d'EA et sa propre
table de coûts — `kEaReadA64[E_COUNT][3]` d'un côté, `kEaRead[kM_COUNT][3]` de
l'autre. Or ces deux objets modélisent **le 68k, pas l'hôte** : ils n'auraient
jamais dû être écrits deux fois. La divergence est déjà observable dans
l'ensemble d'acceptation : `Op::Bitfield`, `Op::MoveSrToReg` et
`Op::ShiftRegister` sont émis par a64 et absents de x64.

Conséquences mesurables, pas théoriques : deux hôtes, deux taux de repli, donc
une mesure faite sur l'un qui ne transfère pas à l'autre ; et aucun gate ne
compare les deux `canEmit`. C'est la cause directe de l'item permanent « porter
les deltas 030 de a64 vers x86-64 » (§ 3) — chaque correction se paie deux
fois, à la main. **`D1F0` l'a payé le dernier, et il est clos** (2026-08-28,
`CHANGELOG.md` (fourth)) : le coût de l'EA de format complet est du 68k pur, et
il vit désormais dans `JitCost.h`. Ce qui reste à payer est nommé ligne par
ligne par la table d'exceptions de `jit_backend_parity_test` — c'est le backlog
exact du portage x64, dernière case de la vague 2.

### Constat 3 — 58 % de la preuve n'existe que sur un hôte

Lu dans `build/pom68k_gate_manifest.tsv`, colonne `assets` :

| classe | gates |
|---|---|
| `required` (ROMs + images disque) | **133** |
| `none` | 86 |
| `optional` | 12 |

La CI valide les 86. Les 133 autres — tous les etalons de boot, donc toute la
preuve « 37 profils démarrent le Finder » — dépendent de ~9,5 Gio d'assets
privés, sur une machine, lancés à la main. § 0 et § 0·B nomment déjà des
morceaux de ce fait (le palier `unit` à rejouer, la couverture à 28,93 %) ;
ce qui est neuf est le chiffre global et ses trois conséquences :

- ASan et TSan ne voient **jamais** le cœur d'émulation sous ROM réelle — la
  jambe sanitizer tourne sur `asset-none` ;
- la couverture publiée mesure le palier CI ; cinq enveloppes CPU sur dix y
  étaient à 0,00 % avant `cpu_wrapper_smoke` ;
- le facteur bus est de 1, et la perte de cette machine coûterait la totalité
  de la preuve.

### Constat 4 — hygiène et coût documentaire

Deux faits, chiffrés, sans dramatisation : `-Wall -Wextra` est arrivé le
2026-08-27 sur un arbre C++20 de 165 000 lignes et `-Werror` n'est toujours pas
armé (§ 0·B) ; et l'ensemble documentaire pèse **31 800 lignes de Markdown**,
dont un `CLAUDE.md` de 5 592 mots qui promet en tête d'être parcourable en
moins d'une minute. `docs_test` compare aujourd'hui une prose écrite à la main
au manifeste ; l'inverse serait moins cher et ne pourrait pas dériver.

### Le plan, par vagues

**Vague 0 — avant la prochaine release.** **POSÉE le 2026-08-28**
(`CHANGELOG.md` 2026-08-28 (later)) : branche `auto` x64 exclue de `WIN32`
(propriété de l'OS cible, MinGW compris — plus large que le `MSVC` du
constat), `X64Backend::usable()` refuse sous `_WIN32` avec le commentaire
d'ABI, jobs de release Windows et macOS construisent les tests et lancent
`ctest -L asset-none`, incident écrit.

- [ ] **La question de suite reste ouverte et indécidable ici** : Windows
  garde-t-il `threaded` de façon permanente et documentée, ou le backend x64
  apprend-il l'ABI Win64 derrière un gate ? **Ne pas trancher sans un hôte
  Windows pour mesurer** — le tier `jit-fast` est exactement l'instrument,
  et il est déjà asset-free. Les deux gardes portent « remove both
  together, behind a gate » pour qu'un port ne puisse pas atterrir à moitié.
- [ ] **Lire la première exécution du palier `asset-none` sous MSVC** : ce
  palier n'a jamais compilé ni tourné sous MSVC, un rouge y est une
  découverte, pas du bruit.

**Vague 1 — rendre la preuve reproductible.** Meilleur rapport valeur/effort de
tout ce fichier après la vague 0.

- [ ] **Un runner auto-hébergé sur la machine qui porte les assets.** Le palier
  `full` devient déclenchable par push, `LastTest.log` devient un artefact, et
  « 228/228 » cesse d'être une phrase dans un document pour redevenir un état
  vérifiable. `tools/gate_execution_census.py` publie déjà la bonne paire ;
  il ne lui manque qu'un endroit où tourner tout seul.
- [ ] **ASan sur trois etalons de boot**, un par famille CPU (68000, 030, 040),
  en nightly sur ce runner. Le cœur n'a jamais été instrumenté sous ROM réelle,
  et c'est là que vivent les défauts mémoire — pas dans `adbline_test`.
  Contrainte héritée de § 0·B : **ne pas forcer `POM68K_CPU_ENGINE`** dans une
  jambe sanitizer, sous peine de transformer un gate JIT en test d'autre chose.
- [ ] **Mesurer la couverture une fois avec les assets**, et publier le nouveau
  `coverage-zero.txt`. La liste actuelle décrit le palier CI, pas le produit.

**Vague 2 — dissoudre la dette JIT.** C'est § 3 vu par l'architecture plutôt
que par le ROI d'opcode ; les deux listes se sont rejointes sur `D1F0`, clos
depuis (ABBA nul).

> **Cette vague n'est plus un nettoyage, c'est le chemin critique de § 3.**
> Décidé le 2026-08-28 : l'item `D1F0` — 53 % des replis sous charge
> applicative, **tenu ce jour-là pour le meilleur ROI du fichier** — est
> **bloqué derrière l'extraction ci-dessous**, et un prototype non commité dans
> l'arbre de travail montre pourquoi. Il écrit un prédicat de sous-forme et une
> constante de 6 cycles dans `JitBackendA64.cpp` ; ces deux objets sont du 68k,
> donc les livrer là crée immédiatement leur jumeau à écrire dans
> `JitBackendX64.cpp`. **Le
> backlog appelle ça « porter les deltas » et le paie depuis des semaines** —
> ce n'est pas une tâche récurrente, c'est le symptôme d'une table écrite au
> mauvais endroit. Faire l'extraction avant `D1F0` la fait disparaître ; la
> faire après, c'est payer le portage une fois de plus et rouvrir l'item.
>
> **L'ordre a tenu, le gain non — et la phrase « meilleur ROI du fichier » est
> périmée depuis le soir même.** L'ABBA murale de `D1F0` a rendu **+0,02 % sur
> un plancher de 0,6 %** (`CHANGELOG.md` 2026-08-28 (seventh)) : l'extraction
> se défend par son seul argument d'architecture — une table du 68k écrite une
> fois, et un gate qui compare les deux `canEmit` — et jamais par le temps
> qu'elle devait débloquer. Aucun item ci-dessous n'hérite du titre : **le
> prochain levier mural exige un profil temporel** (§ 3), pas un rang dans un
> histogramme de replis.

**FAITE le 2026-08-28**, critère d'acceptation tenu :

- **`src/jit/JitCost.h` + `src/jit/JitEaPlan.h` extraits.** Colonnes de
  cycles 68020 (`kEaRead`, `kMoveDst`, `eaRmwCost`), pénalité CMPA
  (`kCmpaExtraCycles`), pénalités de format complet (`fullIndexPenalty`,
  `fullFormatReadExtra` — le prédicat de sous-forme et les 6 cycles du
  prototype `D1F0`), `EaPlan` + `decodeEaPlan` (l'admission opt-in par
  appelant). Les deux backends les LISENT — l'opt-in `AddressAlu` est ouvert
  des deux côtés — et `docs_test` § 9 verrouille la frontière : pas de
  `int8_t kEaRead`, pas de `findEffectiveAddress(`, pas de
  `baseDisplacementWords != 2` dans un fichier `backends/`.
- **`jit_backend_parity_test` posé, asset-free, 65 536 opcodes** — les deux
  TU backends compilent sur tout hôte ISA (ils n'exécutent rien). Premier
  balayage : **15 groupes de divergence**, pas 3 — la table d'exceptions
  datées les nomme tous (5 couvertures a64-only réelles, 10 sur-déclarations
  `canEmit` x64 sur des encodages pour la plupart illégaux, que son émetteur
  refuse ensuite). Une ligne périmée fait AUSSI échouer le gate, pour qu'une
  exception ne survive pas à la correction qu'elle attendait.
- [ ] **Ce qui reste de « porter les deltas »** : les lignes a64-only de la
  table d'exceptions du gate sont le backlog exact du portage x64, en
  rouge-immédiat si l'une bouge sans son jumeau. **Les shifts registre sont
  PORTÉS le 2026-08-29 (sixth)** — le déroulé pas-à-pas d'a64 traduit, la
  ligne d'exception retirée, 82,0 → 88,1 % natif au boot LC II, mur plat
  (la leçon D1F0 tient une 2e fois). **JSR `$4EB0` est PORTÉ le
  2026-08-31 (third)** : x64 partage maintenant le calcul pré/postindexé, le
  coût et la transaction pointeur/pile d'a64. Restent, sur cette photo
  historique à reclasser par un census frais : TST mémoire `$4A11` (552 k),
  destinations indexées de MOVE (`$2F70`,
  391 k), bit ops mémoire (`$08A9`, 332 k), MOVE from SR (105 k),
  bitfields. **L'« ordre `(An)+` » (`$24D0`, 1,64 M) est SORTI de cette
  liste le 2026-08-29 (seventh)** : mesuré identique dans les deux modes
  thunk, ce sont des refus de sonde mémorisés (probe 66 k / codepage 25 k /
  notram 25 k) — un port d'émetteur n'y changerait rien, et a64 porte les
  mêmes familles en tête de ses propres replis runtime. Le vrai levier
  serait un chemin d'écriture directe observée par le guard — une question
  de design, nommée, pas un portage.

**Vague 3 — hygiène et coût documentaire.** Continu, aucun de ces items ne
bloque un autre.

- [ ] **Basculer `-Werror`** une fois le recensement g++ propre — c'est déjà
  l'item nommé en § 0·B, il ne demande qu'à être fini.
- [x] **Générer les chiffres au lieu de les vérifier — FAIT le 2026-08-29.**
  `tools/status_md.py` écrit `STATUS.md` depuis les trois fichiers que CMake
  produit à la configuration (`pom68k_gates.tsv`, `pom68k_gates_absent.tsv`,
  `pom68k_gate_manifest.tsv`) : l'union et ses totaux par label — dérivables à
  l'identique sur chaque hôte, le roster absent réajouté — une section
  *Registered on \<hôte\>* possédée par l'hôte qui la régénère (même partage
  que `gate_resource_budgets.tsv` : l'autre section est préservée telle
  quelle), et une table de runs enregistrés portant la paire
  exécutés/soft-skippés du census (`--record-run`, jamais dérivée). `docs_test`
  § 16 re-dérive l'artefact entier depuis le même roster/manifeste et échoue
  sur toute dérive ; une section hôte jamais générée est une *absence*
  signalée par une note, pas un rouge — l'hôte AArch64 lancera l'outil une
  fois. La prose chiffrée de `CLAUDE.md`/`DEV.md` reste en place, tenue par
  les checks existants : la faire maigrir vers `STATUS.md` est l'item plafond
  ci-dessous.
- [ ] **Appliquer à `CLAUDE.md` sa propre règle** : le plafonner dans
  `tools/file_size_budget.txt` comme n'importe quelle unité de code, et pousser
  ce qui déborde vers `STATUS.md` et `DEV.md`. Il est chargé à chaque session ;
  5 592 mots ne sont pas « parcourable en moins d'une minute ».
