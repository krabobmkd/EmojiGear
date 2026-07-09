#ifndef FS3ESTYLE_H
#define FS3ESTYLE_H

/*
 * fs3estyle — FriendSh3ep color theme system.
 *
 * Each color role is defined by its RGB value (the design intent) and
 * tracked at runtime as an Amiga pen index.  On indexed screens (ECS/AGA)
 * ObtainBestPenA reserves a palette entry; on RTG screens with many colors
 * it usually succeeds immediately.  If allocation fails, FindColor returns
 * the closest existing entry.
 *
 * Usage
 * -----
 *   FS3EStyle_InitDefaults(&fs3eStyle);          // set RGB values + pen=1
 *   FS3EStyle_ApplyColors(&fs3eStyle, screen);   // obtain pens from ColorMap
 *   ... draw using FS3E_PEN(&fs3eStyle, FS3E_COLOR_TEXT) ...
 *   FS3EStyle_ApplyColors(&fs3eStyle, screen);   // theme change: re-applies
 *   FS3EStyle_ReleasePens(&fs3eStyle);           // at exit or screen close
 *
 * Color values are 0x00RRGGBB (alpha byte ignored).
 */

#include <exec/types.h>
#include <intuition/screens.h>
#include <intuition/classusr.h>
#include <libraries/utf8rastport.h>
#include <utility/hooks.h>

#include "bmimage.h"

/* ------------------------------------------------------------------ */
/* FS3EManagedColor — one color role: RGB definition + runtime pen     */
/* ------------------------------------------------------------------ */

typedef struct {
    ULONG rgbcolor;   /* 0x00RRGGBB — theme definition value */
    WORD  pen;        /* pen index for SetAPen/SetRPAttrs; 1 until ApplyColors */
    WORD  allocated;  /* 1 = obtained via ObtainBestPenA; 0 = FindColor result */
} FS3EManagedColor;

#define PATCH9_NORMAL     0
#define PATCH9_SELECTED   1
#define PATCH9_DISABLED   2
#define PATCH9_HOVER      3
#define PATCH9_NUM_STATES 4

typedef struct Patch9 {
    BmImage img;
    WORD    cornerSize;   /* pixels kept unscaled at each corner */
    WORD    patchWidth;   /* one sub-image's pixel width  (img.width / PATCH9_NUM_STATES) */
    WORD    patchHeight;  /* one sub-image's pixel height (== img.height) */

    FS3EManagedColor bgcolors[PATCH9_NUM_STATES]; /* fallback background pens for the 4 states */
    FS3EManagedColor textcolors[PATCH9_NUM_STATES]; /* background pens for the 4 states */
} Patch9;


/* ------------------------------------------------------------------ */
/* Color role enumeration                                               */
/* ------------------------------------------------------------------ */

/*
 * Every role here has a default RGB value (colorDefaults[] in fs3estyle.c)
 * and a style.txt key (colorStyleKeys[] in fs3estyle.c) -- see
 * themes/mouton/style.txt. FS3EStyle_LoadThemeImages() overrides each
 * role's rgbcolor from its style.txt key (falling back to the default if
 * the file or key is missing) before FS3EStyle_ApplyColors() resolves
 * every role to a screen pen. Adding a role means adding both a
 * colorDefaults[] entry and a colorStyleKeys[] entry, in the same order.
 */
typedef enum {
    FS3E_COLOR_BUTTON_BG = 0,       /* normal button / nav-bar button background */
    FS3E_COLOR_BUTTON_SELECTED_BG,  /* pressed / active button background */
    FS3E_COLOR_TIMELINE_BG,         /* TootTimeline empty background */
    FS3E_COLOR_USERNAME,            /* display name of the poster */
    FS3E_COLOR_HASHTAG,             /* clickable-link highlight in post body: hashtags, mentions, URLs */
    FS3E_COLOR_ACCENT,              /* separators, borders, avatar placeholder, resize grip */
    FS3E_COLOR_TEXT,                /* main body text */
    FS3E_COLOR_TEXT_DIM,            /* secondary text: @acct, timestamps */
    FS3E_COLOR_ACTION_TEXT,         /* Reply / Boost / Fave button labels */

    /* UniButtonBGBM image-backed nav buttons (btbgbm.* in style.txt) --
     * not wired to any gadget yet, see FS3EStyle_LoadThemeImages(). */
    FS3E_COLOR_BTBGBM_TEXT_ENABLED,
    FS3E_COLOR_BTBGBM_TEXT_SELECTED,
    FS3E_COLOR_BTBGBM_TEXT_HOVER,
    FS3E_COLOR_BTBGBM_TEXT_DISABLED,

    FS3E_COLOR_COUNT                /* must be last */
} FS3EColorRole;

/* ------------------------------------------------------------------ */
/* Image theme — tbbuttons.png: 2 columns (normal | selected) x 4 rows */
/* (close, iconify, altpos, depth) of 8x8 pixel title bar glyphs.      */
/* ------------------------------------------------------------------ */

/* Reached from the build dir: PROGDIR: is the directory the binary was
 * launched from. */
#define FS3ESTYLE_THEME_DEFAULT_PATH "PROGDIR:/themes/mouton"

#define FS3ESTYLE_TBBUTTON_COUNT 4

/* UniButtonP9 Patch9 skin slots -- each one is a "<prefix>.image" /
 * "<prefix>.cornersize" pair in style.txt (see FS3EStyle_LoadThemeImages'
 * patch9Slots[] table in fs3estyle.c), loaded into st->patch9[slot].
 * Add a new skin by adding both a slot constant here and a patch9Slots[]
 * entry there, in the same order. */
#define FS3ESTYLE_PATCH9_BT1  0
#define FS3ESTYLE_PATCH9_BT2  1
#define FS3ESTYLE_PATCH9_COUNT 2

/* UniButtonBGBM image-backed nav button states (btbgbm.bitmap.* in
 * style.txt) -- same order as UBGBM_STATE_NORMAL/SELECTED/DISABLED in
 * UniButtonBGBM/unibuttonbgbm_private.h, so a gadget's state index can be
 * used directly as st->btbgbmBitmap[state]. No hover slot -- no
 * hover-tracking input handling exists for UniButtonBGBM either. */
#define FS3ESTYLE_BTBGBM_ENABLED  0
#define FS3ESTYLE_BTBGBM_SELECTED 1
#define FS3ESTYLE_BTBGBM_DISABLED 2
#define FS3ESTYLE_BTBGBM_COUNT    3

/* ------------------------------------------------------------------ */
/* FS3EStyle — the complete runtime color theme                        */
/* ------------------------------------------------------------------ */

typedef struct {
    FS3EManagedColor colors[FS3E_COLOR_COUNT];
    struct Screen   *screen;  /* screen whose ColorMap currently owns the pens;
                               * NULL means no pens are allocated */

    /* Draw contexts — one per font role.  Created in FS3EStyle_InitDefaults,
     * released in FS3EStyle_ReleaseDrawContexts.  Not screen-dependent. */
    struct URPDrawContext *dcNormal;    /* body text   : 12 pt regular */
    struct URPDrawContext *dcUsername;  /* display names: 13 pt bold   */
    struct URPDrawContext *dcMini;      /* timestamps, acct: 9 pt      */

    /* Post layout metrics derived from dcNormal line height.
     * Updated by FS3EStyle_InitDefaults and FS3EStyle_SetFontSize.
     * TootTimeline reads these instead of using hardcoded constants. */
    WORD avatarSize;   /* square avatar side in pixels (width = height) */
    WORD postPadLeft;  /* gap from gadget left edge to avatar left edge  */
    WORD avatarGap;    /* gap from avatar right edge to text column left  */

    /* Image theme: a directory of asset files (PNG, loaded via bmimage.c)
     * referenced by themePath.  Mirrors the pens' per-screen lifecycle:
     * LoadThemeImages/UnloadThemeImages pair with ApplyColors/ReleasePens. */
    char    *themePath;    /* AllocVec'd theme directory, e.g. "PROGDIR:themes/mouton" */
    BmImage  tbButtons;    /* tbbuttons.png source bitmap, remapped to the current screen */

    /* bitmap.image objects, one per title bar button, in GID_TITLEBAR_CLOSE,
     * ICONIFY, ALTPOS, DEPTH order.  Each one encodes both the normal and
     * selected sub-images (BITMAP_BitMap/BITMAP_SelectBitMap), so it is the
     * only image button.gadget needs (assigned to GA_Image). */
    struct Image *tbImages[FS3ESTYLE_TBBUTTON_COUNT];

    /* Title bar button cell size, derived from tbbuttons.png as
     * (width / 2, height / FS3ESTYLE_TBBUTTON_COUNT) by
     * FS3EStyle_LoadThemeImages.  0 when no theme image is loaded --
     * TitleBarLayout must fall back to its own dpiHeight-based square size
     * in that case. */
    WORD tbButtonWidth;
    WORD tbButtonHeight;

    /* Title bar background: tbbg.png, tiled across the title bar by
     * tbBgHook. Pass &style->tbBgHook as LAYOUT_BackFill when creating
     * TitleBarLayout (see friendsh3ep.c) -- GA_BackFill is applicability
     * (OM_NEW) only, so it must be installed at creation time, before the
     * image itself is loaded; the hook function reads the current loaded
     * state each time it runs, so it stays correct across theme reloads.
     * h_Entry set up once in FS3EStyle_InitDefaults; h_Data is unused (see
     * FS3EStyle_TitleBarBackFillFunc in fs3estyle.c for why). */
    BmImage     tbBg;
    struct Hook tbBgHook;

    /* Title bar title/logo image: titlebar.title in style.txt, blitted
     * directly onto the RastPort by TitleBarLayout_OnRender, after the
     * EraseRect backfill and before children are rendered. Color 0 in the
     * source is transparent (masked blit via BmImage's PDTA_MaskPlane, same
     * as tbButtons). Positioned at (titlebarTitleX, titlebarTitleY) offset
     * from the title bar's LeftEdge/TopEdge -- titlebar.title.x /
     * titlebar.title.y in style.txt. Optional -- a missing file just means
     * no title image is drawn (see BmImage_IsLoaded). */
    BmImage titlebarTitle;
    WORD    titlebarTitleX;
    WORD    titlebarTitleY;

    /* UniButtonP9 patch9 background skins, one per FS3ESTYLE_PATCH9_* slot
     * (bt1Patch9.x / bt2Patch9.x in style.txt) -- each a 4-sub-image strip
     * (PATCH9_NORMAL/SELECTED/DISABLED/HOVER, left to right). Loaded from
     * st->themePath like the other theme images. Optional per slot -- if a
     * slot's file is missing, buttons that reference &st->patch9[slot] via
     * UBTP9_Patch9 just fall back to their flat colour fill (see
     * Patch9_IsLoaded in patch9.h). */
    Patch9 patch9[FS3ESTYLE_PATCH9_COUNT];

    /* UniButtonBGBM image-backed nav button skins (btbgbm.bitmap.* in
     * style.txt), one full-opaque (no transparency/masking) background
     * image per state -- FS3ESTYLE_BTBGBM_ENABLED/SELECTED/DISABLED.
     * Optional per state -- a gadget referencing &st->btbgbmBitmap[state]
     * via UBGBM_Style just falls back to its flat colour fill for any
     * state whose image failed to load (see BmImage_IsLoaded). */
    BmImage btbgbmBitmap[FS3ESTYLE_BTBGBM_COUNT];

    /* TootTimeline "waiting" screen image (timeline.waitimage in
     * style.txt), shown centered instead of the scrollable post list --
     * see TTIMELINE_ViewMode in TootTimeline/fs3etoottimeline.h. Optional --
     * if missing, the timeline just draws the waiting text alone. */
    BmImage waitImage;
} FS3EStyle;

/* ------------------------------------------------------------------ */
/* API                                                                  */
/* ------------------------------------------------------------------ */

/* Set all rgbcolor fields to the built-in dark theme defaults.
 * Does not touch pens — safe to call before any screen is available. */
void FS3EStyle_InitDefaults(FS3EStyle *st);

/* Obtain Amiga pens for every color role from scr->ViewPort.ColorMap.
 * Releases any previously held pens first, so safe to call on theme change
 * or when switching screens. */
void FS3EStyle_ApplyColors(FS3EStyle *st, struct Screen *scr);

/* Release all ObtainBestPenA-allocated pens back to the ColorMap.
 * Call before closing the screen or at program exit. */
void FS3EStyle_ReleasePens(FS3EStyle *st);

/* Convenience accessor: pen index for a given role */
#define FS3E_PEN(st, role)  ((st)->colors[(role)].pen)

/* Release the three URPDrawContexts.  Call before program exit,
 * after all gadgets that use them have been disposed. */
void FS3EStyle_ReleaseDrawContexts(FS3EStyle *st);

/*
 * Resize the three draw contexts proportionally from a base point size:
 *   dcNormal   = baseSize       (body text)
 *   dcUsername = baseSize + 1   (display names, bold)
 *   dcMini     = max(baseSize - 2, 7)  (timestamps, acct — always smaller)
 *
 * Font paths fall back to the InitDefaults hardcoded names when NULL.
 * Style bits (BOLD on dcUsername) and pref flags are preserved from init.
 * Call after changing app->settings.fontPointSize; the caller must then
 * re-set TTIMELINE_Style on the toot-timeline to refresh cached metrics.
 */
void FS3EStyle_SetFontSize(FS3EStyle *st, int baseSize,
                           const char *primary,
                           const char *fallback1, const char *fallback2,
                           const char *emoji);

/* ------------------------------------------------------------------ */
/* Image theme API                                                     */
/* ------------------------------------------------------------------ */

/* Set the theme directory. path == NULL resets to the built-in default
 * (FS3ESTYLE_THEME_DEFAULT_PATH). Copies the string; frees any previously
 * held themePath. Does not load anything -- call FS3EStyle_LoadThemeImages()
 * afterwards. */
void FS3EStyle_SetThemePath(FS3EStyle *st, const char *path);

/* Load title bar button images (tbbuttons.png), tbbg.png, every
 * patch9[FS3ESTYLE_PATCH9_*] skin, and the btbgbmBitmap[] nav button skins
 * from st->themePath, remapped to scr (see bmimage.c / patch9.c). Also
 * overrides every FS3EColorRole's RGB
 * value from style.txt (see colorStyleKeys[] in fs3estyle.c) -- call this
 * before FS3EStyle_ApplyColors() so the override takes effect. Call once
 * per screen open. Safe to call again on theme or screen change -- disposes
 * previously loaded images first. If st->themePath is unset, defaults it
 * first.
 * Returns FALSE if images/bitmap.image isn't open, or tbbuttons.png could
 * not be loaded -- st->tbImages[] stay NULL and
 * FS3EStyle_SyncTitleBarButtons() becomes a no-op, leaving gadgets with
 * whatever image they had before. */
BOOL FS3EStyle_LoadThemeImages(FS3EStyle *st, struct Screen *scr);

/* Push st->tbImages[] (GA_Image) plus BUTTON_Transparent onto the four
 * title bar button.gadget objects, in GID_TITLEBAR_CLOSE, ICONIFY, ALTPOS,
 * DEPTH order. Any gadget pointer that is NULL, or whose matching image
 * failed to load, is left untouched. Uses SetGdAttrs() (see
 * fs3eboopsimainwindow.h), which targets CurrentMainWindow when open or
 * falls back to a plain SetAttrs() otherwise. */
void FS3EStyle_SyncTitleBarButtons(FS3EStyle *st,
                                    Object *closeBtn, Object *iconifyBtn,
                                    Object *altposBtn, Object *depthBtn);

/* Dispose the title bar button images and unload (but keep the path of) the
 * source bitmap. Call before closing/iconifying the screen, alongside
 * FS3EStyle_ReleasePens. Note: button.gadget does not take ownership of
 * GA_Image, so these must be disposed explicitly once nothing references
 * them anymore. */
void FS3EStyle_UnloadThemeImages(FS3EStyle *st);

/* Unload theme images and free themePath. Call once at final teardown,
 * alongside FS3EStyle_ReleaseDrawContexts. */
void FS3EStyle_FreeThemeImages(FS3EStyle *st);

#endif /* FS3ESTYLE_H */
