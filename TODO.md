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
Les numéros bougent à chaque réorganisation.

## Statut

- **En cours : jalon 2** (le produit prouvé). Le jalon 1 est clos le
  2026-09-16 — les deux jambes du registre complet et la version 0.2.0 ;
  les paliers B (moteur) et C (produit 0.1.0) l'étaient les 2026-09-07/08.
  Census et preuves datées : `STATUS.md`, `CHANGELOG.md`.
- Admission : une nouvelle machine part d'un gate produit réutilisable de
  sa plateforme ; un ajout LLE part d'une trace, d'un observable invité ou
  d'un consommateur réel ; une optimisation dépend d'un profil temporel
  reproductible et se mesure en ABBA intra-binaire à empreintes identiques.

---

## Jalons 2 à 5 — décidés le 2026-09-16

Chaque jalon porte un critère de sortie ; le travail vit dans les sections
thématiques, en un seul exemplaire. Ordre 2 → 3 → 4 → 5 : le jalon 2 rend
chaque suivant vérifiable par quelqu'un d'autre que l'hôte qui l'a produit.

**Jalon 2 — Le produit prouvé.** Sortie : chaque fenêtre a un gate ou une
passe manuelle datée ; un run complet publié par la CI sur un runner à
assets ; version 0.3. Travail : § Preuve, § Bloqué (runner, locksteps
Windows).

**Jalon 3 — Les services réseau clos.** Sortie : session réelle sur bridge
externe verte, zéro opcode refusé sur les sessions live, les deux
mécanismes expliqués ou tranchés. Travail : § Services réseau.

**Jalon 4 — Fidélité matérielle et médias.** Sortie : un jeu CD avec audio
joué de bout en bout, la divergence LC II attribuée, N profils sous etalon
pixel-accurate. Travail : § Fidélité, § Médias optiques.

**Jalon 5 — Les portables.** Sortie : la famille PowerBook boote au Finder
avec preuve au-delà du boot, catalogue et save-state câblés. Travail :
§ Nouvelles machines ; `duo230_sleep_etalon` est en § Bloqué.

**Hors jalon.** § Moteur reste conditionné à un profil temporel
reproductible ; § Recherche conditionnelle derrière la voie conforme.

---

## Bloqué sur références externes ou matériel

Items cadrés qui ne peuvent avancer sans matériel de référence
(désassemblage/schéma/spec), un actif absent, ou du matériel physique.

- [ ] **Créer `duo230_sleep_etalon`.** Sommeil clapet, arrêt CPU, flush
  disque, réveil complet. Milestone 6 de `docs/DUO_BRINGUP.md` : fermer le
  clapet gèle le CPU mais le System ne lance aucune procédure de sommeil
  et rouvrir ne réveille pas. Bloqué par le code System de gestion
  d'énergie ou la spec PMU.
- [ ] **Introduire Retro68 comme oracle invité différentiel.** Sondes
  Toolbox/Device Manager/XPRAM comparées sous MAME et POM68K. La toolchain
  est installée sur le M4 (`dev/Retro68-build`). Bloqué : romset `maclc2`
  bâti depuis notre ROM, tap Lua, recette du co-trace SWIM du LC II, et la
  première sonde à écrire.
- [ ] **Installer un runner auto-hébergé avec les assets.** Rendre le palier
  `full` déclenchable par push et publier `LastTest.log` + le census
  exécutés/soft-skips. Bloqué : infrastructure/hôte à provisionner.
- [ ] **Établir la ligne de base POM68K sur un vrai Pi 400**, puis **rejouer
  l'A/B release native/PGO/LTO** (`jit_bench`/`jit_bench_lcii`, budget
  invité fixe, empreintes archivées, `-mtune`/LTO/PGO séparés). Le paquet
  Cortex-A76 est archivé depuis le 2026-09-07 ; il ne manque que
  l'exécution. Bloqué : la carte physique.

---

## Preuve, outillage et dettes de mesure

Ce que les gates ne prouvent pas encore, et ce qui rend une preuve fragile.

- [ ] **Faire tomber le GUI sous un gate, et lui passer la main dessus.**
  Les quatre fenêtres (Périphériques, AppleTalk / Ethernet, Disques,
  Moteur) sont sous `gui_windows_test`, le formulaire « Configuration des
  services » compris ; la fenêtre machine (barre de menus, tableau de bord,
  surface écran et clavier, mode borne, presets CRT) sous
  `gui_machine_window_test` sur une machine factice (2026-09-16). Reste :
  l'upload du framebuffer et le hot-swap floppy/CD par les bindings des six
  runners (`compactDiskBaysHost` et ses pairs, jamais instanciés hors GL),
  et la passe save-state GUI de bout en bout — le gate voit la demande
  arriver dans le slot, pas le fichier écrit ; les trois gates de relance
  du 2026-09-08 sont hors GUI.
- [ ] **Fermer le `-Wstringop-overflow` de GCC 13 + LTO sur
  `EtherLink::sendToGuest`.** Latent parce que le job `-Werror` construit
  sans LTO : la combinaison qui l'expose n'est couverte par aucun gate.
- [ ] **Exécuter les locksteps sur un hôte Windows.** Préalable nommé de
  « `threaded` est le plancher Windows » : tant qu'aucun hôte Windows ne
  les exécute, le choix reste une décision et non une mesure.

---

## Services réseau

**Chantier du jalon 3.** Fermer les deux mécanismes non élucidés, livrer
les contrôles produit, puis n'ajouter du protocole que sur consommateur
observé.

- [ ] **Payer les dettes de preuve du contrôle DaynaPort au GUI.** Le
  contrôle est complet (ligne d'état, bascule câble, ID SCSI sans variable
  d'environnement) et la fenêtre est cliquée sous gate — mais seulement
  pile désactivée, puis, le soir même, sous sa forme complète : hub attaché,
  formulaire rendu, case AppleShare et « Appliquer » jusqu'au hub
  (`gui_windows_test`). Le re-exec est observé depuis le 2026-09-16
  (`gui_relaunch_smoke_test` : la génération 1 met la carte en attente comme
  « Appliquer » et se ré-exécute, la génération 2 voit la carte en SCSI 3 —
  sur surface GL seulement, la ROM de démonstration, l'invité ne la sonde
  pas). Restent : aucun invité n'a sondé la carte après un relaunch ;
  « Révéler » lance `open` / `xdg-open` / `explorer` sans gate possible ;
  aucun invité n'a remonté un serveur AFP renommé à chaud
  (`afp_server_test` prouve la ré-inscription NBP, pas le Sélecteur).
- [ ] **Élucider pourquoi une date serveur mouvante produisait une seconde
  trajectoire AFP.** Le gate est déterministe (date épinglée) ; le
  mécanisme reste ouvert : on ne sait pas par quoi une date qui avance
  faisait diverger le montage post-reconnexion (2 commandes AFP, 9 trames,
  une réponse en retard de 1,44 s).
- [ ] **Rejouer sur x86-64 date-épinglé la comparaison entre hôtes de
  `q605_afp_live_etalon`.** Référence M4 :
  `scratchpad/2026-09-13/afp_live_trace_aarch64.txt` ;
  `tools/afp_trace_diff.py` situe la première frontière. Le rejeu A64
  retrouve les 22 frontières ; sous `POM68K_CPU_ENGINE=interp` le gate
  passe mais diverge à la frontière 14 (serveurs après reconnexion).
  Localiser cet écart interp/A64 avant toute attribution à l'hôte. La
  référence `scratchpad/2026-09-11/afp_live_trace_x86_64.txt` est
  antérieure à l'épinglage : produire d'abord une référence x86-64
  date-épinglée.
- [ ] **Compléter MacIP : window scaling TCP.** Le réassemblage IP est
  fait. Reste le window scaling, au-dessus d'un endpoint volontairement
  in-order-only (MSS 536) : décider d'abord si cette simplification tombe.
  ICMP sortant est bloqué par l'hôte (`net.ipv4.ping_group_range` = `1 0`) :
  un gate ne pourrait que se sauter. À rouvrir sur un hôte dont la plage
  couvre le gid, ou avec `CAP_NET_RAW`.
- [ ] **Exécuter une session AppleShare complète sur le bridge réel.** Les
  sessions passées vont au serveur in-process. Lancer netatalk ou
  TashRouter, monter « Input » depuis le Chooser, vérifier un transfert.
  C'est aussi là que les adresses multicast de zone
  (`09:00:07:00:00:xx`) se poseraient : le routeur interne répond
  UseBroadcast et la liste de `SET MULTICAST ADDRESS` est acceptée et
  ignorée (`DaynaPort.h`).
- [ ] **Tester l'interop Mini vMac LToUDP.** Même groupe multicast, les
  deux directions.
- [ ] **Étendre le sous-ensemble AFP — seulement sur consommateur observé.**
  DID relatifs traités, Desktop DB volontairement bouchonné, seuls
  `FPCopyFile` (5) et `FPCatSearch` (43) manquent réellement. Une session
  live Mac OS 8.1 rapporte `refused=0/-` aux 22 frontières. Le serveur
  compte les opcodes refusés ; ne rien implémenter avant ce signal.
- [ ] **Ajouter des UAM sûrs.** DHX/random-number lorsqu'un invité refuse
  le cleartext.
- [ ] **Compléter PAP.** Polling de statut, configuration des files et
  sélection CUPS dans le GUI.

---

## Fidélité matérielle et LLE

Tout ajout LLE part d'une trace ROM/pilote, d'un observable invité ou d'un
consommateur réel. Une approximation plus large sans preuve n'est pas un gain.

- [ ] **Comparer le bus et les timings V8 à du matériel réel.** Couvrir IRQ,
  VBL, VIA et mémoire, puis diagnostiquer l'assombrissement après très
  longue exécution. Inclut le Classic II : le bloc derrière `$50F18038`,
  que sa ROM déréférence, n'a jamais été identifié
  (`POM68K_V8_IOHOLE=1`).
- [ ] **Affiner VIA/RTC sur les compacts.** Latences T1/T2/IFR à un cycle,
  alignement E-clock/IACK, et initialiser le RTC GUI depuis l'hôte sans
  rendre les tests non déterministes.
- [ ] **Améliorer la précision sonore des compacts.** Le DAC hôte et la
  courbe DFAC/V8 sont faits. Reste propre aux compacts : lecture du buffer
  par scanline et modélisation du PWM disque.
- [ ] **Trancher le DFAC2 et les machines sans DFAC.** Payload DFAC2
  (Color Classic, Color Classic II) volontairement ACK-only — l'interpréter
  rendrait la machine muette ; le Mac TV n'a pas de DFAC ; aucun filtre
  analogique n'est synthétisé. Décider défaut vs contrat, avec un
  observable invité.
- [ ] **Étendre les commandes Cuda du Q605/LC 475 uniquement depuis des
  traces ROM/pilote.** Timing pin-level 040 et commandes réellement
  observées, pas une nouvelle approximation.
- [ ] **Compléter le low tier SCC seulement avec un consommateur.**
  Transport PTY/TCP clos. Ajouter 8530/85C30/ESCC lorsqu'une machine les
  demande, puis WR9 VIS/NV et DPLL avec gates, en préservant le LLAP déjà
  plus complet que l'oracle MAME.
- [ ] **Revalider l'arithmétique de zones GCR.** La survie des flux hors
  cadence est faite (`SonyDriveFlux.cpp`). Ne reste que les zones GCR, et
  seulement avec un symptôme ou un corpus.
- [ ] **Élucider la divergence entre hôtes de `lcii_floppy_etalon`.** Même
  gate, même code, mêmes actifs : l'hôte x86-64 échoue (309 598 quartets,
  éjection en 60 images, aucun dossier) là où le journal committé à
  `662a64f` passe (586 503 quartets, 180 images, `untitled folder` 0 → 2)
  — l'hôte de ce run passant n'est pas consigné. L'invité éjecte ; c'est
  le dossier qui échoue. La divergence commence dès l'insertion (moitié
  des quartets, TKO=1, aucune marque GCR `D5 AA 96`). **Fait :** le gate
  passe sur le M4. Suite : instrumenter IWM/SWIM1 des deux côtés depuis la
  première lecture qui diffère, ce qui demande l'hôte x86-64. Repro :
  `POM68K_BEYOND=floppy build/lcii_beyond_etalon`. Évidence :
  `scratchpad/2026-09-12/floppy/`.
- [ ] **Re-tester le chemin 030 de « SANE sans FPU ».** Sur la forme LC II
  le mécanisme existe et est correctement paramétré. Ce qui n'est pas su :
  le 030 a-t-il besoin de la sélection `UniversalInfo`/`defaultRSRCs` qui
  a réparé le côté 040 ? Question à re-tester, pas à re-diagnostiquer
  (`docs/BASILISK_ROM_NOTES.md` § 8.5).
- [ ] **Décider les échéanciers Mac II et Duo avec un gate sensible à la
  gigue.** Garder les options expérimentales tant qu'aucun observable ne
  justifie leur coût. Leçon du Q605 : un gate qui écrit la valeur qu'il
  vérifie ne prouve pas le défaut du composant.
- [ ] **Ajouter des etalons pixel-accurate et un build WASM.** Assets
  privés soft-skippables, captures stables. Le WASM n'a aujourd'hui que
  des stubs inactifs.

---

## Médias optiques

- [ ] **Établir la règle des images 512/2048 octets.** Comparer hybrides et
  bare-HFS avec un vrai pilote/MAME avant de modifier le montage.
- [ ] **Ajouter CDDA.** TOC audio, PLAY/PAUSE et le chemin sonore vers
  l'ASC avec un gate consommateur.
- [ ] **Trancher ce que « supporter `.cue/.bin` » veut dire, puis les rips
  2352.** Le balayage média du 2026-09-09 affirme couvrir « ISO/CUE/BIN
  handling » : ou bien cet item se réduit aux pistes multiples en 2352, ou
  bien cette affirmation sur-promet. Trancher avant d'écrire du code.

---

## Nouvelles machines

Les nouveaux contrôleurs sont prouvés sur le premier profil consommateur
avant d'être généralisés ; une ligne catalogue se mérite par une cellule
Finder **plus** le câblage GUI et save-state. Le Mac 128K/512K est livré.

- [ ] **Ajouter les variantes Duo 210 et 250.** Exploiter les IDs déjà
  présents, introduire la sélection de profil et ajouter les lignes
  catalogue/gates (après `duo230_sleep_etalon`).
- [ ] **Ajouter Duo 270c puis Duo 280.** CSC couleur puis le chemin 68040,
  après validation des variantes proches.
- [ ] **Ajouter PowerBook 150.** Framebuffer LCD/GSC, IDE, box ID et PMU
  68HC05 à partir de sa ROM.
- [ ] **Ajouter PowerBook 140–180 puis Portable/PB100.** Power Manager
  M50753 et framebuffer LCD comme nouvelle brique partagée.
- [ ] **Étendre NuBus et la vidéo sur slot.** Porter les cartes au-delà du
  Toby Mac II vers IIx/IIcx/IIci et les Quadra concernés.
- [ ] **Ajouter le target ATA/IDE du Q630/LC580.** Brancher un disque et
  créer un gate de boot qui n'utilise pas SCSI.

---

## Moteur — études conditionnées à un profil temporel

Reliquat du palier B clos. Aucune de ces lignes n'est une lacune de
conformité ; chacune n'ouvre qu'avec un profil temporel reproductible et se
mesure en ABBA intra-binaire, empreintes identiques. Conformité, stabilité
du tier entier, et performance mesurée dans le même processus — aucune ne
se déduit d'une autre.

- [ ] **Décider le sort de l'admission late-poll : rentabilité seulement.**
  Position des polls dans l'IR et admission A64 prouvées conformes, mais
  mesurées −6,3 % sur le bench cache-actif (boucles de poll chaudes du
  boot). Knob `POM68K_JIT_040_LATE_POLL` opt-in. Ne rouvrir que si un
  workload cache-actif montre le gain — ou après une dé-admission
  adaptative des sites qui manquent chroniquement.
- [ ] **Ne pas rouvrir l'écart d'admission 68030 sans profil temporel neuf.**
  Chiffré le 2026-09-06 et refusé : parité opcode zéro et gatée, le seau
  non supporté plafonne à **1,24 %** contre un plancher de 10 ‰. Une règle
  68k commune vit dans l'IR/coût partagé, jamais dans un emitter.
  Évidence : `scratchpad/2026-09-05/b3probe/ADMISSION_GAP.md`. La moitié
  `a64` n'a jamais tourné.
- [ ] **Mesurer le cache de dispatch de `jit::Engine`.** Le sortir des 1 Mo
  en ligne a guéri les fixtures qui segfaultaient ; la mesure n'a jamais
  été faite. Conséquences déjà payées : `/STACK:16777216` sur MSVC et
  15+154 fixtures déplacées sur le tas.
- [ ] **Attribuer les +6 % du bras natif a64 sur le Q605** contre la
  référence du 2026-08-23, et le delta de banc borné mais non attribué du
  2026-09-03 : les deux sont « notés plutôt que poursuivis ».
- [ ] **Isoler ou amplifier les familles Speedometer avant toute
  promotion.** A64 et `threaded` terminent chaque famille aux mêmes
  trames/empreintes/écrans/SCSI. Les profils restent dominés par le boot
  (0,274–0,277 s de CPU utile). QuickDraw : natif à 99,7 % mais son cache
  multi-version de gardes donne **+1,75 %** en ABBA — candidat retiré.
  FPU : fermé le 2026-09-07. Avant de rouvrir un lowering, répéter une
  famille dans l'invité ou échantillonner sa phase seule. Évidence :
  `scratchpad/2026-09-06/a64-m030/SPEEDOMETER_TIME_PROFILE.md`.
- [ ] **Étudier `PFLUSHA` et le retry d'armement seulement après profil.**
  Toute réduction des bumps ou du backoff doit garder les locksteps
  030/040 : le moment où une fenêtre s'arme est observable sur 68040.
- [ ] **Re-mesurer avant de compacter `mmu040InstrStart`.** Les 3,26 %
  venaient du profil 68040 du 2026-09-02, périmé par le cache de dispatch
  (−29,6 %) : le seau MMU/cache est à 7,1 % de la phase de jeu. Après la
  décision late-poll.
- [ ] **Profiler puis isoler les stores à masque nul.** N'ouvrir une
  spécialisation conforme qu'après un profil temporel et des preuves
  empreinte/compteurs/gates identiques.

---

## Recherche conditionnelle

- [ ] **Définir puis expérimenter le profil d'accélération non conforme.**
  Critères fonctionnels, défauts par famille, `purity mode` des gates, et
  un opt-in qui ne puisse jamais contaminer l'oracle. Cette voie reste
  derrière la voie conforme (`docs/HLE_OVERLAY.md`, 2026-08-09).
