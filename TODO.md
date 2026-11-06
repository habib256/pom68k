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
manque (`§ Moteur`, `§ Preuve`, `§ Services`, `§ Fidélité`,
`§ Nouvelles machines`, `§ En sommeil`, `§ Recherche`). Une sous-section du
programme moteur se cite `§ Moteur › Socle`, `› Convergence`,
`› Justesse 68030`, `› Justesse 68040`, `› Temps`, `› Interprète`,
`› JIT justesse`, `› JIT performance`, `› Surface`. Les numéros bougent à
chaque réorganisation.

## Statut

- **En tête : le programme § Moteur** (priorité décidée le 2026-11-06).
  Stabiliser, prouver et accélérer le fork Moira et ses additions
  68030/68040, en interpréteur et en JIT, jusqu'à ce que le cœur puisse
  être réimplémenté dans NeoST sans emporter un Macintosh avec lui.
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

---

## Moteur — Moira 68030/68040, interpréteur et JIT, en vue de NeoST

**Sortie :** (1) chaque groupe de patches 030/040 de
`extern/moira/POM68K_VENDOR.md` a un gate qui ne lie aucun objet Macintosh,
et ses corpus sont épinglés et récupérables ; (2) les deux copies de Moira
ne diffèrent plus sur le 68000 que par des décisions enregistrées ;
(3) l'interpréteur seul garde ses chemins rapides sans `jit::Engine` ;
(4) les deux générateurs natifs refusent les mêmes formes, ou l'écart tient
dans une table datée ; (5) la surface d'extension est décrite sans
Macintosh.

L'interpréteur reste l'oracle : une ligne de justesse passe avant une ligne
de vitesse du même chemin, et un gain de vitesse se prouve à empreintes
identiques. Conformité, stabilité du tier entier et performance mesurée
dans le même processus — aucune ne se déduit d'une autre.

Ordre : Socle → Convergence → Justesse → Temps → Interprète → JIT →
Surface. Qui gagne un arbitrage est écrit (`CLAUDE.md § Evidence grades`,
`oracle/README.md`) : le manuel sur l'oracle, sauf exception connue. Les
cellules à la main des règles qu'aucun corpus n'atteint s'ajoutent à
`moira_rules_test` (`cmake/Pom68kCpuGates.cmake`) ; l'ordre et la date de
chaque cycle bus du 68000 sont tenus par `sst68000`.

Hors de ce fichier : ce que NeoST doit faire chez lui (périmètre
TT/Falcon/accélérateurs, aujourd'hui hors périmètre dans ses propres
documents ; `g_cur` et le modèle CPU figé à `M68000` ; save-state versionné
pour un `Registers` élargi ; C++20 ; entitlement JIT du runtime durci
macOS) vit dans le `TODO.md` de NeoST.

### Socle de preuve CPU

Un cœur ne déménage qu'avec sa preuve. Aujourd'hui les gates sans
Macintosh existent (`sst68000`/`030`/`040`, `fpu_sanity`, `fpu040_test`,
`cache040_test`, `berr030_test`, `callm_rtm_test`, `moira_rules_test`,
`jit_asset_free_lockstep_test`) mais leurs corpus ne sont ni épinglés ni
exécutés en CI, et plusieurs rangs de l'inventaire ne tiennent que par un
boot.

- [ ] **Rejuger les décisions prises contre un manuel, là où aucun gate
  ne tombe.** La politique est alignée depuis le 2026-11-06 : un manuel
  (grade B) l'emporte sur l'oracle (grade D) pour toute décision nouvelle,
  et une décision existante n'est renversée que si rien ne passe au rouge.
  Trois sont donc gardées comme exceptions connues
  (`oracle/README.md § Rulings taken against a manual`) : D12 (adresse
  d'instruction des frames $2), D23a (frame FPU $3 sur 68030) et D23b
  (FRESTORE $41 sur un 68882) — plus deux règles 68000 (sortie anticipée
  de DIVS, ASR au-delà de la largeur). Chacune est tenue par des vecteurs
  épinglés : la renverser demande soit une preuve de grade A, soit un
  mécanisme de divergences assumées dans les harnais `sst` (comme les deux
  vecteurs exclus de `sst68000`). Reste aussi à passer au crible les
  règles 6888x que l'audit soupçonne sans que les notes disent ce qu'en
  dit le manuel : FPIAR, vecteur d'exception gardé armé, longs internes de
  FSAVE IDLE, bit 9 du SSW, PTEST niveau 0, PMOVE TC invalide.
- [ ] **Rendre les corpus 030 et 040 récupérables, avec leur recette.**
  Leur identité est épinglée (`tests/sst_corpora.lock`,
  `sst_corpora_lock_test`) et le corpus 68000 se retélécharge à son commit
  amont ; mais les corpus 68030 (116 Mo) et 68040 (315 Mo), générés ici,
  n'existent que sur deux hôtes et le disque privé. Les publier (asset de
  release, sortie WinUAE sans donnée Apple) et enregistrer les lignes de
  commande `fuzz030.py`/`fuzz040.py` qui les régénèrent — les graines sont
  dans les noms des vecteurs, pas les tailles ni les familles.
- [ ] **Exécuter un corpus de vecteurs en CI, et sous sanitizers.** Les
  corpus 030/040 sont de la sortie WinUAE, pas des données Apple : un
  sous-ensemble public épinglé peut devenir `asset-none`. Aujourd'hui aucun
  job ne rejoue un vecteur, et le million de vecteurs n'a jamais tourné
  sous ASan/UBSan (l'UB du rang 36 a été trouvé dans un miroir de test, pas
  dans Moira). Dépend de la ligne précédente.
- [ ] **Enregistrer les fumées de l'oracle et la recette de fuzz.**
  `oracle_uae_smoke` et `oracle_uae_smoke040` vivent dans le projet CMake
  séparé `oracle/uae`, hors registre et hors CI ; le 040 n'a pas
  d'équivalent de `loop.sh`.
- [ ] **Régénérer le corpus 030 en WinUAE seul.** Les cellules core, mmu et
  random datent du 2026-07-15 et ne gardent que les vecteurs où Musashi
  était d'accord (`oracle/fuzz/disputes/NOTES.md`) : les cas disputés en
  sont exclus par construction. Manquent aussi `mmu_tt` et `random_tt`.
- [ ] **Élargir la géométrie MMU du fuzzer 030.** `gen030.py` n'a qu'un TC
  (pages de 32 Ko, un niveau, descripteurs courts, CRP sans limite) : ni
  FCL/SRE, ni tables à plusieurs niveaux, ni terminaison anticipée, ni
  descripteurs indirects ou longs, ni violation de limite, et la cellule
  `tt` ne parcourt jamais l'arbre. Les géométries réelles ne sont prouvées
  que par les boots. `oracle/README.md` annonce plus que le code ne fait.
- [ ] **Ajouter au fuzzer 040 les pages de 8 Ko (TC.P = 1) et des TTR
  variés.** Cellule écrite mais jamais exercée (`POM68K_VENDOR.md § Model
  support in this copy`) ; `gen040.py` fige `TC040_FUZZ` et une seule
  disposition de TTR. Les coutures JIT supposent 4 Ko par endroits
  (`kSliceShift`).
- [ ] **Donner un corpus différentiel au FPU intégré du 68040.**
  `sst68040` force `FPUModel::NONE` et `fpu040_test` est écrit à la main ;
  `oracle/uae/glue.c` accepte déjà `fpu_model 68040`. Corriger la colonne
  Gate des rangs 27 et 28, qui nomment `sst68040` alors que ce corpus ne
  voit ni le FPU ni les caches (`cacr` vaut 0 dans tous les vecteurs).
- [ ] **Couvrir le 68020 nu par l'oracle.** Aucun corpus 020 ;
  `callm_rtm_test`, calculé à la main, est le seul test qui pose
  `Model::M68020`. Le chemin à file de préfetch du 020 ne repose que sur
  les boots Mac II et LC.
- [ ] **Donner un gate CPU aux rangs qui n'ont qu'un boot.** Rang 11 (pipe
  de préfetch + poll IPL en mode 5), rang 13 (`irqDelay`), rang 21 (/BERR
  du 68020), moitié 010/020 du rang 15, rang 20 (watchpoints sous MMU, sans
  gate), et le rang 37 sur 68040 (`v8_ramsize` ne joue que le 020 et le
  030).
- [ ] **Rejouer les corpus à travers le JIT.** Les harnais `sst` ne
  pilotent que l'interpréteur et aucun gate JIT ne se compare à WinUAE.
  Sans Macintosh il n'existe qu'un programme 040 synthétique, les gates
  copyback et `jit_restart_write_030_test` ; tous les `jit_lockstep_*`
  exigent une ROM.
- [ ] **Obtenir une preuve indépendante du 6888x.** La convergence
  numérique est « par construction » (même famille softfloat que l'oracle)
  et l'interruption en cours d'instruction (format $9, BUSY) ne tient que
  par un scénario FSQRT de `fpu_sanity`, fenêtre que WinUAE n'a pas.
  *Bloqué : vecteurs d'un vrai 68881/68882, ou une seconde référence
  numérique.*

### Convergence avec la copie NeoST

Notre copie part de NeoST `47d6c39` (2026-07-11). Des neuf groupes que
NeoST a ajoutés depuis à son 68000, sept sont portés (rangs 38, 40 et 42).
Les deux forks avaient aussi réécrit les mêmes lignes amont dans des sens
opposés — nous sur SingleStepTests, NeoST sur Hatari et la transcription
du microcode. Depuis le 2026-11-06 `sst68000` juge aussi l'ordre et la
date de chaque cycle bus (rang 41), ce qui donne un juge commun : un cœur
fusionné doit le garder à 1 000 058 / 1 000 058.

- [ ] **Décider des bornes de cycle bus de NeoST.** `willAccessBus` /
  `didAccessBus` (NeoST `ce1cc52`) est le dernier hook non porté : deux
  appels virtuels par accès bus, donc derrière un commutateur de
  compilation éteint ici, et à dupliquer dans `pomJitFetch000`, qui
  contourne `read<>`. Plan prêt : `scratchpad/2026-11-06/neost_backports/`,
  groupe 3. À porter quand NeoST prend ce cœur, pas avant : rien ici ne le
  consomme.
- [ ] **Trancher STOP+8 avant de le porter.** NeoST `993ce5e` puis
  `2fc53a8` (plan : `scratchpad/2026-11-06/neost_backports/`, groupe 8) : `stopGraceUntil`, réveil quantifié sur 4 cycles, STOP sans
  cycle bus. Trois obstacles lus : son code final facture 0 cycle au STOP
  (une IRQ en attente serait prise à +4, pas +8, contrairement à sa
  note) ; le réveil arrondi toucherait aussi nos STOP 020/030/040 s'il
  n'est pas gardé ; il partage le bloc `CHECK_IRQ` d'`execute()` avec
  `irqDelay` et demande un champ de save-state. Le corpus n'a aucun
  vecteur STOP. *Bloqué : un témoin — le microcode, ou une mesure.*
- [ ] **Proposer à NeoST les règles 68000 que le corpus a tranchées.**
  Sa copie garde l'ordre amont que le rang 41 corrige (frame de groupe 0,
  RTE/RTR, JSR, CHK, MOVEM.l, écritures et lectures longues) et diffère
  encore sur ce que `sst68000` tient ici au cycle près : attente de
  l'erreur d'adresse (2 ou 4 cycles avant la frame, 8 chez lui),
  `SYNC(2)` de l'erreur d'adresse de DIVS/DIVU/CMPM/CHK, sortie anticipée
  de DIVS en débordement, drapeau Z de la division par zéro, ADDQ/SUBQ.l
  sur An à 6 cycles (notre table `CYCLES_AN` dit encore 8), FC « data »
  des lectures relatives au PC. SingleStepTests et Hatari sont tous deux
  de grade D : la transcription du microcode, qui a donné raison au
  corpus sur les trois groupes du rang 40, est le témoin à consulter pour
  chacune. Le harnais `tests/sst68000.cpp` ne lie que Moira et se porte
  tel quel.
- [ ] **Unifier les chemins de fetch du 68000.** `read<>`, `prefetch`,
  `fullPrefetch` et `pomJitFetch000` : ce dernier réimplémente la séquence
  de `SYNC` sans hook par accès, donc la fenêtre et un arbitrage de bus par
  cycle (blitter, Shifter) s'excluraient.
- [ ] **Rafraîchir `extern/moira/NEOST_VENDOR.md` et noter le pin amont.**
  Notre copie fait 119 lignes contre 315 chez NeoST ; le pin amont retrouvé
  (`1efd6946…`) et les deux points de divergence rendent mécanique un diff
  à trois voies.

### Justesse 68030, 68020 et 6888x

- [ ] **Sortir la frame FPU $3 du 68030 de l'incohérence de l'oracle.**
  `execFpuException` pousse un format $3 sur le 020/030 (décision D23a) et
  le RTE ne l'accepte qu'à partir du 68EC040 : un handler qui fait RTE
  prend une erreur de format. Ce n'est pas un écart du fork — WinUAE fait
  exactement les deux (`oracle/uae/upstream/gencpu.c`, `i_RTE`, branche
  `cpu_level >= 4`) : l'oracle empile sur 68030 une frame que son propre
  RTE refuse. Le $2 du manuel, lui, est accepté par le RTE dès le 68020.
  Revenir à $2 fait tomber dix vecteurs épinglés de `sst68030` : à faire
  avec le mécanisme de divergences assumées de › Socle, ou sur *une frame
  empilée par un vrai 68030*.
- [ ] **Sérialiser l'état en vol, ou refuser de sauver quand il est
  vivant.** *À reproduire.* `MoiraSnapshot::visitCpuCommon` omet
  l'opération 6888x suspendue (`fpu.busy` et ses registres), le
  micro-état FSAVE UNIMP/BUSY du FPU 68040 (`fpu.cmdreg1b` … `wbt`), les
  mots de pipe capturés après un PMOVE (`mmuPipe*`), le verrou armé par un
  RTE à DF effacé (`mmuRteSubst*`) et le verrou de reprise MOVEM du 68040
  (`mmu040MovemArmed`, `mmu040MovemEa`) — table complète dans
  `POM68K_SURFACE.md` § 8. Les gates de déterminisme ne le voient que si
  une sauvegarde tombe dans une de ces fenêtres : en construire une par
  fenêtre.
- [ ] **Porter le verrou « accès déjà rejoué » du RTE $B au 68020 nu.**
  *À reproduire.* Il n'est consommé que dans `mmuRead`/`mmuWrite` (chemin
  030) ; un handler Mac II ou LC qui efface DF réexécuterait l'accès — le
  mode de panne de la tempête d'origine. Sur le cœur nu, `extBusError`
  empile aussi $B sans fixups ni restauration du CCR.
- [ ] **Modéliser les fautes de préfetch différées, 030 et 040.** Limite
  documentée : `irc` est lu à pc+2 dès le début de l'instruction, donc une
  instruction d'un mot dans le dernier mot d'une page peut fauter là où le
  processeur réel attend que le mot serve.
- [ ] **Remplacer ou prouver le modèle « réexécuter l'instruction » du RTE
  $B.** Les accès antérieurs à la faute sont réémis (lecture de
  périphérique en double), le RMW verrouillé est exclu, et le verrou repose
  sur le bit 9 du SSW, marqueur interne de WinUAE qu'aucun 68030 n'écrit.
  Preuve actuelle : `berr030_test` et un boot.
- [ ] **Généraliser le report du pipe de préfetch.** `mmuCapturePipe`
  capture quatre mots après PMOVE TC/CRP/SRP seulement, pour un idiome de
  la ROM LC II ; PFLUSH, une écriture de TT ou une cible de saut n'ont rien.
- [ ] **Trancher l'ordre de lecture de FMOVEM en EA indirect mémoire à
  extension complète avant un Line-F de direction.** Laissé « still open »
  dans `POM68K_VENDOR.md § MC68882 FPU execution` ; le correctif du
  2026-08-12 ne couvre que le cas préindexé valide.

### Justesse 68040

- [ ] **Donner à l'ATC du 68040 sa géométrie et son tag.** Trente-deux
  entrées associatives sans bit S, là où l'oracle (et le manuel) ont 4
  voies × 16 jeux taggés par S. Avec URP ≠ SRP un accès superviseur peut
  toucher une traduction utilisateur ; PFLUSH (An) ignore DFC ; PTEST ne
  purge ni ne remplit l'entrée. L'ordre de remplacement est visible de
  l'invité (bits U réécrits) et diverge sous la fenêtre de fetch par
  décision (J3b). `sst68040` n'y voit rien : l'oracle purge son ATC à
  chaque vecteur — il faut une cellule à plusieurs instructions.
- [ ] **Arbitrer l'opérande de CINV/CPUSH ligne et page.**
  `pomCache040Phys` traduit An par le MMU de données ; le manuel de
  programmation et WinUAE le prennent comme adresse physique. Sans effet
  tant que logique = physique.
- [ ] **Vider ou sérialiser les lignes copyback sales à la sauvegarde.**
  *À reproduire.* Sous `POM68K_040_DCACHE=1` une ligne sale porte des
  données absentes de la RAM ; la sauvegarde ne les pousse pas et le
  chargement invalide les deux caches. La règle « caches vidés, jamais
  sérialisés » date du modèle à tags seuls.
- [ ] **Corriger l'estampille d'accès d'un /BERR pendant un remplissage,
  une réécriture ou un MOVE16.** *À reproduire.* `pomCache040Fill` et
  `pomCache040Writeback` écrasent l'accès en vol par des adresses
  physiques de battement ; MOVE16 hérite de l'estampille du fetch
  précédent.
- [ ] **Fermer la preuve du mode cache actif.** Aucun `sst68040`
  enregistré avec le cache, aucun boot interprète cache actif, tous les
  etalons ROM sans cache ; le débogueur lit la RAM sous le cache et
  `pomSnoop040Read/Write` n'ont aucun appelant, donc tout écrivain hôte de
  RAM invitée est un maître non espionné.
- [ ] **Décider l'hybride 68LC040 + 68882 logiciel.** Défaut produit des
  LC 475/575/580 et Centris 610/650, combinaison qui n'existe pas en
  matériel ; un vrai `NONE` boote déjà le Quadra 605
  (`q605_barefpu_boot_etalon`). Le commentaire de `Cpu040.cpp` qui dit le
  contraire est périmé.

### Temps — le modèle de débit 030/040

Le 030 et le 040 tournent sur les tables de cycles du 68020, multipliées
par `cacheBoost` ; le reste est correction locale.

- [ ] **Calibrer le débit CPU des 030/040 (`cacheBoost` 4).** Sur le
  LC II, TimeVIADB `$030F` contre `$0187` chez MAME, TimeDBRA `$28C9`
  contre `$0F4A` : le corps de boucle tient dans un cycle E là où MAME en
  prend deux (`CHANGELOG` 2026-10-02 (late night)). Même cause que la
  course ADB du Centris (§ Fidélité) et que l'ordre d'allocation du tas
  système. Le modèle n'est pas uniforme : IIx, IIcx, SE/30 (`Cpu020`) et
  IIfx n'ont ni boost ni overlay. Réglage global (épingles, perfs, JIT) :
  *Bloqué : un chiffre matériel (TimeDBRA/TimeVIADB d'un vrai LC II,
  Centris ou Quadra).*
- [ ] **Sortir le coprocesseur externe et le temps de bus de données du
  boost.** *À reproduire.* Les cycles du 6888x passent par `sync` puis sont
  divisés par le boost dans `flushTicks` : le 68882, puce distincte,
  irait quatre fois trop vite en temps machine. `DEV.md` § 1.2 pose déjà
  la règle pour un cycle VIA.
- [ ] **Donner au 68040 ses propres coûts.** Le FPU intégré est facturé
  aux tables du MC68882 (FADD 56 cycles, FSQRT 110), les marches de tables
  ne coûtent rien, CINV/CPUSH/MOVE16 sont un forfait.
- [ ] **Vérifier le double comptage du temps en mode cache actif.**
  *À reproduire.* Remplissage (8) et push (2 par long) s'ajoutent à des
  tables 020 qui facturent déjà chaque accès, puis sont multipliés par le
  boost ; `setPomCache040Timing` n'a aucun appelant hors tests. Le boot
  n'atteint pas le Finder dans le budget calibré de cinq milliards de
  cycles *invités* — ce n'est donc pas qu'une question de vitesse hôte.
- [ ] **Requalifier `irqDelay` = 2.** Profondeur empirique, réglée sur
  SimCity 2000 avec le boost et appliquée à tout modèle ≥ 68020, 68040
  compris ; le symptôme d'origine a reçu ensuite une autre cause racine
  (Egret, `CHANGELOG` 2026-07-17). Garder, retirer ou remplacer par le
  modèle de latence IPL de NeoST — après la calibration, ou sur trace
  matérielle.
- [ ] **Retirer ou câbler l'overlay i-cache mort du 68040.**
  `pomIcacheFetch` n'est appelé que sur les chemins 030 et exige un bit de
  CACR que le masque 040 ne peut pas poser : `armIcacheOverlay`,
  `POM68K_Q605_ICACHE_MISS` (classé produit dans `config_knobs.tsv`) et le
  test de `didChangeCACR` des quatre wrappers 040 sont sans effet, alors
  que `DEV.md`, `docs/CACHE_040.md` et `Cpu040.h` le décrivent vivant.
- [ ] **Affiner le temps du 6888x.** Le 68881 du Mac II est facturé aux
  tables du 68882 ; la concurrence du 68882 n'est pas modélisée
  (`fpuRunBusy` bloque le processeur par quanta de 8 cycles) ; les cycles
  030/040 ne sont jamais comparés à l'oracle.
- [ ] **Rapprocher `PomIcache` du MC68030UM, sur observable seulement.**
  Tags sans contenu, tag sur S plutôt que sur le FC, FI et IBE ignorés,
  bit CI de l'ATC posé mais jamais lu, pénalité plate.

### Interprète — performance du mode normal

Chaque ligne n'ouvre qu'avec un profil temporel reproductible.

- [ ] **Donner à l'interpréteur son chemin de fetch rapide sans
  `jit::Engine`.** `mmuFetchWord` + `Cpu030::read16` pèsent 44,4 % des
  instructions du LC II interprété, et le Quadra 605 interprété tient
  ×0,96 du temps réel contre ×1,94 pour le LC II. Or le fetch fusionné
  `ird`/`irc` (rang 33), la fenêtre de fetch et la fenêtre de données 030
  (rang 34) ne s'arment que par le moteur : `Engine::setEnabled(false)`
  vide la fenêtre, donc `POM68K_CPU_ENGINE=interp` — et tout portage de
  l'interpréteur seul — les perd. Sur 040 chaque instruction paie deux
  `mmu040Read<Word>` complets.
- [ ] **Re-mesurer avant de compacter `mmu040InstrStart`.** Les 3,26 %
  venaient d'un profil périmé par le cache de dispatch ; le seau MMU/cache
  est à 7,1 % de la phase de jeu. `mmu040Translate` pèse 38,6 % du temps
  interprète et un indice O(1) d'ATC a été mesuré plat : le coût est dans
  la marche et la tenue par accès.
- [ ] **Alléger la remise à zéro par instruction du mode 5.**
  `mmuExecuteStart` efface le journal d'accès et les fixups et sauve le
  CCR à chaque instruction 030, sous un `try`, avec `sync()` et les
  lectures en virtuel et un `dynamic_cast` dans `processException`. Aucun
  profil n'en attribue le coût : mesurer d'abord.
- [ ] **Re-mesurer la fenêtre de données 040 après la géométrie de
  l'ATC.** Éteinte parce que le TLB plafonne aux 32 pages de l'ATC
  (≈ 10 % plus lent, pire sous mémoire virtuelle) ; le passage à 64
  entrées (› Justesse 68040) change la donnée.

### JIT justesse et parité des hôtes

- [ ] **Expliquer pourquoi le moment d'armement d'une fenêtre change
  l'état invité sous le D-cache 040.** Un backoff croissant a fait
  diverger les deux locksteps cache actif au premier pas grossier
  (`CHANGELOG` 2026-08-23 (fourth)) et a été retiré : le vert actuel tient
  à la constante 32, pas à un invariant.
- [ ] **Donner des gates natifs et un inventaire de stores aux plateformes
  030 récentes.** `Pb030Cpu` (neuf PowerBook) reçoit du code natif par
  `auto` sans aucun gate à `POM68K_JIT_REQUIRE_NATIVE` ; le
  `jit_duo230_boot_etalon` promis par `docs/JIT_BRINGUP.md` n'existe pas ;
  `kJitStoreInventoryComplete` manque sur Pb030, IIfx et `Cpu020`, qui
  vident le JIT à chaque écriture de CACR (chiffré −21,8 % ailleurs).
- [ ] **Tenir une table de parité datée au-delà de `canEmit`.**
  `jit_backend_parity_test` ne balaie que l'encodage ; les refus décidés à
  l'émission ne sont pas comparés. Écarts connus : côté a64 seul,
  `POM68K_JIT_REG_CACHE`, `_A64_PACING`, `_WATCH_OPCODE`, `_040_LATE_POLL`
  et `jit_store_guard_a64_test` ; côté x64 seul, le lockstep CCR compact ;
  dans `caps()`, `profitScore68030` (64 contre 0) et `maxAccessThunk030`
  (2 contre 1).
- [ ] **Sortir de l'IR la liste d'opcodes propre aux workloads Mac.**
  `exactRequired` est câblé dans `JitIr.h` sur cinq sites LC II et
  Speedometer ; toute autre attente de périphérique repose sur le refus
  par croisement de coût. Le remplacer par un attribut observé à la trace
  ou une classe d'E/S rendue par `dataSpan`.
- [ ] **Hisser la politique d'admission hors des deux émetteurs.** Deux
  fichiers de plus de cinq mille lignes portent chacun leur copie de la
  décision de poll positionné, de la transaction JSR 040 et de
  l'admission des accès exacts : c'est la dernière surface de dérive que
  les invariants 9 et 10 ne couvrent pas.
- [ ] **Trancher les prototypes éteints et les knobs « chantier »
  échus.** `POM68K_JIT_PACKED_CCR` (+5,8 % plus lent sur a64, garde un
  lockstep permanent), `_REG_CACHE` (neutre), `_EDGE_CELLS` (+0,5 %) :
  instrument d'attribution avec gate, ou suppression. `_PROFIT_SCORE` est
  produit de fait par `caps()`, et `POM68K_DATA_WINDOW` est défaut 030
  depuis le 2026-09-05 : les reclasser.

### JIT performance — études conditionnées à un profil temporel

Aucune ligne n'est une lacune de conformité ; chacune n'ouvre qu'avec un
profil temporel reproductible et se mesure en ABBA intra-binaire,
empreintes identiques. Une règle 68k commune vit dans l'IR ou le coût
partagé, jamais dans un émetteur.

- [ ] **Re-référencer les tables de performance des deux hôtes sur
  l'arbre courant.** Les empreintes de référence de `POM68K_JIT.md`
  (M4 du 2026-08-23, x86-64 du 2026-08-10) ne sont plus reproductibles
  depuis les changements du workload Q605 (synchro SCC, lecteur B,
  identité de carte). Le delta a64 de +6 % jamais attribué ne peut plus
  l'être contre l'ancienne référence. *La jambe a64 : Bloqué : hôte
  AArch64.*
- [ ] **Décider late-poll comme fonction commune aux deux générateurs.**
  Prouvée conforme sur a64 mais −6,3 % sur le seul banc cache actif ;
  inerte sur x64 parce que non implémentée. La promouvoir veut un portage
  x64 ou une décision hissée dans l'IR ; sinon retirer
  `POM68K_JIT_040_LATE_POLL`. Rouvrir sur un workload cache actif qui
  montre le gain.
- [ ] **Amener le JIT du mode D-cache 040 dans le budget produit.**
  46,63 s cache actif contre 11,03 s sans, chemins natifs de ligne
  compris. Restent exacts par décision : le JSR 040 à indirection
  complète, les instructions mémoire multi-poll, et le rejeu de toute
  l'instruction sur un défaut de ligne. Dépend de › Temps (double
  comptage) et de › Justesse 68040.
- [ ] **Ne pas rouvrir l'écart d'admission 68030 sans profil temporel
  neuf.** Refusé le 2026-09-06 : le seau non supporté plafonne à 1,24 %
  contre un plancher de 10 ‰, et les deux générateurs refusent le même
  ensemble. Reste à re-chiffrer ce plafond sur le M4. Évidence :
  `scratchpad/2026-09-05/b3probe/ADMISSION_GAP.md`.
- [ ] **Isoler ou amplifier les familles Speedometer avant toute
  promotion.** Profils dominés par le boot (0,274–0,277 s de CPU utile) ;
  candidat QuickDraw retiré (+1,75 %), FPU fermé le 2026-09-07. Avant de
  rouvrir un lowering, répéter une famille dans l'invité ou échantillonner
  sa phase seule. Évidence :
  `scratchpad/2026-09-06/a64-m030/SPEEDOMETER_TIME_PROFILE.md`.
- [ ] **Étudier `PFLUSHA` et le retry d'armement seulement après
  profil — et après l'explication de › JIT justesse.** Toute réduction
  des bumps ou du backoff doit garder les locksteps 030/040 : le moment où
  une fenêtre s'arme est observable sur 68040.
- [ ] **Nommer le résidu des stores à masque nul, ou retirer la ligne.**
  La garde a64 est exacte depuis le 2026-08-20 ; ce qui reste au
  `CHANGELOG` du 2026-08-21 est de vraies collisions de masque de code.
  Spécialisation conforme seulement après un profil temporel et des
  preuves empreinte/compteurs/gates identiques.

### Surface portable

Ce que le cœur expose doit pouvoir se lire sans connaître un Macintosh.
Le regroupement des hooks derrière une interface virtuelle reste refusé
(≈ 11 % mesurés, `POM68K_VENDOR.md § Status`) : la séparation se fait à la
compilation et dans la documentation.

- [ ] **Retirer les trois lectures de `NEOST_EXC_DIAG` du cœur.** Dernier
  `getenv` de `extern/moira/Moira/` (`Moira.cpp`, `MoiraExceptions_cpp.h`,
  `MoiraDataflow_cpp.h`) depuis que le commutateur de cache 68040 et son
  histogramme sont injectés (rang 39). Ce diagnostic de NeoST concerne
  tous les cœurs, 68000 compris, or `Cpu68k`, `PortableCpu` et `IIfxCpu`
  ne reçoivent qu'une configuration JIT : il faut lui choisir un porteur
  (un drapeau membre et son setter, déclaré avec `RTCDBG` et `SCCDBG`
  dans `StartupOptions.h`) sans élargir quatorze constructeurs pour un
  traceur.
- [ ] **Lever les points « unverified » du contrat de surface.**
  `extern/moira/POM68K_SURFACE.md` décrit ce que le cœur expose et ce que
  l'hôte lui doit ; neuf affirmations y sont lues dans le code sans gate :
  ce que rapporte un `extBusError` levé pendant la lecture d'un
  descripteur de table 030, `setFPUModel` sur 68000/68010, la valeur à
  rendre sur une lecture CIR FC = 7 d'un FPU présent, l'audit complet de
  l'état global de softfloat, les chemins 68EC030/68EC040, l'identité
  d'horloge d'un restore sous i-cache 030 armé, et l'appel de
  `Engine::flushAll()` à la restauration. Chacune devient un gate ou une
  phrase corrigée.
- [ ] **Déplacer la sérialisation du CPU dans un visiteur côté Moira,
  avec un aller-retour sur Moira nu.** Le visiteur vit dans
  `src/MoiraSnapshot.h` et n'est prouvé que par des bancs Macintosh ;
  NeoST copie `reg` et `queue` en bloc et avalerait sans le voir un
  `Registers` élargi. Y faire entrer l'état de › Justesse 68030 et
  `stopGraceUntil` une fois STOP+8 porté.
- [ ] **Détacher `src/jit` du démarrage de POM68K.** Le seul include
  extérieur est `JitConfig.h` → `StartupSnapshot.h` : le remplacer par un
  `jit::Config` injecté. `JitMetrics.h` porte un `#ifdef __aarch64__`
  au-dessus de `backends/`, contre l'invariant 6.
- [ ] **Séparer à la compilation la couture JIT du chemin chaud de
  l'interpréteur.** `mmu040InstrStart`, `mmu040Read` et `mmu040Write`
  portent en ligne fenêtre, DTLB, sonde de timing et publication de
  cache ; un interpréteur seul doit pouvoir se construire sans.
- [ ] **Porter les grades de preuve dans les commentaires du cœur.** Les
  patches de `extern/moira/Moira/` citent leur tranche (`POM68K O4…`) et
  leur oracle, pas un grade ni une révision : les règles tranchées en
  › Socle doivent se lire `[B]`, `[D] … @ révision` ou `[F]` à la tête du
  commentaire, pour qu'un portage ne « réaligne » pas une divergence
  voulue.
- [ ] **Choisir un seul softfloat 80 bits et le rendre réentrant.**
  `extern/softfloat` ici, un second portage de la même lignée dans
  `src/io/SoftFloat*.hpp` chez NeoST ; `moiraFpuFs` est un état statique
  partagé par toutes les instances.
- [ ] **Dédupliquer avant de porter.** `src/Mmu030Peek.h` réimplémente la
  marche de tables et a déjà dérivé deux fois ; les quatre wrappers 040
  sont des quasi-copies qui ont divergé, et la logique
  boost/`flushTicks`/`machineClock` est recopiée sur une demi-douzaine de
  wrappers 030 ; la sonde coprocesseur FC = 7 est émulée dans quatre
  cartes faute de cycles d'espace CPU émis par le cœur.
- [ ] **Supprimer le code mort et les étiquettes périmées.**
  `fpuDisabledSaneFline` (levier Mac PACK 4 que rien n'arme, le code le
  dit lui-même), `MoiraTypes.h` qui dit le 030 et le 040 « Disassembler
  only », et le 68EC040, sans profil ni corpus, dont `hasMMU()` contredit
  le chemin de traduction.
- [ ] **Remettre à jour les documents du cœur.** `POM68K_JIT.md` : journal
  arrêté au 2026-09-07, « douze wrappers » (quatorze), § 6.2 sur les EA
  indirects mémoire, inventaire de stores « sur toutes les cartes 030 »,
  refus x64 « jusqu'à re-run ». `JitBackendA64.md` : régime Finder oisif
  dit ouvert. Commentaire de `caps()` x64 sur la tempête du mode 2.
  `POM68K_VENDOR.md` : frames FPU $2 non annotées face au rang 26.
  `config_knobs.tsv` : ancres d'échéance inexistantes. `DEV.md` : renvoi
  pendant sur le LC II sans FPU.
- [ ] **Écrire ce que le JIT vaut pour NeoST avant de le porter.** Les
  générateurs natifs ne déclarent que 68040 et 68030 ; `threaded` vaut
  ×1,03 à ×1,08 sur un 68000 à cycles exacts. Décider si un émetteur
  68000/68020 est dans le programme, ou si le JIT ne concerne NeoST qu'à
  partir d'un 68030.

---

## Jalon 2 — Preuve, outillage et dettes de mesure

**Sortie :** chaque fenêtre a un gate ou une passe manuelle datée ; un run
complet publié par la CI sur un runner à assets ; version 0.3.

- [ ] **Installer un runner auto-hébergé avec les assets.** Rendre le palier
  `full` déclenchable par push et publier `LastTest.log` + le census
  exécutés/soft-skips. *Bloqué : infrastructure/hôte à provisionner.*
- [ ] **Exécuter les locksteps sur un hôte Windows.** Préalable nommé de
  « `threaded` est le plancher Windows » : tant qu'aucun hôte Windows ne
  les exécute, le choix reste une décision et non une mesure. *Bloqué :
  hôte Windows.*
- [ ] **Trancher la reproductibilité de l'agent Disques entre chaînes
  Retro68.** Sources inchangées depuis le 2026-09-14, mais la chaîne de
  l'hôte x86-64 (Retro68 `f99ecb5`, GCC 16.1.0) produit un
  `POM68KDisques.bin` dont le code diffère de la copie livrée dans
  `share/` : `agent_binary_test` est rouge ici seulement (vert en CI, qui
  n'a pas de build Retro68). Copie livrée gardée le 2026-10-14 ; vérifier
  depuis le M4 si sa chaîne la reproduit, puis décider quelle chaîne fait
  référence (`CHANGELOG` 2026-10-14 (seventh)).
- [ ] **Dater une passe manuelle de « Révéler » (fenêtre DaynaPort).** Seul
  reste de la dette de preuve du contrôle DaynaPort au GUI : le bouton
  lance `open` / `xdg-open` / `explorer`, qu'aucun gate ne peut observer.
  Le reste est couvert par `gui_windows_test`, `gui_relaunch_smoke_test`,
  `q605_afp_rename_etalon` et `q605_dayna_driver_etalon` (`CHANGELOG`
  2026-09-16/17).
- [ ] **Étendre l'oracle invité différentiel aux profils restants.**
  Seize machines ont leur gate `<profil>_prober_oracle_etalon` (Mac II,
  LC, LC II, LC III, LC 475/580, Quadra 605/610/650/630/700/800/900/950,
  Centris 610/650 ; `CHANGELOG` 2026-10-10 (seventh), (eighth), (tenth),
  2026-10-13 (sixth)). Restent le LC 575 (le `maclc575` de MAME 0.287,
  « imperfect », s'arrête sur « Starting Up »)… : chacun = une branche de
  `tests/prober_oracle.cpp` et son rapport MAME.

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
  chemin — c'est la calibration de § Moteur › Temps (boost 2 passe la
  période PIC de justesse, boost 3 non) — *Bloqué : un chiffre matériel
  (TimeDBRA d'un vrai Centris 650 ou Quadra 700, ou une mesure de la
  fenêtre ST).* Le Quadra 700 n'est pas encore instrumenté. Nouvel
  observable (2026-10-10) : la souris du Quadra 650 de POM68K n'a pas de
  routine de service ADB, de façon déterministe sous les deux moteurs, là
  où le `macqd650` de MAME et le Quadra 800 de POM68K (même carte, même
  horloge) en ont une — non jugé dans `q650_prober_oracle_etalon` en
  attendant.
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
