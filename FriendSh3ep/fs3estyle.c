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
#include "patch9.h"
#include "fs3eboopsimainwindow.h"
#include "stylefile.h"
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

    0x00000000,   /* FS3E_COLOR_BTBGBM_TEXT_ENABLED */
    0x00FFFFFF,   /* FS3E_COLOR_BTBGBM_TEXT_SELECTED */
    0x00EEBB33,   /* FS3E_COLOR_BTBGBM_TEXT_HOVER */
    0x00999999,   /* FS3E_COLOR_BTBGBM_TEXT_DISABLED */
};

/* style.txt key for each FS3EColorRole, in the same order as the enum --
 * see themes/mouton/style.txt. FS3EStyle_LoadThemeImages() looks each of
 * these up and overrides the matching defaultColors[] entry when present. */
static const char *colorStyleKeys[FS3E_COLOR_COUNT] = {
    "timeline.color.buttonbg",
    "timeline.color.button_selectedbg",
    "timeline.color.timelinebg",
    "timeline.color.username",
    "timeline.color.hashtag",
    "timeline.color.accent",
    "timeline.color.text",
    "timeline.color.text_dim",
    "timeline.color.action_text",

    "btbgbm.textcolor.enabled",
    "btbgbm.textcolor.selected",
    "btbgbm.textcolor.hover",
    "btbgbm.textcolor.disabled",
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

/* One style.txt-driven Patch9 skin slot: "<prefix>.image" / "<prefix>.cornersize",
 * plus per-state bg/text colour defaults for "<prefix>.bgcolor.<suffix>" /
 * "<prefix>.textcolor.<suffix>" (see patch9StateSuffix[] below). Arrays are
 * indexed by PATCH9_NORMAL/SELECTED/DISABLED/HOVER, same as Patch9.bgcolors/
 * textcolors themselves. */
typedef struct {
    const char *prefix;
    const char *defaultFile;
    WORD        defaultCorner;
    ULONG       defaultBgColor[PATCH9_NUM_STATES];
    ULONG       defaultTextColor[PATCH9_NUM_STATES];
} FS3EPatch9SlotDef;

static const FS3EPatch9SlotDef patch9Slots[FS3ESTYLE_PATCH9_COUNT] = {
    { "bt1Patch9", "bt1patch9.iff", 8,
      { 0x007B7BE3, 0x00BCBCFF, 0x00777777, 0x007B7BE3 },
      { 0x00CCCCCC, 0x00FFFFFF, 0x00999999, 0x00EEEE44 } },
    { "bt2Patch9", "bt2patch9.iff", 8,
      { 0x0083ED60, 0x00B2FFC8, 0x00777777, 0x0083ED60 },
      { 0x00CCCCCC, 0x00FFFFFF, 0x00999999, 0x00EEEE44 } },
};

/* style.txt suffix for each PATCH9_* state, e.g. "bt1Patch9.bgcolor.hover" --
 * see themes/mouton/style.txt. */
static const char *patch9StateSuffix[PATCH9_NUM_STATES] = {
    "enabled",   /* PATCH9_NORMAL */
    "selected",  /* PATCH9_SELECTED */
    "disabled",  /* PATCH9_DISABLED */
    "hover",     /* PATCH9_HOVER */
};

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
        printf("FS3EStyle_LoadThemeImages: %s image not loaded (%s)\n",
               def->prefix, path);
    }

    for (state = 0; state < PATCH9_NUM_STATES; state++) {
        snprintf(key, sizeof(key), "%s.bgcolor.%s", def->prefix, patch9StateSuffix[state]);
        p9->bgcolors[state].rgbcolor = StyleFile_GetColor(sf, key, def->defaultBgColor[state]);

        snprintf(key, sizeof(key), "%s.textcolor.%s", def->prefix, patch9StateSuffix[state]);
        p9->textcolors[state].rgbcolor = StyleFile_GetColor(sf, key, def->defaultTextColor[state]);
    }
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

    if (!st->themePath) FS3EStyle_SetThemePath(st, NULL);

    /* style.txt names the actual asset files/metrics below -- see
     * themes/mouton/style.txt. Missing file/keys just fall back to the
     * names this function always used, so this is safe unconditionally. */
    snprintf(stylePath, sizeof(stylePath), "%s/style.txt", st->themePath);
    StyleFile_Load(&sf, stylePath);

    /* Override every FS3EColorRole's RGB value from its style.txt key (see
     * colorStyleKeys[] above), falling back to whatever rgbcolor already
     * holds (the FS3EStyle_InitDefaults() value, or a previous load's) if
     * the file or key is missing. Must run before FS3EStyle_ApplyColors(),
     * which resolves rgbcolor -> screen pen for every role -- the caller
     * (GenericOpenWindow) already calls them in that order. */
    for (i = 0; i < FS3E_COLOR_COUNT; i++)
        st->colors[i].rgbcolor = StyleFile_GetColor(&sf, colorStyleKeys[i],
                                                    st->colors[i].rgbcolor);

    if (!BitMapBase) {
        printf("FS3EStyle_LoadThemeImages: images/bitmap.image not open, skipping\n");
        return FALSE;
    }

    snprintf(path, sizeof(path), "%s/%s", st->themePath,
             StyleFile_GetString(&sf, "titlebar.buttons", "tbbuttons.iff"));

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
    snprintf(path, sizeof(path), "%s/%s", st->themePath,
             StyleFile_GetString(&sf, "titlebar.bg", "tbbg.png"));
    if (BmImage_Init(&st->tbBg, path) && BmImage_Load(&st->tbBg, scr)) {
        tbBgBitmap = st->tbBg.bitmap;
        tbBgWidth  = st->tbBg.width;
        tbBgHeight = st->tbBg.height;
    } else {
        printf("FS3EStyle_LoadThemeImages: %s not loaded (%s)\n",
               StyleFile_GetString(&sf, "titlebar.bg", "tbbg.png"), path);
    }

    /* UniButtonP9 patch9 background skins: one Patch9 per
     * FS3ESTYLE_PATCH9_* slot (see patch9Slots[] above), each a 4
     * equal-size sub-image strip (PATCH9_NORMAL/SELECTED/DISABLED/HOVER)
     * arranged horizontally. */
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
            "btbgbm.bitmap.disabled",
        };
        static const char *btbgbmDefaults[FS3ESTYLE_BTBGBM_COUNT] = {
            "tbbgenabled.jpg",
            "tbbgselected.jpg",
            "tbbgdisabled.jpg",
        };

        for (i = 0; i < FS3ESTYLE_BTBGBM_COUNT; i++) {
            snprintf(path, sizeof(path), "%s/%s", st->themePath,
                     StyleFile_GetString(&sf, btbgbmKeys[i], btbgbmDefaults[i]));
            if (!BmImage_Init(&st->btbgbmBitmap[i], path) ||
                !BmImage_Load(&st->btbgbmBitmap[i], scr)) {
                printf("FS3EStyle_LoadThemeImages: btbgbm bitmap not loaded (%s)\n", path);
            }
        }
    }

    /* TootTimeline "waiting" screen image. Optional -- a missing file just
     * means the waiting screen shows its text alone (see ttl_render_wait
     * in TootTimeline/fs3etoottimeline_render.c). */
    snprintf(path, sizeof(path), "%s/%s", st->themePath,
             StyleFile_GetString(&sf, "timeline.waitimage", "waitimage.png"));
    if (!BmImage_Init(&st->waitImage, path) ||
        !BmImage_Load(&st->waitImage, scr)) {
        printf("FS3EStyle_LoadThemeImages: timeline.waitimage not loaded (%s)\n", path);
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
    int i;
    if (!st) return;
    free_tb_images(st);
    BmImage_Unload(&st->tbButtons);
    BmImage_Unload(&st->tbBg);
    for (i = 0; i < FS3ESTYLE_PATCH9_COUNT; i++)
        Patch9_Unload(&st->patch9[i]);
    for (i = 0; i < FS3ESTYLE_BTBGBM_COUNT; i++)
        BmImage_Unload(&st->btbgbmBitmap[i]);
    BmImage_Unload(&st->waitImage);
    tbBgBitmap = NULL;
    tbBgWidth  = 0;
    tbBgHeight = 0;
    /* No image loaded -- TitleBarLayout must fall back to dpiHeight sizing. */
    st->tbButtonWidth  = 0;
    st->tbButtonHeight = 0;
}

void FS3EStyle_FreeThemeImages(FS3EStyle *st)
{
    int i;
    if (!st) return;
    FS3EStyle_UnloadThemeImages(st);
    BmImage_Free(&st->tbButtons);
    BmImage_Free(&st->tbBg);
    for (i = 0; i < FS3ESTYLE_PATCH9_COUNT; i++)
        Patch9_Free(&st->patch9[i]);
    for (i = 0; i < FS3ESTYLE_BTBGBM_COUNT; i++)
        BmImage_Free(&st->btbgbmBitmap[i]);
    BmImage_Free(&st->waitImage);
    if (st->themePath) { FreeVec(st->themePath); st->themePath = NULL; }
}
