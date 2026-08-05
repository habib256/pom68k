/*
 * gfx.c — inventaire video + banc QuickDraw. Voir gfx.h.
 *
 * NB : chaines en ASCII pur (source UTF-8, ecran MacRoman).
 */
#include "gfx.h"
#include "bench.h"

#include <Quickdraw.h>
/* QDOffscreen.h absent du jeu multiversal ; NewGWorld & co. vivent dans Multiverse.h. */
#include <Quickdraw.h>
#include "prober_compat.h"
#include <Gestalt.h>
#include <Fonts.h>
#include <string.h>
#include <stdio.h>

/* ── etat partage avec les noyaux (les BenchKernel ne prennent qu'iters) ── */
static WindowPtr gWin;
static Rect      gArea;          /* zone de dessin, dans le port de gWin   */
static long      gAreaPixels;
static GWorldPtr gWorld;         /* miroir hors ecran, ou NULL             */
static Boolean   gToOffscreen;   /* le noyau vise-t-il le GWorld ?          */
static Boolean   gHasColorQD;

/* Prepare le port cible et rend l'ancien pour restauration. */
static void BeginTarget(GrafPtr *save)
{
    GetPort(save);
    if (gToOffscreen && gWorld) {
        SetGWorld(gWorld, NULL);
    } else {
        SetPort(gWin);
        ClipRect(&gArea);
    }
}

static void EndTarget(GrafPtr save)
{
    if (gToOffscreen && gWorld) SetGWorld((CGrafPtr)save, GetMainDevice());
    else SetPort(save);
}

/* ── noyaux de dessin ────────────────────────────────────────────────────
 * Chacun dessine dans gArea. Les motifs alternent pour qu'un cache de
 * couleur ou un "meme valeur, rien a faire" ne fausse pas la mesure. */

static long k_fill(long iters)
{
    GrafPtr save;
    long i;
    BeginTarget(&save);
    for (i = 0; i < iters; i++)
        FillRect(&gArea, (i & 1) ? &qd.black : &qd.ltGray);
    EndTarget(save);
    return 1;
}

static long k_line(long iters)
{
    GrafPtr save;
    long i;
    short w = gArea.right - gArea.left, h = gArea.bottom - gArea.top;
    BeginTarget(&save);
    for (i = 0; i < iters; i++) {
        MoveTo(gArea.left, (short)(gArea.top + (i % h)));
        LineTo(gArea.right, (short)(gArea.bottom - (i % h)));
        MoveTo((short)(gArea.left + (i % w)), gArea.top);
        LineTo((short)(gArea.right - (i % w)), gArea.bottom);
    }
    EndTarget(save);
    return (long)w;
}

/* CopyBits : la bete de somme de QuickDraw, et le chemin qu'empruntent
 * les fenetres, les icones et le defilement. */
static long k_blit(long iters)
{
    GrafPtr save;
    long i;
    Rect dst = gArea;
    BeginTarget(&save);
    for (i = 0; i < iters; i++) {
        BitMap *src = (BitMap *)&(gWin->portBits);
        CopyBits(src, src, &gArea, &dst, srcCopy, NULL);
    }
    EndTarget(save);
    return 1;
}

static long k_text(long iters)
{
    GrafPtr save;
    long i;
    BeginTarget(&save);
    TextFont(systemFont);
    TextSize(12);
    for (i = 0; i < iters; i++) {
        MoveTo(gArea.left + 2,
               (short)(gArea.top + 12 + (i % 8) * 12));
        DrawString("\pPOM68K Prober 0123456789");
    }
    EndTarget(save);
    return 24L;                                   /* caracteres par tour */
}

static long k_oval(long iters)
{
    GrafPtr save;
    long i;
    BeginTarget(&save);
    for (i = 0; i < iters; i++)
        PaintOval(&gArea);
    EndTarget(save);
    return 1;
}


/* ── chemin sprite ───────────────────────────────────────────────────────
 * Deux mesures, et c'est leur RAPPORT qui renseigne. Modele : le Sprite
 * Animation Toolkit de Ragnemalm, qui offrait justement ces deux chemins
 * et basculait de l'un a l'autre a l'execution (`SATRun(fast)`) :
 *
 *   qdmask  QuickDraw `CopyMask` — le chemin "safe" de SAT, celui qu'il
 *           prenait par defaut quand aucune ressource de blitter n'etait
 *           disponible. Borne BASSE, portable partout.
 *   blit    blit masque ecrit a la main, ligne a ligne, dans NOTRE PROPRE
 *           GWorld verrouille. Borne HAUTE realiste.
 *
 * Pourquoi pas d'ecriture directe en memoire ECRAN, comme le vrai chemin
 * rapide de SAT : un outil de diagnostic n'a pas le droit d'ecrire hors
 * de ses fenetres, et Apple avertissait deja dans Inside Macintosh que
 * l'acces direct au framebuffer ne survivrait pas au materiel suivant.
 * Ecrire dans notre offscreen puis le recopier, c'est le modele SAT sans
 * le risque — et l'ecart mesure reste celui qui interesse.
 *
 * A ne pas oublier en lisant ces chiffres : SAT ne se synchronisait PAS
 * sur le retour de balayage (choix explicite de son auteur). Rien ici ne
 * suppose donc une cadence verrouillee a 60 Hz. */

#define SPR_W 32
#define SPR_H 32

static BitMap  gSprBits, gSprMask;
static char    gSprData[(SPR_W/8) * SPR_H];
static char    gSprMaskData[(SPR_W/8) * SPR_H];

static void sprite_build(void)
{
    short y;
    /* Un disque plein, avec son masque : la forme sprite canonique. */
    for (y = 0; y < SPR_H; y++) {
        short dy = (short)(y - SPR_H/2);
        short half = 0, x;
        for (x = SPR_W/2; x > 0; x--)
            if (x*x + dy*dy <= (SPR_W/2)*(SPR_W/2)) { half = x; break; }
        {
            unsigned long row = 0;
            for (x = 0; x < half; x++) {
                row |= 1UL << (15 + x);
                row |= 1UL << (16 - x);
            }
            gSprData[y*4+0] = (char)(row >> 24);
            gSprData[y*4+1] = (char)(row >> 16);
            gSprData[y*4+2] = (char)(row >> 8);
            gSprData[y*4+3] = (char)row;
            gSprMaskData[y*4+0] = gSprData[y*4+0];
            gSprMaskData[y*4+1] = gSprData[y*4+1];
            gSprMaskData[y*4+2] = gSprData[y*4+2];
            gSprMaskData[y*4+3] = gSprData[y*4+3];
        }
    }
    gSprBits.baseAddr = (Ptr)gSprData;
    gSprBits.rowBytes = SPR_W/8;
    SetRect(&gSprBits.bounds, 0, 0, SPR_W, SPR_H);
    gSprMask = gSprBits;
    gSprMask.baseAddr = (Ptr)gSprMaskData;
}

/* Chemin "safe" : CopyMask, comme SATSafeMaskBlit. */
static long k_sprite_qd(long iters)
{
    GrafPtr save;
    long i;
    short w = (short)(gArea.right - gArea.left - SPR_W);
    short h = (short)(gArea.bottom - gArea.top - SPR_H);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    BeginTarget(&save);
    for (i = 0; i < iters; i++) {
        Rect dst;
        SetRect(&dst, (short)(gArea.left + (i * 7) % w),
                      (short)(gArea.top  + (i * 11) % h), 0, 0);
        dst.right  = (short)(dst.left + SPR_W);
        dst.bottom = (short)(dst.top + SPR_H);
        CopyMask(&gSprBits, &gSprMask, (BitMap *)&(gWin->portBits),
                 &gSprBits.bounds, &gSprMask.bounds, &dst);
    }
    EndTarget(save);
    return 1;
}

/* Chemin "rapide" : blit masque a la main dans notre GWorld. Ecrit par
 * lignes via le baseAddr du PixMap — la technique des row lists de SAT,
 * mais bornee a notre propre memoire. */
static long k_sprite_blit(long iters)
{
    PixMapHandle pm;
    Ptr   base;
    long  rb, i;
    short w, h;

    if (!gWorld) return 0;
    pm = GetGWorldPixMap(gWorld);
    base = GetPixBaseAddr(pm);
    rb = (long)((**pm).rowBytes & 0x3FFF);
    w = (short)((**pm).bounds.right - (**pm).bounds.left - SPR_W);
    h = (short)((**pm).bounds.bottom - (**pm).bounds.top - SPR_H);
    if (w < 1) w = 1;
    if (h < 1) h = 1;

    for (i = 0; i < iters; i++) {
        short px = (short)((i * 7) % w), py = (short)((i * 11) % h);
        short y;
        for (y = 0; y < SPR_H; y++) {
            unsigned char *dst = (unsigned char *)base + (long)(py + y) * rb + px;
            const unsigned char *m = (const unsigned char *)&gSprMaskData[y*4];
            short b, bit;
            for (b = 0; b < 4; b++) {
                unsigned char mb = m[b];
                if (!mb) { dst += 8; continue; }
                for (bit = 7; bit >= 0; bit--, dst++)
                    if (mb & (1 << bit)) *dst = 0xFF;
            }
        }
    }
    return 1;
}

/* ── inventaire ─────────────────────────────────────────────────────────
 * On decrit ce qu'on trouve, sans supposer Color QuickDraw : sur une Plus
 * il n'y a pas de GDevice du tout, et le rapport doit rester utile. */
static void collect_inventory(Report *r)
{
    long qdv = 0;
    char key[40], val[80];

    gHasColorQD = (Gestalt(gestaltQuickdrawVersion, &qdv) == noErr &&
                   qdv >= gestalt8BitQD);
    Report_Add(r, "video", "colorQD", gHasColorQD ? "oui" : "non", R_INFO);

    if (!gHasColorQD) {
        /* Noir et blanc pur : la seule geometrie est celle de l'ecran. */
        Report_AddDec(r, "video", "screen0.w",
                      qd.screenBits.bounds.right - qd.screenBits.bounds.left,
                      R_INFO);
        Report_AddDec(r, "video", "screen0.h",
                      qd.screenBits.bounds.bottom - qd.screenBits.bounds.top,
                      R_INFO);
        Report_AddDec(r, "video", "screen0.depth", 1, R_INFO);
        Report_Add(r, "video", "screen0.depths", "1", R_INFO);
        return;
    }

    {
        GDHandle gd = GetDeviceList();
        short n = 0;
        while (gd && n < 8) {
            if (TestDeviceAttribute(gd, screenDevice_) &&
                TestDeviceAttribute(gd, screenActive_)) {
                Rect  b = (**gd).gdRect;
                short depth = (**((**gd).gdPMap)).pixelSize;
                short d;
                char  list[40];

                sprintf(key, "screen%d.w", n);
                Report_AddDec(r, "video", key, b.right - b.left, R_INFO);
                sprintf(key, "screen%d.h", n);
                Report_AddDec(r, "video", key, b.bottom - b.top, R_INFO);
                sprintf(key, "screen%d.depth", n);
                Report_AddDec(r, "video", key, depth, R_INFO);

                /* Les profondeurs que la carte accepte pour cette taille :
                 * c'est ce que le tableau de bord Moniteurs propose. */
                list[0] = 0;
                for (d = 1; d <= 32; d <<= 1) {
                    if (HasDepth(gd, d, gdDevType_, 0)) {
                        char t[8];
                        sprintf(t, "%s%d", list[0] ? "," : "", d);
                        if (strlen(list) + strlen(t) < sizeof list - 1)
                            strcat(list, t);
                    }
                }
                if (!list[0]) strcpy(list, "?");
                sprintf(key, "screen%d.depths", n);
                Report_Add(r, "video", key, list, R_INFO);

                sprintf(key, "screen%d.main", n);
                Report_Add(r, "video", key,
                           (gd == GetMainDevice()) ? "oui" : "non", R_INFO);
                n++;
            }
            gd = GetNextDevice(gd);
        }
        sprintf(val, "%d", n);
        Report_Add(r, "video", "screens", val, R_INFO);
    }
}

/* ── un jeu complet de noyaux a la profondeur courante ── */
static void run_suite(Report *r, short depth)
{
    char name[24];
    long px = gAreaPixels;

    /* Le nom porte la profondeur : "fill@8" — le golden host peut ainsi
     * comparer une meme machine a plusieurs profondeurs sans ambiguite. */
    gToOffscreen = false;
    sprintf(name, "fill@%d",  depth); Bench_Run(r, name, k_fill,  px, "pixel");
    sprintf(name, "blit@%d",  depth); Bench_Run(r, name, k_blit,  px, "pixel");
    sprintf(name, "line@%d",  depth); Bench_Run(r, name, k_line,  4L, "line");
    sprintf(name, "text@%d",  depth); Bench_Run(r, name, k_text,  24L, "char");
    sprintf(name, "oval@%d",  depth); Bench_Run(r, name, k_oval,  px, "pixel");

    sprintf(name, "sprQD@%d", depth);
    Bench_Run(r, name, k_sprite_qd, 1L, "sprite");
    if (gWorld) {
        sprintf(name, "sprBlit@%d", depth);
        Bench_Run(r, name, k_sprite_blit, 1L, "sprite");
    }

    /* Le meme remplissage hors ecran : l'ecart avec fill@ est le prix de
     * la memoire video (gfx.h). Seulement si un GWorld a pu etre cree. */
    if (gWorld) {
        gToOffscreen = true;
        sprintf(name, "fillOff@%d", depth);
        Bench_Run(r, name, k_fill, px, "pixel");
        gToOffscreen = false;
    }
}

void Gfx_Collect(Report *r, WindowPtr win, Boolean allDepths)
{
    GDHandle gd;
    short    startDepth = 1;
    Rect     wr;

    gWin = win;
    gWorld = NULL;
    gToOffscreen = false;

    sprite_build();
    collect_inventory(r);

    /* Zone de dessin : l'interieur de la fenetre, un peu retreci pour ne
     * pas empieter sur la barre de defilement de la liste. */
    wr = win->portRect;
    SetRect(&gArea, wr.left + 4, wr.top + 4, wr.right - 20, wr.bottom - 4);
    gAreaPixels = (long)(gArea.right - gArea.left) *
                  (long)(gArea.bottom - gArea.top);
    /* Zone NORMALISEE : 256x192 tient sur une Plus (512x342) comme sur un
     * Quadra. Sans elle, un debit mesure plein cadre sur une Plus inclut
     * une part de contention video incomparable a celle d'un Quadra — des
     * chiffres qui SEMBLENT comparables et ne le sont pas. */
    if (gArea.right - gArea.left > 256) gArea.right = (short)(gArea.left + 256);
    if (gArea.bottom - gArea.top > 192) gArea.bottom = (short)(gArea.top + 192);
    gAreaPixels = (long)(gArea.right - gArea.left) *
                  (long)(gArea.bottom - gArea.top);
    Report_AddDec(r, "gfx", "area.w", gArea.right - gArea.left, R_INFO);
    Report_AddDec(r, "gfx", "area.h", gArea.bottom - gArea.top, R_INFO);
    Report_Add(r, "gfx", "area.norm",
               (gArea.right - gArea.left == 256 &&
                gArea.bottom - gArea.top == 192) ? "oui" : "non", R_INFO);

    gd = gHasColorQD ? GetMainDevice() : NULL;
    if (gd) startDepth = (**((**gd).gdPMap)).pixelSize;

    /* Miroir hors ecran de la meme taille : sert la mesure d'ecart. */
    if (gHasColorQD) {
        Rect gw = gArea;
        OffsetRect(&gw, -gw.left, -gw.top);
        if (NewGWorld(&gWorld, 0 /* meme profondeur que l'ecran */,
                      &gw, NULL, NULL, 0) != noErr)
            gWorld = NULL;
        if (gWorld) LockPixels(GetGWorldPixMap(gWorld));
    }

    if (!allDepths || !gd) {
        run_suite(r, startDepth);
    } else {
        short d;
        for (d = 1; d <= 32; d <<= 1) {
            if (!HasDepth(gd, d, gdDevType_, 0)) continue;
            if (SetDepth(gd, d, gdDevType_, 0) != noErr) {
                char k[24];
                sprintf(k, "depth%d.skip", d);
                Report_Add(r, "gfx", k, "SetDepth refuse", R_WARN);
                continue;
            }
            /* Le changement de profondeur repeint tout : on laisse le
             * systeme se remettre avant de chronometrer quoi que ce soit. */
            InvalRect(&win->portRect);
            run_suite(r, d);
        }
        /* RESTAURATION — non negociable : un outil de diagnostic rend
         * l'ecran dans l'etat ou il l'a trouve. */
        if (SetDepth(gd, startDepth, gdDevType_, 0) != noErr)
            Report_Add(r, "gfx", "depth.restore", "ECHEC", R_FAIL);
        else
            Report_Add(r, "gfx", "depth.restore", "ok", R_OK);
        InvalRect(&win->portRect);
    }

    if (gWorld) {
        UnlockPixels(GetGWorldPixMap(gWorld));
        DisposeGWorld(gWorld);
        gWorld = NULL;
    }
}
