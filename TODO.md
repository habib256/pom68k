# TODO

Ce fichier contient uniquement du travail ouvert. Les résultats, mesures,
fausses pistes et décisions terminées vivent dans `CHANGELOG.md` et
`CHANGELOG_INDEX.md`. Les détails d'implémentation vivent dans `DEV.md`,
`src/jit/POM68K_JIT.md`, `docs/JIT_BRINGUP.md` et les notes vendor.

Les nombres de gates ne sont pas recopiés ici : `STATUS.md` est généré depuis
les manifests CMake et constitue la source de vérité. Toute tâche fermée quitte
ce fichier dans le même changement qui ajoute son entrée au `CHANGELOG`.

Priorités actuelles :

1. conformité et mesure du JIT sur les chemins produit 68030/68040 ;
2. profondeur des gates au-delà du Finder et reproductibilité des assets ;
3. fidélité LLE et performances mesurées sur le matériel cible ;
4. nouvelles machines sans affaiblir les preuves existantes.

---

## 0. Séquencement de la consolidation architecturale

- [ ] **Décider explicitement la fermeture de la fenêtre de consolidation.**
  Les deux architectures ont maintenant exécuté leur palier `unit` sur un
  hôte portant les assets ; consigner la décision produit, puis mettre à jour
  le séquencement des sections 0·A, 2, 3 et 7 sans transformer les 37 profils
  actuels en plafond du projet.

---

## 0·A. Direction produit — vitesse conforme d'abord

- [ ] **Établir la ligne de base POM68K sur un vrai Pi 400.** Utiliser
  `jit_bench` et `jit_bench_lcii` à budget invité fixe, archiver les
  empreintes, le ratio temps réel et la provenance du build ; ne pas employer
  un boot etalon comme chronomètre.
- [ ] **Rejouer sur ce Pi l'A/B release contre native/PGO/LTO.** Séparer
  `-mtune`, LTO et PGO, garder le même workload et exiger les mêmes
  empreintes entre les bras.
- [ ] **Dispatcher `pi400.yml` une fois pour Cortex-A76/Pi 5.** Vérifier que
  l'artefact armv8.2-a est produit, exécutable et distinct du plancher A72 ;
  archiver le run même si les extensions ISA ne changent pas le code généré.
- [ ] **Rendre le turbo scriptable puis mesurer AppleTalk à bras égaux.**
  Ajouter un réglage injecté équivalent au clic GUI, puis comparer hub
  activé/désactivé avec `POM68K_SPEED_LOG`, même image, même mode turbo et
  même lancement.
- [ ] **Étendre l'échéancier événementiel Q605 à un troisième périphérique.**
  Ajouter son flush MMIO, sa dette sérialisée et des gates de timing avant
  toute activation par défaut.
- [ ] **Profiler puis isoler les stores à masque nul.** N'ouvrir une
  spécialisation conforme qu'après un profil temporel et des preuves
  empreinte/compteurs/gates identiques.

---

## 0·B. Revue externe — hygiène mesurable

- [ ] **Nettoyer le recensement GCC et armer `-Werror`.** Corriger les sites
  encore signalés par g++ 13, faire reconnaître les diagnostics tels que
  `[-Warray-bounds=]`, relancer la recette CI exacte, puis activer
  `-DPOM68K_WERROR=ON` dans le job GCC.
- [ ] **Lire le premier rapport de fuites Linux et rendre la nightly
  bloquante.** Conserver `detect_leaks=1`, publier le rapport comme artefact
  et ne rendre l'étape obligatoire qu'après une première lecture réelle.

---

## 1. Régressions, fixtures et marges de gates

- [ ] **Réparer les fixtures de `macii_persist_etalon` et
  `q605_afp_live_etalon`.** Restaurer les deux images à leurs identités
  `assets.lock` — ou enregistrer délibérément de nouvelles références —,
  vérifier le bit de volume propre, puis rejouer les deux gates avant toute
  chasse dans le code.
- [ ] **Donner une marge mesurée à `iivx_persist_etalon`.** Refaire un run
  isolé, expliquer son coût relatif ou relever son `TIMEOUT` avec la même
  politique que les autres persists.
- [ ] **Éliminer la préférence implicite pour `hdv/boot.vhd` dans les gates
  et outils concernés.** Identifier ceux réellement calibrés sur « MacPack » ;
  pour les autres, préférer une petite référence versionnée et propre. Ajouter
  une référence propre dédiée lorsque les signatures sont image-spécifiques.
- [ ] **Installer ou documenter les deux assets absents du poste de preuve.**
  Fournir `cd/MacOS_86.iso` et le module `machfs` de `.venv-tools`, puis
  enregistrer un census sans soft-skip correspondant.
- [ ] **Conserver le prochain échec de `gui_smoke_test` comme preuve.** Si le
  flake de renommage revient, capturer les chemins et l'`errno` désormais
  imprimés avant d'envisager un retry.
- [ ] **Recalibrer `finder_boot_matrix`.** Exécuter le sweep sur toutes les
  images visées, remplacer le seuil SCSI brut du Mac II par
  `FinderSignature`, puis seulement enregistrer les cellules stables.
- [ ] **Ajouter une preuve de rendu audio ASC.** Couvrir la sortie audible, le
  tempo et la variation de hauteur ; les tests de registres/IRQ ne suffisent
  pas.

---

## 2. Profondeur de validation au-delà du boot

- [ ] **Créer un gate applicatif soutenu avec SimCity 2000.** Transformer le
  census de développement en scénario reproductible qui lance réellement
  l'application, simule une charge et vérifie CPU, écran, SCSI et progression
  fonctionnelle sous interpréteur et moteur accéléré.
- [ ] **Ajouter le beyond-boot floppy du Q605.** Monter le média depuis
  l'invité, effectuer une action observable et vérifier l'état persistant.
- [ ] **Ajouter une écriture floppy initiée par l'invité sur LC II.** Attendre
  le montage réel, créer/modifier un fichier, éjecter, rouvrir l'image et
  vérifier le contenu ; conserver `floppy_persist_test` comme garde device.
- [ ] **Élargir `finder_boot_matrix` aux profils récents.** Commencer par
  Classic II, Macintosh LC, Color Classic, LC III et la famille AIO, avec une
  cellule par image validée.
- [ ] **Ajouter la cellule Plus/System 4.1 sur floppy.** Étendre `bootPlus`
  avec un chemin `insertDisk` distinct du boot SCSI.
- [ ] **Créer `duo230_input_etalon`.** Exercer clavier et trackball au niveau
  invité, indépendamment du persist.

---

## 3. JIT — second moteur d'exécution

Source de vérité à préserver dans chaque changement : les deux générateurs
déclarent la conformité 68030+68040 ; `auto` choisit A64 pour 030+040, x64
pour 040 seulement, et `threaded` reste le plancher portable. Un fallback
vers Moira est conforme ; la couverture native n'est jamais une preuve à elle
seule.

- [ ] **Rebaseliner le commit courant avec une ABBA complète.** Mesurer
  interpréteur, `threaded` et codegen sur Q605 ainsi que LC II, à budget fixe
  et empreinte identique. Publier séparément A64 et x64 ; les chiffres
  antérieurs aux gardes cache 040 du 2026-09-01 ne doivent pas être présentés
  comme une mesure de l'arbre courant.
- [ ] **Produire un profil temporel sous charge applicative.** Utiliser
  SimCity/Speedometer et attribuer le temps aux corps générés, fenêtres,
  moteur, thunks, MMU/cache et LLE. Ne plus ordonner le travail par histogramme
  brut de fallbacks.
- [ ] **Décider la re-promotion x64/68030.** Comparer les modes d'accès 1 et 2
  dans le même binaire, puis exiger les locksteps natifs, `ctest -L m030` et
  le tier etalon complet sur x86-64 avant d'ajouter 030 à
  `X64Backend::caps().autoFamilies`.
- [ ] **Revalider le chemin A64/68030 sur l'arbre courant.** Rejouer le tier
  natif complet et une ABBA sur hôte silencieux après les changements de
  garde de slices ; conserver le score de profit 64 seulement si le gain
  dépasse le plancher de bruit.
- [ ] **Achever la preuve `accessClockBias` sur x86-64.** Rejouer le tier
  etalon interrompu avec les admissions par défaut, enregistrer
  exécutés/soft-skips et vérifier que les gates épinglent réellement le
  générateur attendu.
- [ ] **Retirer conformément le hint CACR/SMC sur VASP, RBV et MSC.** Prouver
  pour chaque carte que toute écriture RAM — CPU, pseudo-DMA et périphériques
  — traverse `CodeGuard::note()`, ajouter le gate d'inventaire, puis retirer
  le flush carte par carte. Ne pas extrapoler la preuve
  `V8Memory::kJitStoreInventoryComplete`.
- [ ] **Fermer l'écart de timing/admission 68030 entre A64 et x64.** La parité
  opcode est déjà zéro ; le travail restant concerne les pénalités i-cache,
  les positions d'accès et les sous-familles PI/PD encore volontairement
  refusées. Toute règle 68k commune doit vivre dans l'IR/coût partagé, pas
  dans un seul emitter.
- [ ] **Encoder la position des polls IPL 68040 dans l'IR.** Remplacer le
  fallback conservateur des instructions mémoire à plusieurs polls par une
  séquence accès/poll prouvée sur A64 et x64, avec cache actif et livraison
  périphérique comparée.
- [ ] **Rendre le `JSR` cache-actif 68040 transactionnel.** Modéliser lecture
  du mot cible, push, faute et effets I-cache dans leur ordre architectural
  afin de récupérer le chemin natif sans rendre le replay non pristine.
- [ ] **Étudier `PFLUSHA` et le retry d'armement seulement après profil.**
  Toute réduction des bumps ou du backoff doit garder les locksteps 030/040 :
  le moment où une fenêtre s'arme est observable sur 68040.
- [ ] **Promouvoir la suite Speedometer uniquement depuis un profil
  temporel.** Garder `C029`, `08D1` et les lectures périphériques variables
  dans Moira tant qu'un contrat de phase n'est pas démontré ; ne pas créer des
  lowerings pour des familles absentes du corpus.
- [ ] **Compacter `mmu040InstrStart`.** Mesurer si les remises à zéro
  adjacentes et le pack CCR peuvent devenir un ou deux stores larges sans
  changer l'état privé vérifié par les locksteps.
- [ ] **Re-mesurer la densité du code généré avant toute action.** Si elle
  redevient dominante, évaluer stubs froids partagés et branches courtes ;
  sinon laisser cet item derrière les coûts MMU/cache/LLE observés.

---

## 4. Fidélité LLE

- [ ] **Étendre les commandes Cuda du Q605/LC 475 uniquement depuis des traces
  ROM/pilote.** Le prochain travail porte sur le timing pin-level 040 et les
  commandes réellement observées, pas sur une nouvelle approximation.
- [ ] **Compléter le low tier SCC seulement avec un consommateur.** Ajouter
  l'échantillonnage série asynchrone avec un transport réel, les variantes
  8530/85C30/ESCC lorsqu'une machine les demande, puis WR9 VIS/NV et DPLL avec
  gates ; préserver le comportement LLAP déjà plus complet que l'oracle MAME.
- [ ] **Ajouter un etalon invité « Redémarrer ».** Déclencher Finder →
  Redémarrer et vérifier un nouveau boot complet, afin de couvrir le chemin
  Toolbox jusqu'au firmware.
- [ ] **Créer un store de piste flux de première classe.** Faire survivre les
  flux écrits hors cadence à un commit et revalider l'arithmétique de zones
  GCR ; exiger un symptôme ou un corpus avant d'élargir le modèle.
- [ ] **Décider les échéanciers Mac II et Duo avec un gate sensible à la
  gigue.** Garder les options expérimentales tant qu'aucun observable ne
  justifie leur coût ; comparer état, débit et jitter avant un défaut produit.

---

## 5. Backlogs par machine

### LC II / V8

- [ ] **Ajouter un etalon invité de montage/boot 1,44 Mo.** Utiliser
  `disks35/Stuffit_Expander_5.5.dsk` pour couvrir le SWIM1 depuis le système.
- [ ] **Terminer DFAC et la sortie audio.** Ajouter le resampling sur horloge
  hôte et vérifier le tempo sur une session GUI longue.
- [ ] **Comparer le bus et les timings à du matériel réel.** Couvrir IRQ, VBL,
  VIA et mémoire, puis diagnostiquer l'assombrissement après très longue
  exécution.
- [ ] **Rejouer le repro GISTPERSO/SimCity.** Utiliser `LCII_HOLD_KEYS`,
  confirmer que la course de démarrage a disparu ou rouvrir le différentiel
  avec une image de référence propre.
- [ ] **Vérifier le chemin LC II sans FPU.** Refaire le boot 68030 avec
  `POM68K_NOFPU` avant de modifier UniversalInfo/defaultRSRCs.

### Mac Plus et compacts

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

### CD-ROM

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

## 6. Réseau

### AppleTalk interne

- [ ] **Fermer la course de défense d'adresse `lapENQ`.** Fournir un chemin
  de contrôle qui répond dans le délai LLAP sans détourner le sens
  `express`, puis ajouter un gate où un invité sonde l'adresse du serveur.
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
- [ ] **Créer un etalon Chooser AppleShare.** Monter le serveur interne depuis
  un vrai invité et vérifier une opération de fichier.

### LocalTalk inter-processus

- [ ] **Exécuter une session AppleShare complète sur le bridge réel.** Lancer
  netatalk/TashRouter, monter « Input » depuis le Chooser et vérifier un
  transfert.
- [ ] **Tester l'interop Mini vMac LToUDP.** Utiliser le même groupe multicast
  et vérifier les deux directions.

### Ethernet SCSI/Link

- [ ] **Tester un vrai driver DaynaPort.** Installer le driver, configurer
  MacTCP et valider le jeu de commandes contre un invité réel.
- [ ] **Ajouter le contrôle DaynaPort au GUI.** Attacher/détacher et choisir
  l'ID SCSI sans variable d'environnement.
- [ ] **Sérialiser DaynaPort au prochain bump de format.** Restaurer anneau RX,
  configuration et liaison hôte dans `SaveStateMachines.*`.
- [ ] **Ajouter EtherTalk.** Implémenter AARP et DDP sur 802.3/SNAP pour sortir
  AppleTalk du SCC.
- [ ] **Découpler l'uplink de `AtalkHub`.** Faire fonctionner le NAT Ethernet
  même avec `POM68K_APPLETALK=0`.
- [ ] **Porter la carte aux autres plateformes SCSI.** Ajouter membre,
  accessor, configuration et gate par famille.

---

## 7. Nouvelles machines

- [ ] **Ajouter les variantes Duo 210 et 250.** Exploiter les IDs déjà présents,
  introduire la sélection de profil et ajouter les lignes catalogue/gates.
- [ ] **Ajouter Duo 270c puis Duo 280.** Modéliser respectivement CSC couleur
  et le chemin 68040 après validation des variantes proches.
- [ ] **Créer `duo230_sleep_etalon`.** Couvrir la préparation système,
  l'arrêt CPU au clamshell, le flush disque, puis un réveil complet.
- [ ] **Ajouter PowerBook 150.** Implémenter framebuffer LCD/GSC, IDE, box ID et
  PMU 68HC05 à partir de sa ROM.
- [ ] **Ajouter PowerBook 140–180 puis Portable/PB100.** Introduire le Power
  Manager M50753 et le framebuffer LCD comme nouvelle brique partagée.
- [ ] **Étendre NuBus et la vidéo sur slot.** Porter les cartes au-delà du Toby
  Mac II vers IIx/IIcx/IIci et les Quadra concernés.
- [ ] **Ajouter le target ATA/IDE du Q630/LC580.** Brancher un disque et créer
  un gate de boot qui n'utilise pas SCSI.

---

## 8. Architecture transverse

- [ ] **Faire une passe GUI réelle des save states.** Sauver/restaurer une
  machine bootée, vérifier les panneaux spécifiques et la reprise des
  périphériques host-backed.
- [ ] **Définir puis expérimenter le profil d'accélération non conforme.**
  Rester bloqué derrière les mesures conformes de § 3 ; définir d'abord les
  critères fonctionnels, les défauts par famille et le purity mode des gates.
- [ ] **Introduire Retro68 comme oracle invité différentiel.** Construire des
  sondes Toolbox/Device Manager/XPRAM et comparer les mêmes binaires sous MAME
  et POM68K.
- [ ] **Exécuter une journée avec
  `-DPOM68K_PRODUCT_LLE_GATES=ON`.** Vérifier notamment la fusion des labels
  et la présence effective du backend A64 requis.

---

## 9. Hygiène documentaire

- [ ] **Appliquer à `CLAUDE.md` sa propre limite de taille.** Ajouter un budget
  dans `tools/file_size_budget.txt`, déplacer les données générables vers
  `STATUS.md` et les détails internes vers `DEV.md`, puis garder
  `docs_test` vert.

---

## 10. Portabilité et reproductibilité de la preuve

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
- [ ] **Activer LTO dans les artefacts.** Retirer le veto macOS après validation
  `lipo` et ajouter `/GL` + `/LTCG` au build MSVC.
