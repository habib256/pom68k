/*
 * Bonjour de Pomme One
 * -----------------------------------------------------------------------------
 * Petit programme Macintosh 68k classique : ouvre une fenetre QuickDraw et y
 * dessine « Bonjour de Pomme One ». Compile avec la toolchain Retro68
 * (m68k-apple-macos). Cliquez dans la fenetre (ou tapez une touche) pour quitter.
 *
 * References Toolbox : Inside Macintosh: Macintosh Toolbox Essentials
 *   InitGraf/InitWindows/NewWindow/DrawString + boucle WaitNextEvent.
 */

#include <Quickdraw.h>
#include <Windows.h>
#include <Fonts.h>
#include <Events.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>

/* Chaine Pascal : le premier octet est la longueur, gere par le prefixe "\p". */
static const unsigned char kGreeting[] = "\pBonjour de Pomme One";

static void DrawContent(WindowPtr win)
{
    SetPort(win);
    EraseRect(&win->portRect);

    /* Titre en gras, centre approximativement. */
    TextFont(systemFont);
    TextFace(bold);
    TextSize(12);
    MoveTo(70, 70);
    DrawString(kGreeting);

    /* Sous-titre : d'ou vient ce salut. */
    TextFace(normal);
    TextSize(9);
    MoveTo(70, 95);
    DrawString("\pEmule sur POM68K -- Retro68");

    MoveTo(70, 130);
    DrawString("\pCliquez pour quitter.");
}

int main(void)
{
    WindowPtr   win;
    Rect        bounds;
    EventRecord event;
    Boolean     done = false;

    /* Sequence d'initialisation classique du Toolbox. */
    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();

    /* Fenetre document a peu pres centree sur un ecran 512x342. */
    SetRect(&bounds, 90, 80, 420, 260);
    win = NewWindow(NULL, &bounds, "\pBonjour", true, documentProc,
                    (WindowPtr)-1L, true, 0);
    if (win == NULL)
        return 1;

    SetPort(win);

    while (!done) {
        if (WaitNextEvent(everyEvent, &event, 30L, NULL)) {
            switch (event.what) {
            case updateEvt:
                BeginUpdate((WindowPtr)event.message);
                DrawContent((WindowPtr)event.message);
                EndUpdate((WindowPtr)event.message);
                break;

            case mouseDown:
            case keyDown:
                done = true;
                break;

            default:
                break;
            }
        }
    }

    DisposeWindow(win);
    return 0;
}
