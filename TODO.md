# TODO — feuille de route

Ce fichier contient uniquement du travail ouvert. Les résultats, mesures,
fausses pistes et décisions terminées vivent dans `CHANGELOG.md` et
`CHANGELOG_INDEX.md`. Les détails d'implémentation vivent dans `DEV.md`,
`src/jit/POM68K_JIT.md`, `docs/JIT_BRINGUP.md` et les notes vendor.

`STATUS.md` est généré depuis les manifests CMake et reste la source de vérité
pour les gates. Toute tâche fermée quitte ce fichier dans le même changement
qui ajoute son entrée au `CHANGELOG`.

**Un renvoi extérieur cite le NOM d'une section, jamais son numéro** — écrire
`TODO.md § Services réseau`, abrégeable à son premier mot quand la place
manque (`§ Fidélité`, `§ Réseau`, `§ Preuve`, `§ Machines`, `§ Moteur`).
Les numéros bougent à chaque réorganisation : le
2026-09-12, vingt-quatre renvois hérités de la numérotation d'avant le
2026-09-08 (`§ 0·A`, `§ B.2`, `§ 4bis`, `§ 7`, `§ 8`, `§ App-compat`…) ont dû
être réparés dans `docs/`, `DEV.md` et cinq commentaires de production. Les uns
pointaient dans le vide ; les autres, plus graves, désignaient en silence une
section qui n'avait plus rien à voir — `RASPBERRY_PI.md` renvoyait au « § 4 »
pour le mécanisme de deadline périphérique, devenu « Nouvelles machines ».

## Statut

- **Palier B (terminer et qualifier le moteur) — CLOS** le 2026-09-07. Les
  deux générateurs natifs déclarent la conformité 68030+68040, la parité
  opcode est zéro et gatée, `auto` choisit A64/x64 par tier vert et census
  exécuté, tout opcode non émis est un rejeu Moira exact (fenêtre FPU incluse
  depuis le 2026-09-07), l'interpréteur reste l'oracle. Reliquats d'études
  moteur conditionnées à un profil temporel : § Moteur.
- **Palier C (en faire un produit) — CLOS** le 2026-09-08. Chaque famille CPU
  a un scénario applicatif déterministe au-delà du boot (interpréteur et
  moteur accéléré identiques dans le même processus), la reprise save-state
  inter-instances est gatée sur les trois familles, `asset-none` est vert sur
  toutes les toolchains, et **la version 0.1.0 est taguée et publiée**.
- **La suite est décidée le 2026-09-12** : deux chantiers dotés en parallèle,
  le **Mac 128K/512K** (§ Nouvelles machines) et la **fin des services
  réseau** (§ Services réseau). Les autres thèmes restent ouverts et non
  dotés ; aucun item n'est sur un chemin critique.
- Règles d'admission, inchangées : une nouvelle machine part d'un gate produit
  réutilisable de sa plateforme ; un ajout LLE part d'une trace, d'un
  observable invité ou d'un consommateur réel ; une optimisation dépend d'un
  profil temporel reproductible et se mesure en ABBA intra-binaire à
  empreintes identiques.

---

## La suite — les deux chantiers dotés

Ce bloc ne porte que la décision et le premier pas ; le travail lui-même vit
dans les sections thématiques, en un seul exemplaire.

**1. Le Mac 128K/512K — ne pas commencer la collection au deuxième album.**
La couverture commence au Plus alors que les deux ROMs 64 K sont en main et
que `docs/68K_FAMILY_SCOPE.md` § 4 classe la machine *cheap, unblocked* :
c'est un sous-ensemble du Plus, pas une brique. **Premier pas** : épingler les
deux identités dans `assets.lock`, qui ne contient aujourd'hui aucune ligne
64 K. Item complet : § Nouvelles machines.

**2. Finir les services réseau.** Le parcours invité est prouvé de bout en
bout — Chooser, montage, énumération, copie des deux forks, Put Away, sur
LocalTalk puis sur EtherTalk — et le serveur ne refuse plus rien qu'un invité
demande. Ce qui manque est au-dessus et au-dessous : les contrôles produit et
deux mécanismes non élucidés. **Premier pas** : les deux contrôles GUI
absents (configuration réseau, attache DaynaPort), parce que l'attache d'une
carte passe encore par une variable d'environnement sur une fonction qui,
elle, est gatée sur les douze plateformes. Items : § Services réseau.

---

## Bloqué sur références externes ou matériel

Items cadrés qui ne peuvent avancer sans matériel de référence
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
  empreintes entre bras). Le paquet Cortex-A76 existe et est archivé depuis le
  2026-09-07 (binaire distinct du A72) : il ne manque que l'exécution. Bloqué :
  nécessite la carte physique.
- [ ] **Ajouter la cellule Plus/System 4.1 sur floppy.** Le chemin est prêt et
  gaté depuis le 2026-09-09 : les 36 desktops ont leur lecteur externe et
  `external_floppy_boot_etalon` boote la ROM Plus depuis le drive B avec le
  drive A vide. Bloqué par le seul actif : `hdv/System 4.1.dsk` est une image
  SCSI, pas une disquette 800 K.
- [ ] **Trouver une image système 400 K amorçable** (System 1.x–3.x). Aucune
  n'existe dans l'arbre : les quatre images de `disks35/` font 819 200 octets
  et tout le reste est SCSI. C'est ce qui sépare le Mac 128K/512K de son
  etalon Finder — le câblage, lui, n'est pas bloqué (§ Nouvelles machines).
- [ ] **Confirmer sur le M4 les sections AArch64 de `STATUS.md`.** Les quatorze
  gates DaynaPort du 2026-09-12 sont tous `host-any` : la section x86_64 vient
  d'un run réel, les deux sections aarch64 ont reçu le même +14 par report
  manuel — vérifié étiquette par étiquette, attendu 276 gates / 503 créneaux et
  287 à l'union PRODUCT_LLE. Bloqué : aucun hôte ARM ici. Un run sur le M4
  remplace ces chiffres reportés par des chiffres mesurés et confirme que les
  quatorze s'y exécutent au lieu de se sauter.

---

## Fidélité matérielle et LLE

Tout ajout LLE part d'une trace ROM/pilote, d'un observable invité ou d'un
consommateur réel. Une approximation plus large sans preuve n'est pas un gain.

- [ ] **Comparer le bus et les timings V8 à du matériel réel.** Couvrir IRQ,
  VBL, VIA et mémoire, puis diagnostiquer l'assombrissement après très longue
  exécution. Inclut la question ouverte du Classic II : le bloc derrière
  `$50F18038`, que sa ROM déréférence, n'a jamais été identifié et ne peut
  l'être qu'en observant le motif d'accès (`POM68K_V8_IOHOLE=1`).
- [ ] **Affiner VIA/RTC sur les compacts.** Modéliser les latences T1/T2/IFR à
  un cycle, l'alignement E-clock/IACK et initialiser le RTC GUI depuis l'hôte
  sans rendre les tests non déterministes.
- [ ] **Améliorer la précision sonore des compacts.** Le temps de lecture
  appartient au DAC hôte depuis le 2026-09-09 (rééchantillonnage rationnel
  22 257 → 48 000 Hz, sons mécaniques sur l'horloge native) et la courbe de
  volume analogique est implémentée côté DFAC/V8 depuis la table MAME. Reste
  ce qui est vraiment propre aux compacts : lecture du buffer par scanline et
  modélisation du PWM disque.
- [ ] **Trancher le DFAC2 et les machines sans DFAC.** Le payload DFAC2
  (Color Classic, Color Classic II) est volontairement ACK-only — interpréter
  son atténuation rendrait la machine muette — le Mac TV n'a pas de DFAC du
  tout, et aucun filtre analogique n'est synthétisé. Décider ce qui est un
  défaut et ce qui est le contrat, avec un observable invité.
- [ ] **Étendre les commandes Cuda du Q605/LC 475 uniquement depuis des traces
  ROM/pilote.** Le prochain travail porte sur le timing pin-level 040 et les
  commandes réellement observées, pas sur une nouvelle approximation.
- [ ] **Compléter le low tier SCC seulement avec un consommateur.** Le
  transport série réel PTY/TCP et son chemin octet asynchrone sont clos le
  2026-09-09 ; ajouter désormais les variantes 8530/85C30/ESCC lorsqu'une
  machine les demande, puis WR9 VIS/NV et DPLL avec gates, en préservant le
  comportement LLAP déjà plus complet que l'oracle MAME.
- [ ] **Revalider l'arithmétique de zones GCR.** La moitié « faire survivre à
  un commit les flux écrits hors cadence » est faite le 2026-09-09
  (reconstruction flux → cellules à l'horloge PLL nominale de part et d'autre
  de la soudure, reprise de la queue sur sa grille, recadrage MFM aux deux
  bords, `SonyDriveFlux.cpp`). Ne reste que les zones GCR, et seulement avec un
  symptôme ou un corpus avant d'élargir le modèle.
- [ ] **Élucider la divergence entre hôtes de `lcii_floppy_etalon`.** Au commit
  `662a64f` — même gate, même code, mêmes actifs — cet hôte x86-64 échoue
  (309 598 quartets, éjection en 60 images, aucun dossier dans le fichier hôte)
  là où le journal committé à ce même commit passe (586 503 quartets, 180
  images, `untitled folder` 0 → 2). L'hôte de ce run passant n'est **pas
  consigné** : ni `662a64f` ni le journal ne le nomment. Le rouge est apparu le
  2026-09-07 avec `f557e88`, qui a promu en assertion (`&& guestEjected &&
  grewF < folderprobe::kCount`) une question ouverte depuis le 2026-08-05 ;
  aucun code d'émulation n'y a changé. Des deux conjonctions ajoutées seule
  celle du dossier échoue ici : l'invité éjecte bien le volume. La divergence
  commence **dès l'insertion** — moitié des quartets, tête piste 10 (TKO=1),
  aucune marque GCR `D5 AA 96` — donc avant le dossier que le gate observe.
  Écartés : horloge hôte (aucune dans ce chemin), flottant (chemin entier),
  dérive de l'image, et le changement `senseAddr()` (le rouge lui est
  antérieur). **Premier pas, non fait** : rejouer le gate sur le M4 pour
  confirmer qu'il y passe encore — c'est ce qui ferait de « divergence entre
  hôtes » un fait plutôt qu'une hypothèse. Ensuite instrumenter IWM/SWIM1 des
  deux côtés depuis la première lecture qui diffère. Repro :
  `POM68K_BEYOND=floppy build/lcii_beyond_etalon` (65 s). Évidence :
  `scratchpad/2026-09-12/floppy/`.
- [ ] **Re-tester le chemin 030 de « SANE sans FPU ».** Sur la forme LC II,
  `$AE` = 0 retombe sur `defaultRSRCs` = 4 → masque `$08000000` → le `PACK 4`
  entier, et `hwCfgWord` `$CC00` n'a pas le bit 12 : le mécanisme existe et est
  correctement paramétré sur cette ROM. Ce qui n'est pas su, c'est si le 030 a
  besoin de la sélection `UniversalInfo`/`defaultRSRCs` qui a réparé le côté
  040 — question à re-tester, pas à re-diagnostiquer
  (`docs/BASILISK_ROM_NOTES.md` § 8.5).
- [ ] **Décider les échéanciers Mac II et Duo avec un gate sensible à la
  gigue.** Garder les options expérimentales tant qu'aucun observable ne
  justifie leur coût ; comparer état, débit et jitter avant un défaut produit.
  Leçon du 2026-09-03 à appliquer d'abord : sur le Q605, le défaut promu
  SCC/53C96 était redevenu opt-in **en silence** parce que le gate forçait les
  champs au lieu de consommer le défaut du composant — un gate qui écrit la
  valeur qu'il vérifie ne prouve pas le défaut.
- [ ] **Ajouter des etalons pixel-accurate et un build WASM.** Garder les
  assets privés soft-skippables et comparer des captures stables. Le WASM n'a
  aujourd'hui que des stubs inactifs.

---

## Services réseau

**Chantier doté.** L'ordre interne est : fermer les deux mécanismes non
élucidés, livrer les contrôles produit, puis n'ajouter du protocole que sur
consommateur observé.

- [ ] **Rendre la configuration réseau éditable dans le GUI.** Partage,
  serveur, imprimante, subnet/DNS et révélation du spool.
- [ ] **Ajouter le contrôle DaynaPort au GUI.** Attacher/détacher et choisir
  l'ID SCSI sans variable d'environnement. La fonction, elle, est finie : la
  carte tient sur les douze bus depuis le 2026-09-12 (un gate par plateforme
  prouve que l'invité l'a trouvée) et voyage en save state format v15.
- [ ] **Élucider pourquoi une date serveur mouvante produisait une seconde
  trajectoire AFP.** Le 2026-09-12 a rendu le gate déterministe en épinglant la
  seule entrée hôte variable du chemin (`FPGetSrvrParms` renvoyait
  `std::time(nullptr)` ; `setFixedDate` plus mtimes du seed épinglés,
  bit-identique sur douze runs) — mais **le mécanisme reste ouvert** : six
  hypothèses sont mortes et on ne sait toujours pas par quoi une date qui
  avance faisait diverger le montage post-reconnexion (2 commandes AFP,
  9 trames, une réponse en retard de 1,44 s).
- [ ] **Localiser sur l'hôte AArch64 l'écart entre hôtes de
  `q605_afp_live_etalon`.** Le gate imprime une ligne `trace:` par frontière de
  phase (horloge, empreinte, compteurs) ; rejouer sur le M4 et comparer ligne à
  ligne situe la divergence à la première frontière qui diffère, puis sous
  `POM68K_CPU_ENGINE=interp` pour séparer le générateur `a64` de l'hôte.
  **Le protocole est à refaire**, pas à reprendre : la référence
  `scratchpad/2026-09-11/afp_live_trace_x86_64.txt` est antérieure à
  l'épinglage de la date et les chiffres 171,67 s / 165,17 s sont antérieurs au
  correctif du reliquat FCS. Produire d'abord une référence x86-64
  date-épinglée.
- [ ] **Activer EtherTalk par défaut.** Le bridge porte une session AFP réelle
  et un transfert mesuré à 18,6 Kio/s de temps invité contre 8,6 sur le SCC ;
  reste à joindre les adresses multicast de zone et à décider du défaut
  produit.
- [ ] **Compléter MacIP : window scaling TCP.** Le réassemblage IP est fait le
  2026-09-12 : un premier fragment passait le test d'offset et était livré
  **tronqué** à la socket hôte, la queue étant jetée — le gate le prouve rouge
  avant / vert après. Reste le window scaling, qui se poserait au-dessus d'un
  endpoint volontairement in-order-only (MSS 536) : décider d'abord si cette
  simplification tombe.
  **ICMP sortant est bloqué par l'hôte, pas par l'effort** : sans `CAP_NET_RAW`
  il faudrait une socket `IPPROTO_ICMP` non privilégiée, or
  `net.ipv4.ping_group_range` vaut `1 0` — une plage vide — sur cette machine.
  Un gate ne pourrait que se sauter, ce qui ne prouverait rien. À rouvrir sur un
  hôte dont la plage couvre le gid, ou avec la capability accordée.
- [ ] **Exécuter une session AppleShare complète sur le bridge réel.** Les
  sessions réelles du 2026-09-09 et du 2026-09-11 passent par le serveur
  **in-process** (LocalTalk puis EtherTalk sur la carte) : ce qui n'a jamais
  tourné, c'est netatalk/TashRouter. Lancer l'un des deux, monter « Input »
  depuis le Chooser et vérifier un transfert.
- [ ] **Tester l'interop Mini vMac LToUDP.** Utiliser le même groupe multicast
  et vérifier les deux directions.
- [ ] **Étendre le sous-ensemble AFP — seulement sur consommateur observé.**
  Dimensionné le 2026-09-12 : les DID relatifs sont déjà traités, le Desktop DB
  n'est pas absent mais **volontairement bouchonné** (`FPOpenDT` implémenté,
  côté Get `kErrNoItem`, documenté dans `AfpServer.h`), et seuls `FPCopyFile`
  (5) et `FPCatSearch` (43) manquent réellement. Or une session live complète
  du Finder de Mac OS 8.1 rapporte `refused=0/-` aux 22 frontières de phase :
  **aucun consommateur réel**. Le serveur compte désormais les opcodes refusés
  (`refusedCount`/`lastRefused`, tracés par `q605_afp_live_etalon`), donc le
  jour où un invité en demande un, cela se verra. Ne rien implémenter avant ce
  signal. AFP 3/UTF-8 reste de même conditionné à un invité qui l'exige.
- [ ] **Ajouter des UAM sûrs.** Implémenter DHX/random-number lorsqu'un invité
  refuse le cleartext.
- [ ] **Compléter PAP.** Ajouter polling de statut, configuration des files et
  sélection CUPS dans le GUI.

---

## Preuve, outillage et dettes de mesure

Ce que les gates ne prouvent pas encore, et ce qui rend une preuve fragile.
Section ouverte le 2026-09-12 : douze de ces items étaient du travail ouvert
consigné au `CHANGELOG` sans jamais avoir d'entrée au backlog.

- [ ] **Donner une preuve au-delà du boot aux 28 profils qui n'en ont pas.**
  37 profils sur 37 ont un etalon Finder ; **9 sur 37** seulement ont un gate
  *après* la signature. `docs/68K_FAMILY_SCOPE.md` § 5 appelle cela le plus
  gros écart du projet, et son § 6 le classe premier en retour sur effort —
  devant toute nouvelle machine. Le compromis est explicite : ajouter un 38e
  profil coûte moins cher que durcir les 37 existants.
- [ ] **Faire tomber le GUI sous un gate, et lui passer la main dessus.** Le
  GUI n'a toujours aucun gate : `--version` passe, l'arbre compile, `-L unit`
  et `-L smoke` sont verts, et rien de tout cela n'ouvre une fenêtre. Une passe
  à la main sur les fenêtres machine (menus, upload framebuffer, hot-swap
  floppy/CD, save/restore) reste la validation due — c'est le même reliquat que
  la passe save-state GUI, jamais fermée, que les trois gates de relance du
  2026-09-08 ne couvrent pas (ils sont hors GUI).
- [ ] **Réparer `declrom_test`, qui compte « exécuté » en perdant trois
  assertions.** Sans sa ROM il en saute trois et sort quand même 0 : même
  classe de défaut que les étiquettes `asset-none` menteuses, et invisible au
  census. Consigné le 2026-09-02 comme « hors périmètre du jour ».
- [ ] **Trancher la cellule `finder_boot_matrix` macii × 7.5.5**, enregistrée
  UNSTABLE le 2026-09-02 (Stickies au premier plan).
- [ ] **Décider le sort d'`assets.lock` mono-hôte, et tenir la jambe AArch64.**
  Quatre volumes sont nés sur x86-64 ; les options posées le 2026-09-02 sont le
  transport (~660 Mo), un verrou par hôte, ou une recette reproductible — le
  2026-09-03 n'a accordé qu'un *waiver de séquencement*, pas une fermeture. Tant
  que ce n'est pas tranché, **la seconde jambe du critère de sortie du jalon 1
  n'a pas d'item** : deux runs tout-verts existent sur x86-64 (2026-09-01/02),
  aucun sur AArch64.
- [ ] **Fermer le `-Wstringop-overflow` de GCC 13 + LTO sur
  `EtherLink::sendToGuest`.** Latent parce que le job `-Werror` construit sans
  LTO : la combinaison qui l'expose n'est couverte par aucun gate.
- [ ] **Exécuter les locksteps sur un hôte Windows.** C'est le préalable nommé
  de la décision « `threaded` est le plancher Windows » : tant qu'aucun hôte
  Windows ne peut exécuter les locksteps, le choix reste une décision et non
  une mesure.
- [ ] **Résoudre la table KCHR de l'invité pour la frappe des harnais.** Sur
  8.1, `$1B40` pointe dans le code Système ; la recherche a été abandonnée
  plutôt que devinée. Conséquence vivante : les gates ne tapent que lettres et
  espaces, et ouvrent à la souris ce qui demande un tiret.
- [ ] **Rendre `docs_test` capable de voir une citation fausse.** Son § 10 ne
  prouve que l'**existence** de la plage citée, pas qu'elle dise ce que la
  phrase affirme — la moitié faible de la garantie, assumée comme telle dans
  son propre commentaire. Le balayage du 2026-09-12 l'a mesurée : des dizaines
  de citations `file:line` pointaient sur un tout autre code **pendant que le
  gate était vert** (`DEV.md` citait `MacMemory.h:117-124` pour
  `loadPram`/`savePram`, qui vivent à `:149-150` ; `MachineCatalog.h:35-49`
  pour un enum qui ouvre à `:50`). Piste : exiger de toute citation un ancrage
  vérifiable — un symbole ou un fragment que le gate retrouve dans la plage.

---

## Médias optiques

- [ ] **Établir la règle des images 512/2048 octets.** Comparer le comportement
  des hybrides et bare-HFS avec un vrai pilote/MAME avant de modifier le
  montage.
- [ ] **Ajouter CDDA.** Implémenter TOC audio, PLAY/PAUSE et le chemin sonore
  vers l'ASC avec un gate consommateur.
- [ ] **Trancher ce que « supporter `.cue/.bin` » veut dire, puis les rips
  2352.** Le balayage média du 2026-09-09 affirme couvrir « ISO/CUE/BIN
  handling » : ou bien cet item se réduit aux pistes multiples en 2352, ou bien
  cette affirmation sur-promet et c'est elle qu'il faut corriger. Trancher
  avant d'écrire du code, et ne jamais accepter en silence un format mal
  interprété.

---

## Nouvelles machines

**Chantier doté (Mac 128K/512K).** Les nouveaux contrôleurs sont prouvés sur le
premier profil consommateur avant d'être généralisés ; une ligne catalogue se
mérite par une cellule Finder **plus** le câblage GUI et save-state.

- [ ] **Ajouter le Macintosh 128K et le 512K/512Ke.** C'est un sous-ensemble du
  Plus, pas une brique : ROM 64 K, pas de SCSI, moins de RAM, mécanisme
  400 K. Les deux dumps sont en main (`roms/64KB ROMs/`, 65 536 octets chacun,
  `28BA61CE` et `28BA4E50`) et **aucun n'est épinglé dans `assets.lock`** — qui
  n'a aucune ligne 64 K. Ce que le code demande, dans l'ordre :
  épingler les deux identités ; étendre `MacMemory::Model`
  (`src/MacMemory.h:58`), qui ne connaît que `Plus/SE/SEFDHD/Classic`, et sa
  taille de ROM figée à 128 Ko (`src/MacMemory.h:42`) ; ajouter le mécanisme
  400 K à `FloppyKind` (`src/MachineCatalog.h:24`), qui ne propose que
  `None/Gcr800K/SuperDrive`, et décrire `scsi = false` dans
  `storageCapabilities` (`src/MachineCatalog.h:130`) — ce sera le premier
  profil sans SCSI de l'arbre ; enfin la ligne catalogue, le `SnapMachine`, le
  menu Machine et la ligne de la table ROM du `README.md`. L'etalon Finder, lui,
  attend une image 400 K amorçable (§ Bloqué).
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

---

## Moteur — études conditionnées à un profil temporel

Reliquat du palier B clos. Aucune de ces lignes n'est une lacune de
conformité ; chacune n'ouvre qu'avec un profil temporel reproductible et se
mesure en ABBA intra-binaire, empreintes identiques. La barre d'admission est
faite de preuves indépendantes : conformité, stabilité du tier entier, et
performance mesurée dans le même processus — aucune ne se déduit d'une autre.

- [ ] **Décider le sort de l'admission late-poll : rentabilité seulement.** La
  position des polls est dans l'IR et l'admission A64
  (accès/poll/faute/validation) est prouvée conforme — mais mesurée −6,3 %
  sur le bench cache-actif, car la classe admise est les boucles de poll
  chaudes du boot. Le knob `POM68K_JIT_040_LATE_POLL` reste opt-in ;
  précondition de rejeu levée le 2026-09-04. Ne rouvrir que si un workload
  cache-actif montre le gain — ou après une dé-admission adaptative des sites
  qui manquent chroniquement.
- [ ] **Ne pas rouvrir l'écart d'admission 68030 sans profil temporel neuf.**
  Chiffré le 2026-09-06 et refusé : parité opcode zéro et gatée, le seau non
  supporté plafonne à **1,24 %** contre un plancher de 10 ‰. Reste la
  convention, pas un défaut : une règle 68k commune vit dans l'IR/coût
  partagé, jamais dans un emitter. Évidence :
  `scratchpad/2026-09-05/b3probe/ADMISSION_GAP.md`. La moitié `a64` de cette
  mesure n'a jamais tourné : seul un hôte AArch64 peut l'exécuter.
- [ ] **Mesurer le cache de dispatch de `jit::Engine`.** Le sortir des 1 Mo
  en ligne a guéri d'un coup toutes les fixtures qui segfaultaient, mais
  « doit sa propre mesure » — jamais faite. Conséquences déjà payées :
  `/STACK:16777216` sur MSVC et 15+154 fixtures déplacées sur le tas.
- [ ] **Attribuer les +6 % du bras natif a64 sur le Q605** contre la référence
  du 2026-08-23, et le delta de banc borné mais non attribué du 2026-09-03 :
  les deux sont « notés plutôt que poursuivis ».
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
- [ ] **Re-mesurer avant de compacter `mmu040InstrStart`.** Les 3,26 % qui
  justifiaient l'item viennent du profil 68040 du 2026-09-02, que le
  2026-09-03 déclare **périmé par son propre succès** : le cache de dispatch a
  effacé son poste n°1 (−29,6 %) et le paysage post-cache met tout le seau
  MMU/cache à 7,1 % de la phase de jeu. Le plafond « connu et petit » n'est
  plus connu. Après la décision late-poll.
- [ ] **Profiler puis isoler les stores à masque nul.** N'ouvrir une
  spécialisation conforme qu'après un profil temporel et des preuves
  empreinte/compteurs/gates identiques.

---

## Recherche conditionnelle

- [ ] **Définir puis expérimenter le profil d'accélération non conforme.**
  Définir d'abord les critères fonctionnels, les défauts par famille, le
  `purity mode` des gates et un opt-in qui ne puisse jamais contaminer
  l'oracle. Cette voie reste derrière la voie conforme, par la décision
  d'ordonnancement du 2026-08-09 (`docs/HLE_OVERLAY.md`).
