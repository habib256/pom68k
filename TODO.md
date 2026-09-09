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

Tout ajout LLE part d'une trace ROM/pilote, d'un observable invité ou d'un
consommateur réel. Une approximation plus large sans preuve n'est pas un gain.

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
- [ ] **Compléter le low tier SCC seulement avec un consommateur.** Le
  transport série réel PTY/TCP et son chemin octet asynchrone sont clos le
  2026-09-09 (`CHANGELOG`, eighth) ; ajouter désormais les variantes
  8530/85C30/ESCC lorsqu'une machine les demande, puis WR9 VIS/NV et DPLL avec
  gates, en préservant le comportement LLAP déjà plus complet que l'oracle
  MAME.
- [ ] **Créer un store de piste flux de première classe.** Faire survivre les
  flux écrits hors cadence à un commit et revalider l'arithmétique de zones
  GCR ; exiger un symptôme ou un corpus avant d'élargir le modèle.
- [ ] **Décider les échéanciers Mac II et Duo avec un gate sensible à la
  gigue.** Garder les options expérimentales tant qu'aucun observable ne
  justifie leur coût ; comparer état, débit et jitter avant un défaut produit.
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
