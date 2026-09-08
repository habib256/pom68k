# TODO — feuille de route

Ce fichier contient uniquement du travail ouvert. Les résultats, mesures,
fausses pistes et décisions terminées vivent dans `CHANGELOG.md` et
`CHANGELOG_INDEX.md`. Les détails d'implémentation vivent dans `DEV.md`,
`src/jit/POM68K_JIT.md`, `docs/JIT_BRINGUP.md` et les notes vendor.

`STATUS.md` est généré depuis les manifests CMake et reste la source de vérité
pour les gates. Toute tâche fermée quitte ce fichier dans le même changement
qui ajoute son entrée au `CHANGELOG`.

## Statut

- **Palier B (terminer et qualifier le moteur) — CLOS** le 2026-09-07. Les
  deux générateurs natifs déclarent la conformité 68030+68040, la parité
  opcode est zéro et gatée, `auto` choisit A64/x64 par tier vert et census
  exécuté, tout opcode non émis est un rejeu Moira exact (fenêtre FPU incluse
  depuis le 2026-09-07), l'interpréteur reste l'oracle. Reliquats d'études
  moteur conditionnées à un profil temporel : § 5 ci-dessous.
- **Palier C (en faire un produit) — CLOS** le 2026-09-08. Chaque famille CPU
  a un scénario applicatif déterministe au-delà du boot (interpréteur et
  moteur accéléré identiques dans le même processus), la reprise save-state
  inter-instances est gatée sur les trois familles, `asset-none` est vert sur
  toutes les toolchains, et **la version 0.1.0 est taguée et publiée**.
- Ce qui suit est le travail ouvert **post-1.0**, regroupé par thème. Aucun
  item n'est sur un chemin critique ; la priorité entre thèmes reste à
  décider. Une nouvelle machine (§ 4) part toujours d'un gate produit
  réutilisable de sa plateforme ; un ajout LLE (§ 1) part d'une trace, d'un
  observable invité ou d'un consommateur réel ; une optimisation (§ 5) dépend
  d'un profil temporel reproductible et se mesure en ABBA intra-binaire à
  empreintes identiques.

---

## Bloqué sur références externes ou matériel

Items cadrés mais qui ne peuvent avancer sans matériel de référence
(désassemblage/schéma/spec), un actif absent, ou du matériel physique.

- [ ] **Créer `duo230_sleep_etalon`.** Sommeil clapet, arrêt CPU, flush disque,
  réveil complet. Milestone 6 de `docs/DUO_BRINGUP.md` : fermer le clapet
  gèle le CPU mais le System ne lance aucune procédure de sommeil (aucune
  écriture disque) et rouvrir ne réveille pas — « le désassemblage est le seul
  oracle » pour ce chemin. Débloqué par le code System de gestion d'énergie ou
  la spec PMU.
- [ ] **Introduire Retro68 comme oracle invité différentiel.** Sondes
  Toolbox/Device Manager/XPRAM comparées sous MAME et POM68K. Bloqué : la
  toolchain Retro68 n'est pas installée.
- [ ] **Installer un runner auto-hébergé avec les assets.** Rendre le palier
  `full` déclenchable par push et publier `LastTest.log` + le census
  exécutés/soft-skips — ce qui transforme une preuve personnelle en preuve
  vérifiable par un tiers. Bloqué : infrastructure/hôte à provisionner.
- [ ] **Établir la ligne de base POM68K sur un vrai Pi 400**, puis **rejouer
  l'A/B release native/PGO/LTO** sur ce Pi (`jit_bench`/`jit_bench_lcii`,
  budget invité fixe, empreintes archivées, `-mtune`/LTO/PGO séparés, mêmes
  empreintes entre bras). Bloqué : nécessite la carte physique.
- [ ] **Ajouter la cellule Plus/System 4.1 sur floppy.** Bloqué par l'actif :
  `hdv/System 4.1.dsk` est une image SCSI, pas une disquette 800 K ; `bootPlus`
  reçoit son chemin `insertDisk` le jour où l'image existe.

---

## 1. Fidélité matérielle et LLE

- [ ] **Monter une 1,44 Mo depuis le System (LC II SWIM1, Q605 SWIM2) — bug de
  lecture MFM ISM live, PAS bloqué sur référence.** Diagnostic corrigé le
  2026-09-08 (le désassemblage + trace pilote infirment le cadrage « densité »
  précédent, `CHANGELOG` (ninth)) : (1) la détection de densité à l'insertion
  fonctionne — `$A6EC6A` lit reg `$F` (is_2m), un disque HD donne var`$18`=FF
  puis var`$17`=MFM en `$A6EC84`, et le pilote renvoie **-400 (retry)** = l'erreur
  de PRIME #1 ; (2) le System relance la lecture en MFM ; (3) la lecture MFM
  ISM live livre le secteur **corrompu** : le pilote trouve la signature MDB
  `42 44` ('BD') mais tout à partir de l'octet +$2 est faux (`lcii_sony_trace`
  : PRIME #2/#3 → offLinErr, buffer diverge à +$2). La polarité de strobe est
  correcte (table MAME) et la densité aussi ; le défaut est notre moteur/
  livraison MFM du SWIM1 (`Swim1::ismRead`/décode nb/bb, FIFO 2-deep,
  overruns massifs au poll System `$A0BB8C`). Reproducteur : `POM68K_BEYOND=
  floppy POM68K_FLOPPY_IMG=disks35/Stuffit_Expander_5.5.dsk lcii_beyond_etalon`
  ou `./build/lcii_sony_trace --img disks35/Stuffit_Expander_5.5.dsk`.
  Localisé plus finement (2026-09-08, `CHANGELOG` (tenth)) : au retry MFM, le
  moteur de lecture ISM `Swim1::tickRead` **ne tourne jamais** — il est gaté
  sur `ismMode_ && (mode_ & 0x08)` (ACTION) et `selectedDrive()` exige
  `mode_ & 0x80`. Mesuré pendant la lecture : `mode=$42` (ISM bit6 + sel=1
  bit1), `setup=$20` (ISM en mode MFM, correct), mais **ACTION (bit3) jamais
  armé et bit7 (motor/soft-select) jamais mis** → `selectedDrive()` renvoie
  null, aucune cellule décodée, seulement 2 octets poussés sur 68973 nibbles.
  Le pilote configure l'ISM en MFM puis stalle avant d'armer la lecture. C'est
  le « n'arme jamais ACTION » de l'entrée (eighth) avec l'état exact. Prochaine
  étape (`CHANGELOG` (eleventh)) : le 800K lit via la personnalité IWM/GCR
  (jamais de W7), le 1,44 Mo via le moteur ISM — deux chemins distincts, donc
  le moteur ISM MFM n'a jamais été exercé par un cas vert. En MFM le pilote
  fait sa séquence de setup (pulses `W7=$82`/`W6=$80` ×6 + phases + param RAM)
  puis lit la param RAM et abandonne (offLinErr) **sans armer ACTION** (mode
  bit 3), donc `tickRead` ne tourne jamais. Décoder ce que le pilote attend en
  retour après ces strobes (sémantique du handshake reg 7, ou l'ordre de
  relecture de la param RAM) qui le fait renoncer avant ACTION — le défaut est
  dans notre handshake ISM SWIM1. Écarté (2026-09-08) : la param RAM lit
  correctement ce qui est écrit (Pr = Pw sur les vrais params MFM `18 41 2E…`),
  donc ni la relecture ni l'ordre param ne sont en cause. Il reste la sémantique
  du handshake/ACTION après chargement des params — à croiser avec `swim1.cpp`
  de MAME (les préconditions d'armement d'ACTION). Densité, polarité, table de
  mode et param RAM sont toutes confirmées correctes ; le bug est isolé au
  protocole d'armement de lecture ISM. Raffiné (2026-09-08) : la vérif param
  PASSE (2 succès, 1 réécriture), puis le pilote saute dans un callback System
  (`$06AF72` via le global `$b40`) qui TIMEOUT et abandonne — il attend des
  données que l'ISM ne produit pas (le CSM ne se synchronise pas : ~2 octets
  poussés sur 68973 nibbles). Racine = le moteur de lecture ISM MFM ne
  synchronise pas sur les cellules (armement ACTION/sélection lecteur, ou
  classification de cellules/params de timing). La source MAME `swim1.cpp` est
  désormais disponible (`/Volumes/TEST/sauvegarde-20260906/refs/mame/src/
  devices/machine/swim1.cpp`, copiée). Audit MAME-vs-nous fait (2026-09-08) :
  le handshake reg 7, les bits de mode (motoron/ism/hdsel/rw/action/devsel),
  le mapping devsel (sel=1 -> drive interne), la table param, le sense et
  l'encodeur de piste MFM HD sont TOUS fidèles. Séquence observée après la
  vérif param : le pilote sélectionne le lecteur interne (mode c2), lit le
  sense, puis DÉSÉLECTIONNE (mode 42) en boucle, sans JAMAIS armer ACTION
  (bit 3) — donc `tickRead`/CSM/TSM ne tournent jamais et le FIFO reste vide.
  `commandSwim` n'est jamais strobé et `senseSwim` à peine sollicité dans la
  fenêtre de lecture : l'abandon n'est PAS dans notre sense/flux. Le pilote
  renonce dans un helper System (`$06AF72`, atteint via le global bas `$b40`)
  dont la décision est purement arithmétique sur ses arguments pile, AVANT
  ACTION. Prochaine étape = désassembler ce helper System depuis la RAM (pas
  de symboles) : FAIT (2026-09-08). Le vrai code de lecture 1,44 Mo n'est PAS
  dans la ROM — celle-ci n'arme jamais ACTION (aucune écriture de $08 vers reg
  7/$e00). C'est le PATCH System SuperDrive chargé en RAM (~$06Axxx depuis
  System 7.1) qui implémente le MFM, en interceptant le pilote `.Sony` de la
  ROM. La ROM fait une E/S synchrone ($A6EA68 : efface le flag $142.w, appelle
  la routine d'E/S, attend $142.w) ; le patch REMPLACE cette attente par un
  handshake ASYNCHRONE via un flag driver ($19,A1) et deux trampolines
  ($6af6c = `st ($19,A1)` signale la fin, $6af62 = `cmpi.b #-1,($19,A1); bne`
  attend), espacés de 10 octets. Le dispatcher du patch ($06AF72, installé au
  vecteur bas $b40) vérifie que ses trampolines sont sur la pile
  (`10 + [sp+8] - [sp+4] == 0`) ; dans notre run ils sont ABSENTS, donc le
  patch DÉCLINE et retombe sur le chemin ROM ($a6d722) qui ne sait pas faire du
  MFM -> offLinErr. La divergence est EN AMONT : une interception antérieure du
  patch n'installe pas ses trampolines async. Prochaine étape = tracer la
  chaîne d'interception du patch depuis l'entrée PRIME jusqu'à $06AF72 pour
  trouver le hook qui échoue à installer le trampoline dans notre émulation.
  RÉSOLU au niveau architectural (2026-09-08) : la ROM `.Sony` n'arme JAMAIS
  ACTION (aucune écriture du bit 3 vers reg 7/$e00 dans toute la ROM) — elle ne
  sait faire que du GCR (400/800K). Le MFM 1,44 Mo EXIGE le chemin ASYNCHRONE du
  patch SuperDrive. Notre émulation ISM est vérifiée correcte : le test de
  readback des registres ISM ($A6EB1C : écrit reg 4/phases via $800, relit reg
  12 via $1800 aliasé &7) PASSE, et la vérif param PASSE. (Le 1er readback
  échoue normalement : l'ISM n'est ré-entré qu'à $A6EB6C via la séquence magique
  reg 15 $57/$17/$57/$57 ; l'itération 2 passe.) Le read est dispatché au
  dispatcher du patch ($06AF72 via le vecteur bas $b40) avec le frame de retour
  du Device Manager sur la pile ($a0b69e), PAS le frame du wrapper async `.Sony`
  ($a6ea7c/$a6ea72). Le patch n'intercepte QUE le frame du wrapper async
  (`[sp+4]==$a6ea7c && [sp+4]-[sp+8]==10`), donc il DÉCLINE et retombe sur le
  chemin GCR de la ROM -> offLinErr. Le chemin de dispatch est gouverné par le
  bit 6 de ioPosMode du ParamBlock ($A6CEA2 `btst #6,($2d,A0)` -> flag $12c.w) ;
  le flag async-capable ($138,A1 bit7) est bien posé chez nous. Prochaine étape
  = comprendre pourquoi le read atteint le dispatcher en direct/synchrone au
  lieu de passer par le wrapper async `.Sony` : tracer l'entrée Prime du `.Sony`
  face aux appelants du wrapper ($A6E6FC/$A6E774/$A6E97C...) et vérifier si le
  ParamBlock de notre requête (ioPosMode) ou le dispatch queued/immediate diffère
  d'un vrai read 1,44 Mo du Finder. Affiné (2026-09-08, dump de pile + carte des
  vecteurs) : le read qui échoue est le montage lisant le MDB HFS (secteur 2,
  ioPosMode 1, synchrone, caller Device Manager $A0B69E). Les dispatchers RAM du
  patch — $b40->$06AF72, $8fc->$06AFEA, $704->$0133B6 — vérifient chacun le
  contexte appelant et n'agissent que pour un appelant précis ($06AF72 exige le
  frame du wrapper async `.Sony` : [sp+4]==$a6ea7c && [sp+4]-[sp+8]==10). Le dump
  de pile confirme que notre appelant est TOUJOURS le frame Device Manager
  ($40A0B69E), jamais le wrapper — donc tous passent en transparence et le chemin
  GCR de la ROM tourne (ne sait pas lire du MFM) -> offLinErr. Le vecteur Prime
  $226 reste sur le handler ROM $A6CE9A (le patch intercepte plus bas), et le
  chemin Prime SuperDrive est bien pris ($138,A1 bit7 posé). Prochaine étape =
  déterminer pourquoi le read de montage est dispatché en synchrone/direct plutôt
  que via le wrapper async `.Sony` — le plus probable étant les flags de la DCE
  du `.Sony` (bits dCtlFlags async/lock que le patch poserait) ou le chemin
  d'émission de `_MountVol`. Affiné (2026-09-08, comparaison 800K/1,44 Mo) :
  l'échec est en DEUX phases. PRIME #1 = tentative GCR sur disque MFM (68952
  nibbles lus, 931 valides, abandon après 4311 polls vs 107667 en 800K car le
  séparateur GCR ne décode pas du flux MFM) -> -400. PRIME #2 = reprise MFM ->
  -65 offLinErr. Le 800K réussit dès PRIME #1 en GCR et n'exerce jamais le
  chemin MFM (pas de diff instruction possible). L'abandon -65 est une lecture
  de statut : à `$A6CEDA` le pilote appelle `$a6d450` qui lit le registre mode
  ISM (reg 14/`$1c00`, aliasé offset&7) et teste son signe ; on renvoie `$FF`
  car le SWIM est en mode IWM avec le lecteur non-enable (le registre DATA IWM
  lit `$FF` désactivé) -> bit 7 posé -> `$A6CEDE bpl` non pris -> `$A6CEEE` -65.
  Les polls sense dominants (CSTIN addr 1, READY addr D) sont identiques entre
  run qui marche et run qui échoue (pas la cause). Fil ouvert = le suivi du mode
  ISM/IWM sur la reprise MFM : le pilote lit des registres ISM 4779 fois alors
  que le SWIM est en IWM (bit 6 clair), n'entrant en ISM que 3 fois via la
  séquence magique reg 15 ; soit le pilote devrait RESTER en ISM et on en sort
  trop tôt, soit ce sont de vrais polls IWM dont le `$FF` lecteur-désactivé est
  le défaut. Prochaine étape = tracer les transitions du bit 6 sur une reprise
  MFM face au switch ism/iwm de MAME. Outils : `lcii_sony_trace --frames N
  --trace N` + `POM68K_DUMPASM=addr:count`.

Tout ajout LLE part d'une trace ROM/pilote, d'un observable invité ou d'un
consommateur réel. Une approximation plus large sans preuve n'est pas un gain.

- [ ] **Terminer DFAC et la sortie audio du LC II / V8.** Ajouter le
  resampling sur horloge hôte et vérifier le tempo sur une session GUI longue.
- [ ] **Comparer le bus et les timings V8 à du matériel réel.** Couvrir IRQ,
  VBL, VIA et mémoire, puis diagnostiquer l'assombrissement après très longue
  exécution.
- [ ] **Affiner VIA/RTC sur les compacts.** Modéliser les latences T1/T2/IFR à
  un cycle, l'alignement E-clock/IACK et initialiser le RTC GUI depuis l'hôte
  sans rendre les tests non déterministes.
- [ ] **Améliorer la précision sonore des compacts.** Lire le buffer par
  scanline, modéliser le PWM disque et la courbe de volume analogique.
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
- [ ] **Câbler le second lecteur 800K.** Fournir un deuxième `SonyDrive` aux
  machines concernées et ajouter un gate de sélection externe.
- [ ] **Gérer les préfixes clavier `$79`.** Couvrir pavé numérique et flèches
  M0110 là où le protocole les exige.
- [ ] **Étendre SCSI et série.** Ajouter plusieurs targets/LUNs, REQUEST SENSE
  après CHECK CONDITION et un transport série hôte PTY/TCP.
- [ ] **Ajouter des etalons pixel-accurate et un build WASM.** Garder les
  assets privés soft-skippables et comparer des captures stables.

## 2. Services réseau

L'ordre interne est : prouver le parcours invité existant, fermer les défauts
de protocole observés, puis ajouter les extensions et les contrôles GUI. C'est
la plus grande dimension produit encore peu exploitée.

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
- [ ] **Porter la carte DaynaPort aux autres plateformes SCSI.** Ajouter
  membre, accessor, configuration et gate par famille.

## 3. Médias optiques

- [ ] **Établir la règle des images 512/2048 octets.** Comparer le comportement
  des hybrides et bare-HFS avec un vrai pilote/MAME avant de modifier le
  montage.
- [ ] **Ajouter CDDA.** Implémenter TOC audio, PLAY/PAUSE et le chemin sonore
  vers l'ASC avec un gate consommateur.
- [ ] **Supporter les rips 2352 et `.cue/.bin`.** Ajouter les pistes multiples
  sans accepter silencieusement un format mal interprété.

## 4. Nouvelles machines par réutilisation prouvée

Les nouveaux contrôleurs sont prouvés sur le premier profil consommateur avant
d'être généralisés.

- [ ] **Ajouter les variantes Duo 210 et 250.** Exploiter les IDs déjà
  présents, introduire la sélection de profil et ajouter les lignes
  catalogue/gates (après `duo230_sleep_etalon`).
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

## 5. Moteur — études conditionnées à un profil temporel

Reliquat du palier B clos. Aucune de ces lignes n'est une lacune de
conformité ; chacune n'ouvre qu'avec un profil temporel reproductible et se
mesure en ABBA intra-binaire, empreintes identiques.

- [ ] **Décider le sort de l'admission late-poll : rentabilité seulement.** La
  position des polls est dans l'IR et l'admission A64
  (accès/poll/faute/validation) est prouvée conforme — mais mesurée −6,3 %
  sur le bench cache-actif, car la classe admise est les boucles de poll
  chaudes du boot (`CHANGELOG` 2026-09-03 (seventh)). Le knob
  `POM68K_JIT_040_LATE_POLL` reste opt-in. Précondition de rejeu levée
  (`CHANGELOG` 2026-09-04 (sixth)). Ne rouvrir que si un workload cache-actif
  montre le gain — ou après une dé-admission adaptative des sites qui manquent
  chroniquement.
- [ ] **Ne pas rouvrir l'écart d'admission 68030 sans profil temporel neuf.**
  Chiffré le 2026-09-06 et refusé : parité opcode zéro et gatée, le seau non
  supporté plafonne à **1,24 %** contre un plancher de 10 ‰. Reste la
  convention, pas un défaut : une règle 68k commune vit dans l'IR/coût
  partagé, jamais dans un emitter. Évidence :
  `scratchpad/2026-09-05/b3probe/ADMISSION_GAP.md`.
- [ ] **Isoler ou amplifier les familles Speedometer avant toute promotion.**
  A64 et `threaded` terminent chaque famille aux mêmes trames/empreintes/
  écrans/SCSI. Les profils capturés restent dominés par le boot (0,274–0,277 s
  de CPU utile). QuickDraw : natif à 99,7 % mais son cache multi-version de
  gardes donne **+1,75 %** en ABBA — candidat retiré. FPU : **fermé** le
  2026-09-07 (fenêtre `$F200-$F23F` membre de bloc, −11,6 % phase isolée).
  Avant de rouvrir un lowering, répéter une famille dans l'invité ou
  échantillonner sa phase seule. Évidence :
  `scratchpad/2026-09-06/a64-m030/SPEEDOMETER_TIME_PROFILE.md` et voisins.
- [ ] **Étudier `PFLUSHA` et le retry d'armement seulement après profil.**
  Toute réduction des bumps ou du backoff doit garder les locksteps 030/040 :
  le moment où une fenêtre s'arme est observable sur 68040.
- [ ] **Compacter `mmu040InstrStart`.** Mesuré à 3,26 % du run Rogue 040 — gain
  plafond connu et petit. Voir si les remises à zéro adjacentes et le pack CCR
  peuvent devenir un ou deux stores larges sans changer l'état privé vérifié
  par les locksteps. Après la décision late-poll.
- [ ] **Profiler puis isoler les stores à masque nul.** N'ouvrir une
  spécialisation conforme qu'après un profil temporel et des preuves
  empreinte/compteurs/gates identiques.

## 6. Recherche conditionnelle

- [ ] **Définir puis expérimenter le profil d'accélération non conforme.**
  Définir d'abord les critères fonctionnels, les défauts par famille, le
  `purity mode` des gates et un opt-in qui ne puisse jamais contaminer
  l'oracle.
