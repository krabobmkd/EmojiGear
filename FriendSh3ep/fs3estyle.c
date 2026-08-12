#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/alib.h>
#include <graphics/view.h>
#include <stdio.h>
#include <string.h>
#include <proto/utf8rastport.h>

#include <intuition/gadgetclass.h>
#include <gadgets/button.h>
#include <images/bitmap.h>
#include <proto/bitmap.h>
#include <graphics/layers.h>

#include "compilers.h"
#include "fs3estyle.h"
#include "fs3etbdefaultbtn.h"
#include "patch9.h"
#include "fs3eboopsimainwindow.h"
#include "stylefile.h"
#include "bdbprintf.h"
#include "rgbimage.h"
#include "friendsh3ep.h"
#include "UniButtonP9/unibuttonp9.h"
/* Opened optionally in friendsh3ep.c, mirroring BevelBase: if the class
 * isn't available, FS3EStyle_LoadThemeImages() fails gracefully and title
 * bar buttons simply keep whatever image (none) they had. */
extern struct Library *BitMapBase;

/* GA_BackFill / LAYOUT_BackFill hook message, as passed by layout.gadget --
 * not declared in the NDK headers. Field names/layout verified against the
 * working reference implementation in
 * amigapetmate/src/boopsimainwindow.c:BackFillHook_Pattern(). */
struct FS3EBackFillMsg {
    struct Layer     *bf_Layer;
    struct Rectangle  bf_Bounds;
    LONG              bf_OffsetX;
    LONG              bf_OffsetY;
};

/* Forward declaration: referenced by FS3EStyle_InitDefaults to set up
 * tbBgHook, defined further down alongside the rest of the image theme.
 *
 * IMPORTANT: do NOT add a REG(a0, struct Hook *) parameter here. Backfill
 * hooks are invoked directly by layers.library during layer repair, not
 * via CallHookPkt; declaring the a0 binding here shifted the a1/a2 values
 * GCC handed to bfm/rp, so the function only ever saw one small (~80px)
 * garbage rectangle sampling the wrong part of the source image. The
 * verified reference (amigapetmate) only binds a2/a1 and reads its state
 * from a plain static instead of hook->h_Data -- do the same. */
static void ASM SAVEDS FS3EStyle_TitleBarBackFillFunc(
    REG(a2, struct RastPort *rp),
    REG(a1, struct FS3EBackFillMsg *bfm));

/* ------------------------------------------------------------------ */
/* Default dark theme                                                   */
/* ------------------------------------------------------------------ */

static const ULONG defaultColors[FS3E_COLOR_COUNT] = {
    0x00303034,   /* FS3E_COLOR_BUTTON_BG          dark panel */
    0x00563ACC,   /* FS3E_COLOR_BUTTON_SELECTED_BG  Mastodon purple */
    0x00191B22,   /* FS3E_COLOR_TIMELINE_BG         near-black */
    0x00232840,   /* FS3E_COLOR_PROFILE_HEADER_BG   near-black with a subtle blue/purple tint */
    0x00FFFFFF,   /* FS3E_COLOR_USERNAME            white */
    0x0078BFFF,   /* FS3E_COLOR_HASHTAG             teal-blue */
    0x00444466,   /* FS3E_COLOR_ACCENT              muted purple (separators, borders) */
    0x00DADDE4,   /* FS3E_COLOR_TEXT                light gray */
    0x00707580,   /* FS3E_COLOR_TEXT_DIM            muted gray */
    0x005599EE,   /* FS3E_COLOR_ACTION_TEXT         clear blue (Reply/Boost/Fave) */
    0x00232840,   /* FS3E_COLOR_CARD_BG             same as PROFILE_HEADER_BG by default */
    0x00444466,   /* FS3E_COLOR_CARD_BORDER         same as ACCENT by default */

    0x00000000,   /* FS3E_COLOR_BTBGBM_TEXT_ENABLED */
    0x00FFFFFF,   /* FS3E_COLOR_BTBGBM_TEXT_SELECTED */

    0x00E0B000,   /* FS3E_COLOR_SENSITIVE_WARNING   warning yellow */
};

/* style.txt key for each FS3EColorRole, in the same order as the enum --
 * see themes/mouton/style.txt. FS3EStyle_LoadThemeImages() looks each of
 * these up and overrides the matching defaultColors[] entry when present. */
static const char *colorStyleKeys[FS3E_COLOR_COUNT] = {
    "timeline.color.buttonbg",
    "timeline.color.button_selectedbg",
    "timeline.color.timelinebg",
    "timeline.color.profileheaderbg",
    "timeline.color.username",
    "timeline.color.hashtag",
    "timeline.color.accent",
    "timeline.color.text",
    "timeline.color.text_dim",
    "timeline.color.action_text",
    "timeline.color.cardbg",
    "timeline.color.cardborder",

    "btbgbm.textcolor.enabled",
    "btbgbm.textcolor.selected",

    "timeline.color.sensitivewarning",
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
// printf("obtain pen for %08x\n",c->rgbcolor);
    pen = ObtainBestPenA(cm, r, g, b, NULL);
    if (pen != -1) {
//     printf("allocated:%d\n",pen);
        c->pen       = (WORD)pen;
        c->allocated = 1;
    } else {
        c->pen       = (WORD)FindColor(cm, r, g, b, -1);        
        c->allocated = 0;
//     printf("found:%d\n",(int)c->pen);
    }
}

static void release_pen(struct ColorMap *cm, FS3EManagedColor *c)
{
    if (c->allocated && c->pen >= 0)
        ReleasePen(cm, (LONG)c->pen);
    c->pen       = 1;   /* fall back to pen 1, always valid */
    c->allocated = 0;
}

/* Same obtain/release pair, applied to a whole FS3EManagedColor[] base in
 * one call -- FS3EStyle_ApplyColors/ReleasePens run this once for
 * st->colors[] and once per st->patch9[] slot for bgcolors[]/textcolors[],
 * so all three pen bases stay in lockstep through one shared loop body. */
static void obtain_pens(struct ColorMap *cm, FS3EManagedColor *arr, int count)
{
    int i;
    for (i = 0; i < count; i++)
        obtain_pen(cm, &arr[i]);
}

static void release_pens(struct ColorMap *cm, FS3EManagedColor *arr, int count)
{
    int i;
    for (i = 0; i < count; i++)
        release_pen(cm, &arr[i]);
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

/* Derive post-layout pixel values from dcNormal line metrics.
 *   avatarSize  = lineH × 2.5  (1.25× the old dpiHeight×2 formula),
 *                 further ×1.25 when settings.biggerThumbnails is on
 *   postPadLeft = lineH/3 + 2  (~6px at 12pt, scales with font)
 *   avatarGap   = lineH/3 + 2  (same rule as postPadLeft)
 */
static void compute_layout(FS3EStyle *st)
{
    struct URPTextMetric m;
    WORD lineH = 14;

    if (st->dcNormal) {
        URPDC_GetFontLineMetrics(st->dcNormal, &m);
        if (m.height > 0) lineH = (WORD)m.height;
    }

    st->avatarSize  = (WORD)(lineH * 5 / 2);
    if (app && app->settings.biggerThumbnails)
        st->avatarSize = (WORD)(st->avatarSize * 5 / 4);
    st->postPadLeft = (WORD)(lineH / 3 + 2);
    st->avatarGap   = (WORD)(lineH / 3 + 2);

 //   printf("lineH:%d avatarGap:%d\n",lineH,st->avatarGap);
}

/* Flush fonts in dc and re-add them at the given size.
 * styleBits is re-applied (non-zero only for dcUsername = URP_STYLE_BOLD). */
static void resize_dc(struct URPDrawContext *dc, int size,
                      const char *primary,
                      const char *fallback1, const char *fallback2,
                      const char *emoji, ULONG styleBits)
{
    if (!dc) return;
    URPDC_FlushFonts(dc);
    URPDC_AddFont(dc, primary, size, 0);
    if (fallback1) URPDC_AddFont(dc, fallback1, size, 0);
    if (fallback2) URPDC_AddFont(dc, fallback2, size, 0);
    URPDC_AddFont(dc, emoji, size, 0);
    if (styleBits) URPDC_SetStyle(dc, styleBits);
}

void FS3EStyle_SetFontSize(FS3EStyle *st, int baseSize,
                           const char *primary,
                           const char *fallback1, const char *fallback2,
                           const char *emoji)
{
    const char *pri = primary ? primary : "LiberationSans-Regular.ttf";
    const char *emo = emoji   ? emoji   : "NotoColorEmoji32.ttf";
    int normalSize   = baseSize;
    int usernameSize = (baseSize * 18)/16;
    int miniSize     = (baseSize *14)/16;

    if (!st) return;

    resize_dc(st->dcNormal,   normalSize,   pri, fallback1, fallback2, emo, 0);
    resize_dc(st->dcUsername, usernameSize, pri, fallback1, fallback2, emo, URP_STYLE_BOLD);
    resize_dc(st->dcMini,     miniSize,     pri, fallback1, fallback2, emo, 0);

    /* Re-bind the screen so the DCs can access the colour map for CLUT mode.
     * URPDC_FlushFonts may reset internal bitmap state. */
    if (st->screen) {
        if (st->dcNormal)   URPDC_SetDrawScreen(st->dcNormal,   st->screen);
        if (st->dcUsername) URPDC_SetDrawScreen(st->dcUsername, st->screen);
        if (st->dcMini)     URPDC_SetDrawScreen(st->dcMini,     st->screen);
    }

    compute_layout(st);
}

void FS3EStyle_ResetColors(FS3EStyle *st)
{
    int i;
    if (!st) return;
    for (i = 0; i < FS3E_COLOR_COUNT; i++)
        st->colors[i].rgbcolor = defaultColors[i];
}

void FS3EStyle_InitDefaults(FS3EStyle *st)
{
    int i;
    if (!st) return;

    FS3EStyle_ResetColors(st);
    for (i = 0; i < FS3E_COLOR_COUNT; i++) {
        st->colors[i].pen      = 0;   /* safe fallback until ApplyColors */
        st->colors[i].allocated = 0;
    }
    st->screen = NULL;

    /* Release any previous contexts before (re)creating them */

    FS3EStyle_ReleaseDrawContexts(st);

    st->dcNormal   = make_dc("fs3e-normal",   12, 12, URP_STYLE_NORMAL);
    st->dcUsername = make_dc("fs3e-username", 13, 13, URP_STYLE_BOLD);
    st->dcMini     = make_dc("fs3e-mini",      10,  10, URP_STYLE_NORMAL);

    compute_layout(st);

    /* Image theme: default path only; assets are loaded once a screen is
     * available (FS3EStyle_LoadThemeImages, called from GenericOpenWindow). */
    memset(&st->tbButtons, 0, sizeof(st->tbButtons));
    memset(st->tbImages, 0, sizeof(st->tbImages));
    memset(st->tbDefaultImages, 0, sizeof(st->tbDefaultImages));
    st->tbButtonWidth  = 0;
    st->tbButtonHeight = 0;
    st->themePath = NULL;
    FS3EStyle_SetThemePath(st, NULL);

    memset(&st->tbBg, 0, sizeof(st->tbBg));
    st->tbBgHook.h_Entry    = (HOOKFUNC)FS3EStyle_TitleBarBackFillFunc;
    st->tbBgHook.h_SubEntry = NULL;
    st->tbBgHook.h_Data     = NULL;  /* unused -- see FS3EStyle_TitleBarBackFillFunc */

    memset(&st->titlebarTitle, 0, sizeof(st->titlebarTitle));
    st->titlebarTitleX = 0;
    st->titlebarTitleY = 0;
    memset(&st->titlebarLeft, 0, sizeof(st->titlebarLeft));
    memset(&st->titlebarRight, 0, sizeof(st->titlebarRight));

    memset(&st->patch9, 0, sizeof(st->patch9));
    memset(&st->btbgbmBitmap, 0, sizeof(st->btbgbmBitmap));
    memset(&st->waitImage, 0, sizeof(st->waitImage));
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
    obtain_pens(cm, st->colors, FS3E_COLOR_COUNT);
    for (i = 0; i < FS3ESTYLE_PATCH9_COUNT; i++) {
        obtain_pens(cm, st->patch9[i].bgcolors,   PATCH9_NUM_STATES);
        obtain_pens(cm, st->patch9[i].textcolors, PATCH9_NUM_STATES);
    }

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
    release_pens(cm, st->colors, FS3E_COLOR_COUNT);
    for (i = 0; i < FS3ESTYLE_PATCH9_COUNT; i++) {
        release_pens(cm, st->patch9[i].bgcolors,   PATCH9_NUM_STATES);
        release_pens(cm, st->patch9[i].textcolors, PATCH9_NUM_STATES);
    }

    st->screen = NULL;
}

/* ------------------------------------------------------------------ */
/* Image theme                                                          */
/* ------------------------------------------------------------------ */

void free_tb_images(FS3EStyle *st)
{
    int i;
    for (i = 0; i < FS3ESTYLE_TBBUTTON_COUNT; i++) {
        if (st->tbImages[i]) {
            DisposeObject((Object *)st->tbImages[i]);
            st->tbImages[i] = NULL;
        }
    }
}

/* Mirrors st->tbBg for FS3EStyle_TitleBarBackFillFunc, which (see the note
 * on its forward declaration) cannot receive per-instance data via
 * hook->h_Data. There is only one FS3EStyle instance in this app, so a
 * plain file-scope static is sufficient and keeps this hook decoupled from
 * struct App. Kept in sync by FS3EStyle_LoadThemeImages/UnloadThemeImages. */
static struct BitMap *tbBgBitmap = NULL;
static LONG           tbBgWidth  = 0;
static LONG           tbBgHeight = 0;

/* Same mirroring, same reason, for the title bar border images
 * (titlebarLeft/titlebarRight) -- these are conceptually part of the
 * background too (see FS3EStyle_TitleBarBackFillFunc), so they're painted
 * from this same hook rather than TitleBarLayout_OnRender. */
static struct BitMap *tbLeftBitmap  = NULL;
static PLANEPTR       tbLeftMask   = NULL;
static LONG           tbLeftWidth  = 0;
static LONG           tbLeftHeight = 0;
static struct BitMap *tbRightBitmap  = NULL;
static PLANEPTR       tbRightMask   = NULL;
static LONG           tbRightWidth  = 0;
static LONG           tbRightHeight = 0;

/* Tile tbBgBitmap across bfm->bf_Bounds. Verified against the working
 * reference (amigapetmate's BackFillHook_Pattern): BltBitMap operates on
 * the raw RastPort BitMap directly and is unaffected by Layer clipping, so
 * (unlike a RastPort-drawing-function hook) there is no need to copy rp and
 * null out itsimgX Layer field here. */
static void ASM SAVEDS FS3EStyle_TitleBarBackFillFunc(
    REG(a2, struct RastPort *rp),
    REG(a1, struct FS3EBackFillMsg *bfm))
{
    struct BitMap *dst;
    LONG dstX, dstY, dstW, dstH;
    LONG srcStartX, srcStartY;
    LONG x, y, srcX, srcY, chunkW, chunkH;

    dst  = rp->BitMap;
    dstX = (LONG)bfm->bf_Bounds.MinX;
    dstY = (LONG)bfm->bf_Bounds.MinY;
    dstW = (LONG)bfm->bf_Bounds.MaxX - dstX + 1;
    dstH = (LONG)bfm->bf_Bounds.MaxY - dstY + 1;
    if (dstW <= 0 || dstH <= 0) return;

// bdbprintf("bfhook l:%d t:%d r:%d b:%d ofsx:% ofsy:%d\n",(LONG)bfm->bf_Bounds.MinX,
// (LONG)bfm->bf_Bounds.MinY,(LONG)bfm->bf_Bounds.MaxX,
// bfm->bf_Bounds.MaxY,bfm->bf_OffsetX,bfm->bf_OffsetY);

// if(rp->Layer)
// {
// bdbprintf("layer:%08x bm:%08x layerflag:%04x\n",rp->Layer, rp->BitMap,rp->Layer->Flags);

// }
    if (tbBgBitmap && tbBgWidth > 0 && tbBgHeight > 0) {
        /* Phase of the pattern at the top-left of the damage rect, anchored
         * to the layer's own origin so tiles stay aligned across separate
         * (partial) backfill calls and when the window moves. */
        srcStartX = (((dstX - rp->Layer->bounds.MinX) % tbBgWidth)  + tbBgWidth)  % tbBgWidth;
        srcStartY = (((dstY - rp->Layer->bounds.MinY) % tbBgHeight) + tbBgHeight) % tbBgHeight;

        /* only tile on X axis */
        y = 0;
       // while (y < dstH) {
            srcY   = srcStartY;// (srcStartY + y) % tbBgHeight;
            chunkH = tbBgHeight - srcY;
            if (chunkH > dstH - y) chunkH = dstH - y;

            x = 0;
            while (x < dstW) {
                srcX   = (srcStartX + x) % tbBgWidth;
                chunkW = tbBgWidth - srcX;
                if (chunkW > dstW - x) chunkW = dstW - x;

                BltBitMap(tbBgBitmap, srcX, srcY,
                          dst, dstX + x, dstY + y,
                          (WORD)chunkW, (WORD)chunkH, 0xC0, 0xFF, NULL);
                x += chunkW;
            }
        //    y += chunkH;
        // }
    }

    /* Title bar border images (titlebar.left/titlebar.right in style.txt),
     * anchored to the layer's own top-left/top-right corner -- same
     * absolute coordinate space as bf_Bounds and dst (rp->BitMap), NOT
     * gadget-local -- then clipped by hand against the current damage
     * rect (bf_Bounds) before blitting, since BltBitMap/
     * BltMaskBitMapRastPort have no clip region of their own the way a
     * Layer-attached RastPort draw call would; a corner image bigger than
     * whatever sliver of the title bar is being repaired this call could
     * otherwise paint outside bf_Bounds and corrupt whatever's there. */
    if (tbLeftBitmap && tbLeftWidth > 0 && tbLeftHeight > 0) {
        LONG imgX = rp->Layer->bounds.MinX;
        LONG imgY = rp->Layer->bounds.MinY;
        LONG clipX0 = imgX > dstX ? imgX : dstX;
        LONG clipY0 = imgY > dstY ? imgY : dstY;
        LONG clipX1 = (imgX + tbLeftWidth)  < (dstX + dstW) ? (imgX + tbLeftWidth)  : (dstX + dstW);
        LONG clipY1 = (imgY + tbLeftHeight) < (dstY + dstH) ? (imgY + tbLeftHeight) : (dstY + dstH);
        if (clipX1 > clipX0 && clipY1 > clipY0) {
            LONG w = clipX1 - clipX0, h = clipY1 - clipY0;
            if (tbLeftMask) {
                struct RastPort tmpRp;
                InitRastPort(&tmpRp);
                tmpRp.BitMap = dst;
                BltMaskBitMapRastPort(tbLeftBitmap, clipX0 - imgX, clipY0 - imgY,
                                      &tmpRp, clipX0, clipY0, (LONG)w, (LONG)h,
                                      PATCH9_MASK_MINTERM, tbLeftMask);
            } else {
                BltBitMap(tbLeftBitmap, clipX0 - imgX, clipY0 - imgY,
                          dst, clipX0, clipY0, (WORD)w, (WORD)h, 0xC0, 0xFF, NULL);
            }
        }
    }

    if (tbRightBitmap && tbRightWidth > 0 && tbRightHeight > 0) {
        LONG imgX = rp->Layer->bounds.MaxX - tbRightWidth + 1;
        LONG imgY = rp->Layer->bounds.MinY;
        LONG clipX0 = imgX > dstX ? imgX : dstX;
        LONG clipY0 = imgY > dstY ? imgY : dstY;
        LONG clipX1 = (imgX + tbRightWidth)  < (dstX + dstW) ? (imgX + tbRightWidth)  : (dstX + dstW);
        LONG clipY1 = (imgY + tbRightHeight) < (dstY + dstH) ? (imgY + tbRightHeight) : (dstY + dstH);
        if (clipX1 > clipX0 && clipY1 > clipY0) {
            LONG w = clipX1 - clipX0, h = clipY1 - clipY0;
            if (tbRightMask) {
                struct RastPort tmpRp;
                InitRastPort(&tmpRp);
                tmpRp.BitMap = dst;
                BltMaskBitMapRastPort(tbRightBitmap, clipX0 - imgX, clipY0 - imgY,
                                      &tmpRp, clipX0, clipY0, (LONG)w, (LONG)h,
                                      PATCH9_MASK_MINTERM, tbRightMask);
            } else {
                BltBitMap(tbRightBitmap, clipX0 - imgX, clipY0 - imgY,
                          dst, clipX0, clipY0, (WORD)w, (WORD)h, 0xC0, 0xFF, NULL);
            }
        }
    }
}

void FS3EStyle_SetThemePath(FS3EStyle *st, const char *path)
{
    char *copy = NULL;

    if (!st) return;

    if (path && path[0]) {
        ULONG len = (ULONG)strlen(path) + 1;
        copy = (char *)AllocVec(len, MEMF_ANY);
        if (copy) strcpy(copy, path);
    }

    if (st->themePath) FreeVec(st->themePath);
    st->themePath = copy;   /* NULL = no theme */
}

/* One style.txt-driven Patch9 skin slot: "<prefix>.image" / "<prefix>.cornersize",
 * plus per-state bg/text colour defaults for "<prefix>.bgcolor.<suffix>" /
 * "<prefix>.textcolor.<suffix>" (see patch9StateSuffix[] below). Arrays are
 * indexed by PATCH9_NORMAL/SELECTED, same as Patch9.bgcolors/textcolors
 * themselves. */
typedef struct {
    const char *prefix;
    const char *defaultFile;
    WORD        defaultCorner;
    ULONG       defaultBgColor[PATCH9_NUM_STATES];
    ULONG       defaultTextColor[PATCH9_NUM_STATES];
} FS3EPatch9SlotDef;

static const FS3EPatch9SlotDef patch9Slots[FS3ESTYLE_PATCH9_COUNT] = {
    { "bt1Patch9", "bt1patch9.iff", 8,
      { 0x007B7BE3, 0x00BCBCFF },
      { 0x00000000, 0x00000000 } },
    { "bt2Patch9", "bt2patch9.iff", 8,
      { 0x0083ED60, 0x00B2FFC8 },
      { 0x00000000, 0x00000000 } },
    { "btbgbmPatch9", "btbgbmpatch9.iff", 8,
      { 0x00888888, 0x00BCBCFF },
      { 0x00CCCCCC, 0x00FFFFFF } },
};

/* style.txt suffix for each PATCH9_* state, e.g. "bt1Patch9.bgcolor.selected" --
 * see themes/mouton/style.txt. */
static const char *patch9StateSuffix[PATCH9_NUM_STATES] = {
    "enabled",   /* PATCH9_NORMAL */
    "selected",  /* PATCH9_SELECTED */
};

void FS3EStyle_ResetPatch9Colors(FS3EStyle *st)
{
    int slot, state;
    if (!st) return;
    for (slot = 0; slot < FS3ESTYLE_PATCH9_COUNT; slot++) {
        const FS3EPatch9SlotDef *def = &patch9Slots[slot];
        for (state = 0; state < PATCH9_NUM_STATES; state++) {
            st->patch9[slot].bgcolors[state].rgbcolor   = def->defaultBgColor[state];
            st->patch9[slot].textcolors[state].rgbcolor = def->defaultTextColor[state];
        }
    }
}

/* Load one Patch9 slot from style.txt (see patch9Slots[] above), falling
 * back to defaultFile/defaultCorner/defaultBgColor/defaultTextColor if
 * style.txt or the key is missing. Image load is optional/non-fatal -- a
 * missing file just means buttons referencing this slot keep their flat
 * colour fill (see Patch9_IsLoaded in patch9.h); the bg/textcolors are that
 * flat fill (and the blend colours over the skin once loaded), so they are
 * always set regardless of whether the image itself loaded. */
static void fs3estyle_load_patch9_slot(FS3EStyle *st, const StyleFile *sf, int slot,
                                       struct Screen *scr)
{
    const FS3EPatch9SlotDef *def = &patch9Slots[slot];
    Patch9 *p9 = &st->patch9[slot];
    char path[256];
    char key[80];
    int state;

    snprintf(key, sizeof(key), "%s.image", def->prefix);
    snprintf(path, sizeof(path), "%s/%s", st->themePath,
             StyleFile_GetString(sf, key, def->defaultFile));

    snprintf(key, sizeof(key), "%s.cornersize", def->prefix);

    /* Patch9_Init() memsets the whole struct, so bg/textcolors are (re)set
     * below, after Init -- setting them before would just get wiped. */
    if (!Patch9_Init(p9, path,
                     (WORD)StyleFile_GetInt(sf, key, def->defaultCorner)) ||
        !Patch9_Load(p9, scr)) {
    }

    for (state = 0; state < PATCH9_NUM_STATES; state++) {
        snprintf(key, sizeof(key), "%s.bgcolor.%s", def->prefix, patch9StateSuffix[state]);
        p9->bgcolors[state].rgbcolor = StyleFile_GetColor(sf, key, def->defaultBgColor[state]);

        snprintf(key, sizeof(key), "%s.textcolor.%s", def->prefix, patch9StateSuffix[state]);
        p9->textcolors[state].rgbcolor = StyleFile_GetColor(sf, key, def->defaultTextColor[state]);
    }
}

void FS3EStyle_UpdateAllDCColorMap(FS3EStyle *st, struct Screen *scr)
{
    if(st->dcMini) URPDC_UpdateColorMap(st->dcMini,scr);
    if(st->dcNormal) URPDC_UpdateColorMap(st->dcNormal,scr);
    if(st->dcUsername) URPDC_UpdateColorMap(st->dcUsername,scr);
    if(app->buttonDC)  URPDC_UpdateColorMap(app->buttonDC,scr);
}

BOOL FS3EStyle_LoadThemeImages(FS3EStyle *st, struct Screen *scr)
{
    char path[256];
    char stylePath[280];
    StyleFile sf;
    struct BitMap *bm;
    WORD cellW, cellH;
    int i;

    if (!st) return FALSE;

    FS3EStyle_UnloadThemeImages(st);

    /* No theme set -- nothing to load. Not an error: FS3EApp_LoadTheme's
     * "no theme" case (friendsh3ep.c) relies on this to be a clean no-op,
     * distinct from the unload just above (which it still wants). */
    if (!st->themePath || !st->themePath[0]) return FALSE;

    /* style.txt names the actual asset files/metrics below -- see
     * themes/mouton/style.txt. Missing file/keys just fall back to the
     * names this function always used, so this is safe unconditionally. */
    snprintf(stylePath, sizeof(stylePath), "%s/style.txt", st->themePath);
    StyleFile_Load(&sf, stylePath);

    /* Reset every role to its hardcoded default first -- otherwise a role
     * a *previous* theme's style.txt set (this function is also called on
     * a live theme switch, not just once at startup -- see
     * FS3EApp_LoadTheme in friendsh3ep.c) would keep bleeding through for
     * any key the *new* theme's style.txt doesn't happen to mention,
     * instead of falling back to the default like a fresh load would. */
    FS3EStyle_ResetColors(st);

    /* Override every FS3EColorRole's RGB value from its style.txt key (see
     * colorStyleKeys[] above), falling back to the default (just reset
     * above) if the file or key is missing. Must run before
     * FS3EStyle_ApplyColors(), which resolves rgbcolor -> screen pen for
     * every role -- the caller (GenericOpenWindow) already calls them in
     * that order. */
    for (i = 0; i < FS3E_COLOR_COUNT; i++)
        st->colors[i].rgbcolor = StyleFile_GetColor(&sf, colorStyleKeys[i],
                                                    st->colors[i].rgbcolor);

    if (!BitMapBase) {
        return FALSE;
    }

    snprintf(path, sizeof(path), "%s/%s", st->themePath,
             StyleFile_GetString(&sf, "titlebar.buttons", "tbbuttons.iff"));

    /* Optional, like every other theme image -- a missing/failed load just
     * means st->tbImages[] stay NULL (already the case after
     * FS3EStyle_UnloadThemeImages()'s free_tb_images() above) and
     * FS3EStyle_SyncTitleBarButtons() falls back to tbDefaultImages[]. Must
     * NOT build tbImages[] from an unloaded st->tbButtons below -- a
     * bitmap.image over a NULL BITMAP_BitMap is a non-NULL Image object
     * that DrawImageState() will still try to blit from, and that's how a
     * minimal/partial style.txt used to crash. */
    BmImage_Init(&st->tbButtons, path);
    BmImage_Load(&st->tbButtons, scr);

    if (BmImage_IsLoaded(&st->tbButtons)) {
        bm = st->tbButtons.bitmap;

        /* tbbuttons.png is 2 columns (normal | selected) x FS3ESTYLE_TBBUTTON_COUNT
         * rows; derive the per-button cell size from the actual loaded image
         * instead of assuming a fixed pixel size. */
        cellW = (WORD)(st->tbButtons.width  / 2);
        cellH = (WORD)(st->tbButtons.height / FS3ESTYLE_TBBUTTON_COUNT);
        if (cellW < 1) cellW = 1;
        if (cellH < 1) cellH = 1;
        st->tbButtonWidth  = cellW;
        st->tbButtonHeight = cellH;

        /* tbbuttons.png has color index 0 marked transparent; the datatype
         * turns that into a proper mask plane (see PDTA_MaskPlane in
         * bmimage.c), matching the FULL loaded bitmap's coordinate space --
         * the same plane is reused for every cropped cell below.
         * BITMAP_Transparent is also set as a pen-0 chroma-key fallback for
         * the (unexpected) case where the datatype produced no mask. */
        for (i = 0; i < FS3ESTYLE_TBBUTTON_COUNT; i++) {
            st->tbImages[i] = (struct Image *)NewObject(BITMAP_GetClass(), NULL,
                BITMAP_BitMap,          (ULONG)bm,
                BITMAP_Width,           cellW,
                BITMAP_Height,          cellH,
                BITMAP_OffsetX,         0,
                BITMAP_OffsetY,         i * cellH,
                BITMAP_MaskPlane,       (ULONG)st->tbButtons.mask,
                BITMAP_SelectBitMap,    (ULONG)bm,
                BITMAP_SelectWidth,     cellW,
                BITMAP_SelectHeight,    cellH,
                BITMAP_SelectOffsetX,   cellW,
                BITMAP_SelectOffsetY,   i * cellH,
                BITMAP_SelectMaskPlane, (ULONG)st->tbButtons.mask,
                BITMAP_Masking,         TRUE,
                BITMAP_Transparent,     TRUE,
                TAG_DONE);
        }
    }

    /* Title bar background: tbbg.png, tiled by FS3EStyle_TitleBarBackFillFunc
     * (installed on TitleBarLayout's GA_BackFill at creation, see
     * friendsh3ep.c). Optional -- a missing file just means no custom
     * background, not a load failure. tbBgBitmap/Width/Height mirror
     * st->tbBg for FS3EStyle_TitleBarBackFillFunc -- see the note there. */
    snprintf(path, sizeof(path), "%s/%s", st->themePath,
             StyleFile_GetString(&sf, "titlebar.bg", "tbbg.png"));
    if (BmImage_Init(&st->tbBg, path) && BmImage_Load(&st->tbBg, scr)) {
        tbBgBitmap = st->tbBg.bitmap;
        tbBgWidth  = st->tbBg.width;
        tbBgHeight = st->tbBg.height;
    } else {
    }

    /* Title bar title/logo image: titlebar.title in style.txt, blitted
     * directly by TitleBarLayout_OnRender at (titlebarTitleX,
     * titlebarTitleY) offset from the title bar's top-left, masked on
     * color 0 (see BmImage_Load's PDTA_MaskPlane). Optional -- a missing
     * file just means no title image is drawn. */
    snprintf(path, sizeof(path), "%s/%s", st->themePath,
             StyleFile_GetString(&sf, "titlebar.title", "title.iff"));
    if (!BmImage_Init(&st->titlebarTitle, path) ||
        !BmImage_Load(&st->titlebarTitle, scr)) {
    }
    st->titlebarTitleX = (WORD)StyleFile_GetInt(&sf, "titlebar.title.x", 0);
    st->titlebarTitleY = (WORD)StyleFile_GetInt(&sf, "titlebar.title.y", 0);

    /* Title bar border images: titlebar.left/titlebar.right in style.txt,
     * blitted at the title bar's top-left/top-right corners by
     * FS3EStyle_TitleBarBackFillFunc (below) -- like tbBg, these are
     * conceptually part of the background, not something
     * TitleBarLayout_OnRender draws itself. No configurable offset (see
     * the titlebarLeft/titlebarRight doc comment in fs3estyle.h) --
     * optional, a missing file just means that border isn't drawn.
     * tbLeftBitmap/tbRightBitmap etc. mirror these for the hook the same
     * way tbBgBitmap mirrors tbBg above. */
    snprintf(path, sizeof(path), "%s/%s", st->themePath,
             StyleFile_GetString(&sf, "titlebar.left", "left.iff"));
    if (BmImage_Init(&st->titlebarLeft, path) &&
        BmImage_Load(&st->titlebarLeft, scr)) {
        tbLeftBitmap = st->titlebarLeft.bitmap;
        tbLeftMask   = st->titlebarLeft.mask;
        tbLeftWidth  = st->titlebarLeft.width;
        tbLeftHeight = st->titlebarLeft.height;
    }

    snprintf(path, sizeof(path), "%s/%s", st->themePath,
             StyleFile_GetString(&sf, "titlebar.right", "right.iff"));
    if (BmImage_Init(&st->titlebarRight, path) &&
        BmImage_Load(&st->titlebarRight, scr)) {
        tbRightBitmap = st->titlebarRight.bitmap;
        tbRightMask   = st->titlebarRight.mask;
        tbRightWidth  = st->titlebarRight.width;
        tbRightHeight = st->titlebarRight.height;
    }

    /* UniButtonP9 patch9 background skins: one Patch9 per
     * FS3ESTYLE_PATCH9_* slot (see patch9Slots[] above), each a 2
     * equal-size sub-image strip (PATCH9_NORMAL/SELECTED) arranged
     * horizontally. */
    for (i = 0; i < FS3ESTYLE_PATCH9_COUNT; i++)
        fs3estyle_load_patch9_slot(st, &sf, i, scr);

    /* UniButtonBGBM image-backed nav button skins: one full-opaque
     * background image per state, no masking. Optional per state -- a
     * gadget falls back to its own flat colour fill for whichever states
     * failed to load (see BmImage_IsLoaded in ubgbm_blit_state). */
    {
        static const char *btbgbmKeys[FS3ESTYLE_BTBGBM_COUNT] = {
            "btbgbm.bitmap.enabled",
            "btbgbm.bitmap.selected",
        };
        static const char *btbgbmDefaults[FS3ESTYLE_BTBGBM_COUNT] = {
            "tbbgenabled.jpg",
            "tbbgselected.jpg",
        };

        for (i = 0; i < FS3ESTYLE_BTBGBM_COUNT; i++) {
            snprintf(path, sizeof(path), "%s/%s", st->themePath,
                     StyleFile_GetString(&sf, btbgbmKeys[i], btbgbmDefaults[i]));
            if (!BmImage_Init(&st->btbgbmBitmap[i], path) ||
                !BmImage_Load(&st->btbgbmBitmap[i], scr)) {
            }
        }
    }

    /* UniButtonBGBM background render mode -- see the FS3EStyle.
     * btbgbmUsePatch9 doc comment in fs3estyle.h. */
    st->btbgbmUsePatch9 = StyleFile_GetInt(&sf, "btbgbm.usepatch9", 0) != 0;

    /* TootTimeline "waiting" screen image. Optional -- a missing file just
     * means the waiting screen shows its text alone (see ttl_render_wait
     * in TootTimeline/fs3etoottimeline_render.c). */
    snprintf(path, sizeof(path), "%s/%s", st->themePath,
             StyleFile_GetString(&sf, "timeline.waitimage", "waitimage.png"));
    if (!BmImage_Init(&st->waitImage, path) ||
        !BmImage_Load(&st->waitImage, scr)) {
    }

    /* Audio attachment placeholder -- see st->soundPlayImage's doc comment
     * in fs3estyle.h. RgbImage, not BmImage: no Init/scr step, and
     * RgbImage_LoadViaDatatype decodes via picture.datatype so any format
     * datatypes.library can open works, same as every other theme image
     * here (through BmImage's own picture.datatype use), just without the
     * screen remap. Optional -- a missing file just means the play-glyph
     * rect keeps being drawn instead (see RgbImage_IsLoaded's use in
     * ttl_render_tile). */
    snprintf(path, sizeof(path), "%s/%s", st->themePath,
             StyleFile_GetString(&sf, "media.soundplay", "soundplay.png"));
    RgbImage_LoadViaDatatype(&st->soundPlayImage, path);





    return TRUE;
}

void FS3EStyle_CreateDefaultButtonImages(FS3EStyle *st, struct Screen *scr)
{
    if (!st) return;
    FS3ETBDefaultBtn_Create(st->tbDefaultImages, scr);
}

/* Always acts, whether or not st->tbImages[N] is currently loaded --
 * unlike an earlier version of this function, which only ever attached an
 * image and left a button's GA_Image/BUTTON_Transparent/BUTTON_BevelStyle
 * untouched when there wasn't one. That left a button holding GA_Image
 * pointing at whatever FS3EStyle_UnloadThemeImages() had just disposed
 * the moment "no theme" was selected (see FS3EApp_LoadTheme in
 * friendsh3ep.c) -- a live dangling pointer on an already-open gadget.
 * When tbImages[N] is NULL, this now falls back to tbDefaultImages[N]
 * instead of a literal NULL GA_Image -- button.gadget does not tolerate
 * going from a real image back to NULL on an already-live gadget (see
 * fs3etbdefaultbtn.h). Only when tbDefaultImages[N] itself hasn't been
 * built yet (FS3EStyle_CreateDefaultButtonImages not called yet, or
 * images/penmap.image unavailable) does this fall all the way back to a
 * plain BVS_BUTTON bevel with no image, matching how these buttons render
 * before any image is ever attached (see their creation in friendsh3ep.c:
 * no GA_Image, no GA_Text). */
void FS3EStyle_SyncTitleBarButtons(FS3EStyle *st,
                                    Object *closeBtn, Object *iconifyBtn,
                                    Object *altposBtn, Object *depthBtn)
{
    struct Image *img;

    if (!st) return;

    if (closeBtn) {
        img = st->tbImages[0] ? st->tbImages[0] : st->tbDefaultImages[0];
        SetGdAttrs(closeBtn,
            GA_Image, (ULONG)img,
            BUTTON_BevelStyle, (ULONG)(img ? BVS_NONE : BVS_BUTTON),
            BUTTON_Transparent, (ULONG)(img ? TRUE : FALSE), TAG_DONE);
    }

    if (iconifyBtn) {
        img = st->tbImages[1] ? st->tbImages[1] : st->tbDefaultImages[1];
        SetGdAttrs(iconifyBtn,
            GA_Image, (ULONG)img,
            BUTTON_BevelStyle, (ULONG)(img ? BVS_NONE : BVS_BUTTON),
            BUTTON_Transparent, (ULONG)(img ? TRUE : FALSE), TAG_DONE);
    }

    if (altposBtn) {
        img = st->tbImages[2] ? st->tbImages[2] : st->tbDefaultImages[2];
        SetGdAttrs(altposBtn,
            GA_Image, (ULONG)img,
            BUTTON_BevelStyle, (ULONG)(img ? BVS_NONE : BVS_BUTTON),
            BUTTON_Transparent, (ULONG)(img ? TRUE : FALSE), TAG_DONE);
    }

    if (depthBtn) {
        img = st->tbImages[3] ? st->tbImages[3] : st->tbDefaultImages[3];
        SetGdAttrs(depthBtn,
            GA_Image, (ULONG)img,
            BUTTON_BevelStyle, (ULONG)(img ? BVS_NONE : BVS_BUTTON),
            BUTTON_Transparent, (ULONG)(img ? TRUE : FALSE), TAG_DONE);
    }
}

/* Push UBTP9_Patch9 onto accountBtn (FS3ESTYLE_PATCH9_BT1) and newtootBtn
 * (FS3ESTYLE_PATCH9_BT2) -- the two UniButtonP9 gadgets skinned from a
 * Patch9 slot instead of a plain GA_Image (see FS3EStyle_SyncTitleBarButtons
 * just above, for those). Always passes &st->patch9[slot] -- never NULL --
 * even when that slot's image isn't loaded: st->patch9[slot] is a plain
 * struct member of the one, stable FS3EStyle instance (not a separately
 * AllocVec'd/freed object), so the pointer itself is always valid for the
 * app's whole lifetime, and its bgcolors[]/textcolors[] are always
 * populated from style.txt regardless of whether the image loaded (see
 * fs3estyle_load_patch9_slot's doc comment) -- a button whose skin image
 * failed to load still needs the theme's own per-state colours for its
 * flat-fill fallback, not just some image or nothing. UniButtonP9's own
 * ubtp9_state_pens/ubtp9_blit_state (unibuttonp9_render.c/attribs.c)
 * already correctly test Patch9_IsLoaded(inst->patch9) to decide
 * image-vs-flat-fill, separately from the inst->patch9 NULL check that
 * only means "no Patch9 attached at all yet" -- confirmed bug this fixes:
 * passing 0 here instead collapsed those two distinct cases into one,
 * silently losing the theme's flat-fill colours whenever a skin image was
 * merely missing. Still called again on every theme (re)load or unload
 * (not just once at window-open time), since a button holding this same
 * pointer never notices *st->patch9[slot] changing underneath it on its
 * own, and still needs telling to redraw. Uses SetGdAttrs() (targets
 * CurrentMainWindow when open, plain SetAttrs() otherwise), same as
 * FS3EStyle_SyncTitleBarButtons. */
void FS3EStyle_SyncPatch9Buttons(FS3EStyle *st, Object *accountBtn, Object *newtootBtn)
{
    if (!st) return;

    if (accountBtn)
        SetGdAttrs(accountBtn, UBTP9_Patch9,
            (ULONG)&st->patch9[FS3ESTYLE_PATCH9_BT1], TAG_DONE);

    if (newtootBtn)
        SetGdAttrs(newtootBtn, UBTP9_Patch9,
            (ULONG)&st->patch9[FS3ESTYLE_PATCH9_BT2], TAG_DONE);
}

void FS3EStyle_UnloadThemeImages(FS3EStyle *st)
{
    int i;
    if (!st) return;
    free_tb_images(st);
    BmImage_Unload(&st->tbButtons);
    BmImage_Unload(&st->tbBg);
    BmImage_Unload(&st->titlebarTitle);
    BmImage_Unload(&st->titlebarLeft);
    BmImage_Unload(&st->titlebarRight);
    for (i = 0; i < FS3ESTYLE_PATCH9_COUNT; i++)
        Patch9_Unload(&st->patch9[i]);
    for (i = 0; i < FS3ESTYLE_BTBGBM_COUNT; i++)
        BmImage_Unload(&st->btbgbmBitmap[i]);
    BmImage_Unload(&st->waitImage);
    /* RgbImage has no Unload/Init split like BmImage (it's never
     * screen-remapped in the first place) -- Free() is the only release,
     * called here too so a theme switch drops the old image the same way
     * every other slot above does. */
    RgbImage_Free(&st->soundPlayImage);
    st->btbgbmUsePatch9 = FALSE;
    tbBgBitmap = NULL;
    tbBgWidth  = 0;
    tbBgHeight = 0;
    tbLeftBitmap  = NULL;
    tbLeftMask    = NULL;
    tbLeftWidth   = 0;
    tbLeftHeight  = 0;
    tbRightBitmap = NULL;
    tbRightMask   = NULL;
    tbRightWidth  = 0;
    tbRightHeight = 0;
    /* No image loaded -- TitleBarLayout must fall back to dpiHeight sizing. */
    st->tbButtonWidth  = 0;
    st->tbButtonHeight = 0;
}

void FS3EStyle_FreeThemeImages(FS3EStyle *st)
{
    int i;
    if (!st) return;
    FS3EStyle_UnloadThemeImages(st);
    /* tbDefaultImages[] (the built-in title bar glyphs from
     * FS3EStyle_CreateDefaultButtonImages) are NOT touched by
     * FS3EStyle_UnloadThemeImages() above -- they must survive every theme
     * switch and only go away here, at final app exit. */
    FS3ETBDefaultBtn_Dispose(st->tbDefaultImages);
    BmImage_Free(&st->tbButtons);
    BmImage_Free(&st->tbBg);
    BmImage_Free(&st->titlebarTitle);
    BmImage_Free(&st->titlebarLeft);
    BmImage_Free(&st->titlebarRight);
    for (i = 0; i < FS3ESTYLE_PATCH9_COUNT; i++)
        Patch9_Free(&st->patch9[i]);
    for (i = 0; i < FS3ESTYLE_BTBGBM_COUNT; i++)
        BmImage_Free(&st->btbgbmBitmap[i]);
    BmImage_Free(&st->waitImage);
    if (st->themePath) { FreeVec(st->themePath); st->themePath = NULL; }
}
