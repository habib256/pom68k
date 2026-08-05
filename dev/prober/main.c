/*
 * main.c — POM68K Prober : fenetre + menus + List Manager, orchestration.
 * Voir dev/prober/SPEC.md §1 et §6.
 *
 * NB : chaines Toolbox en ASCII pur (le source est UTF-8, l'ecran Mac est
 * MacRoman — les accents seraient mal rendus).
 */
#include <Quickdraw.h>
#include <Windows.h>
#include <Menus.h>
#include <Fonts.h>
#include <Events.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <Lists.h>
#include <Devices.h>
#include <Sound.h>
#include <Memory.h>
#include <Gestalt.h>
#include <Traps.h>
#include <OSUtils.h>
#include "prober_compat.h"
#include <stdio.h>

#include "report.h"
#include "ident.h"
#include "net.h"
#include "interp.h"
#include "bench.h"
#include "gfx.h"
#include "power.h"
#include "ui.h"
#include "chart.h"
#include "devs.h"

#define mApple 128
#define mFile  129
#define mTest  130
#define mView  131

/* items Fichier */
#define iSaveFile 1
#define iCopy     2
#define iSaveAFP  3
#define iQuit     5
/* items Test */
#define iRunAll  1
#define iIdent   2
#define iNet     3
#define iBench   4
#define iGfx     5
#define iPower   6
#define iDevs    7
#define iTone    8
/* items Vue */
#define iInterp  1
#define iRaw     2
#define iChart   3

static const unsigned char kVolName[] = "\pPOM68K Logs";

/* Deux vues sur LA MEME collecte (voir interp.h) : l'interpretation pour
 * l'humain devant la machine, le brut pour la boucle de conformite. Le
 * FICHIER ne contient jamais que du brut, quelle que soit la vue active. */
typedef enum { VIEW_INTERP = 0, VIEW_RAW, VIEW_CHART } ViewMode;

static WindowPtr gWin;
static ListHandle gList;
static Report    gReport;
static Interp    gInterp;
static ViewMode  gView = VIEW_INTERP;   /* ce que l'utilisateur veut voir */
static short     gHeaderH = 40;        /* hauteur du bandeau (ui.c)      */
static Boolean   gDone = false;
static long      gMachineType = 0;
static short     gChartPage = 0;

/* ── construit la barre de menus a l'execution ── */
static void SetupMenus(void)
{
    MenuHandle m;

    m = NewMenu(mApple, "\p\024");                 /* 0x14 = pomme */
    AppendMenu(m, "\pA propos du Prober;-");
    AppendResMenu(m, 'DRVR');
    InsertMenu(m, 0);

    m = NewMenu(mFile, "\pFichier");
    AppendMenu(m, "\pEnregistrer le rapport/S;Copier dans le presse-papiers/C;Envoyer sur AFP;-;Quitter/Q");
    InsertMenu(m, 0);

    m = NewMenu(mTest, "\pTest");
    AppendMenu(m, "\pTout lancer/R;Identite;Reseau;Performances/P;Graphismes/G;Energie/E;Peripheriques/D;-;Tonalite de test/T");
    InsertMenu(m, 0);

    m = NewMenu(mView, "\pVue");
    AppendMenu(m, "\pIdentification/I;Donnees brutes/B;Graphiques/K");
    InsertMenu(m, 0);

    DrawMenuBar();
}

/* ── fenetre + liste ── */
/* La fenetre se dimensionne sur l'ecran TROUVE (ui.c) : 520 px de large
 * ecrits en dur ne tiennent pas sur une Macintosh Plus (512 x 342), et ce
 * logiciel doit etre presentable de la Plus au Quadra. */
static void MakeWindow(void)
{
    Rect view;
    ListBounds data;
    Point cSize;

    gWin = UI_MakeWindow(&gHeaderH);
    SetPort(gWin);

    view = gWin->portRect;
    view.top   += gHeaderH;                        /* sous le bandeau         */
    view.right -= 15;                              /* place pour la scrollbar */
    SetRect(&data, 0, 0, 1, 0);                    /* 1 colonne, 0 lignes     */
    SetPt(&cSize, 0, 0);                           /* taille auto             */
    gList = LNew(&view, &data, cSize, 0, gWin, true, false, false, true);
}

/* ── rendu ─────────────────────────────────────────────────────────────
 * Deux vues, une seule collecte. Les deux lisent gReport ; la vue
 * "Identification" passe par Interp_Build, qui DERIVE et ne fabrique rien. */

static char Glyph(RStatus st)
{
    return (st == R_OK)   ? '+'
         : (st == R_WARN) ? '!'
         : (st == R_FAIL) ? 'x' : '.';
}

static void PushLine(short row, const char *line, short n)
{
    Cell c;
    short r = LAddRow(1, row, gList);
    SetPt(&c, 0, r);
    LSetCell((Ptr)line, n, c, gList);
}

static void RenderReport(void)
{
    short i, rows, row = 0;
    char  line[160];

    rows = (**gList).dataBounds.bottom;
    if (rows > 0) LDelRow(rows, 0, gList);

    if (gView == VIEW_CHART) {
        /* La liste n'a pas sa place ici : on la vide pour qu'elle ne
         * reapparaisse pas sous le dessin au prochain LUpdate. */
        Rect body = gWin->portRect;
        body.top += gHeaderH;
        EraseRect(&body);
        ClipRect(&body);
        {
            Rect in = body;
            in.left += 8; in.right -= 8; in.top += 4; in.bottom -= 4;
            Chart_Draw(gWin, &in, &gReport, gChartPage);
        }
        ClipRect(&gWin->portRect);
        UI_DrawHeader(gWin, gHeaderH,
                      (MachineShape)Interp_MachineShape(gMachineType),
                      Interp_TitleFor(&gReport), Interp_SubtitleFor(&gReport));
        return;
    }

    if (gView == VIEW_INTERP) {
        Interp_Build(&gInterp, &gReport);
        for (i = 0; i < gInterp.count; i++) {
            IFinding *f = &gInterp.items[i];
            short n = (short)sprintf(line, "%c %-18s %s",
                                     Glyph(f->status), f->label, f->text);
            PushLine(row++, line, n);
        }
        /* Le pont entre les deux publics : dire ou sont les faits bruts. */
        {
            short n = (short)sprintf(line, "  %-18s %d faits bruts "
                                     "(menu Vue > Donnees brutes)",
                                     "Detail", gReport.count);
            PushLine(row++, line, n);
        }
    } else {
        for (i = 0; i < gReport.count; i++) {
            RFinding *f = &gReport.items[i];
            short n = (short)sprintf(line, "%c %-6s %-22s %s",
                                     Glyph(f->status), f->section,
                                     f->key, f->value);
            PushLine(row++, line, n);
        }
    }
    LUpdate(gWin->visRgn, gList);
    UI_DrawHeader(gWin, gHeaderH,
                  (MachineShape)Interp_MachineShape(gMachineType),
                  Interp_TitleFor(&gReport), Interp_SubtitleFor(&gReport));
}

/* ── actions ── */
/* Le pont entre bench.c (qui ignore tout de l'interface) et le bandeau.
 * Captureless : bench.c ne veut qu'un pointeur de fonction nu. */
static void ProgressHook(const char *what, short pct)
{
    UI_Progress(gWin, gHeaderH, what, pct);
}

static void RunSection(Boolean ident, Boolean net, Boolean bench,
                       Boolean gfx, Boolean allDepths, Boolean power,
                       Boolean devs)
{
    Report_Init(&gReport);
    if (ident) Ident_Collect(&gReport);
    Gestalt(gestaltMachineType, &gMachineType);   /* pour l'icone du bandeau */
    if (net)   Net_Collect(&gReport);
    if (bench) {
        /* Plusieurs secondes de calcul : on affiche AVANT de partir et on
         * met le sablier, sinon l'attente muette passe pour un gel. */
        Report_Add(&gReport, "bench", "status", "mesure en cours...", R_INFO);
        RenderReport();
        SetCursor(*GetCursor(watchCursor));
        Bench_SetProgress(ProgressHook);
        Bench_Collect(&gReport);
        Bench_SetProgress(NULL);
        UI_Progress(gWin, gHeaderH, NULL, 100);
        InitCursor();
    }
    if (power) Power_Collect(&gReport);
    if (devs)  Devs_Collect(&gReport);
    if (gfx) {
        /* Le banc graphique DESSINE dans la fenetre : la liste est effacee
         * pendant la mesure, c'est normal et on la reconstruit apres. */
        SetCursor(*GetCursor(watchCursor));
        Bench_SetProgress(ProgressHook);
        Gfx_Collect(&gReport, gWin, allDepths);
        Bench_SetProgress(NULL);
        InitCursor();
        InvalRect(&gWin->portRect);
    }
    RenderReport();
}

/* Sortie PRINCIPALE : un fichier texte a cote de l'application. Marche
 * sur une vraie machine sans reseau ni volume monte. */
static void DoSaveLocal(void)
{
    OSErr e = Report_WriteLocal(&gReport);
    if (e == noErr) Report_Add(&gReport, "report", "fileWrite",
                               "POM68K Prober.txt", R_OK);
    else            Report_AddDec(&gReport, "report", "fileWriteErr", e, R_FAIL);
    RenderReport();
    SysBeep(3);
}

/* Presse-papiers : ce qui permet de coller le rapport dans un courriel ou
 * un forum. Le BRUT, evidemment — c'est ce qu'on demande a quelqu'un
 * d'envoyer quand on l'aide a distance. */
static void DoCopyToClipboard(void)
{
    static char buf[REPORT_MAX * 90 + 256];
    long o = 0;
    short i;

    o += (long)sprintf(buf + o, "POM68K Prober\r");
    for (i = 0; i < gReport.count && o + 120 < (long)sizeof buf; i++) {
        RFinding *f = &gReport.items[i];
        /* Retour CHARIOT : la fin de ligne du Macintosh classique. */
        o += (long)sprintf(buf + o, "%s\t%s\t%s\r",
                           f->section, f->key, f->value);
    }
    ZeroScrap();
    if (PutScrap(o, 'TEXT', (Ptr)buf) == noErr)
        Report_Add(&gReport, "report", "clipboard", "ok", R_OK);
    else
        Report_Add(&gReport, "report", "clipboard", "echec", R_WARN);
    RenderReport();
    SysBeep(3);
}

/* Sortie SECONDAIRE : le meme brut sur le volume AFP, pour la boucle de
 * conformite host quand netatalk est en place. Silencieuse si absent. */
static void DoSaveAFP(void)
{
    OSErr e = Report_WriteAFP(&gReport, kVolName);
    if (e == noErr) Report_Add(&gReport, "report", "afpWrite", "ok", R_OK);
    else            Report_AddDec(&gReport, "report", "afpWriteErr", e, R_WARN);
    RenderReport();
}

/* ── menus ── */
static void DoMenu(long r)
{
    short menu = HiWord(r), item = LoWord(r);
    Str255 daName;

    switch (menu) {
    case mApple:
        if (item == 1)
            UI_About((MachineShape)Interp_MachineShape(gMachineType),
                     Interp_TitleFor(&gReport));
        else {
            MenuHandle am = GetMenuHandle(mApple);
            GetMenuItemText(am, item, daName);
            OpenDeskAcc(daName);
        }
        break;
    case mFile:
        if (item == iSaveFile) DoSaveLocal();
        else if (item == iCopy) DoCopyToClipboard();
        else if (item == iSaveAFP) DoSaveAFP();
        else if (item == iQuit) gDone = true;
        break;
    case mTest:
        if (item == iRunAll)      RunSection(true, true, true, true, true, true, true);
        else if (item == iIdent)  RunSection(true, false, false, false, false, false, false);
        else if (item == iNet)    RunSection(false, true, false, false, false, false, false);
        else if (item == iBench)  RunSection(false, false, true, false, false, false, false);
        else if (item == iGfx)    RunSection(false, false, false, true, true, false, false);
        else if (item == iPower)  RunSection(false, false, false, false, false, true, false);
        else if (item == iDevs)   RunSection(false, false, false, false, false, false, true);
        else if (item == iTone)   Devs_PlayTestTone();
        break;
    case mView:
        /* Change ce qui est AFFICHE, jamais ce qui est collecte ni ecrit. */
        if (item == iInterp)    { gView = VIEW_INTERP; RenderReport(); }
        else if (item == iRaw)  { gView = VIEW_RAW;    RenderReport(); }
        else if (item == iChart) {
            /* Re-choisir "Graphiques" fait defiler les pages : une
             * fenetre de Mac Plus ne tient pas tout d'un coup. */
            if (gView == VIEW_CHART) gChartPage = (short)((gChartPage + 1) % 2);
            else                     gChartPage = 0;
            gView = VIEW_CHART;
            RenderReport();
        }
        break;
    }
    HiliteMenu(0);
}

static void HandleEvent(EventRecord *ev)
{
    WindowPtr w;
    Point pt;

    switch (ev->what) {
    case mouseDown:
        switch (FindWindow(ev->where, &w)) {
        case inMenuBar:
            DoMenu(MenuSelect(ev->where));
            break;
        case inSysWindow:
            SystemClick(ev, w);
            break;
        case inDrag:
            DragWindow(w, ev->where, &qd.screenBits.bounds);
            break;
        case inContent:
            if (w == gWin) {
                pt = ev->where;
                SetPort(gWin);
                GlobalToLocal(&pt);
                if (gView == VIEW_CHART) {
                    gChartPage = (short)((gChartPage + 1) % 2);
                    RenderReport();
                } else {
                    Boolean dbl = LClick(pt, ev->modifiers, gList);
                    (void)dbl;
                }
            }
            break;
        case inGoAway:
            if (TrackGoAway(w, ev->where)) gDone = true;
            break;
        }
        break;

    case keyDown:
    case autoKey:
        if (ev->modifiers & cmdKey)
            DoMenu(MenuKey((char)(ev->message & charCodeMask)));
        break;

    case updateEvt:
        w = (WindowPtr)ev->message;
        BeginUpdate(w);
        if (w == gWin) {
            Rect body = gWin->portRect;
            body.top += gHeaderH;
            EraseRect(&body);
            if (gView == VIEW_CHART) {
                Rect in = body;
                in.left += 8; in.right -= 8; in.top += 4; in.bottom -= 4;
                Chart_Draw(gWin, &in, &gReport, gChartPage);
            } else
            LUpdate(gWin->visRgn, gList);
            UI_DrawHeader(gWin, gHeaderH,
                          (MachineShape)Interp_MachineShape(gMachineType),
                          Interp_TitleFor(&gReport),
                          Interp_SubtitleFor(&gReport));
        }
        EndUpdate(w);
        break;

    case activateEvt:
        if ((WindowPtr)ev->message == gWin)
            LActivate((ev->modifiers & activeFlag) != 0, gList);
        break;
    }
}

/* ── System 6 : WaitNextEvent n'existe pas partout ───────────────────────
 * Il arrive avec MultiFinder / 6.0.4. Sur un System 6 nu, l'appeler part
 * dans le vide — et ce logiciel doit tourner sur une Macintosh Plus.
 * Test de trap classique : une trap absente pointe sur _Unimplemented. */
static Boolean gHasWNE;

static Boolean TrapAvailable(short trap, TrapType tt)
{
    return (Boolean)(NGetTrapAddress(trap, tt) !=
                     NGetTrapAddress(_Unimplemented, kToolboxTrapType));
}

static Boolean NextEvent(EventRecord *ev)
{
    if (gHasWNE) return WaitNextEvent(everyEvent, ev, 20L, NULL);
    /* Repli : le couple d'origine. SystemTask fait vivre les accessoires
     * de bureau, que WaitNextEvent prend en charge tout seul. */
    SystemTask();
    return GetNextEvent(everyEvent, ev);
}

int main(void)
{
    EventRecord ev;

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();

    gHasWNE = TrapAvailable(_WaitNextEvent, kToolboxTrapType);

    SetupMenus();
    MakeWindow();

    /* Au lancement : identite + reseau, PAS le banc. Plusieurs secondes
     * d'attente muette a l'ouverture seraient prises pour un plantage ;
     * le banc est un choix explicite (Test > Performances). */
    RunSection(true, true, false, false, false, true, true);
    DoSaveLocal();               /* le fichier texte, a cote de l'app */
    DoSaveAFP();                 /* + le canal AFP s'il existe (sinon WARN) */

    while (!gDone) {
        if (NextEvent(&ev))
            HandleEvent(&ev);
    }

    if (gList) LDispose(gList);
    if (gWin)  DisposeWindow(gWin);
    return 0;
}
