#ifndef FS3EBOOPSIMAINWINDOW_H
#define FS3EBOOPSIMAINWINDOW_H

#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <intuition/classusr.h>
#include "compilers.h"

/*
 * FS3EMainWindow - small extension over the persistent window.class BOOPSI
 * object, mirroring EmojiGear's BoopsiMainWindow:
 *
 *  - The BOOPSI window object (Object *) is allocated once and survives for
 *    the whole run of the program.
 *  - The Intuition-level "struct Window *" (CurrentMainWindow) and the
 *    screen it is open on (CurrentMainScreen) are transient: WM_ICONIFY
 *    tears them down, WM_OPEN (via FS3EMain_Show) recreates them on the
 *    locked Workbench screen.
 *  - FS3EMainWindow remembers the last window position/size so it can be
 *    restored when reopened after an iconify.
 */
typedef struct FS3EMainWindow {
    LONG top, left, width, height;

    struct Screen   *lockedScreen; /* locked while the window is open */
    struct DrawInfo *drawInfo;

    char title[80];
} FS3EMainWindow;

/* Set the window title (applied immediately if the window is open). */
void FS3EMain_SetTitle(FS3EMainWindow *mw, const char *title);

/* Open (or reopen after iconify) the window on the locked Workbench screen. */
void FS3EMain_Show(FS3EMainWindow *mw, Object *window_obj);

/* Close the Intuition-level window. iconify!=0 sends WM_ICONIFY (keeps the
 * BOOPSI object and app_port alive so WMHI_UNICONIFY can be received),
 * iconify==0 sends WM_CLOSE (used at quit, before DisposeObject). */
void FS3EMain_Close(FS3EMainWindow *mw, Object *window_obj, int iconify);

/* Save the current window position/size into *mw. */
void FS3EMain_GetWindowPos(FS3EMainWindow *mw, Object *window_obj);

/* Propagates app->style (colors, theme images, Patch9 skins -- already
 * loaded/reset and pen-applied by the caller) onto every widget that
 * caches its own rendering from it: the 4 plain-image title bar buttons,
 * the 2 Patch9-skinned ones, the toot timeline (background/text colors
 * via TTIMELINE_Style, which also re-derives avatarSize and other
 * font-size-dependent layout -- see FS3EApp_ApplyFontSettings_Delayed's
 * own doc comment), and the 8 nav bar buttons. Callers are expected to
 * have already called FS3EStyle_LoadThemeImages()/ResetColors()/
 * ResetPatch9Colors() and FS3EStyle_ApplyColors() -- this only propagates
 * that already-updated state onward, it doesn't compute anything itself.
 * Used by GenericOpenWindow (every window open/uniconize, this file) and
 * FS3EApp_LoadTheme (friendsh3ep.c, a live theme switch) -- kept as one
 * function specifically so those two call sites can't silently drift
 * apart the way FS3EApp_LoadTheme once shipped without the toot
 * timeline/nav bar steps below. */
void FS3EMain_SyncStyleToWidgets(void);

/* SetGadgetAttrs() if a window is currently open, else SetAttrs(). */
void SetGdAttrsA(Object *g, CONST struct TagItem *tags);
void  __attribute__((noinline)) SetGdAttrs(Object *g, ULONG tag, ...);

/* Intuition-level Window, recreated each time the window is (re)opened. */
extern struct Window *CurrentMainWindow;

/* Intuition-level Screen the window is currently open on (locked WB screen). */
extern struct Screen *CurrentMainScreen;

#endif /* FS3EBOOPSIMAINWINDOW_H */
