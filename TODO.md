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
  pas). Le serveur renommé à chaud est remonté par l'invité depuis le
  2026-09-17 (`q605_afp_rename_etalon` : `hub.reconfigure()` entre les
  deux cycles, le Sélecteur ne liste que le nouveau nom, login et copie).
  La sonde invité de la carte après un relaunch est tranchée le 2026-09-17
  et n'appelle pas de gate léger : la DaynaPort est une cible SCSI qui
  n'est pas un disque, que le Mac ignore au démarrage tant que l'ADEV
  SCSI/Link de Dayna n'est pas installé et n'ouvre pas la carte (mesuré :
  zéro CDB vers l'ID 3 sur un boot complet sans le pilote). « Sonder la
  carte » est donc indissociable de l'installation du pilote, que
  `q605_dayna_driver_etalon` fait déjà (present -> enabled -> AARP, ~400 s) ;
  et une carte arrivée par `--daynaport=3` (ligne de relance) ou par
  attache directe est identique sur le bus. La paire
  `gui_relaunch_smoke_test` + `q605_dayna_driver_etalon` couvre la dette ;
  un boot GUI en avance rapide n'atteint pas l'état pilote-chargé dans un
  budget de smoke. Reste : « Révéler » lance `open` / `xdg-open` /
  `explorer` sans gate possible.
- [ ] **Situer l'écart `a64` de `q605_afp_live_etalon` à la frontière 14.**
  Tranché le 2026-09-18 : il n'y a **pas** d'écart entre hôtes. Interp
  x86-64, `x64` x86-64 et interp AArch64 sont identiques aux 22 frontières
  (horloge ET empreinte) ; seul le défaut `a64` du 2026-09-13 s'en écarte,
  dès `afp_live_3_servers` après reconnexion — 8 cycles de retard là, puis
  des totaux réseau différents en fin de course (afp 210 vs 209, trames
  678/507 vs 676/506). Le gate passe dans tous les bras : seule la trace le
  voit. Reste à bisecter le bras `a64` entre les frontières 13 et 14 (du
  Sélecteur rouvert à la liste de serveurs repeinte), ce qui demande un
  hôte AArch64. Références date-épinglées :
  `scratchpad/2026-09-18/afp/`.
- [ ] **MacIP : ICMP sortant.** Bloqué par l'hôte
  (`net.ipv4.ping_group_range` = `1 0`) : un gate ne pourrait que se
  sauter. Mesuré aussi sur l'hôte x86-64 le 2026-09-18 — même `1 0`, gid
  1000 : les deux hôtes du projet le refusent, inutile de revérifier. À
  rouvrir sur un hôte dont la plage couvre le gid, ou avec `CAP_NET_RAW`. Le window scaling TCP est tranché le 2026-09-16 : rien à
  bâtir tant qu'aucun invité ne le demande — le compteur
  `Status::tcpSynWindowScale` (option kind 3 sur un SYN invité,
  `macip_gw_test`) est le signal ; l'endpoint reste in-order-only, MSS 536,
  32 × MSS en vol.
- [ ] **Poser les adresses multicast de zone (`09:00:07:00:00:xx`).** Le
  routeur interne répond UseBroadcast et la liste de `SET MULTICAST
  ADDRESS` est acceptée et ignorée (`DaynaPort.h`). Rouvrir sur un
  consommateur : la session sur bridge réel, faite le 2026-09-18, ne l'a
  pas demandée (LocalTalk, pas EtherTalk). La session elle-même est close
  — netatalk 2.4.9 via TashRouter, volume « Input » monté, dossier créé et
  copie deux-fourches vérifiée octet à octet sur l'hôte, deux passes aux
  mêmes chiffres ; harnais `q605_afp_bridge_probe`, évidence
  `scratchpad/2026-09-18/bridge/`.
- [ ] **Faire apprendre au hub son numéro de réseau au lieu de l'affirmer.**
  `AtalkHub::attach` configure la pile en **réseau 2, nœud 128** en dur
  (`stack_.configure(2, 128, …)`), quel que soit le segment. Constaté le
  2026-09-18 pendant l'interop Mini vMac : le segment LToUDP est le réseau 1
  (semé par TashRouter) et celui de netatalk le réseau 2 — deux réseaux
  différents portant le même numéro, notre hub sur le mauvais par
  construction. Rien n'a échoué (NBP porte l'adresse, et le nœud 128 est
  joignable localement), mais un invité qui se fie au numéro serait induit
  en erreur, et la trace le montre : la première tentative de Mini vMac est
  partie en `25->125 local` avant de repasser par le routeur. Le correctif
  est d'apprendre le réseau par RTMP ; il demande un gate à lui.
  L'interop Mini vMac elle-même est close : Mini vMac 37.03 (`-lt -lto
  udp`) a monté le volume servi par NOTRE pile et y a lu un fichier, session
  ATP de nœud à nœud sans routeur. Évidence :
  `scratchpad/2026-09-18/minivmac/`.
- [ ] **Compléter PAP.** Polling de statut, configuration des files et
  sélection CUPS dans le GUI.

---

## Fidélité matérielle et LLE

Tout ajout LLE part d'une trace ROM/pilote, d'un observable invité ou d'un
consommateur réel. Une approximation plus large sans preuve n'est pas un gain.

- [ ] **Comparer le bus et les timings V8 à du matériel réel.** Couvrir IRQ,
  VBL, VIA et mémoire, puis diagnostiquer l'assombrissement après très
  longue exécution. Le bloc derrière `$50F18038` que la ROM du Classic II
  déréférence est identifié le 2026-09-17 : rien. L'Eagle utilise
  `v8_device::map` sans surcharge (MAME `v8.cpp:57`), qui ne décode pas
  `$518xxx` ; seul le Sonora/Spice y mappe `$518000` (luminosité/contraste
  de son écran intégré, `v8.cpp:700`), que l'écran fixe 512×342 de l'Eagle
  n'a pas. C'est donc du bus ouvert, un pointeur sauvage sans consommateur —
  `classic2_boot_etalon` boote au Finder sans jamais toucher `$F18xxx`
  (`POM68K_V8_IOHOLE=200` : zéro accès). Reste le timing cycle vs matériel
  réel.
- [ ] **Affiner VIA/RTC sur les compacts.** Latences T1/T2/IFR à un cycle,
  alignement E-clock/IACK. Le RTC GUI est semé depuis l'hôte
  (`services.hostMacSeconds()`) sur les six plateformes GUI (Compact, Toby,
  V8, Dafb, Sonora, Duo — `Platform*.cpp`), et les tests restent
  déterministes en n'appelant jamais `setSeconds` (image usine à 0) :
  clause close le 2026-09-17. Reste le timing cycle des latences.
- [ ] **Améliorer la précision sonore des compacts.** Le DAC hôte et la
  courbe DFAC/V8 sont faits. Reste propre aux compacts : lecture du buffer
  par scanline et modélisation du PWM disque.
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
- [ ] **Expliquer la réouverture de la fenêtre de volume à l'insertion.**
  Le rouge x86-64 de `lcii_floppy_etalon` est tranché le 2026-09-18 et le
  gate est vert : le lecteur ne refusait rien (24 Primes, 2 Controls, zéro
  échec, `_MountVol` noErr), c'est la fenêtre au premier plan qui différait
  — le Cmd-N créait le dossier dans la fenêtre Games du volume de
  démarrage. Le geste nomme désormais la sienne. Reste la question de
  fidélité, sans consommateur pressé : un Mac réel rouvre les fenêtres
  qu'un volume avait ouvertes à son éjection, donc de l'état de VOLUME et
  non d'hôte. Première expérience : éjecter fenêtre racine ouverte,
  réinsérer l'image écrite, regarder si le Finder la rouvre
  (`lcii_sony_trace --img`, la ligne « centre white » tranche seule : 0,65
  sans fenêtre, 0,91 avec). Évidence : `scratchpad/2026-09-18/floppy/`.
- [ ] **Ajouter des etalons pixel-accurate et un build WASM.** Assets
  privés soft-skippables, captures stables. Le WASM n'a aujourd'hui que
  des stubs inactifs.

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
- [ ] **Ajouter PowerBook 150.** Framebuffer LCD/GSC, IDE (le target ATA
  existe et amorce déjà un Q630 : `AtaDisk`, `q630_ide_boot_etalon`), box ID
  et PMU 68HC05 à partir de sa ROM.
- [ ] **Ajouter PowerBook 140–180 puis Portable/PB100.** Power Manager
  M50753 et framebuffer LCD comme nouvelle brique partagée.
- [ ] **Étendre NuBus et la vidéo sur slot.** Porter les cartes au-delà du
  Toby Mac II vers IIx/IIcx/IIci et les Quadra concernés.
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
