# Audit de parité MAME — puce par puce

**Date : 2026-08-05.** Rapport produit par une comparaison multi-agent (14 auditeurs, un par puce/grappe)
entre les sources POM68K et **MAME** : miroir local `~/src/refs/mame/` **plus** le master en ligne
`mamedev/mame` (fetch frais pour chaque fichier de référence ; les dérives miroir↔master constatées sont
signalées dans les sections concernées). Les findings rapportés par deux agents ont été dédupliqués
(4 doublons : quirk IER `$FF→$1F`, masque de lecture pseudo-VIA Sonora, re-latch ASC de niveau,
latch disk-change SWIM).

**Bilan chiffré (après déduplication) :** 0 bug-suspect *high*, **19 bug-suspect *medium***,
**40 bug-suspect *low***, 40 simplifications, 35 cosmétiques, 36 cas où POM68K est **plus riche** que MAME.
Aucun finding ne remettait en cause un chemin couvert par les gates vertes du jour — c'est précisément le
motif récurrent : chaque divergence vit sur un chemin que les ROMs/drivers expédiés n'empruntent pas.

---

## 0. ÉTAT AU 2026-08-12 — l'audit a été traité, ce document est sa clé

**Ce rapport est daté. Ne le lisez plus comme une liste de bugs ouverts.** Les
vagues de correction du 2026-08-06 au 2026-08-12 ont statué sur **chaque** entrée
de la table § 1 ; la disposition définitive vit **dans le code**, pas ici :

```bash
grep -rn "MAME-parity audit\|parity audit" src/ tests/    # 39 sites (2026-08-12)
```

Quatre dispositions, chacune marquée in-file à l'endroit concerné :

| Marqueur in-code | Sens |
|---|---|
| *(pas de marqueur, code changé)* | **CORRIGÉ** — le finding a été appliqué |
| `PIN` / `KNOWN MAME DIVERGENCE, deliberately kept` | divergence gardée **exprès**, avec sa raison |
| `DOCUMENT-SKIP <date>` | écart cosmétique assumé, commenté, non corrigé |
| `refuted` | le finding était **faux** — POM68K avait raison |

Vérifié en relisant le code le 2026-08-12 (échantillon large : #2-#6, #10-#14,
#15-#20, #24-#26, #28-#32, #36-#41, #45, #47-#57, #59) : la table ne contient plus
de finding non traité. Deux exemples des extrêmes — **#46 a été RÉFUTÉ**
(`M68hc05.cpp:120`, `tests/m68hc05_test.cpp:188` : le latch 6805 de POM68K est
correct) et **#21-#23 ont leur propre gate**, `via6522_parity_test`.

**Les 13 actions du § 3 sont faites, sauf une moitié.** Seule reste ouverte la
partie **Egret/Cuda de l'action 9** : `CudaLle.cpp:273-297` ne fait toujours que
relâcher le hold de boot sur PC3, donc un `RESET_SYSTEM $11` firmware n'atteint
jamais le 68030 — le chemin « Redémarrer » du Finder. La moitié Duo est faite
(`PgePmu::onCpuReset`, `PgePmu.h:71` → `PgePmu.cpp:338` → `MscMemory.cpp:88`).
Détail et condition de fermeture : `LLE_VS_HLE.md` § 1.9.

**Ce que ce fichier garde donc :** les citations MAME `fichier:lignes` (le coût de
re-dérivation), les verdicts par puce du § 2, et la liste « POM68K plus riche »
— qui est la seule trace écrite des endroits où un futur diff de parité
« corrigerait » le code dans le mauvais sens.

**Une entrée était FAUSSE dès l'origine** et a été propagée dans deux autres
documents : voir § 2.2 (persistance PRAM).

---

## 1. Table des findings *bug-suspect* (sévérité décroissante)

| # | Sév. | Puce | Description | POM68K | MAME |
|---|------|------|-------------|--------|------|
| 1 | M | ASC | Quadra 700/900/950 : ASC Sonora `$BC` au lieu de l'EASC `$B0` — aucun modèle EASC (pas de SRC, pas de CD-XA ADPCM, mauvais registres version/idle/clock) | `src/Q700Memory.h:313` | `sound/asc.cpp:1420-1771`, `macquadra700.cpp:805` |
| 2 | M | V8/TinkerBell | Mac TV : `ram_size` V8 réutilisé — 4 Mo utilisables au lieu de 8 sous config `$C0`, RAM fantôme à `$800000` que Tinker Bell ne décode pas | `src/V8Memory.cpp:236-255` | `apple/v8.cpp:1065-1101` |
| 3 | M | NCR 5380 | DRQ parasite en STATUS/MSG_IN sous MODE_DMA après réception ; un `dmaRead` en STATUS consomme l'octet de statut comme donnée | `src/Ncr5380.cpp:310-315,290-291` | `machine/ncr5380.cpp:227-245` |
| 4 | M | NCR 5380 | Aucune phase DATA OUT hors WRITE(6)/(10) : les octets paramètres de MODE SELECT / FORMAT UNIT sont perdus, saut direct en STATUS | `src/Ncr5380.cpp:76-99` | `bus/nscsi/hd.cpp:622-631` |
| 5 | M | NCR 5380 | Bus reset (ICR_RST) : pas d'IRQ, pas de latch RST lisible dans ICR/CSR (sur Mac II, l'edge IRQ VIA2 manque à chaque SCSIReset) | `src/Ncr5380.cpp:240-242` | `machine/ncr5380.cpp:330-355,449-463` |
| 6 | M | NCR 5380 | Arbitrage jamais annulable : clearing MODE_ARBITRATE laisse `phase_` bloqué, CSR lit BSY pour toujours | `src/Ncr5380.cpp:253-256,160-166` | `machine/ncr5380.cpp:389-405` |
| 7 | M | SCC 8530 | L'écriture data ne clear pas un TxIP pendant (buffer 1 slot) — l'écriture du caractère suivant EST l'acquittement | `src/Scc8530.cpp:475-489` | `machine/z80scc.cpp:2477-2500` |
| 8 | M | SCC 8530 | Reset Ext/Status ne re-présente jamais un changement d'état persistant → perte d'un front de quadrature souris (DCD) arrivé pendant la fenêtre latched | `src/Scc8530.cpp:563-572` | `machine/z80scc.cpp:1763-1781,793-818` |
| 9 | M | SCC 8530 | Reset Highest IUS (`$38`) clear tous les IP du canal au lieu d'un seul bit IUS — une 2e source pendante est jetée | `src/Scc8530.cpp:577-584` | `machine/z80scc.cpp:1782-1802` |
| 10 | M | NCR 53C96 | File de commandes 2 niveaux absente (pas de queue, pas de S_GROSS_ERROR au débordement, pas de pop-and-chain sur lecture ISTAT) | `src/Ncr53c96.cpp:319,334-434` | `machine/ncr53c90.cpp:887-916,1116-1117` |
| 11 | M | NCR 53C96 | CM_RESET_BUS ne purge pas les buffers en vol ni l'IRQ différée : lectures FIFO post-reset renvoient des octets fantômes | `src/Ncr53c96.cpp:352-356` | `machine/ncr53c90.cpp:965-970,324-334` |
| 12 | M | NCR 53C96 | CI_PAD : mauvaise cause de complétion sur épuisement TC (I_BUS au lieu de I_FUNCTION+S_TC0), pad émission (DATA OUT) non implémenté | `src/Ncr53c96.cpp:405-423` | `machine/ncr53c90.cpp:694-737,1031-1040` |
| 13 | M | IWM/Sony | Strobes MFM-on/GCR-on aux mauvaises adresses CA (chemin IWM classique) ; MFM-on fusionné sur DskchgClear — chaque ack disk-change bascule le SuperDrive en MFM | `src/SonyDrive.cpp:926-943` | `imagedev/floppy.cpp:3369-3386` |
| 14 | M | IWM/Sony | Sense RDDATA0/1 câblé faux : la signature de capacité `f..c` d'un SuperDrive (chemin SWIM1-as-IWM, LC II) se lit comme un HD-20 | `src/SonyDrive.cpp:823-830` | `imagedev/floppy.cpp:3229-3235,3494-3503` |
| 15 | M | MSC/PG&E | SOUND_BUSY modélisé bit 0 au lieu de bit 6 (le TODO est dans le code) — la logique idle/sommeil du PMU verra toujours « son inactif » | `src/MscMemory.cpp:42-54,291-295` | `apple/msc.cpp:24,130-134,229-238` |
| 16 | M | MSC/PG&E | Reset CPU piloté par PMU (port E bit 2) : ne reset pas le 68030 et ne ré-arme pas l'overlay ROM — reboot commandé par BORG = reprise d'état périmé | `src/PgePmu.cpp:289-299`, `src/MscMemory.cpp:55-61` | `apple/macpwrbkmsc.cpp:431-441`, `apple/msc.cpp:363-378` |
| 17 | M | DAFB | TurboSCSI PDMA : bus error immédiate sur !DRQ au lieu du hold-off /DTACK (bits 7/8 de scsiCtrl ignorés) — bus error parasite pour un driver légal | `src/Q700Memory.cpp:550-561` | `apple/dafb.cpp:995-1078` |
| 18 | M | Valkyrie | Horloge trame gated sur screen-enable : `$10` n'arme pas le VBL, `$14` (vblank) gelé tant que `$18` n'est pas écrit — un guest qui attend le VBL avant `$18` se wedge | `src/Valkyrie.cpp:118-122,177-179` | `apple/valkyrie.cpp:317-337,255-256` |
| 19 | M | Pseudo-VIA | Flavour Base : quirk IER « write `$FF` ⇒ stocke `$1F` » absent (le POST ROM IIci l'exige selon MAME) ; le commentaire in-file décrit un master périmé | `src/PseudoVia.cpp:89-94` (comm. :45-49) | `machine/pseudovia.cpp:290-305` |
| 20 | L | VIA 6522 | Décodage pseudo-VIA par flavour : Base write `&0x13` vs forme V8, Sonora read `&0x13` vs `&0x1f` (regs 4/5 aliasés), Msc write `&0x1f` vs NOP `$30-$FF` | `src/PseudoVia.cpp:30,51-65` | `machine/pseudovia.cpp:250-252,411-413,558-617` |
| 21 | L | VIA 6522 | T2 mode comptage d'impulsions PB6 (ACR bit 5) ne compte jamais | `src/Via6522.cpp:45-51` | `machine/6522via.cpp:129-150,1146-1165` |
| 22 | L | VIA 6522 | SR ne recircule pas en shift-out externe (MSB non rebouclé en bit 0) | `src/Via6522.cpp:95` | `machine/6522via.cpp:443` |
| 23 | L | VIA 6522 | Onde carrée PB7 pilotée par T1 (ACR bit 7) non modélisée (vSndEnb sur Plus) | `src/Via6522.h:36` | `machine/6522via.cpp:538-553,607-620` |
| 24 | L | IWM | Fenêtre 14 ticks du data latch ancrée au premier read MSB-set, pas ré-armée à chaque accès | `src/Iwm.cpp:93-109` | `machine/iwm.cpp:284-285,461-465` |
| 25 | L | IWM | Sense/commandes drive accessibles ENABLE off (pas de gating devsel) | `src/Iwm.cpp:36-37,113-115` | `machine/iwm.cpp:243-247` |
| 26 | L | IWM | Flux de nibbles moteur arrêté ; tach classique non gated sur moteur/disque | `src/Iwm.cpp:133`, `src/SonyDrive.cpp:813-822` | `machine/iwm.cpp:398-405`, `imagedev/floppy.cpp:836-849` |
| 27 | L | IWM/SWIM | Latch disk-change absent côté SWIM (`senseSwim 0x3` = présence, strobe `0xC` no-op) ; latch classique plus strict que master | `src/SonyDrive.cpp:842,861,899` | `imagedev/floppy.cpp:723,998-1001,3268-3269` |
| 28 | L | SWIM1 | Compteur magique IWM→ISM non reseté par accès intercalés ni avancé par les reads d'offset 0xf | `src/Swim1.cpp:116-130,150-159` | `machine/swim1.cpp:457-461,571-598` |
| 29 | L | SWIM1 | Écritures ISM offsets 8-15 aliasées sur 0-7 ; MAME les ignore | `src/Swim1.cpp:158` | `machine/swim1.cpp:286-355` |
| 30 | L | SonyDrive | Sense 0x6 (double-face) reflète le média au lieu du mécanisme (constante drive chez MAME) | `src/SonyDrive.cpp:861,164` | `imagedev/floppy.cpp:3278-3279` |
| 31 | L | SonyDrive | Strobe GCR-on (0xD) refusé sur média HD ; MAME bascule sans garde | `src/SonyDrive.cpp:82-88,900` | `imagedev/floppy.cpp:3382-3386` |
| 32 | L | SCC 8530 | Latch RR0 gèle D7-D3 en bloc ; MAME ne gèle que les bits armés dans WR15 | `src/Scc8530.cpp:381-388` | `machine/z80scc.cpp:1438-1443` |
| 33 | L | SCC 8530 | WR8 écrit via le pointeur control est stocké, jamais transmis (asymétrie avec RR8) | `src/Scc8530.cpp:594-595` | `machine/z80scc.cpp:2003-2008` |
| 34 | L | SCC 8530 | Désactiver le récepteur ne lève pas Sync/Hunt ; pin /SYNC absente de RR0 bit 4 en async | `src/Scc8530.cpp:710-715,129-145` | `machine/z80scc.cpp:1937-1940,2673-2706` |
| 35 | L | SCC 8530 | Activer WR1 Tx-int rejoue un évènement became-empty périmé comme TxIP frais | `src/Scc8530.cpp:743` | `machine/z80scc.cpp:1866-1903` |
| 36 | L | SCC 8530 | WR9 Status High/Low (bit 4) ignoré : le code de statut modifie toujours V3-V1 | `src/Scc8530.cpp:397-419` | `machine/z80scc.cpp:700-710` |
| 37 | L | NCR 5380 | Lectures reg 0/6 sous MODE_DMA auto-ACK un octet ; chez MAME les reads sont sans effet (seul DACK avance) | `src/Ncr5380.cpp:197-203,221-224` | `machine/ncr5380.cpp:273-279,501-506,791-799` |
| 38 | L | NCR 5380 | `sdir_w`/`enterCommand` clear le latch IRQ (compensation HLE documentée) ; seul RPI le fait sur silicium | `src/Ncr5380.cpp:262-268,53-59` | `machine/ncr5380.cpp:521-540` |
| 39 | L | ScsiDisk | READ/WRITE hors limites : zero-fill / clamp silencieux + GOOD au lieu de CHECK CONDITION (ILLEGAL REQUEST) | `src/ScsiDisk.cpp:328-354,411-427` | `bus/nscsi/hd.cpp:202-241,567-600` |
| 40 | L | NCR 53C96 | Pas de validation mode/commande : CI_XFER déconnecté ⇒ I_BUS différé au lieu d'I_ILLEGAL | `src/Ncr53c96.cpp:429-432,660-662` | `machine/ncr53c90.cpp:930-935,1059-1069` |
| 41 | L | NCR 53C96 | S_TC0 levé à la fin de tout payload, même non-DMA ou chunk court (drapeau strictement DMA-counter-zero chez MAME) | `src/Ncr53c96.cpp:541,255-258,40-45` | `machine/ncr53c90.cpp:1234-1251` |
| 42 | L | ASC | F09/F29 classique lit `$01` ; le master courant retourne 0 (conforme dump IIci) — fix MAME récent à adopter | `src/Asc.cpp:70-73` | `sound/asc.cpp:657-667,621` |
| 43 | L | ASC | Écriture FIFO B classique ignore le gate stéréo CONTROL (bits `$804` 2/3 + IRQ parasites en mono) | `src/Asc.cpp:84-104` | `sound/asc.cpp:704-739` |
| 44 | L | ASC | Écriture FIFO A (V8/classique) ignore le gate record-mode R_PLAYRECA sur les bits de statut | `src/Asc.cpp:106-127` | `sound/asc.cpp:386-404` |
| 45 | L | Apple PIC | Lectures du trou de registres `$F000-$F7FF` renvoient `$FF` vs `$00` chez MAME (même classe que le wedge Sonora ProductInfo) | `src/ApplePic.cpp:187` | `machine/applepic.cpp:63-77,345-351` |
| 46 | L | M68hc05 | L'acquittement TOF/CPI clear le latch d'interruption pendant ; MAME le garde jusqu'à la prise (« 6805 latches internally ») | `src/M68hc05.cpp:112-121` | `m6805/m6805.cpp:541-546,656-667` |
| 47 | L | M68hc05 | `onesec_w` ne ré-arme pas la phase du timer 1 Hz à chaque écriture (le commentaire prétend la parité, le code arme une fois) | `src/M68hc05.cpp:118-123,447-461` | `m6805/m68hc05e1.cpp:199-208` |
| 48 | L | PG&E | Port H lit `$FF` ; MAME retourne le latch (départ `$00`, précondition config DFAC du boot ROM PGE) | `src/PgePmu.cpp:267-272` | `apple/macpwrbkmsc.cpp:129,543-546` |
| 49 | L | PG&E | RTC PMU jamais seedée (`setSeconds` sans appelant) — horloge guest Duo à l'époque 1904 | `src/PgePmu.h:67` | `m6805/m68hc05pge.cpp:185-187` |
| 50 | L | DAFB | Bus TurboSCSI 2 (Eclipse) : registre `$28` sans DRQ vivant bit 9 ni latch wait-states (écho brut) | `src/Q700Memory.cpp:405-435` | `apple/dafb.cpp:424,533-576` |
| 51 | L | DAFB | Registre version câblé 3 partout ; MAME retourne 1 sur Q700/Q900 (le driver Apple branche sur ce champ) | `src/Dafb.cpp:69` | `apple/dafb.cpp:84,426-427` |
| 52 | L | DAFB | Danse Antelope PCBR1/x555 appliquée à tous les flavours ; Q950 reçoit l'ID Antelope `$02` au lieu d'AC842a `$01` | `src/Dafb.cpp:87-90,144-161` | `apple/dafb.cpp:712-747,1123-1174` |
| 53 | L | DAFB | Quirk Q700 512×384 (base=0x1000, vres=384 en version 1) absent de `recalcMode` | `src/Dafb.cpp:174-195` | `apple/dafb.cpp:833-839` |
| 54 | L | DAFB | Timing CRTC programmé ignoré si vtotal ≤ 480 : les modes 512×384 tournent sur la trame legacy 60.15 Hz | `src/Dafb.cpp:329-347` | `apple/dafb.cpp:869-875` |
| 55 | L | DAFB | L'appelant Q700 omet le clamp 12 bits que le contrat de la cellule Dafb suppose (stride/base/Swatch non tronqués) | `src/Q700Memory.cpp:417-435` | `apple/dafb.cpp:435,625-626` |
| 56 | L | Ariel | La lecture du registre d'adresse ne reset pas la phase RGB (comportement Brooktree standard) | `src/Ariel.h:26-27` | `video/ariel.cpp:96-106` |
| 57 | L | V8/Spice | VIA1 port A Color Classic lit `$83` ; MAME retourne `$82` (PA0 en plus, non documenté comme divergence) | `src/V8Memory.cpp:222-225` | `apple/v8.cpp:755-758` |
| 58 | L | VASP | Page device miroitée sur tout `$51000000-$5FFFFFFB` (SCC/SCSI atteignables) là où MAME laisse non mappé — RBV et Sonora gatent, VASP non | `src/VaspMemory.cpp:209-250` | `apple/vasp.cpp:54-64` |
| 59 | L | Sonora | Bande `$51000000-$5FFFFFFB` lit `$FF` vs 0 chez MAME (en contradiction avec la règle maison unmapped-IO=0) | `src/SonoraMemory.cpp:354` | `apple/sonora.cpp:49-61` |

---

## 2. Sections par puce

### 2.1 VIA 6522 + pseudo-VIA + E-clock

**Verdict** : parité forte sur tout ce que les ROMs Mac exercent (map registres, IFR/IER, sémantique T1/T2,
recalc pseudo-VIA, latches actifs-bas reg 2, split edge/level ASC par flavour). Les divergences vivent aux
marges du jeu de fonctions 6522 que le logiciel expédié ne touche pas, plus trois glissements de largeur de
décodage pseudo-VIA et le quirk IER IIci réellement manquant (le commentaire in-file pointe un master périmé).

**Bug-suspect** : table #19 (IER `$FF→$1F`, medium), #20, #21, #22, #23.

**Simplifications** (une ligne chacune) :
- Polarité des fronts CA1/CA2/CB1/CB2 fixe, PCR ignoré (sauf `extCb1Int` Duo) — correct pour toute ROM expédiée.
- Modes shifter à horloge interne : délai de complétion seulement, pas de bits déplacés ; shift-in déclenché par lecture SR jamais complété (couvert par DEV.md § M4).
- Modes de sortie CA2/CB2 (handshake/pulse/fixe) absents — aucun consommateur dans POM68K.
- Latching d'entrée port A/B (ACR bits 0-1) absent — les ROMs tournent latching off.

**Cosmétique** : période T1 N+2 (datasheet) vs N+3 MAME (~1,28 µs) ; `reset()` efface latches/SR/compteurs que MAME préserve.

**POM68K plus riche** :
- Re-échantillonnage du niveau ASC à chaque recalc (flavours Level/Msc) — corrige une interruption que MAME perd (fix SimCity 2000, documenté in-file) ; la Base garde le latch-edge MAME. *(rapporté par 2 agents)*
- Hold de niveau /PMU_INT sur IFR.CB1 (MSC) intégré à la classe de base, avec deadlock mesuré comme justification.
- `ViaEClock` : ratio E-clock fractionnaire exact + grille de stall /VPA (item LLE_VS_HLE § 1.2 clos).

### 2.2 RTC 343-0042

**Verdict** : correspondance étroite, par endroits délibérément supérieure, avec `macrtc.cpp` (master en ligne =
seule référence, le miroir local n'a pas le fichier). Moteur série bit-exact, commandes étendues, write-protect,
polarité enable — tout à parité. **Aucun bug-suspect.**

**Simplifications** :
- Pas d'onde carrée CKO : les machines pulsent CA2 à 1 Hz depuis `cpuHz` (nuance de phase demi-seconde perdue).
- ~~**Persistance PRAM absente sur 4 plateformes** (compacts, Mac II, IIfx, Duo)~~ — **FINDING FAUX,
  retiré le 2026-08-12.** Les **douze** plateformes déclarent `loadPram`/`savePram` (`MacMemory.h:123`,
  `MacIIMemory.h:158`, `IIfxMemory.h:111`, `MscMemory.h:126`, et les huit autres) et **chacun des douze
  runners** de `main.cpp` câble la paire (douze `loadPram`, douze `savePram` ; la première paire à
  `:1117` / `:1340`). Le fichier est `<image>.<tag-profil>.pram`. Ce qui varie est le **magasin**, pas la
  persistance : `Rtc` discret, XPRAM Egret/Cuda, ou RAM interne + SRAM du PG&E sur le Duo. L'erreur venait
  d'une ligne périmée de la table `CLAUDE.md` et a été recopiée dans `SIMPLIFICATIONS_REVIEW.md` (F1) —
  les deux sont corrigés. **Leçon : une « simplification » lue dans un doc et non dans le code n'est pas
  un finding.**
- Seed de l'heure une fois au lancement (pas de resync continu pré-écriture-guest).

**Cosmétique** : regs 12-15 lisent `$FF` vs `$00` ; registre test ignoré (inerte des deux côtés) ; superset 343-0042 unique servant aussi les compacts 343-0040.

**POM68K plus riche** : WP honoré sur écritures étendues (trou MAME) ; WP survit au reset (chip sur pile) ; seed PRAM Basilisk II déterministe ; comportement fin-de-lecture propre là où MAME underflow son compteur u8.

### 2.3 IWM + Sony 3.5" GCR

**Verdict** : bonne parité sur les chemins porteurs (décodage q6/q7, moteur d'écriture async, hold 14 ticks
correctement scalé C7M, tables GCR 6&2, checksum roulant bit-à-bit, 5 zones de vitesse). Le chemin lecture est
une HLE octet/nibble assumée (LLE_VS_HLE § 1.3, `FluxPll.h` porté mais non câblé). Les vraies divergences se
concentrent dans les tables commande/sense du chemin IWM classique (SWIM1-as-IWM, LC II), masquées par les
gates car `insertImage()` re-dérive `mfmMode_` de la taille du média.

**Bug-suspect** : table #13, #14 (medium) ; #24, #25, #26, #27.

**Simplifications** :
- Ready = `!motorOn` au lieu du compteur 2 tours de MAME (~0,25 s plus tôt).
- Chemin lecture byte-granulaire, cellules idéales, PLL non câblée — inventorié § 1.3 avec plan de réouverture.
- Timer moteur MODE_DELAY (~1 s) et bits mode 5-7 non modélisés (le Mac programme `$1F`, MAME devient immédiat aussi).

**Cosmétique** : géométrie de piste GCR (pregap/sync/gap4 légèrement différents, tags supprimés — § 1.3) ; mode 2M (MFM-sur-DD 600 RPM) inatteignable.

**POM68K plus riche** : write-back DC42 avec régénération de checksums (temp+rename), save-state mi-secteur bit-identique, PLL entière déterministe, compteurs diagnostiques (ceux du root-cause boost/denibble du 2026-08-05).

### 2.4 SWIM1 + SWIM2 (ISM/MFM)

**Verdict** : portage quasi ligne-à-ligne du master (FIFO taggé 2 niveaux, bits d'erreur, param-RAM rotative,
handshake, CRC-CCITT, chasse au sync MFM, sérialiseur TSS). Divergences en cas limites et dans le sense drive ;
la simplification cellule-idéale-vs-flux est celle documentée § 1.3.

**Bug-suspect** : table #28, #29, #30, #31 (+ #27 partagé avec l'IWM).

**Simplifications** :
- Moteur de lecture ISM du SWIM1 réduit au shifter SWIM2 : bits d'erreur CSM `0x08/0x20/0x40` jamais levés — **medium**, inventorié § 1.3.
- Cellules discrètes à cadence programmée au lieu de flux attotime + `fdc_pll` (§ 1.3).
- Nibble output-enable du registre phases non modélisé ; ligne SEL35 (et son kill moteur) ignorée.

**Cosmétique** : pas de DAT1BYTE sur Swim2 (aucun consommateur MAME non plus) ; seed CRC/préservation d'état ISM au reset ; underrun-avec-erreur-pendante termine quand même l'ACTION ; span d'écriture sans transition n'efface pas la piste.

**POM68K plus riche** : `cyclesToNextEvent()` pour le mécanisme deadline + save-state qui sérialise les transitions d'écriture en vol.

### 2.5 SCC 8530

**Verdict** : parité forte sur les mécaniques de pointeur, alias NMOS, miroirs WR2/WR9, vecteur RR2 (POM68K suit
même l'UM Zilog plus fidèlement que MAME sur le ranking), BRG, gating DCD. Sur SDLC, POM68K modélise beaucoup
plus que MAME (oracle faible, § 1.4). Les vraies divergences forment un **trio d'interruptions qui se compensent
mutuellement** sur le chemin Mac OS habituel (« servir une source, finir par `$38` ») — raison exacte pour
laquelle les gates ne les voient pas.

**Bug-suspect** : table #7, #8, #9 (le trio, medium) ; #32, #33, #34, #35, #36.

**Simplifications** :
- Pas de chemin d'entrée CTS (niveau machine constant, pas d'ext-int CTS, pas d'auto-enable Tx).
- DCD n'auto-enable pas le récepteur sous WR3 Auto Enables (DCD = quadrature souris sur Mac).
- Zero Count BRG (RR0 bit 1 + interruption) absent.
- Pas de verrou FIFO sur condition spéciale (Error Reset ne gate pas le flux).
- Pace unique dérivée de la source d'horloge Tx ; bits Rx-clock de WR11 ignorés (§ 1.4 en esprit).

**Cosmétique** : quel octet survit à l'overrun Rx ; reset matériel WR9 `$C0` applique les valeurs channel-reset (WR11/WR14, rr1Rd résiduel).

**POM68K plus riche** : moteur SDLC complet (Send Abort, underrun/EOM, FCS gen/verif, hunt, résidus, ligne), MIE en gate de niveau (plus proche du silicium que le drop MAME) — inventorié § 1.4.

### 2.6 NCR 5380 + ScsiDisk

**Verdict** : HLE phase-engine assumée (Ncr5380.h:18-20, § 1.5) — pas de bus nscsi, cibles synchrones. Layouts
ICR/MODE/TCR/CSR/BSR exacts, handshake initiateur et pseudo-DMA fidèles aux chemins du SCSI Manager, et le
comportement subtil dont dépend le Mac Plus (DRQ survivant au mismatch côté émission) reproduit par construction.
Les gaps sont côté réception et chemins d'erreur — jamais atteints par les boucles TIB à comptage exact d'Apple.

**Bug-suspect** : table #3, #4, #5, #6 (medium) ; #37, #38, #39 (ScsiDisk).

**Simplifications** : SER stocké sans interruption de (re)sélection (aucun producteur) ; chemin busy-error MONBSY absent ; aucun timing d'arbitrage/sélection (tout dans l'écriture registre — posture § 1.5).

**Cosmétique** : BAS_ENDOFDMA/EOP, bits de parité, phase-match à bus free, bit 6 ICR relu.

**POM68K plus riche** : façade flat-HFS (DDM+partition+driver synthétiques), log copy-on-first-write pour save-states, UNIT ATTENTION `$28` d'insertion à chaud, pages CD Apple `$30`/`$0E` conformes octet-à-octet au master.

### 2.7 NCR 53C96 (TurboSCSI)

**Verdict** : modèle fonctionnel à parité sur registres, latch IRQ, étapes de séquence, ordre S_TC0/I_BUS et
DRQ-gated PDMA — les drivers 7.5.5→8.1 sont entièrement servis. Les simplifications § 1.5 sont confirmées comme
inventoriées. Les gaps réels sont tous dans des chemins froid/erreur jamais exercés par les gates.

**Bug-suspect** : table #10, #11, #12 (medium) ; #40, #41.

**Simplifications** : CI_COMPLETE instantané (contourne le modèle de latence différée) + seq non mis à jour après MSG_ACCEPT ; staging tcounter↔FIFO court-circuité (inventorié § 1.5, avec les deux écarts délibérés documentés : pré-avance de phase Q6.6b et reload tcounter non-DMA du chemin 7.5.5).

**Cosmétique** : effet de bord `status_r` du 53C90A absent (inobservable : S_GROSS_ERROR/S_PARITY/S_TCC jamais levés).

**POM68K plus riche** : compteur 24 bits + `R_TCHIGH` toujours actifs (plus proche du vrai 53C96 que la base MAME ; caveat TC=0 → `0x10000` lisible) ; R_FLAGS retourne le sequence step en bits 7:5 (fidèle datasheet, MAME non).

### 2.8 ASC (V8 / Sonora / IOSB) + EASC

**Verdict** : parité forte sur tous les chemins chauds ; la divergence phare (`$804` clear inconditionnel vs gate
`!(HALF_B)` de MAME) est délibérée, justifiée par les dumps ASCTester que MAME embarque lui-même, et inventoriée.
Sur plusieurs points POM68K colle mieux au vrai matériel que le câblage MAME. Manques réels au moment de
l'audit : **pas de modèle EASC du tout** (Quadra 700/900/950 servis par le Sonora `$BC`) — **corrigé depuis**
(action 8 du § 3 : classe `AscEasc`, version `$B0`, câblée dans `Q700Memory.h:324`, gate `asc_easc_test`) ;
stub wavetable ; et quelques bords classiques.
Le master fetché diffère du master 2026-07-15 cité dans les commentaires (F09/F29 lit désormais 0 ; l'EASC
complet a atterri) — reflété dans les findings.

**Bug-suspect** : table #1 (medium) ; #42, #43, #44.

**Simplifications** :
- Mode wavetable classique = stub silence, registres wavetable 2/3 perdus (`regs_[0x20]`) — **medium** ; **inventorié depuis le 2026-08-12** à `LLE_VS_HLE.md` § 1.7 (c'est la seule simplification audio avec un oracle : MAME l'implémente et embarque le dump ASCTester).
- IRQ idle-empty-cycle classique (dérivée QEMU) inexistante chez MAME et sur le dump IIci — **condition de réouverture écrite depuis le 2026-08-12**, `LLE_VS_HLE.md` § 1.7 (soit un binaire réel qui en dépend, soit la retirer derrière un flag et vérifier que `asc_test` reste vert).
- Duo : AscV8 `$E8` au lieu du variant MSC `$E9` (clone exact, TODO in-file).

**Cosmétique** : ports FIFO 16 bits IOSB non modélisés (non câblés chez MAME non plus) ; largeur du fichier registres (lectures hors blocs → 0 vs backing store complet).

**POM68K plus riche** : `$804` read-clear inconditionnel (quirk épinglé, `LLE_VS_HLE.md` § 1.7) ; état reset AscIosb conforme au dump LC 475 (MAME ne câble même pas son asc_iosb) ; FIFOSTAT reset Sonora `$0A` (fix hang LC 520/550) ; V8 F09/F29=0 conforme au vrai LC (à épingler d'un commentaire pour éviter une « correction » vers la valeur MAME fausse) ; `$807` CLOCK RATE honoré.

### 2.9 Grappe ADB (AdbBus / AdbLine / AdbVia / Pic1654s)

**Verdict** : le chemin LLE par défaut est en parité forte et par endroits délibérément meilleur — AdbLine est un
portage fidèle de la machine d'états de macadb.cpp (jusqu'au quirk buffer[1]-first), Pic1654s colle au pic16c5x
instruction par instruction. **Aucun bug-suspect sur le chemin par défaut.** Le garde anti-race CB2 de MAME est
structurellement inutile ici (co-stepping cycle-exact). Les défauts restants vivent sur les fallbacks HLE
annoncés NON-CONFORMANT sur stderr et inventoriés § 1.6/§ 2.

**Simplifications** :
- Machine octet HLE AdbVia (fallback sans dump 342s0440-b) : Listen supposé 2 octets, filler `$FF`, pacing fixe — **medium**, inventorié, jamais silencieux.
- AdbBus HLE (fallback Egret-HLE) : handler ID 5, pas de bitmap modificateurs R2, Talk R0 sans timeout — marqué « retires with the Egret HLE » § 2.

**Cosmétique** : garde CB2 MAME absent (inutile par construction — à ne pas « porter ») ; opcodes indéfinis `0x050-0x05F` = CLRW vs illégaux ; effets de bord d'écriture TMR0 (2 cycles) ; ordre d'octets Talk R0 incohérent entre LLE et HLE.

**POM68K plus riche** : SendReset/pulse ligne restaurent les défauts device (MAME : rien) ; Talk R0 par « pending » au lieu du dedup octets (fix famine souris LC II) ; handler IDs + protocoles commutables + bloc identité R1 + LEDs R2 (§ 1.6, oracle DingusPPC) ; buffer Listen borné ; fil RTCC/TMR0 réellement piloté (MAME ne l'assert jamais) ; port B quasi-bidirectionnel pins-AND-latch ; ST0/ST1 conscient de DDRB ; constantes de timing mesurées sur le vrai firmware.

### 2.10 68HC05 (M68hc05) + Egret/Cuda LLE

**Verdict** : parité forte sur tout le porteur — table de cycles verbatim, sémantique de flags opcode par opcode,
charge d'interruption 11 cycles conforme (la note mémoire « serviceInterrupts à 0 » est périmée pour l'E1),
map E1, câblage ports Cuda/Egret conforme master. `Egret.cpp` est le fallback HLE documenté (§ 1.9/§ 2), audité
comme glue.

**Bug-suspect** : table #46, #47.

**Simplifications** :
- **Ligne reset PC3 = seul le release de boot** : un RESET_SYSTEM (`$11`) piloté par firmware n'atteint jamais la machine (Egret surtout ; le Cuda de MAME a son propre quirk symétrique) — **medium** ; **inventorié depuis le 2026-08-12** à `LLE_VS_HLE.md` § 1.9, et c'est la moitié encore ouverte de l'action 9 (§ 3). Sorties PC2 NMI et PA4 DFAC également absentes, même raison : aucun consommateur.
- Timer programmable fixé à 512 cycles quel que soit le rate PLL (le cheat rate-2→3 partagé rend ça invisible pour le firmware expédié).
- DFAC2 = ACK I2C seulement, audio non routé dans l'atténuateur (`LLE_VS_HLE.md` § 3, « The Cuda's I2C bus and its DFAC2 » — c'est la parité MAME, dont le `dfac2_device::write_data` ne fait que logger ; la parité *stricte* muterait ces machines).
- Egret.cpp : HLE totale du transport, fallback uniquement (§ 1.9).

**Cosmétique** : ACK esclave I2C un demi-clock en avance (couvre la phase échantillonnée) ; opcodes indéfinis = halt (aide au debug) ; CC empilé bits 7-5 forcés à 1 (plus fidèle silicium que MAME).

**POM68K plus riche** : WAIT/STOP implémentés là où MAME `fatalerror` (STOP approximé en WAIT).

### 2.11 Apple PIC IOP (R65C02 + ApplePic)

**Verdict** : portage fidèle ligne-à-ligne d'`applepic_device` (map registres, fenêtre hôte, timer, DMA, bypass),
cœur R65C02 à parité opcodes/cycles/flags. Résidus tous *low* ; sur le chaînage DMA DENxONx, POM68K corrige
même un bug de copie par valeur du master.

**Bug-suspect** : table #45.

**Simplifications** : échantillonnage d'interruption à granularité instruction (IRQ à travers CLI pris une instruction plus tôt) — déviation assumée, documentée in-source.

**Cosmétique** : IRQ = recompute de niveau pur (résultat identique) ; cadence timer/DMA asservie au flux d'instructions avec report de dette (formules MAME respectées).

**POM68K plus riche** : chaînage DMA DENxONx fonctionnel (bug MAME `auto other = m_dma_channel[ch^1]` par valeur — à commenter pour qu'un futur diff de parité ne le « répare » pas à l'envers) ; `$CB/$DB` = WAI/STP WDC (documenté, condition de réouverture) vs NOPs 1 cycle du R65C02 MAME ; watchpoints/trace/sérialisation de phase.

### 2.12 PG&E Power Manager + MSC

**Verdict** : parité forte sur le cœur (banking OPTION/CSCR dont dépend l'upload BORG `$E1`, moteur SPI, CPICSR/KCSR,
timers ADB, transport REQ/ACK, DS2400). Divergences en trois familles : deux écarts transport délibérés, mesurés
et env-gated (SPI↛IFR.CB1, re-hold /PMU_INT) ; une cellule ADB bien plus riche que celle de MAME (qui ne lève
jamais RDRF) ; et de vrais slips, menés par SOUND_BUSY bit 0 vs 6. `duo230_boot_etalon` ne couvre que le cold
boot — aucun des bug-suspects ne pouvait la faire tomber.

**Bug-suspect** : table #15, #16 (medium) ; #48, #49.

**Simplifications** :
- Matrice clavier/power key/trackball non câblées, entrée injectée au niveau cellule ADB — milestone déclaré (DUO_BRINGUP, « Next: input through the PMU ») ; piège documenté : le `$DF` littéral de MAME = power-key-held → hang.
- ~~Pas de persistance NVRAM RAM interne + SRAM (PRAM/flag power)~~ — **PÉRIMÉ : livré avec le 37e profil**, `MscMemory::loadPram`/`savePram` (`MscMemory.cpp:137-175`) sérialisent la RAM interne + la SRAM 32 Ko du PG&E et **appliquent le scrub `$91`** (cold boot forcé, `m68hc05pge.cpp:959`) que cette ligne annonçait comme « à copier ». Même classe d'erreur que le finding PRAM du § 2.2 : lu dans un doc, pas dans le code.
- Entrée d'interruption facturée 0 cycle (inexactitude délibérée partagée avec l'E1 — leçon Mac TV).
- `power_cycle_w` et le bit clock-divide MSC loggés, non modélisés (milestone sommeil).

**Cosmétique** : latch /IRQ externe non annulé au deassert (pin non câblée) ; STOP traité comme WAIT (MAME garde aussi ses timers) ; entrées non câblées lisent `$FF` vs 0.

**POM68K plus riche** : re-hold /PMU_INT sur IFR.CB1 à travers les acks guest (deadlock mesuré, medium) ; horloge SPI volontairement sans IFR.CB1 (0 vs 1122 sélections SCSI, knobs `POM68K_PGE_CB1INT` pour retrouver le câblage MAME littéral — à re-tester si master rebinde) ; cellule modem ADB avec vrai trafic device (Listen/Talk pacés, relocation R3 effective).

### 2.13 DAFB/Antelope + Valkyrie + Ariel

**Verdict** : parité forte sur tout ce que les profils expédiés exercent — les trois générateurs d'horloge
(+`$300`) bit-à-bit et routés aux bonnes plateformes, Swatch, sense étendu, IRQ read-to-clear, table de modes
Valkyrie. Les divergences se concentrent là où POM68K a aplati les quatre sous-classes DAFB de MAME en une
cellule, et dans la cellule TurboSCSI Q700/Eclipse.

**Bug-suspect** : table #17, #18 (medium) ; #50, #51, #52, #53, #54, #55, #56.

**Cosmétique** : 4e lecture CLUT consécutive wrap sur rouge vs 0 non borné ; palette mono (sense 1/3) canal bleu non répliqué ; divide stride convolution non appliqué (branche jamais activée) ; interruption aux-scanline ignorée en silence (MAME fatalerror).

**POM68K plus riche** : Ariel garde le registre key-color séparé (master l'aliase sur control — bug MAME probable, à remonter upstream) ; typo I2C Valkyrie `(m_P = 98)` reproduite par effet sans l'effet de bord ; VBL Valkyrie à `vres` chaque trame (MAME ré-arme à la ligne 480 codée en dur) ; gardes div-zéro/overflow sur les trois clockgens.

### 2.14 Gate arrays plateforme (RBV / V8-Eagle-Spice-TinkerBell / Sonora / VASP)

**Verdict** : parité très proche au niveau fichier de registres (split trois-devices du PseudoVia, table de
modes Sonora, algèbre de sense, machine DAC, défauts de reset). Gaps réels : le quirk IER `$FF→$1F` (table #19,
partagé avec l'agent VIA) et le `ram_size` Tinker Bell (table #2).

**Bug-suspect** : table #2, #19 (medium) ; #57, #58, #59 (+ #20 partagé).

**Simplifications** : géométrie trame/VBL V8 fixée sur la modeline 12" quel que soit le sense (MAME fixe la 13" ; seul RBV a `recalcFrame()`) — **inventorié depuis le 2026-08-12** à `LLE_VS_HLE.md` § 1.1, avec sa condition de réouverture (un sense V8 sélectionnable à l'exécution) ; duplication canal-bleu CLUT du mode portrait mono Sonora absente (`RbvVideo.h:66-72` l'a, `SonoraVideo.h` non) — `SIMPLIFICATIONS_REVIEW.md` F2.

**Cosmétique** : registres DAC VASP miroités sur toute la fenêtre 8 Ko (probablement plus fidèle au partial-decode réel) ; quirk matériel « IER bit 7 lit 1 » du MDU IIci/IIsi absent des deux émulateurs (parité MAME conservée, divergence matérielle documentée chez MAME même).

---

## 3. Actions recommandées — **12,5 / 13 FAITES** (état 2026-08-12)

Classées à l'origine par rapport risque/effort. Rappel maison : jamais de ctest complet en itération — viser
la gate la plus étroite. Le statut vérifié de chaque action est en tête de ligne ; la preuve est le code cité.

| # | État | Preuve dans le code |
|---|---|---|
| 1 | ✅ | `MscMemory.cpp:61-67,391-392` — `SOUND_BUSY` est bien le bit `0x40` |
| 2 | ✅ | `PseudoVia.cpp:100-108` — cas `v == 0xFF && Flavour::Base` ⇒ `$1F`, commentaire refait |
| 3 | ✅ | `SonyDrive.cpp:1036-1052` — cas `0b011` ajouté, MFM-on découplé de DskchgClear ; sense `f..c` à `:907-913` |
| 4 | ✅ | `Scc8530.cpp:515` (TxIP sur écriture data), `:615-623` (re-présentation Ext/Status), `:635-652` (IUS : plus aucun IP jeté) ; **plus** #33 à `:658-661` |
| 5 | ✅ | `V8Memory.cpp:321-338` — override Tinker Bell dans `applyRamConfig`, pas d'alias `$800000` |
| 6 | ✅ | `Ncr5380.cpp:355-364,391-395` — `drqActive()` directionnel, l'octet de statut n'est plus consommé |
| 7 | ✅ | `Q700Memory.cpp:608-641` — hold-off /DTACK gaté sur `scsiCtrl` bits 7/8, cap ~20 ms puis /BERR ; `$28` bus 2 à `:432-435` |
| 8 | ✅ | `Asc.h:243` — flavour `AscEasc`, gate `asc_easc_test` |
| 9 | ⚠️ **moitié** | Duo fait (`PgePmu.h:71`, `PgePmu.cpp:338`, `MscMemory.cpp:88`) ; **Egret/Cuda PC3 toujours ouvert** (`CudaLle.cpp:273-297`) — seul reliquat de tout l'audit |
| 10 | ✅ | `Ncr53c96.cpp:377-400` — file 2 niveaux, `S_GROSS_ERROR`, pop-and-chain ; gate `ncr53c96_queue_test` |
| 11 | ✅ | `Valkyrie.cpp:124-132` — `vblArmed_ = true` sur l'écriture `$10` ; `$14` lit la ligne vivante (`:85-92`) |
| 12 | ✅ | `ApplePic.cpp:205-211`, `SonoraMemory.cpp:365-372`, `VaspMemory.cpp:219-222` — tous à 0, règle maison unmapped=0 |
| 13 | ✅ | `Asc.cpp:71-80` (« Do not "fix" toward MAME »), `ApplePic.cpp:115-125` (DMA DENxONx), `Ariel.h:15-21` (key-color) |

Le texte d'origine de chaque action, avec son raisonnement :

1. **MSC SOUND_BUSY bit 6** (#15) — fix une ligne (`0x40` dans `MscMemory.cpp`) ; vérifier `duo230_boot_etalon`. À faire avant le milestone sommeil, qui lit ce registre.
2. **Quirk IER `$FF→$1F` flavour Base** (#19) — ajouter le cas + corriger le commentaire périmé (:45-49) ; un check dans `pseudovia_test` (écrire `$FF`, relire `$1F`) le fige. `iisi/iici/iivx/iivi_boot_etalon` en confirmation.
3. **Strobes SuperDrive chemin IWM classique** (#13/#14) — ajouter le cas `0b011` dans `SonyDrive::command`, découpler MFM-on de DskchgClear, gate RDDATA/2M ; nouveau check unitaire de signature `f..c` (l'infra `lcii_sony_trace` est le microscope tout trouvé) ; `lcii_floppy_etalon` en garde-fou.
4. **Trio d'interruptions SCC** (#7/#8/#9) — corriger d'abord Reset Ext/Status (re-présentation par diff latched/live : perte réelle de fronts souris), puis TxIP-on-write et l'IUS ; nouveau test unitaire multi-sources sur un canal (aucune gate existante ne compte les fronts). Rejouer `llap_loop_test` + `macii_mouse_etalon`.
5. **Mac TV `ram_size`** (#2) — override Tinker Bell dans `applyRamConfig` (MB à 0, pas d'alias `$800000`) ; test unitaire headless : peek `$800000` + total RAM Gestalt sur le profil 8 Mo ; `mactv_boot_etalon` en garde-fou (transport Cuda↔VIA fragile — mémoire projet).
6. **5380 DRQ parasite en STATUS** (#3) — étendre `scsi_pdma_test` : après la fin d'un DATA IN, une lecture PDMA supplémentaire ne doit pas consommer l'octet de statut ; aligner `drqActive` sur l'asymétrie send/receive du master.
7. **DAFB TurboSCSI hold-off** (#17) — implémenter le spin-until-DRQ gaté par scsiCtrl bits 7/8 (le mécanisme deadline donne le point d'accroche) ; test unitaire sur la cellule ; `q700/q900/q950_boot_etalon` en confirmation. Au passage, le registre `$28` du bus 2 (#50).
8. **ASC EASC pour les Quadra discrets** (#1) — nouveau flavour `AscEasc` (version `$B0`, idle `$0F`, SRC, CD-XA) d'après le `asc_easc_device` récent du master ; commencer par le registre version + idle (test type `q605_asc_test` sur Q700), le SRC/ADPCM ensuite. Gros morceau — à milestoner.
9. **Reset PMU→CPU** (#16 port E Duo, et § 2.10 PC3 Egret/Cuda) — câbler un callback reset machine (re-arm overlay compris) ; nouvelle etalon « Restart depuis le Finder » sur une machine Egret, la seule façon de couvrir ce chemin (aucune gate actuelle ne l'exerce).
10. **53C96 file de commandes** (#10) — queue 2 niveaux + S_GROSS_ERROR + pop-and-chain sur ISTAT ; test unitaire pipelinant deux commandes avant lecture ISTAT. Prépare A/UX/NetBSD.
11. **Valkyrie VBL** (#18) — armer le VBL sur l'écriture `$10` et laisser le compteur de trame free-run ; check dans une unité Valkyrie (lecture `$14` doit osciller avant `$18`) ; `q630/lc580_boot_etalon` en garde-fou.
12. **Hygiène open-bus** (#45 Apple PIC `$FF`, #59 bande Sonora, #58 miroir VASP) — aligner sur la règle maison unmapped=0 (leçon ProductInfo LC III+) ; gates boot des plateformes concernées en confirmation.
13. **Épinglages sans code** — commentaires citant MAME sur : V8 F09/F29=0 (POM68K a raison, master V8 a tort), chaînage DMA DENxONx (bug MAME par-valeur), Ariel key-color (bug master à remonter upstream), F09/F29 classique à passer à 0 (fix master récent à adopter, #42).
