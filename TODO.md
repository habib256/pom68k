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

**A — réparer le socle de preuve.** Les mécanismes de qualité du projet sont
en panne : CI rouge depuis le 2026-08-23, nightly rouge, et la preuve complète
n'existe que sur une seule architecture. Rien de ce qui suit n'a de valeur
vérifiable tant que ce palier reste ouvert.

**B — terminer le moteur.** Le travail de performance et de conformité qui
reste, ordonné par du temps mesuré et non par un nombre d'opcodes.

**C — en faire un produit.** Les scénarios au-delà du boot, la portabilité de
la preuve, le matériel cible et une première version publiée.

**D — pas maintenant.** Du vrai travail, hors chemin critique. Y toucher avant
la fermeture du palier A ajoute des gates à un registre qui ne tourne pas
propre sur l'une de ses deux architectures.

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

## A — P0 — réparer le socle de preuve

### A.1 Rendre la CI verte, et la garder verte

- [ ] **Réparer l'étape census de `.github/workflows/ci.yml:104-110`.**
  `--fail-on-skip` a été calibré sur l'hôte de développement, qui porte les
  assets privés. Sur un runner nu, `ncr5380_test`, `cuda_restart_test` et
  `m68hc05_test` soft-skippent sur des assets alors qu'ils sont enregistrés
  `asset-none`, et `jit_store_guard_a64_test` soft-skippe sur l'ISA hôte : le
  compte ne peut jamais valoir zéro. Le défaut est l'étiquette, pas le
  runner — reclasser ces gates en assets optionnels, puis exiger zéro
  soft-skip inattendu.
- [ ] **Faire de la CI verte une condition de push.** Une fois les deux points
  ci-dessus fermés, tout rouge redevient une découverte à traiter le jour
  même. Dix jours de rouge ont enterré deux régressions réelles.

### A.2 Rendre la nightly lisible

- [ ] **Réparer la compilation ASan.** `src/MachineCatalog.h:141` est refusé
  comme « not a constant expression » par GCC 13 sous les drapeaux sanitizer :
  la jambe `asan-ubsan` ne construit pas et n'a donc jamais rien exercé. C'est
  le préalable réel de « ASan sur trois boots » (C.4) et du basculement de la
  leak census.
- [ ] **Fermer la course de données `gui_smoke_test` sous TSan.** La preuve
  attendue existe, chaque nuit, et ce n'est pas le flake de renommage :
  `src/GuiSmokeScenario.cpp:22` réassigne une `std::string` du `SaveStateSlot`
  sur le fil GUI pendant que le fil machine la lit dans
  `src/SaveStateSlot.h:59` via `src/MachineHost.h:506`. C'est le contrat de
  propriété GUI/machine de `DEV.md` § 6, violé, avec sa pile conservée dans
  les artefacts nightly.
- [ ] **Lire le premier artefact `pom68k-leak-census`, puis retirer
  `continue-on-error`.** L'étape publie son rapport ; la lecture reste due, et
  dépend de la jambe ASan ci-dessus.

### A.3 Rendre la preuve reproductible sur les deux architectures

- [ ] **Transporter les quatre références nées sur l'hôte x86-64.** La récolte
  du 2026-09-02 sur la sauvegarde externe a fermé `HD20SC` et `7.5.5` aux
  identités verrouillées exactes (`asset_lock_test` : 5 échecs → 3) et posé
  `cd/MAC_OS_8-1_RETAIL_0.ISO` pour le gate CD optionnel. Le reliquat —
  `GISTPERSO-boot.vhd`, `MacOS-8.1-boot.vhd`, `System 7.1 HD.dsk` (divergents)
  et `System 7.5 HD.dsk` (absent) — est constitué des volumes flushés ou créés
  PAR L'INVITÉ sur l'hôte x86-64 les 2026-09-01/02 ; un re-flush local ne
  redonnerait pas les mêmes octets. Copier ces quatre fichiers (~660 Mo)
  depuis `hdv/ref/` de l'hôte x86-64, ou décider un lock par-hôte.
- [ ] **Exécuter deux runs complets consécutifs sur l'hôte AArch64.** Même
  protocole que la jambe x86-64 (`CHANGELOG` 2026-09-02) : aucune fixture
  sale, aucun timeout posé sur sa borne, aucun rouge inexpliqué, et le couple
  exécutés / soft-skips consigné dans `STATUS.md`.
- [ ] **Enregistrer un census AArch64 sans soft-skip pour les gates à assets
  optionnels.** Sur l'hôte x86-64 les deux gates s'exécutent (`machfs` était
  déjà dans `.venv-tools` — seuls les shebangs des scripts console sont morts
  depuis le renommage `POM68K`→`pom68k` ; `q605_cdrom_etalon` court sur
  `cd/MAC_OS_8-1_RETAIL_0.ISO` en fallback, `cd/MacOS_86.iso` reste le nom
  préféré). Reste à constater l'équivalent ici et, si souhaité, recréer la
  venv pour réparer les shebangs.
- [ ] **Exécuter une journée avec `-DPOM68K_PRODUCT_LLE_GATES=ON`.** Vérifier
  la fusion des labels, le backend A64 effectivement sélectionné et le census
  exécutés/soft-skips.

### A.4 Prouver ou retirer le cache de dispatch 68040

- [ ] **Prouver le cache de dispatch et l'éviction générationnelle posés le
  2026-09-02.** L'implémentation est dans l'arbre (`CHANGELOG` 2026-09-02
  (ninth) : cache direct-mapped de 65 536 slots devant le hashtable, éviction
  des blocs froids à saturation, compteurs dans `censusPhase()`). Seul vert
  acquis : `jit_asset_free_lockstep_test` sur x86-64 — lockstep 040
  synthétique, slow=0, scénario de 384 évictions inclus. Restent exigés avant
  toute revendication de gain : locksteps 030/040 sous invités réels, tier
  etalon complet sous défauts, ABBA sur le run Rogue. Les chiffres 3,3 % et
  992 M contre 34 M viennent d'un stderr non conservé. C'est du code
  d'éviction non prouvé : il ne survit pas à un échec de mesure.

### A.5 Fermer le palier

- [ ] **Décider explicitement la fermeture de la fenêtre de consolidation.**
  Attendre les critères de sortie de A.1 à A.4, consigner la décision produit,
  puis autoriser le travail des paliers C et D sans affaiblir les preuves
  acquises. Les 37 profils actuels ne sont pas un plafond : ce palier rend
  leur socle mesurable.

**Critère de sortie du palier A :** la CI et la nightly sont vertes et
redeviennent bloquantes, les runs complets AArch64 et x86-64 sont propres et
archivés, et le cache de dispatch est prouvé ou retiré.

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

### B.1 Le poste n°1 du 68040 : trouver les blocs, pas les exécuter

Le profil (`CHANGELOG` 2026-09-02 (eighth)) attribue 45,4 % du run Rogue au
runtime moteur pour 7,6 % de corps générés, dont environ un tiers du run passé
à chercher des blocs. Après A.4, l'autre moitié du levier est
l'allongement des fenêtres : c'est le seul chantier de performance qui ait
cette masse derrière lui.

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

- [ ] **Revalider le chemin A64/68030 sur l'arbre courant.** Rejouer le tier
  natif complet et une ABBA sur hôte silencieux après les changements de garde
  de slices ; conserver le score de profit 64 seulement si le gain dépasse le
  plancher de bruit.
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

- [ ] **Retirer conformément le hint CACR/SMC sur VASP, RBV et MSC.** Prouver
  pour chaque carte que toute écriture RAM — CPU, pseudo-DMA et périphériques
  — traverse `CodeGuard::note()`, ajouter le gate d'inventaire, puis retirer
  le flush carte par carte. Ne pas extrapoler la preuve
  `V8Memory::kJitStoreInventoryComplete`.
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

### B.5 Ligne de base courante

- [ ] **Rebaseliner la jambe A64 sur l'hôte AArch64.** La jambe x64 est
  publiée (`CHANGELOG` 2026-09-02 (fifth) : Q605 ×0,96/×1,88/×6,29, LC II
  ×1,96/×2,20/×6,22, empreintes identiques, plancher NULL 0,1 %) ; rejouer le
  même protocole ABBA sur l'autre poste avant toute comparaison
  inter-architectures. Dépend de A.3 : sans références identiques, les deux
  jambes ne mesurent pas le même invité.

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
  disponible, et il n'y en a aucun. À déclencher après la fermeture du
  palier A.
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
pas les reperdre, mais aucune ne s'ouvre avant la fermeture du palier A — et
les nouvelles machines (D.4) attendent en plus le palier C, faute de quoi
elles ajoutent des gates à un registre qui ne tourne pas propre partout.

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
