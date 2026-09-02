# TODO — feuille de route

Ce fichier contient uniquement du travail ouvert. Les résultats, mesures,
fausses pistes et décisions terminées vivent dans `CHANGELOG.md` et
`CHANGELOG_INDEX.md`. Les détails d'implémentation vivent dans `DEV.md`,
`src/jit/POM68K_JIT.md`, `docs/JIT_BRINGUP.md` et les notes vendor.

`STATUS.md` est généré depuis les manifests CMake et reste la source de vérité
pour les gates. Toute tâche fermée quitte ce fichier dans le même changement
qui ajoute son entrée au `CHANGELOG`.

## Ordre d'exécution

Le programme suit une chaîne de dépendances explicite :

1. **P0 — rétablir la preuve** : fixtures, assets, toolchains et runs complets ;
2. **P0 — mesurer** : ligne de base ABBA et profil temporel applicatif ;
3. **P0 — terminer le JIT conforme** : effets 68040, puis qualification 68030 ;
4. **P1 — approfondir le produit** : scénarios au-delà du Finder et fidélité ;
5. **P1 — rendre la preuve portable** : Windows, sanitizers et runners ;
6. **P2 — étendre** : réseau, médias, nouvelles machines et recherche optionnelle.

Règles de travail :

- une optimisation dépend d'un profil temporel reproductible ;
- la couverture native n'est pas une preuve de conformité ;
- une nouvelle machine dépend d'un gate produit réutilisable de sa plateforme ;
- un gate asset-backed n'est probant que si son census indique qu'il a été
  exécuté, sans soft-skip ;
- l'interpréteur reste l'oracle et `threaded` le plancher portable ;
- les critères de sortie d'un jalon sont cumulatifs et doivent être consignés
  dans le `CHANGELOG`.

---

## 0. P0 — fermer la consolidation

Ce jalon décide quand le projet peut reprendre une expansion régulière. Il ne
transforme pas les 37 profils actuels en plafond : il rend leur socle mesurable.

- [ ] **Décider explicitement la fermeture de la fenêtre de consolidation.**
  Attendre les critères de sortie des jalons 1 à 3, consigner la décision
  produit, puis autoriser le travail P1/P2 sans affaiblir les preuves acquises.

**Critère de sortie :** les runs complets AArch64 et x86-64 sont propres, les
mesures courantes sont archivées et chaque couple hôte/CPU a une décision
`auto` — promotion ou refus — justifiée par conformité et performance.

---

## 1. P0 — rétablir le plancher de preuve

### 1.1 Régressions et fixtures connues

- [ ] **Conserver le prochain échec de `gui_smoke_test` comme preuve.** Si le
  flake de renommage revient, capturer les chemins et l'`errno` désormais
  imprimés avant d'envisager un retry.

### 1.2 Assets et gates reproductibles

- [ ] **Enregistrer un census AArch64 sans soft-skip pour les gates à
  assets optionnels.** Sur le poste x86-64 les deux gates s'exécutent
  (machfs était déjà dans `.venv-tools` — seuls les shebangs des scripts
  console sont morts depuis le renommage `POM68K`→`pom68k` ;
  `q605_cdrom_etalon` court sur `cd/MAC_OS_8-1_RETAIL_0.ISO` en fallback,
  `cd/MacOS_86.iso` reste le nom préféré). Reste à constater l'équivalent
  sur l'hôte AArch64 et, si souhaité, recréer la venv pour réparer les
  shebangs.
- [ ] **Exécuter une journée avec
  `-DPOM68K_PRODUCT_LLE_GATES=ON`.** Vérifier notamment la fusion des labels,
  le backend A64 effectivement sélectionné et le census exécutés/soft-skips.

### 1.3 Toolchains et hygiène bloquante

- [ ] **Lire le premier artefact `pom68k-leak-census` et rendre la nightly
  bloquante.** L'étape publie désormais son rapport ; après une première
  lecture réelle, retirer `continue-on-error`.

**Critère de sortie :** deux runs complets consécutifs par architecture, sans
fixture sale, timeout posé sur sa limite, rouge inexpliqué ni soft-skip masqué.

---

## 2. P0 — construire l'observatoire de performances

Les mesures utilisent un budget invité fixe, des empreintes identiques et une
alternance ABBA dans le même environnement. Aucun boot etalon ne sert de
chronomètre. Le résultat archivé doit distinguer moteur demandé/réel, temps
mur, cycles invités, corps générés, fenêtres, moteur, thunks, MMU/cache, LLE et
causes de fallback.

### 2.1 Ligne de base courante

- [ ] **Rebaseliner la jambe A64 sur l'hôte AArch64.** La jambe x64 est
  publiée (CHANGELOG 2026-09-02 (fifth) : Q605 ×0,96/×1,88/×6,29, LC II
  ×1,96/×2,20/×6,22, empreintes identiques, plancher NULL 0,1 %) ; rejouer
  le même protocole ABBA sur l'autre poste avant toute comparaison
  inter-architectures.
- [ ] **Attaquer le poste n°1 du profil temporel : la traduction.**
  `tools/profile_census.py` sur SimCity/Speedometer (CHANGELOG 2026-09-02
  (sixth)) attribue ~30-34 % du temps à Moira translate + thunks mémoire,
  ~2 % au hashtable de dispatch et ~5,7 % au M68HC05 seul ; la densité du
  code généré n'est PAS dominante (compilation 0,2 %) et reste parquée.
  Toute optimisation part de ces chiffres, pas d'un histogramme.

### 2.2 Matériel cible et variables produit

- [ ] **Établir la ligne de base POM68K sur un vrai Pi 400.** Utiliser
  `jit_bench` et `jit_bench_lcii` à budget invité fixe, archiver les
  empreintes, le ratio temps réel et la provenance du build.
- [ ] **Rejouer sur ce Pi l'A/B release contre native/PGO/LTO.** Séparer
  `-mtune`, LTO et PGO, garder le même workload et exiger les mêmes empreintes
  entre les bras.
- [ ] **Dispatcher `pi400.yml` une fois pour Cortex-A76/Pi 5.** Vérifier que
  l'artefact armv8.2-a est produit, exécutable et distinct du plancher A72 ;
  archiver le run même si les extensions ISA ne changent pas le code généré.
- [ ] **Rendre le turbo scriptable puis mesurer AppleTalk à bras égaux.**
  Ajouter un réglage injecté équivalent au clic GUI, puis comparer hub
  activé/désactivé avec `POM68K_SPEED_LOG`, même image, même mode turbo et même
  lancement.

**Critère de sortie :** une ligne de base reproductible Q605/LC II existe sur
A64 et x64, et chaque coût important peut être attribué avant d'ouvrir une
optimisation.

---

## 3. P0 — terminer et qualifier le JIT conforme

Contrat à préserver : les générateurs déclarent la conformité 68030+68040 ;
`auto` choisit A64 pour 030+040 et x64 pour 040 seulement. Un fallback Moira
est conforme. Une promotion `auto` exige locksteps, tiers CPU, etalon complet,
empreintes identiques et gain supérieur au bruit de mesure.

### 3.1 Effets architecturaux 68040

- [ ] **Encoder la position des polls IPL 68040 dans l'IR.** Remplacer le
  fallback conservateur des instructions mémoire à plusieurs polls par une
  séquence ordonnée accès/poll/faute/validation, prouvée sur A64 et x64 avec
  cache actif et livraison périphérique comparée.
- [ ] **Rendre le `JSR` cache-actif 68040 transactionnel.** Modéliser lecture
  du mot cible, push, faute et effets I-cache dans leur ordre architectural
  afin de récupérer le chemin natif sans rendre le replay non pristine.
- [ ] **Étendre l'échéancier événementiel Q605 à un troisième périphérique.**
  Ajouter son flush MMIO, sa dette sérialisée et des gates de timing avant
  toute activation par défaut.

### 3.2 Gardes, mémoire et coût partagé

- [ ] **Retirer conformément le hint CACR/SMC sur VASP, RBV et MSC.** Prouver
  pour chaque carte que toute écriture RAM — CPU, pseudo-DMA et périphériques
  — traverse `CodeGuard::note()`, ajouter le gate d'inventaire, puis retirer
  le flush carte par carte. Ne pas extrapoler la preuve
  `V8Memory::kJitStoreInventoryComplete`.
- [ ] **Étudier `PFLUSHA` et le retry d'armement seulement après profil.**
  Toute réduction des bumps ou du backoff doit garder les locksteps 030/040 :
  le moment où une fenêtre s'arme est observable sur 68040.
- [ ] **Prouver le cache de dispatch et l'éviction générationnelle posés
  le 2026-09-02.** L'implémentation est dans l'arbre (CHANGELOG
  2026-09-02 (ninth) : cache direct-mapped 65 536 slots devant le
  hashtable + éviction des blocs froids à saturation, compteurs dans
  `censusPhase()`), mais la session s'est terminée par un gel de l'hôte
  AVANT la validation. Seul smoke vert depuis :
  `jit_asset_free_lockstep_test` passe post-commit sur x86-64 (lockstep
  040 synthétique, slow=0, scénario 384 évictions). RESTE EXIGÉ avant
  toute revendication de gain : locksteps 030/040 sous invités réels,
  tier etalon complet sous défauts, ABBA sur le run Rogue (les chiffres
  3,3 % / 992 M vs 34 M viennent du stderr de la session, pas d'un
  artefact conservé — re-mesurer). Contexte : le
  profil 040 (CHANGELOG 2026-09-02 (eighth)) attribue ~34,5 % du run
  Rogue à executeUntil + hashtable + dispatchBlockKey + armWindow pour
  7,6 % de corps générés.
- [ ] **Compacter `mmu040InstrStart`.** Mesuré à 3,26 % du run Rogue 040
  (le plafond du gain est connu) ; voir si les remises à zéro adjacentes
  et le pack CCR peuvent devenir un ou deux stores larges sans changer
  l'état privé vérifié par les locksteps.
- [ ] **Profiler puis isoler les stores à masque nul.** N'ouvrir une
  spécialisation conforme qu'après un profil temporel et des preuves
  empreinte/compteurs/gates identiques.

### 3.3 Qualification 68030 par hôte

- [ ] **Revalider le chemin A64/68030 sur l'arbre courant.** Rejouer le tier
  natif complet et une ABBA sur hôte silencieux après les changements de
  garde de slices ; conserver le score de profit 64 seulement si le gain
  dépasse le plancher de bruit.
- [ ] **Décider la re-promotion x64/68030.** Comparer les modes d'accès 1 et 2
  dans le même binaire, puis exiger les locksteps natifs, `ctest -L m030` et
  le tier etalon complet sur x86-64 avant d'ajouter 030 à
  `X64Backend::caps().autoFamilies`.
- [ ] **Fermer l'écart de timing/admission 68030 entre A64 et x64.** La parité
  opcode est déjà zéro ; le travail restant concerne les pénalités i-cache,
  les positions d'accès et les sous-familles PI/PD encore refusées. Toute
  règle 68k commune doit vivre dans l'IR/coût partagé, pas dans un emitter.
- [ ] **Promouvoir la suite Speedometer uniquement depuis un profil
  temporel.** Garder `C029`, `08D1` et les lectures périphériques variables
  dans Moira tant qu'un contrat de phase n'est pas démontré ; ne pas créer des
  lowerings pour des familles absentes du corpus.

**Critère de sortie :** les fallbacks cache-actifs 040 encore identifiés sont
fermés ou justifiés, et chaque couple hôte/CPU possède une décision `auto`
séparée, étayée par conformité produit et gain mesuré.

---

## 4. P1 — prouver le produit au-delà du Finder

Ce jalon commence après le plancher de preuve. Chaque scénario vérifie un
observable invité ou un artefact persistant, pas seulement un compteur interne.

### 4.1 Applications et oracles invités

- [ ] **Créer un gate applicatif soutenu avec SimCity 2000.** Transformer le
  census de développement en scénario reproductible qui lance réellement
  l'application, simule une charge et vérifie CPU, écran, SCSI et progression
  fonctionnelle sous interpréteur et moteur accéléré.
- [ ] **Introduire Retro68 comme oracle invité différentiel.** Construire des
  sondes Toolbox/Device Manager/XPRAM et comparer les mêmes binaires sous MAME
  et POM68K.
- [ ] **Ajouter un etalon invité « Redémarrer ».** Déclencher Finder →
  Redémarrer et vérifier un nouveau boot complet, afin de couvrir le chemin
  Toolbox jusqu'au firmware.
- [ ] **Faire une passe GUI réelle des save states.** Sauver/restaurer une
  machine bootée, vérifier les panneaux spécifiques et la reprise des
  périphériques host-backed.

### 4.2 Médias et persistance invités

- [ ] **Ajouter le beyond-boot floppy du Q605.** Monter le média depuis
  l'invité, effectuer une action observable et vérifier l'état persistant.
- [ ] **Ajouter une écriture floppy initiée par l'invité sur LC II.** Attendre
  le montage réel, créer/modifier un fichier, éjecter, rouvrir l'image et
  vérifier le contenu ; conserver `floppy_persist_test` comme garde device.
- [ ] **Ajouter un etalon invité de montage/boot 1,44 Mo sur LC II.** Utiliser
  `disks35/Stuffit_Expander_5.5.dsk` pour couvrir le SWIM1 depuis le système.
- [ ] **Ajouter la cellule Plus/System 4.1 sur floppy.** Étendre `bootPlus`
  avec un chemin `insertDisk` distinct du boot SCSI.

### 4.3 Matrice, entrée, audio et profils particuliers

- [ ] **Élargir `finder_boot_matrix` aux profils récents.** Commencer par
  Classic II, Macintosh LC, Color Classic, LC III et la famille AIO, avec une
  cellule par image validée.
- [ ] **Créer `duo230_input_etalon`.** Exercer clavier et trackball au niveau
  invité, indépendamment du persist.
- [ ] **Créer `duo230_sleep_etalon`.** Couvrir la préparation système,
  l'arrêt CPU au clamshell, le flush disque, puis un réveil complet.
- [ ] **Ajouter une preuve de rendu audio ASC.** Couvrir la sortie audible, le
  tempo et la variation de hauteur ; les tests de registres/IRQ ne suffisent
  pas.
- [ ] **Rejouer le repro GISTPERSO/SimCity sur LC II.** Utiliser
  `LCII_HOLD_KEYS`, confirmer que la course de démarrage a disparu ou rouvrir
  le différentiel avec une image de référence propre.
- [ ] **Vérifier le chemin LC II sans FPU.** Refaire le boot 68030 avec
  `POM68K_NOFPU` avant de modifier UniversalInfo/defaultRSRCs.

**Critère de sortie :** chaque grande famille matérielle supportée possède au
moins un scénario déterministe au-delà du boot, et les scénarios sensibles au
CPU passent sous interpréteur et moteur accéléré avec le même résultat.

---

## 5. P1 — rendre la preuve portable et exploitable

- [ ] **Décider le sort du JIT x64 sous Windows.** Soit documenter
  `threaded` comme solution permanente, soit porter prologue, registres
  non-volatils, appels de thunks et shadow space à l'ABI Win64 derrière le
  tier asset-free `jit-fast`.
- [ ] **Lire la première exécution MSVC de `asset-none`.** Traiter tout rouge
  comme une découverte de configuration et archiver le log.
- [ ] **Installer un runner auto-hébergé avec les assets.** Rendre le palier
  `full` déclenchable par push et publier `LastTest.log` ainsi que le census
  exécutés/soft-skips.
- [ ] **Ajouter ASan sur trois boots réels.** Couvrir un 68000, un 68030 et un
  68040 sans forcer artificiellement `POM68K_CPU_ENGINE`.
- [ ] **Mesurer la couverture avec les assets.** Publier le rapport et une
  nouvelle `coverage-zero.txt` représentant le produit, pas seulement le
  palier CI.
- [ ] **Activer LTO dans les artefacts.** Retirer le veto macOS après
  validation `lipo` et ajouter `/GL` + `/LTCG` au build MSVC.

**Critère de sortie :** `asset-none` est vert sur toutes les toolchains prises
en charge et un palier `full` reproductible existe sur au moins un hôte A64 et
un hôte x64 portant les assets.

---

## 6. P1 — approfondir la fidélité matérielle et LLE

Tout ajout LLE part d'une trace ROM/pilote, d'un observable invité ou d'un
consommateur réel. Une approximation plus large sans preuve n'est pas un gain.

### 6.1 Contrôleurs et échéanciers

- [ ] **Étendre les commandes Cuda du Q605/LC 475 uniquement depuis des traces
  ROM/pilote.** Le prochain travail porte sur le timing pin-level 040 et les
  commandes réellement observées, pas sur une nouvelle approximation.
- [ ] **Compléter le low tier SCC seulement avec un consommateur.** Ajouter
  l'échantillonnage série asynchrone avec un transport réel, les variantes
  8530/85C30/ESCC lorsqu'une machine les demande, puis WR9 VIS/NV et DPLL avec
  gates ; préserver le comportement LLAP déjà plus complet que l'oracle MAME.
- [ ] **Créer un store de piste flux de première classe.** Faire survivre les
  flux écrits hors cadence à un commit et revalider l'arithmétique de zones
  GCR ; exiger un symptôme ou un corpus avant d'élargir le modèle.
- [ ] **Décider les échéanciers Mac II et Duo avec un gate sensible à la
  gigue.** Garder les options expérimentales tant qu'aucun observable ne
  justifie leur coût ; comparer état, débit et jitter avant un défaut produit.

### 6.2 LC II / V8

- [ ] **Terminer DFAC et la sortie audio.** Ajouter le resampling sur horloge
  hôte et vérifier le tempo sur une session GUI longue.
- [ ] **Comparer le bus et les timings à du matériel réel.** Couvrir IRQ, VBL,
  VIA et mémoire, puis diagnostiquer l'assombrissement après très longue
  exécution.

### 6.3 Mac Plus et compacts

- [ ] **Affiner VIA/RTC.** Modéliser les latences T1/T2/IFR à un cycle,
  l'alignement E-clock/IACK et initialiser le RTC GUI depuis l'hôte sans rendre
  les tests non déterministes.
- [ ] **Câbler le second lecteur 800K.** Fournir un deuxième `SonyDrive` aux
  machines concernées et ajouter un gate de sélection externe.
- [ ] **Gérer les préfixes clavier `$79`.** Couvrir pavé numérique et flèches
  M0110 là où le protocole les exige.
- [ ] **Améliorer la précision sonore.** Lire le buffer par scanline, modéliser
  le PWM disque et la courbe de volume analogique.
- [ ] **Étendre SCSI et série.** Ajouter plusieurs targets/LUNs, REQUEST SENSE
  après CHECK CONDITION et un transport série hôte PTY/TCP.
- [ ] **Ajouter des etalons pixel-accurate et un build WASM.** Garder les
  assets privés soft-skippables et comparer des captures stables.

---

## 7. P2 — compléter les services réseau

L'ordre interne est : prouver le parcours invité existant, fermer les défauts
de protocole observés, puis ajouter les extensions et les contrôles GUI.

### 7.1 AppleTalk interne

- [ ] **Créer un etalon Chooser AppleShare.** Monter le serveur interne depuis
  un vrai invité et vérifier une opération de fichier.
- [ ] **Fermer la course de défense d'adresse `lapENQ`.** Fournir un chemin de
  contrôle qui répond dans le délai LLAP sans détourner le sens `express`,
  puis ajouter un gate où un invité sonde l'adresse du serveur.
- [ ] **Persister les CNID.** Stocker l'identité catalogue dans `.AppleDB` ou
  les sidecars AppleDouble et vérifier sa stabilité après redémarrage.
- [ ] **Étendre le sous-ensemble AFP.** Ajouter Desktop DB, CopyFile,
  CatSearch, chemins DID relatifs et, si requis, AFP 3/UTF-8.
- [ ] **Ajouter des UAM sûrs.** Implémenter DHX/random-number lorsqu'un invité
  refuse le cleartext.
- [ ] **Compléter PAP.** Ajouter polling de statut, configuration des files et
  sélection CUPS dans le GUI.
- [ ] **Compléter MacIP.** Ajouter ICMP sortant, réassemblage IP et window
  scaling TCP.
- [ ] **Rendre la configuration réseau éditable dans le GUI.** Partage,
  serveur, imprimante, subnet/DNS et révélation du spool.

### 7.2 LocalTalk inter-processus

- [ ] **Exécuter une session AppleShare complète sur le bridge réel.** Lancer
  netatalk/TashRouter, monter « Input » depuis le Chooser et vérifier un
  transfert.
- [ ] **Tester l'interop Mini vMac LToUDP.** Utiliser le même groupe multicast
  et vérifier les deux directions.

### 7.3 Ethernet SCSI/Link

- [ ] **Tester un vrai driver DaynaPort.** Installer le driver, configurer
  MacTCP et valider le jeu de commandes contre un invité réel.
- [ ] **Découpler l'uplink de `AtalkHub`.** Faire fonctionner le NAT Ethernet
  même avec `POM68K_APPLETALK=0`.
- [ ] **Ajouter EtherTalk.** Implémenter AARP et DDP sur 802.3/SNAP pour sortir
  AppleTalk du SCC.
- [ ] **Ajouter le contrôle DaynaPort au GUI.** Attacher/détacher et choisir
  l'ID SCSI sans variable d'environnement.
- [ ] **Sérialiser DaynaPort au prochain bump de format.** Restaurer anneau RX,
  configuration et liaison hôte dans `SaveStateMachines.*`.
- [ ] **Porter la carte aux autres plateformes SCSI.** Ajouter membre,
  accessor, configuration et gate par famille.

---

## 8. P2 — compléter les médias optiques

- [ ] **Automatiser une installation depuis CD.** Piloter l'Installer jusqu'au
  disque cible, redémarrer dessus et vérifier le Finder.
- [ ] **Établir la règle des images 512/2048 octets.** Comparer le comportement
  des hybrides et bare-HFS avec un vrai pilote/MAME avant de modifier le
  montage.
- [ ] **Ajouter CDDA.** Implémenter TOC audio, PLAY/PAUSE et le chemin sonore
  vers l'ASC avec un gate consommateur.
- [ ] **Supporter les rips 2352 et `.cue/.bin`.** Ajouter les pistes multiples
  sans accepter silencieusement un format mal interprété.

---

## 9. P2 — étendre les machines par réutilisation prouvée

Les gates du Duo 230 précèdent ses variantes ; les nouveaux contrôleurs sont
prouvés sur le premier profil consommateur avant d'être généralisés.

### 9.1 Famille Duo et PowerBook

- [ ] **Ajouter les variantes Duo 210 et 250.** Exploiter les IDs déjà
  présents, introduire la sélection de profil et ajouter les lignes
  catalogue/gates après `duo230_input_etalon` et `duo230_sleep_etalon`.
- [ ] **Ajouter Duo 270c puis Duo 280.** Modéliser respectivement CSC couleur
  et le chemin 68040 après validation des variantes proches.
- [ ] **Ajouter PowerBook 150.** Implémenter framebuffer LCD/GSC, IDE, box ID
  et PMU 68HC05 à partir de sa ROM.
- [ ] **Ajouter PowerBook 140–180 puis Portable/PB100.** Introduire le Power
  Manager M50753 et le framebuffer LCD comme nouvelle brique partagée.

### 9.2 Bus et stockage

- [ ] **Étendre NuBus et la vidéo sur slot.** Porter les cartes au-delà du
  Toby Mac II vers IIx/IIcx/IIci et les Quadra concernés.
- [ ] **Ajouter le target ATA/IDE du Q630/LC580.** Brancher un disque et créer
  un gate de boot qui n'utilise pas SCSI.

---

## 10. P2 — recherche conditionnelle

- [ ] **Définir puis expérimenter le profil d'accélération non conforme.** Ne
  commencer qu'après les mesures conformes du jalon 3 ; définir d'abord les
  critères fonctionnels, les défauts par famille, le `purity mode` des gates
  et un opt-in qui ne puisse jamais contaminer l'oracle.
