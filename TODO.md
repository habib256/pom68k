# TODO — feuille de route

Ce fichier contient uniquement du travail ouvert. Les résultats, mesures,
fausses pistes et décisions terminées vivent dans `CHANGELOG.md` et
`CHANGELOG_INDEX.md`. Les détails d'implémentation vivent dans `DEV.md`,
`src/jit/POM68K_JIT.md`, `docs/JIT_BRINGUP.md` et les notes vendor.

`STATUS.md` est généré depuis les manifests CMake et reste la source de vérité
pour les gates. Toute tâche fermée quitte ce fichier dans le même changement
qui ajoute son entrée au `CHANGELOG`. Une tâche dit ce qui **reste** : ce qui
a été tranché en chemin part au `CHANGELOG`, la tâche n'en garde qu'un
pointeur.

**Un renvoi extérieur cite le NOM d'une section, jamais son numéro** — écrire
`TODO.md § Services réseau`, abrégeable à son premier mot quand la place
manque (`§ Objectif`, `§ Phase 1`, `§ Phase 2`, `§ Vitesse`, `§ Preuve`,
`§ Services`, `§ Fidélité`, `§ Nouvelles machines`, `§ En sommeil`,
`§ Recherche`, `§ NeoST`). Une sous-section se cite `§ Phase 1 › Socle`,
`› Boucle amont`, `› Couverture`, `› Défauts`, `› Listes`,
`› Justesse 68030`, `› Justesse 68040`, `› Temps`, `› Interprète`,
`› Surface`, `§ Phase 2 › JIT justesse`, `› JIT performance`. Les renvois
antérieurs à la réorganisation du 2026-11-24, `§ Moteur › <sous-section>`,
désignent la sous-section de même nom, rangée désormais dans l'une des deux
phases (`› Convergence` : dans § NeoST). Les numéros bougent à chaque
réorganisation.

## Objectif principal — la Moira de Dirk pour tous les Macintosh 68k, puis le JIT

**Le but.** Améliorer la Moira de Dirk Hoffmann — d'abord en correctifs de
notre file (`extern/moira/patches/`), puis en pull requests chez lui —
jusqu'à ce que son cœur émule parfaitement toutes les ROM des Macintosh 68k
et tous les Mac OS 68k (décision du propriétaire, 2026-11-24). Un défaut
trouvé sur un Mac qui est un comportement du processeur va dans son cœur ;
seul ce qui est propre au Macintosh reste dans la couche de POM68K
(`src/MoiraCompat.h`, les wrappers CPU).

**Deux branches.** `main_old` (`0139930a`, poussée) fige l'ancien POM68K :
le fork NeoST de Moira et le JIT actif, tels qu'avant la bascule
(`1689ebe7`). Elle ne reçoit plus de travail ; elle sert de référence pour
comparer un comportement et de source pour reconstruire le JIT. `main` est
la nouvelle référence : la Moira de Dirk (`master`, commit dans
`extern/moira/patches/BASE`, branche FPU fusionnée) plus la file de
correctifs (`extern/moira/POM68K_VENDOR.md` § Inventory of local patches),
interpréteur seul — le JIT est compilé hors (`POM68K_JIT=OFF` par défaut),
ses gates enregistrés absents dans `cmake/pom68k_jit_gates.tsv`. Bascule et
état de départ : `CHANGELOG` 2026-11-24 (`2026-11-24-dirk-moira-base`).

**Deux phases, dans l'ordre** (décision du propriétaire, 2026-11-23/24) :

1. **§ Phase 1 — Valider l'interpréteur** jusqu'à ce que tout soit vert, au
   sens du critère de sortie écrit en tête de la phase (règles de
   `CLAUDE.md` § Validating the interpreter). En cours.
2. **§ Phase 2 — Reconstruire le JIT** sur ce cœur, petit à petit à partir
   du code de `main_old` : fonctionnel d'abord, rapide ensuite, chaque étape
   admise par ses propres gates (`src/jit/POM68K_JIT.md`).

Tout le reste (jalons produit, vitesse hors CPU, NeoST, dépôt) est après les
deux phases, hors du chemin principal.

---

## Phase 1 — Valider l'interpréteur

**Sortie — « tout est vert », concrètement :**

1. **Le cœur de Dirk, avec des correctifs proposables chez lui, démarre
   chaque ROM du catalogue** (`kMachineProfiles`, `src/MachineCatalog.h`)
   **et chaque logiciel système pour lequel POM68K a un gate**, par un gate
   exécuté (pas sauté faute d'asset) sur les deux hôtes — AArch64 et
   x86-64.
2. **Les listes de divergences connues sont vides ou tranchées** :
   `tests/known/` (via `tools/known_failures.py`) ne garde plus aucune
   rubrique « OPEN DEFECT » ; chaque ligne restante porte une décision
   écrite (manuel cité, ou capture de grade A attendue et nommée), et les
   cellules mises de côté par `#define` dans `tests/fpu040_test.cpp`,
   `tests/fpu_sanity.cpp` et `tests/moira_state_test.cpp` sont rendues ou
   retirées avec leur raison.
3. **Les planchers des vecteurs publics** (`floor` dans
   `tests/cpu_vectors/manifest.json`) ne baissent jamais ; ils montent à
   chaque correctif et chaque écart restant est expliqué.
4. **`cmake/pom68k_pending_gates.tsv` est vide** : chaque gate réécrit sur
   son cœur et enregistré, ou retiré avec sa raison au `CHANGELOG`.
5. **Chaque correctif de la file qui touche un comportement CPU est proposé
   chez Dirk** (ou couvert par lui et retiré de la file) ; ce qui reste dans
   la file est nommé et justifié.
6. **Les paliers passent** : `asset-none`, `cpu-vectors`, `m030`, `m040`,
   `etalon` et les etalons des machines 68000/68020, gates à assets
   exécutés et non sautés, sur les deux hôtes ; `ci.yml`, `macos.yml` et
   `nightly.yml` verts ; une exécution enregistrée dans `STATUS.md`
   (`tools/status_md.py --record-run`).
7. **Le temps est requalifié** sur ce cœur (`› Temps`) avant tout
   ré-épinglage d'écran.

L'interpréteur est l'oracle du JIT à venir : une ligne de justesse passe
avant une ligne de vitesse du même chemin. Qui gagne un arbitrage est écrit
(`CLAUDE.md § Evidence grades`, `oracle/README.md`) : le manuel du modèle
sur l'oracle, sauf exception connue.

### Boucle amont — la file de correctifs vers Dirk

Fil récurrent de toute la phase, pas une action unique. Pour **chaque**
défaut CPU, dans cet ordre :

1. Récupérer son `master` et lire ses nouveaux commits qui touchent
   `Moira/` **avant** d'écrire quoi que ce soit : il corrige en parallèle
   (son `58e0405b` a rendu un des nôtres inutile). Rebaser la file
   (`extern/moira/POM68K_VENDOR.md` § Updating the base) et retirer le
   correctif qu'il couvre.
2. Reproduire sur son arbre non modifié
   (`tools/dirk_moira/findings_repro.cpp`) ; un défaut qui ne s'y montre
   pas n'est pas le sien.
3. Citer le manuel verrouillé du modèle (grade B), ou nommer la capture
   qui manque (grade A ou F).
4. Écrire le correctif dans son style, un commit par correctif, exporté dans
   `extern/moira/patches/`, avec son gate ou son reproducteur dans
   l'inventaire de `POM68K_VENDOR.md`.
5. Le proposer en petite PR contre `master` (accord dans la discussion #36,
   2026-11-23), avec un test `Tests/Unit` tiré du reproducteur.
   **Le propriétaire voit et approuve le texte exact avant toute
   publication** (PR, commentaire GitHub, discussion #36).
6. Retirer le correctif de la file quand son `master` le couvre, puis
   rejouer `findings_repro` sur l'arbre vierge et l'arbre corrigé, et les
   paliers machines.

- [ ] **Proposer la file actuelle à Dirk, une PR par correctif.** Les
  correctifs de `extern/moira/patches/` (inventaire dans `POM68K_VENDOR.md`)
  partent un par un selon la boucle ci-dessus ; le 18 (FPU logicielle
  optionnelle d'un LC040) rétablit une configuration POM68K : décider avec
  lui s'il entre chez lui ou s'il reste une option de la couche Macintosh.
- [ ] **Trier ce qui reste dans la couche de POM68K.** `src/MoiraCompat.h`
  garde des membres inertes du fork (couture `pomJit*`, surcouche
  `pomIcache`, verrous `mmuPipeCnt`/`trace040Pending`/`irqDelay`) et les
  entrées `extBusError*()` : pour chacun, comportement CPU (→ correctif de
  la file), besoin Macintosh (→ reste), ou mort (→ retrait).

### Couverture — ROM et systèmes

- [ ] **Inventorier d'abord la couverture prouvée sur ce cœur.** Pour
  chaque profil de `kMachineProfiles` : quel gate exécuté prouve son boot
  sur le cœur de Dirk, sur quel hôte, avec quel système ; et pour chaque
  version de Mac OS dont POM68K a un volume de référence, quels gates la
  démarrent. Les exécutions enregistrées dans `STATUS.md` datent toutes du
  fork : aucune ne prouve encore ce cœur, et ce fichier ne sait pas dire
  quels trous restent — l'inventaire est la première action, ses résultats
  vont au `CHANGELOG`, les trous trouvés deviennent des lignes ici.
- [ ] **Vérifier la mémoire virtuelle de Mac OS sur Quadra.** Liée au
  défaut prioritaire ci-dessous : un gestionnaire de mémoire virtuelle
  termine une écriture fautive depuis WB3 ; un boot 68040 avec la mémoire
  virtuelle active dit si le défaut touche un invité réel.

### Défauts ouverts du cœur

- [ ] **Prioritaire — faute d'écriture 68040.** Le 68040 termine une
  écriture fautive après l'instruction (le gestionnaire vide WB3,
  MC68040UM §8.4.6, §8.4.6.5) ; le cœur de Dirk relance l'instruction, qui
  applique deux fois un lecture-modification-écriture une fois le
  write-back fait (388 lignes de `tests/known/fault_transparency_test.txt`).
- [ ] **Faute de pipe d'instruction signalée à l'usage de l'étage**
  (MC68030UM §8.2.1, FB/FC) : le cœur faute l'instruction qui a préchargé et
  la relance, une instruction non idempotente (TAS, champ de bits, FPU) agit
  deux fois (38 lignes de `fault_transparency_test.txt`).
- [ ] **Bit RM du SSW pour TAS/CAS/CAS2** (MC68020UM §6.2.1 ; rubrique
  « OPEN DEFECT » de `tests/known/berr020_test.txt`). À corriger dans la
  file.
- [ ] **Continuation après RTE quand DF est effacé au-delà du cycle
  terminé** (MC68030UM-1989 §8.2.2) : rubriques « Restart model » de
  `berr020_test`, `berr030_test` et `fault_transparency_test` ; sur la
  liste de Dirk après sa version (discussion #36).

### Listes de divergences connues

Chaque ligne d'une liste se corrige (correctif de la file), se tranche
(manuel cité dans la rubrique) ou attend une capture nommée.

- [ ] **`moira_rules_test`** (`tests/known/moira_rules_test.txt`, 395
  échecs) : cellules du fork, non revues une à une sur ce cœur — contenus
  de cadre du modèle de continuation, lectures WinUAE, détails PTEST/MMUSR,
  temps, défauts possibles.
- [ ] **`berr030_test`** : les lectures WinUAE épinglées par le fork sans
  phrase du manuel (format de cadre d'un PMOVE en écriture traduite qui
  faute, image brute de FP7, ordre de bus d'un FMOVEM indirect) — à revoir
  contre MC68030UM.
- [ ] **`berr020_test`** : la rubrique « Ruling » (ordre des mots d'un long
  en `-(An)`, MC68020UM-1992 §5.2.1) est tranchée ; vérifier qu'elle ne
  cache rien d'autre.
- [ ] **`icache020_test`** : totaux de cycles notés [F] (modèle du fork,
  aucune capture de bus) et remplissage du cache au premier fetch d'un
  gestionnaire d'exception ; les boucles du manuel passent (correctifs 2
  et 3). *Bloqué pour les totaux : un TimeDBRA d'un vrai Mac II ou LC, ou
  une capture de bus* — y compris le +6 d'un port étroit et le DSACK par
  région de la GLUE (32 bits) et du V8 (16 bits).
- [ ] **Cellules `#define` à zéro** : `fpu040_test` (type de donnée non
  supporté, vecteur 55, et cadres exceptionnel/BUSY — sur la liste de
  Dirk ; ordre FMOVEM 040 de WinUAE ; prédicat FScc sans source),
  `fpu_sanity` (temps FPU, MC68882UM Table 8-3 ; 68882 interrompu en cours
  d'instruction, format $9), `moira_state_test` (verrous du fork,
  continuation 040, modèle de cache 040, FPU BUSY 040, 68882 interrompu).

### Gates en attente

`cmake/pom68k_pending_gates.tsv`.

- [ ] **Décider du modèle de cache 68040 au-dessus du cœur**
  (`cache040_test`), avant le JIT : le mode cache actif
  (`POM68K_040_DCACHE=1`) et ses lignes de `› Justesse 68040` en
  dépendent.
- [ ] **Réécrire `mmu040_atc_test` sur son MMU040.**
- [ ] **CALLM/RTM** (`callm_rtm_test`, `moira_interp_callm_rtm_test`) :
  talons chez lui, inutilisés par Mac OS — un gate réécrit ou un retrait
  motivé.

### Couche Macintosh

- [ ] **Remplacer `src/Mmu030Walk.h` par `MMU::peek`** (Dirk, `5fde078`),
  en gardant la lecture des pages superviseur pour le débogueur
  (`src/Mmu030Peek.h`).
- [ ] **Dédupliquer les wrappers 68030.** Les quatre wrappers 040 partagent
  `Moira040Cpu.h` ; `RbvCpu`, `VaspCpu` et `SonoraCpu` partagent
  `Moira030Cpu.h` (`CHANGELOG` 2026-11-21). Restent : `Cpu030` (LC II),
  `Pb030Cpu` et `MscCpu`, dont la cadence diffère vraiment (paquets de 256
  cycles sous le Power Manager, reset tenu par le PMU) ; la sonde
  coprocesseur FC = 7 émulée dans deux wrappers (`Cpu030`, `Pb030Cpu`) —
  à requalifier : le cœur de Dirk émet-il les cycles d'espace CPU ?
- [ ] **Décider l'hybride 68LC040 + 68882 logiciel.** Défaut produit des
  LC 475/575/580 et Centris 610/650, combinaison qui n'existe pas en
  matériel ; un vrai `NONE` boote déjà le Quadra 605
  (`q605_barefpu_boot_etalon`) ; l'option repose désormais sur le
  correctif 18.

### Temps — le modèle de débit 030/040

À requalifier en entier sur le cœur de Dirk : la surcouche de temps du
fork (`pomIcache`) est inerte, le cœur compte les cycles 020/030 avec son
propre cache (correctifs 2 et 3), et les empreintes machine ont bougé.

- [ ] **Requalifier le facteur `cacheBoost` (×4).** Il compensait le modèle
  sans cache du fork. Mesurer le temps CPU au Finder par carte avant de
  ré-épingler d'autres écrans. Contexte : sur le LC II du fork, TimeVIADB
  `$030F` contre `$0187` chez MAME, TimeDBRA `$28C9` contre `$0F4A`
  (`CHANGELOG` 2026-10-02 (late night)) — même cause que la course ADB du
  Centris (§ Fidélité) et que l'ordre d'allocation du tas système. IIx,
  IIcx, SE/30 (`Cpu020`) et IIfx n'ont pas de boost. *La calibration
  absolue reste bloquée par un chiffre matériel (TimeDBRA/TimeVIADB d'un
  vrai LC II, Centris ou Quadra).*
- [ ] **Sortir le coprocesseur externe et le temps de bus de données du
  boost.** *À reproduire, à requalifier sur le cœur de Dirk* (le chemin
  du fork, `sync` puis division dans `flushTicks`, n'existe peut-être plus
  sous cette forme) : le 68882, puce distincte, ne doit pas aller quatre
  fois trop vite en temps machine. `DEV.md` § 1.2 pose la règle pour un
  cycle VIA.
- [ ] **Donner au 68040 ses propres coûts.** Sur le fork, le FPU intégré
  était facturé aux tables du MC68882 (FADD 56 cycles, FSQRT 110), les
  marches de tables ne coûtaient rien, CINV/CPUSH/MOVE16 étaient un forfait.
  *À requalifier sur le cœur de Dirk.*
- [ ] **Affiner le temps du 6888x.** 68881 du Mac II facturé aux tables du
  68882, concurrence du 68882 non modélisée, cycles 030/040 jamais comparés
  à l'oracle ; temps FPU absent du cœur de Dirk (`fpu_sanity`,
  `POM68K_FPU_TIMING`). *À requalifier sur le cœur de Dirk.*
- [ ] **Vérifier le double comptage du temps en mode cache actif.** *À
  reproduire, après la décision sur `cache040_test`.* Sur le fork,
  remplissage (8) et push (2 par long) s'ajoutaient à des tables qui
  facturent déjà chaque accès, multipliés par le boost ; le boot cache actif
  n'atteignait pas le Finder dans le budget de cinq milliards de cycles
  invités.
- [ ] **Latence de prise d'interruption (`irqDelay` = 2 sur le fork).**
  Profondeur empirique réglée sur SimCity 2000, dont le symptôme a reçu
  ensuite une autre cause (Egret, `CHANGELOG` 2026-07-17). Le membre est
  inerte dans `src/MoiraCompat.h` : *à requalifier sur le cœur de Dirk* —
  mesurer si un invité en dépend, sinon retirer le membre ; un modèle de
  latence IPL vient d'une trace matérielle.
- [ ] **Rendre le PIC1654S des cartes DjMEMC indépendant du découpage en
  tranches, puis leur porter l'échéance VIA.** Q630 et Q700/900/950 l'ont
  (`CHANGELOG` 2026-11-07 (seizième)) ; sur les Centris 610/650 et Quadra
  610/650/800 elle a été retirée : tout changement de la tranche déplace la
  réponse de l'ADB (`adb.dev2.service` des Prober q800 puis q610 contre
  MAME, bissecté). Le temps du PIC doit être fonction du temps machine ; un
  gate qui découpe la même fenêtre en deux tailles de tranche et compare le
  flux ADB le prouvera, puis la recette (`CHANGELOG` 2026-11-07
  (quatorzième)). Reproducteur au 2026-11-22 (fork, JIT) :
  `centris_soak_etalon` rouge sur le M4 (`a64`), vert sous Rosetta (`x64`)
  — à rejouer sur ce cœur. Reste nommé sur le Q605 : les lectures
  53C96/ASC/DAFB et les octets pseudo-DMA voient l'appareil là où le
  dernier flush l'a laissé (retard ≤ une tranche) ; le modèle de
  `syncSwimFromCpu` l'effacerait pour le 53C96 et l'ASC au prix du débit
  sérialisé (`scsiDebt_`, `q605_event_scheduler_test`).
- [ ] **Modéliser l'intervalle hors tension du power cycling des PowerBook
  030.** Le réveil est un reset immédiat (`Pb030Memory::consumeReset`) :
  1,29 M de resets par boot de PowerBook 180 (mesure du fork), le
  processeur tourne dans le chemin de réveil de la ROM au lieu d'attendre
  l'interruption suivante. Changement de temps machine : son témoin (ROM :
  la séquence d'endormissement ; MAME `jaws_w` @ révision à nommer) et un
  ré-épinglage éventuel des écrans.

*Obsolète :* « Rapprocher `PomIcache` du MC68030UM » — la surcouche de cache
du fork est inerte (`src/MoiraCompat.h`) ; le cache du cœur de Dirk se juge
par `icache020_test` et sa liste connue (§ Phase 1 › Listes).

### Socle de preuve CPU

Les gates sans Macintosh (`sst68000`/`030`/`040`, `fpu_sanity`,
`fpu040_test`, `berr030_test`, `moira_rules_test`,
`fault_transparency_test`, et ceux en attente) sont la preuve qui voyage
avec le cœur ; les grands corpus sont épinglés dans
`tests/sst_corpora.lock` et restent des assets optionnels.

- [ ] **Activer la RAM stricte dans le replay public.**
  `tools/cpu_vectors.py replay` ne passe pas `--strict-ram` (les gates
  `sst68030`/`sst68040` le font). Sur le fork, le corpus public 030 passait
  1127 / 1128 avec ce contrôle (`$2010A3` vaut `$09` au lieu de `$01`,
  vecteur `rmw/identity seed=2026111532 #15 geometry=4k-2level-long`). *À
  requalifier sur le cœur de Dirk*, dont les vecteurs publics tournent
  contre un plancher. Ne pas faire d'exception de vecteur entier pour
  rendre le contrôle vert.
- [ ] **Rejuger les décisions prises contre un manuel, là où aucun gate ne
  tombe.** Exceptions connues (`oracle/README.md § Rulings taken against a
  manual`) : D30 (trappe de type de donnée immédiate), D44 (CAS2 `Dn:Dn`
  sur 68040), et deux règles 68000 (sortie anticipée de DIVS, ASR au-delà
  de la largeur), désormais tenues par les correctifs 20 et 21 en mode
  `DEFAULT`. La renverser demande une preuve de grade A, ou une divergence
  déclarée sans perdre de vecteur. Reste aussi à passer au crible les
  règles 6888x que l'audit soupçonne : FPIAR, vecteur d'exception gardé
  armé, longs internes de FSAVE IDLE, PTEST niveau 0, PMOVE TC invalide
  (le bit 9 du SSW est tranché réservé, `CHANGELOG` 2026-11-24).
- [ ] **Rendre les corpus épinglés d'origine récupérables.** `sst68030_solo`
  et `sst68040_8k` se régénèrent depuis l'arbre (recette dans
  `tests/sst_corpora.lock`). Les deux corpus d'origine (`sst68030`, 116 Mo ;
  `sst68040`, 315 Mo) n'ont pas de recette et n'existent que sur deux hôtes
  et le disque privé : les publier (asset de release, sortie WinUAE sans
  donnée Apple), ou les remplacer par des corpus à recette.
- [ ] **Donner au corpus FPU du 68040 les exceptions armées et les
  caches.** `sst68040_fpu` pose FPCR avec des exceptions armées sur un
  vecteur sur dix seulement, et aucun corpus 68040 n'a `cacr` ≠ 0 : les
  caches porteurs de données n'ont que `cache040_test`, en attente.
- [ ] **Couvrir le 68020 nu par l'oracle.** Aucun corpus 020 ;
  `callm_rtm_test` (en attente) et `berr020_test` tiennent des règles
  ciblées. Le fetch à file de préfetch repose sur les boots Mac II et LC.
- [ ] **Décider ce que fait un 68010 interrompu avec une pile impaire.** Vu
  sur le fork en écrivant `oddStackInterruptFrame` (`CHANGELOG` 2026-11-13
  (tenth)) : interruption prise, handler jamais atteint. Un 68010 a un bus
  16 bits : erreur d'adresse attendue. Aucun Macintosh n'a de 68010 ; *à
  requalifier sur le cœur de Dirk*, à trancher avec l'amont et NeoST.
- [ ] **Donner des vecteurs à plusieurs instructions aux autres états en
  vol.** `fuzz030.py`/`fuzz040.py --steps` et le champ `steps` existent ;
  ils tiennent D24 (`sst68040_fpu2`) et les exceptions postées du 6888x
  (`sst68030_fpu2`). Il y faut des familles dont la première instruction
  arme l'état : ATC (remplacement, bit S), pipe de préfetch, verrou du RTE
  $B du 68030.
- [ ] **Obtenir une preuve indépendante du 6888x.** Le FPU de Dirk a sa
  propre arithmétique (`extern/moira/Moira/FPU/`, conversions entières
  d'après Berkeley SoftFloat) : *à requalifier* — ce que valent encore
  l'oracle WinUAE et `fpu_sanity` comme juges. L'interruption en cours
  d'instruction (format $9, BUSY) n'a plus de gate (`POM68K_FPU_MIDINSTR`).
  *Bloqué : vecteurs d'un vrai 68881/68882, ou une seconde référence
  numérique.*

### Justesse 68030, 68020 et 6888x

Ces lignes ont été écrites sur le fork : chacune est *à requalifier sur le
cœur de Dirk* — son comportement d'abord, puis correctif de la file, ligne
de liste connue ou capture attendue.

- [ ] **Relever le cycle d'opérande d'un champ de bits de trois octets.**
  MC68030UM-1989 § 11.6.14 compte un seul cycle sous cinq octets et le bus
  020/030 a un SIZ de trois octets ; le fork et l'oracle émettaient un mot
  puis un octet (`execBitFieldEa`). *Bloqué : capture de bus de
  `BFEXTU (An){0:24}` sur un vrai 68030 ou 68040.*
- [ ] **RTE $B : le RMW verrouillé à DF effacé.** WinUAE tient
  l'instruction pour finie, le manuel suggère de continuer avec le tampon
  d'entrée ; trancher sur un vrai 68030 (TAS sur page protégée, DF effacé
  par le gestionnaire). Dépend de la continuation (§ Phase 1 › Défauts).
- [ ] **Transparence des fautes : les angles morts du gate.** Hors champ de
  `fault_transparency_test` : (1) le store d'un champ de bits sur 68040
  (PC de l'instruction et WB3 valide : BFCHG basculerait deux fois) — [F] ;
  (2) les stores de CAS et CAS2 sur 68040 ; (3) un MOVEM qui charge son
  propre registre de base ou d'index avant qu'une lecture suivante faute
  (ruling D34) ; (4) les opérandes décimaux condensés du FPU.
- [ ] **TRAPV pris en fin de page sur 68030.** `opcells.py generate --set
  classes --pagend --seed 8101` : le fork prenait la faute de bus de son
  second mot de remplissage, l'oracle le trap (10 vecteurs). Sur 68040 le
  ruling D39 donne la faute d'accès.
- [ ] **Rendre le 68020 nu transparent à une faute suivie d'un RTE nu.** Un
  vrai 68020 rejoue le cycle quand DF, RB ou RC est resté armé ([B]
  MC68020UM-1992 §6.2) ; le fork n'y modélisait que les cycles terminés par
  logiciel (`berr020_test`). Ajouter la ligne 68020 à
  `fault_transparency_test` une fois la reprise écrite.
- [ ] **Confirmer RTR sur un vrai 68030 (D45).** [F] : A7 vu par le
  gestionnaire quand l'adresse de retour faute (`oracle/README.md`,
  `disputes/NOTES.md`).
- [ ] **T0 : les restes.** (1) un FBcc pris vers une cible impaire sous
  T0, 68030 : l'oracle trace d'abord, le fork levait l'erreur d'adresse
  (11 vecteurs sur 12 000 `random/off seed=31`) ; (2) sur 68040, `MOVEC
  Rc,Rn`, `MOVE USP,An`, FMOVEM depuis la mémoire et FRESTORE ne tracent pas
  chez WinUAE, [F] ; (3) le 68020 nu n'a que `moira_rules_test` ; (4) STOP
  sous T0 quand le SR chargé ne change rien : tranché pour le manuel, un
  vrai 68030/68040 le confirmerait.
- [ ] **Comprendre les mots d'état internes d'un MOVES indexé en faute.**
  `opcells.py generate --set ea --mmu tt --seed 5401 --n 15`, vecteur
  « op=MOVES_B.ixb #1584 » (68030) : différent dans les seuls mots internes
  de l'oracle (`CHANGELOG` 2026-11-20 (second)). [F] ; sans effet invité
  connu.
- [ ] **Trancher CAS2 `Dn:Dn` sur 68040 (D44).** Le manuel (M68000PRM-1992,
  CAS2) met l'opérande mémoire 1 pour toute la famille, WinUAE l'opérande 2
  sur le 68040. Décision du mainteneur ou d'un vrai 68040. Même question,
  sans manuel, pour `MULx.L Dn,Dm:Dm` sur 68040.
- [ ] **Confirmer `LINK A7` sur un vrai 68030 (D42).** M68000PRM-1992 :
  A7 décrémenté ; la table 68030 de WinUAE empile A7 d'origine. [F] : un
  68030 réel, ou l'ordre du microcode.
- [ ] **Généraliser le report du pipe de préfetch.** Le fork capturait
  quatre mots après PMOVE TC/CRP/SRP seulement (`mmuCapturePipe`, absent
  du cœur de Dirk), pour un idiome de la ROM LC II ; PFLUSH, une écriture
  de TT ou une cible de saut n'avaient rien.
- [ ] **Décider le sixième bit du prédicat FBcc du 68040 sur preuve.**
  Réglé pour le 6888x (MC68881UM-1989 §4.7.3, Table 4-20, note 3) ; le
  MC68040UM-1993 ne dit rien du bit 5. *Tranche : FBcc `$F2A1` sur un
  vrai 68040.*
- [ ] **Trancher l'ordre de lecture de FMOVEM en EA indirect mémoire à
  extension complète avant un Line-F de direction** (« still open » dans
  `POM68K_VENDOR.md § MC68882 FPU execution`).

### Justesse 68040

Écrites sur le fork, comme ci-dessus : *à requalifier sur le cœur de Dirk*.
Les lignes de cache attendent la décision sur `cache040_test`.

- [ ] **Livrer la trappe de type de donnée des opclasses 000/010 en
  pré-instruction (D30).** M68040UM § 9.7 : postée, prise par l'instruction
  FPU suivante (format $0) ; le fork et l'oracle la prenaient en format $3.
  Le type non supporté (vecteur 55) est sur la liste de Dirk
  (`POM68K_FPU040_DATATYPE`).
- [ ] **Trancher FMOVE.P vers Dn (D25) et la valeur de An au moment de la
  trappe de D26.** *Bloqué : FMOVE.P FP0,D0 et FMOVE.P FP0,-(A0){D1} sur un
  vrai 68040.*
- [ ] **Mesurer la phase exacte du remplacement ATC 040 sur matériel.**
  MC68040UM-1993 § 3.3 ne fixe ni la phase de reset, ni le front
  d'incrément, ni le couplage I/D, ni la phase d'installation de PTEST ; à
  comparer au MMU040 de Dirk avec `mmu040_atc_test`. *Bloqué : traces de
  remplacement d'un vrai 68040/68LC040.*
- [ ] **Trancher la frame d'un MOVEP 040 partiellement écrit sur
  matériel.** PC, WB1/WB2/WB3 et octets répétés après RTE, à chaque
  position de faute .W/.L (`CHANGELOG` 2026-11-09, cinquième). *Bloqué :
  capture d'un vrai 68040/68LC040.*
- [ ] **Trancher les erreurs tardives de write-allocate 040.** Terminer une
  écriture dont le remplissage avorte sans exception, sans perdre les
  voisins ni la victime sale (§ 4.6.1) ; capturer une frame réelle pour le
  choix de WB3 et le PC de reprise.
- [ ] **Prouver les buffers simultanés après une faute de poussée 040.**
  WB2 d'une MOVE16 en attente et combinaisons WB2/WB3 (MC68040UM-1993,
  Table 8-6) ; invalidation sur faute de poussée déclenchée par un accès NC
  (§ 7.6.1).
- [ ] **Fermer la preuve du mode cache actif.** Aucun `sst68040` enregistré
  avec le cache, aucun boot interprète cache actif ; le débogueur lit la RAM
  sous le cache et tout écrivain hôte de RAM invitée est un maître non
  espionné. Hit verrouillé avec CACR.DE désactivé (§ 4.3.3) ; témoins
  MOVE16 sur hit propre/sale (§ 4.3.3).

### Interprète — performance du mode normal

Chaque ligne n'ouvre qu'avec un profil temporel reproductible, mesuré en
ABBA intra-binaire à empreintes identiques. Les profils du fork
(`scratchpad/2026-11-06/INTERP_PROFILE.md`,
`scratchpad/2026-11-07/INTERP_HOT040.md`) ne décrivent plus ce cœur.

- [ ] **Profiler l'interpréteur de Dirk sur les bancs à budget fixe** (LC II,
  Quadra 605, Mac II, Plus) pour ordonner la suite. Les fenêtres de fetch et
  de données que l'interpréteur du fork armait lui-même passaient par la
  couture `pomJit*`, inerte ici : leur retour est l'étape 2 de
  § Phase 2.

*Obsolète :* « Alléger la remise à zéro par instruction du mode 5 »
(`mmuExecuteStart`, fonction du fork, absente de `extern/moira/Moira`) — le
nouveau profil dira si le cœur de Dirk a un équivalent.

### Surface portable

- [ ] **Requalifier `extern/moira/POM68K_SURFACE.md`.** Il décrit ce que le
  cœur du fork exposait et ce que l'hôte lui devait ; sur `main` le cœur est
  celui de Dirk et la couche est `src/MoiraCompat.h`. Le récrire pour ce
  cœur, ou le marquer historique. Restait ouvert sur le fork : la DMA son
  côté Atari (à NeoST, § NeoST).

---

## Phase 2 — Reconstruire le JIT

Ouvre quand la phase 1 est sortie. Le code vient de `main_old`, famille par
famille ; chaque étape est admise par ses propres gates, réenregistrés
depuis `cmake/pom68k_jit_gates.tsv`. Règles d'admission
(`src/jit/POM68K_JIT.md`, `CLAUDE.md` § JIT policy) : l'interpréteur est
l'oracle ; conformité, stabilité du tier entier et performance mesurée dans
le même processus sont trois admissions indépendantes ; `guestFamilies`
(justesse) ne se déduit jamais d'`autoFamilies` (politique mesurée) ; la
couverture d'opcodes seule ne fait jamais d'un backend natif le défaut. Une
règle 68k commune vit dans l'IR ou le coût partagé, jamais dans un émetteur.

### Fonctionnel d'abord — étapes

- [ ] **1. La couture sur le cœur de Dirk.** Ce que le JIT de `main_old`
  demandait au cœur : `PomJitLayout`, `pomJitSync`, fenêtres de fetch et de
  données, ATC 040. Hook par hook, décider ce qui peut entrer chez Dirk
  (boucle amont de la phase 1) et ce qui reste dans la couche de POM68K.
- [ ] **2. Les fenêtres de l'interpréteur.** La fenêtre de fetch du 68000
  (`pomJitFetch000` sur `main_old`, gate `jit_lockstep_68000_test`) que la
  politique garde (`POM68K_JIT.md` § 2.2), puis la fenêtre de fetch des
  030/040 et la fenêtre de données du 68030 ; re-mesurer alors la fenêtre
  de données 040, laissée éteinte sur le fork après une mesure sur l'ancien
  ATC de 32 entrées.
- [ ] **3. Le backend `threaded`**, correct pour toute famille, avec ses
  locksteps et ses gates de conformité (`jit_asset_free_lockstep_test`,
  `cpu_vectors030_jit_test`, `cpu_vectors040_jit_test`, le replay des
  corpus par `sst68030_jit`/`sst68040_jit`, les locksteps de carte).
- [ ] **4. Les backends natifs `a64` puis `x64`**, chacun admis par ses
  locksteps et par la parité (`jit_backend_parity_test`) ; la jambe
  x86-64 se rejoue sur un vrai hôte x86-64, pas seulement sous Rosetta
  (`build-x64-proof`).
- [ ] **5. Rétablir la politique par défaut.** `POM68K_JIT` à `ON`,
  `auto` → `a64` sur AArch64 et `x64` sur x86-64 hors Windows pour
  68030/68040, `threaded` sur Windows x64 ; régénérer
  `cmake/pom68k_jit_gates.tsv` ; rétablir dans `ci.yml` la preuve x86-64 du
  JIT, ses métriques et leur archive, suspendues depuis le 2026-11-23.
- [ ] **6. Le JIT du mode D-cache 040**, après la décision de la phase 1
  sur le modèle de cache 68040 (`cache040_test`).

### JIT justesse et parité des hôtes

Écrites sur `main_old` ; chacune se requalifie quand son étape revient.

- [ ] **GLUE et Pb030 après le retrait du vidage CACR : les restes.**
  `se30_boot_etalon` perdait 5 % (blocs périmés gardés à travers 826
  changements de carte mémoire, sorties `WindowLost` × 3) ; aucun banc
  GLUE, PowerBook ni IIfx à empreinte.
- [ ] **LC III / IIvx : l'ATC de 22 entrées tourne sous pages de 8 Ko.** Le
  boot LC III refusait 30 M de sondes de fenêtre et 180 M de remplissages du
  TLB de données faute d'entrée ATC (`CHANGELOG` 2026-11-12 (tenth)) ;
  voir si la sonde peut répondre sans entrée (marche en lecture seule, sans
  écrire U/M) sans rompre l'identité avec l'interpréteur.
- [ ] **Tenir une table de parité datée au-delà de `canEmit`.**
  `jit_backend_parity_test` ne balaie que l'encodage. Écarts connus sur
  `main_old` : côté a64 seul, `POM68K_JIT_REG_CACHE`, `_A64_PACING`,
  `_WATCH_OPCODE`, `_040_LATE_POLL` et `jit_store_guard_a64_test` ; côté
  x64 seul, le lockstep CCR compact ; dans `caps()`, `profitScore68030` (64
  contre 0) et `maxAccessThunk030` (2 contre 1).
- [ ] **Sortir de l'IR la liste d'opcodes propre aux workloads Mac.**
  `exactRequired` est câblé dans `JitIr.h` sur cinq sites LC II et
  Speedometer : un attribut observé à la trace ou une classe d'E/S rendue
  par `dataSpan`.
- [ ] **Hisser la politique d'admission hors des deux émetteurs** (poll
  positionné, transaction JSR 040, admission des accès exacts), dernière
  surface de dérive que les invariants 9 et 10 ne couvrent pas.
- [ ] **Trancher les prototypes éteints et les knobs « chantier » échus.**
  `POM68K_JIT_PACKED_CCR` (+5,8 % sur a64), `_REG_CACHE` (neutre),
  `_EDGE_CELLS` (+0,5 %) : instrument avec gate, ou suppression.
  `_PROFIT_SCORE` est produit de fait par `caps()`, `POM68K_DATA_WINDOW`
  défaut 030 depuis le 2026-09-05 : les reclasser.
- [ ] **Le cache d'instructions 68020 et le JIT.** Le JIT ne modélise pas
  le cache : un 68020 tourne sur l'interpréteur (`POM68K_JIT.md` § 2.1) ; le
  modéliser rendrait l'accélération opt-in au Mac II et au LC.
- [ ] **`MOVE.B (A0),(A1)+` à source périphérique jamais natif sur a64**
  (`CHANGELOG` 2026-11-13) : l'émetteur du `MOVE` à deux EA refuse un coût
  tracé (11, dont 3 cycles d'attente) différent du coût statique (8) avant
  la branche du thunk exact, alors que `runExactSourceMove` l'affirme. À
  trancher : admettre l'attente tracée, ou donner au test un périphérique
  sans attente. Non vérifié : x86-64.
- [ ] **Exécuter les locksteps sur un hôte Windows** — préalable de
  « `threaded` est le plancher Windows ». *Bloqué : hôte Windows.*

### JIT performance — puis rapide, sur profil temporel

Aucune ligne n'est une lacune de conformité ; chacune n'ouvre qu'avec un
profil temporel reproductible sur le JIT reconstruit et se mesure en ABBA
intra-binaire, empreintes identiques. Ordre de rendement attendu d'après
les profils de `main_old` (`CHANGELOG` 2026-11-07 (quatorzième), profils du
2026-11-11, (sixth) du 2026-11-12).

- [ ] **Re-référencer les tables de performance des deux hôtes** sur
  l'arbre reconstruit : les empreintes de `POM68K_JIT.md` (M4 du
  2026-08-23, x86-64 du 2026-08-10) n'étaient déjà plus reproductibles.
- [ ] **Le runtime du moteur** (21 % sur le LC II, 14 % sur le Q605 dans
  les profils de `main_old`) : chaînage direct de blocs ; les sorties « cpu
  flags » et « not compilable » sont les deux autres fuites.
- [ ] **Le code généré lui-même** (22 à 52 % selon le profil) : profil par
  bloc d'abord. Restaient non mesurés : écritures de `pc`/`pc0` devant
  chaque accès mémoire, base du DTLB rematérialisée à chaque accès, `movW`
  sur une constante à moitié basse nulle. Bases épinglées dans `x27`/`x28` :
  a64 seul.
- [ ] **La MMU 030 hors fenêtre de données** (9 % du LC II) : quels accès
  ratent la fenêtre.
- [ ] **Régler dans le dispatcheur un `CHECK_IRQ` sans rien à livrer.**
  Candidat mesuré, non versé (LC II −2,0 à −2,6 %), bloqué par la ligne
  `MOVE.B (A0),(A1)+` de § Phase 2 › JIT justesse.
- [ ] **Décider late-poll comme fonction commune aux deux générateurs.**
  Conforme sur a64 mais −6,3 % sur le seul banc cache actif ; non
  implémentée sur x64. Portage x64 ou décision dans l'IR ; sinon retirer
  `POM68K_JIT_040_LATE_POLL`.
- [ ] **Amener le JIT du mode D-cache 040 dans le budget produit.**
  46,63 s cache actif contre 11,03 s sans sur `main_old`. Dépend de
  § Phase 1 › Temps (double comptage) et › Justesse 68040.
- [ ] **Ne pas rouvrir l'écart d'admission 68030 sans profil temporel
  neuf.** Seau non supporté plafonné à 1,24 % contre un plancher de 10 ‰ ;
  à re-chiffrer sur le M4. Évidence :
  `scratchpad/2026-09-05/b3probe/ADMISSION_GAP.md`.
- [ ] **Isoler ou amplifier les familles Speedometer avant toute
  promotion.** Profils dominés par le boot ; répéter une famille dans
  l'invité ou échantillonner sa phase seule. Évidence :
  `scratchpad/2026-09-06/a64-m030/SPEEDOMETER_TIME_PROFILE.md`.
- [ ] **Étudier `PFLUSHA` et le retry d'armement seulement après profil.**
  Toute réduction des bumps ou du backoff garde les locksteps 030/040 : le
  moment où une fenêtre s'arme est observable sur 68040.
- [ ] **Nommer le résidu des stores à masque nul, ou retirer la ligne.**
  Vraies collisions de masque de code (`CHANGELOG` 2026-08-21) ;
  spécialisation seulement après profil et preuves identiques.

Déjà mesurés et refusés sur `main_old`, à ne rouvrir que sur un profil qui
les redemande : fenêtre gardée armée sur les pas à drapeau, fenêtre
plafonnée à la longueur du bloc, `pomJitFetch` forcé en ligne,
`POM68K_JIT_ARM_BACKOFF` (bruit en ABBA), taille du cache de dispatch
(`CHANGELOG` 2026-11-11 à 2026-11-13).

---

## Vitesse — côté hôte POM68K, hors CPU

Indépendant du moteur CPU ; à re-profiler sur l'interpréteur de Dirk, les
parts relevées sur `main_old` (`CHANGELOG` 2026-11-07 (quatorzième),
profils du 2026-11-11) ne valent plus.

- [ ] **L'interpréteur 68HC05 (Cuda/Egret, 5 à 17 % sur `main_old`)** :
  code hôte pur, sans effet sur la trace — table de dispatch, `read8`
  inliné.
- [ ] **Ne réveiller à chaque instruction du MCU que ce que le MCU voit —
  la suite** (première moitié : `CHANGELOG` 2026-11-11 (fifth)) :
  - la même recette sur les autres MCU en LLE qui bornent la tranche à
    l'instruction : PG&E (`PgePmu`), M50753 (`Pb030Pmu`, `PortablePmu`),
    65C02 des IOP (`ApplePic`), PIC1654S des Centris — un banc à budget
    fixe d'abord ;
  - les périphériques que personne n'observe entre deux échéances (DAFB,
    lecteurs, pompe CD, ASC, CA1) avancés à l'accès : flush devant chaque
    observateur côté hôte et décision de format de save-state ;
  - les compacts ADB (SE, SE FDHD, Classic), non regroupés tant
    qu'`AdbVia` est resynchronisé dans `tick()` et que son HLE borne à 1 ;
    et la disquette en cours de lecture (IWM non indépendant du
    découpage).
- [ ] **L'audit d'échéance des Centris**, après le PIC1654S de § Phase 1 ›
  Temps.
- [ ] **`Cpu030::catchUp` et `Cpu040::catchUp` hors ligne** : petit, 0,5 %
  du profil du LC II sur `main_old`, sous le plancher de mesure.

---

## Statut

- **En tête : les deux phases de § Objectif principal** (décision du
  propriétaire, 2026-11-23/24) — la phase 1 d'abord.
- **En parallèle : jalon 2** (le produit prouvé). Jalon 1 clos le 2026-09-16
  (registre complet sur les deux jambes, version 0.2.0) ; paliers B et C
  clos les 2026-09-07/08. Census et preuves datées : `STATUS.md`,
  `CHANGELOG.md`.
- Ordre des jalons 2 → 3 → 4 → 5, décidé le 2026-09-16 : le jalon 2 rend
  chaque suivant vérifiable par quelqu'un d'autre que l'hôte qui l'a
  produit. Chaque section ouvre sur son critère de sortie ; ce qui en est
  déjà atteint est daté au `CHANGELOG`.
- Admission : une nouvelle machine part d'un gate produit réutilisable de
  sa plateforme ; un ajout LLE part d'une trace, d'un observable invité ou
  d'un consommateur réel ; une optimisation dépend d'un profil temporel
  reproductible et se mesure en ABBA intra-binaire à empreintes identiques.
- Une tâche marquée *Bloqué :* nomme la ressource qui la débloque. Une
  tâche qui attend un signal plutôt qu'une ressource vit en § En sommeil.
- Une tâche marquée *À reproduire :* vient de l'audit de lecture du
  2026-11-06 (`CHANGELOG` de ce jour) : le défaut a été lu dans le code,
  jamais exécuté. Son premier pas est le gate qui le montre ; s'il ne se
  montre pas, la ligne sort avec sa réfutation au `CHANGELOG`.
- Une tâche marquée *à requalifier sur le cœur de Dirk* a été écrite sur le
  fork : son premier pas est de vérifier ce que fait le cœur de `main`.

---

## Jalon 2 — Preuve, outillage et dettes de mesure

**Sortie :** chaque fenêtre a un gate ou une passe manuelle datée ; un run
complet publié par la CI sur un runner à assets ; version 0.3.

- [ ] **Preuves de plateforme dues sur le nouveau cœur.** Le rebasage du
  fork (2026-11-20) les avait laissées ouvertes ; elles valent pour le cœur
  de Dirk et nourrissent la sortie de la phase 1 : (1) les paliers ROM
  `m030`/`m040` et les etalons 68000/68020 sur l'hôte AArch64 ; (2) les
  neuf etalons disquette qui se sautent sur l'hôte x86-64 faute d'images
  (`plus_moof_*`, `*_oids_moof_etalon`, `moof_media_state_etalon`,
  `lcii_floppy144_etalon`, `maciifdhd_moof_boot_etalon`,
  `maciifdhd_floppy_boot_etalon`) — à rejouer avec le volume d'assets
  monté ; (3) un build Windows/MSVC (`release.yml` est le seul workflow qui
  le fasse) ; (4) le build Pi et sa passe PGO, dont les chiffres de
  `docs/RASPBERRY_PI.md` datent du fork.
- [ ] **`-Werror` + `POM68K_NATIVE=ON` ne compile pas sous GCC 15.**
  `src/AtalkStack.cpp` (`std::vector::resize` dans `handleAtp`) :
  « writing 16 bytes into a region of size between 1 and 15
  [-Werror=stringop-overflow=] », faux positif de la vectorisation sous
  `-march=native`. Les builds `-Werror` de l'arbre et de la CI ont
  `NATIVE=OFF`, le défaut produit a `NATIVE=ON` sans `-Werror` (vu le
  2026-11-07). Soit un `reserve`/`assign` qui calme l'analyse, soit un
  `-Wno-error=stringop-overflow` ciblé sur le fichier.
- [ ] **Installer un runner auto-hébergé avec les assets.** Rendre le palier
  `full` déclenchable par push et publier `LastTest.log` + le census
  exécutés/soft-skips. *Bloqué : infrastructure/hôte à provisionner.*
- [ ] **Enregistrer la chaîne Retro68 de la copie livrée de l'agent.**
  `share/POM68KDisques.toolchain` dit `unrecorded` : la copie du
  2026-09-14 précède le tampon (`agent_binary_test` ne tient plus que
  l'identité de l'application quand les chaînes diffèrent, depuis le
  2026-11-14). Vérifier depuis le M4 si sa chaîne la reproduit et y écrire
  son tampon ; sinon relivrer depuis une chaîne tamponnée
  (`share/README.md`).
- [ ] **Dater une passe manuelle de « Révéler » (fenêtre DaynaPort).** Le
  bouton lance `open` / `xdg-open` / `explorer`, qu'aucun gate ne peut
  observer. Le reste est couvert par `gui_windows_test`,
  `gui_relaunch_smoke_test`, `q605_afp_rename_etalon` et
  `q605_dayna_driver_etalon` (`CHANGELOG` 2026-09-16/17).
- [ ] **Isoler les étalons DaynaPort concurrents.** Dans `-L "m030|m040"
  -j5` du 2026-11-18, `q605_dayna_rarp_etalon` et
  `q605_dayna_rarp_network_etalon`, lancés côte à côte, sont rouges
  (« RARP automatic mode=0, requests=0 ») ; seuls, verts. La ressource
  partagée reste à nommer (sockets hôtes ? `run/` ?) avant de poser un
  `RESOURCE_LOCK` sur les quatre registrations, comme `afp_live_share`.
- [ ] **Étendre l'oracle invité différentiel aux profils restants.** Seize
  machines ont leur gate `<profil>_prober_oracle_etalon` (`CHANGELOG`
  2026-10-10 (seventh), (eighth), (tenth), 2026-10-13 (sixth)). Restent le
  LC 575 (le `maclc575` de MAME 0.287, « imperfect », s'arrête sur
  « Starting Up »)… : chacun = une branche de `tests/prober_oracle.cpp` et
  son rapport MAME.

---

## Jalon 3 — Services réseau

**Sortie :** session réelle sur bridge externe verte (faite le
2026-09-18), zéro opcode refusé sur les sessions live (fait le 2026-09-19,
Mac OS 8.1 et System 7.0), les deux mécanismes expliqués ou tranchés. Puis
livrer les contrôles produit, et n'ajouter du protocole que sur
consommateur observé (§ En sommeil).

---

## Jalon 4 — Fidélité matérielle et LLE

**Sortie :** un jeu CD avec audio joué de bout en bout (fait le
2026-09-19), la divergence LC II attribuée (faite le 2026-09-18), N profils
sous etalon pixel-accurate (les 39 le 2026-09-26, sur x86-64). Tout ajout
LLE part d'une trace ROM/pilote, d'un observable invité ou d'un consommateur
réel ; une approximation plus large sans preuve n'est pas un gain.

- [ ] **Remplacer le `Disk605.dsk` du M4 par la référence, puis rejouer
  ses six épingles rouges.** Tranché le 2026-10-02 (sixth) : `6ea0c1c7…`,
  épinglé dans `assets.lock`, vert sur x86-64 ; la copie du M4
  (`533a3e30…`) a dérivé. Source : `TEST/pom68K/disks35/ref/`. *Bloqué :
  hôte AArch64.*
- [ ] **Rejouer les épingles WASM sur l'hôte AArch64 et avec la disquette
  System 6.0.8 du Mac II FDHD.** `ctest -L wasm` (2026-10-14 (fourth)) :
  43 des 44 rejeux exécutés sur x86-64, tous égaux aux épingles natives ;
  `wasm_maciifdhd_floppy_boot_etalon` s'abstient faute de l'image ici
  (le gate natif aussi). *Bloqué : hôte AArch64.*
- [ ] **Comparer le bus et les timings V8 à du matériel réel.** IRQ, VBL,
  VIA et mémoire, puis diagnostiquer l'assombrissement après très longue
  exécution. (Le bloc `$50F18038` du Classic II est du bus ouvert, tranché
  le 2026-09-17.)
- [ ] **Synchroniser le SCC sur l'horloge VIA au Centris et au Quadra 700.**
  Fait sur Q605/Q630 (`calibration_040_etalon`), retenu ici. La souris
  morte n'est pas un défaut du PIC : c'est une course de la ROM (`@sendCmd`
  repasse ST 3→0 en 17,8 µs, sous le balayage de 23 µs du PIC), qu'Apple a
  corrigée plus tard, et que notre 040 provoque parce qu'il atteint une
  lecture PRAM 74 µs après l'autopoll contre 379 µs chez MAME (`CHANGELOG`
  2026-10-02). Le défaut sans synchro gagne la même course de 63 cycles :
  il est fragile aussi. Reste à trancher le débit réel du 68LC040 sur ce
  chemin — c'est la calibration de § Phase 1 › Temps — *Bloqué : un chiffre
  matériel (TimeDBRA d'un vrai Centris 650 ou Quadra 700, ou une mesure de
  la fenêtre ST).* Le Quadra 700 n'est pas encore instrumenté. Nouvel
  observable (2026-10-10) : la souris du Quadra 650 de POM68K n'a pas de
  routine de service ADB, de façon déterministe, là où le `macqd650` de
  MAME et le Quadra 800 de POM68K (même carte, même horloge) en ont une —
  non jugé dans `q650_prober_oracle_etalon` en attendant ; à rejouer sur
  le cœur de Dirk.
- [ ] **Display Card 8•24 : écrans monochromes, millions de couleurs,
  second écran.** Sur les écrans Portrait et Two-Page, le dernier
  chargement de CLUT du pilote laisse à zéro le canal bleu que lit le
  balayage mono (le même chez MAME) : écran noir, donc non proposés
  (`DisplayCard824::monitorSupported`). Le mode 24 bits (`$84`) est refusé
  par le pilote sans adressage 32 bits ; aucun disque de référence n'a
  MODE32. La carte remplace Toby au slot 9 ; un second écran (Toby + 8•24)
  demande une GUI multi-écrans. *Bloqué : une capture matérielle ou MAME
  d'un écran mono, un volume avec MODE32.*
- [ ] **Affiner les latences VIA sur les compacts : T1 et l'IACK.** Reste
  la latence écriture T1CH → IFR.T1 (N+1 ici, N+3 chez MAME) et l'IACK
  autovecteur calé sur l'horloge E (`vpa_sync` chez MAME). Chacune attend
  son observable invité, comme T2 et le cycle /VPA ont eu TimeDBRA/TimeSCCDB
  (`compact_timing_etalon`, `CHANGELOG` 2026-09-26).
- [ ] **Améliorer la précision sonore des compacts : la sortie PWM.** Le
  son prend l'octet comme PCM linéaire là où la carte le rend en PWM 1 bit
  dans un intégrateur. (Lecture par ligne faite le 2026-09-27, DAC hôte et
  courbe DFAC/V8 faits.)

---

## Jalon 5 — Nouvelles machines : les portables

**Sortie :** la famille PowerBook boote au Finder avec preuve au-delà du
boot, catalogue et save-state câblés. Les nouveaux contrôleurs sont prouvés
sur le premier profil consommateur avant d'être généralisés ; une ligne
catalogue se mérite par une cellule Finder **plus** le câblage GUI et
save-state.

- [ ] **Étendre les modes vidéo externes des PowerBook 030.** Qualifier
  les autres moniteurs et le miroir logiciel au-delà du RGB 13 pouces
  640×480 (`docs/PB030_BRINGUP.md`, `CHANGELOG` 2026-10-20 (second)).
- [ ] **Étendre NuBus et la vidéo sur slot.** Porter les cartes au-delà du
  Toby Mac II vers IIx/IIcx/IIci et les Quadra concernés.

---

## En sommeil — rouvrir sur déclencheur

Pas du travail à planifier : chaque ligne attend un signal nommé. Quand il
arrive, la tâche remonte dans la section de son jalon.

- [ ] **MacIP : ICMP sortant.** *Rouvrir sur* un hôte dont
  `net.ipv4.ping_group_range` couvre le gid, ou avec `CAP_NET_RAW` (les deux
  hôtes du projet sont à `1 0`, mesuré le 2026-09-18 ; un gate ne pourrait
  que se sauter). Le window scaling TCP attend de même le compteur
  `Status::tcpSynWindowScale` (`macip_gw_test`).
- [ ] **Poser les adresses multicast de zone (`09:00:07:00:00:xx`).** *Rouvrir
  sur* un consommateur EtherTalk : le routeur interne répond UseBroadcast et
  la liste `SET MULTICAST ADDRESS` est acceptée et ignorée (`DaynaPort.h`) ;
  la session bridge du 2026-09-18 ne l'a pas demandée.
- [ ] **Compléter le low tier SCC.** *Rouvrir sur* une machine qui demande
  8530/85C30/ESCC, puis WR9 VIS/NV et DPLL avec gates, en préservant le
  LLAP déjà plus complet que l'oracle MAME.
- [ ] **Étendre les commandes Cuda du Q605/LC 475.** *Rouvrir sur* une trace
  ROM/pilote : timing pin-level 040 et commandes réellement observées.
- [ ] **Revalider l'arithmétique de zones GCR.** *Rouvrir sur* un symptôme ou
  un corpus (la survie des flux hors cadence est faite,
  `SonyDriveFlux.cpp`).
- [ ] **Expliquer la réouverture de la fenêtre de volume à l'insertion.**
  *Rouvrir sur* un consommateur : un Mac réel rouvre les fenêtres qu'un
  volume avait ouvertes à son éjection (état de VOLUME, pas d'hôte).
  Première expérience : éjecter fenêtre racine ouverte, réinsérer l'image
  écrite, regarder si le Finder la rouvre (`lcii_sony_trace --img`, ligne
  « centre white » : 0,65 sans fenêtre, 0,91 avec). Évidence :
  `scratchpad/2026-09-18/floppy/`.

---

## NeoST — la copie du fork et ce qu'on lui doit

Hors du chemin principal : NeoST garde le fork de Moira dont `main_old` est
la dernière forme POM68K. Ce que NeoST doit faire chez lui (périmètre
TT/Falcon/accélérateurs, `g_cur` et le modèle CPU figé à `M68000`,
save-state versionné pour un `Registers` élargi, C++20, entitlement JIT du
runtime durci macOS) vit dans le `TODO.md` de NeoST.

### Convergence avec la copie NeoST

Le juge commun du 68000 est `sst68000` (ordre et date de chaque cycle bus).

- [ ] **Proposer à NeoST les règles 68000 que le corpus a tranchées.** Sa
  copie garde l'ordre amont que le rang 41 du fork corrige (frame de groupe
  0, RTE/RTR, JSR, CHK, MOVEM.l, écritures et lectures longues) et diffère
  sur l'attente de l'erreur d'adresse, le `SYNC(2)` de l'erreur d'adresse de
  DIVS/DIVU/CMPM/CHK, la sortie anticipée de DIVS en débordement, le
  drapeau Z de la division par zéro, ADDQ/SUBQ.l sur An, FC « data » des
  lectures relatives au PC (tranché programme, `CHANGELOG` 2026-11-21
  (ninth)). Les correctifs 19 à 21 portent trois de ces règles chez Dirk ;
  la transcription du microcode est le témoin à consulter pour chacune. Le
  harnais `tests/sst68000.cpp` se porte tel quel.
- [ ] **Trancher STOP+8 avant de le porter.** NeoST `993ce5e` puis
  `2fc53a8` (plan : `scratchpad/2026-11-06/neost_backports/`, groupe 8) :
  `stopGraceUntil`, réveil quantifié sur 4 cycles, STOP sans cycle bus. Son
  code final facture 0 cycle au STOP (une IRQ en attente serait prise à +4,
  pas +8) ; le corpus n'a aucun vecteur STOP. *À requalifier sur le cœur de
  Dirk* (sa propre boucle STOP). *Bloqué : un témoin — le microcode, ou une
  mesure.*
- [ ] **Bornes de cycle bus de NeoST.** `willAccessBus` / `didAccessBus`
  (NeoST `ce1cc52`), deux appels par accès bus. *À requalifier sur le cœur
  de Dirk* : il a un `willAccessBus` par opérande derrière
  `MOIRA_REPORT_BUS_ACCESS` (`MoiraConfig.h`), pas de `didAccessBus` ; à
  porter quand NeoST prend ce cœur, pas avant (plan :
  `scratchpad/2026-11-06/neost_backports/`, groupe 3).
- [ ] **Rafraîchir le texte NeoST de `extern/moira/NEOST_VENDOR.md`.** Notre
  copie garde le texte de NeoST du 2026-07-11 ; la sienne a doublé depuis.
  Reprendre ses sections postérieures, ou renvoyer vers elles.
- [ ] **Unifier le softfloat 80 bits avec NeoST.** *À requalifier sur le
  cœur de Dirk* : `extern/softfloat` reste lié à `moira` (`CMakeLists.txt`)
  mais aucun fichier de `extern/moira/Moira` ni de `src/` ne l'inclut plus
  (le FPU de Dirk a sa propre arithmétique) ; décider s'il sort de l'arbre,
  et ce que NeoST garde de son second portage (`src/io/SoftFloat*.hpp` chez
  lui).
- [ ] **Le PowerBook 100 et le Portable gardent la montre au Finder.** Le
  reset 68000 de NeoST `9f20046` est porté et la carte Portable ne dépend
  plus du découpage (`CHANGELOG` 2026-11-11 (thirteenth), 2026-11-12
  (fourteenth), (fifteenth), (seventh)). Reste à comprendre pourquoi le
  Finder laisse la montre sur les deux machines (`InitCursor` puis
  `SetCursor` depuis `$7ABEFA`) — *à requalifier sur le cœur de Dirk* (le
  pointeur du Portable a été ré-épinglé le 2026-11-24). À ne pas reprendre :
  `71b92cc` (retard d'échantillonnage IPL, non tranché côté NeoST).
La question du 68010 interrompu avec une pile impaire vaut aussi pour
NeoST : elle est en § Phase 1 › Socle.

*Obsolètes avec la bascule sur le cœur de Dirk* (le fork n'est plus sur
`main`, il reste sur `main_old`) :
- « Unifier les chemins de fetch du 68000 » (`read<>`, `prefetch`,
  `fullPrefetch`, `pomJitFetch000`) : `pomJitFetch000` n'existe plus dans
  `extern/moira/Moira` ; la fenêtre de fetch 68000 revient en § Phase 2,
  étape 2, et devra alors respecter la même contrainte (pas d'arbitrage de
  bus par cycle).
- « Porter la fenêtre de fetch 68000 vers NeoST, ou la laisser » : même
  raison ; à reconsidérer quand l'étape 2 l'aura reconstruite.
- « Projet Xcode de l'amont » (fichiers du fork `MoiraState.h`,
  `MoiraCache040.h`, `MoiraMmu030Walk.h` absents de `extern/moira/Moira`)
  et « Tenir le Runner de l'amont vert » (il jugeait le fork déposé dans
  l'arbre de Dirk ; `moira_release_test` est retiré) : remplacés par la
  boucle amont de § Phase 1, où chaque PR porte son test `Tests/Unit`.
- « Preuves dues à l'état rebasé » : le rebasage était celui du fork ; ses
  preuves de plateforme encore utiles sont en § Preuve.

Relevé NeoST du 2026-11-13 (NeoST `9db090b`) : rien de nouveau à porter ;
`cc554ef` sans objet ici, `e809c24` et l'espace des lectures relatives au PC
tranchés (`CHANGELOG` 2026-11-21 (ninth)) ; `ea81c29` corrige l'enveloppe
de NeoST, pas Moira — si une enveloppe 68000 gagne un jour un saut d'horloge
sur un CPU arrêté, il devra avancer d'un nombre pair de cycles.

---

## Dépôt — purge GitHub

- [ ] **Faire purger par GitHub les objets inaccessibles.** Le journal
  disque `lc580_ide_boot.img.pomundo`, poussé par erreur le 2026-11-11, est
  retiré de l'historique de `main` (`CHANGELOG` 2026-11-21 (tenth)).
  **Reste au propriétaire** : demander au support GitHub de purger les
  objets désormais inaccessibles — les anciens commits `f9a29795` et
  `c5dc026c` restent servis par SHA tant que GitHub ne les a pas collectés —
  et réaligner les autres clones (`git fetch && git reset --hard
  origin/main`).

---

## Hors jalon

### Mesure sur matériel

- [ ] **Établir la ligne de base POM68K sur un vrai Pi 400**, puis **rejouer
  l'A/B release native/PGO/LTO** (`jit_bench`/`jit_bench_lcii`, budget
  invité fixe, empreintes archivées, `-mtune`/LTO/PGO séparés). Le paquet
  Cortex-A76 est archivé depuis le 2026-09-07. *Bloqué : la carte
  physique.*

### Recherche conditionnelle

- [ ] **Définir puis expérimenter le profil d'accélération non conforme.**
  Critères fonctionnels, défauts par famille, `purity mode` des gates, et
  un opt-in qui ne puisse jamais contaminer l'oracle. Cette voie reste
  derrière la voie conforme (`docs/HLE_OVERLAY.md`, 2026-08-09).
