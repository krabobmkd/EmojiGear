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
#include "fs3eboopsimainwindow.h"
#include "bdbprintf.h"
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
 *   avatarSize  = lineH × 2.5  (1.25× the old dpiHeight×2 formula)
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
    st->postPadLeft = (WORD)(lineH / 3 + 2);
    st->avatarGap   = (WORD)(lineH / 3 + 2);

    printf("lineH:%d avatarGap:%d\n",lineH,st->avatarGap);
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

    st->dcNormal   = make_dc("fs3e-normal",   12, 12, URP_STYLE_NORMAL);
    st->dcUsername = make_dc("fs3e-username", 13, 13, URP_STYLE_BOLD);
    st->dcMini     = make_dc("fs3e-mini",      10,  10, URP_STYLE_NORMAL);

    compute_layout(st);

    /* Image theme: default path only; assets are loaded once a screen is
     * available (FS3EStyle_LoadThemeImages, called from GenericOpenWindow). */
    memset(&st->tbButtons, 0, sizeof(st->tbButtons));
    memset(st->tbImages, 0, sizeof(st->tbImages));
    st->tbButtonWidth  = 0;
    st->tbButtonHeight = 0;
    st->themePath = NULL;
    FS3EStyle_SetThemePath(st, NULL);

    memset(&st->tbBg, 0, sizeof(st->tbBg));
    st->tbBgHook.h_Entry    = (HOOKFUNC)FS3EStyle_TitleBarBackFillFunc;
    st->tbBgHook.h_SubEntry = NULL;
    st->tbBgHook.h_Data     = NULL;  /* unused -- see FS3EStyle_TitleBarBackFillFunc */

    memset(&st->bt1Patch9, 0, sizeof(st->bt1Patch9));
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

/* ------------------------------------------------------------------ */
/* Image theme                                                          */
/* ------------------------------------------------------------------ */

static void free_tb_images(FS3EStyle *st)
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

/* Tile tbBgBitmap across bfm->bf_Bounds. Verified against the working
 * reference (amigapetmate's BackFillHook_Pattern): BltBitMap operates on
 * the raw RastPort BitMap directly and is unaffected by Layer clipping, so
 * (unlike a RastPort-drawing-function hook) there is no need to copy rp and
 * null out its Layer field here. */
static void ASM SAVEDS FS3EStyle_TitleBarBackFillFunc(
    REG(a2, struct RastPort *rp),
    REG(a1, struct FS3EBackFillMsg *bfm))
{
    struct BitMap *dst;
    LONG dstX, dstY, dstW, dstH;
    LONG srcStartX, srcStartY;
    LONG x, y, srcX, srcY, chunkW, chunkH;

    if (!tbBgBitmap || tbBgWidth <= 0 || tbBgHeight <= 0) return;

    dst  = rp->BitMap;
    dstX = (LONG)bfm->bf_Bounds.MinX;
    dstY = (LONG)bfm->bf_Bounds.MinY;
    dstW = (LONG)bfm->bf_Bounds.MaxX - dstX + 1;
    dstH = (LONG)bfm->bf_Bounds.MaxY - dstY + 1;
    if (dstW <= 0 || dstH <= 0) return;

    /* Phase of the pattern at the top-left of the damage rect, anchored to
     * the layer's own origin so tiles stay aligned across separate
     * (partial) backfill calls and when the window moves. */
    srcStartX = (((dstX - rp->Layer->bounds.MinX) % tbBgWidth)  + tbBgWidth)  % tbBgWidth;
    srcStartY = (((dstY - rp->Layer->bounds.MinY) % tbBgHeight) + tbBgHeight) % tbBgHeight;

    y = 0;
    while (y < dstH) {
        srcY   = (srcStartY + y) % tbBgHeight;
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
        y += chunkH;
    }
}

void FS3EStyle_SetThemePath(FS3EStyle *st, const char *path)
{
    ULONG len;
    char *copy;

    if (!st) return;

    if (!path || path[0] == '\0')
        path = FS3ESTYLE_THEME_DEFAULT_PATH;

    len  = (ULONG)strlen(path) + 1;
    copy = (char *)AllocVec(len, MEMF_ANY);
    if (!copy) return;
    strcpy(copy, path);

    if (st->themePath) FreeVec(st->themePath);
    st->themePath = copy;
}

BOOL FS3EStyle_LoadThemeImages(FS3EStyle *st, struct Screen *scr)
{
    char path[256];
    struct BitMap *bm;
    WORD cellW, cellH;
    int i;

    if (!st) return FALSE;

    FS3EStyle_UnloadThemeImages(st);

    if (!st->themePath) FS3EStyle_SetThemePath(st, NULL);

    if (!BitMapBase) {
        printf("FS3EStyle_LoadThemeImages: images/bitmap.image not open, skipping\n");
        return FALSE;
    }

    snprintf(path, sizeof(path), "%s/tbbuttons.iff", st->themePath);

    if (!BmImage_Init(&st->tbButtons, path)) {
        printf("FS3EStyle_LoadThemeImages: BmImage_Init failed for %s\n", path);
        return FALSE;
    }
    if (!BmImage_Load(&st->tbButtons, scr)) {
        printf("FS3EStyle_LoadThemeImages: BmImage_Load failed for %s (error %d)\n",
               path, (int)st->tbButtons.error);
        return FALSE;
    }

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

    /* Title bar background: tbbg.png, tiled by FS3EStyle_TitleBarBackFillFunc
     * (installed on TitleBarLayout's GA_BackFill at creation, see
     * friendsh3ep.c). Optional -- a missing file just means no custom
     * background, not a load failure. tbBgBitmap/Width/Height mirror
     * st->tbBg for FS3EStyle_TitleBarBackFillFunc -- see the note there. */
    snprintf(path, sizeof(path), "%s/tbbg.png", st->themePath);
    if (BmImage_Init(&st->tbBg, path) && BmImage_Load(&st->tbBg, scr)) {
        tbBgBitmap = st->tbBg.bitmap;
        tbBgWidth  = st->tbBg.width;
        tbBgHeight = st->tbBg.height;
    } else {
        printf("FS3EStyle_LoadThemeImages: tbbg.png not loaded (%s)\n", path);
    }

    /* UniButtonP9 patch9 background skin: 96x24, 4 sub-images of 24x24
     * (PATCH9_NORMAL/SELECTED/DISABLED/HOVER), corner size 8. Optional --
     * a missing file just means buttons keep their flat colour fill. */
    snprintf(path, sizeof(path), "%s/bt1patch9.iff", st->themePath);
    if (!Patch9_Init(&st->bt1Patch9, path, 8) ||
        !Patch9_Load(&st->bt1Patch9, scr)) {
        printf("FS3EStyle_LoadThemeImages: bt1patch9.iff not loaded (%s)\n", path);
    }

    return TRUE;
}

void FS3EStyle_SyncTitleBarButtons(FS3EStyle *st,
                                    Object *closeBtn, Object *iconifyBtn,
                                    Object *altposBtn, Object *depthBtn)
{
    if (!st) return;

    if (closeBtn && st->tbImages[0])
        SetGdAttrs(closeBtn,
            GA_Image, (ULONG)st->tbImages[0],
            BUTTON_BevelStyle, BVS_NONE,
            BUTTON_Transparent, TRUE, TAG_DONE);

    if (iconifyBtn && st->tbImages[1])
        SetGdAttrs(iconifyBtn,
            GA_Image, (ULONG)st->tbImages[1],
            BUTTON_BevelStyle, BVS_NONE,
            BUTTON_Transparent, TRUE, TAG_DONE);

    if (altposBtn && st->tbImages[2])
        SetGdAttrs(altposBtn,
            GA_Image, (ULONG)st->tbImages[2],
            BUTTON_BevelStyle, BVS_NONE,
            BUTTON_Transparent, TRUE, TAG_DONE);

    if (depthBtn && st->tbImages[3])
        SetGdAttrs(depthBtn,
            GA_Image, (ULONG)st->tbImages[3],
            BUTTON_BevelStyle, BVS_NONE,
            BUTTON_Transparent, TRUE, TAG_DONE);


}

void FS3EStyle_UnloadThemeImages(FS3EStyle *st)
{
    if (!st) return;
    free_tb_images(st);
    BmImage_Unload(&st->tbButtons);
    BmImage_Unload(&st->tbBg);
    Patch9_Unload(&st->bt1Patch9);
    tbBgBitmap = NULL;
    tbBgWidth  = 0;
    tbBgHeight = 0;
    /* No image loaded -- TitleBarLayout must fall back to dpiHeight sizing. */
    st->tbButtonWidth  = 0;
    st->tbButtonHeight = 0;
}

void FS3EStyle_FreeThemeImages(FS3EStyle *st)
{
    if (!st) return;
    FS3EStyle_UnloadThemeImages(st);
    BmImage_Free(&st->tbButtons);
    BmImage_Free(&st->tbBg);
    Patch9_Free(&st->bt1Patch9);
    if (st->themePath) { FreeVec(st->themePath); st->themePath = NULL; }
}
