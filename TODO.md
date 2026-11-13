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

## Objectif principal — Moira CPU/PMMU/JIT prêt à migrer vers NeoST

Préparer le cœur Moira de POM68K — CPU 68000 à 68040, PMMU, FPU, et son
JIT — pour qu'il migre vers NeoST **dans un état propre**, et pour pouvoir
en présenter une version à Dirk Hoffmann, l'auteur de Moira. Tout le reste
de ce fichier passe après, la section « Vitesse » ci-dessous comprise :
une optimisation côté hôte POM68K ne rapproche pas de NeoST.

**Où on en est (2026-11-08).**

- **Le 68000 est prêt à reprendre** : 1 000 058 vecteurs SingleStepTests
  tenus cycle de bus par cycle de bus, frames /BERR externes, les trois
  hooks que NeoST attend (`illegalOpcodeHook`, `willProcessFault`,
  `setBusLock`) ; le contrat de surface (`extern/moira/POM68K_SURFACE.md`)
  écrit ce que l'hôte doit au cœur, et le cœur ne lit plus
  l'environnement du processus.
- **Les additions 68030/68040 et le JIT sont prouvés dans POM68K, pas
  prêts à exporter.** Corpus différentiels verts contre WinUAE strict,
  premier oracle FPU 68040, D23b/D24 tranchées par le manuel — mais :
  1. les restes nommés ne sont pas corrigés (D25–D29, six octets de
     frame IDLE 68882) ; depuis le 2026-11-10 (`CHANGELOG`, sixième à
     onzième), les champs de bits mémoire ne touchent plus que leurs
     octets, les restes 030 du balayage de 60 000 vecteurs sont réglés et
     `sst68030_rmw` n'a plus de liste de divergences ;
  2. le JIT n'a jamais tourné hors d'un Macintosh : fenêtres de fetch et
     de données, garde d'écriture dans le code traduit reposent sur
     `codeSpan`/`jitGuard_` fournis par nos cartes ; le contrat de
     surface le décrit, un premier portage le prouvera ;
  3. pas de jambe AArch64 récente (tier 030 ARM dû depuis septembre, delta
     a64 de +6 % jamais attribué).

**Ordre de travail** (le détail vit dans `§ Moteur`) :

1. *Propreté présentable* — code mort et étiquettes périmées du cœur, grades
   de preuve `[B]`/`[C]`/`[D] @ révision`/`[F]` en tête de chaque patch
   `POM68K` du cœur, documents du cœur à jour (`§ Moteur › Surface`).
2. *Couture JIT séparable à la compilation* : un interpréteur nu doit se
   construire sans fenêtre, DTLB ni sonde (`› Surface`).
3. *Contrat de surface vérifié* : chaque « unverified » devient un gate ou
   une phrase corrigée.
4. *Restes nommés* décidables ici (`› Justesse 68030`, `› Justesse 68040`),
   puis la jambe AArch64 quand l'hôte est disponible.
5. *Premier portage réel* vers NeoST, qui dira ce que le contrat a oublié.

**Dépôt — décision en attente (2026-11-12).** Le commit `f9a29795`
(2026-11-11, 21:54) contient par erreur `lc580_ide_boot.img.pomundo`, un
journal d'écriture disque de 2,5 Mo laissé par un test en cours : des
blocs d'une image système privée, poussés sur le dépôt public. Retiré de
l'arbre et `*.pomundo` ignoré depuis `c5dc026c`. **Il reste dans
l'historique** : le purger demande de réécrire `main` depuis `f9a29795`
et de pousser en force, puis de demander à
GitHub la suppression des objets devenus inaccessibles. Décision du
propriétaire.

**Rétro-portages NeoST (2026-11-11, `CHANGELOG` (thirteenth)).** STOP
68000 et `<cstdlib>` portés (format de save-state v31), sur la branche
`port/neost-68000-stop-reset`. **Reste : le reset 68000 de NeoST
`9f20046`** (2 cycles de plus entre les deux lectures de préfetch).
Porté seul, il laisse le PowerBook 100 au Finder avec le curseur montre
pour toujours (91 pixels, encore à 9 000 trames) ; les 111 autres gates
des machines 68000 ne bougent pas. Ce n'est pas une empreinte à
ré-épingler tel quel. **Cause trouvée** (`CHANGELOG` (thirteenth)) : la
carte Portable dépend du découpage hôte — `PortableCpu::runCycles`
avance la carte par paquets de 256 cycles pendant que le Power Manager
tient le processeur, et n'interroge ses lignes qu'aux frontières de ces
paquets. **Corrigé et fusionné le 2026-11-12** (`CHANGELOG` (fourteenth),
(fifteenth), (seventh)) : la carte ne dépend plus du découpage,
`pb100_boot_etalon` est ré-épinglé sur l'écran du Portable
(`cc4a869f52d960ad`) et le reset de NeoST est porté. Reste à comprendre
pourquoi le Finder laisse la montre sur les deux machines (`InitCursor`
puis `SetCursor` depuis `$7ABEFA`). À ne pas
reprendre : `71b92cc` (retard d'échantillonnage IPL, non tranché côté
NeoST).

**Relevé NeoST du 2026-11-13** (NeoST `9db090b`, dernier commit du cœur
`c9925da`) : `diff` fichier par fichier, rien de nouveau à porter.
`cc554ef` ne s'applique pas (`MOIRA_MIMIC_MUSASHI` est faux ici).
`e809c24` (lecture en plus d'un MOVEM mémoire→registres dans le même
espace que les transferts) est déjà vrai ici, mais **les deux cœurs ne
choisissent pas le même espace en relatif PC** : NeoST lit en espace
programme d'après fx68k @ 0602ee46 [D], ce cœur en espace données
d'après SingleStepTests 68000/v1 [D] (`readM`, `sst68000` 16 130/16 130
sur MOVEM avec FC comparé). À trancher par une source de rang B ou A
avant le portage vers NeoST.
`ea81c29` (2026-11-12, « le saut d'attente STOP garde la phase du
68000 ») corrige l'enveloppe de NeoST (`Cpu68k::run` posait l'horloge
sur une échéance impaire pendant un STOP), pas Moira. Sans objet ici :
aucune enveloppe 68000 ne déplace l'horloge d'un CPU arrêté — il
traverse `execute()` par pas de 2 cycles, et le moteur JIT lui rend la
main dès que `flags != 0`. Si une enveloppe gagne un jour un tel saut,
il devra avancer d'un nombre pair de cycles.

## Vitesse — côté hôte POM68K, après l'objectif principal (profils du 2026-11-07)

Deux profils à 1 kHz sur les bancs à budget fixe, après l'échéance VIA du
Quadra 605 (`CHANGELOG` 2026-11-07 (quatorzième)) : Q605 JIT x64 — code
généré 33 %, LLE Cuda 68HC05 ~17 %, marche des périphériques
(`Q605Memory::tick`/`cyclesToNextEvent`/`Cpu040::flushTicks`) ~14 %,
`Dafb::tick` 3,5 %, MMU 4 %, repli interpréteur 1,7 % ; LC II JIT x64 —
code généré 22 %, runtime du moteur (`executeUntil`, `record`, dispatch)
21 %, `V8Memory::tick` + `Cpu030::flushTicks` 12,5 %, MMU 030 9 %, Egret
LLE 5 %, repli interpréteur 5 %. Donc, par ordre de rendement attendu :

1. **La même audit d'échéance sur le LC II (V8) — FAIT le 2026-11-07
   (quinzième) — puis Q630/Centris/Q700.** Recette prouvée : recenser qui
   borne la tranche, remplacer la borne « prochain tick E », stall-puis-flush,
   ré-armement par accès. Q605 : −50 % en JIT, −19 % en interpréteur. LC II :
   la mesure a dit autre chose que la supposition — 60 % des flushs
   venaient du flush systématique de la carte avant chaque accès E/S (le
   53C80 et l'ASC en scrutation), l'Egret borne le reste à ~27 cycles ;
   bornes de tous les appareils publiées et mises en cache en temps
   absolu, flush seulement là où le temps de l'appareil compte : −24 % au
   boot, −5 % en JIT à 3 000 trames, −1 % en interpréteur. Q630 et
   Q700/900/950 faits (seizième) ; les Centris refusent tant que leur
   PIC1654S dépend du découpage : `§ Moteur › Temps`.
2. **Le runtime du moteur sur le 030 (21 %)** : chaque bloc rend la main au
   dispatcheur (1 sortie toutes les 300 instructions sur le Q605 ; 830
   instr/bloc sur le LC II). Du chaînage direct de blocs (saut compilé →
   bloc suivant sans repasser par `executeUntil`) est le gain classique
   ici ; les sorties « cpu flags » et « not compilable » (1 par 1 700 et
   1 000 instr) sont les deux autres fuites — ce sont les lignes de
   `§ Moteur › JIT performance`, qui exigent exactement ce profil.
3. **L'interpréteur 68HC05 (Cuda/Egret, 5–17 %)** : code hôte pur, sans
   effet sur la trace — table de dispatch, `read8` inliné. Gain modeste
   mais sûr.
4. **La MMU 030 (9 %)** : ce qui reste hors fenêtre de données — à voir
   quels accès ratent la fenêtre.
5. **Le code généré lui-même (22–33 %)** : la qualité d'émission, mais
   c'est le poste le plus coûteux à améliorer et il faut un profil par
   bloc.
6. **Ne réveiller à chaque instruction du MCU que ce que le MCU voit —
   première moitié FAITE le 2026-11-11 (`CHANGELOG` (fifth)).** Le 68HC05
   court en avance sur ses instructions internes ; la carte n'est plus
   réveillée qu'aux accès de port (Q605 27,3 M → 7,6 M `mem.tick` pour
   3 000 trames, −11 à −19 % ; LC II −15 à −16 %). Restent :
   - la même recette sur les autres MCU en LLE qui bornent la tranche à
     l'instruction : PG&E (`PgePmu`, « CudaLle debt pattern »), M50753
     (`Pb030Pmu`, `PortablePmu`), 65C02 des IOP (`ApplePic`), PIC1654S des
     Centris — aucun n'a de banc à budget fixe, il en faut un d'abord ;
   - les périphériques que personne n'observe entre deux échéances (DAFB,
     lecteurs, pompe CD, ASC, CA1) avancés à l'accès : flush devant chaque
     observateur côté hôte et décision de format de save-state ; à
     re-profiler, le gisement a fondu avec les réveils ;
   - cartes 68000 : échéances FAITES sur 128K–Plus le 2026-11-11
     (`CHANGELOG` (eighth), Plus −64 %). Restent les compacts ADB (SE,
     SE FDHD, Classic), non regroupés tant qu'`AdbVia` est resynchronisé
     dans `tick()` et que son HLE borne à 1 ; et la disquette en cours de
     lecture (IWM non indépendant du découpage).

7. **Profils du soir du 2026-11-11, après les douze entrées du jour**
   (`sample` 1 kHz, binaires de `main`). *Corrigé le soir même* : une
   première version de cette note comptait seulement les échantillons
   dont le sommet de pile est symbolisé ; `sample` ne remonte pas la pile
   depuis le code JIT, et ces échantillons — la moitié — manquaient. Parts
   sur le total des échantillons du fil :
   - *Quadra 605, après le boot* : code généré environ 52 %, runtime du
     moteur JIT 14 % (`executeUntil` 4 %, `armWindow` 2 %, dispatch,
     `record`), chemin interpréteur MMU/ATC 13 %, helpers JIT de Moira
     4,5 %, carte mémoire 3,7 %, interpréteur 3,3 %, 68HC05 3 %, marche
     des périphériques 2,5 %. 2,2 % des instructions passent hors du code
     natif (drapeaux CPU, recul d'armement, traçage) et coûtent le quart
     du temps.
   - *LC II* : code généré environ 52 %, 68HC05 8 %, runtime JIT 8 %,
     interpréteur 7 % (les handlers de Moira apparaissent repliés sous
     `<deduplicated_symbol>`), chemin MMU 5,5 %.
   - Le poste dominant est donc bien l'émission elle-même, que `sample`
     ne détaille pas : il faut un profil par bloc (compteurs du moteur)
     avant d'y toucher.
   - *Fait le 2026-11-12* : `LINK` natif sur le 68040, a64 puis x64
     (replis en bloc 25,8 M → 1,75 M sur a64). La jambe x86-64 se joue
     sur cet hôte sous Rosetta (`build-x64-proof`) : `unit|jit` 211 / 211.
   - *Clos le 2026-11-12* : les deux rouges x64 du tier 68040
     (`quadra610_agent_boot_etalon`, `q950_agent_boot_etalon`) venaient
     du snapshot ATC copie-sur-écriture, dont le pointeur survivait à la
     pile de `X64Backend::run` ; corrigé par
     `pomJitAtcReplayRelease040`. L'admission `LINK` x64 n'était pas en
     cause. Reste utile : rejouer le tier sur un vrai hôte x86-64.
   - *Fait le 2026-11-12 (`CHANGELOG` (sixth))* : le profil par bloc
     existe (adresses de `sample` rattachées à une table des blocs,
     instrument non versé) et dit que le code généré est plat — pas de
     bloc chaud, mais 3 à 4 instructions hôte par accès à un champ du
     CPU. Deux bases épinglées dans `x27`/`x28` : LC II −22 à −28 %,
     Quadra 605 −6 à −11 %, a64 seul ; aucun gate n'épingle la taille
     du code émis. Restent dans l'émission, non
     mesurés : les écritures de `pc`/`pc0` devant chaque accès mémoire
     (lues seulement par les chemins froids), la base du DTLB
     rematérialisée à chaque accès, `movW` sur une constante à moitié
     basse nulle.
   - *Candidat mesuré, non versé* : régler dans le dispatcheur un
     `CHECK_IRQ` sans rien à livrer (LC II −2,0 à −2,6 %, exact, tiers
     ROM des deux hôtes dus). Rangé le 2026-11-13 (`CHANGELOG`) : six
     compteurs épinglés bougent d'une instruction, le septième révèle
     que `MOVE.B (A0),(A1)+` à source périphérique n'est jamais natif
     sur a64 (seul dans son bloc il est refusé et interprété ; dans un
     bloc plus long il prend le pas lent) alors que `runExactSourceMove` l'affirme. Cause :
     l'émetteur du `MOVE` à deux EA refuse un coût tracé (11, dont 3
     cycles d'attente du périphérique) différent du coût statique (8),
     avant la branche du thunk exact. À trancher d'abord : admettre
     l'attente tracée sur cette branche, ou donner au test un
     périphérique sans attente. Non vérifié : x86-64, et quelles cartes
     réelles facturent une attente dans leurs boucles de pseudo-DMA.
     *Mesurés et refusés* : fenêtre gardée armée sur les pas à drapeau
     (−1,2 %, au plancher) ; fenêtre plafonnée à la longueur du bloc
     (−5 % au boot, rien à 6 000 trames, écritures en page de code
     doublées).
   - *Petit, pas prioritaire* : `Cpu030::catchUp` et `Cpu040::catchUp`
     sont hors ligne comme l'était `Cpu020::catchUp`, mais sur ces cœurs
     `SYNC` ne coûte rien et l'appel est rare (3,9 M pour 3 000 trames du
     Q605) : 0,5 % du profil du LC II, sous le plancher de mesure.
   - *Refusé le même soir* : `pomJitFetch` forcé en ligne (dans le bruit
     sur les quatre bancs).
   - *Mesuré et sans suite* : `POM68K_JIT_ARM_BACKOFF` (défaut 32).
     Quatre exécutions isolées du Q605 à 6 000 trames semblaient donner
     une tendance (12,83 s à 0, 13,56 s à 32). L'ABBA entrelacé 32 contre
     4, machine encore chargée par deux tiers : Q605 −0,3 % (3 000
     trames) et +1,1 % (6 000), LC II +4,5 %. La tendance était du bruit ;
     à refaire au calme seulement si un profil le redemande. Taille du cache de dispatch : 131 072 contre
     16 384 emplacements, ABBA intra-processus, −0,5 % sous un plancher de
     3,6 % — rien.

## Statut

- **En tête : le programme § Moteur** (priorité décidée le 2026-11-06,
  objectif restaté le 2026-11-08 — section « Objectif principal »).
  Stabiliser, prouver et nettoyer le fork Moira et ses additions
  68030/68040, en interpréteur et en JIT, jusqu'à ce que le cœur puisse
  migrer vers NeoST sans emporter un Macintosh avec lui.
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
  Deux sont donc gardées comme exceptions connues
  (`oracle/README.md § Rulings taken against a manual`) : D12 (adresse
  d'instruction des frames $2) et D23a (frame FPU $3 sur 68030) — plus
  D30 (trappe de type de donnée immédiate) et deux règles 68000 (sortie
  anticipée de DIVS, ASR au-delà de la largeur). Chacune est tenue par des vecteurs
  épinglés : la renverser demande soit une preuve de grade A, soit le
  mécanisme de divergences assumées — il existe depuis le 2026-11-07 dans
  `sst68030` et `sst68040` (`--known`, `tests/SstKnown.h` : liste nommée
  par décision, gate rouge si un vecteur listé se met à passer), ce qui
  permet de renverser D23a et D12 sans perdre un vecteur. **D23b est
  appliquée** depuis le 2026-11-07 (rang 53) : le « hack » FRESTORE
  n'existait que dans le réglage permissif de l'oracle ; les quatre
  vecteurs épinglés de juillet sont listés dans `tests/sst68030.known`,
  `sst68030_solo` est régénéré en strict. Reste aussi à passer au crible les
  règles 6888x que l'audit soupçonne sans que les notes disent ce qu'en
  dit le manuel : FPIAR, vecteur d'exception gardé armé, longs internes de
  FSAVE IDLE, bit 9 du SSW, PTEST niveau 0, PMOVE TC invalide.
- [ ] **Rendre les corpus épinglés d'origine récupérables.** Les deux
  corpus du 2026-11-06 (`sst68030_solo`, `sst68040_8k`) se régénèrent à
  l'identique depuis l'arbre : leur recette est dans
  `tests/sst_corpora.lock`. Les deux corpus d'origine (`sst68030`,
  116 Mo ; `sst68040`, 315 Mo) n'ont pas de recette enregistrée et
  n'existent que sur deux hôtes et le disque privé : les publier (asset de
  release, sortie WinUAE sans donnée Apple), ou les remplacer par des
  corpus à recette une fois celle-ci retrouvée.
- [ ] **Élargir la géométrie MMU du fuzzer 030.** `gen030.py` n'a qu'un TC
  (pages de 32 Ko, un niveau, descripteurs courts, CRP sans limite) : ni
  FCL/SRE, ni tables à plusieurs niveaux, ni terminaison anticipée, ni
  descripteurs indirects ou longs, ni violation de limite, et la cellule
  `tt` ne parcourt jamais l'arbre. Les géométries réelles ne sont prouvées
  que par les boots. `oracle/README.md` annonce plus que le code ne fait.
- [ ] **Varier les TTR du fuzzer 040 et balayer des graines neuves à
  chaque passe.** Les pages de 8 Ko ont leur corpus (`sst68040_8k`,
  4 000 vecteurs, recette dans `tests/sst_corpora.lock`) ; reste la
  disposition des TTR, figée (ITT0 = tout, DTT0 = superviseur seul). Deux
  leçons : le 2026-11-06, 13 100 vecteurs de graines neuves ont trouvé
  trois défauts que les 7 200 épinglés ne contenaient pas ; le 2026-11-07,
  une famille ciblée (`fline`, la fenêtre F-line brute) est partie de
  3 074 / 4 500 là où la famille `random` ne plaçait que quatre vecteurs
  épinglés. Un corpus épinglé est un plancher, pas une couverture, et une
  famille aléatoire ne remplace pas une famille par fenêtre de décodage.
  Faire du balayage de graines neuves une étape de la boucle
  (`oracle/fuzz/loop.sh` n'a pas d'équivalent 040), et écrire les familles
  brutes qui manquent : extensions complètes d'EA réservées, et le
  pendant 68040 de la famille `rmw` (écritures fautives des instructions
  RMW, `sst68030_rmw`). La fenêtre
  `$F400-$F7FF` (CINV/CPUSH/PFLUSH/PTEST/MOVE16) a la sienne (`sysraw`,
  6 000 / 6 000 au premier rejeu, non épinglée). La fenêtre cpID 1 du
  68030 a la sienne depuis le 2026-11-07 (`sst68030_fpuraw`, 6 000
  vecteurs contre l'oracle strict).
- [ ] **Donner au corpus FPU du 68040 les exceptions armées et les
  caches.** `sst68040_fpu` (7 500 vecteurs) pose FPCR avec des exceptions
  armées sur un vecteur sur dix seulement, et aucun corpus 68040 n'a
  `cacr` ≠ 0 : le rang 28 (caches porteurs de données) n'a que
  `cache040_test`, écrit à la main.
- [ ] **Couvrir le 68020 nu par l'oracle.** Aucun corpus 020 ;
  `callm_rtm_test` et les 404 séquences de `berr020_test` tiennent des
  règles ciblées, mais aucun corpus différentiel ne juge le 020. Le fetch
  général à file de préfetch repose encore sur les boots Mac II et LC.
- [ ] **Décider ce que fait un 68010 interrompu avec une pile impaire.**
  Vu en écrivant `oddStackInterruptFrame` (`CHANGELOG` 2026-11-13
  (tenth)) : le cœur prend l'interruption (SR passe au niveau, SSP − 8)
  mais n'atteint pas le handler et n'empile rien de lisible. Un 68010 a
  un bus 16 bits : erreur d'adresse attendue. Aucun Macintosh n'a de
  68010 ; à trancher pour NeoST et l'amont, avec le corpus `sst68000`
  comme juge du côté 68000.
- [ ] **Rejouer les corpus à travers le JIT.** Les harnais `sst` ne
  pilotent que l'interpréteur et aucun gate JIT ne se compare à WinUAE.
  Sans Macintosh il n'existe qu'un programme 040 synthétique, les gates
  copyback et `jit_restart_write_030_test` ; tous les `jit_lockstep_*`
  exigent une ROM.
- [ ] **Obtenir une preuve indépendante du 6888x.** La convergence
  numérique repose sur la même famille SoftFloat que l'oracle
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
  `irqDelay` et demande un champ dans `Moira::pomVisitState`. Le corpus n'a aucun
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
- [ ] **Évaluer le cache d'instructions 68020 de l'amont (`a4c273b`).**
  Seul commit amont postérieur au pin qui change un comportement et que le
  fork n'a pas (`NEOST_VENDOR.md` § *Upstream origin*) : un i-cache 68020
  repris de vAmiga, avec un budget de cycles fractionnaire (`426e4a3`). Il
  déplace les cycles 68020 que tiennent les boots Mac II ; à mesurer contre
  le modèle actuel avant de le prendre ou de le refuser par écrit.
- [ ] **Rafraîchir le texte NeoST de `extern/moira/NEOST_VENDOR.md`.**
  Notre copie garde le texte de NeoST du 2026-07-11 ; la sienne a doublé
  depuis. Le pin amont, la recette du diff à trois voies et l'état des
  onze commits amont postérieurs y sont notés depuis le 2026-11-10
  (§ *Upstream origin and the three-way diff*) ; reste à reprendre ses
  sections postérieures, ou à renvoyer vers elles.

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
- [ ] **Relever le cycle d'opérande d'un champ de bits de trois octets.**
  Le MC68030UM-1989 § 11.6.14 compte un seul cycle d'opérande sous cinq
  octets et le bus 020/030 a un SIZ de trois octets ; le cœur et l'oracle
  émettent un mot puis un octet (grade F au site, `execBitFieldEa`). Le JIT
  compte deux accès ATC 040 pour suivre le cœur. *Bloqué : capture de bus de
  `BFEXTU (An){0:24}` sur un vrai 68030 ou 68040.*
- [ ] **Modéliser les fautes de préfetch différées, 030 et 040.** Limite
  documentée : `irc` est lu à pc+2 dès le début de l'instruction, donc une
  instruction d'un mot dans le dernier mot d'une page peut fauter là où le
  processeur réel attend que le mot serve.
- [ ] **Remplacer ou prouver le modèle « réexécuter l'instruction » du RTE
  $B.** Les accès antérieurs à la faute sont réémis (lecture de
  périphérique en double) et le RMW verrouillé à DF effacé est exclu. Le
  68030 dépend encore du bit 9 du SSW, marqueur interne de WinUAE qu'aucun
  68030 n'écrit. Le 020/EC020 n'en dépend plus : `berr020_test` tient les
  cycles complétés, les (An)±, les demi-longs, MOVES et la reprise restaurée
  (`CHANGELOG` 2026-11-10, troisième), pas une continuation matérielle
  complète. Garder aussi `berr030_test` et les boots.
- [ ] **Généraliser le report du pipe de préfetch.** `mmuCapturePipe`
  capture quatre mots après PMOVE TC/CRP/SRP seulement, pour un idiome de
  la ROM LC II ; PFLUSH, une écriture de TT ou une cible de saut n'ont rien.
- [ ] **Trancher les champs EA $3D-$3F sur 68030 (D29).** Le fork laisse le
  coprocesseur décider (un registre-à-registre s'exécute, une forme
  mémoire efface l'octet d'état puis prend un Line-F) ; la table d'opcodes
  de l'oracle les refuse d'emblée. 63 vecteurs listés. *Tranche :
  `$F23D $0000` sur un vrai 68030 + 68882.*
- [ ] **Décider le sixième bit du prédicat FBcc sur preuve.** Depuis le
  2026-11-07 FBcc évalue cinq bits sur tout FPU, comme FScc/FDBcc/FTRAPcc
  et comme l'oracle ; l'ancien Line-F venait d'une lecture du bloc mort
  de `fpp_cond`. Aucun manuel sous la main ne tranche (grade F au site).
  *Tranche : FBcc `$F2A1` sur un vrai 68882 et un vrai 68040.*
- [ ] **Trancher l'ordre de lecture de FMOVEM en EA indirect mémoire à
  extension complète avant un Line-F de direction.** Laissé « still open »
  dans `POM68K_VENDOR.md § MC68882 FPU execution` ; le correctif du
  2026-08-12 ne couvre que le cas préindexé valide.

### Justesse 68040

- [ ] **Donner des vecteurs à plusieurs instructions aux autres états en
  vol.** `fuzz030.py`/`fuzz040.py --steps` et le champ `steps` des
  vecteurs existent (2026-11-07) ; ils tiennent D24 (`sst68040_fpu2`) et
  les exceptions postées du 6888x (`sst68030_fpu2`, 5 979 / 6 000, le
  reste étant D23b). Le même outil sert l'ATC ci-dessous (remplacement,
  bit S), le pipe de préfetch et le verrou du RTE $B du 68030 : il y
  faut des familles dont la première instruction arme l'état.
- [ ] **Livrer la trappe de type de donnée des opclasses 000/010 en
  pré-instruction (D30).** Le manuel (M68040UM § 9.7, types non
  supportés) la place comme les exceptions arithmétiques de D24 : postée,
  prise en pré-instruction (format $0) par l'instruction FPU suivante ;
  le fork et l'oracle la prennent immédiatement en format $3, et l'oracle
  le dit lui-même (« simplification: always mid/post-instruction
  exception »). Des centaines de vecteurs épinglés tiennent la
  simplification : à faire avec le mécanisme de D24 étendu au vecteur 55,
  des vecteurs à deux instructions, et une liste de divergences connues
  le temps de régénérer. Noté le 2026-11-07 en lisant § 9.7 pour D24.
- [ ] **Six octets de frame IDLE 68882 après une sortie mémoire qui
  trappe.** Famille `fpuh` (FSAVE en tête des handlers, trois pas, sur la
  branche `engine/next`), 68030 + 68882, oracle strict : 5 977 / 6 000 ;
  17 sont D23b, six sont une sortie FMOVE vers la mémoire ou un entier
  dont l'exception est prise, et dont la frame IDLE sauvée par le handler
  diffère d'un octet (offset $29 : `fpuh/tt seed=1313 #1298`, `#869`,
  `#1388`, `#1501`, `fpuh/off seed=1311 #1078`, `#1393`). Même famille sur
  le 68040 : voir rang 51 de la branche.
- [ ] **Trancher FMOVE.P vers Dn (D25) et le pas (An)± du k-facteur
  dynamique (D26).** D25 : Line-F ici (`fpu040_test`), trappe de type de
  donnée dans l'oracle — 27 vecteurs. D26 : le fork avance An de 12, la
  table de tailles de l'oracle contient 0 pour le format 7 — 16 vecteurs ;
  là le manuel donne raison au fork. *Tranche : la même instruction sur
  un vrai 68040.*
- [ ] **Évaluer l'EA avant le Line-F d'un déplacement de registres de
  contrôle vers une destination relative au PC (D27).** Un vecteur : un
  EA indirect mémoire marque une page « utilisée » dans l'oracle. Même
  famille que l'ordre de lecture de FMOVEM (› Justesse 68030).
- [ ] **Mesurer la phase exacte du remplacement ATC 040 sur matériel.**
  La géométrie 64 entrées par ATC, seize jeux de quatre voies, la priorité
  aux invalides et le compteur aux accès sont modélisés et comparés en
  interprète, fenêtres, threaded et natif sur les deux hôtes
  (`CHANGELOG` 2026-11-09, dix-septième). MC68040UM-1993 § 3.3 ne fixe
  ni la phase de reset, ni le front d'incrément, ni le couplage I/D, ni
  la phase d'installation de PTEST. Les compteurs indépendants initialisés
  à zéro et le débit recherche + installation/reprise restent des choix
  grade F. *Bloqué : traces de remplacement d'un vrai 68040/68LC040.*
- [ ] **Trancher la frame d'un MOVEP 040 partiellement écrit sur matériel.**
  Relever PC, WB1/WB2/WB3 et les octets répétés après RTE, à chaque
  position de faute .W/.L. La figure générique du MC68040UM-1993 § 8.4
  ne décrit pas cette séquence périphérique ; la reprise actuelle est un
  choix de modèle grade D, pas une preuve de silicium (`CHANGELOG`
  2026-11-09, cinquième). *Bloqué : capture d'un vrai 68040/68LC040.*
- [ ] **Trancher les erreurs tardives de write-allocate 040.** Distinguer
  les portions demandées des battements inutiles, puis terminer une
  écriture dont le remplissage avorte sans exception, sans perdre les
  voisins ni la victime sale (§ 4.6.1). Le modèle signale encore chaque
  erreur de remplissage d'écriture. Capturer aussi une frame réelle pour
  valider le choix de WB3 pour l'écriture en attente et le PC de reprise.
- [ ] **Prouver les buffers simultanés après une faute de poussée 040.**
  Étendre les témoins au WB2 d'une MOVE16 en attente et aux combinaisons
  WB2/WB3 de la vraie pipeline (MC68040UM-1993, Table 8-6). Les nouveaux
  témoins ne couvrent que l'écriture ordinaire de leur instruction.
  Capturer aussi l'invalidation sur faute de poussée déclenchée par un
  accès NC ; § 7.6.1 décrit explicitement le remplacement et CPUSH.
- [ ] **Fermer la preuve du mode cache actif.** Aucun `sst68040`
  enregistré avec le cache, aucun boot interprète cache actif, tous les
  etalons ROM sans cache ; le débogueur lit la RAM sous le cache et
  `pomSnoop040Read/Write` n'ont aucun appelant, donc tout écrivain hôte de
  RAM invitée est un maître non espionné.
  Vérifier aussi le cas d'un hit verrouillé avec CACR.DE désactivé :
  MC68040UM-1993 § 4.3.3 décrit alors le premier miss ordinaire suivant
  comme non alloué ; cette transition reste à modéliser.
  Donner aussi des témoins MOVE16 sur hit propre/sale : § 4.3.3 impose
  une lecture servie par le cache et une écriture qui invalide. Le chemin
  commun NC invalide aujourd'hui le hit de lecture et pousse les anciennes
  données avant la réécriture. Les tests de faute MOVE16 couvrent les misses.
- [ ] **Décider l'hybride 68LC040 + 68882 logiciel.** Défaut produit des
  LC 475/575/580 et Centris 610/650, combinaison qui n'existe pas en
  matériel ; un vrai `NONE` boote déjà le Quadra 605
  (`q605_barefpu_boot_etalon`).

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
- [ ] **Rendre le PIC1654S des cartes DjMEMC indépendant du découpage en
  tranches, puis leur porter l'échéance VIA.** Le Q630 et les Q700/900/950
  l'ont depuis le 2026-11-07 (seizième) ; sur les Centris 610/650 et
  Quadra 610/650/800 elle a été retirée : tout changement de la tranche
  déplace la réponse de l'ADB (`adb.dev2.service` des Prober q800 puis
  q610 contre MAME, bissecté). Le temps du PIC doit être fonction du temps
  machine, pas du motif des flushs ; un gate qui découpe la même fenêtre
  en deux tailles de tranche et compare le flux ADB le prouvera. Ensuite
  seulement la recette (`CHANGELOG` quatorzième). Reste nommé sur le
  Q605 : les lectures 53C96/ASC/DAFB et les octets pseudo-DMA voient
  l'appareil là où le dernier flush l'a laissé (retard ≤ une tranche) —
  un modèle « temps de l'appareil = horloge CPU à l'accès », celui de
  `syncSwimFromCpu`, l'effacerait pour le 53C96 et l'ASC au prix du débit
  sérialisé (`scsiDebt_`, `q605_event_scheduler_test`).
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
- [ ] **Affiner le temps du 6888x.** Le 68881 du Mac II est facturé aux
  tables du 68882 ; la concurrence du 68882 n'est pas modélisée
  (`fpuRunBusy` bloque le processeur par quanta de 8 cycles) ; les cycles
  030/040 ne sont jamais comparés à l'oracle.
- [ ] **Rapprocher `PomIcache` du MC68030UM, sur observable seulement.**
  Tags sans contenu, tag sur S plutôt que sur le FC, FI et IBE ignorés,
  bit CI de l'ATC posé mais jamais lu, pénalité plate.

### Interprète — performance du mode normal

Chaque ligne n'ouvre qu'avec un profil temporel reproductible. Les profils
de référence sont `scratchpad/2026-11-06/INTERP_PROFILE.md` et, pour le
68040 après le chemin chaud du 2026-11-07,
`scratchpad/2026-11-07/INTERP_HOT040.md` (bancs à budget fixe, 1 500
trames, empreintes `a2036281a5c2ac55` et `663d67e4bc2d9714` — la première
tient encore après l'échéance VIA du 2026-11-07 (quatorzième) ; à 3 000
trames l'empreinte Q605 est désormais `3835bb7a075cd81c`) : le 68040
interprété passe désormais 31 % de son temps en traduction d'adresse
(63 % avant) et 30 % dans ses handlers ; le 68030, un tiers dans le fetch
d'instruction.

- [ ] **Donner à l'interpréteur la fenêtre de données et reprofiler.**
  Depuis le 2026-11-07 (rang 55) l'interpréteur arme la fenêtre de fetch
  lui-même sur les cinq cœurs quand le moteur est éteint (Quadra 605
  −36 %, LC II −17 %, Mac II −13 %, Plus dans le bruit, empreintes
  inchangées). Reste la fenêtre de données du 68030 (rang 34), dont la
  fonction de remplissage est encore celle du moteur (`POM68K_SURFACE.md`
  § 9.2), et un nouveau profil des deux bancs pour ordonner la suite.
- [ ] **Alléger la remise à zéro par instruction du mode 5.**
  `mmuExecuteStart` (5,8 %) efface le journal d'accès et les fixups et
  sauve le CCR à chaque instruction 030, sous un `try`, avec `sync()` et
  les lectures en virtuel et un `dynamic_cast` dans `processException`.
- [ ] **Re-mesurer la fenêtre de données 040 après la géométrie de
  l'ATC.** La mesure qui la laisse éteinte utilisait l'ancien ATC de
  32 entrées (≈ 10 % plus lent, pire sous mémoire virtuelle). Le modèle
  64 entrées en seize jeux de quatre voies change cette donnée ; le
  défaut reste inchangé en attendant une nouvelle mesure.

### JIT justesse et parité des hôtes

- [ ] **Donner un inventaire de stores au IIfx.** MSC, Jaws, Niagra et
  Pangola ont leur gate à `POM68K_JIT_REQUIRE_NATIVE` depuis le
  2026-11-13 (`CHANGELOG` (fifth)).
  `kJitStoreInventoryComplete` ne manque plus que sur le IIfx, qui vide
  le JIT à chaque écriture de CACR (DMA SCSI et IOP à auditer avant toute
  déclaration ; compter d'abord les vidages par trame au Finder). GLUE et
  Pb030 l'ont depuis le 2026-11-12 (`CHANGELOG` (eighth), (ninth)) :
  SE/30 sous System 7 quatre fois plus vite, PowerBook 030 −29 à −40 %.
  Restes : `se30_boot_etalon` perd 5 % (blocs périmés gardés à travers
  826 changements de carte mémoire, sorties `WindowLost` × 3) ; aucun
  banc GLUE ni PowerBook à empreinte — les chiffres viennent de gates
  entiers.
- [ ] **LC III / IIvx : l'ATC de 22 entrées tourne sous pages de 8 Ko.**
  Après le réessai immédiat des refus transitoires (`CHANGELOG`
  2026-11-12 (tenth), LC III et IIvx −17 à −18 %), le boot LC III refuse
  encore 30 M de sondes de fenêtre et 180 M de remplissages du TLB de
  données faute d'entrée ATC. C'est le comportement de l'invité ; reste à
  voir si la sonde peut répondre sans entrée (marche de table en lecture
  seule, sans écrire U/M) sans rompre l'identité avec l'interpréteur.
- [ ] **Modéliser l'intervalle hors tension du power cycling des
  PowerBook 030.** Le réveil est un reset immédiat
  (`Pb030Memory::consumeReset`) : 1,29 M de resets par boot de PowerBook
  180, le processeur tourne dans le chemin de réveil de la ROM au lieu
  d'attendre l'interruption suivante comme le matériel. Sauter ce temps
  profiterait aux deux moteurs ; c'est un changement de temps machine,
  qui demande son témoin (ROM : la séquence d'endormissement ; MAME
  `jaws_w` @ révision à nommer) et un ré-épinglage éventuel des écrans.
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

- [ ] **Lever les points « unverified » du contrat de surface.**
  `extern/moira/POM68K_SURFACE.md` décrit ce que le cœur expose et ce que
  l'hôte lui doit. Trois affirmations réglées le 2026-11-08 :
  `setFPUModel` sur 68000/68010 (cellule de `moira_rules_test`), l'audit
  complet de l'état global de softfloat (`nm` de la bibliothèque), et
  l'invalidation du JIT à la restauration (relue dans les quatorze
  cartes). Le 2026-11-08 aussi, le `extBusError` levé pendant la lecture
  d'un descripteur de table 030 (un vrai défaut, corrigé : rang 56).
  Restent : la valeur à rendre sur une
  lecture CIR FC = 7 d'un FPU présent,
  les séquences qui dépendent de la continuation, et la DMA son côté Atari
  (à NeoST). Les champs omis du visiteur : corrigés et tenus par
  `moira_state_test` (`CHANGELOG` 2026-11-08, septième). Chacune
  devient un gate ou une phrase corrigée.
- [ ] **Terminer la revue des affirmations matérielles sans source du cœur.**
  Les citations explicites de manuel ont une édition et un emplacement
  vérifiés ; les comparaisons Musashi perdues sont `[F]` avec leur témoin
  manquant et les alias Clock Signal sans preuve indépendante sont retirés.
  `core_grades_test` tient ces sources (`CHANGELOG` 2026-11-08, huitième).
  Restent les règles sans citation, à relier à une preuve ou à étiqueter
  `[F]` avec ce qui les trancherait ; le gate de citations ne les détecte pas.
- [ ] **Unifier le softfloat 80 bits avec NeoST.** POM68K utilise
  `extern/softfloat` avec un contexte par CPU, y compris les captures
  intermédiaires (`fpu_context_test`, `CHANGELOG` 2026-11-10, quatrième).
  Reste le second portage de la même lignée dans `src/io/SoftFloat*.hpp`
  chez NeoST : choisir une seule implémentation et y porter les adaptations.
- [ ] **Dédupliquer avant de porter.** `src/Mmu030Peek.h` réimplémente la
  marche de tables et a déjà dérivé deux fois ; les quatre wrappers 040
  sont des quasi-copies qui ont divergé, et la logique
  boost/`flushTicks`/`machineClock` est recopiée sur une demi-douzaine de
  wrappers 030 ; la sonde coprocesseur FC = 7 est émulée dans quatre
  cartes faute de cycles d'espace CPU émis par le cœur.
- [ ] **Trancher le 68EC040 : le refuser ou le rendre vrai.** Sans profil
  ni corpus, il exécute le chemin 68040 complet, pagination comprise, que
  le silicium n'a pas, et `hasMMU()` répond faux. Étiqueté comme tel dans
  `MoiraTypes.h` le 2026-11-08 (avec le reste des étiquettes de modèles et
  la suppression de `fpuDisabledSaneFline`) ; reste à choisir : `setModel`
  le refuse, ou la traduction s'arrête aux TTR et un corpus le tient.
- [ ] **Écrire ce que le JIT vaut pour NeoST avant de le porter.** Les
  générateurs natifs ne déclarent que 68040 et 68030 ; `threaded` vaut
  ×1,03 à ×1,08 sur un 68000 à cycles exacts. Décider si un émetteur
  68000/68020 est dans le programme, ou si le JIT ne concerne NeoST qu'à
  partir d'un 68030.

---

## Jalon 2 — Preuve, outillage et dettes de mesure

**Sortie :** chaque fenêtre a un gate ou une passe manuelle datée ; un run
complet publié par la CI sur un runner à assets ; version 0.3.

- [ ] **`-Werror` + `POM68K_NATIVE=ON` ne compile pas sous GCC 15.**
  `src/AtalkStack.cpp:565` (`std::vector::resize` dans `handleAtp`) :
  « writing 16 bytes into a region of size between 1 and 15
  [-Werror=stringop-overflow=] », faux positif de la vectorisation sous
  `-march=native`. Les builds `-Werror` de l'arbre et de la CI ont
  `NATIVE=OFF`, le défaut produit a `NATIVE=ON` sans `-Werror` : personne
  ne voit la combinaison sauf à la demander (vu le 2026-11-07 sur un
  worktree). Soit un `reserve`/`assign` qui calme l'analyse, soit un
  `-Wno-error=stringop-overflow` ciblé sur le fichier.
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
