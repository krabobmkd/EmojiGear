/*
 * fs3emediaview.c - "FriendSh3ep Media" full-size attachment viewer.
 *
 * $VER: fs3emediaview.c 1.0 (13.07.2026)
 * Copyright (C) 2026 FriendSh3ep contributors. All rights reserved.
 *
 * See fs3emediaview.h. A bare Intuition window (no window.class/layout.gadget
 * like the rest of this app's sub-windows) since there's nothing to lay
 * out -- just BmImage's bitmap blitted at its natural size.
 */

#include "fs3emediaview.h"
#include "fs3eboopsimainwindow.h"  /* extern CurrentMainScreen */

#include <string.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/dos.h>
#include <exec/memory.h>
#include <graphics/gfxbase.h>
#include <graphics/text.h>

/* Declared in friendsh3ep.c; fs3eaction.c already calls this the same way. */
extern BOOL FS3EApp_NetSend(ULONG type, APTR data, ULONG dataLen);

#define FS3EMV_TITLE          "FriendSh3ep Media"
#define FS3EMV_PLACEHOLDER_W  260
#define FS3EMV_PLACEHOLDER_H  80

static char *mediaview_strdup(const char *s)
{
    ULONG len;
    char *copy;
    if (!s) return NULL;
    len  = (ULONG)strlen(s) + 1;
    copy = (char *)AllocVec(len, MEMF_ANY);
    if (copy) strcpy(copy, s);
    return copy;
}

/* Redraws the whole content area: either the loaded image (unscaled, blitted
 * flush against the window's borders) or a "Loading..." placeholder. */
static void mediaview_render(FS3EMediaView *mv)
{
    struct RastPort *rp;
    WORD left, top, right, bottom;

    if (!mv->window) return;
    rp = mv->window->RPort;

    left   = mv->window->BorderLeft;
    top    = mv->window->BorderTop;
    right  = (WORD)(mv->window->Width  - mv->window->BorderRight  - 1);
    bottom = (WORD)(mv->window->Height - mv->window->BorderBottom - 1);

    SetAPen(rp, 0);
    RectFill(rp, left, top, right, bottom);
    SetAPen(rp, 1);
    SetBPen(rp, 0);
    SetDrMd(rp, JAM2);

    if (BmImage_IsLoaded(&mv->image)) {
        BltBitMapRastPort(mv->image.bitmap, 0, 0, rp, left, top,
                           mv->image.width, mv->image.height, 0xC0);
    } else if (mv->loading) {
        const char *msg = "Loading media...";
        Move(rp, (WORD)(left + 8), (WORD)(top + 20));
        Text(rp, (STRPTR)msg, (ULONG)strlen(msg));
    }
}

/* Resizes the window so its content area exactly fits w x h (border sizes
 * are fixed once opened, so this is just adding them back on). */
static void mediaview_resize_to(FS3EMediaView *mv, UWORD w, UWORD h)
{
    if (!mv->window) return;
    ChangeWindowBox(mv->window, mv->window->LeftEdge, mv->window->TopEdge,
                    (WORD)(w + mv->window->BorderLeft + mv->window->BorderRight),
                    (WORD)(h + mv->window->BorderTop  + mv->window->BorderBottom));
}

void FS3EMediaView_Init(FS3EMediaView *mv)
{
    if (!mv) return;
    memset(mv, 0, sizeof(*mv));
}

void FS3EMediaView_Dispose(FS3EMediaView *mv)
{
    if (!mv) return;
    FS3EMediaView_Close(mv);
    BmImage_Free(&mv->image);
    if (mv->pendingUrl) { FreeVec(mv->pendingUrl); mv->pendingUrl = NULL; }
}

void FS3EMediaView_ShowUrl(FS3EMediaView *mv, const char *url)
{
    FS3ENetFetchImageReq *req;

    if (!mv || !url || !url[0]) return;

    if (!mv->window) {
        if (!CurrentMainScreen) return;

        mv->window = OpenWindowTags(NULL,
            WA_Left,        mv->left > 0 ? mv->left : 100,
            WA_Top,         mv->top  > 0 ? mv->top  : 60,
            WA_InnerWidth,  FS3EMV_PLACEHOLDER_W,
            WA_InnerHeight, FS3EMV_PLACEHOLDER_H,
            WA_Title,       (ULONG)FS3EMV_TITLE,
            WA_DragBar,     TRUE,
            WA_CloseGadget, TRUE,
            WA_DepthGadget, TRUE,
            WA_SimpleRefresh, TRUE,
            WA_IDCMP,       IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_RAWKEY,
            WA_CustomScreen, (ULONG)CurrentMainScreen,
            TAG_END);
        if (!mv->window) return;
    } else {
        WindowToFront(mv->window);
        ActivateWindow(mv->window);
    }

    if (mv->pendingUrl) { FreeVec(mv->pendingUrl); mv->pendingUrl = NULL; }
    mv->pendingUrl = mediaview_strdup(url);

    BmImage_Free(&mv->image);
    mv->loading = TRUE;
    mediaview_resize_to(mv, FS3EMV_PLACEHOLDER_W, FS3EMV_PLACEHOLDER_H);
    mediaview_render(mv);

    /* keepOriginal=TRUE: an explicit click means the user wants this image,
     * worth persisting from now on even if "Keep big thumbnails" is off --
     * see fs3emediaview.h's header comment. */
    req = FS3ENetFetchImageReq_Alloc(url, url, FS3E_CACHE_SUBDIR_THUMBNAILS, TRUE);
    if (req) FS3EApp_NetSend(FS3ENETQ_FETCH_IMAGE, req, sizeof(*req));
}

void FS3EMediaView_OnFetchReply(FS3EMediaView *mv, ULONG result,
                                 const FS3ENetFetchImageReply *reply)
{
    if (!mv || !mv->pendingUrl || !reply || !reply->fs3enf_Key) return;
    if (strcmp(reply->fs3enf_Key, mv->pendingUrl) != 0) return;

    FreeVec(mv->pendingUrl);
    mv->pendingUrl = NULL;
    mv->loading    = FALSE;

    if (result == FS3ENETR_OK && reply->fs3enf_LocalPath) {
        BmImage_Free(&mv->image);
        if (BmImage_Init(&mv->image, reply->fs3enf_LocalPath) &&
            BmImage_Load(&mv->image, CurrentMainScreen))
        {
            mediaview_resize_to(mv, mv->image.width, mv->image.height);
        }

        /* fs3enf_IsTemp: a RAM:T download we're the last user of (see its
         * doc comment in fs3enet.h) -- the persistent-cache case (the
         * common one now that we always request keepOriginal=TRUE) needs
         * no cleanup, that copy is meant to stay. */
        if (reply->fs3enf_IsTemp)
            DeleteFile((STRPTR)reply->fs3enf_LocalPath);
    }

    mediaview_render(mv);
}

void FS3EMediaView_Close(FS3EMediaView *mv)
{
    if (!mv || !mv->window) return;

    mv->left = mv->window->LeftEdge;
    mv->top  = mv->window->TopEdge;

    CloseWindow(mv->window);
    mv->window = NULL;
}

BOOL FS3EMediaView_HandleInput(FS3EMediaView *mv)
{
    struct IntuiMessage *imsg;

    if (!mv || !mv->window) return TRUE;

    while ((imsg = (struct IntuiMessage *)GetMsg(mv->window->UserPort)) != NULL) {
        ULONG class = imsg->Class;
        UWORD code  = imsg->Code;

        ReplyMsg((struct Message *)imsg);

        switch (class) {
            case IDCMP_CLOSEWINDOW:
                FS3EMediaView_Close(mv);
                return TRUE;

            case IDCMP_REFRESHWINDOW:
                BeginRefresh(mv->window);
                mediaview_render(mv);
                EndRefresh(mv->window, TRUE);
                break;

            case IDCMP_RAWKEY:
                if (code == 0x45) { /* Esc */
                    FS3EMediaView_Close(mv);
                    return TRUE;
                }
                break;

            default:
                break;
        }
    }

    return TRUE;
}

ULONG FS3EMediaView_GetSignalMask(FS3EMediaView *mv)
{
    if (!mv || !mv->window) return 0;
    return (1L << mv->window->UserPort->mp_SigBit);
}
