# TODO

**Active work only.** Resolved work, investigation trails and design rationale
live in `CHANGELOG.md` (implementation detail in `DEV.md`, vendor notes in
`extern/*/POM68K_VENDOR.md`, LLE inventory in `docs/LLE_VS_HLE.md`, JIT design
in `src/jit/POM68K_JIT.md`).

**Counts verified 2026-08-07** — re-verify before quoting them anywhere:
- **162 CTest gates** (`ctest -N`): 77 `unit`, 8 `smoke`, 23 `jit`, 36 `m040`,
  81 `etalon`. Last FULL suite **162/162 on 2026-08-07**, 3 h 35
  (11:35:58 → 15:10:58), on a fully rebuilt tree (`make -j4` first,
  `BUILD_EXIT=0`, no truncated binary, per-gate freshness checked). The
  previous full run was 143/143 on 2026-08-03, so the 19 gates added since
  had never been in one until now.
  Both boot images read `drVolAtrb = $0100` (clean); the corrupted
  `MacOS-7.6-boot.vhd` was deleted 2026-08-06 — see § 1.
- **37 machine profiles** = 37 tags in `SnapMachine`, `src/SaveStateMachines.h`.

House rule for this file: an item earns its place by saying **what to do next**,
concretely. When it lands, it moves to `CHANGELOG.md` and leaves at most one
line here. **Every unchecked box below was re-verified against the code on
2026-07-31**, and § 0/§ 1/§ 4 again on **2026-08-03**.

---

## 0·A. Direction produit — la vitesse, et l'ordre dans lequel on la paie

**Décision utilisateur, 2026-08-09.** Le problème n°1 de POM68K est sa
**vitesse d'exécution**. Toutes les machines doivent être *utilisables*, quitte
à perdre la conformité sur les plus puissantes. L'échelle voulue :

| Classe | Contrat | État constaté |
|---|---|---|
| **68000 compacts** (Plus & co) | **LLE complet, conformité non négociable.** Un Raspberry Pi 400 suffit et doit suffire | tenu |
| **68020** (Mac II & co) | conformant | la fenêtre ne vaut que ×1,0-1,2 (pas d'ATC à sauter) |
| **68030** (LC II, 15,67 MHz) | utilisable sur Pi 400 | ~×1,3 temps réel en turbo sur un x86 costaud ; **inutilisable sur Pi 400** |
| **68040** (Centris, Performa, Quadra) | utilisable | **même un x86 costaud ne suit plus** |

**L'ordre est fixé et il n'est pas négociable :**

> **1. D'abord épuiser toutes les accélérations possibles en LLE et en JIT
> conformant. 2. Ensuite seulement, ajouter du HLE et du JIT non conformant.**

C'est la règle § *Principle* de `docs/LLE_VS_HLE.md` appliquée à la
performance : le raccourci ne se mesure et ne se valide que contre une
référence conforme qui existe déjà. Concrètement, **§ 3 est désormais le
chemin critique du projet**, et l'item *Optional HLE acceleration overlay*
(§ 8) est explicitement **bloqué derrière lui**.

### Ce que la bascule non conforme rapportera — estimation, à ne pas citer comme mesure

Base **mesurée** (`POM68K_JIT.md` § 3, bench à budget de cycles fixe, Q605) :
interpréteur → `threaded` ×1,60-1,69, → `x86-64` **×2,09-2,18**. Le ×2,68 du
`q605_boot_etalon` est flatté (un etalon s'arrête à la signature Finder, donc
les deux moteurs sont chronométrés sur des quantités de travail invité
différentes) — **la base de raisonnement est ×2,1, pas ×2,7**.

| Levier | Gain attendu sur le JIT actuel | Statut |
|---|---|---|
| Soft TLB / ATC relâché | +10 à 30 % | **estimé.** Assis sur un fait mesuré (794 M sorties de fenêtre / 12,2 G instr = 1 toutes les ~15) mais le **coût par sortie n'a jamais été mesuré** — c'est le seul terme inconnu de tout le calcul, et le premier à chiffrer |
| Longues traces, contrat par instruction abandonné | +10 à 20 % | estimé ; le block linking a déjà pris le gros (entrées −53 %, 268 → 566 instr/entrée) |
| Interruptions + temps grossiers | +0 à 10 % | estimé, et **la relaxation à laquelle ce tree est le plus fragile** (voir plus bas) |
| ~~Lazy flags~~ | **≈0,8 %** | **MESURÉ et abandonné** — voir § 3 *Measured and DROPPED*. Ne pas le remettre dans un plan de perf |

Composé : **×1,3 à ×2,0 sur le JIT actuel**, soit **×2,7 à ×4,2 face à
l'interpréteur**. Sacrifier toute la conformité CPU rend donc de l'ordre de
**+50 %** — ce n'est pas un changement d'échelle. Raison structurelle : le
générateur x64 *est déjà* un vrai JIT (code machine émis, DTLB inline,
transferts de contrôle compilés comme terminateurs de bloc) ; les relaxations
d'un JIT 68k classique servent surtout à *atteindre* cet état, et on y est
déjà, en payant l'exactitude. On ne rachète que la taxe.

**Le changement d'échelle, s'il existe, est dans le HLE, pas dans le JIT.**
La liste des instructions non couvertes est menée par les décalages line-$E,
`Scc`, `PEA` et les modes indexés 68020 — *ce dont sont faits les blitters de
QuickDraw* (§ 3, *Coverage tail*). Un JIT exécute ces boucles plus vite ; un
HLE QuickDraw ne les exécute pas du tout. C'est le seul levier dont le gain
n'est pas borné par le taux d'instructions — et le plus invasif.

### Deux garde-fous à poser AVANT la première ligne de code non conforme

1. **Ne jamais relâcher l'horloge périphérique/MCU.** Relâcher l'exactitude
   côté CPU, oui ; le temps vu par le VIA, l'Egret/Cuda, l'IWM/SWIM reste sur
   le compteur machine. Ce tree a trois cicatrices qui disent pourquoi : la
   Mac TV deadlocke sur **2 %** de dérive du taux d'instructions MCU
   (§ 1, *Cuda↔VIA phase robustness*) ; le boost i-cache comprimait le
   dénibblage sous le hold de 14 ticks de l'IWM → `badDCksum` (d'où le gel du
   boost pendant que le moteur tourne) ; `CudaLle::tick` doit reporter son
   dépassement en `mcuDebt_` sous peine de suroverclocker le MCU de ~37 %.
   Le **temps grossier est la relaxation la plus dangereuse ici**, pas la plus
   rentable — la prendre en dernier, machine par machine.
2. **L'instrument de mesure ne survit pas au mode non conforme.** Chaque
   chiffre de la table § 3 a été pris avec les trois moteurs imprimant la
   **même empreinte** d'état architectural. Un profil relâché imprimera une
   empreinte différente **par construction**. Il lui faut donc son critère
   d'équivalence *fonctionnel* (atteint le Finder, lit le bon bloc) **avant**
   toute mesure, sinon « plus rapide » et « cassé mais rapide » deviennent
   indiscernables — le mode d'échec que ce projet a déjà payé plusieurs fois.

### Forme visée (pas un booléen)

Le mot est « **progressivement** » : un **profil de fidélité gradué** avec des
défauts **par famille de machine** (compacts verrouillés au profil conforme ;
030 et 040 relâchés par cran), pas un interrupteur conforme/non-conforme. Le
*purity mode* de `docs/HLE_OVERLAY.md` § 7 s'applique tel quel : tous les
gates d'accuracy forcent le profil conforme, `abort()` si quelque chose tente
de l'armer.

### Ce qui manque pour transformer tout ceci en plan

- [ ] **Une ligne de base sur le matériel cible.** Aucun chiffre POM68K
  n'existe sur un vrai Pi 400 (§ 3, *Build recipe*) et « inutilisable » n'est
  pas une cible tant qu'on ne sait pas *combien* il manque. Mesurer avec
  `jit_bench` (`POM68K_BENCH_FRAMES`), **jamais** un boot etalon.
- [ ] **Le ×1,3 de la LC II : avec ou sans JIT ?** Si c'est l'interpréteur, il
  reste ~×1,5 gratuit juste en activant le moteur. Si c'est déjà `threaded`,
  la marche est plus raide. La réponse change tout le calcul ci-dessus.
- [ ] **Chiffrer le coût d'une sortie de fenêtre** — le seul terme estimé de
  la table. Tant qu'il ne l'est pas, « ATC relâché = +10 à 30 % » est une
  hypothèse, pas un argument.

---

## 0. État au 2026-08-03 — la suite complète est verte, le chantier IOP est clos

**`ctest` complet : 143/143, 3 h 03, aucun échec — sur arbre entièrement
reconstruit** (`make` d'abord, 150 objets et binaires relinkés).

**Le `make` fait partie de l'affirmation, pas du décor.** Un premier run le
même jour avait rendu 143/143 sur des binaires liés à des moments
différents — 102 sur ~110 étaient plus anciens que `libpom68k_core.a` — et
ne prouvait rien : **`ctest` ne compile pas.** Le piège est asymétrique :
un échec fantôme se fait enquêter, un succès fantôme se fait citer. Il
avait déjà été écrit dans `CLAUDE.md` avant qu'on ne vérifie.
**Un `ctest` vert ne vaut que la fraîcheur de ses binaires.**

**36 profils, tous bootant le Finder.** Les deux derniers sont les tours
Eclipse (Quadra 900 et 950), arrivées le 2026-08-02 avec le chantier IOP :

| Run | Résultat |
|---|---|
| `q900_boot_etalon` | 640×480×1, 5830 cmd SCSI |
| `q950_boot_etalon` | 640×480×**8**, 33,333 MHz, ROM `$3DC27823` |
| `q700_boot_etalon` (garde-fou) | 5838 cmd SCSI, aucune régression |

Le mur M7 n'était **pas dans l'IOP** : `read16` appliquait encore la règle de
voie d'octets du Quadra 700 à la fenêtre hôte de l'IOP SWIM, la ROM lisait le
registre d'adresse partagée `$0203` comme `$0200`, décrémentait et écrivait
sur la pile du 65C02. Un `!eclipse()`. C'est le **quatrième** bug du motif
« règle héritée du Q700 » (`docs/IOP_BRINGUP.md` § 5b) — et la moitié non
corrigée du #2. Détail : `CHANGELOG.md` § 2026-08-02 (quatrième).

**`docs/LLE_VS_HLE.md` ne porte plus aucun bug vivant.** Le seul qui restait,
le « symptôme des modificateurs du Quadra », a été rétracté par expérience le
2026-08-02 : même machine, autre image, tout arrive. Tout ce qui subsiste
en § 1 est une simplification, avec sa raison et sa condition de réouverture.

**La prochaine action, par ordre de trou architectural décroissant :**

1. **§ 2, profondeur de test** — 9 profils sur 36 ont un gate au-delà de la
   signature Finder. Décision produit prise le 2026-08-02 : on implémente le
   LLE d'abord, on construira les gates longs ensuite.

*(Copyback/snooping 040 : chantier **CLOS à M1** le 2026-08-05 —
balayage `m040` 33/33 flag ON sur binaires frais, décision M2
documentée avec ses conditions de réouverture, `docs/CACHE_040.md`
§ 3 / `CHANGELOG.md` § 2026-08-05 (third).)*

*(Échéances périphériques : huit plateformes converties les 2026-08-03/04,
27 gates sériels verts — les quatre restantes ont chacune leur raison,
`TODO.md` § 4 / `CHANGELOG.md`.)*

**Garde-fou permanent.** `Q700Memory` sert trois machines : après toute
retouche, relancer `q700` **et** `q900`, jamais en parallèle
(`pom68k-no-concurrent-ctest`).

---

## 1. Red now

*(The IIfx etalons are **RESOLVED 2026-08-06**. They had been red since
2026-08-04 because `hdv/MacOS-7.6-boot.vhd` was corrupted by the all-ID
SCSI mirror era — seven concurrent mounts of one volume; `drVolAtrb` read
`$0000`, bit 8 clear, and the failing signature `menu bar 0.50 / desktop
0.26` was a modal repair alert sitting over the desktop, which is also what
ate the injected mouse motion and the held key. The emulator-side fix
(single-ID attach) had been in for days; what remained was the image, and
the image was not repairable. Deleted; the gates fall back to
`GISTPERSO-boot.vhd` and all four pass — `f=539 menu bar 0.06, desktop
0.69`, 8/8 input checks, `jit_iifx_boot_etalon` identical. So the
GISTPERSO-wedges-at-`$4081B66E` lead from the same hunt is **also stale**:
that combination boots. Mechanism, and the two wrong diagnoses it cost:
`CHANGELOG.md` § 2026-08-06 (late night).)*
*(The 800K-GCR-on-boosted-030 refusal is **RESOLVED 2026-08-05**: the
boost compressed Apple's denibble inner path below the IWM's 14-tick
hold → duplicated nibbles → badDCksum on every field. Fix = the floppy
boost gate (`boost_` frozen to 1 while the motor runs, in
`Cpu030`/`RbvCpu`/`VaspCpu`; `POM68K_FLOPPY_BOOST_GATE=0` reproduces
the defect); `lcii_floppy_etalon` now ASSERTS the mount + guest write —
the first GCR mount this platform ever had. Mechanism + fix:
`CHANGELOG.md` § 2026-08-05 (eighth)/(ninth), microscope:
`tests/lcii_sony_trace`. Residual, parked in § 2: Cmd-N on the mounted
floppy repaints the window but lands no folder in its catalog.)*
- **System 7.5.5 refuses a hot-inserted GCR floppy on SWIM2 machines**
  — reported in the GUI, and **NOT reproduced headless**: judged on the
  desktop (the mounted volume's icon, screen-diff) rather than on
  `nibblesRead` (an IWM-only counter that reads 0 on SWIM2 and produced
  a night of false negatives — `CHANGELOG.md` § 2026-08-04 (soir),
  retraction), the plain tree mounts the disk under 7.5.5 on the Quadra.
  So the difference lives in the GUI path, not the SWIM2 model: a
  machine-thread insert against a running emulation, the live PRAM/Finder
  state, the actual image on the actual profile. Next: reproduce IN THE
  GUI with `POM68K_FLOPPY` unset, insert from the Disques window, and
  compare that Swim2 dialogue against the headless one (traced harness in
  the session scratchpad, linked object-before-archive). Only then a gate
  — "Q605 + 7.5.5 boot volume + hot GCR insert mounts" — which must fail
  on today's tree before it is worth anything.

*(nothing else — `jit_q605_boot_etalon` went red on 2026-08-04 after the
A64/pacing merge and green again the same day: the x64 emitter handed the
dynamic link target across `chargeCycles` in RDI, which the pacing
callout clobbers; a garbage tag that matched a populated slot jumped
into an unrelated block. `leaveToDynamic` now reloads the target from
`at(L_.pc)` — the exact shape the A64 backend already had, which is why
only x64 failed. Full hunt: `CHANGELOG.md` § 2026-08-04. Reproducer kept
for regressions: the coarse x64 lockstep,
`POM68K_JIT_BACKEND=x64 POM68K_JIT_HOT=1 POM68K_JIT_LOCKSTEP_BUDGET=50
./build/jit_lockstep_test 5000000`.)*

*(previously: `q605_cudalle_key_etalon` went green 2026-07-31: the cause was
Easy Access **Slow Keys enabled inside `MacOS-8.1-boot.vhd`**, not the
emulator. Full diagnosis in `CHANGELOG.md` § 2026-07-31; the gate now holds
each key 150 frames, which both a Slow-Keys guest and a normal one accept.)*

### Follow-ups from that hunt

- [x] **The 8.1 image is CLEAN — done 2026-08-02, verified.** Easy Access is
  gone from `hdv/MacOS-8.1-boot.vhd`: the non-destructive read of `$484185`
  now reports **no Easy Access engine at all** (`$FA` = ordinary RAM, not
  `$00`), **6-frame taps land**, and Cmd-N repaints the screen — the same
  behaviour the GISTPERSO image always had. Re-ran `ctest -L m040`
  afterwards as this entry required: **32/32 green**, no dirty-volume
  signature. It had cost two investigations (the ten-month red gate, then
  the phantom "Quadra modifier bug").
  Note for whoever checks this again: read `$484185` with
  `adb_key_probe` — do NOT use the hold-Return gesture, which is a toggle
  and answers the question by changing the answer. And expect three
  outcomes, not two: `$FF` on, `$00` off, **anything else = the engine is
  not loaded**, which is what a properly cleaned image looks like.
- [ ] **"Beeps sound wrong / differ per letter"** (field report): the beep
  itself was the Slow Keys rejection beep — expected, and it stops now that
  the image is clean (2026-08-02). What remains is the unrelated half:
  whether ASC renders audible output *correctly* is still untested
  (`q605_asc_test` covers registers/IRQ, not sound). Low-priority ASC item.
- [x] **Cmd-N "Quadra modifier bug" — RETRACTED 2026-08-02.** Crossed
  machine against image instead of arguing: Q605 + 8.1 does not repaint,
  **Q605 + GISTPERSO does**, LC II + GISTPERSO does — and on all three the
  guest's KeyMap holds Command and N *simultaneously*. The variable was the
  image, never the Quadra, and the modifier reaches the guest everywhere.
  `LLE_VS_HLE.md` § 5 has **no live bug left**. Root cause of the false
  lead: the probe's Cmd-N block hardcoded 3/6-frame taps — the exact length
  Slow Keys rejects — and never sampled KeyMap during the gesture. Both
  fixed. Full story: `CHANGELOG.md` § 2026-08-02 (sixth).

Methodology notes, now paid for three times: `KeyLast` ($0184) moves in **no**
cell, including working ones; **`KeyTime` ($0186) is stamped continuously by
the Slow Keys periodic task, keystrokes or not** — its "lockstep with typing"
was a coincidence of sampling cadence; and a VRAM-hash probe reported "keys
lost" on a cell where KeyMap proves they arrive. **Believe an observable only
after it has demonstrated sensitivity** — and only after it has demonstrated
*silence without stimulus*.

### Cuda↔VIA phase robustness — CLOSED 2026-08-03

- [x] `M68hc05::serviceInterrupts` now charges the hardware's **11 cycles**
  (`m6805.cpp:570`). The former zero-cycle accommodation is pinned by a
  synthetic IRQ gate (`11 + 2` cycles for entry plus the first NOP).
  `CudaLle::cyclesToNextEvent()` derives the first machine cycle on which the
  MCU can execute after its fractional-clock remainder and overshoot debt;
  `cuda_lle_test` proves both halves of that contract (silence before, progress
  exactly at the deadline). The historical seven-of-eight-CB1 Mac TV wedge no
  longer reproduces: `mactv_boot_etalon` reaches the Finder with the 11-cycle
  cost, and all three `q605_cudalle_{boot,mouse,key}_etalon` gates are green.

---

## 2. Test & validation depth — the single biggest gap

The gates prove **boot**, not **use**, and the machine fan-out made the ratio
worse. Of the **37 profiles** covered by the **170 gates**, only **9** have any
gate past the Finder signature: LC II (`lcii_soak/persist/launch/floppy_etalon`
+ `lcii_savestate_etalon`), Quadra 605 (`q605_soak/persist_etalon` —
2026-08-05, the second beyond-boot machine —, `q605_cudalle_mouse/key_etalon`,
`q605_cdrom/cdboot_etalon`, `q605_savestate_etalon`, `q605_ot_bind_etalon`),
Mac II (`macii_mouse_etalon`), **Mac Plus** (`input_etalon` — mouse
quadrature + M0110 keys), the four input gates from 2026-07-29
(`lc3_`, `lc520_`, `iivx_`, `iisi_input_etalon`) and the IIfx
(`iifx_input_etalon`, 2026-08-01). That is NINE profiles
with any gate past the Finder signature; enumerate them with
`ctest -N | grep -E 'input_etalon|beyond|mouse_etalon|key_etalon|savestate_etalon'`
rather than trusting this sentence. **The other 28 profiles are
boot-to-Finder signature only.** A machine can pass its etalon and still be
useless for real work.

**Depth is a second axis, and it moved on 2026-08-09.** Of those nine, only
**three** now have the soak+persist pair that proves a machine *keeps* working
and *writes*: the LC II, the Quadra 605 and — since `iivx_soak/persist_etalon`
— the IIvx, which had an input gate and nothing more. Counting profiles alone
hides that: the IIvx was already inside the nine before it could survive three
idle minutes or create a folder.

Highest-ROI closers, in order:

- [x] **Soak + persist on the Quadra 605 — LANDED 2026-08-05**
  (`q605_soak_etalon`, `q605_persist_etalon`; the persist run drives the
  53C96 WRITE end to end — `CHANGELOG.md` § 2026-08-05 (fifth)).
- [x] **Soak + persist on the Macintosh IIvx — LANDED 2026-08-09**, the THIRD
  machine (`iivx_soak_etalon`, `iivx_persist_etalon`). VASP rather than an RBV
  sibling because on the IIsi/IIci physical low RAM IS the framebuffer, so
  `peek8(0x20C)` reads desktop pixels instead of the Time global. Soak: 180 s
  on the Mac clock for 180 s of frames, 377 488 381 Egret MCU cycles —
  matching the LC II reference, an independent check of the MCU timebase on
  this board.
  **It also found a defect in the two older gates' shared criterion**: the
  folder-name search was case-sensitive AND picked whichever candidate was
  most frequent. On the French 7.5 volume `Nouveau dossier` is a constant
  localization resource at ×51 and always won, while the created folder is
  `Dossier sans titre` 10 → 12. The IIvx gate now judges on **the candidate
  that changes** and prints all of them; `lcii_beyond_etalon` and
  `q605_beyond_etalon` still use the old heuristic — green on their images,
  which is not the same as right.
- [ ] **Next beyond-boot machines**: a `launch`/`floppy` pair on the Q605, or
  soak on the RBV — which needs a LOGICAL-address read of the Time global
  first, since `peek8` is physical there.
- [ ] **Floppy: a guest-INITIATED write — BLOCKED on the §1 SWIM1-IWM
  mount bug, and its 2026-07-29 evidence is RETRACTED** (2026-08-05).
  The "volume mounts, window auto-opens, Cmd-N dropped" story was the
  System 7.5 INIT DIALOG end to end: the changed region x 3..494 /
  y 2..240 *was the dialog box*, the modal dialog ate Cmd-N, and the
  gesture's Return pressed its default [Eject]. KeyMap sampling proves
  the keystrokes arrive at every settle time; nothing was ever dropped.
  `lcii_floppy_etalon` now detects the dialog instead of calling it a
  mount. The write gesture becomes trivial the day the volume actually
  mounts — fix §1 first. Device-side write→eject→flush stays gated by
  `floppy_persist_test`. Full story: `CHANGELOG.md` § 2026-08-05 (sixth).
- [ ] **Widen per-machine System coverage.** Each profile's etalon pins one
  reference image (`GISTPERSO`, Infinite Mac 8.1…). `finder_boot_matrix`
  currently accepts only four machines (`tests/finder_boot_matrix.cpp:328-333`
  — `plus`/`macii`/`lcii`/`q605`); every profile added since Phase C has no
  matrix cell. This is the calcify-around-one-image trap `LLE_VS_HLE.md` warns
  about. Add cells as images are validated — Classic II, LC, Color Classic,
  LC III and the AIO family are the oldest debts.
- [ ] **Plus floppy System 4.1 cell.** `bootPlus`
  (`tests/finder_boot_matrix.cpp:136-155`) only does `attachScsi`; the 4.1 cell
  needs an `insertDisk` path. All HD cells PASS.

**Per-machine LLE-completeness estimates** (±5 pts, re-scored 2026-07-25):
Plus ~85, Q605 ~80, LC II ~80, LC 475/575 ~79, Mac II ~78, LC / LC III/III+
~75, Centris/Quadra 610/650 ~74, Classic II / LC 520 family ~73, IIx/IIcx ~73,
IIvx/vi ~72, IIsi / IIci ~70, Color Classic ~70, Mac TV ~70. Common ceilings:
whole-frame video everywhere, cycle-exact CPU only on the Plus, and the
beyond-boot gap above. Lowest scores are **freshness** (booted-once, not
hardened — RBV / Tinker Bell / VASP / AIO).
*(The "whole-frame video everywhere" ceiling lifted on six of nine decoders
2026-08-02 — see § 4bis; these scores predate it.)*

---

## 3. JIT — second execution engine

> **Chemin critique du projet depuis le 2026-08-09** (§ 0·A). L'ordre décidé
> est : épuiser d'abord tout le conformant listé ici, **puis** seulement le
> HLE / JIT non conformant (§ 8). Les deux premiers items ci-dessous sont ce
> qui reste de plus gros à gain conformant — et le premier porte sur la LC II,
> exactement la machine que l'objectif nomme.

Landed and documented: J0/J1 (engine seam, backends, fetch window, block cache),
J2 (x86-64 code generator), J3 (inline DTLB), block linking, 030 + 020 seams,
GUI engine switch on every family, PGO training per CPU family. Design and
measurements: `src/jit/POM68K_JIT.md`; per-item history in `CHANGELOG.md`
(2026-07-27 → 2026-07-31).

Current state: engine available on **every CPU wrapper in the tree** —
twelve of them, 68000 included since 2026-08-06, so there is no machine
left without a second engine; the **x86-64
backend is declared 68040-only** (`BackendCaps::guestFamilies`, `JitBackend.h §
GuestFamily`) so `auto` gives the 030s `threaded`. Best measured figure: Q605
boot etalon **61.3 s interpreter → 22.9 s JIT (×2.68)**.

Open, in ROI order:

- [ ] **Widen the x86-64 backend to the 68030 family.** The divergence is
  **localized**: with `POM68K_JIT_ACCESS_THUNK=0` — which hands every
  memory-touching instruction back to the interpreter — x64 boots the LC II to
  the Finder, so what breaks is the natively-compiled memory-access path, not
  the emitters. Work items, in order:
  - **an `lcii`/x64 lockstep gate FIRST.** `jit_lockstep_test` and friends run
    two **Quadra 605** machines, so an 030 code generator has no differential
    coverage at all — and this bug is exactly what that costs.
  - a 68030 branch in `pomJitProbeData` (it returns false below `M68EC040`
    today, so the inline DTLB never fills on an 030 at all);
  - model-correct access thunks: `pomJitReadData`/`pomJitWriteData` call
    `mmu040Read`/`mmu040Write` unconditionally;
  - the 030's `(An)+` update order (before the access, not after —
    `MoiraDataflow_cpp.h:326-332`), its restartable last write (`:355-361`), and
    the end-of-instruction prefetch refill that makes `queue.irc` mean something
    different at a block exit.
- [ ] **Coverage tail** (after MOVEM/DBcc/JMP): register-count and memory
  shifts/rotates, Scc (`JitBackendX64.cpp:1819` refuses it), PEA; **the 68020 indexed modes are
  the big block** (a brief extension-word decoder — QuickDraw's blitters;
  currently refused at `JitBackendX64.cpp:36,171,175`). MULU/DIVU stay fallback
  (data-dependent cycles — the cross-check refuses them honestly).
- [ ] **Compact `mmu040InstrStart`.** Eight per-instruction field resets + a
  `getCCR()` pack; adjacent fields could collapse into one or two wide stores.
  Small, but it sits on every single 040 instruction.
- [x] **Bring aarch64 to x86-64's opcode-family level.** Register/memory ALU,
  branches, calls/returns, MOVE, BTST, LINK/UNLK and MOVEM now use native
  AArch64 plus the same exact fallback policy; 5 M-step lockstep passes.
- [x] **Close the AArch64 full-boot gate and enable `auto`.** Hidden peripheral
  lockstep localized four emitter defects: NEG used an immediate instead of a
  register, EA commit clobbered an ALU operand, short MOVE immediates were not
  masked, and BFINS lost its mask while setting flags. Five-million-step fine
  and coarse locksteps plus the complete Q605 Finder boot now pass. Flushing
  only newly emitted code instead of the entire 128 MiB code reservation also
  removes the dominant Apple-Silicon compile cost. Replacing the slice
  invalidation `unordered_multimap` with an O(1)-append index removes a second,
  quadratic long-boot cost (and duplicate indexing). The fixed Release
  benchmark is 1.22 s A64 versus 4.55 s threaded (3.73x); the full Finder gate
  is 9.19 s versus 21.14 s (2.30x), with identical signatures.
- [ ] **Generated-code density** — deprioritized 2026-07-30. The stale
  150 B/instr figure predates boundary-deferral + cold emission; the re-baseline
  shows x64 already beating threaded on both regimes (−10 %). The residual
  idle-Finder gap is dominated by the ATC-eviction exactness contract, which
  density cannot touch. Micro-wins (local rel8, shared stubs) are third-order.

### Build recipe — Raspberry Pi (landed 2026-08-08, `docs/RASPBERRY_PI.md`)

`POM68K_NATIVE` / `POM68K_TUNE` / `POM68K_LTO` are three knobs now, not one;
the aarch64 AppImage gets LTO + `-mtune=cortex-a72`;
`packaging/raspberry/build_native_pi.sh` builds `-mcpu=<exact core>` + PGO on
the board; `tools/pgo_train_run.sh` is the shared training load and **fails
loudly** where PGO used to fail silently. Left open:

- [ ] **Measure POM68K on an actual Pi, before and after.** Every ARM number
  in `docs/RASPBERRY_PI.md` is NeoST's, borrowed from the same Moira
  interpreter on a Cortex-A72 (−20 % PGO, −34 % PGO+LTO, ~10-20 % `-mcpu`).
  POM68K's own PGO figure is x86-64 only. Use `jit_bench`
  (`POM68K_BENCH_FRAMES`), never a boot etalon: an etalon stops when it
  recognises the Finder, so two builds get timed over different amounts of
  guest work. The fingerprints it prints must match across builds.
- [x] **Validate the release AppImage change** — DONE 2026-08-08, run
  31263114761: four jobs green, `-- POM68K: tuned for cortex-a72 (ISA floor
  unchanged)` in the aarch64 log, glibc floor still 2.17. The LTO is visible
  in the size: aarch64 3 779 143 → 3 216 148 B (−14.9 %), x86_64 3 720 638 →
  3 127 936 B (−15.9 %), macOS and Windows unchanged — exactly the scope
  enabled.
- [x] **Dispatch `pi400.yml`** — DONE 2026-08-08, run 31264225875 green
  (81 objects, `-mcpu=cortex-a72` on the real compile line, glibc floor 2.17,
  both packages launched on the runner). **And it refuted its own premise**:
  the binary is byte-identical to the release build's `-mtune=cortex-a72`
  (27 bytes differ out of 8 698 128 — build-id + version string). `-mcpu=X` is
  `-march=<X's arch> -mtune=X`, and the ISA delta (crc, crypto) is code GCC
  never emits by itself. The workflow now builds both ways and reports the
  verdict per run. `CHANGELOG.md` 2026-08-08 (fourth).
- [ ] **Dispatch `pi400.yml` with `cortex-a76` once.** A Pi 5 is armv8.2-a —
  LSE atomics, fp16, dotprod — the one case where the raised floor might
  actually change codegen. The comparison step will now say so either way;
  nobody should re-derive the a72 result to find out.
- [ ] **LTO for the macOS `.dmg` and the Windows `.zip`.** The same argument
  applies (`package_macos_release.sh`, `release.yml`'s Windows job both set
  `POM68K_NATIVE=OFF` and so still get no LTO). Stopped at Linux deliberately:
  the universal-2 `lipo` path was not exercised, and MSVC needs `/GL` +
  `/LTCG`, which the CMake block does not emit.

### Measured and DROPPED — do not re-open without new data

These cost real time to measure. The numbers, not the conclusions, are the
value here.

- **Lazy condition codes in x64: ceiling ≈0.8 %.** Method: duplicate the flag
  emission (storing the same byte twice is a semantic no-op, so the delta is the
  marginal cost of one full materialisation set). Q605 boot on the x64 backend
  26.13/26.15 s → 26.37/26.30 s. Lazy CC can only remove the *dead* subset of
  that. **Measurement trap**: the first published figure (2.5 %) used
  `POM68K_CPU_ENGINE=jit` alone, which selects **threaded** — the modified x64
  code never ran. Any x64 measurement must set `POM68K_JIT_BACKEND=x64`.
- **Page-granular dispatch tables for the memory maps: premise was wrong.**
  Accesses by destination over an LC II boot (1 475 M): RAM 69.6 %, ROM 29.3 %,
  I/O 0.33 %, other 0.80 %. 99 % land on RAM/ROM, already 2-4 perfectly
  predicted compares; a 4 KB table would put a **dependent load** in front of
  the 99 % case to save branches that cost nothing. Re-open only with a profile
  showing decode as a real share; the honest lever for I/O-heavy code is
  per-device caching.
- **O(1) ATC lookup for `mmu040Translate`: flat.** callgrind put it at 38.6 % of
  the interpreter, so a 256-entry direct-mapped hint table went in front of the
  32-entry scan (bit-identical by construction). Interpreter 213.2 vs 213.2 s
  (5 G), 906.7 vs 903.9 s (20 G), x64 98.9 vs 97.6 s. The single-entry
  `last[write]` memo already catches the hot case. **The 38.6 % is real but it
  is the WALK and per-access bookkeeping, not the lookup** — re-open only with a
  profile that separates them.
- **The interpreter's data window (J1c) is opt-in, not open work.** It exists
  (`JitEngine.cpp:50`, `POM68K_DATA_WINDOW=1`, `POM68K_JIT.md §8`); capping it
  at ATC coverage for bit-exactness made it a net loss on the interpreter. The
  x64 keeps its inline TLB.

Invariant worth restating: **derived state dies with the ATC entry it came
from** (`pomJitAtcEvict`). A window hit must imply the interpreter would have
ATC-hit too, or the engines walk different subsets of the page tables — and
walks write the descriptor U bit, which Mac OS VM reads for page aging. That
class of divergence mimics memory corruption; see CHANGELOG 2026-07-28.

---

## 4. LLE fidelity — replace HLE shortcuts

Inventory and migration plan: `docs/LLE_VS_HLE.md`. Policy settled 2026-07-29
(§2): HLE fallbacks are **kept but LOUD** (stderr NON-CONFORMANT notice at every
HLE ADB entry) because MCU dumps are non-distributable; deletion would be a
deliberate "POM68K requires MCU dumps" product decision, not a cleanup.

- [x] **`AdbLine` device model gaps** — CLOSED 2026-08-02. Second mouse button
  (Extended Mouse Protocol, handler 4, + the R1 identifier block),
  extended-keyboard handler IDs (1/2/3 as a real register, with a standard
  keyboard *refusing* 3), and Listen R2 LEDs. Also: SendReset now restores the
  default handler, not just the default address. `docs/LLE_VS_HLE.md` § 1.6;
  gate `adbline_test`. *(Talk R2 modifiers had landed 2026-07-31.)*
  - [x] **Host side — CLOSED 2026-08-03**: `ScreenInput` sends both mouse
    buttons with an index through every `Cmd::MouseButton` route; conformant
    `AdbLine` consumers receive button 1 while HLE/one-button paths ignore it.
    The ten ADB GUI maps also send distinct right Shift/Option/Control codes
    (and both Command keys); handler 3 preserves them, handlers 1/2 fold them.
- [x] **Peripheral event deadlines — LANDED 2026-08-03.**
  `POM68K_PERIPH_STATS=1` counts the path. Over 1200 frames of
  `q605_boot_etalon`: batch 256 → 25.6 M `mem.tick()` calls / 60.1 s;
  batch 1 → **833.2 M calls** / 103.3 s. **Same 1.650 G machine cycles
  delivered either way**, and `catchUp()` is called 879 M times in both —
  so the cost is neither the devices nor the hook, it is entering the
  ~15-device fan-out **32.5× more often**.
  Fixed batching is now replaced by the minimum conservative deadline over
  Cuda, VIA, SCC, ASC, SWIM, 53C96, CA1 and DAFB on Q605, and by the binding
  firmware-MCU deadline on V8. `catchUp()` returns until that absolute clock;
  device-space accesses still force `flushTicks()`. A too-small bound remains
  merely slow; no bound is allowed to be larger than the next observable
  transition. The explicit HLE fallback retains its historical batch.

  **The bounds, derived from the code 2026-08-03 — the hard part is done.**
  Every device in `Q605Memory::tick` can state a bound; **none has to fall
  back to 1**, which is what makes the refactor worth doing at all:

  | device | bound | value @ 25 MHz |
  |---|---|---|
  | `CudaLle` 6805 | `ceil(((mcuDebt_+1)*cpuHz - mcuAcc_) / kMcuHz)` | **~12 — binding, gated** |
  | VIA E clock | `ceil((cpuHz - viaEClock_.acc) / 783360)` | ~32 |
  | `AscIosb` drain | `ceil((kCpuHz - drainAcc_) / drainHz())`, scaled C15M→CPU | ~1123 |
  | `Scc8530` | min of the live countdowns (`peerHold_`, `txShiftIn`, …) | **∞ when idle** |
  | `Swim*` / `SonyDrive` | cell/spin clock — only while the motor runs | ∞ when parked |
  | `Ncr53c96` | service-latency countdown — only during a transfer | ∞ when idle |
  | 60.15 Hz CA1 | `ceil((kCpuHz*100 - tickAcc_) / 6015)` | ~416 000 |
  | `Dafb` VBL | from `framePos_` | ~416 000 |

  Measured on `q605_boot_etalon` with `POM68K_PERIPH_STATS=1`: **86.65 M**
  `mem.tick()` calls for 1.675 G machine cycles, or **19.34 cycles/call**.
  That is 9.6× fewer full fan-outs than the old exact batch-1 measurement
  (833.2 M), while preserving exact event timing. Gates: the Q605 boot plus
  all three firmware boot/input etalons, Mac TV, unit and save-state suites.
- [x] **Extend the event deadlines to the six 030/040 platforms — LANDED
  2026-08-04** (same day as the inventory below; 27 serial gates green,
  incl. both jit cousins on `setPeriphDeadline` and the save-state
  suites with the new serialized field). The contract carries
  over unchanged: only a device that can raise an interrupt or flip an
  externally visible line *spontaneously* needs a bound; pure state
  (SonyDrive spin, rotation angle) is covered by the access-forced
  `flushTicks()`. Mechanical wrapper change per CPU (copy
  `Cpu030::catchUp`/`schedulePeriphDeadline`): add `periphDeadline_`,
  early-return in catchUp, schedule after tick — **and add the new member
  to the wrapper's `visit()`** (savestate_030/040 gates will catch an
  omission). Per-platform bound terms, from each `tick()`:
  - **Sonora** (5 profiles, 25/33 MHz): Egret/Cuda LLE (HLE → historical
    batch), VIA Bresenham `ceil((cpuHz_−viaAcc_)/kViaHz)`, CA1
    `ceil((cpuHz_·20−tickAcc_)/1203)`, modeline VBL
    (`vblStart_`/`frameCycles_` − `framePos_`), AscSonora (check it has a
    deadline API — AscIosb does, the Sonora flavour may not), SWIM2
    through `swimAcc_` C15M bridge, SCC.
  - **VASP** (31.3344/16 MHz): same shape; fixed 60 Hz VBL (480/525);
    ASC+SWIM1+spin share the `c15Acc_` C15M domain — SWIM1 has no
    `cyclesToNextEvent` yet (add one, ∞ when parked).
  - **RBV**: IIsi = Egret LLE like V8; **IIci is the delicate one** —
    `adbVia_.tick` + `syncTo(machineClock())` every tick (host-paced PIC
    transport) plus the discrete RTC 1 Hz (`secAcc_`); derive the AdbVia
    bound from its live countdowns before touching anything
    (`pom68k-mcu-lle-clock-drift` applies).
  - **Centris** (20/25/33 MHz): Q605 mix (AscIosb, SWIM2, 53C96, DAFB)
    plus the Mac II front end — AdbVia/PIC1654S is the binding transport.
  - **Q700** (Spike/Q900/Q950): Q605 mix + Eclipse tail — two `ApplePic`
    (65C02 always running: bound = the C15M bridge, small but > 1) +
    `Egret`. Guard rail: q700 **and** q900, never in parallel.
  - **Q630**: Q605 mix + Valkyrie VBL + the Cuda I2C slaves.
  Deliberately NOT in this batch: the compacts (cycle-exact by
  construction — a deadline there risks the Plus's whole accuracy claim
  for the cheapest machine to emulate), Mac II family, IIfx (two IOPs
  always executing → the fan-out is intrinsic) and MSC — each needs its
  own analysis. Gates per platform: its boot etalons + the input etalons
  it owns (`iisi_input`, `lc3_`/`lc520_input`) + the save-state suites,
  serially.
- [ ] **Quadra 605 / LC 475**: expand Cuda commands only from ROM/driver traces;
  accurate 040 timing, cache copyback/snooping and on-chip-FPU/FPSP behaviour as
  separate oracle-gated milestones.
- [ ] **SCC LLE, Low tier** (2026-07-22 MAME `z80scc.cpp` audit,
  `docs/LLE_VS_HLE.md` §1.4). **The two items that did not need a new
  transport landed 2026-08-02**: ~~WR5 RTS output tracking (auto-RTS deassert
  on all-sent, `tra_complete:1090`)~~ — `/RTS` **and** `/DTR` are now pins,
  with the Auto-Enables deferral released from `tick()` as the shifter drains;
  ~~SDLC Rx residue codes (RR1 bits 3-1)~~ — byte-aligned code 011, which the
  idle RR1 already reported while frame bytes said 000. Both gated in
  `scc_engine_test`. What is left is deliberately deferred, each for a stated
  reason: true bit-serial Tx/Rx sampling (only worth it with a real async
  transport to talk to); chip-variant gating (NMOS 8530 / 85C30 / ESCC — FIFO
  depth 3/8, WR7', status FIFO `:1363`, needed the day a machine wants the
  ESCC); WR9 VIS/NV options (hardcoded VIS=1, correct for every Mac target);
  DPLL (MAME stubs it too, `:305-318`). **Also missing: a consumer** — nothing
  on the emulated side of the wire reads `rtsAsserted()` yet.
  **Caveat that applies to the whole SCC backlog**: MAME's SDLC side is partial
  (Send Abort / CRC resets `:1602/:1635` "not implemented", no EOM latch, no
  hunt/sync) — for LLAP behaviours **we are the more complete model**. Use MAME
  as oracle for the ASYNC side only; do not regress LLAP chasing parity.
- [x] **Valkyrie I2C pixel clock — CLOSED 2026-08-02.** The Cuda's I2C bus
  now carries the full `i2c_hle` frame and **two** slaves (DFAC2 `$6F`,
  ACK-only; Valkyrie `$28`, payload load-bearing). Measured, not assumed: a
  traced Q630 boot programs M/N/P = `$0E`/`$1B`/`$02` → 30.752 MHz, so
  640×480 refreshes at 67.80 Hz instead of the frozen 69.08 Hz. Gate
  `valkyrie_i2c_test` (asserts frame cadence, not the setter).
  **Left open on purpose**: the result is 1.7 % above Apple's nominal
  66.67 Hz; a `31.3344/8` reference would be exact, which suggests MAME's
  `3986400` is off. Needs a real observable before anyone touches it.
- [x] **`Swim1` DAT1BYTE — CLOSED 2026-08-02.** Level line, `swim1.cpp:1226`
  semantics, wired per machine: Q900/950 → both IOP DMA channels, IIfx →
  channel A only, LC II → unwired (unchanged). Every etalon owning a `Swim1`
  re-run green.
- [~] **Floppy flux + PLL layer (§ 1.3) — step 1 of 4 landed 2026-08-02.**
  `src/FluxPll.h` is the integer port of MAME's `fdc_pll_t` (phase
  feedback, `freq_hist` period trim, ±25 % clamps, `limit` protocol, write
  side); time unit = **flux ticks**, `kSubCell = 1024` per nominal cell,
  int64 so snapshots are bit-identical. Gate `flux_pll_test` (17 checks)
  shows ±12 % jitter and ±8 % rate error both recovered, where a
  fixed-window separator slips inside 32 cells.
  **Nothing reads it yet** — `SonyDrive` still stores discrete cells.
  Next, in order:
  1. a flux representation in `SonyDrive` beside `cells_`, plus
     `nextTransition(after)` (MAME's `get_next_transition`) and an opt-in
     jitter model;
  2. move **`Swim2`** onto the PLL first — it is the best-gated controller
     (`swim2_test`, `swim2_media_test`, `q605` floppy etalons);
  3. then `Swim1` (its LS-pair correction machinery, `swim1.cpp:965-1140`,
     stops being dead code at that point) and the `Iwm` READ path.
  Test-first note that already paid: the obvious "prove a dumb decoder
  fails" case (jitter) does NOT bite — ±12 % never pushes a transition out
  of its own fixed window. The off-rate track is the case that does.
- [x] **Classic II Eagle `$F18000` — NAMED 2026-08-02.** It is the analogue
  **CRT brightness/contrast DAC**, the same cell MAME maps as
  `spice_device::bright_contrast_w` on the Color Classic. Three independent
  pieces of evidence, no guessing:
  1. `rominfo --universal` on the `$3193670E` ROM — the Classic II record
     (`$003D14`, Gestalt 23, `UnivROMFlags $1A6`) has its own DecoderInfo
     `$04C7A6` whose **`decoder[6] = $50F18000`** (→ LMG `$0B0A`/`$0312`).
     That entry is **zero on every LC / LC II / IIci / IIfx / IIsi record**
     in the same table: the Classic II is the only machine here that
     declares a device at this base.
  2. The ROM routine at `$A51350` disassembles as a DAC feed — a 0-255
     setting scaled `×$2B >> 8` to 0-42, de-scrambled through a 43-entry
     table, then shifted out **370 times at stride 2** from `$50F18000`.
     The other two sites are its presence test (write / read back / compare,
     `$A48D12`) and a 768-byte ramp init (`$A03CE2`).
  3. Same address, same purpose, in MAME's Spice.

  POM68K now names it instead of dropping it in the map hole. **Behaviour is
  unchanged** — the Eagle's forgiving-bus tail already discarded the writes —
  but a named device that is deliberately ignored (our CRT has no analogue
  stage) is not the same thing as an unexplained hole. `classic2_boot_etalon`
  and `cclassic_boot_etalon` re-run green.

  Deliberately NOT done: the presence test fails here because reads return
  `$FF` and never echo the written value. That is probably right — the DAC is
  likely write-only — but "probably" is not a measurement, so no read-back
  value was invented to make the ROM's probe succeed.

  Also settled while there: the **hole value does not matter to this guest**.
  `POM68K_V8_HOLEVAL=00` (MAME's default-unmapped 0) and `$FF` both reach the
  Finder with 9619 SCSI commands, identical. And MAME's 0 here is its
  `address_space` DEFAULT, not a modelled decision — unlike the Sonora's 0,
  which `iosb.cpp:54-65` states in a comment. Do NOT align to 0 "for parity":
  there is no oracle statement to be parallel to.
- [x] **ADB report rate — MEASURED 2026-08-02, and it is right.** The open
  question was whether the ~1.6× amplification of a sustained stream came from
  POM68K servicing *fewer* polls than real hardware. It does not. Trace of a
  full LC III run (`POM68K_ADB_LLE_TRACE=1 ./build/family_input_etalon lc3`,
  199 s emulated, 17 850 ADB commands): the aggregate autopoll interval is
  **11.18 ms — p10 = p90 = median**, i.e. dead steady, against the Egret's
  nominal 11.1 ms / 90 Hz. That is **89.5 Hz vs 90**, a 0.6 % deficit, and it
  is exact by construction because the cadence is the firmware's own timer.
  The mouse is polled at ~67 Hz *while it has data* (SRQ-driven bursts, the
  keyboard holding the poll otherwise) — more than one report per 60 Hz frame.
  → The amplification is therefore System 7's mouse scaling acting on
  coalesced deltas, exactly as this entry already suspected; the poll rate is
  not a suspect any more. Anyone re-opening it needs a *new* observable.

---

## 4ter. Closed 2026-08-02 — two rounded rates

- [x] **VIA E clock** (§ 1.2). Was an integer divisor on the four 040 boards
  (25 MHz ÷ 32 = 781.25 kHz, 33 MHz ÷ 42 = 785.7 kHz); now exact rational
  arithmetic for BOTH the rate and `viaSync()`'s phase grid, in one header
  (`src/ViaEClock.h`) because the two must not drift apart — mis-scaling that
  grid is the 2026-07-25 IIsi/LC III bug. All five 040 boot etalons green.
- [x] **ASC drain rate** (§ 1.7). Follows `$807` CLOCK RATE now (0 = 22 257,
  2 = 22 050, 3 = 44 100; 1 undefined → Mac rate). Free on every booting
  machine because only the classic Mac II ASC accepts the write — asserted,
  not assumed. Gate `asc_test`, measured on cycles-to-drain, verified to bite.

---

## 4bis. Video — the raster beam (LLE_VS_HLE § 1.1, the #1-ranked gap)

**Landed 2026-08-02**: `src/VideoBeam.h` + row-granular decode on **all nine**
decoders (`V8Video`, `SonoraVideo`, `VaspVideo`, `RbvVideo`, `TobyVideo`,
`Se30Video`, `Dafb`, `Valkyrie`, `MacVideo`), wired into every GUI loop on the
slicing `runQuantumWithWire` already does when the wire is active — and, on
the Plus, on a 16× slicing of `MacFrameClock::runFrame` that cannot move the
vblank edge. Gates `video_beam_test`, `v8_raster_test`, `raster_equiv_test`.
Design note that matters to anyone touching it: **the beam owns no clock** —
it adopts the platform's existing `framePos_`, so the VBL edges are untouched.
Full story in `CHANGELOG.md` § 2026-08-02 (later).

- [x] **Expose the beam to the guest — resolved 2026-08-02, and smaller than
  it looked.** Valkyrie's `$14` blanking bit was the only real position
  register in the tree and now reads the LIVE line (it answered from
  `prevLine_`, which only advances inside `tick()`, i.e. quantised to the
  peripheral batch). **DAFB has no position register at all** — not in
  `Dafb.cpp` ($000 regs / $100 Swatch / $200 RAMDAC / $300 clockgen) and not
  in MAME's either. And the Plus's VIA PB6 already read the same
  `cpu.getClock() % 130240` the decoder now uses. One scan position per
  machine, everywhere.
- [x] **A raster gate beyond the V8** — `raster_equiv_test` (2026-08-02)
  covers V8/VASP/RBV/Sonora at every depth. Read its header before extending
  it: the equivalence invariant **cannot** catch a pitch that is wrong but
  consistent (both sides share the arithmetic), and its harness had to be
  taught to refuse a uniform frame as a pass after three false greens.
- [x] **The two unverified JIT boot etalons — CLOSED 2026-08-02.**
  `jit_q630_boot_etalon` (31.9 s) and `jit_q700_boot_etalon` (153.9 s) both
  PASSED in the post-image-cleanup `ctest -L m040` run (32/32). They had
  been *killed* mid-run after the raster work, never failed — a coverage
  gap, now a result.

---

## 5. Per-machine backlogs

### LC II / V8

- [ ] **1.44 MB guest-level mount/boot etalon.** The SWIM1 controller is done
  (IWM+ISM personalities, 1-0-1-1 switch, param RAM, MFM read/write through the
  cell engines — CHANGELOG 2026-07-23, gate `swim1_test`); what is missing is a
  guest-level gate. Asset on hand: `disks35/Stuffit_Expander_5.5.dsk`.
- [ ] Finish DFAC / sound-out behaviour and host-clock resampling; verify
  long-running audio tempo under GUI load.
- [ ] Bus and timing gaps: compare interrupt, VBL, VIA and memory timings with
  real hardware; diagnose the idle screen dim seen after very long runs.
- [ ] **Confirm the GISTPERSO / SimCity 2000 startup race is closed.** CHANGELOG
  2026-07-18 records only the root-cause analysis (CPU spins in the ROM Memory
  Manager heap-walk at `$40A0E148` = guest heap corruption during Finder
  startup; PRAM and HFS structure exonerated) and the Shift/Option workaround.
  Re-run the deterministic headless repro (`LCII_HOLD_KEYS` in `lcii_trace`) and
  either land a fix entry in CHANGELOG or reopen the differential hunt. Backup
  image: `hdv/GISTPERSO-boot.vhd.avant-reparation`.
- [ ] **No-FPU SANE.** Solved on the 040 side (CHANGELOG 2026-07-21 "Bare no-FPU
  solved: _FP68K binds the integer PACK 4"; gate `q605_barefpu_boot_etalon`
  reaches the Finder under a true `FPUModel::NONE`). **Unverified whether the
  030/LC II path still needs the same UniversalInfo / defaultRSRCs selection**
  (`POM68K_NOFPU` at `main.cpp:2070,2083`, `Cpu030.cpp:38`) — re-test before
  spending effort here; the original O6.13 diagnosis may already be obsolete.

### 68030 / MMU / FPU oracle gaps

- [ ] RTE format `$A/$B` instruction restart.
- [ ] PMOVE-through-translation fault frames.
- [ ] Instruction-stream fetches across page boundaries.
- [ ] FMOVEM indirect-EA read order.

### Mac Plus

- [ ] **VIA/RTC accuracy**: model 6522 T1/T2 ±1-cycle reload/IFR latency; VIA
  E-clock access alignment and IACK E-cycles; persist PRAM and seed the GUI RTC
  from the host while keeping tests deterministic. *(PRAM file persistence
  exists on the later machines — `main.cpp:2151,2406` for the LC II, one such
  pair per machine loop — but `MacMemory` has no `loadPram`/`savePram`, so the
  Plus/compact family is the gap; `MacIIMemory`, `IIfxMemory` and `MscMemory`
  lack it too.)*
- [ ] **Floppy**: external-drive selection (`Iwm.h:23,63` takes a second
  `SonyDrive*`; `MacMemory.h:147` still passes only the internal one). *(Write
  support, GCR write-back and host file persistence are DONE — CHANGELOG
  2026-07-23 / 2026-07-24, `SonyDrive::setWriteBack`/`flushToFile`, gates
  `iwm_write_test`, `floppy_persist_test`; the eject/insert GUI exists,
  `main.cpp:3936,4080`.)*
- [ ] Keypad/arrow `$79`-prefix handling where required by M0110 input.
- [ ] **Sound accuracy**: fetch the sound buffer per scanline instead of once
  per frame; model the disk-PWM byte and the analog volume curve.
- [ ] **SCSI/serial**: multiple targets/LUNs and correct REQUEST SENSE after
  CHECK CONDITION; a host-side serial transport (PTY/TCP) to make the SCC ports
  *usable*, not just correctly timed. *(WR4 clock mode, WR12/13 BRG, WR11
  routing, WR5 Tx gating, parity/framing, Rx CRC are all DONE — CHANGELOG
  2026-07-23, gates `scc_baud_test`, `scc_engine_test`.)*
- [ ] Pixel-accurate etalons and a WASM build: a screenshot regression runner for
  the Plus boot/Finder paths, keeping asset-dependent tests soft-skippable.

### CD-ROM

Base support DONE 2026-07-29 (`ScsiDisk::openCdrom` — INQUIRY type $05 +
removable, 2048-byte blocks, READ TOC, START/STOP eject, read-only, the Apple
magic MODE SENSE page $30; `attachCdrom(path, id=3)` on all eleven
multi-target machines; CLI routes `.iso`/`.cdr`/`.toast`). Gates `scsi_cdrom_test`,
`q605_cdrom_etalon` (8.1 boots from HD, an 8.6 CD mounts as data),
`q605_cdboot_etalon` (no HD → the ROM's 6→0 scan boots the disc, 3913 blocks).
**Layout matters**: the ROM scans 6→0, so the boot volume goes to ID 6 and the
CD to 3, or a bootable disc wins the scan. **8.5/8.6 are PowerPC-only** (8.1 is
the last 68k release) — a black screen on them is correct behaviour.

- [ ] **Install from CD**: the disc boots and the Installer is reachable, but
  driving it to completion (mouse through the Installer UI, then a reboot onto
  the freshly written volume) is not automated. The natural next "real work"
  gate.
- [ ] **Only 2048-byte-DDM discs mount.** `MacOS_86.iso` (`sbBlkSize = 2048`)
  mounts; the hybrid `TIM_3.iso` (`sbBlkSize = 512`) and bare-HFS `.toast`
  images are read (4 blocks of probes) then ignored. Observed, cause not
  established — **it may well be correct** (a real Apple CD driver may require
  its own `Apple_Driver43_CD` partition, which only the 8.6 disc has). Verify
  against MAME or a real drive before "fixing" anything.
- [ ] **CD audio**: READ TOC already reports the data track; CDDA playback,
  PLAY AUDIO/PAUSE and the audio-through-ASC path are absent — no consumer yet.
- [ ] 2352-byte raw rips + `.cue`/`.bin` multi-track (refused today rather than
  mis-read).

---

## 6. Networking

### AppleTalk in-process stack — DONE 2026-07-24, polish backlog

`AtalkStack` (DDP/RTMP/ZIP/NBP/AEP/ATP), `AfpServer` (ASP + AFP 2.1 +
`.AppleDouble`), `PapServer` (PAP → CUPS/`.ps`), `MacIpGateway` (DDP-22 +
user-mode NAT), tied by `AtalkHub` with a **Réseau → AppleTalk** window; GUI
default. Gates `atalk_stack_test`, `afp_server_test`, `pap_server_test`,
`macip_gw_test`. Reference: `docs/APPLETALK.md` §6.5.

- [ ] Persist CNIDs across runs (per-session today; a `.AppleDB` or an id in the
  `.AppleDouble` sidecar would survive a reboot, matching netatalk's catalog).
- [ ] AFP corners the subset skips: a proper Desktop database (icons / comments /
  APPL mapping, not the current "no item" stubs), CopyFile, CatSearch,
  DID-relative variable paths, AFP ≥ 3.0 / UTF-8 names.
- [ ] More UAMs than guest/cleartext (DHX / random-number) if a guest refuses
  cleartext.
- [ ] PAP status polling + per-queue `papd.conf`-style config; expose the CUPS
  queue choice in the GUI.
- [ ] MacIP: outbound ICMP (needs raw sockets or a ping helper), IP fragment
  reassembly guest→host, TCP window scaling for big transfers. In-order-only,
  MSS 536 today.
- [ ] GUI: editable fields (share dir / server + printer name / gateway subnet +
  DNS) instead of env-only; a "reveal spool folder" button.
- [ ] A live-boot etalon that mounts the internal AppleShare from a real guest
  Chooser. `q605_ot_bind_etalon` already proves the OT `.MPP` bind under 8.1;
  what is missing is the scripted Chooser drive (same need as the external
  bridge below, and a Sys 7 image).

### LocalTalk between two POM68K instances

Milestone 1 DONE 2026-07-22: bidirectional SCC LLAP wire (`Scc8530` SDLC Tx
capture, paced 3-deep Rx FIFO, Hunt carrier sense, WR1 modes, WR3/WR6 address
search + `$FF` broadcast, EOF + FCS in RR1) and the `LtoUdp` cable (Mini vMac /
TashRouter format, 239.192.76.84:1954). Gates `llap_loop_test`,
`ltoudp_test`, `llap_two_system_etalon`. TashRouter interop verified. Note:
System 6 only opens `.MPP` lazily from the Chooser — headless LLAP tests need
Sys 7.

- [~] **Full AppleShare session over the real bridge**: infrastructure DONE
  (netatalk **2.4.9** + TashRouter vendored, `tools/netatalk2/build_netatalk2.sh`
  builds hermetically, `appleshare_bridge.sh` + `router.py` serve `input/` as
  "Input" in zone "POM68K"). **Remaining: run the bridge with a GUI guest and
  mount the volume from the Chooser.**
- [ ] Interop check against Mini vMac's LToUDP (same multicast group).

### Ethernet over SCSI — DaynaPort SCSI/Link, opt-in since 2026-08-07

`DaynaPort` (the SCSI target) + `EtherLink` (framing + proxy ARP) onto the
NAT already in `MacIpGateway`. `POM68K_DAYNAPORT=<id>` on the Quadra 605;
gate `daynaport_test`; design in `DEV.md` § 3.3bis, rationale in
`CHANGELOG.md` 2026-08-07 (later). Every machine here has a SCSI bus, so
this is the one Ethernet path that can reach all of them.

- [ ] **Run a real SCSI/Link driver against it.** This is the only test that
  settles whether the command set is right; everything gated so far is our
  own reading of SLINKCMD.TXT. Needs the DaynaPort driver on a boot image
  and a scripted MacTCP config (address in the gateway's subnet, gateway as
  router) — the same "scripted control panel" need as the Chooser drive
  above.
- [ ] GUI: a **Réseau** entry to attach/detach the card and pick its SCSI ID,
  instead of an env knob at startup only.
- [ ] Save states: the card is not in any machine's chunk list, so a restore
  comes back with an empty Rx ring. Cheap to add, but it changes the on-disk
  format for every existing `.pomss` — do it with the next format bump.
- [ ] **EtherTalk** (AARP + 802.3/SNAP DDP) so AppleTalk can leave the SCC
  too. Today the card carries IPv4 and ARP only, and AFP/PAP still go over
  LocalTalk at 230.4 kbit/s through the most timing-fragile device in the
  tree. This is the item with the most to gain.
- [ ] Decouple the uplink from `AtalkHub`: with `POM68K_APPLETALK=0` the
  guest sees the card and it carries nothing, because the NAT lives in the
  hub.
- [ ] Carry the card to the other eleven platforms (a member + an accessor
  each — `AtalkHub::attach` already detects it with a `requires` clause).

---

## 7. New machine profiles

Phase A/B/C are done — 36 profiles, all booting the Finder (per-machine detail
in `CLAUDE.md` § Status and `CHANGELOG.md` 2026-07-21 → 2026-08-02). Effort
tiers and the full family map: `docs/68K_FAMILY_SCOPE.md`. Rule kept: **each new
profile gets at least one Finder cell before the next.**

Explicitly **out of scope** for now: PowerBook PMU, AV DSP, all 4 MB PPC ROMs.

### Independent majors — the only things left that are not just a ROM dump

- [x] **Apple PIC IOP + OSS** → unlocked **IIfx** *and* **Quadra 900/950**.
  **Opened 2026-08-01** — blueprint + milestone plan: `docs/IOP_BRINGUP.md`;
  recon findings in `CHANGELOG.md` § 2026-08-01 (headline: the IOP firmware
  is *downloaded by the host ROM*, no dump needed; the Q900/950 keep the
  Egret — their IOPs carry only SCC/SWIM). **M1 DONE**: `src/R65c02.*`
  vendored from POM2's `M6502` (CMOS + full Rockwell set — NOT the
  POMIIGS `CPU65816`, whose emulation mode lacks RMB/SMB/BBR/BBS), gate
  `r65c02_test` green on Klaus's two images. **M2 DONE**: `src/ApplePic.*`
  (host window / shared RAM / timer / 2-ch DMA / ints / GPIO / bypass),
  gate `applepic_test` — a 65C02 program uploaded through the window
  proves both mailbox directions, the timer cadences and DMA both ways.
  **M3 DONE (2026-08-01 evening)**: platform #12
  (`IIfxMemory.*`/`IIfxCpu.*`), gate `iifx_post_etalon` — both IOP
  firmwares upload byte-perfect, OSS programmed, boot scan reads the
  disk; WAI/STP CLOSED (zero in either firmware). **M5 DONE (2026-08-01
  night)**: gate `iifx_boot_etalon` — **the IIfx boots System 7.6 to the
  Finder**, ADB served end to end by the SWIM IOP's real firmware
  bit-banging `AdbLine` (screenshot-verified thresholds; video =
  `TobyVideo` on slot 9, the IIfx has no built-in video). Remaining, in
  **M6 DONE (2026-08-01 night)**: **the IIfx is the 34th profile** —
  `kProfiles` row (group "OSS + IOP"), `MachineKind::IIfx`,
  `SnapMachine::IIfx = 34`, `IIfxMachine` GUI loop with the two IOP
  cycle counters in the CPU window, save/load pair + coverage in
  `savestate_030_test`, ROM dispatch on the 512 KB `$4147DD77`, and
  gate `iifx_input_etalon` (mouse repaint 43 px vs 0 idle; KeyMap
  0→1→0). **M7 DONE (2026-08-02): both Eclipse towers boot the Finder and
  are the 35th/36th profiles.** `Q700Memory::Model {Spike,Q900,Q950}`
  carries the front end (two `ApplePic`, `AdbLine`, `Egret` on VIA1, the
  2nd 53C96, VIA1 PA identities, the $1000-wide IOP windows); gates
  `q900_boot_etalon` (640×480×1, 5830 SCSI cmds) and `q950_boot_etalon`
  (640×480×**8** at 33.333 MHz, its own `$3DC27823` ROM), Q700 guard rail
  re-run green. The BRK that walled M7 for a day was **not in the IOP**:
  `read16` still applied the Spike's odd-byte-lane SWIM rule to the SWIM
  IOP's host window, so the ROM read the shared-RAM address register as
  `hi<<8` and wrote a byte onto the 65C02's stack — one `!eclipse()`
  guard. Story, and the bring-up watchpoints that found it, in
  `CHANGELOG.md` § 2026-08-02 (fourth) and `docs/IOP_BRINGUP.md` § M7/§ 5b.
  **M4 (deferred, LOUD)** SCSIDMA true DMA + restartable handshake —
  A/UX-only per `scsidma.cpp:12`, nothing in the Mac OS path needs it;
  also dedupe the multi-ID mirror (7.6 mounts all seven copies of the
  boot volume — cosmetic, guest-visible).
- [ ] **Power Manager** → Portable / PowerBook 1xx / Duo. **Platform #11
  (MSC + PG&E): the Duo 230 BOOTS THE FINDER, gated 2026-07-31** —
  milestones 1-3 of `docs/DUO_BRINGUP.md` are done (`MscMemory`/`MscCpu`,
  `M68hc05Pge`/`PgePmu` LLE incl. the mid-boot BORG v2 upload and the
  /PMU_INT level, `MscMemory::decodeScreen`; gate `duo230_boot_etalon`,
  System 7.5.5, SCSI 3448 cmds). **The GUI machine loop, the `kProfiles`
  row and save states landed 2026-08-06 — the Duo is the 37th profile
  and platform #12's first GUI citizen** (`runDuo`, `MachineKind::Duo`,
  `SnapMachine::Duo230`, battery file through the PG&E; milestone 3b of
  `docs/DUO_BRINGUP.md` lists what is deliberately NOT wired — floppy,
  drive sounds, live CD-bay swap, right mouse button, all machine-side
  API absences rather than shell gaps). Remaining, in milestone order:
  **input through the PMU** (trackball + matrix keyboard →
  `duo230_input_etalon`), variants (210/250 trivial, 270c CSC, 280 040,
  then PB150 as the no-oracle MSC variant), and **the actual point — a
  sleep/wake gate** (`duo230_sleep_etalon`), which no other machine can
  test.
  The 140-180 line is a different PMU (Mitsubishi M50753, 6502-class —
  POMIIGS `CPU65816` candidate) — same brick as Portable/PB100.
- [ ] **AV DSP (DSP3210)** → 660AV/840AV. Not planned.
- [ ] **NuBus + slot video** beyond Mac II Toby: IIx / IIcx / IIci and the NuBus
  Quadras. VASP/IIvx currently reads its three slots as empty; real cards would
  reuse the Mac II NuBus/DeclRom port.
- [ ] **ATA/IDE target** on the Quadra 630 / LC 580. The port is mapped but has
  no drive, so boot goes through SCSI — the remaining gap on that board.

### Remaining machines with the ROM already in `roms/`

*(The Mac IIfx shipped 2026-08-01 as the 34th profile and left this table;
the PowerBook Duo 230 followed on 2026-08-06 as the 37th — the first
laptop — and left it too.)*

| Machine | ROM on hand | New brick |
|---|---|---|
| **Quadra 900 / 950** | `420DBFF3` / `3DC27823` | none left — the IOP brick landed with the IIfx and the Eclipse front end is in `Q700Memory`; what remains is the SWIM IOP's BRK (§ 0) |
| **PowerBook 150** | `FDA22562` | LCD framebuffer + 68HC05 PM — the `M68hc05` core already ships |
| **PowerBook 140-180** | `E33B2724` | LCD framebuffer + the M50753 Power Manager (a different MCU from the Duos') |
| **PowerBook Duo 210/250/270c/280** | `0024D346` / `015621D7` | none left — `MscMemory` + `PgePmu` ship; identity/clock variants of the gated Duo 230 |
| **Portable / PowerBook 100** | `96CA3846` / `96645F9C` | LCD framebuffer + M50753 (740/6502) Power Manager |

### Assets for new profiles

Local, never committed (`hdv/` is gitignored): Infinite Mac copies of System 4.1
(floppy), 5.1 / 6.0 / 6.0.8 / 7.0 / 7.1 / 7.5 / 7.5.5 HD `.dsk`, plus
`HD20SC.vhd`, `boot.vhd` / `GISTPERSO-boot.vhd`, `MacOS-7.6-boot.vhd`,
`MacOS-8.1-boot.vhd`. Full tree also at `../refs/infinite-mac/Images`. Missing
files: fetch with **Scrapling** (not raw `curl` through the sandbox proxy) —
`Fetcher.get` / `scrapling extract get` on
`https://raw.githubusercontent.com/mihaip/infinite-mac/main/Images/…`.
Flat HFS → SCSI façade: `ScsiDisk::open` (gate `scsi_hfs_facade_test`; offline
bake `tools/wrap_hfs.py`).

---

## 8. Cross-machine architecture

**Items marked [AR] come from the architecture review of the tree at
`d3bbd81` (2026-08-09)**, which measured the repository rather than reading
its docs. Two of its findings are already closed: the gate asset preamble
(`CHANGELOG.md` 2026-08-09) and the Moira fork decision
(`extern/moira/POM68K_VENDOR.md` § *Status*). The rest are below, in the
review's own ROI order.

- [x] **[AR] `MachineHost<Derived, Mem, Cpu, Audio>` — LANDED 2026-08-09.**
  The six `*Machine` structs (1 671 l.) are one CRTP host (`src/MachineHost.h`,
  383 l.) plus 681 lines of genuinely per-platform code; `src/main.cpp` went
  6 711 → 5 616. Measured before extracting: every shared member except
  `publish()` was **byte-identical across all six**, and `stepTick` was 92-100 %
  identical — so the split was not a judgement call.
  **The half the review did not name mattered more than the half it did**:
  `main.cpp` is the only TU outside `pom68k_core`, so this contract could never
  be linked by a test. It was lifted into a header *before* being unified, and
  `machinehost_test` (33 checks, `unit`) now gates what the compiler cannot —
  queue ordering, the framebuffer double buffer, both pacing branches and the
  thread teardown. `CHANGELOG.md` 2026-08-09 (third).
  **The GUI pass was done 2026-08-09** (dedicated Xvfb, a COPY of the boot
  image — the GUI attaches `writeBack = true`): framebuffer publish, status
  atomics, save + restore, and floppy hot-swap through the command queue all
  verified on screen, ending with the guest mounting `Rogue.dsk` picked from
  the Disques window. It also found a bug no gate could see — the 040 loops
  auto-inserted `disks35/Disk605.dsk` at power-on and the Quadra booted System
  6.0.5 off it (`CHANGELOG.md` 2026-08-09 (seventh)).
  **Still not gated, only checked once by hand.** There is no automated GUI
  gate and this pass does not create one; a screenshot harness under Xvfb is
  now demonstrably possible, and would be the way to keep it.
- [ ] **[AR] The `run*()` bodies are the remaining half of § 2·A.** The hosting
  is unified; the eleven `run*()` functions (3 428 l.) are not.
  `runCentris()` (344 l.) and `runQ700()` (350 l.) still share **304 identical
  lines after normalising platform identifiers, 88 %** — and the diff is
  almost entirely a *descriptor*: model selection from an env knob → name /
  clock / machine ID, RAM size, PRAM file suffix and which clock source takes
  the host time, window title and geometry. Collapse them the same way: one
  templated `runMachine(desc)` plus a literal per profile. Now cheaper than it
  was, since the host they all wire up is a single type.
- [x] **[AR] `etalon-core` — LANDED 2026-08-09.** 12 gates, one representative
  profile per platform, **12/12 green in 31 min 41 s** (the review estimated
  ~40). A name in `POM68K_ETALON_CORE` that stops being a registered gate is a
  configure-time `FATAL_ERROR`. **Building it found that four gates carried no
  label at all** — `iifx_boot_etalon`, `iifx_input_etalon`, `iifx_post_etalon`,
  `duo230_boot_etalon` were registered *after* the label-derivation block, so
  two whole platforms were invisible to every `ctest -L` tier. Block moved;
  `etalon` 81 → 85.
  **Link speed: wired, unmeasured.** `POM68K_FAST_LINK` (ON) probes for mold
  then lld and uses whichever the driver accepts; neither is installed on this
  host, so there is **no measurement to quote**. Ninja is available and would
  be the other half — it needs a fresh build tree, which is the user's call.
- [x] **[AR] `docs_test` — LANDED 2026-08-09**, 75 checks, `unit`, asset-free.
  Checks `kProfiles` rows == `SnapMachine` tags == every profile count
  `CLAUDE.md` states; every gate the file names in full is registered; every
  gate carries a label; every gate total quoted matches, **including the ones
  inside fenced code blocks** (`ctest -L unit   # 79 gates` — three counts were
  stale). Reads a roster CMake writes at configure time; the path is baked in,
  because searching for it relative to the working directory made the gate
  return 0 after two checks when run from the source tree. A negative control
  confirms it fails on a wrong number.
- [~] **[AR] Declare an expiry per environment knob — HALF LANDED 2026-08-09.**
  The surface is **133 distinct `POM68K_*` names** read as string literals
  across `src/`, `tests/` and the Moira fork (the review said "105 in `src/`",
  which both over-counts — build defines like `POM68K_VERSION_STRING` are not
  knobs — and under-counts, since gates and the fork read their own). 172
  `getenv` call sites, 37 memoised in a `static`.
  **Coverage: done.** `config_test` (`unit`, asset-free) now checks `DEV.md`
  § 5 + `src/jit/POM68K_JIT.md` against the tree in both directions. It found
  **12** knobs the code reads that no document mentioned — five real
  emulator knobs (`POM68K_Q700_MODEL`, `POM68K_Q900_IOPWATCH`,
  `POM68K_Q900_IOP_TRACE`, `POM68K_V8_IOHOLE`, `POM68K_V8_HOLEVAL`), four
  PG&E bring-up probes, three gate-local — plus one documented ghost
  (`POM68K_PERIPH_BATCH`), now under a **Retired** heading the gate enforces.
  All twelve are documented. *Two earlier counts of this same gap (24, then
  23) were the checker misreading the doc's own notation: § 5 writes
  `` `POM68K_PROBE*` `` and `` `POM68K_CENTRIS_FPU` / `_BAREFPU` ``, and both
  forms are real declarations. The gate models all three notations.*
  **Expiry: not done.** The seven bring-up probes now declare their chantier;
  the other ~120 entries still do not say whether they are a permanent product
  option (which earns a gate) or a chantier leftover. That is a decision per
  knob — the mechanism is in place, the classification is not.
  Secondary, non-urgent: an unmemoised `getenv` is a linear scan of the
  environment on glibc — cheap to rule out with a profiler, probably nothing.
- [ ] **[AR] Separate fixture roles, then version them.** The gates never write
  their images (`ScsiDisk::open()` defaults `writeBack = false`, no test passes
  `true`, and `q605_persist_etalon` replays its reboot against the in-memory
  image) — but the GUI attaches the *same* `hdv/*.vhd` with
  `attachScsi(path, true)`, twelve sites. A mutable, unversioned file is not a
  fixture: that is how `MacOS-7.6-boot.vhd` was corrupted and the IIfx gates
  went red. Two steps left now that the preamble prints the digest: a read-only
  `hdv/ref/` distinct from the volumes the GUI mounts, then a versioned
  `assets.lock` (name, size, SHA-256, provenance) + `tools/verify_assets.py` —
  which distributes no copyrighted content and makes the drift *nameable*.
- [x] **[AR] `CHANGELOG_INDEX.md` — LANDED 2026-08-09.** 205 dated entries in
  13 subsystem groups, generated by `tools/changelog_index.py`. The grouping is
  a keyword heuristic over each entry's hook and says so; an entry filed wrong
  is a bug in the table in that script, not something to hand-edit. The anchor
  slugger reproduces the changelog's own existing links exactly, so both index
  styles stay usable. `docs_test` fails when the index stops covering every
  dated entry — a generated index that silently falls behind is worse than
  none, because it looks complete.

- [ ] **Save states — one residual.** The feature shipped 2026-07-30 across all
  11 machine families and 36 profiles (archive core `src/SaveState.h/.cpp`,
  container `SaveStateMachines.h/.cpp`, `MoiraSnapshot.h`, GUI/CLI wiring in
  `main.cpp` `SaveStateSlot`). Gates: `savestate_test`, `savestate_v8_test`,
  `savestate_030/040/68k_test` (all `unit`), plus the real-OS
  `lcii_savestate_etalon` and `q605_savestate_etalon`.
  **Remaining: a hands-on GUI pass** — click « Sauver l'état » / « Restaurer
  l'état » on a booted machine. The machine-level save/load is gated; the GUI
  layer is compile-verified only.
  Conventions the chunks follow, worth keeping: callbacks and cross-device
  pointers are **re-bound, never serialized** (a pointer becomes an index —
  `Ncr5380::disk_`); pure caches are **flushed** on restore (ATC via the one
  vendored line `Moira::pomFlushAtcs()`, the 030 i-cache, the JIT guard) rather
  than carried; host-backed bulk data stays on the host (`ScsiDisk` ships a
  copy-on-first-write log of what the guest changed, not the image).
- [ ] **Optional HLE acceleration overlay** — **BLOQUÉ derrière § 3** par la
  décision du 2026-08-09 (§ 0·A) : on épuise le conformant d'abord, le HLE et
  le JIT non conformant ensuite, jamais l'inverse. Ce que cette décision fixe
  aussi, et que le doc laissait ouvert : la bifurcation § 2.3 de
  `HLE_OVERLAY.md` (HLE invité vs profil JIT relâché) se tranche en faveur du
  **profil JIT relâché côté hôte**, parce que le besoin réel est du *débit CPU
  soutenu sur 030/040*, pas de la latence d'I/O — patcher `.Sony` ne fait pas
  tourner le Finder d'une LC II plus vite. Le HLE au niveau invité redevient
  secondaire, **sauf QuickDraw**, qui est le seul endroit où le gain n'est pas
  borné par le taux d'instructions (§ 0·A).

  Reste vrai par ailleurs (`docs/HLE_OVERLAY.md`, after the
  `docs/LLE_VS_HLE.md` cleanup pass). Start with one hidden `boot.checksum`
  address hook and an HLE-forbidden accuracy-test mode; then signature-matched
  modules, per-module A/B gates and a visible non-conformant-mode indicator.
  ~~Prioritize disk HLE; defer timing-loop elision until its overlap with the
  JIT is understood~~ — **réordonné 2026-08-09** : le disque est de la latence
  d'I/O, pas du débit, donc secondaire pour l'objectif ; l'élision de boucles
  temporelles n'est plus « à comprendre » mais tranchée en profil JIT relâché.
  **The premise moved 2026-07-31**: the conformant JIT
  now measures **×2.68** on `q605_boot_etalon` (61.3 s → 22.9 s), i.e.
  THROUGH the "~×2.5-3 conformant ceiling" this item used to invoke as its
  justification — so the overlay can no longer be sold as the way to make
  POM68K fast. Its surviving argument is narrower and sharper: the residual
  idle-Finder cost is the ATC-exactness contract itself (794 M window-lost
  exits over 12.2 G instructions), and a non-conformant mode is exactly
  where the five relaxations the JIT refuses become legal. `docs/
  HLE_OVERLAY.md` § 0 dates every premise; read it before building anything.
  Note that the JIT now reaches **every** CPU wrapper (2026-08-06), so HLE
  is no longer the only accelerator anywhere — but on the 68000 and 68020
  guests the window is worth ×1.0-1.1, because there is no ATC walk to skip,
  which is precisely where an HLE overlay would have room.
- [ ] **Retro68 as a guest-level differential oracle**: build small Toolbox /
  Device Manager / XPRAM probes, run identical binaries under MAME and POM68K,
  compare. Known friction: no `Lists.h`/`AppleTalk.h` shims in multiversal
  (hardcoded list in `cincludes.rb`); the prober's `compat/` carries the
  PBControl glue; a 0-byte `.APPL` is normal.
- [ ] **Refactor the remaining GUI globals**: move compile-unit state such as
  `demoMode` (5 sites in `main.cpp`) into a machine/UI status object; keep
  machine threads, command queues and Emscripten's single-thread path
  behaviourally aligned. *This is the visible tip of the `MachineHost<M, C>`
  item at the top of this section — "keep the paths behaviourally aligned" is
  exactly the obligation that a single host would discharge structurally
  instead of by hand, across six copies.*

---

## 9. Closed this cycle — index only

One line each; the story is in `CHANGELOG.md` under the date.

- **i-cache boost vs bus time** (2026-07-25) — `viaSync` read the *boosted*
  clock and `stall()` charged E-clock waits in boosted cycles, so every VIA-paced
  pulse was `cacheBoost_`× too short in machine time and host-paced Egret
  transports wedged above ~20 MHz. **Bus time is charged in machine cycles on
  all four 030 CPUs** now; `AdbVia::syncTo` was the one remaining `getClock()`
  reader. `POM68K_Q605_CACHE_BOOST` default is back to **4**; the IIsi's
  `cacheBoost_ = 1` pin is retired.
- **Floppy write persistence** (2026-07-24) — gate `floppy_persist_test`.
- **Beyond-boot gates on the LC II** (2026-07-24 / 2026-07-29) — soak, persist,
  launch, floppy; caught the ~37 % MCU overclock (`CudaLle mcuDebt_`).
- **Save states** (2026-07-30) — see §8.
- **Egret / Cuda firmware LLE** (2026-07-23 → 2026-07-29) — `M68hc05` core, the
  instruction-slaved ADB wire (`M68hc05::onCycles`), the Cuda 341S0417 "wedge"
  that was a missing DFAC2 I2C ACK, factory firmware as the default everywhere.
- **SCC**: async baud machinery + Tx/Rx engine fidelity (2026-07-23); the OS 8.1
  / OpenTransport standing-abort hang (2026-07-28, gate `q605_ot_bind_etalon`) —
  a *virgin* line reads clean, the standing abort begins with the first frame the
  line carries.
- **CD-ROM** base + mount + boot-from-CD (2026-07-29) — see §5.
- **Input-delivery gates** for four boot-only machines (2026-07-29).
- **The IIsi "ADB Manager never initializes" bug — RETRACTED** (2026-07-29):
  there was never a bug. Three rounds of coherent findings were one artifact —
  `peek8()` is **physical**, and on a RAM-based-video machine physical low RAM
  *is* the framebuffer while the ROM's PMMU (`TC=$80F84500`) moves the System's
  logical low memory elsewhere. "ADBBase" was screen pixels. Lessons kept:
  **check `TC` bit 31 before trusting any low-memory read**, assert on something
  the MMU cannot move (pixels, wire traffic, device state), and **corroborate
  with the observation closest to the user's experience** — "does the cursor
  move?" (idle diff 0 px, after motion 46 px) would have killed all three
  rounds at the start. A real gate bug was found en route: a 16-byte scan of the
  8-byte KeyMap read `$017D=$41` as a keystroke — **a positive assertion over a
  too-wide window is a false green.**
- **Block linking** (commit `b2c4e19`, `POM68K_JIT.md §9`) — **this entry had
  gone stale and misdirected a whole planning round on 2026-07-30.** It is a
  link *table* (O(1) slot invalidation, no patched jumps); loading phase block
  entries −53 %, 268 → 566 instr/entry, 18.4 → 16.75 s. At the idle Finder it
  helps less because the hot exits are `JSR`/`RTS` (still `Unsafe`). *Keep this
  line as the reminder of why §0's house rule exists.*
- **JIT window survival under VM page-aging** (2026-07-31) — the culprit was not
  eviction selectivity but an unconditional `pomJitDtlbFlush()` (a 2 KB memset)
  on every window re-arm: ≈1.3 TB of memset traffic per long run, paid by both
  backends. Deleting it is conformance-neutral (every invalidation it stood in
  for has an exact owner). threaded −22.8/−26.7 %, x64 −28.5/−33.0 %; DTLB fills
  942 M → 7.8 M; boot etalon 61.3 s interp → **22.9 s JIT**.
- **`selectBackend("auto")` could never pick x64** (2026-07-30) — and the fix
  needed a second half: with the filter lifted, `auto` handed x64 the **68030**
  machines it was never written for and `jit_lcii_boot_etalon` wedged for an
  hour. **Backend validity is per GUEST family**, not just per host
  (`BackendCaps::guestFamilies`).
- **PGO training extended to the 030/020 machines** (2026-07-29,
  `tools/pgo_train.sh`) — LC II boot −26 %, LC 68020 boot −12 %, on the
  *default* engine.
