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

#include <string.h>

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

/* WM_OPEN the persistent window object on CurrentMainScreen. */
static void GenericOpenWindow(FS3EMainWindow *mw, Object *window_obj)
{
    if (CurrentMainWindow) return;  /* already open */
    if (!CurrentMainScreen) return; /* need an active screen */

    CurrentMainWindow = (struct Window *)DoMethod(window_obj, WM_OPEN, NULL);
    if (!CurrentMainWindow) return;

    if (!mw->drawInfo)
        mw->drawInfo = GetScreenDrawInfo(CurrentMainScreen);

    FS3EStyle_ApplyColors(&app->style, CurrentMainScreen);
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

    if (mw->lockedScreen)
    {
        UnlockPubScreen(NULL, mw->lockedScreen);
        mw->lockedScreen = NULL;
    }

    if (CurrentMainScreen)
        FS3EStyle_ReleasePens(&app->style);

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
