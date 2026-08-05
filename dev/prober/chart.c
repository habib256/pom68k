/*
 * chart.c — barres horizontales des mesures. Voir chart.h.
 *
 * NB : chaines en ASCII pur (source UTF-8, ecran MacRoman).
 */
#include "chart.h"
#include "interp.h"
#include "ui.h"

#include <Quickdraw.h>
#include <Fonts.h>
#include <string.h>
#include <stdio.h>

#define CHART_PAGES 2

typedef struct {
    const char *kernel;    /* nom du noyau dans le rapport ("alu", "fill@8") */
    const char *label;     /* ce que lit l'humain                            */
    const char *unit;      /* "op", "pixel", "octet"...                      */
    double      scale;     /* facteur applique au debit (4 pour longword->o) */
} Bar;

/* ── habillage ──────────────────────────────────────────────────────────
 * Quatre niveaux discernables partout : en couleur ce sont des teintes,
 * en noir et blanc les quatre trames systeme. Aucun gris invente. */
static void BarInk(short idx)
{
    static const RGBColor kHues[4] = {
        { 0x2000, 0x4000, 0x9000 },   /* bleu   */
        { 0x1000, 0x7000, 0x3000 },   /* vert   */
        { 0x9000, 0x5000, 0x1000 },   /* ambre  */
        { 0x7000, 0x2000, 0x6000 },   /* prune  */
    };
    if (UI_HasColor()) {
        RGBColor c = kHues[idx & 3];
        RGBForeColor(&c);
        PenPat(&qd.black);
    } else {
        const Pattern *p;
        switch (idx & 3) {
        case 0:  p = &qd.black;  break;
        case 1:  p = &qd.dkGray; break;
        case 2:  p = &qd.gray;   break;
        default: p = &qd.ltGray; break;
        }
        ForeColor(blackColor);
        PenPat(p);
    }
}

static void InkPlain(void)
{
    if (UI_HasColor()) { RGBColor k = {0,0,0}; RGBForeColor(&k); }
    else ForeColor(blackColor);
    PenPat(&qd.black);
}

/* Format court et honnete : on ne montre jamais plus de chiffres que la
 * mesure n'en supporte (3 passes a ~0,5 s : deux decimales au plus). */
static void FormatRate(char *out, double v, const char *unit)
{
    if (v >= 1.0e6)      sprintf(out, "%.2f M%s/s", v / 1.0e6, unit);
    else if (v >= 1.0e3) sprintf(out, "%.1f k%s/s", v / 1.0e3, unit);
    else                 sprintf(out, "%.0f %s/s", v, unit);
}

static void PStr(Str255 s, const char *c)
{
    short n = (short)strlen(c);
    if (n > 255) n = 255;
    s[0] = (unsigned char)n;
    memcpy(s + 1, c, n);
}

/* ── un groupe de barres, normalise sur SON propre maximum ────────────── */
static short DrawGroup(const Rect *area, short y, const char *title,
                       const Bar *bars, short n, const Report *raw,
                       short hue)
{
    double val[8], max = 0.0;
    short  i, have = 0;
    short  labelW = 96, gutter = 8;
    short  x0 = (short)(area->left + labelW + gutter);
    short  x1 = (short)(area->right - 96);
    Str255 s;
    char   buf[64];

    if (x1 <= x0 + 20) return y;                  /* fenetre trop etroite */

    for (i = 0; i < n && i < 8; i++) {
        val[i] = 0.0;
        if (Interp_Rate(raw, bars[i].kernel, &val[i])) {
            val[i] *= bars[i].scale;
            if (val[i] > max) max = val[i];
            have++;
        }
    }
    if (!have) return y;                          /* rien de mesure : muet */

    TextFont(systemFont); TextSize(9); TextFace(bold);
    InkPlain();
    MoveTo(area->left, (short)(y + 10));
    PStr(s, title); DrawString(s);
    TextFace(normal);
    y += 16;

    for (i = 0; i < n && i < 8; i++) {
        Rect bar;
        short w;
        if (val[i] <= 0.0) continue;

        MoveTo(area->left, (short)(y + 9));
        InkPlain();
        PStr(s, bars[i].label); DrawString(s);

        w = (short)((double)(x1 - x0) * (val[i] / max));
        if (w < 2) w = 2;
        SetRect(&bar, x0, y, (short)(x0 + w), (short)(y + 11));
        BarInk(hue);
        PaintRect(&bar);
        InkPlain();
        FrameRect(&bar);

        /* La VALEUR, toujours : une barre normalisee par groupe ne dit
         * rien de l'absolu, et c'est ainsi qu'un graphique ment. */
        FormatRate(buf, val[i], bars[i].unit);
        MoveTo((short)(x1 + 6), (short)(y + 9));
        PStr(s, buf); DrawString(s);

        y += 14;
    }
    return (short)(y + 6);
}

/* ── pages ──────────────────────────────────────────────────────────── */

static const Bar kCpu[] = {
    { "alu",    "Entiers",     "op",   1.0 },
    { "branch", "Branchements","iter", 1.0 },
    { "div",    "Divisions",   "divu", 1.0 },
};
static const Bar kMem[] = {
    { "mem",    "Lecture RAM", "o",    4.0 },   /* longwords -> octets */
};
static const Bar kFpu[] = {
    { "fpu",    "FPU",         "flop", 1.0 },
    { "float",  "Systeme",     "flop", 1.0 },
};

short Chart_Draw(WindowPtr win, const Rect *area, const Report *raw,
                 short page)
{
    short y = (short)(area->top + 4);
    PenState ps;

    SetPort(win);
    GetPenState(&ps);
    PenNormal();
    TextFont(systemFont); TextSize(9);

    if (page == 0) {
        y = DrawGroup(area, y, "Processeur",       kCpu, 3, raw, 0);
        y = DrawGroup(area, y, "Memoire",          kMem, 1, raw, 1);
        y = DrawGroup(area, y, "Virgule flottante",kFpu, 2, raw, 2);
        if (y == area->top + 4) {
            InkPlain();
            MoveTo(area->left, (short)(area->top + 16));
            DrawString("\pAucune mesure : menu Test > Performances.");
        }
    } else {
        /* Graphismes : une barre par profondeur mesuree, pour chaque
         * primitive. Les noms de noyaux portent la profondeur (fill@8). */
        static const char *prim[3] = { "fill", "blit", "text" };
        static const char *pnam[3] = { "Remplissage", "Recopie", "Texte" };
        static const char *punit[3] = { "pixel", "pixel", "car." };
        short p, d, k;
        Bar   bars[8];
        char  names[8][16], labels[8][16];

        for (p = 0; p < 3; p++) {
            k = 0;
            for (d = 1; d <= 32 && k < 8; d <<= 1) {
                double v;
                sprintf(names[k], "%s@%d", prim[p], d);
                if (!Interp_Rate(raw, names[k], &v)) continue;
                sprintf(labels[k], "%d bit%s", d, d > 1 ? "s" : "");
                bars[k].kernel = names[k];
                bars[k].label  = labels[k];
                bars[k].unit   = punit[p];
                bars[k].scale  = 1.0;
                k++;
            }
            if (k) y = DrawGroup(area, y, pnam[p], bars, k, raw, (short)p);
        }
        if (y == area->top + 4) {
            InkPlain();
            MoveTo(area->left, (short)(area->top + 16));
            DrawString("\pAucune mesure : menu Test > Graphismes.");
        }
    }

    ForeColor(blackColor);
    SetPenState(&ps);
    TextFace(normal); TextSize(12);
    return CHART_PAGES;
}
