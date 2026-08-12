/*
 * FS3EMainWindow - open/close/iconify management for the FriendSh3ep
 * main window. See fs3eboopsimainwindow.h for the design notes.
 *
 * Adapted from EmojiGear/boopsimainwindow.c, trimmed down to what
 * FriendSh3ep needs so far: no menus, no border scrollers, no
 * fullscreen/custom-screen mode yet - just open on the locked Workbench
 * screen, and survive WM_ICONIFY / WM_UNICONIFY.
 */

#include "fs3eboopsimainwindow.h"

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/alib.h>

#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>

#include <proto/window.h>
#include <classes/window.h>

#include "compilers.h"
#include "bdbprintf.h"
#include "friendsh3ep.h"

#include "TootTimeline/fs3etoottimeline.h"

#include <string.h>
#include <stdio.h>

#include "UniButtonP9/unibuttonp9.h"
#include "UniButtonBGBM/unibuttonbgbm.h"
/* This is the Intuition-level Window. On OS3 it is recreated every time the
 * window is (re)opened: when iconifying/reopening, the BOOPSI objects are
 * kept but the Intuition-level Window/Screen instances are torn down. */
struct Window *CurrentMainWindow = NULL;

/* The locked Workbench screen the window is currently open on. */
struct Screen *CurrentMainScreen = NULL;

void FS3EMain_SetTitle(FS3EMainWindow *mw, const char *title)
{
    if (!mw) return;
/* re, title faked
    strncpy(mw->title, title, sizeof(mw->title) - 1);
    mw->title[sizeof(mw->title) - 1] = 0;

    if (CurrentMainWindow)
        SetWindowTitles(CurrentMainWindow, mw->title, NULL);
*/
}

void FS3EMain_GetWindowPos(FS3EMainWindow *mw, Object *window_obj)
{
    if (!mw || !window_obj) return;

    GetAttr(WA_Top,    window_obj, (ULONG *)&mw->top);
    GetAttr(WA_Left,   window_obj, (ULONG *)&mw->left);
    GetAttr(WA_Width,  window_obj, (ULONG *)&mw->width);
    GetAttr(WA_Height, window_obj, (ULONG *)&mw->height);

    if (mw->width  < 128) mw->width  = 128;
    if (mw->height < 64)  mw->height = 64;
}

/* Propagates app->style onto every widget that caches its own rendering
 * from it -- see the doc comment in fs3eboopsimainwindow.h. */
void FS3EMain_SyncStyleToWidgets(void)
{
    FS3EStyle_SyncTitleBarButtons(&app->style,
        app->titlebar_closeBtn, app->titlebar_iconifyBtn,
        app->titlebar_altposBtn, app->titlebar_depthBtn);

    FS3EStyle_SyncTitleBarDeckButtons(&app->style,
        app->titlebar_networkLedBtn, app->titlebar_scrollUpBtn,
        app->titlebar_playModeBtn);

    /* Re-derive style's font-size-dependent layout (avatarSize and friends)
     * and push TTIMELINE_Style so the toot timeline re-reads app->style's
     * (possibly just-changed) colors -- see FS3EApp_ApplyFontSettings_
     * Delayed's own doc comment for why this also needs a real screen
     * bound to the draw contexts, not just a colour change. */
    FS3EApp_ApplyFontSettings_Delayed();

    /* account/newtoot are UniButtonP9 skinned from a Patch9 slot, not a
     * plain GA_Image -- see FS3EStyle_SyncPatch9Buttons's doc comment for
     * why this needs its own sync step distinct from
     * FS3EStyle_SyncTitleBarButtons above. */
    FS3EStyle_SyncPatch9Buttons(&app->style,
        app->titlebar_accountBtn, app->titlebar_newtootBtn);

    /* nav_btns[] are UniButtonBGBM: always pass &app->style -- unlike
     * UBTP9_Patch9 above, there's no need to gate this on any one image
     * having loaded, since UniButtonBGBM checks style->btbgbmBitmap[state]
     * per state and falls back to its flat colour fill for whichever
     * states don't have one (see ubgbm_blit_state). */
    {
        int i;
        for (i = 0; i < 8; i++) {
            if (app->nav_btns[i])
                SetGdAttrs(app->nav_btns[i],
                    UBGBM_Style, (ULONG)&app->style,
                    UBGBM_Patch9Mode, (ULONG)app->style.btbgbmUsePatch9,
                    TAG_END);
        }
    }
}

/* WM_OPEN the persistent window object on CurrentMainScreen. */
static void GenericOpenWindow(FS3EMainWindow *mw, Object *window_obj)
{

    if (CurrentMainWindow) return;  /* already open */
    if (!CurrentMainScreen) return; /* need an active screen */



    /* Load title bar button images for this screen. The title bar
     * background (tbbg.png) is handled separately: its GA_BackFill hook
     * was installed once at titleBarLayout's creation (see friendsh3ep.c)
     * and re-reads the freshly loaded state itself.
     *
     * Only reloads images when a theme is actually active (app->
     * style.themePath set -- see FS3EApp_LoadTheme in friendsh3ep.c,
     * which is what sets or clears it); "no theme" resets colors instead.
     * This matters because GenericOpenWindow runs on EVERY reopen, not
     * just the first: WM_ICONIFY/WM_UNICONIFY tear down and recreate the
     * Intuition-level Window (see this file's header comment), so without
     * this check "no theme" selected via the chooser would keep coming
     * back with whatever the last loaded theme's images were the moment
     * the app got uniconized and restored -- confirmed as a real bug. */
    if (app->style.themePath && app->style.themePath[0]) {
        FS3EStyle_LoadThemeImages(&app->style, CurrentMainScreen);
    } else {
        FS3EStyle_ResetColors(&app->style);
        FS3EStyle_ResetPatch9Colors(&app->style);

    }

    /* Theme images are the slow part of first startup (datatype decode of
     * every theme bitmap) -- mark it done on the startup progress bar (see
     * FS3EInitStage in friendsh3ep.h). No-op on every later reopen
     * (WM_ICONIFY/WM_UNICONIFY), same as every FS3EApp_InitProgress() call
     * below -- the bar is long closed by then. */
    FS3EApp_InitProgress(FS3E_INIT_THEME_IMAGES);

    /* Synchronize screen palette colors.
     * must be done before opening window, and when screen is known.
     * note: as datatype image reading in FS3EStyle_LoadThemeImages can realloc colors,
     * better do this after for 8bit indexed palette modes:
     */
    FS3EStyle_ApplyColors(&app->style, CurrentMainScreen);

    /* Build the 4 built-in title bar button glyphs (idempotent -- a no-op
     * past the very first successful call) so FS3EStyle_SyncTitleBarButtons
     * below never has to fall back to a literal NULL GA_Image for "no
     * theme" -- see FS3EStyle_CreateDefaultButtonImages' doc comment for
     * why button.gadget doesn't tolerate that once an image has been
     * attached at least once. */
    FS3EStyle_CreateDefaultButtonImages(&app->style, CurrentMainScreen);

    /* Loading screen bitmap datatypes allocs/free colors to screen palette */
    FS3EStyle_UpdateAllDCColorMap(&app->style,CurrentMainScreen);

    FS3EApp_InitProgress(FS3E_INIT_STYLE_APPLIED);
    /* Button cell size may have changed (theme-dependent), so this is
     * followed by a full relayout (WM_RETHINK below). */
    FS3EMain_SyncStyleToWidgets();

    /* Colors/style fully applied -- the startup progress bar is done (see
     * FS3EInitStage in friendsh3ep.h); close it right here, before WM_OPEN,
     * rather than back in main() right after fs3e_setViewMode() like
     * before -- everything from FS3EStyle_LoadThemeImages() above through
     * here is real startup time that used to run after the bar had
     * already closed. */

    FS3EApp_InitProgressDone();

    DoMethod(window_obj, WM_RETHINK);

    CurrentMainWindow = (struct Window *)DoMethod(window_obj, WM_OPEN, NULL);
    if (!CurrentMainWindow) return;

    if (!mw->drawInfo)
        mw->drawInfo = GetScreenDrawInfo(CurrentMainScreen);


}

void FS3EMain_Show(FS3EMainWindow *mw, Object *window_obj)
{
    LONG x1, y1, w, h;

    if (!mw || !window_obj) return;
    if (CurrentMainWindow) return; /* already open */

    if (!mw->lockedScreen)
        mw->lockedScreen = LockPubScreen(NULL);
    if (!mw->lockedScreen) return;

    CurrentMainScreen = mw->lockedScreen;

    /* recover last position/size, or pick a default for the first open */
    if (mw->width > 0)
    {
        x1 = mw->left;
        y1 = mw->top;
        w  = mw->width;
        h  = mw->height;
    }
    else
    {
        x1 = 40;
        y1 = 40;
        w  = 400;
        h  = 220;
    }

    /* reconfigure the persistent BOOPSI window object while it is closed */
    SetAttrs(window_obj,
        WA_CustomScreen, (ULONG)mw->lockedScreen,
    //    WA_Title,        (ULONG)&mw->title[0],
  //  WA_Title, (ULONG)"",
        WA_Top,          y1,
        WA_Left,         x1,
        WA_Width,        w,
        WA_Height,       h,
        TAG_END);

    GenericOpenWindow(mw, window_obj);

    if (CurrentMainWindow)
        ScreenToFront(CurrentMainScreen);
}

void FS3EMain_Close(FS3EMainWindow *mw, Object *window_obj, int iconify)
{
    if (!mw) return;

    if (CurrentMainWindow)
    {
        /* remember position/size so FS3EMain_Show can restore it */
        FS3EMain_GetWindowPos(mw, window_obj);

        if (iconify)
            DoMethod(window_obj, WM_ICONIFY, NULL);
        else
            DoMethod(window_obj, WM_CLOSE);

        CurrentMainWindow = NULL;
    }

    if (mw->drawInfo && CurrentMainScreen)
    {
        FreeScreenDrawInfo(CurrentMainScreen, mw->drawInfo);
        mw->drawInfo = NULL;
    }
    if (CurrentMainScreen) {
        FS3EStyle_ReleasePens(&app->style);
        FS3EStyle_UnloadThemeImages(&app->style);
    }

    if (mw->lockedScreen)
    {
        UnlockPubScreen(NULL, mw->lockedScreen);
        mw->lockedScreen = NULL;
    }



    CurrentMainScreen = NULL;
}

void SetGdAttrsA(Object *g, CONST struct TagItem *tags)
{
    if (CurrentMainWindow)
    {
        SetGadgetAttrsA((struct Gadget *)g, CurrentMainWindow, NULL, tags);
    }
    else
    {
        SetAttrsA(g, tags);
    }
}

void  __attribute__((noinline))
    SetGdAttrs(Object *g, ULONG tag, ... ) {
    SetGdAttrsA(g, ( struct TagItem *)&tag);
}
