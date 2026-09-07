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

**B — terminer le moteur.** **Clos le 2026-09-07** : ses trois critères de
sortie sont remplis et consignés (`CHANGELOG` 2026-09-07 (second)). Les
études moteur encore conditionnées à un profil temporel vivent en D.6 ; elles
ne sont plus sur le chemin critique.

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

## B — P0 — terminer et qualifier le moteur — CLOS

Le codegen JIT conformant est terminé au sens que ce fichier lui donnait.
Contrat en vigueur : les deux générateurs natifs déclarent la conformité
68030+68040 (`guestFamilies`), la parité opcode est zéro et gatée
(`jit_backend_parity_test`), `auto` choisit A64 pour 030+040 sur AArch64 et
x64 pour 030+040 sur x86-64 non-Windows, chaque promotion portée par son
propre tier vert et son propre census exécuté ; tout opcode non émis est un
rejeu Moira exact — y compris, depuis le 2026-09-07, la fenêtre générale FPU
à l'intérieur des blocs — et l'interpréteur reste l'oracle.

Critère de sortie, rempli (`CHANGELOG` 2026-09-07 (second)) :

- les fallbacks cache-actifs 040 encore identifiés sont fermés ou justifiés :
  JSR à lecture ordonnée, polls IPL positionnés (admission conforme, knob
  opt-in mesuré −6,3 %), miss de ligne froid rejoué entier ;
- chaque coût important est attribuable avant qu'une optimisation ne
  s'ouvre : profils whole-route macOS/Linux, census par phase et sampler
  attaché à la phase (`phase_sample.py`) ;
- chaque couple hôte/CPU possède une décision `auto` séparée, étayée par
  conformité produit et gain mesuré (x86-64/030 restauré le 2026-09-06,
  AArch64/030 requalifié le même jour, 040 sur les deux hôtes).

Leçons conservées ici parce qu'elles gouvernent toute mesure future :

- **mesurer le bras qui expédie** — jusqu'au 2026-09-06 le plan B.2
  chiffrait ses tranches sur `POM68K_JIT_BACKEND=x64`, alors override
  diagnostique, et classait 4ᵉ sur 5 la tranche qui vaut −10 % ;
- **ne comparer que des bras du même binaire** — une configuration fraîche
  active LTO et `-mcpu=native`, `build/` non ; l'écart de 4–5 % s'est lu
  comme une régression de knob avant d'être identifié ;
- **un compteur de fallbacks n'est pas un gain** — l'extension QuickDraw des
  versions de shift retirait 70,5 % des fallbacks et perdait 1,75 %.

---

## C — P1 — en faire un produit

Chaque scénario vérifie un observable invité ou un artefact persistant, pas
seulement un compteur interne.

### C.1 Applications et oracles invités

`lcii_simcity_etalon` (2026-09-07) est le premier scénario applicatif
soutenu : lancement prouvé par `CurApName`, simulation avancée sous budget
invité fixe, sauvegarde écrite, et les deux jambes interpréteur/A64 comparées
dans le même processus. Il marche aussi le chemin de la course de démarrage
GISTPERSO de 2026-07-18 sans touche maintenue et atteint le Finder à chaque
jambe : ce repro n'est plus une tâche ouverte, il est gaté.

- [ ] **Introduire Retro68 comme oracle invité différentiel.** Construire des
  sondes Toolbox/Device Manager/XPRAM et comparer les mêmes binaires sous MAME
  et POM68K.
- [ ] **Faire une passe GUI réelle des save states.** Sauver/restaurer une
  machine bootée, vérifier les panneaux spécifiques et la reprise des
  périphériques host-backed.

### C.2 Médias et persistance invités

Les écritures floppy initiées par l'invité sont gatées depuis le 2026-09-07
sur les deux contrôleurs : `lcii_floppy_etalon` (SWIM1) et
`q605_hotfloppy_etalon` (SWIM2) créent un dossier sur la disquette montée, la
rangent depuis le Finder (Cmd-Y, ce qui vide le cache et éjecte) et
retrouvent le nom dans le fichier hôte rouvert. La leçon : une éjection
forcée par l'hôte est une disquette arrachée d'une machine qui tourne — le
catalogue restait dans le cache invité.

- [ ] **Monter une 1,44 Mo depuis le System, sur LC II (SWIM1) et Q605
  (SWIM2).** Reproducteurs : `POM68K_BEYOND=floppy
  POM68K_FLOPPY_IMG=disks35/Stuffit_Expander_5.5.dsk lcii_beyond_etalon` et
  `POM68K_FLOPPY_IMG=… q605_hotfloppy_etalon` (image brute de 1 474 560
  octets + 84 de queue, tronquée dans la copie privée). Ce que la chasse
  du 2026-09-07 a établi (`CHANGELOG` (eighth), traces dans
  `scratchpad/2026-09-07/floppy/`) : l'engine ISM du SWIM1 décode le MFM
  correctement — identifiants A1 A1 A1 FE, champs FB, CRC0 en place, et le
  secteur 3 rendu au pilote est octet pour octet le MDB de l'image — mais
  seulement une fois le lecteur en mode MFM, et c'est là que les deux
  pilotes se contredisent : la ROM du LC II strobe `CA2=1` sur (0,1,1) puis
  attend « MFM mode on » = 1 (sous la table de MAME ce strobe est *GCR on*
  et le pilote boucle sans jamais armer ACTION), tandis que le .Sony de
  7.5.5 sur Q605 associe son setup MFM à `$9` et son setup GCR à `$D` —
  la polarité de MAME. Le bit F (« 2M ») n'est pas ce que les pilotes
  consultent : inverser sa polarité ne change ni la décision LC II ni le
  « Format : Macintosh 800K » du dialogue Q605. Avec la polarité inversée
  sur le seul chemin CA, le LC II lit le MDB et ne monte pas ; le Q605,
  chemin ROM (`q605_floppy_boot_etalon`) vert, échoue sous le pilote du
  System avec le dialogue d'initialisation. Prochaine étape : vérifier le
  câblage CA2 du LC II (VIA/V8) contre le schéma, puis tracer ce que le
  File Manager lit après le MDB. La table reste celle de MAME
  (`iwm_write_test` l'épingle) ; `Swim1::ismStats()` compte désormais ce
  que le pilote dépile et ce que l'engine produit.
- [ ] **Ajouter la cellule Plus/System 4.1 sur floppy.** Bloqué par l'actif :
  `hdv/System 4.1.dsk` est une image SCSI de 1,5 Mo, pas une disquette ;
  aucune 800 K System 4.1 n'est présente. `bootPlus` reçoit son chemin
  `insertDisk` le jour où l'image existe.

### C.3 Matrice, entrée, audio et profils particuliers

Fermés le 2026-09-07 (`CHANGELOG` (sixth)) : `finder_boot_matrix` porte les
cellules Classic II, LC, Color Classic, LC III (System 7.5) et LC 520
(GISTPERSO) ; `duo230_input_etalon` juge clavier matriciel et trackball par
KeyMap et le global Mouse ; `lcii_asc_chime_etalon` rend le chime de boot
par le ring de sortie de l'ASC et en épingle durée, bande et variation de
hauteur.

- [ ] **Créer `duo230_sleep_etalon`.** Couvrir la préparation système,
  l'arrêt CPU au clamshell, le flush disque, puis un réveil complet.

### C.4 Rendre la preuve portable et exploitable

- [ ] **Installer un runner auto-hébergé avec les assets.** Rendre le palier
  `full` déclenchable par push et publier `LastTest.log` ainsi que le census
  exécutés/soft-skips. C'est ce qui transforme une preuve personnelle en
  preuve vérifiable par un tiers.

### C.5 Matériel cible Raspberry Pi

Fait le 2026-09-07 : `pi400.yml` dispatché pour `cortex-a76` (run
34079617764, ELF aarch64 distinct de l'artefact A72 du 2026-08-08) ;
`POM68K_TURBO` rend le turbo du GUI scriptable, et la mesure hub AppleTalk
activé/désactivé à bras égaux ne montre aucun coût au-dessus de la
dispersion de la jauge (`CHANGELOG` (sixth)). Ce qui reste exige un Pi
physique.

- [ ] **Établir la ligne de base POM68K sur un vrai Pi 400.** Utiliser
  `jit_bench` et `jit_bench_lcii` à budget invité fixe, archiver les
  empreintes, le ratio temps réel et la provenance du build.
- [ ] **Rejouer sur ce Pi l'A/B release contre native/PGO/LTO.** Séparer
  `-mtune`, LTO et PGO, garder le même workload et exiger les mêmes empreintes
  entre les bras.

### C.6 Publier

- [ ] **Publier une première version.** Zéro tag, zéro release, alors que
  `README.md` annonce une page Releases et que `.github/workflows/release.yml`
  est complet. Des utilisateurs sont le chercheur de bugs le moins cher
  disponible, et il n'y en a aucun. À déclencher après les preuves produit
  minimales de C, sans attendre les quatre références externes de A.
Le job `windows` de `release.yml` est vert depuis le 2026-09-07 (run
34154944920) : `POM68K.exe` en LTO MSVC, arbre de gates compilé, tier
asset-free complet sous MSVC, ZIP autonome et `--version`. La première
publication n'attend plus que le tag.

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

### D.6 Moteur — études conditionnées à un profil temporel

Reliquat du palier B clos. Aucune de ces lignes n'est une lacune de
conformité ; chacune n'ouvre qu'avec un profil temporel reproductible et se
mesure en ABBA intra-binaire, empreintes identiques.

- [ ] **Décider le sort de l'admission late-poll : rentabilité seulement.** La
  position des polls est dans l'IR et l'admission A64
  (accès/poll/faute/validation) est prouvée conforme — mais mesurée −6,3 %
  sur le bench cache-actif, car la classe admise est les boucles de poll
  chaudes du boot (`CHANGELOG` 2026-09-03 (seventh)). Le knob
  `POM68K_JIT_040_LATE_POLL` reste opt-in. La jambe x86-64 a rejoué tous les
  locksteps x64 et les deux tiers CPU sans soft-skip et prouvé le knob
  inerte octet pour octet sur x64 (`CHANGELOG` 2026-09-04 (sixth)) : la
  précondition de rejeu est levée. Ne rouvrir le défaut que si un workload
  cache-actif montre le gain — ou après une dé-admission adaptative des
  sites qui manquent chroniquement.
- [ ] **Ne pas rouvrir l'écart d'admission 68030 sans profil temporel neuf.**
  Chiffré le 2026-09-06 sur le chemin qui expédie et **refusé** : la parité
  opcode est zéro et gatée (`jit_backend_parity_test`), donc les refus sont
  communs aux deux générateurs, pas un écart a64/x64. Le seau non supporté
  vaut 562 194 instructions dont `2F70` (`MOVE.L idx(A0) → d16(A7)`) fait
  70 % ; rapporté au 8,64 % de rejeu d'instruction entière du profil, son
  plafond est **0,87 %** et celui de *tout* le seau **1,24 %**, contre un
  plancher de 10 ‰. La moitié timing de l'item est close autrement : les deux
  tiers verts sous le défaut restauré et six locksteps à 120 000 pas
  comparent précisément les compteurs i-cache et les positions d'accès.
  Reste la convention, pas un défaut : une règle 68k commune vit dans
  l'IR/coût partagé, jamais dans un emitter.
  Évidence : `scratchpad/2026-09-05/b3probe/ADMISSION_GAP.md`.
- [ ] **Isoler ou amplifier les familles Speedometer avant toute promotion.**
  La navigation est réparée et le harnais sélectionne séparément CPU,
  Benchmark Mix, FPU et Color QuickDraw (les cinq profondeurs). A64 et
  `threaded` terminent chaque famille aux mêmes trames, empreintes, écrans et
  comptes SCSI. Les trois profils temporels complets déjà capturés restent
  toutefois dominés par le boot : 33 322–33 512 échantillons on-CPU, fallback
  interprété stable à 5,54–5,64 %, mais seulement 0,274–0,277 s de CPU utile.
  Les familles plus longues rendent enfin une capture attachée à la phase
  praticable ; elles ne transforment pas le bucket whole-route en attribution.
  Premier tri : QuickDraw est natif à 99,7 % mais produit 1,88 M rejouements
  de gardes de shifts. Étendre leur cache multi-version les retire et baisse
  tous les fallbacks de 70,5 %, mais un ABBA donne **+1,75 % plus lent** :
  candidat retiré, ne pas le ressusciter depuis le compteur seul. La ligne
  FPU (438 964 instructions `UNSAFE`, 15,2 % de sa phase) est **fermée le
  2026-09-07** : la fenêtre générale `$F200-$F23F` est membre de bloc rejoué
  exactement, **−11,6 %** sur la phase FPU isolée et −2,3 % sur le Mix en
  ABBA intra-binaire, empreintes identiques sur les quatre bras. Leçon à
  garder : ne comparer que des bras du **même binaire** — une configuration
  fraîche active LTO et `-mcpu=native`, `build/` non, et l'écart de 4–5 % qui
  en résulte s'est d'abord lu comme une régression du knob.
  Avant de rouvrir un lowering, répéter une famille dans l'invité ou
  échantillonner sa phase seule (`phase_sample.py` attache `sample` à la
  première trame de la phase). `C029`, `08D1` et les lectures périphériques
  variables restent dans Moira jusque-là. Évidence :
  `scratchpad/2026-09-06/a64-m030/SPEEDOMETER_TIME_PROFILE.md`,
  `SPEEDOMETER_SUITE.md` dans le même répertoire et
  `scratchpad/2026-09-06/fpu-member/FPU_MEMBER.md`.
- [ ] **Étudier `PFLUSHA` et le retry d'armement seulement après profil.**
  Toute réduction des bumps ou du backoff doit garder les locksteps 030/040 :
  le moment où une fenêtre s'arme est observable sur 68040.
- [ ] **Compacter `mmu040InstrStart`.** Mesuré à 3,26 % du run Rogue 040 — le
  plafond du gain est donc connu et petit. Voir si les remises à zéro
  adjacentes et le pack CCR peuvent devenir un ou deux stores larges sans
  changer l'état privé vérifié par les locksteps. À faire après la décision late-poll ci-dessus.
- [ ] **Profiler puis isoler les stores à masque nul.** N'ouvrir une
  spécialisation conforme qu'après un profil temporel et des preuves
  empreinte/compteurs/gates identiques.

### D.5 Recherche conditionnelle

- [ ] **Définir puis expérimenter le profil d'accélération non conforme.** Ne
  commencer qu'après les mesures conformes du palier B ; définir d'abord les
  critères fonctionnels, les défauts par famille, le `purity mode` des gates
  et un opt-in qui ne puisse jamais contaminer l'oracle.
