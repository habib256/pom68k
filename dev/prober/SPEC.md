# POM68K Prober — spécification

Outil Macintosh 68k (compilé avec Retro68) qui teste **POM68K de l'intérieur** :
il collecte l'identité machine/ROM/chips, l'état AppleTalk, des mesures de
performance et l'inventaire des périphériques, et **dépose son rapport brut à
côté de l'application** — plus une copie JSON Lines sur le volume AFP monté
quand netatalk est en place. Le rapport est relu côté host (boucle de
conformité).

> Angle double : ce que le *guest déclare* (Gestalt, NBP) doit correspondre à ce
> que le *host observe* (netatalk, pcap). Tout écart = un point où POM68K n'est
> pas exact.

## 0. Périmètre

- Aucune modification de l'émulateur (au plus `POM68K_LTOUDP=1` + netatalk pour
  le canal AFP).
- L'app **rapporte des valeurs brutes** ; c'est le golden côté host qui *juge*.
  Donc pas de table de constantes machine à maintenir dans le guest. Le
  corollaire d'architecture — deux couches, jamais deux vérités — est au § 10,
  et c'est la règle la moins négociable du programme.

## 1. Arborescence & build

`CMakeLists.txt` déclare `project(POM68KProber C ASM)` (probe.s) et un seul
`add_application(POM68KProber ...)` ; `compat/` est ajouté aux includes.
Ressources (menus/fenêtre) créées à l'exécution → pas de `.r`.

| Fichier | Rôle | Sections émises |
|---|---|---|
| `main.c` | fenêtre, menus, boucle d'événements, orchestration, les trois vues | `report` |
| `report.h/.c` | modèle `Report` + sortie TSV locale (principale) + sortie AFP JSONL | — |
| `ident.h/.c` | Gestalt, ROM/low-mem, sondage bus-error, horloge | `ident`, `pram`, `clock`, `probe` |
| `net.h/.c` | AppleTalk : `.MPP`/`.XPP`, node/net, zone, lookups NBP, MacIP | `net` |
| `probe.s` | handler bus/address-error (68k asm) + `longjmp` | — |
| `bench.h/.c` | noyaux CPU/FPU chronométrés au tic, contre-vérification des horloges | `bench`, `clock` |
| `gfx.h/.c` | inventaire vidéo + banc QuickDraw écran vs GWorld | `video`, `gfx` |
| `power.h/.c` | Power Manager (batterie, temporisations, vitesse CPU) | `power` |
| `devs.h/.c` | ADB, file des lecteurs, volumes montés, capacités son, slots | `adb`, `drive`, `volume`, `sound`, `slot` |
| `interp.h/.c` | **couche 2** — traduction lisible + anomalies (écran seulement) | — |
| `ui.h/.c` | fenêtre dimensionnée sur l'écran trouvé, bandeau, icônes, jauge | — |
| `chart.h/.c` | troisième vue : barres normalisées par groupe, valeur écrite à côté | — |
| `compat/` | glues absentes du multiversal : `Lists.h`, AppleTalk, traps Power Manager / `Microseconds` / `ReadXPRam` | — |

`compat/prober_compat.h` porte trois interrupteurs, chacun avec sa
justification vérifiée : `PROBER_HAVE_POWER 1`, `PROBER_HAVE_MICROSECONDS 1`,
`PROBER_HAVE_XPRAM 0` (`_ReadXPRam` n'a **aucun** prototype ni glue dans les
Universal Interfaces 3.4 — seulement un numéro de trap dans `Traps.h` ; l'écrire
resterait un pari). Tant qu'il vaut 0, la section `pram` du § 3.4 n'est pas
compilée.

## 2. Modèle de données (`report.h`)

```c
typedef enum { R_INFO, R_OK, R_WARN, R_FAIL } RStatus;

typedef struct {
    char    section[16];   /* "ident" | "net" | "probe" | "bench" | ...   */
    char    key[40];       /* "cpu", "myZone", "VIA2@II", ...             */
    char    value[80];     /* valeur textuelle (hex "0x04", déc "128", …) */
    RStatus status;        /* le guest émet surtout R_INFO                */
} RFinding;

typedef struct { RFinding items[256]; short count; } Report;   /* REPORT_MAX */
```

Le guest émet des faits (`R_INFO`) ; `R_OK/R_FAIL` seulement pour les
auto-tests internes et l'écriture des fichiers. Le verdict OK/FAIL par machine
se fait côté host (golden). Les champs sont **tronqués silencieusement** au
dépassement, et un rapport plein (256 constats) cesse d'en accepter :
`Report_Add` sort sans rien signaler.

## 3. `ident` — identité

### 3.1 Palier 0 : Gestalt
Sélecteurs collectés (valeur brute, non interprétée dans le guest) :
`gestaltMachineType`, `gestaltSystemVersion`, `gestaltROMVersion`,
`gestaltROMSize`, `gestaltProcessorType`, `gestaltFPUType`, `gestaltMMUType`,
`gestaltHardwareAttr`, `gestaltADBVersion`, `gestaltAppleTalkVersion`,
`gestaltMacTCPVersion`, `gestaltQuickdrawVersion`. Un sélecteur inconnu fait
échouer `Gestalt` → la ligne est simplement omise. `adbv`/`mtcp` sont définis
localement (absents du multiversal), le reste vient de `Gestalt.h`.

### 3.2 Palier 1 : ROM + low-mem
Collecté **en plus**, pas seulement en repli : il lit la ROM que POM68K mappe,
indépendamment du System. `LMGetROMBase()` ($2AE) ; checksum long à ROMBase+0 ;
mot d'ID à ROMBase+8 (hi = famille, lo = révision) ; `$028E` (ROM85) ;
`HWCfgFlags` ($B22) ; `LMGetMemTop()`.

### 3.3 Palier 2 : sondage bus-error
`ident.c` installe le handler (`probe.s`), sonde un **surensemble** d'adresses,
émet `present@0x…`/`absent@0x…` par site, restaure les vecteurs. Le golden sait
lesquelles doivent répondre par machine. **C'est le cœur du test** : POM68K doit
répondre aux adresses présentes et bus-errorer aux absentes, comme la vraie
machine.

Les 17 sites codés dans `kSites` (recoupés avec les `read8`/`write8` de
`MacMemory.cpp` / `V8Memory.cpp` / `MacIIMemory.cpp` / `Q605Memory.cpp`) :

| Puce | Plus (24-bit) | V8 (LC/LCII/CII/CC) | Mac II | Q605 |
|---|---|---|---|---|
| VIA1 | `$EFE1FE` | `$F00000` | `$50F00000` | — |
| pseudo-VIA2 | — | `$F26000` | `$50F02000` | — |
| SCC (r) | `$9FFFF8` | `$F04000` | `$50F04000` | — |
| SCSI | `$580000` | `$F10000` | `$50F10000` | — |
| ASC | — | `$F14000` | `$50F14000` | — |
| IWM/SWIM | `$DFE1FF` | `$F16000` | `$50F16000` | — |
| DAFB | — | — | — | `$50F40000` |

Le Q605 partage les bases `$50Fxxxxx` du Mac II ; seul son DAFB a un site
propre. NuBus (`$Fs000000`, s=9..E) n'est pas sondé.

> **Attention** : lire un registre de périphérique a des effets de bord (une
> lecture de VIA/SCC peut acquitter un flag d'interruption). Toléré pour un
> prober ; commenté dans le code. Ne rien appeler du Toolbox entre install et
> restore (la fenêtre de sondage court-circuite le handler bus-error de l'OS).

### 3.4 Palier 3 : PRAM (éteint) + horloge
La XPRAM porte la configuration que POM68K **amorce lui-même** au reset
(`Rtc::factoryDefaults` / `Egret`), politique documentée que rien ne vérifiait
vu du guest : offsets `$00-$0F`, `$13` (SPConfig), `$58` (vidéo sPRAM),
`$8A-$8B` (marqueur de validité). Le code existe mais est derrière
`PROBER_HAVE_XPRAM`, à 0 (§ 1) — **la section `pram` n'est donc pas émise
aujourd'hui**. L'horloge, elle, est toujours collectée (`clock.macSeconds`,
`clock.dateTime`) : une pile morte ou un RTC arrêté ne se voit nulle part
ailleurs.

## 4. `net` — état AppleTalk

Séquence (`.MPP` = DDP/NBP, `.XPP` = zones), tout s'arrête si `.MPP` ne s'ouvre
pas (`mppOpen = down`, `R_WARN`) :
1. `OpenDriver("\p.MPP", &mppRef)` → up/down.
2. `GetNodeAddress(&node, &net)`.
3. `.XPP` : `zipGetMyZone` via `GetMyZone` → zone locale (bloc `PROBER_ZONES`,
   désactivable sans casser le reste). `GetZoneList` n'est pas appelé.
4. Lookups **NBP** `=:<type>@*` pour `AFPServer`, `LaserWriter`, `Workstation` :
   `NBPSetEntity`, `PLookupName`, puis `NBPExtract` de chaque tuple →
   `objet:type@zone net.node.socket` (+ un `<type>.count`).
5. MacIP : `OpenDriver("\p.IPP")` → `present`/`absent` (la version MacTCP arrive
   par Gestalt au § 3.1).

Les glues et les noms de champs viennent de `compat/AppleTalk.h` ; repli bas
niveau via `Control(.MPP/.XPP)` avec le `csCode` documenté
(`docs/APPLETALK.md` §3.3/§4.4) si une glue manque.

## 5. `report` — sorties

### 5.1 Sortie PRINCIPALE : un fichier texte à côté de l'app
`Report_WriteLocal()` : `Create`/`FSOpen` par nom simple sur le volume et le
dossier par défaut (celui de l'application lancée), `SetEOF(0)` pour écraser le
run précédent, `FSWrite`, `FlushVol`. Nom fixe **`POM68K Prober.txt`**.
Format délibérément pauvre — un en-tête `POM68K-Prober v1⇥time=…⇥findings=…`,
deux lignes de commentaire `#`, puis une ligne `section⇥clé⇥valeur⇥statut` par
constat. Lisible par SimpleText sur la machine **et** par le golden sans
parseur. Elle ne demande ni réseau ni volume monté : elle marche sur une vraie
machine sortie du grenier.

### 5.2 Sortie SECONDAIRE : le volume AFP
`Report_WriteAFP()` localise le volume par son nom (`PBHGetVInfoSync` sur
`ioVolIndex = 1..n`, `EqualString` avec `"\pPOM68K Logs"` ; `fnfErr` si absent),
puis `FSMakeFSSpec` → `FSpDelete` → `FSpCreate('ttxt','TEXT')` → `FSpOpenDF` →
`FSWrite` → `FSClose` → **`FlushVol`** (pousse netatalk vers le FS host). Nom :
`Probe-<secondes-Mac>.log`. Son absence n'est **pas** une erreur (`R_WARN`).

### 5.3 Format AFP : **JSON Lines** (1 objet/ligne, LF `0x0A`)
```json
{"sec":"_meta","key":"header","val":"POM68K-Prober v1","time":3801234567,"st":"INFO"}
{"sec":"ident","key":"cpu","val":"0x00000004","st":"INFO"}
{"sec":"net","key":"myZone","val":"POM68K-Zone","st":"INFO"}
{"sec":"probe","key":"VIA2@II","val":"absent@0x50F02000","st":"INFO"}
```
- Tout `val` est une chaîne → sérialisation triviale, le host caste.
- Échappement : `"`→`\"`, `\`→`\\`, contrôles `<0x20`→`\uXXXX`.
- `time` = secondes Mac (epoch 1904) ; le host convertit. Le modèle de machine
  n'est pas répété dans `_meta` — il est dans `ident.machineType`.
- Un garde-fou coupe la sérialisation avant de déborder le tampon statique
  (`REPORT_MAX * 200 + 512` octets) : un rapport tronqué s'arrête sur une ligne
  entière.

### 5.4 Miroir écran : **List Manager**
`LNew` (1 colonne, scrollbar V, sous le bandeau de `ui.c`) ; `LAddRow` +
`LSetCell` par ligne ; préfixe glyphe **ASCII pur** — `.` INFO, `+` OK,
`!` WARN, `x` FAIL (le source est UTF-8, l'écran Mac est MacRoman : tout
caractère accentué ou symbolique s'y afficherait faux) ;
`LUpdate`/`LClick`/`LActivate` dans la boucle.

## 6. `main` — squelette

Init Toolbox ; détection de `WaitNextEvent` par test de trap (une trap absente
pointe sur `_Unimplemented`) avec repli `SystemTask` + `GetNextEvent`, parce que
ce logiciel doit tourner sur une Macintosh Plus sous System 6 nu.

Menus construits à l'exécution : **Pomme** (À propos), **Fichier**
(Enregistrer le rapport ⌘S, Copier dans le presse-papiers ⌘C, Envoyer sur AFP,
Quitter ⌘Q), **Test** (Tout lancer ⌘R, Identité, Réseau, Performances ⌘P,
Graphismes ⌘G, Énergie ⌘E, Périphériques ⌘D, Tonalité de test ⌘T), **Vue**
(Identification ⌘I, Données brutes ⌘B, Graphiques ⌘K — re-choisir « Graphiques »
fait défiler les pages, une fenêtre de Mac Plus ne tient pas tout d'un coup).

Au lancement : identité + réseau + énergie + périphériques, **pas** les bancs
(plusieurs secondes d'attente muette à l'ouverture passeraient pour un
plantage), puis écriture locale, puis AFP. Les bancs sont un choix explicite,
sablier et jauge compris (`UI_Progress` dans le bandeau, pas de fenêtre modale).

## 7. Handler bus-error (`probe.s`)

Approche **frame-agnostique** : le handler ne fait PAS de RTE (qui
ré-exécuterait la faute selon le format de frame CPU). Il pose `gProbeFaulted`,
bascule sur une pile scratch privée, et appelle `longjmp(gProbeEnv, 1)` — qui
restaure D2-D7/A2-A7/PC/SR sauvés par `setjmp`. Le frame d'exception abandonné
sur la pile superviseur est inoffensif : le prober est éphémère et
`ProbeRestore` rétablit les vecteurs juste après.

Contrat (l'asm référence en `extern` ce que `ident.c` définit) :
```c
extern void ProbeInstall(void);   /* asm : sauve+installe vecteurs $08/$0C */
extern void ProbeRestore(void);   /* asm : restaure                        */
jmp_buf       gProbeEnv;          /* C, rempli par setjmp                  */
volatile long gProbeFaulted;      /* C, posé à 1 par le handler            */
static Boolean ProbeReadable(volatile unsigned long *a);  /* C, ident.c    */
```
Hypothèses : VBR = 0 (convention Mac classique — vecteurs à `$0`) ; low-mem
inscriptible en mode user (vrai sur Mac classique). **À revérifier sur 040.**

## 8. Contrepartie host

netatalk `afp.conf` : partage `[POM68K Logs] path=/srv/pom68k/logs` (accès
invité pour le banc) — ou le bridge tout fait de `tools/netatalk2/`. Lancer
`POM68K_LTOUDP=1 ./POM68K …`, lire `/srv/pom68k/logs/Probe-*.log`, parser le
JSONL, differ contre `golden/<machine>.expected`. Le golden lui-même n'existe
pas encore.

## 9. Points encore ouverts

- `PROBER_HAVE_XPRAM` (§ 3.4) : la seule section spécifiée mais non émise.
- Sondage bus-error sur 68040 : l'écriture des vecteurs en `$08/$0C` depuis le
  mode user reste à confirmer (§ 7).
- Le golden host (§ 8) et la comparaison par machine.
- Le sondage n'a **pas** d'entrée de menu propre : il part avec « Identité ».

---

## 10. Les deux couches — ajouté 2026-08-03

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
juge ») est donc intact : le golden lit le fichier, pas l'écran. La troisième
vue (`chart.c`) obéit à la même règle — elle ne lit que le rapport brut, et
comme ses barres sont normalisées **par groupe**, la valeur chiffrée est
toujours écrite à côté de la barre.

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
- Les **anomalies** (`interp_anomalies`, `interp.c`) sont des croisements entre sources
  indépendantes, destinés à l'humain devant la machine. Elles ne sont **pas**
  un verdict sur POM68K : côté conformité, c'est le golden qui juge, sur le
  fichier brut. Et quand rien n'est suspect, l'écran le **dit** — un affichage
  muet ne se distingue pas d'un test qui n'a pas tourné, leçon assez chère
  côté émulateur pour valoir aussi ici.

Le même principe gouverne les bancs (`bench.h`) : le fichier porte les
**grandeurs primitives** — itérations et tics — jamais un score calculé, pour
que le host recalcule comme il veut et qu'un changement de formule n'invalide
pas les fichiers déjà collectés. Et l'horloge utilisée, `TickCount()`, est
justement une des choses que l'émulateur fabrique : on ne mesure pas la vitesse
de l'hôte, on mesure le **travail par tic vu de l'intérieur du guest** — la
grandeur qui doit coïncider entre une vraie machine et son émulation.
