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
- [ ] **Confirmer sur le M4 les sections AArch64 de `STATUS.md`.** Les quatorze
  gates DaynaPort du 2026-09-12 sont tous `host-any` : la section x86_64 vient
  d'un run réel, les deux sections aarch64 ont reçu le même +14 par report
  manuel — vérifié étiquette par étiquette, attendu 276 gates / 503 créneaux et
  287 à l'union PRODUCT_LLE. Bloqué : aucun hôte ARM ici. Un run sur le M4
  remplace ces chiffres reportés par des chiffres mesurés et confirme que les
  quatorze s'y exécutent au lieu de se sauter.

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
- [ ] **Élucider la divergence entre hôtes de `lcii_floppy_etalon`.** Au commit
  `662a64f` — même gate, même code, mêmes actifs — cet hôte x86-64 échoue
  (309 598 quartets, éjection en 60 images, aucun dossier dans le fichier hôte)
  là où le journal committé à ce même commit passe (586 503 quartets, 180
  images, `untitled folder` 0 → 2). L'hôte de ce run passant n'est **pas
  consigné** : ni `662a64f` ni le journal ne le nomment. Le rouge est apparu le
  2026-09-07 avec `f557e88`, qui a promu en assertion (`&& guestEjected &&
  grewF < folderprobe::kCount`) une question ouverte depuis le 2026-08-05 ;
  aucun code d'émulation n'y a changé, et les deux runs tout-verts du
  2026-09-01 couvraient ce gate sans pouvoir échouer là-dessus. Des deux
  conjonctions ajoutées seule celle du dossier échoue ici : l'invité éjecte bien
  le volume. La divergence commence **dès l'insertion** — moitié des quartets,
  tête piste 10 (TKO=1), aucune marque GCR `D5 AA 96` — donc avant le dossier
  que le gate observe. Écartés : horloge hôte (aucune dans ce chemin), flottant
  (chemin entier), dérive de l'image, et le changement `senseAddr()` (le rouge
  lui est antérieur). Repro : `POM68K_BEYOND=floppy build/lcii_beyond_etalon`
  (65 s). Évidence : `scratchpad/2026-09-12/floppy/`. Suite : instrumenter
  IWM/SWIM1 des deux côtés depuis la première lecture qui diffère.
- [ ] **Décider les échéanciers Mac II et Duo avec un gate sensible à la
  gigue.** Garder les options expérimentales tant qu'aucun observable ne
  justifie leur coût ; comparer état, débit et jitter avant un défaut produit.
- [ ] **Ajouter des etalons pixel-accurate et un build WASM.** Garder les
  assets privés soft-skippables et comparer des captures stables.

## 2. Services réseau

L'ordre interne est : prouver le parcours invité existant, fermer les défauts
de protocole observés, puis ajouter les extensions et les contrôles GUI. C'est
la plus grande dimension produit encore peu exploitée.

- [ ] **Étendre le sous-ensemble AFP — seulement sur consommateur observé.**
  Dimensionné le 2026-09-12 (`CHANGELOG`) : les DID relatifs sont déjà traités,
  le Desktop DB n'est pas absent mais **volontairement bouchonné** (`FPOpenDT`
  implémenté, côté Get `kErrNoItem`, documenté dans `AfpServer.h`), et seuls
  `FPCopyFile` (5) et `FPCatSearch` (43) manquent réellement. Or une session
  live complète du Finder de Mac OS 8.1 — Chooser, montage, énumération, Cmd-N,
  duplication, 41 Kio sur les deux forks, Put Away — rapporte `refused=0/-` aux
  22 frontières de phase : **aucun consommateur réel**. Le serveur compte
  désormais les opcodes refusés (`refusedCount`/`lastRefused`, tracés par
  `q605_afp_live_etalon`), donc le jour où un invité en demande un, cela se
  verra. Ne rien implémenter avant ce signal. AFP 3/UTF-8 reste de même
  conditionné à un invité qui l'exige.
- [ ] **Ajouter des UAM sûrs.** Implémenter DHX/random-number lorsqu'un invité
  refuse le cleartext.
- [ ] **Compléter PAP.** Ajouter polling de statut, configuration des files et
  sélection CUPS dans le GUI.
- [ ] **Compléter MacIP : window scaling TCP.** Le réassemblage IP est fait le
  2026-09-12 (`CHANGELOG`) : un premier fragment passait le test d'offset et
  était livré **tronqué** à la socket hôte, la queue étant jetée — le gate le
  prouve rouge avant / vert après. Reste le window scaling, qui se poserait
  au-dessus d'un endpoint volontairement in-order-only (MSS 536) : décider
  d'abord si cette simplification tombe.
  **ICMP sortant est bloqué par l'hôte, pas par l'effort** : sans `CAP_NET_RAW`
  il faudrait une socket `IPPROTO_ICMP` non privilégiée, or
  `net.ipv4.ping_group_range` vaut `1 0` — une plage vide — sur cette machine.
  Un gate ne pourrait que se sauter, ce qui ne prouverait rien. À rouvrir sur un
  hôte dont la plage couvre le gid, ou avec la capability accordée.
- [ ] **Rendre la configuration réseau éditable dans le GUI.** Partage,
  serveur, imprimante, subnet/DNS et révélation du spool.
- [ ] **Exécuter une session AppleShare complète sur le bridge réel.** Lancer
  netatalk/TashRouter, monter « Input » depuis le Chooser et vérifier un
  transfert.
- [ ] **Tester l'interop Mini vMac LToUDP.** Utiliser le même groupe multicast
  et vérifier les deux directions.
- [ ] **Localiser sur l'hôte AArch64 l'écart entre hôtes de
  `q605_afp_live_etalon`.** Avant le correctif du reliquat FCS, la seconde
  copie LocalTalk valait 171,67 s sur x86-64 (interpréteur, `threaded`, x64,
  cadence ½) contre 165,17 s sur le M4 : une micro-divergence amplifiée par
  ~80 retransmissions ATP. Le correctif retire l'amplificateur, pas la cause.
  Le gate imprime une ligne `trace:` par frontière de phase (horloge,
  empreinte, compteurs) ; référence x86-64 :
  `scratchpad/2026-09-11/afp_live_trace_x86_64.txt`. Rejouer le gate sur le
  M4 et comparer ligne à ligne — la première frontière qui diffère situe la
  divergence —, puis sous `POM68K_CPU_ENGINE=interp` pour séparer le
  générateur `a64` de l'hôte (`CHANGELOG` 2026-09-11 (third) et (fourth)).
- [ ] **Activer EtherTalk par défaut.** Le bridge porte une session AFP réelle
  et un transfert mesuré à 18,6 Kio/s de temps invité contre 8,6 sur le SCC
  (0,2 avant le correctif du reliquat FCS) ; reste à joindre les adresses
  multicast de zone et à décider du défaut produit.
- [ ] **Ajouter le contrôle DaynaPort au GUI.** Attacher/détacher et choisir
  l'ID SCSI sans variable d'environnement.

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
