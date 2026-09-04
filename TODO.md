# TODO — feuille de route

Ce fichier contient uniquement du travail ouvert. Les résultats, mesures,
fausses pistes et décisions terminées vivent dans `CHANGELOG.md` et
`CHANGELOG_INDEX.md`. Les détails d'implémentation vivent dans `DEV.md`,
`src/jit/POM68K_JIT.md`, `docs/JIT_BRINGUP.md` et les notes vendor.

`STATUS.md` est généré depuis les manifests CMake et reste la source de vérité
pour les gates. Toute tâche fermée quitte ce fichier dans le même changement
qui ajoute son entrée au `CHANGELOG`.

## Ordre d'exécution

Quatre paliers, ordonnés par ce qui débloque le reste — pas par intérêt
technique. Le classement date du 2026-09-02 et repose sur deux profils
temporels (`CHANGELOG` 2026-09-02 (sixth) et (eighth)) et sur l'audit du même
jour (`CHANGELOG` 2026-09-02 (tenth)).

**A — limite externe acceptée, sans faux vert.** La CI et la nightly ont été
réparées, mais quatre images créées sur l'ancien hôte x86-64 restent non
reproductibles bit à bit. La décision du 2026-09-03 autorise la suite sans
faire passer cette asymétrie de preuve pour une réussite ; elle est consignée
dans le `CHANGELOG` et n'est plus une tâche locale exécutable.

**B — terminer le moteur.** Le travail de performance et de conformité qui
reste, ordonné par du temps mesuré et non par un nombre d'opcodes.

**C — en faire un produit.** Les scénarios au-delà du boot, la portabilité de
la preuve, le matériel cible et une première version publiée.

**D — pas maintenant.** Du vrai travail, hors chemin critique. Il ne passe
pas devant les priorités moteur et produit des paliers B et C.

Règles de travail :

- une optimisation dépend d'un profil temporel reproductible ;
- la couverture native n'est pas une preuve de conformité ;
- une nouvelle machine dépend d'un gate produit réutilisable de sa plateforme ;
- un gate asset-backed n'est probant que si son census indique qu'il a été
  exécuté, sans soft-skip ;
- l'interpréteur reste l'oracle et `threaded` le plancher portable ;
- les critères de sortie d'un palier sont cumulatifs et doivent être consignés
  dans le `CHANGELOG`.

---

## B — P0 — terminer et qualifier le moteur

Contrat à préserver : les générateurs déclarent la conformité 68030+68040 ;
`auto` choisit A64 pour 030+040 et x64 pour 040 seulement. Un fallback Moira
est conforme. Une promotion `auto` exige locksteps, tiers CPU, etalon complet,
empreintes identiques et gain supérieur au bruit de mesure.

Les mesures utilisent un budget invité fixe, des empreintes identiques et une
alternance ABBA dans le même environnement. Aucun boot etalon ne sert de
chronomètre. Le résultat archivé distingue moteur demandé/réel, temps mur,
cycles invités, corps générés, fenêtres, moteur, thunks, MMU/cache, LLE et
causes de fallback.

### B.1 Le poste n°1 du 68040 : la pompe d'échéancier, et finir les fenêtres

Le profil post-cache (`CHANGELOG` 2026-09-03 (sixth), jambe macOS de
l'instrument) re-classe le poste : le runtime moteur est retombé à 11,6 % de
la phase gameplay (hashtable hors top-45, `dispatchBlockKey` 0,24 %), les
corps générés montent à 41 %, et le premier agrégat hors corps générés est la
pompe tick/échéancier des périphériques (~36 %). L'allongement des fenêtres
cache-actives reste un chantier de conformité réel, mais son levier temps est
borné à ~14 points ; l'échéancier événementiel — troisième item — porte
désormais la masse mesurée.

- [ ] **Décider le sort de l'admission late-poll : x64 et rentabilité.** La
  position des polls est dans l'IR et l'admission A64
  (accès/poll/faute/validation) est prouvée conforme — mais mesurée −6,3 %
  sur le bench cache-actif, car la classe admise est les boucles de poll
  chaudes du boot (`CHANGELOG` 2026-09-03 (seventh)). Le knob
  `POM68K_JIT_040_LATE_POLL` reste opt-in. Reste : rejouer les locksteps
  x64 sur l'hôte x86-64 avant toute admission x64, et ne rouvrir le défaut
  que si un workload cache-actif montre le gain — ou après une dé-admission
  adaptative des sites qui manquent chroniquement.
- [ ] **Décider la double interrogation d'échéance SCC.** Le profil après
  fast-path ROM place `Scc8530::cyclesToNextEvent()` à 246/6 840 échantillons
  exclusifs et le fan-out Q605 à 135. Instrumenter une variante qui réutilise
  uniquement la borne déjà calculée entre `tick()` et le réarmement ; la
  retirer si son A/B ne dépasse pas le bruit, comme le cache SCC général déjà
  refusé.

### B.2 Le poste n°1 du 68030 : la traduction, pas le générateur

- [ ] **Attaquer la traduction et les thunks mémoire.**
  `tools/profile_census.py` sur SimCity/Speedometer (`CHANGELOG` 2026-09-02
  (sixth)) attribue ~30-34 % du temps à Moira translate + thunks mémoire
  (`mmuFetchWord` 6,1 %, `mmuRead<2/4>` 8,1 %, `V8Memory::read16/write16`
  5,7 %), ~2 % au hashtable de dispatch et ~5,7 % au M68HC05 seul. Les corps
  générés pèsent 21-24 % et la compilation 0,2 % : le levier est du côté du
  fork Moira et de la carte mémoire, pas du générateur. La densité du code
  généré reste parquée, et cette ligne est la conséquence directe du profil —
  pas un pressentiment.

### B.3 Qualification 68030 par hôte

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

### B.4 Gardes, mémoire et coût partagé

- [ ] **Statuer sur le hint CACR/SMC de Sonora.** VASP, RBV et MSC sont
  faits (`CHANGELOG` 2026-09-03 (ninth), (tenth), (eleventh)) ;
  `store_inventory_test` pinne chaque inventaire revendiqué au niveau
  source et tient Sonora — hors liste d'origine mais porteur du même
  hint — à son flush tant qu'il n'a pas son propre audit (SWIM2 intégré,
  famille AIO). Même exigence : toute écriture RAM traverse
  `CodeGuard::note()`, la ligne s'ajoute au gate, puis le flush se retire.
  Ne pas extrapoler la preuve d'une autre carte.
- [ ] **Étudier `PFLUSHA` et le retry d'armement seulement après profil.**
  Toute réduction des bumps ou du backoff doit garder les locksteps 030/040 :
  le moment où une fenêtre s'arme est observable sur 68040.
- [ ] **Compacter `mmu040InstrStart`.** Mesuré à 3,26 % du run Rogue 040 — le
  plafond du gain est donc connu et petit. Voir si les remises à zéro
  adjacentes et le pack CCR peuvent devenir un ou deux stores larges sans
  changer l'état privé vérifié par les locksteps. À faire après B.1.
- [ ] **Profiler puis isoler les stores à masque nul.** N'ouvrir une
  spécialisation conforme qu'après un profil temporel et des preuves
  empreinte/compteurs/gates identiques.

**Critère de sortie du palier B :** les fallbacks cache-actifs 040 encore
identifiés sont fermés ou justifiés, chaque coût important est attribuable
avant qu'une optimisation ne s'ouvre, et chaque couple hôte/CPU possède une
décision `auto` séparée, étayée par conformité produit et gain mesuré.

---

## C — P1 — en faire un produit

Chaque scénario vérifie un observable invité ou un artefact persistant, pas
seulement un compteur interne.

### C.1 Applications et oracles invités

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

### C.2 Médias et persistance invités

- [ ] **Ajouter le beyond-boot floppy du Q605.** Monter le média depuis
  l'invité, effectuer une action observable et vérifier l'état persistant.
- [ ] **Ajouter une écriture floppy initiée par l'invité sur LC II.** Attendre
  le montage réel, créer/modifier un fichier, éjecter, rouvrir l'image et
  vérifier le contenu ; conserver `floppy_persist_test` comme garde device.
- [ ] **Ajouter un etalon invité de montage/boot 1,44 Mo sur LC II.** Utiliser
  `disks35/Stuffit_Expander_5.5.dsk` pour couvrir le SWIM1 depuis le système.
- [ ] **Ajouter la cellule Plus/System 4.1 sur floppy.** Étendre `bootPlus`
  avec un chemin `insertDisk` distinct du boot SCSI.

### C.3 Matrice, entrée, audio et profils particuliers

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

### C.4 Rendre la preuve portable et exploitable

- [ ] **Installer un runner auto-hébergé avec les assets.** Rendre le palier
  `full` déclenchable par push et publier `LastTest.log` ainsi que le census
  exécutés/soft-skips. C'est ce qui transforme une preuve personnelle en
  preuve vérifiable par un tiers.
- [ ] **Ajouter ASan sur trois boots réels.** Couvrir un 68000, un 68030 et un
  68040 sans forcer artificiellement `POM68K_CPU_ENGINE`. Bloqué par A.2.
- [ ] **Mesurer la couverture avec les assets.** Publier le rapport et une
  nouvelle `coverage-zero.txt` représentant le produit, pas seulement le
  palier CI.
- [ ] **Activer LTO dans les artefacts.** Retirer le veto macOS après
  validation `lipo` et ajouter `/GL` + `/LTCG` au build MSVC.
- [ ] **Décider le sort du JIT x64 sous Windows.** Soit documenter `threaded`
  comme solution permanente, soit porter prologue, registres non-volatils,
  appels de thunks et shadow space à l'ABI Win64 derrière le tier asset-free
  `jit-fast`.

### C.5 Matériel cible Raspberry Pi

- [ ] **Établir la ligne de base POM68K sur un vrai Pi 400.** Utiliser
  `jit_bench` et `jit_bench_lcii` à budget invité fixe, archiver les
  empreintes, le ratio temps réel et la provenance du build.
- [ ] **Rejouer sur ce Pi l'A/B release contre native/PGO/LTO.** Séparer
  `-mtune`, LTO et PGO, garder le même workload et exiger les mêmes empreintes
  entre les bras.
- [ ] **Dispatcher `pi400.yml` une fois pour Cortex-A76/Pi 5.** Les deux
  exécutions au dossier (2026-08-08) ont toutes deux tourné `MCPU:
  cortex-a72` : la jambe A76 n'a jamais été produite. Vérifier que l'artefact
  armv8.2-a est produit, exécutable et distinct du plancher A72 ; archiver le
  run même si les extensions ISA ne changent pas le code généré.
- [ ] **Rendre le turbo scriptable puis mesurer AppleTalk à bras égaux.**
  Ajouter un réglage injecté équivalent au clic GUI, puis comparer hub
  activé/désactivé avec `POM68K_SPEED_LOG`, même image, même mode turbo et
  même lancement.

### C.6 Publier

- [ ] **Publier une première version.** Zéro tag, zéro release, alors que
  `README.md` annonce une page Releases et que `.github/workflows/release.yml`
  est complet. Des utilisateurs sont le chercheur de bugs le moins cher
  disponible, et il n'y en a aucun. À déclencher après les preuves produit
  minimales de C, sans attendre les quatre références externes de A.
- [ ] **Lire la première exécution MSVC de `asset-none`.** Elle vit dans le
  job `windows` de `.github/workflows/release.yml` et ne peut donc pas exister
  avant une publication : traiter tout rouge comme une découverte de
  configuration et archiver le log.

**Critère de sortie du palier C :** chaque grande famille matérielle supportée
possède au moins un scénario déterministe au-delà du boot, les scénarios
sensibles au CPU passent sous interpréteur et moteur accéléré avec le même
résultat, `asset-none` est vert sur toutes les toolchains prises en charge, un
palier `full` reproductible existe sur un hôte A64 et un hôte x64 portant les
assets, et une version est téléchargeable.

---

## D — P2 — pas maintenant

Du travail réel, hors chemin critique. Chaque section garde ses items pour ne
pas les reperdre, mais aucune ne passe devant les P0 de B et C — et les
nouvelles machines (D.4) attendent en plus le palier C, faute de quoi elles
ajoutent des gates sans approfondir les plateformes déjà annoncées.

### D.1 Fidélité matérielle et LLE

Tout ajout LLE part d'une trace ROM/pilote, d'un observable invité ou d'un
consommateur réel. Une approximation plus large sans preuve n'est pas un gain.

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
- [ ] **Terminer DFAC et la sortie audio du LC II / V8.** Ajouter le
  resampling sur horloge hôte et vérifier le tempo sur une session GUI longue.
- [ ] **Comparer le bus et les timings V8 à du matériel réel.** Couvrir IRQ,
  VBL, VIA et mémoire, puis diagnostiquer l'assombrissement après très longue
  exécution.
- [ ] **Affiner VIA/RTC sur les compacts.** Modéliser les latences T1/T2/IFR à
  un cycle, l'alignement E-clock/IACK et initialiser le RTC GUI depuis l'hôte
  sans rendre les tests non déterministes.
- [ ] **Câbler le second lecteur 800K.** Fournir un deuxième `SonyDrive` aux
  machines concernées et ajouter un gate de sélection externe.
- [ ] **Gérer les préfixes clavier `$79`.** Couvrir pavé numérique et flèches
  M0110 là où le protocole les exige.
- [ ] **Améliorer la précision sonore des compacts.** Lire le buffer par
  scanline, modéliser le PWM disque et la courbe de volume analogique.
- [ ] **Étendre SCSI et série.** Ajouter plusieurs targets/LUNs, REQUEST SENSE
  après CHECK CONDITION et un transport série hôte PTY/TCP.
- [ ] **Ajouter des etalons pixel-accurate et un build WASM.** Garder les
  assets privés soft-skippables et comparer des captures stables.

### D.2 Services réseau

L'ordre interne est : prouver le parcours invité existant, fermer les défauts
de protocole observés, puis ajouter les extensions et les contrôles GUI.

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
- [ ] **Exécuter une session AppleShare complète sur le bridge réel.** Lancer
  netatalk/TashRouter, monter « Input » depuis le Chooser et vérifier un
  transfert.
- [ ] **Tester l'interop Mini vMac LToUDP.** Utiliser le même groupe multicast
  et vérifier les deux directions.
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

### D.3 Médias optiques

- [ ] **Automatiser une installation depuis CD.** Piloter l'Installer jusqu'au
  disque cible, redémarrer dessus et vérifier le Finder.
- [ ] **Établir la règle des images 512/2048 octets.** Comparer le comportement
  des hybrides et bare-HFS avec un vrai pilote/MAME avant de modifier le
  montage.
- [ ] **Ajouter CDDA.** Implémenter TOC audio, PLAY/PAUSE et le chemin sonore
  vers l'ASC avec un gate consommateur.
- [ ] **Supporter les rips 2352 et `.cue/.bin`.** Ajouter les pistes multiples
  sans accepter silencieusement un format mal interprété.

### D.4 Nouvelles machines par réutilisation prouvée

Les gates du Duo 230 précèdent ses variantes ; les nouveaux contrôleurs sont
prouvés sur le premier profil consommateur avant d'être généralisés. Aucune
de ces lignes ne s'ouvre avant que C.3 ait donné au Duo ses deux etalons.

- [ ] **Ajouter les variantes Duo 210 et 250.** Exploiter les IDs déjà
  présents, introduire la sélection de profil et ajouter les lignes
  catalogue/gates après `duo230_input_etalon` et `duo230_sleep_etalon`.
- [ ] **Ajouter Duo 270c puis Duo 280.** Modéliser respectivement CSC couleur
  et le chemin 68040 après validation des variantes proches.
- [ ] **Ajouter PowerBook 150.** Implémenter framebuffer LCD/GSC, IDE, box ID
  et PMU 68HC05 à partir de sa ROM.
- [ ] **Ajouter PowerBook 140–180 puis Portable/PB100.** Introduire le Power
  Manager M50753 et le framebuffer LCD comme nouvelle brique partagée.
- [ ] **Étendre NuBus et la vidéo sur slot.** Porter les cartes au-delà du
  Toby Mac II vers IIx/IIcx/IIci et les Quadra concernés.
- [ ] **Ajouter le target ATA/IDE du Q630/LC580.** Brancher un disque et créer
  un gate de boot qui n'utilise pas SCSI.

### D.5 Recherche conditionnelle

- [ ] **Définir puis expérimenter le profil d'accélération non conforme.** Ne
  commencer qu'après les mesures conformes du palier B ; définir d'abord les
  critères fonctionnels, les défauts par famille, le `purity mode` des gates
  et un opt-in qui ne puisse jamais contaminer l'oracle.
