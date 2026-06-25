#include <proto/exec.h>
#include <proto/graphics.h>
#include <graphics/view.h>
#include <stdio.h>
#include <proto/utf8rastport.h>
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
    0x00707580,   /* FS3E_COLOR_TEXT_DIM            muted gray */
    0x005599EE,   /* FS3E_COLOR_ACTION_TEXT         clear blue (Reply/Boost/Fave) */
};

/* ------------------------------------------------------------------ */
/* Internal helpers (same algorithm as aukstylesheet.c)                */
/* ------------------------------------------------------------------ */

/* Expand 8-bit channel to full 32-bit range: 0xFF → 0xFFFFFFFF */
static inline ULONG expand8(ULONG v)
{
    return 0x01010101UL * v;
    //(v << 24) | (v << 16) | (v << 8) | v;
}

static void obtain_pen(struct ColorMap *cm, FS3EManagedColor *c)
{
    ULONG r = expand8((c->rgbcolor >> 16) & 0xFF);
    ULONG g = expand8((c->rgbcolor >>  8) & 0xFF);
    ULONG b = expand8( c->rgbcolor        & 0xFF);
    LONG  pen;
 printf("obtain pen for %08x\n",c->rgbcolor);
    pen = ObtainBestPenA(cm, r, g, b, NULL);
    if (pen != -1) {
     printf("allocated:%d\n",pen);
        c->pen       = (WORD)pen;
        c->allocated = 1;
    } else {
        c->pen       = (WORD)FindColor(cm, r, g, b, -1);        
        c->allocated = 0;
     printf("found:%d\n",(int)c->pen);
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

/* ------------------------------------------------------------------ */
/* Draw context helpers                                                 */
/* ------------------------------------------------------------------ */

/* Create one DC, add the two standard fonts, return it (or NULL). */
static struct URPDrawContext *make_dc(const char *label,
                                     LONG bodySize, LONG emojiSize,
                                     ULONG styleBits)
{
    struct URPDrawContext *dc = URPDC_Create(/*(STRPTR)label*/NULL);
    if (!dc) return NULL;
    URPDC_SetPreferenceFlags(dc, URP_PREF_ANTIALIAS |
                                 URP_PREF_HIGHFILTERING |
                                 URP_PREF_CLUTMODE_NOMASK);
    URPDC_AddFont(dc, "LiberationSans-Regular.ttf", bodySize,  0);
    URPDC_AddFont(dc, "NotoColorEmoji32.ttf",       emojiSize, 0);
    if (styleBits)
        URPDC_SetStyle(dc, styleBits);
    return dc;
}

void FS3EStyle_ReleaseDrawContexts(FS3EStyle *st)
{
    if (!st) return;
    if (st->dcNormal)   { URPDC_Release(st->dcNormal);   st->dcNormal   = NULL; }
    if (st->dcUsername) { URPDC_Release(st->dcUsername); st->dcUsername = NULL; }
    if (st->dcMini)     { URPDC_Release(st->dcMini);     st->dcMini     = NULL; }
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

void FS3EStyle_InitDefaults(FS3EStyle *st)
{
    int i;
    if (!st) return;
    printf("FS3EStyle_InitDefaults\n");
    for (i = 0; i < FS3E_COLOR_COUNT; i++) {
        st->colors[i].rgbcolor = defaultColors[i];
        st->colors[i].pen      = 0;   /* safe fallback until ApplyColors */
        st->colors[i].allocated = 0;
    }
    st->screen = NULL;

    /* Release any previous contexts before (re)creating them */

    FS3EStyle_ReleaseDrawContexts(st);
    printf("FS3EStyle_InitDefaults2\n");
    st->dcNormal   = make_dc("fs3e-normal",   12, 12, URP_STYLE_NORMAL);
    st->dcUsername = make_dc("fs3e-username", 13, 13, URP_STYLE_BOLD);
    st->dcMini     = make_dc("fs3e-mini",      10,  10, URP_STYLE_NORMAL);
    printf("FS3EStyle_InitDefaults3\n");

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

    if(st->dcNormal) URPDC_SetDrawScreen(st->dcNormal,scr);
    if(st->dcMini) URPDC_SetDrawScreen(st->dcMini,scr);
    if(st->dcUsername) URPDC_SetDrawScreen(st->dcUsername,scr);
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
