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

**1. Le Mac 128K/512K — livré pour l'essentiel.** Les profils 38 et 39
sont au catalogue, les deux identités 64 K sont épinglées dans `assets.lock`
et `mac128k_boot_etalon` / `mac512k_boot_etalon` bootent une disquette
System 400 K jusqu'au bureau du Finder — exécutés sur x86-64 le 2026-09-13
(7,40 s chacun) et sur le M4 le 2026-09-16 (5,3 s et 4,4 s) depuis que les
deux images 400 K y sont arrivées du lecteur TEST, épinglées le jour même
dans `assets.lock` (rôle `reference-floppy`, `disks35/ref/`, jumeau `work/`
comme `hdv/ref/`). Ce que l'épinglage ne donne pas : elles sont en MFS et
l'arbre n'a aucun parseur MFS, donc pas de vérification hôte d'un fichier
écrit par l'invité — le modèle de `lcii_floppy_etalon` reste hors d'atteinte
sur ces deux machines. La sérialisation PWM du lecteur 400 K est livrée
(format v16, 2026-09-15). Le paragraphe précédent disait encore, le
2026-09-14, qu'`assets.lock` n'avait aucune ligne 64 K : il datait du
2026-09-12.

**2. Finir les services réseau.** Le parcours invité est prouvé de bout en
bout — Chooser, montage, énumération, copie des deux forks, Put Away, sur
LocalTalk puis sur EtherTalk — et le serveur ne refuse plus rien qu'un invité
demande. Ce qui manque est au-dessus et au-dessous : les contrôles produit et
deux mécanismes non élucidés. **Les deux contrôles produit sont livrés le
2026-09-13** — l'attache DaynaPort (présence et ID stagés, appliqués par
relaunch) et la configuration des services éditable à chaud, chacune portée
par la ligne de relance. Restent les mécanismes non élucidés et les dettes de
preuve. Items : § Services réseau.

---

## Bloqué sur références externes ou matériel

Items cadrés qui ne peuvent avancer sans matériel de référence
(désassemblage/schéma/spec), un actif absent, ou du matériel physique.

- [ ] **Dumps et images manquants — l'état exact, hôte par hôte.** Ce qui
  manque se dit ici, pas dans un soft-skip. Sur le M4 (2026-09-16) : les
  **42 identités d'`assets.lock` sont toutes présentes** — aucune ROM
  machine, aucun micrologiciel MCU (Cuda, Egret, PIC), aucune ROM de
  déclaration ne manque ; la carte Toby `342-0008-a.bin` est là sous
  `roms/archive/macroms/Misc/Video cards/…`, chemin que le produit cherche
  lui-même depuis le 2026-09-16 (avant, seuls les etalons le connaissaient et
  une session GUI sur cet hôte tournait sur la ROM synthétique sans le
  savoir). Les deux disquettes 400 K `disks35/System 1.1.dsk` et
  `System 2.0.dsk` sont arrivées du lecteur TEST le 2026-09-16 :
  `mac128k_boot_etalon` et `mac512k_boot_etalon` s'exécutent désormais ici,
  et elles sont épinglées (`disks35/ref/`, rôle `reference-floppy`).
  Manquent encore des **images**, pas des dumps : les volumes
  `hdv/lc3-boot.vhd`, `hdv/lcii-boot.vhd`, `hdv/iisi-boot.vhd`,
  `hdv/lc-boot.vhd`, `hdv/classic2-boot.vhd`, `hdv/cclassic-boot.vhd`,
  `hdv/mactv-boot.vhd`, `hdv/iici-boot.vhd`, premiers choix de leurs
  etalons, que les replis (`GISTPERSO`, `System 7.5 HD.dsk`, `boot.vhd`)
  remplacent sans le dire autrement que par la ligne `ASSET disk` du gate.
  Pour un **utilisateur du paquet**, deux dumps changent ce qu'il voit, et
  les deux sont maintenant dits dans le produit : sans `342-0008-a.bin` les
  Mac II, IIx, IIcx et IIfx tournent sur la ROM de déclaration synthétique —
  System 6 et 7.0 oui, System 7.5.5 figé à « Welcome to Macintosh » — et la
  fenêtre Périphériques s'ouvre d'elle-même pour le dire (`README.md`
  § Additional firmware, `TobyDeclChoice.h`) ; sans les micrologiciels MCU,
  l'ADB passe en HLE (strict LLE refuse). Le lecteur TEST
  (`/Volumes/TEST/pom68K`) porte depuis le 2026-09-16 les dix volumes de
  `hdv/ref/` et `cd/MacThemePark.toast` pour l'hôte x86-64.
- [ ] **Créer `duo230_sleep_etalon`.** Sommeil clapet, arrêt CPU, flush disque,
  réveil complet. Milestone 6 de `docs/DUO_BRINGUP.md` : fermer le clapet
  gèle le CPU mais le System ne lance aucune procédure de sommeil (aucune
  écriture disque) et rouvrir ne réveille pas — « le désassemblage est le seul
  oracle » pour ce chemin. Débloqué par le code System de gestion d'énergie ou
  la spec PMU.
- [ ] **Introduire Retro68 comme oracle invité différentiel.** Sondes
  Toolbox/Device Manager/XPRAM comparées sous MAME et POM68K. La toolchain
  est installée sur le M4 depuis le 2026-09-14 (`dev/Retro68-build`, elle a
  compilé l'agent « POM68K Disques ») ; ce qui bloque encore est le côté
  MAME de la comparaison — un romset `maclc2` bâti depuis notre ROM et un
  tap Lua, la recette du co-trace SWIM du LC II — et la première sonde à
  écrire.
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
  antérieur). **Premier pas fait le 2026-09-16** : `lcii_floppy_etalon` passe
  sur le M4 (30,2 s, au commit `9196a50`, après le routage réf/work des
  disquettes) — « divergence entre hôtes » est donc un fait, plus une
  hypothèse. Suite : instrumenter IWM/SWIM1 des deux côtés depuis la
  première lecture qui diffère, ce qui demande l'hôte x86-64. Repro :
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

- [ ] **Payer les dettes de preuve du contrôle DaynaPort au GUI.** Le contrôle
  est complet le 2026-09-13 : ligne d'état, bascule « câble », et le choix
  présence + ID SCSI **sans variable d'environnement** — stagé dans la fenêtre
  AppleTalk / Ethernet (`src/NetworkWindow.cpp`), appliqué par relaunch sur
  `--daynaport=<id>`, les ID tenus par un disque grisés. Le débranchement est
  traversé par un vrai invité depuis le 2026-09-14 (`q605_dayna_driver_etalon`
  : câble sorti, 8 requêtes ICMP émises et rien en retour ; rentré, 6 trames
  reviennent, la cible et le bit ENABLE du pilote intacts). Restent : aucun
  invité n'a traversé un relaunch avec carte ; la fenêtre n'a
  jamais été **rendue** — elle compile et lie, sa mise en page (sélecteur
  compris, et le formulaire « Configuration des services » du même jour)
  n'est pas vérifiée à l'œil ; et le relaunch lui-même n'est couvert que par
  sa sérialisation (`daynaport_test` et `atalk_hub_test` prouvent l'aller-retour
  argument → `RuntimeConfig`), pas par un re-exec observé. S'y ajoute, pour le
  formulaire : « Révéler » lance `open` / `xdg-open` / `explorer` sans gate
  possible, et aucun invité n'a remonté un serveur AFP renommé à chaud —
  `afp_server_test` prouve la ré-inscription NBP, pas le Sélecteur.
- [ ] **Élucider pourquoi une date serveur mouvante produisait une seconde
  trajectoire AFP.** Le 2026-09-12 a rendu le gate déterministe en épinglant la
  seule entrée hôte variable du chemin (`FPGetSrvrParms` renvoyait
  `std::time(nullptr)` ; `setFixedDate` plus mtimes du seed épinglés,
  bit-identique sur douze runs) — mais **le mécanisme reste ouvert** : six
  hypothèses sont mortes et on ne sait toujours pas par quoi une date qui
  avance faisait diverger le montage post-reconnexion (2 commandes AFP,
  9 trames, une réponse en retard de 1,44 s).
- [ ] **Rejouer sur x86-64 date-épinglé la comparaison entre hôtes de
  `q605_afp_live_etalon`.** Le gate imprime une ligne `trace:` par frontière de
  phase (horloge, empreinte, compteurs). La référence M4 date-épinglée est
  `scratchpad/2026-09-13/afp_live_trace_aarch64.txt` ;
  `tools/afp_trace_diff.py` situe la première frontière qui diffère entre
  cette référence et un autre run, y compris un log brut `ctest -V`.
  Le rejeu A64 retrouve les 22 frontières ; le rejeu sous
  `POM68K_CPU_ENGINE=interp` passe le gate mais diverge à la frontière 14
  (serveurs après reconnexion, horloge −8 ticks puis empreinte différente).
  Sa trace est conservée dans `afp_live_trace_aarch64_interp.txt` du même
  dossier : cet écart entre moteurs reste à localiser avant attribution à
  l'hôte.
  **Le protocole est à refaire**, pas à reprendre : la référence
  `scratchpad/2026-09-11/afp_live_trace_x86_64.txt` est antérieure à
  l'épinglage de la date et les chiffres 171,67 s / 165,17 s sont antérieurs au
  correctif du reliquat FCS. Produire d'abord une référence x86-64
  date-épinglée.
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
  depuis le Chooser et vérifier un transfert. C'est aussi là que les adresses
  multicast de zone (`09:00:07:00:00:xx`) se poseraient : le routeur interne
  répond UseBroadcast et le segment de la carte est point à point, si bien
  que la liste que `SET MULTICAST ADDRESS` reçoit est acceptée et ignorée
  (`DaynaPort.h`) ; un routeur externe qui n'annonce pas UseBroadcast la
  rendrait nécessaire.
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

- [ ] **Donner une preuve au-delà du boot aux profils qui n'en ont toujours
  pas.** Depuis le 2026-09-15, 36 profils portent `<profil>_agent_boot_etalon`
  (`tests/AgentBootProbe.h`, `cmake/Pom68kAgentGates.cmake`) : le Finder lance
  l'agent installé par l'hôte, l'agent monte un disque attaché à chaud —
  Process Manager, SCSI Manager et File Manager après la signature ; les
  compacts et le IIx/IIcx y compris, sur le volume System 7.0. Reste au boot
  seul le 128K/512K (System 1.1/2.0, sans Startup Items).
  `docs/68K_FAMILY_SCOPE.md` § 5.
- [ ] **Faire tomber le GUI sous un gate, et lui passer la main dessus.** Le
  GUI n'a toujours aucun gate : `--version` passe, l'arbre compile, `-L unit`
  et `-L smoke` sont verts, et rien de tout cela n'ouvre une fenêtre. Une passe
  à la main sur les fenêtres machine (menus, upload framebuffer, hot-swap
  floppy/CD, save/restore) reste la validation due — c'est le même reliquat que
  la passe save-state GUI, jamais fermée, que les trois gates de relance du
  2026-09-08 ne couvrent pas (ils sont hors GUI).
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
