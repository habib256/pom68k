# POM68K Prober — Phase 1 (spécification d'implémentation)

Outil Macintosh 68k (compilé avec Retro68) qui teste **POM68K de l'intérieur** :
il collecte l'identité machine/ROM/chips, énumère l'état AppleTalk, et **dépose
son rapport en JSON Lines sur le volume AFP monté** — un répertoire du host servi
par netatalk. Le rapport est ensuite relu côté host (boucle de conformité).

> Angle double : ce que le *guest déclare* (Gestalt, NBP) doit correspondre à ce
> que le *host observe* (netatalk, pcap). Tout écart = un point où POM68K n'est
> pas exact. La Phase 1 pose la moitié « intérieur » + le canal de retour AFP.

## 0. Périmètre

- Aucune modification de l'émulateur (n'utilise que `POM68K_LTOUDP=1` + netatalk).
- Trois collectes : `ident` (identité + sondage bus-error), `net` (AppleTalk),
  `report` (écran + fichier AFP).
- L'app **rapporte des valeurs brutes** ; c'est le golden côté host qui *juge*.
  Donc pas de table de constantes machine à maintenir dans le guest.

## 1. Arborescence & build

```
dev/prober/
  main.c        — fenêtre, menus, boucle d'événements, orchestration
  report.h/.c   — modèle Report + rendu List Manager + écriture AFP (JSONL)
  ident.h/.c    — Gestalt + ROM/low-mem + sondage bus-error (palier 2)
  net.h/.c      — AppleTalk : node/net, zone, lookups NBP, MacIP
  probe.s       — handler bus/address-error (68k asm) + setjmp/longjmp
  CMakeLists.txt
```

```cmake
cmake_minimum_required(VERSION 3.5)
project(POM68KProber C)
add_application(POM68KProber
    main.c report.c ident.c net.c probe.s)
```
Ressources (menus/fenêtre) créées à l'exécution → pas de `.r`.

## 2. Modèle de données (`report.h`)

```c
typedef enum { R_INFO, R_OK, R_WARN, R_FAIL } RStatus;

typedef struct {
    char    section[16];   /* "ident" | "net" | "probe" | "serial"       */
    char    key[40];       /* "cpu", "myZone", "VIA2@0x50F02000", ...     */
    char    value[80];     /* valeur textuelle (hex "0x04", déc "128", …) */
    RStatus status;        /* le guest émet surtout R_INFO                */
} RFinding;

typedef struct { RFinding items[256]; short count; } Report;
```

Le guest émet des faits (`R_INFO`) ; `R_OK/R_FAIL` seulement pour les auto-tests
internes (ex. loopback série en Phase 2). Le verdict OK/FAIL par machine se fait
côté host (golden).

## 3. `ident` — identité

### 3.1 Palier 0 : Gestalt (System ≥ 6.0.4)
Vérifier la présence du trap `_Gestalt` ($A0AD) avant appel ; sinon repli 3.2.
Sélecteurs collectés (valeur brute, non interprétée dans le guest) :
`gestaltMachineType`, `gestaltSystemVersion`, `gestaltROMVersion`,
`gestaltROMSize`, `gestaltProcessorType` (1..5 = 000/010/020/030/040),
`gestaltFPUType`, `gestaltMMUType`, `gestaltHardwareAttr` (bitfield),
`gestaltADBVersion`, `gestaltAppleTalkVersion`, `gestaltMacTCPVersion`.
Codes 4-cc et bits exacts : inclure `Gestalt.h` du multiversal, ne pas recopier.

### 3.2 Palier 1 : ROM + low-mem (repli, tout System)
`LMGetROMBase()` ($2AE) ; checksum long à ROMBase+0 ; mot d'ID à ROMBase+8
(hi = famille, lo = révision) ; `LMGetROM85()` ($28E) ; `HWCfgFlags` ($B22) ;
`LMGetMemTop()`.

### 3.3 Palier 2 : sondage bus-error (inclus)
`ident.c` installe le handler (`probe.s`), sonde un **surensemble** d'adresses,
émet `present|absent` par adresse, restaure les vecteurs. Le golden sait
lesquelles doivent répondre par machine. **C'est le cœur du test** : POM68K doit
répondre aux adresses présentes et bus-errorer aux absentes, comme la vraie
machine.

Adresses candidates documentées (Apple/MAME ; recouper avec les `read8`/`write8`
de `V8Memory.cpp` / `Q605Memory.cpp` / `MacIIMemory.cpp` — le header V8 fixe la
base I/O à `$F00000+`) :

| Puce | Plus (24-bit) | V8 (LC/LCII/CII/CC) | Mac II | Q605 |
|---|---|---|---|---|
| VIA1 | `$EFE1FE` | `$F00000` | `$50F00000` | `$50F00000` |
| VIA2/RBV | — | pseudo-VIA `$F26000`* | `$50F02000` | pseudo-VIA* |
| SCC (r) | `$9FFFF8` | `$F04000` | `$50F04000` | `$50F04000` |
| SCSI | `$580000` | `$F10000`* | `$50F10000` | 53C96 `$F10000`* |
| ASC | — | `$F14000`* | `$50F14000` | IOSB ASC* |
| IWM/SWIM | `$DFE1FF` | `$F16000`* | `$50F16000` | SWIM2* |
| Vidéo | framebuffer RAM | Ariel/VRAM* | slot NuBus | DAFB `$50F40000`* |
| NuBus | — | — | `$Fs000000` s=9..E | — |

\* offset exact à lever du décodage de la carte concernée.

> **Attention** : lire un registre de périphérique a des effets de bord (une
> lecture de VIA/SCC peut acquitter un flag d'interruption). Toléré pour un
> prober ; commenté dans le code. Ne rien appeler du Toolbox entre install et
> restore (la fenêtre de sondage court-circuite le handler bus-error de l'OS).

## 4. `net` — état AppleTalk

Séquence (`.MPP` = DDP/NBP, `.XPP` = zones) :
1. `OpenDriver("\p.MPP", &mppRef)` → up/down.
2. `GetNodeAddress(&node, &net)` → adresse de nœud/réseau.
3. `.XPP` : `GetMyZone` → zone locale ; `GetZoneList` → zones connues (si routeur).
4. Lookups **NBP** : pour chaque type (`AFPServer`, `LaserWriter`, `Workstation`),
   `NBPSetEntity(buf,"=",type,"*")`, `PLookupName`, puis `NBPExtract` de chaque
   tuple → `objet:type@zone net.node.socket`.
5. MacIP : `Gestalt(gestaltMacTCPVersion)` + `OpenDriver("\p.IPP")` → présent/absent.

Champs de `MPPParamBlock` via les macros classiques `NBPinterval`, `NBPcount`,
`NBPentityPtr`, `NBPretBuffPtr`, `NBPretBuffSize`, `NBPmaxToGet`, `NBPnumGotten`
(Inside Macintosh: Networking). Vérifier les noms exacts dans `AppleTalk.h` du
multiversal ; repli bas niveau via `Control(.MPP/.XPP)` avec le `csCode`
documenté (`docs/APPLETALK.md` §3.3/§4.4) si une glue manque.

## 5. `report` — rendu + dépôt AFP (canal de retour IA)

### 5.1 Localiser le volume
Boucle `PBHGetVInfoSync` sur `ioVolIndex = 1..n`, match `EqualString` avec
`"\pPOM68K Logs"`. Repli : `StandardPutFile` interactif.

### 5.2 Écrire (write-through forcé)
`FSMakeFSSpec` → `FSpDelete` (écrase) → `FSpCreate(&spec,'ttxt','TEXT',smSystemScript)`
→ `FSpOpenDF(fsWrPerm)` → `FSWrite` → `FSClose` → **`FlushVol`** (pousse netatalk
vers le FS host). Nom : `Probe-<mach>-<time>.log` (horodaté via `GetDateTime`).

### 5.3 Format : **JSON Lines** (1 objet/ligne, LF `0x0A`)
```json
{"sec":"_meta","key":"header","val":"POM68K-Prober v1","mach":"0x0023","time":3801234567,"st":"INFO"}
{"sec":"ident","key":"cpu","val":"0x04","st":"INFO"}
{"sec":"net","key":"myZone","val":"POM68K-Zone","st":"INFO"}
{"sec":"probe","key":"VIA2@0x50F02000","val":"absent","st":"INFO"}
```
- Tout `val` est une chaîne → sérialisation triviale, le host caste.
- Échappement `JsonEscape` : `"`→`\"`, `\`→`\\`, contrôles `<0x20`→`\uXXXX`.
- `time` = secondes Mac (epoch 1904) ; le host convertit.

### 5.4 Miroir écran : **List Manager**
`LNew` (1 colonne, scrollbar V) ; `LAddRow`+`LSetCell` par finding ; préfixe
glyphe selon `status` (`•`INFO `✓`OK `!`WARN `✗`FAIL) ; `LUpdate`/`LClick`/
`LActivate` dans la boucle.

## 6. `main` — squelette
Init Toolbox ; menus **Apple** (À propos), **Fichier** (Enregistrer sur AFP ⌘S,
Quitter ⌘Q), **Test** (Tout lancer ⌘R, Identité, Réseau, Sondage bus-error) ;
boucle `WaitNextEvent` ; « Tout lancer » = `Report_Init` → `Ident_Collect` →
`Net_Collect` → rendu List → `Report_WriteAFP`.

## 7. Handler bus-error (`probe.s`)
Approche **frame-agnostique** : le handler ne fait PAS de RTE (qui ré-exécuterait
la faute selon le format de frame CPU). Il pose `gProbeFaulted`, bascule sur une
petite pile scratch, et appelle `longjmp(gProbeEnv, 1)` — qui restaure D2-D7/A2-A7/
PC/SR sauvés par `setjmp` (donc retour propre en mode user si le `setjmp` du
libc Retro68 sauve le SR ; à confirmer au 1er build). Le frame d'exception
abandonné sur la pile superviseur est inoffensif (prober éphémère).

Contrat :
```c
extern void ProbeInstall(void);   /* sauve+installe vecteurs $08/$0C     */
extern void ProbeRestore(void);   /* restaure                            */
extern jmp_buf          gProbeEnv;
extern volatile long    gProbeFaulted;
Boolean ProbeReadable(volatile unsigned long *a); /* setjmp autour de *a  */
```
Hypothèses : VBR = 0 (convention Mac classique — vecteurs à `$0`) ; low-mem
inscriptible en mode user (vrai sur Mac classique). À vérifier sur 040.

## 8. Contrepartie host (Phase 1)
netatalk `afp.conf` : partage `[POM68K Logs] path=/srv/pom68k/logs` (accès
invité pour le banc). Lancer `POM68K_LTOUDP=1 ./POM68K …`. Je lis
`/srv/pom68k/logs/Probe-*.log`, je parse le JSONL, et (Phase 4) je diffe contre
`golden/<machine>.expected`.

## 9. État de compilation / points à valider au 1er build
- Noms exacts des glues AppleTalk (`GetNodeAddress`, `PLookupName`, `NBPExtract`,
  `GetMyZone`, `XPPParamBlock`) dans le multiversal.
- Détails List Manager (`LNew` signature, `Cell`/`ListBounds`).
- `probe.s` : syntaxe GAS m68k, sauvegarde du SR par `setjmp`, VBR=0.
- FSSpec / `PBHGetVInfoSync` disponibles (System 7 — OK sur les images cibles).

---

## 8. Les deux couches — ajouté 2026-08-03

Le Prober sert **deux publics avec le même binaire**, et c'est délibéré :

1. le **propriétaire d'un vrai Macintosh**, qui veut savoir quelle machine il
   a et si quelque chose cloche dedans ;
2. la **boucle de conformité de POM68K**, qui veut des faits bruts à comparer
   à un golden côté host.

Ils sont servis par **deux couches, jamais par deux vérités** :

| | couche 1 — l'enregistrement | couche 2 — l'affichage |
|---|---|---|
| fichier | `report.c` → `POM68K Prober.txt`, à côté de l'app | jamais |
| écran | menu **Vue ▸ Données brutes** | menu **Vue ▸ Identification** (défaut) |
| contenu | `machineType = 0x5E` | `Machine : Macintosh Quadra 605` |
| rôle | constater | traduire |

**La couche 2 dérive de la couche 1 et ne s'y substitue jamais.** L'invariant
du § 0 (« l'app rapporte des valeurs brutes ; c'est le golden côté host qui
juge ») est donc intact : le golden lit le fichier, pas l'écran.

Pourquoi ça compte : **un instrument qui partage les préjugés de ce qu'il
mesure ne mesure plus rien.** Le jour où le golden lirait l'interprétation,
le Prober cesserait d'être un oracle pour devenir une seconde implémentation
de nos propres croyances.

> **Le guest constate, le host juge, la vraie machine arbitre.**

Deux conséquences concrètes :

- La table d'identification de `interp.c` vient de l'**énumération d'Apple**
  (Inside Macintosh / `Gestalt.h`, recoupée avec `tools/rominfo.cpp`
  `modelName()`), **pas** de la liste `kProfiles` de POM68K. Provenances
  différentes : si elles divergent un jour, cette divergence est une
  *information*, pas un bug à masquer.
- Les **anomalies** (`interp_anomalies`) sont des croisements entre sources
  indépendantes, destinés à l'humain devant la machine. Elles ne sont **pas**
  un verdict sur POM68K : côté conformité, c'est le golden qui juge, sur le
  fichier brut. Et quand rien n'est suspect, l'écran le **dit** — un affichage
  muet ne se distingue pas d'un test qui n'a pas tourné, leçon assez chère
  côté émulateur pour valoir aussi ici.

### Sortie fichier

`Report_WriteLocal()` écrit **à côté de l'application** (le répertoire par
défaut d'une app lancée est celui qui la contient), nom fixe
`POM68K Prober.txt`, une ligne `section⇥clé⇥valeur⇥statut` par constat.
Format délibérément pauvre : lisible par SimpleText sur la machine **et** par
le golden côté host sans parseur. C'est la sortie **principale** — elle ne
demande ni réseau ni volume monté, donc elle marche sur une vraie machine
sortie du grenier. `Report_WriteAFP()` reste la sortie secondaire pour la
boucle host, et son absence n'est plus une erreur (`R_WARN`, pas `R_FAIL`).
