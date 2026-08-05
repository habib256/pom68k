/*
 * mac_shell.c -- the Macintosh side: window, menus, event pump, blitting.
 * ---------------------------------------------------------------------------
 * Everything that is NOT the game lives here. The 6502 original owned the
 * machine -- SEI, poll KBDCR, write VRAM -- so the port needs exactly three
 * things from the Toolbox:
 *
 *   ShellWaitKey()   a blocking key read that still services the Finder,
 *                    which is what lets the game logic keep its original
 *                    straight-line shape instead of becoming a state machine
 *   ShellPresent()   composite the software VDP and CopyBits it to the window
 *   ColdStart()      the longjmp that stands in for the cartridge's JMP $4000
 *
 * The window is a fixed 256x192 -- the TMS9918's exact frame -- so it fits
 * on a Mac Plus's 512x342 screen with room to spare, and the artwork is
 * shown at 1:1 with no resampling.
 */

#include <Quickdraw.h>
#include <Windows.h>
#include <Fonts.h>
#include <Events.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <OSUtils.h>
#include <ToolUtils.h>
#include <Devices.h>
#include <Memory.h>
#include <Traps.h>
#include <Gestalt.h>
/* No <QDOffscreen.h>: Multiversal generates every API into Multiverse.h and
 * only ships per-name shims for a hardcoded list that QDOffscreen is not on.
 * NewGWorld and friends arrive through <Quickdraw.h> above. */
#include <setjmp.h>
#include <string.h>

#include "rogue.h"
#include "snd.h"
#include "vdp.h"

#define kMenuApple   128
#define kMenuFile    129
#define kMenuDisplay 130
#define kAboutItem   1
#define kQuitItem    1
#define kColorItem   1
#define kZoom1Item   3
#define kZoom2Item   4
#define kSoundItem   6
#define kMusicItem   7

/* Multiversal types Pascal strings as `const unsigned char *`, but a PSTR("\p...")
 * literal is a plain char array, so every Toolbox call site needs the cast. */
#define PSTR(s) ((ConstStringPtr)(s))

#define kArrowLeft  0x1C
#define kArrowRight 0x1D
#define kArrowUp    0x1E
#define kArrowDown  0x1F

static WindowPtr gWin;
static BitMap    gBits;
static MenuHandle gAppleMenu, gFileMenu, gDisplayMenu;
static Boolean   gHasWNE;
static Boolean   gAboutUp;
static jmp_buf   gCold;

/* --- Display mode ----------------------------------------------------------
 * gColorOk  the machine can do it: Color QuickDraw present, screen >= 4 bpp,
 *           and the 8-bit offscreen was actually created
 * gColorOn  the user wants it (menu toggle; defaults to gColorOk)
 * gZoom     1 or 2. The offscreen always stays 256x192 -- CopyBits does the
 *           stretch, which is an exact pixel double and costs nothing. */
static Boolean    gColorOk;
static Boolean    gColorOn;
static short      gZoom = 1;
static GWorldPtr  gOffscreen;
static CTabHandle gTmsCTab;

/* --------------------------------------------------------------------------
 * Cold start. death_screen and win_screen reached `JMP $4000` from deep
 * inside finish_turn, several JSRs down; a longjmp is the same thing said in
 * C, and it keeps every caller between here and there free of "did the game
 * just end?" plumbing.
 * -------------------------------------------------------------------------- */
void ColdStart(short reason)
{
    longjmp(gCold, reason);
}

unsigned long ShellTickCount(void)
{
    return (unsigned long)TickCount();
}

void ShellDelayTicks(short ticks)
{
    long final;
    Delay((long)ticks, &final);
}

void ShellDrainKeys(void)
{
    FlushEvents(keyDownMask | autoKeyMask, 0);
}

/* --- Idle animation ---------------------------------------------------------
 * The game blocks in ShellWaitKey, so the wait IS the animation loop: every
 * 15 ticks (4 Hz) the phase advances and, if the scene actually carries an
 * animated element, the frame is recomposited. The gate matters on a Mac
 * Plus -- a static scene costs nothing while a loot-strewn room shimmers.
 * A full cycle is 4 steps, so bob and flicker breathe at 1 Hz. */
#define kAnimPeriod 15
static unsigned long gAnimLast;

static void AnimIdle(void)
{
    unsigned long now = (unsigned long)TickCount();
    if (now - gAnimLast < kAnimPeriod)
        return;
    gAnimLast = now;
    gVdpPhase++;
    if (!gAboutUp && VdpAnimated())
        ShellPresent();
}

/* --- Colour detection -------------------------------------------------------
 * Two questions, and both have to answer yes. Is Color QuickDraw in the ROM
 * at all (a Mac Plus, SE or Classic says no), and is the screen the user is
 * actually looking at deeper than 1 bit (a Quadra driving a monochrome
 * monitor has CQD and nothing to show for it)? */
static Boolean ColorScreenAvailable(void)
{
    long v;
    GDHandle gd;

    if (Gestalt(gestaltQuickdrawVersion, &v) != noErr)
        return false;                       /* no Gestalt at all: pre-CQD */
    if (v < gestalt8BitQD)
        return false;

    gd = GetMainDevice();
    if (!gd || !(**gd).gdPMap)
        return false;
    return (**((**gd).gdPMap)).pixelSize >= 4;
}

/* A 16-entry table holding the TMS9918A palette, handed to NewGWorld so the
 * offscreen's pixel VALUES are palette indices -- exactly what the
 * compositor produces, so presenting is a straight row copy. */
static CTabHandle MakeTmsCTab(void)
{
    CTabHandle ct = (CTabHandle)NewHandleClear(
        (Size)(sizeof(ColorTable) + 15 * sizeof(ColorSpec)));
    short i;
    if (!ct)
        return NULL;
    (**ct).ctSeed  = GetCTSeed();
    (**ct).ctFlags = 0;
    (**ct).ctSize  = 15;                    /* SCSI-style: entries - 1 */
    for (i = 0; i < 16; i++) {
        unsigned char r = kTmsPaletteRGB[i][0];
        unsigned char g = kTmsPaletteRGB[i][1];
        unsigned char b = kTmsPaletteRGB[i][2];
        (**ct).ctTable[i].value = i;
        /* QuickDraw wants 16-bit components; replicating the byte keeps
         * white at full scale instead of half. */
        (**ct).ctTable[i].rgb.red   = (unsigned short)((r << 8) | r);
        (**ct).ctTable[i].rgb.green = (unsigned short)((g << 8) | g);
        (**ct).ctTable[i].rgb.blue  = (unsigned short)((b << 8) | b);
    }
    return ct;
}

static Boolean SetupOffscreen(void)
{
    Rect r;
    SetRect(&r, 0, 0, VDP_WIDTH, VDP_HEIGHT);
    gTmsCTab = MakeTmsCTab();
    if (!gTmsCTab)
        return false;
    if (NewGWorld(&gOffscreen, 8, &r, gTmsCTab, NULL, 0) != noErr || !gOffscreen) {
        gOffscreen = NULL;
        return false;
    }
    return true;
}

/* --- Presentation ---------------------------------------------------------- */

static void BlitColor(void)
{
    GrafPtr      port = (GrafPtr)gWin;
    PixMapHandle pm   = GetGWorldPixMap(gOffscreen);
    Ptr          base;
    short        rb, y;

    if (!pm || !LockPixels(pm))
        return;
    base = GetPixBaseAddr(pm);
    rb   = (short)((**pm).rowBytes & 0x3FFF);   /* high bits are PixMap flags */
    for (y = 0; y < VDP_HEIGHT; y++)
        memcpy(base + (long)y * rb, gPix + (long)y * VDP_WIDTH, VDP_WIDTH);

    SetPort(port);
    ForeColor(blackColor);
    BackColor(whiteColor);
    CopyBits((BitMap *)*pm, &port->portBits,
             &gBits.bounds, &port->portRect, srcCopy, NULL);
    UnlockPixels(pm);
}

static void BlitMono(void)
{
    GrafPtr port = (GrafPtr)gWin;
    VdpPackMono();
    SetPort(port);
    /* A colour port would otherwise tint a 1-bit source with whatever pen
     * colours happen to be current. */
    ForeColor(blackColor);
    BackColor(whiteColor);
    CopyBits(&gBits, &port->portBits,
             &gBits.bounds, &port->portRect, srcCopy, NULL);
}

/* The source rect is always 256x192 and the destination is the window, so
 * CopyBits performs the 2x stretch itself. At an exact integer ratio that is
 * pixel doubling -- no filtering, no seams, and no second buffer to keep. */
static void BlitFrame(void)
{
    if (gColorOn && gOffscreen)
        BlitColor();
    else
        BlitMono();
}

void ShellPresent(void)
{
    /* The hit flash is a colour on a colour screen and a punched-out
     * negative on a 1-bit one; only the compositor can draw the negative. */
    VdpComposite(!(gColorOn && gOffscreen));
    BlitFrame();
}

/* --- About card ------------------------------------------------------------
 * Drawn straight into the window with QuickDraw rather than through a DLOG,
 * so the application needs no resource fork beyond what Retro68 generates.
 * It is dismissed by the next key or click, handled inside the event pump --
 * no nested modal loop, no re-entrancy into ShellWaitKey. */
static void DrawAbout(void)
{
    GrafPtr port = (GrafPtr)gWin;
    Rect r;

    SetPort(port);
    ForeColor(blackColor);
    BackColor(whiteColor);
    r = port->portRect;
    EraseRect(&r);
    FrameRect(&r);

    TextFont(systemFont);
    TextFace(bold);
    TextSize(12);
    MoveTo(20, 40);
    DrawString(PSTR("\pROGUE"));

    TextFace(normal);
    TextSize(9);
    MoveTo(20, 62);
    DrawString(PSTR("\pA Macintosh 68k port of TMS_Rogue,"));
    MoveTo(20, 76);
    DrawString(PSTR("\pthe Apple-1 + TMS9918 roguelike."));
    MoveTo(20, 98);
    DrawString(PSTR("\pGame and artwork pipeline: VERHILLE Arnaud"));
    MoveTo(20, 112);
    DrawString(PSTR("\pTiles: Quale, SCROLL-O-SPRITES (CC-BY-3.0)"));
    MoveTo(20, 126);
    DrawString(PSTR("\pBoss: Hexany Ives, Monster Menagerie (CC0)"));
    MoveTo(20, 148);
    DrawString(PSTR("\pBuilt with Retro68."));
    MoveTo(20, 170);
    DrawString(PSTR("\pPress any key."));
}

/* --- Menus ------------------------------------------------------------------ */

static void SetUpMenus(void)
{
    gAppleMenu = NewMenu(kMenuApple, PSTR("\p\024"));     /* 0x14 = the apple glyph */
    AppendMenu(gAppleMenu, PSTR("\pAbout Rogue;(-"));
    AppendResMenu(gAppleMenu, 'DRVR');
    InsertMenu(gAppleMenu, 0);

    gFileMenu = NewMenu(kMenuFile, PSTR("\pFile"));
    AppendMenu(gFileMenu, PSTR("\pQuit/Q"));
    InsertMenu(gFileMenu, 0);

    gDisplayMenu = NewMenu(kMenuDisplay, PSTR("\pDisplay"));
    AppendMenu(gDisplayMenu,
               PSTR("\pColor;(-;Actual Size/1;Double Size/2;(-;Sound;Music"));
    if (!gColorOk)                      /* nothing to toggle on a 1-bit Mac */
        DisableItem(gDisplayMenu, kColorItem);
    if (!SfxAvailable())                /* no Sound Manager on this System */
        DisableItem(gDisplayMenu, kSoundItem);
    if (!MusicAvailable())              /* second channel did not open */
        DisableItem(gDisplayMenu, kMusicItem);
    InsertMenu(gDisplayMenu, 0);

    DrawMenuBar();
}

/* Resize the window around the fixed 256x192 source. Called on start-up and
 * whenever the Display menu changes the zoom. */
static void ApplyZoom(short zoom)
{
    if (zoom < 1) zoom = 1;
    if (zoom > 2) zoom = 2;
    gZoom = zoom;
    if (!gWin)
        return;
    SizeWindow(gWin, (short)(VDP_WIDTH * gZoom), (short)(VDP_HEIGHT * gZoom),
               true);
    InvalRect(&gWin->portRect);
    ShellPresent();
}

static void SyncDisplayMenu(void)
{
    CheckItem(gDisplayMenu, kColorItem, gColorOn && gColorOk);
    CheckItem(gDisplayMenu, kZoom1Item, gZoom == 1);
    CheckItem(gDisplayMenu, kZoom2Item, gZoom == 2);
    CheckItem(gDisplayMenu, kSoundItem, SfxEnabled());
    CheckItem(gDisplayMenu, kMusicItem, MusicEnabled());
}

static void DoMenu(long choice)
{
    short menu = HiWord(choice);
    short item = LoWord(choice);
    Str255 name;

    switch (menu) {
    case kMenuApple:
        if (item == kAboutItem) {
            gAboutUp = true;
            DrawAbout();
        } else {
            GetMenuItemText(gAppleMenu, item, name);
            OpenDeskAcc(name);
        }
        break;
    case kMenuFile:
        if (item == kQuitItem) {
            HiliteMenu(0);                  /* unhighlight before we longjmp */
            ColdStart(COLD_QUIT);           /* never returns */
        }
        break;
    case kMenuDisplay:
        if (item == kColorItem && gColorOk) {
            gColorOn = !gColorOn;
            SyncDisplayMenu();
            ShellPresent();
        } else if (item == kZoom1Item) {
            ApplyZoom(1);
            SyncDisplayMenu();
        } else if (item == kZoom2Item) {
            ApplyZoom(2);
            SyncDisplayMenu();
        } else if (item == kSoundItem && SfxAvailable()) {
            SfxSetEnabled(!SfxEnabled());
            SyncDisplayMenu();
        } else if (item == kMusicItem && MusicAvailable()) {
            MusicSetEnabled(!MusicEnabled());
            SyncDisplayMenu();
        }
        break;
    default:
        break;
    }
    HiliteMenu(0);
}

/* --- Event pump -------------------------------------------------------------
 * Returns an upper-case ASCII character. Arrow keys fold onto the same IJKL
 * codes the 6502 build bound at its title screen, so the whole game only
 * ever sees the four original direction codes. */
static char TranslateKey(EventRecord *ev)
{
    char c = (char)(ev->message & charCodeMask);

    switch ((unsigned char)c) {
    case kArrowUp:    return KEY_NORTH;
    case kArrowDown:  return KEY_SOUTH;
    case kArrowLeft:  return KEY_WEST;
    case kArrowRight: return KEY_EAST;
    default: break;
    }
    if (c >= 'a' && c <= 'z')
        c = (char)(c - 'a' + 'A');
    return c;
}

char ShellWaitKey(void)
{
    EventRecord ev;

    for (;;) {
        Boolean got;

        /* The music refill and the idle animation ride the event pump:
         * every wait for a key is also a chance to keep the loop's queue
         * topped up and the torches flickering. */
        MusicIdle();
        AnimIdle();

        if (gHasWNE)
            got = WaitNextEvent(everyEvent, &ev, 15L, NULL);
        else {
            SystemTask();
            got = GetNextEvent(everyEvent, &ev);
        }
        if (!got)
            continue;

        switch (ev.what) {
        case keyDown:
        case autoKey:
            if (ev.modifiers & cmdKey) {
                long choice = MenuKey((short)(ev.message & charCodeMask));
                if (HiWord(choice) != 0) {
                    DoMenu(choice);
                    break;
                }
            }
            if (gAboutUp) {
                gAboutUp = false;
                ShellPresent();
                break;                      /* the key only dismissed the card */
            }
            return TranslateKey(&ev);

        case mouseDown: {
            WindowPtr which;
            short part = FindWindow(ev.where, &which);
            switch (part) {
            case inMenuBar:
                DoMenu(MenuSelect(ev.where));
                break;
            case inSysWindow:
                SystemClick(&ev, which);
                break;
            case inDrag: {
                Rect bounds = qd.screenBits.bounds;
                InsetRect(&bounds, 4, 4);
                DragWindow(which, ev.where, &bounds);
                break;
            }
            case inGoAway:
                if (TrackGoAway(which, ev.where))
                    ColdStart(COLD_QUIT);   /* never returns */
                break;
            case inContent:
                if (which != FrontWindow())
                    SelectWindow(which);
                else if (gAboutUp) {
                    gAboutUp = false;
                    ShellPresent();
                }
                break;
            default:
                break;
            }
            break;
        }

        case updateEvt:
            if ((WindowPtr)ev.message == gWin) {
                BeginUpdate(gWin);
                if (gAboutUp)
                    DrawAbout();
                else
                    BlitFrame();
                EndUpdate(gWin);
            }
            break;

        default:
            break;
        }
    }
}

/* --- Start-up ---------------------------------------------------------------- */

static void InitToolbox(void)
{
    Rect r;
    short left, top;

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();
    FlushEvents(everyEvent, 0);

    /* WaitNextEvent is a MultiFinder-era trap; on a System 4/5 Plus it is not
     * there and the classic GetNextEvent + SystemTask pair is. Comparing its
     * address against _Unimplemented is the standard probe. (Multiversal
     * spells the tool-trap accessor GetToolTrapAddress and has no ToolTrap
     * constant, so this is the inline-trap form rather than
     * NGetTrapAddress.) */
    gHasWNE = (GetToolTrapAddress(_WaitNextEvent)
               != GetToolTrapAddress(_Unimplemented));

    /* Colour first: it decides which kind of window to make. A basic
     * GrafPort window on a colour Mac would render the CLUT source in
     * black and white, so the port has to be a CGrafPort. */
    gColorOk = ColorScreenAvailable() && SetupOffscreen();
    gColorOn = gColorOk;

    SetUpMenus();

    /* Double size when the screen can hold it with room for the menu bar and
     * the title bar. A Mac Plus is 512x342, so 512x384 does not fit and it
     * stays at 1:1 -- which is also the only size the artwork was drawn for. */
    gZoom = 1;
    if (qd.screenBits.bounds.right  >= VDP_WIDTH  * 2 + 8 &&
        qd.screenBits.bounds.bottom >= VDP_HEIGHT * 2 + 60)
        gZoom = 2;

    left = (short)((qd.screenBits.bounds.right - VDP_WIDTH * gZoom) / 2);
    top  = (short)((qd.screenBits.bounds.bottom - VDP_HEIGHT * gZoom) / 2);
    if (top < 40)
        top = 40;                   /* clear of the menu bar and title bar */
    if (left < 4)
        left = 4;
    SetRect(&r, left, top,
            (short)(left + VDP_WIDTH * gZoom),
            (short)(top + VDP_HEIGHT * gZoom));

    if (gColorOk)
        gWin = NewCWindow(NULL, &r, PSTR("\pRogue"), true, noGrowDocProc,
                          (WindowPtr)-1L, true, 0);
    else
        gWin = NewWindow(NULL, &r, PSTR("\pRogue"), true, noGrowDocProc,
                         (WindowPtr)-1L, true, 0);
    SetPort((GrafPtr)gWin);

    /* The 1-bit packing of gPix; this BitMap is the only thing standing
     * between it and CopyBits on a monochrome screen. It also supplies the
     * 256x192 source rectangle both blit paths hand to CopyBits. */
    gBits.baseAddr = (Ptr)gFrame;
    gBits.rowBytes = VDP_ROWBYTES;
    SetRect(&gBits.bounds, 0, 0, VDP_WIDTH, VDP_HEIGHT);

    SyncDisplayMenu();
}

int main(void)
{
    /* Before InitToolbox: SetUpMenus (inside it) reads SfxAvailable() to
     * enable the Sound item, and the Sound Manager needs no QuickDraw. */
    SfxInit();
    MusicInit();                /* second channel; effects keep priority */
    InitToolbox();

    for (;;) {
        short reason = (short)setjmp(gCold);
        if (reason == COLD_QUIT)
            break;
        /* RogueRun never returns normally: it ends via ColdStart, either
         * COLD_RESTART from a death or a win, or COLD_QUIT from the menu. */
        RogueRun();
    }
    MusicShutdown();
    SfxShutdown();
    return 0;
}
