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

/* SetGadgetAttrs() if a window is currently open, else SetAttrs(). */
void SetGdAttrsA(Object *g, CONST struct TagItem *tags);
void  __attribute__((noinline)) SetGdAttrs(Object *g, ULONG tag, ...);

/* Intuition-level Window, recreated each time the window is (re)opened. */
extern struct Window *CurrentMainWindow;

/* Intuition-level Screen the window is currently open on (locked WB screen). */
extern struct Screen *CurrentMainScreen;

#endif /* FS3EBOOPSIMAINWINDOW_H */
