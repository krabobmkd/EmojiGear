#include <proto/exec.h>
#include <proto/graphics.h>
#include <graphics/view.h>

#include "fs3estyle.h"

/* ------------------------------------------------------------------ */
/* Default dark theme                                                   */
/* ------------------------------------------------------------------ */

static const ULONG defaultColors[FS3E_COLOR_COUNT] = {
    0x00303034,   /* FS3E_COLOR_BUTTON_BG          dark panel */
    0x00563ACC,   /* FS3E_COLOR_BUTTON_SELECTED_BG  Mastodon purple */
    0x00191B22,   /* FS3E_COLOR_TIMELINE_BG         near-black */
    0x00FFFFFF,   /* FS3E_COLOR_USERNAME            white */
    0x0078BFFF,   /* FS3E_COLOR_HASHTAG             teal-blue */
    0x00444466,   /* FS3E_COLOR_ACCENT              muted purple (separators, borders) */
    0x00DADDE4,   /* FS3E_COLOR_TEXT                light gray */
    0x00606570,   /* FS3E_COLOR_TEXT_DIM            muted gray */
};

/* ------------------------------------------------------------------ */
/* Internal helpers (same algorithm as aukstylesheet.c)                */
/* ------------------------------------------------------------------ */

/* Expand 8-bit channel to full 32-bit range: 0xFF → 0xFFFFFFFF */
static ULONG expand8(ULONG v)
{
    return (v << 24) | (v << 16) | (v << 8) | v;
}

static void obtain_pen(struct ColorMap *cm, FS3EManagedColor *c)
{
    ULONG r = expand8((c->rgbcolor >> 16) & 0xFF);
    ULONG g = expand8((c->rgbcolor >>  8) & 0xFF);
    ULONG b = expand8( c->rgbcolor        & 0xFF);
    LONG  pen;

    pen = ObtainBestPenA(cm, r, g, b, NULL);
    if (pen != -1) {
        c->pen       = (WORD)pen;
        c->allocated = 1;
    } else {
        c->pen       = (WORD)FindColor(cm, r, g, b, -1);
        c->allocated = 0;
    }
}

static void release_pen(struct ColorMap *cm, FS3EManagedColor *c)
{
    if (c->allocated && c->pen >= 0)
        ReleasePen(cm, (LONG)c->pen);
    c->pen       = 1;   /* fall back to pen 1, always valid */
    c->allocated = 0;
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

void FS3EStyle_InitDefaults(FS3EStyle *st)
{
    int i;
    if (!st) return;
    for (i = 0; i < FS3E_COLOR_COUNT; i++) {
        st->colors[i].rgbcolor = defaultColors[i];
        st->colors[i].pen      = 1;   /* safe fallback until ApplyColors */
        st->colors[i].allocated = 0;
    }
    st->screen = NULL;
}

void FS3EStyle_ApplyColors(FS3EStyle *st, struct Screen *scr)
{
    struct ColorMap *cm;
    int i;

    if (!st) return;

    /* Release any pens from a previous apply (covers theme-change case too) */
    FS3EStyle_ReleasePens(st);

    st->screen = scr;
    if (!scr) return;

    cm = scr->ViewPort.ColorMap;
    for (i = 0; i < FS3E_COLOR_COUNT; i++)
        obtain_pen(cm, &st->colors[i]);
}

void FS3EStyle_ReleasePens(FS3EStyle *st)
{
    struct ColorMap *cm;
    int i;

    if (!st || !st->screen) return;

    cm = st->screen->ViewPort.ColorMap;
    for (i = 0; i < FS3E_COLOR_COUNT; i++)
        release_pen(cm, &st->colors[i]);

    st->screen = NULL;
}
