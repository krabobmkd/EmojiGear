/*
 * fs3eprogressview.c - bare-bones progress bar window. See
 * fs3eprogressview.h for the design notes.
 */

#include "fs3eprogressview.h"

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>

BOOL FS3EProgressView_Open(FS3EProgressView *pv)
{
    struct Screen *scr;
    LONG w, h, left, top;

    if (!pv) return FALSE;
    if (pv->window) return TRUE; /* already open */

    scr = LockPubScreen(NULL);
    if (!scr) return FALSE;

    /* A quarter of the screen's width, 16px tall, centered on the screen --
     * clamped to a sane minimum in case of an unusually narrow screen mode. */
    w = scr->Width / 4;
    if (w < 32) w = 32;
    h = 16;
    left = (scr->Width  - w) / 2;
    top  = (scr->Height - h) / 2;

    pv->window = OpenWindowTags(NULL,
        WA_CustomScreen,  (ULONG)scr,
        WA_Left,          left,
        WA_Top,           top,
        WA_Width,         w,
        WA_Height,        h,
        WA_Borderless,    TRUE,
        WA_DragBar,       FALSE,
        WA_DepthGadget,   FALSE,
        WA_CloseGadget,   FALSE,
        WA_SizeGadget,    FALSE,
        WA_SimpleRefresh, TRUE,
        WA_Activate,      FALSE,
        WA_RMBTrap,       FALSE,
        TAG_END);

    if (!pv->window) {
        UnlockPubScreen(NULL, scr);
        return FALSE;
    }

    pv->lockedScreen = scr;
    pv->width  = w;
    pv->height = h;
    pv->value  = 0;

    FS3EProgressView_SetValue(pv, 0);

    return TRUE;
}

void FS3EProgressView_Close(FS3EProgressView *pv)
{
    if (!pv) return;

    if (pv->window) {
        CloseWindow(pv->window);
        pv->window = NULL;
    }
    if (pv->lockedScreen) {
        UnlockPubScreen(NULL, pv->lockedScreen);
        pv->lockedScreen = NULL;
    }
}

void FS3EProgressView_SetValue(FS3EProgressView *pv, UBYTE value)
{
    struct RastPort *rp;
    LONG w, h, fillW;

    if (!pv || !pv->window) return;

    pv->value = value;

    rp = pv->window->RPort;
    w  = pv->width;
    h  = pv->height;

    fillW = ((LONG)value * w) / 255;
    if (fillW > w) fillW = w;

    SetDrMd(rp,JAM1);
    if (fillW > 0) {
        SetAPen(rp, 2);
        RectFill(rp, 0, 0, fillW - 1, h - 1);
    }
    if (fillW < w) {
        SetAPen(rp, 0);
        RectFill(rp, fillW, 0, w - 1, h - 1);
    }

    /* Frame, drawn last so it sits on top of both fill rectangles above --
     * RectFill() has no unfilled-rectangle counterpart, so this is 4
     * Move()/Draw() line segments closing back to the start corner. */
    SetAPen(rp, 1);
    Move(rp, 0, 0);
    Draw(rp, w - 1, 0);
    Draw(rp, w - 1, h - 1);
    Draw(rp, 0, h - 1);
    Draw(rp, 0, 0);
}
