/*
 * ui.c — presentation. Voir ui.h pour la contrainte Plus -> Quadra.
 *
 * NB : chaines en ASCII pur (source UTF-8, ecran MacRoman).
 */
#include "ui.h"

#include <Fonts.h>
#include <Gestalt.h>
#include <Dialogs.h>
#include <TextUtils.h>
#include <string.h>
#include <stdio.h>

static short gColorKnown = 0, gColor = 0;

Boolean UI_HasColor(void)
{
    long v;
    if (!gColorKnown) {
        gColor = (Gestalt(gestaltQuickdrawVersion, &v) == noErr &&
                  v >= gestalt8BitQD);
        gColorKnown = 1;
    }
    return gColor ? true : false;
}

/* ── couleur, avec repli trame ───────────────────────────────────────────
 * En 8 bits on pose une vraie couleur ; en 1 bit on choisit une TRAME.
 * Ce n'est pas un pis-aller : les trames etaient le vocabulaire graphique
 * de ces machines, et un aplat 50 % lit mieux qu'un gris invente. */
static void SetInk(short r, short g, short b, const Pattern *bwPat)
{
    if (UI_HasColor()) {
        RGBColor c;
        c.red = r; c.green = g; c.blue = b;
        RGBForeColor(&c);
        PenPat(&qd.black);
    } else {
        ForeColor(blackColor);
        PenPat(bwPat);
    }
}

static void InkBlack(void)  { SetInk(0x0000,0x0000,0x0000, &qd.black); }
static void InkShade(void)  { SetInk(0x7000,0x7000,0x8000, &qd.gray);  }
static void InkLight(void)  { SetInk(0xBB00,0xBB00,0xC800, &qd.ltGray);}

/* ── icones dessinees ────────────────────────────────────────────────────
 * Quatre silhouettes, tracees en primitives : nettes en 1 bit comme en
 * 8 bits, et rien a stocker. Chacune tient dans le rectangle fourni. */

static void icon_compact(const Rect *b)
{
    Rect body = *b, scr, slot;
    short w = b->right - b->left, h = b->bottom - b->top;

    body.bottom = (short)(b->top + h * 3 / 4);
    InkLight();  PaintRoundRect(&body, 6, 6);
    InkBlack();  FrameRoundRect(&body, 6, 6);

    SetRect(&scr, (short)(body.left + w/6),  (short)(body.top + h/8),
                  (short)(body.right - w/6), (short)(body.top + h/2));
    InkBlack();  PaintRect(&scr);

    SetRect(&slot, (short)(body.left + w/4),  (short)(body.bottom - h/8),
                   (short)(body.right - w/4), (short)(body.bottom - h/12));
    InkShade();  PaintRect(&slot);

    /* le socle */
    SetRect(&body, (short)(b->left + w/8), (short)(b->top + h*3/4),
                   (short)(b->right - w/8), (short)(b->bottom - 1));
    InkLight();  PaintRect(&body);
    InkBlack();  FrameRect(&body);
}

static void icon_desktop(const Rect *b)
{
    Rect box = *b, slot;
    short w = b->right - b->left, h = b->bottom - b->top;

    box.top = (short)(b->top + h / 3);
    InkLight();  PaintRect(&box);
    InkBlack();  FrameRect(&box);

    SetRect(&slot, (short)(box.left + w/8), (short)(box.top + h/8),
                   (short)(box.left + w/2), (short)(box.top + h/5));
    InkShade();  PaintRect(&slot);
    /* le moniteur pose dessus */
    SetRect(&slot, (short)(b->left + w/4), b->top,
                   (short)(b->right - w/4), (short)(b->top + h/3));
    InkBlack();  PaintRect(&slot);
}

static void icon_tower(const Rect *b)
{
    Rect box = *b, slot;
    short w = b->right - b->left, h = b->bottom - b->top;
    short i;

    box.left = (short)(b->left + w / 4);
    InkLight();  PaintRect(&box);
    InkBlack();  FrameRect(&box);
    for (i = 0; i < 3; i++) {
        SetRect(&slot, (short)(box.left + w/10),
                       (short)(box.top + h/8 + i * h/6),
                       (short)(box.right - w/10),
                       (short)(box.top + h/8 + i * h/6 + h/12));
        InkShade();  PaintRect(&slot);
    }
}

static void icon_portable(const Rect *b)
{
    Rect lid, base;
    short w = b->right - b->left, h = b->bottom - b->top;

    SetRect(&lid, (short)(b->left + w/8), b->top,
                  (short)(b->right - w/8), (short)(b->top + h/2));
    InkBlack();  PaintRect(&lid);

    SetRect(&base, b->left, (short)(b->top + h/2),
                   b->right, (short)(b->bottom - h/6));
    InkLight();  PaintRect(&base);
    InkBlack();  FrameRect(&base);
}

void UI_DrawMachineIcon(const Rect *box, MachineShape shape)
{
    PenState ps;
    GetPenState(&ps);
    PenNormal();
    switch (shape) {
    case SHAPE_COMPACT:  icon_compact(box);  break;
    case SHAPE_DESKTOP:  icon_desktop(box);  break;
    case SHAPE_TOWER:    icon_tower(box);    break;
    case SHAPE_PORTABLE: icon_portable(box); break;
    default: {
        /* Modele inconnu : un boitier neutre avec un point d'interrogation.
         * On ne fait pas semblant de savoir. */
        Rect r = *box;
        InkLight(); PaintRoundRect(&r, 5, 5);
        InkBlack(); FrameRoundRect(&r, 5, 5);
        TextFont(systemFont); TextSize(12);
        MoveTo((short)(r.left + (r.right-r.left)/2 - 3),
               (short)(r.top  + (r.bottom-r.top)/2 + 4));
        DrawString("\p?");
        break;
    }
    }
    ForeColor(blackColor);
    SetPenState(&ps);
}

/* ── fenetre ─────────────────────────────────────────────────────────────
 * Dimensionnee sur l'ecran TROUVE. Sur une Plus (512x342) on prend tout
 * ce qui reste sous la barre de menus ; sur un Quadra on plafonne, une
 * fenetre de 1000 px de large n'aide personne a lire une liste. */
WindowPtr UI_MakeWindow(short *headerHeight)
{
    Rect scr = qd.screenBits.bounds;
    Rect b;
    short sw = (short)(scr.right - scr.left);
    short sh = (short)(scr.bottom - scr.top);
    short w, h, hdr;
    WindowPtr win;

    w = (short)(sw - 40);   if (w > 560) w = 560;
    h = (short)(sh - 80);   if (h > 380) h = 380;

    b.left   = (short)(scr.left + (sw - w) / 2);
    b.top    = (short)(scr.top + 40 + (sh - 40 - h) / 3);
    b.right  = (short)(b.left + w);
    b.bottom = (short)(b.top + h);

    /* En-tete proportionnel, mais jamais au point d'avaler la liste sur
     * un petit ecran : 40 px suffisent a une icone 32x32 et deux lignes. */
    hdr = (short)(h / 6);  if (hdr < 40) hdr = 40;  if (hdr > 56) hdr = 56;
    if (headerHeight) *headerHeight = hdr;

    win = NewWindow(NULL, &b, "\pPOM68K Prober", true, documentProc,
                    (WindowPtr)-1L, true, 0);
    return win;
}

/* ── bandeau ─────────────────────────────────────────────────────────── */
void UI_DrawHeader(WindowPtr win, short headerHeight,
                   MachineShape shape, const char *title, const char *sub)
{
    Rect band, icon;
    Str255 s;
    short n, textLeft;

    SetPort(win);
    band = win->portRect;
    band.bottom = headerHeight;

    InkLight();  PaintRect(&band);
    InkBlack();
    MoveTo(band.left, (short)(band.bottom - 1));
    LineTo(band.right, (short)(band.bottom - 1));

    SetRect(&icon, (short)(band.left + 6), (short)(band.top + 4),
                   (short)(band.left + 6 + 32), (short)(band.top + 4 + 32));
    if (icon.bottom > band.bottom - 4) icon.bottom = (short)(band.bottom - 4);
    UI_DrawMachineIcon(&icon, shape);

    textLeft = (short)(icon.right + 8);
    TextFont(systemFont);
    if (title) {
        TextSize(12); TextFace(bold);
        n = (short)strlen(title); if (n > 255) n = 255;
        s[0] = (unsigned char)n; memcpy(s + 1, title, n);
        MoveTo(textLeft, (short)(band.top + 16));
        InkBlack(); DrawString(s);
    }
    if (sub) {
        TextSize(9); TextFace(normal);
        n = (short)strlen(sub); if (n > 255) n = 255;
        s[0] = (unsigned char)n; memcpy(s + 1, sub, n);
        MoveTo(textLeft, (short)(band.top + 30));
        InkShade(); DrawString(s);
    }
    TextFace(normal); TextSize(12);
    ForeColor(blackColor); PenNormal();
}


/* ── jauge ───────────────────────────────────────────────────────────────
 * Tracee dans le bandeau, a droite de l'icone : pas de fenetre modale a
 * gerer, pas de place volee a la liste, et elle disparait toute seule.
 * En noir et blanc la barre est une trame pleine ; en couleur, un aplat.
 * Volontairement sans animation : chaque redessin coute des cycles a la
 * mesure qu'elle est censee observer. */
void UI_Progress(WindowPtr win, short headerHeight,
                 const char *what, short pct)
{
    Rect band, bar, fill;
    Str255 s;
    short n, w;
    GrafPtr save;

    GetPort(&save);
    SetPort(win);

    band = win->portRect;
    band.bottom = headerHeight;
    SetRect(&bar, (short)(band.right - 150), (short)(band.top + 14),
                  (short)(band.right - 10),  (short)(band.top + 26));
    if (bar.left < band.left + 60) { SetPort(save); return; }  /* trop etroit */

    if (pct >= 100) {                       /* fin : on efface proprement */
        Rect clear = bar;
        clear.top -= 12; clear.bottom += 2; clear.left -= 4;
        InkLight();
        PaintRect(&clear);
        ForeColor(blackColor); PenNormal();
        SetPort(save);
        return;
    }

    if (what) {
        Rect lbl = bar;
        lbl.bottom = bar.top; lbl.top = (short)(bar.top - 12);
        InkLight(); PaintRect(&lbl);
        TextFont(systemFont); TextSize(9); TextFace(normal);
        n = (short)strlen(what); if (n > 255) n = 255;
        s[0] = (unsigned char)n; memcpy(s + 1, what, n);
        InkBlack();
        MoveTo(bar.left, (short)(bar.top - 2));
        DrawString(s);
    }

    InkLight(); PaintRect(&bar);
    InkBlack(); FrameRect(&bar);
    if (pct > 0) {
        w = (short)(((long)(bar.right - bar.left - 2) * pct) / 100);
        SetRect(&fill, (short)(bar.left + 1), (short)(bar.top + 1),
                       (short)(bar.left + 1 + w), (short)(bar.bottom - 1));
        InkShade();
        PaintRect(&fill);
    }
    ForeColor(blackColor); PenNormal(); TextSize(12);
    SetPort(save);
}

/* ── A propos ────────────────────────────────────────────────────────── */
void UI_About(MachineShape shape, const char *machineName)
{
    Rect b, icon;
    WindowPtr w, save;
    EventRecord ev;
    Rect scr = qd.screenBits.bounds;
    short sw = (short)(scr.right - scr.left);
    Str255 s;
    short n;

    SetRect(&b, 0, 0, 300, 132);
    OffsetRect(&b, (short)(scr.left + (sw - 300) / 2), (short)(scr.top + 70));
    w = NewWindow(NULL, &b, "\p", true, dBoxProc, (WindowPtr)-1L, false, 0);
    if (!w) return;
    GetPort(&save);
    SetPort(w);

    SetRect(&icon, 16, 20, 48, 52);
    UI_DrawMachineIcon(&icon, shape);

    TextFont(systemFont); TextSize(12); TextFace(bold);
    MoveTo(64, 32);  DrawString("\pPOM68K Prober");
    TextFace(normal); TextSize(9);
    MoveTo(64, 48);  DrawString("\pDiagnostic Macintosh 68k");
    if (machineName) {
        n = (short)strlen(machineName); if (n > 255) n = 255;
        s[0] = (unsigned char)n; memcpy(s + 1, machineName, n);
        MoveTo(64, 62); DrawString(s);
    }
    TextSize(9);
    MoveTo(16, 88);
    DrawString("\pLe rapport BRUT est ecrit a cote de l'application,");
    MoveTo(16, 100);
    DrawString("\pdans POM68K Prober.txt. L'ecran, lui, interprete.");
    MoveTo(16, 118);
    DrawString("\pCliquez pour fermer.");

    /* Modale pauvre : n'importe quel clic ou touche ferme. */
    for (;;) {
        if (WaitNextEvent(mDownMask | keyDownMask, &ev, 30L, NULL)) break;
    }
    DisposeWindow(w);
    SetPort(save);
}
