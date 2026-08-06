# Revue des simplifications délibérées — garder ou fermer ?

**Date : 2026-08-06.** Évaluation de décision sur les ~40 simplifications relevées par
l'audit de parité MAME (`MAME_PARITY_AUDIT.md` § 2.x), croisées avec l'inventaire
`LLE_VS_HLE.md`. Trois verdicts possibles :

- **GARDER** — le rapport coût/bénéfice est mauvais : aucun consommateur connu, ou la
  parité stricte serait *pire* (mute une machine, casse un fix mesuré). On ne touche pas.
- **FERMER** — un bénéfice guest-visible existe ; entrée à mettre au backlog avec son gate.
- **ÉPINGLER** — on garde le raccourci mais il manque l'inventaire/le commentaire/la
  condition de réouverture exigés par la règle maison (`LLE_VS_HLE.md` § 5).

Rappel de la règle qui domine tout : *« Adding fidelity on top of unverifiable coverage
is work with no way to know it landed »* — 25 profils sur 36 ne sont épinglés que par
« a atteint le Finder ». Toute fermeture recommandée ici doit apporter son gate.

---

## 1. Verdict d'ensemble

**28 simplifications sur 40 : GARDER.** Elles partagent le même profil : la fonctionnalité
absente n'a **aucun producteur/consommateur** dans le parc logiciel visé (ROMs Apple,
System 6→8.1, drivers expédiés), et la moitié sont déjà inventoriées dans `LLE_VS_HLE.md`
avec leur condition de réouverture. Les refaire serait de la fidélité sans oracle ni gate.

**7 : FERMER** (§ 2 ci-dessous) — celles qui ont un bénéfice utilisateur ou un chantier
déjà ouvert qui les absorbe.

**5 : ÉPINGLER** (§ 3) — raccourcis légitimes mais non conformes à la règle maison
(pas d'entrée d'inventaire, pas de condition de réouverture écrite).

---

## 2. À FERMER — par rapport bénéfice/effort décroissant

| # | Simplification | Pourquoi fermer | Effort | Gate à fournir |
|---|---|---|---|---|
| F1 | **Persistance PRAM absente sur 4 plateformes** (compacts, Mac II, IIfx, Duo — `Rtc` § 2.2) | Seul item **visible par l'utilisateur final** : réglages (souris, son, démarrage) perdus à chaque session ; MAME persiste partout. Le mécanisme `loadPram`/`savePram` existe déjà sur les 7 autres plateformes — c'est du câblage, pas de la conception. | Faible | Un test round-trip PRAM par famille (écrire via RTC série, relancer, relire) |
| F2 | **CLUT Sonora : duplication canal bleu en mono portrait** (§ 2.14) | RBV l'a déjà — le code existe dans le dépôt ; écart intra-projet plus que simplification. Trivial. | Trivial | Check dans un test vidéo Sonora existant |
| F3 | **Duo : ASC `$E8` au lieu du variant MSC `$E9`** (§ 2.8) | TODO in-file déjà posé ; clone + registre version. À faire avec le milestone Duo suivant. | Trivial | `msc_parity_test` (nouveau, déjà en place) |
| F4 | **Ligne reset PC3 Egret/Cuda = boot seulement** (§ 2.10) — un `RESET_SYSTEM $11` firmware n'atteint jamais la machine | Le chantier vague-1 #16 vient de construire **exactement** l'infrastructure nécessaire (callback reset + ré-armement overlay, `PgePmu.onCpuReset`) ; la généraliser aux plateformes Egret/Cuda est le même motif. C'est le chemin « Redémarrer » du Finder. | Moyen | Nouvelle etalon « Restart depuis le Finder » sur une machine Egret (aucune gate actuelle ne couvre ce chemin — c'était l'action 9 du rapport d'audit) |
| F5 | **Wavetable ASC classique = stub silence** (§ 2.8) | Seule simplification avec un **manque audible** plausible : jeux/apps Mac II-era utilisant le mode wavetable. MAME l'implémente (oracle disponible), le dump ASCTester aussi. | Moyen | Extension d'`asc_test` (mode wavetable, registres 2/3, sortie non-silence) |
| F6 | **Entrée Duo par cellule ADB au lieu de la matrice PMU** + **NVRAM PGE non persistée** (§ 2.12) | Déjà des milestones déclarés (`DUO_BRINGUP.md` « Next: input through the PMU », recette scrub `$91` notée). Pas une décision à prendre — un ordre à confirmer : les faire *avant* la ligne `kProfiles` Duo (règle maison : profil GUI = Finder + GUI/save-state). | Moyen×2 | Les gates du milestone Duo |
| F7 | **Floppy flux/PLL — cellules idéales** (§ 2.3/2.4, `LLE_VS_HLE` § 1.3) | Le seul gros chantier qui mérite de rester ouvert : étape 1/4 déjà faite (`FluxPll.h` gaté mais orphelin), et il activerait la machinerie LS-pair morte du SWIM1 + les bits d'erreur CSM jamais levés. **Mais** : aucun symptôme guest depuis le fix boost/denibble — priorité *derrière* la passe test-depth de `TODO.md`, pas devant. | Élevé | `flux_pll_test` existe ; ajouter la lecture via flux sous flag, comparée bit-à-bit au chemin octet |

**Ordre recommandé : F1 → F2/F3 (une après-midi à trois) → F4 → F5 → F6 → F7.**
F1-F4 sont des fermetures « sans regret ». F5-F7 méritent chacune une entrée TODO datée.

---

## 3. À ÉPINGLER — garder le raccourci, écrire ce qui manque

| # | Raccourci | Ce qui manque |
|---|---|---|
| E1 | **IRQ idle-empty-cycle ASC classique** (dérivée QEMU, inexistante chez MAME **et** sur le dump IIci) | C'est une *addition* non sourcée, pas une simplification : écrire la condition de réouverture (quel binaire du monde réel la requiert ?) ou planifier sa suppression contrôlée derrière un env-flag. Seul item de la liste où POM68K modélise *plus* que toutes ses sources. |
| E2 | **Timer 6805 fixé 512 cycles quel que soit le rate PLL** (§ 2.10) | Invisible avec le firmware expédié (le cheat rate-2→3 partagé le masque) — l'écrire dans `LLE_VS_HLE.md` § 1 avec cette justification et le firmware qui le briserait. |
| E3 | **Géométrie de trame V8 fixée modeline 12"** quel que soit le sense (§ 2.14 ; MAME fixe la 13", seul RBV a `recalcFrame()`) | Choix *différent* de MAME et non documenté : une ligne d'inventaire + le commentaire in-file. Fermer seulement si un jour le sense V8 devient sélectionnable dans le GUI. |
| E4 | **CI_COMPLETE instantané + seq non mis à jour après MSG_ACCEPT** (53C96, § 2.7) | Inventorié à moitié (§ 1.5 couvre le staging, pas ces deux-là) — les ajouter à la même entrée. |
| E5 | **Ready floppy = `!motorOn`** au lieu du compteur 2 tours (§ 2.3) | Une ligne d'inventaire § 1.3 ; le fix est trivial mais sans gate qui l'observe, il violerait la règle « fidélité sans couverture ». (L'agent vague-1 a noté la même classe sur `senseSwim` 0xF mfd51w — même ligne d'inventaire.) |
|  | *(également : divergence UM-vs-MAME du re-latch SCC vague 1, gate `!(HALF_B)` EASC vs ruling Sonora — déjà épinglées in-code par les agents, à reporter dans `LLE_VS_HLE.md` § 3 « fidelity facts that look like bugs »)* | |

---

## 4. À GARDER — liste compacte et pourquoi

**VIA 6522 (×4)** : polarité PCR fixe, shifter interne partiel, modes sortie CA2/CB2,
latching ports — aucune ROM expédiée ne les touche ; déjà couverts par DEV.md § M4.
**RTC (×2)** : pas de CKO (les machines pulsent à 1 Hz depuis `cpuHz` — équivalent au
demi-cycle près), seed unique au lancement. **IWM (×1)** : timer moteur MODE_DELAY
(MAME est immédiat aussi). **SWIM (×1)** : output-enable nibble + SEL35 (aucun
consommateur). **SCC (×5)** : CTS, auto-enable DCD, Zero Count, verrou FIFO condition
spéciale, pace WR11 — tous « only worth it with a real async transport » (§ 1.4) ;
le jour où un vrai périphérique série est branché, rouvrir en bloc. **5380 (×3)** :
SER/(re)sélection, MONBSY, timing d'arbitrage — posture § 1.5 assumée, pas de mode target.
**53C96 (×1)** : staging tcounter↔FIFO (2 écarts délibérés documentés, dont le chemin
7.5.5). **ADB (×2)** : les deux fallbacks HLE — non-conformité annoncée sur stderr,
retraite programmée avec l'Egret HLE (§ 2). **MCU (×2)** : DFAC2 ACK-seulement (la
parité stricte **muterait** les machines — § 4.1), Egret.cpp fallback. **ApplePic (×1)** :
échantillonnage IRQ à l'instruction (documenté in-source). **PG&E (×2)** : entrée
d'interruption 0 cycle (leçon Mac TV : 2 % de dérive = deadlock — c'est un *fix*, pas
une dette), `power_cycle_w`/clock-divide loggés (milestone sommeil les absorbera).

Point commun : chacun a soit une **raison mesurée** (deadlock, mute, oracle absent),
soit un **absent structurel** (pas de producteur). Les fermer serait du risque pur.

---

## 5. Décision proposée

1. **Continuer la politique actuelle** — le triptyque « raison écrite + condition de
   réouverture + fallback annoncé » tient : l'audit multi-agent n'a trouvé **aucune**
   simplification cachée non assumée, seulement 5 défauts d'épinglage (§ 3).
2. **Fermer F1-F4** dès la fin des vagues de correction en cours (F1 est la seule dette
   avec un coût utilisateur récurrent).
3. **Inscrire F5-F7 au TODO** datés, *derrière* la passe test-depth qui reste prioritaire.
4. **Faire la passe d'épinglage E1-E5** (une session docs+commentaires, zéro risque).
