# TODO — feuille de route

Ce fichier contient uniquement du travail ouvert. Les résultats, mesures,
fausses pistes et décisions terminées vivent dans `CHANGELOG.md` et
`CHANGELOG_INDEX.md`. Les détails d'implémentation vivent dans `DEV.md`,
`src/jit/POM68K_JIT.md`, `docs/JIT_BRINGUP.md` et les notes vendor.

`STATUS.md` est généré depuis les manifests CMake et reste la source de vérité
pour les gates. Toute tâche fermée quitte ce fichier dans le même changement
qui ajoute son entrée au `CHANGELOG`. Une tâche dit ce qui **reste** : ce qui
a été tranché en chemin part au `CHANGELOG`, la tâche n'en garde qu'un
pointeur.

**Un renvoi extérieur cite le NOM d'une section, jamais son numéro** — écrire
`TODO.md § Services réseau`, abrégeable à son premier mot quand la place
manque (`§ Preuve`, `§ Services`, `§ Fidélité`, `§ Nouvelles machines`,
`§ Moteur`, `§ En sommeil`, `§ Recherche`). Les numéros bougent à chaque
réorganisation.

## Statut

- **En cours : jalon 2** (le produit prouvé). Jalon 1 clos le 2026-09-16
  (registre complet sur les deux jambes, version 0.2.0) ; paliers B et C
  clos les 2026-09-07/08. Census et preuves datées : `STATUS.md`,
  `CHANGELOG.md`.
- Ordre 2 → 3 → 4 → 5, décidé le 2026-09-16 : le jalon 2 rend chaque
  suivant vérifiable par quelqu'un d'autre que l'hôte qui l'a produit.
  Chaque section de jalon ouvre sur son critère de sortie ; ce qui en est
  déjà atteint est daté au `CHANGELOG`.
- Admission : une nouvelle machine part d'un gate produit réutilisable de
  sa plateforme ; un ajout LLE part d'une trace, d'un observable invité ou
  d'un consommateur réel ; une optimisation dépend d'un profil temporel
  reproductible et se mesure en ABBA intra-binaire à empreintes identiques.
- Une tâche marquée *Bloqué :* nomme la ressource qui la débloque. Une
  tâche qui attend un signal plutôt qu'une ressource vit en § En sommeil.

---

## Jalon 2 — Preuve, outillage et dettes de mesure

**Sortie :** chaque fenêtre a un gate ou une passe manuelle datée ; un run
complet publié par la CI sur un runner à assets ; version 0.3.

- [ ] **Installer un runner auto-hébergé avec les assets.** Rendre le palier
  `full` déclenchable par push et publier `LastTest.log` + le census
  exécutés/soft-skips. *Bloqué : infrastructure/hôte à provisionner.*
- [ ] **Exécuter les locksteps sur un hôte Windows.** Préalable nommé de
  « `threaded` est le plancher Windows » : tant qu'aucun hôte Windows ne
  les exécute, le choix reste une décision et non une mesure. *Bloqué :
  hôte Windows.*
- [ ] **Dater une passe manuelle de « Révéler » (fenêtre DaynaPort).** Seul
  reste de la dette de preuve du contrôle DaynaPort au GUI : le bouton
  lance `open` / `xdg-open` / `explorer`, qu'aucun gate ne peut observer.
  Le reste est couvert par `gui_windows_test`, `gui_relaunch_smoke_test`,
  `q605_afp_rename_etalon` et `q605_dayna_driver_etalon` (`CHANGELOG`
  2026-09-16/17).
- [ ] **Introduire Retro68 comme oracle invité différentiel.** Sondes
  Toolbox/Device Manager/XPRAM comparées sous MAME et POM68K ; la toolchain
  est installée sur le M4 (`dev/Retro68-build`). *Bloqué : romset `maclc2`
  bâti depuis notre ROM, tap Lua, recette du co-trace SWIM du LC II, et la
  première sonde à écrire.*

---

## Jalon 3 — Services réseau

**Sortie :** session réelle sur bridge externe verte (faite le
2026-09-18), zéro opcode refusé sur les sessions live (fait le 2026-09-19,
Mac OS 8.1 et System 7.0), les deux mécanismes expliqués ou tranchés. Puis
livrer les contrôles produit, et n'ajouter du protocole que sur
consommateur observé (§ En sommeil).

- [ ] **Bisecter le bras `a64` de `q605_afp_live_etalon` entre les
  frontières 13 et 14** (du Sélecteur rouvert à la liste de serveurs
  repeinte). Interp x86-64, `x64` x86-64 et interp AArch64 sont identiques
  aux 22 frontières ; seul le défaut `a64` s'en écarte, dès
  `afp_live_3_servers` (8 cycles de retard, puis des totaux réseau
  différents). Le gate passe dans tous les bras : seule la trace le voit.
  Références : `scratchpad/2026-09-18/afp/`. *Bloqué : hôte AArch64.*
- [ ] **Compléter PAP.** Polling de statut, configuration des files et
  sélection CUPS dans le GUI.

---

## Jalon 4 — Fidélité matérielle et LLE

**Sortie :** un jeu CD avec audio joué de bout en bout (fait le
2026-09-19), la divergence LC II attribuée (faite le 2026-09-18), N profils
sous etalon pixel-accurate (38 sur 39 le 2026-09-25). Tout ajout LLE part
d'une trace ROM/pilote, d'un observable invité ou d'un consommateur réel ;
une approximation plus large sans preuve n'est pas un gain.

- [ ] **Épingler le LC 520, relever la jambe M4, et le build WASM.** 38
  profils sur 39 épinglés ici (`tools/pixel_pins.tsv`, clé
  `<gate>@<volume>`). Le LC 520 démarre ici son repli GISTPERSO, qui laisse
  une alerte : son gate est rouge et l'écran n'est pas épinglé — il attend
  `hdv/ref/System 7.5.3 HD.dsk` (au lock, absent de cet hôte). Sur le M4,
  relever les clés des volumes que cet hôte n'a pas (7.5.3 pour LC III,
  III+ et 520 ; System 3.3 pour le Plus). Le WASM n'a que des stubs
  inactifs.
- [ ] **Comparer le bus et les timings V8 à du matériel réel.** IRQ, VBL,
  VIA et mémoire, puis diagnostiquer l'assombrissement après très longue
  exécution. (Le bloc `$50F18038` du Classic II est du bus ouvert, tranché
  le 2026-09-17.)
- [ ] **Affiner les latences VIA sur les compacts.** T1/T2/IFR à un cycle,
  alignement E-clock/IACK. (Le RTC semé depuis l'hôte est clos le
  2026-09-17.)
- [ ] **Améliorer la précision sonore des compacts.** Lecture du buffer par
  scanline et modélisation du PWM disque (DAC hôte et courbe DFAC/V8
  faits).

---

## Jalon 5 — Nouvelles machines : les portables

**Sortie :** la famille PowerBook boote au Finder avec preuve au-delà du
boot, catalogue et save-state câblés. Les nouveaux contrôleurs sont prouvés
sur le premier profil consommateur avant d'être généralisés ; une ligne
catalogue se mérite par une cellule Finder **plus** le câblage GUI et
save-state.

- [ ] **Créer `duo230_sleep_etalon`.** Sommeil clapet, arrêt CPU, flush
  disque, réveil complet. Milestone 6 de `docs/DUO_BRINGUP.md` : fermer le
  clapet gèle le CPU mais le System ne lance aucune procédure de sommeil
  et rouvrir ne réveille pas. *Bloqué : code System de gestion d'énergie
  ou spec PMU.*
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

## En sommeil — rouvrir sur déclencheur

Pas du travail à planifier : chaque ligne attend un signal nommé. Quand il
arrive, la tâche remonte dans la section de son jalon.

- [ ] **MacIP : ICMP sortant.** *Rouvrir sur* un hôte dont
  `net.ipv4.ping_group_range` couvre le gid, ou avec `CAP_NET_RAW` (les deux
  hôtes du projet sont à `1 0`, mesuré le 2026-09-18 ; un gate ne pourrait
  que se sauter). Le window scaling TCP attend de même le compteur
  `Status::tcpSynWindowScale` (`macip_gw_test`).
- [ ] **Poser les adresses multicast de zone (`09:00:07:00:00:xx`).** *Rouvrir
  sur* un consommateur EtherTalk : le routeur interne répond UseBroadcast et
  la liste `SET MULTICAST ADDRESS` est acceptée et ignorée (`DaynaPort.h`) ;
  la session bridge du 2026-09-18 ne l'a pas demandée.
- [ ] **Compléter le low tier SCC.** *Rouvrir sur* une machine qui demande
  8530/85C30/ESCC, puis WR9 VIS/NV et DPLL avec gates, en préservant le
  LLAP déjà plus complet que l'oracle MAME.
- [ ] **Étendre les commandes Cuda du Q605/LC 475.** *Rouvrir sur* une trace
  ROM/pilote : timing pin-level 040 et commandes réellement observées.
- [ ] **Revalider l'arithmétique de zones GCR.** *Rouvrir sur* un symptôme ou
  un corpus (la survie des flux hors cadence est faite,
  `SonyDriveFlux.cpp`).
- [ ] **Expliquer la réouverture de la fenêtre de volume à l'insertion.**
  *Rouvrir sur* un consommateur : un Mac réel rouvre les fenêtres qu'un
  volume avait ouvertes à son éjection (état de VOLUME, pas d'hôte).
  Première expérience : éjecter fenêtre racine ouverte, réinsérer l'image
  écrite, regarder si le Finder la rouvre (`lcii_sony_trace --img`, ligne
  « centre white » : 0,65 sans fenêtre, 0,91 avec). Évidence :
  `scratchpad/2026-09-18/floppy/`.

---

## Hors jalon

### Moteur — études conditionnées à un profil temporel

Reliquat du palier B clos. Aucune ligne n'est une lacune de conformité ;
chacune n'ouvre qu'avec un profil temporel reproductible et se mesure en
ABBA intra-binaire, empreintes identiques. Conformité, stabilité du tier
entier et performance mesurée dans le même processus — aucune ne se déduit
d'une autre.

Ordre : late-poll → `mmu040InstrStart` ; tout le reste attend son propre
profil. Les deux lignes `a64` sont *Bloquées : hôte AArch64*, comme la
bisection du jalon 3 — une session AArch64 les traite ensemble.

- [ ] **Décider le sort de l'admission late-poll : rentabilité seulement.**
  Prouvée conforme mais −6,3 % sur le bench cache-actif. Knob
  `POM68K_JIT_040_LATE_POLL` opt-in. Rouvrir sur un workload cache-actif
  qui montre le gain, ou après une dé-admission adaptative des sites qui
  manquent chroniquement.
- [ ] **Re-mesurer avant de compacter `mmu040InstrStart`** (après
  late-poll). Les 3,26 % venaient d'un profil 68040 périmé par le cache de
  dispatch ; le seau MMU/cache est à 7,1 % de la phase de jeu.
- [ ] **Ne pas rouvrir l'écart d'admission 68030 sans profil temporel
  neuf.** Refusé le 2026-09-06 : le seau non supporté plafonne à 1,24 %
  contre un plancher de 10 ‰. Une règle 68k commune vit dans l'IR/coût
  partagé, jamais dans un emitter. La moitié `a64` n'a jamais tourné.
  Évidence : `scratchpad/2026-09-05/b3probe/ADMISSION_GAP.md`.
- [ ] **Attribuer les +6 % du bras natif a64 sur le Q605** contre la
  référence du 2026-08-23, et le delta de banc borné mais non attribué du
  2026-09-03.
- [ ] **Isoler ou amplifier les familles Speedometer avant toute
  promotion.** Profils dominés par le boot (0,274–0,277 s de CPU utile) ;
  candidat QuickDraw retiré (+1,75 %), FPU fermé le 2026-09-07. Avant de
  rouvrir un lowering, répéter une famille dans l'invité ou échantillonner
  sa phase seule. Évidence :
  `scratchpad/2026-09-06/a64-m030/SPEEDOMETER_TIME_PROFILE.md`.
- [ ] **Étudier `PFLUSHA` et le retry d'armement seulement après profil.**
  Toute réduction des bumps ou du backoff doit garder les locksteps
  030/040 : le moment où une fenêtre s'arme est observable sur 68040.
- [ ] **Profiler puis isoler les stores à masque nul.** Spécialisation
  conforme seulement après un profil temporel et des preuves
  empreinte/compteurs/gates identiques.

### Mesure sur matériel

- [ ] **Établir la ligne de base POM68K sur un vrai Pi 400**, puis **rejouer
  l'A/B release native/PGO/LTO** (`jit_bench`/`jit_bench_lcii`, budget
  invité fixe, empreintes archivées, `-mtune`/LTO/PGO séparés). Le paquet
  Cortex-A76 est archivé depuis le 2026-09-07. *Bloqué : la carte
  physique.*

### Recherche conditionnelle

- [ ] **Définir puis expérimenter le profil d'accélération non conforme.**
  Critères fonctionnels, défauts par famille, `purity mode` des gates, et
  un opt-in qui ne puisse jamais contaminer l'oracle. Cette voie reste
  derrière la voie conforme (`docs/HLE_OVERLAY.md`, 2026-08-09).
