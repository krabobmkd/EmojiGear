/*
 * fs3emediaview.c - "FriendSh3ep Media" full-size attachment viewer.
 *
 * $VER: fs3emediaview.c 3.0 (17.07.2026)
 * Copyright (C) 2026 FriendSh3ep contributors. All rights reserved.
 *
 * See fs3emediaview.h for the window.class/layout.gadget design and why
 * the picture is a private FS3EMediaPic gadgetclass instance (pattern
 * copied from fs3eemojibox.c's FSEBGrid class) rather than a
 * picture.datatype object swapped in and out of the layout directly.
 */

#include "fs3emediaview.h"
#include "fs3eboopsimainwindow.h"  /* extern CurrentMainScreen */

#include <string.h>
#include <stdio.h>

#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/alib.h>
#include <proto/utility.h>
#include <proto/graphics.h>
#include <proto/dos.h>
#include <proto/gadtools.h>
#include <proto/asl.h>

#include <intuition/gadgetclass.h>
#include <hardware/blit.h>

#include <proto/layout.h>
#include <gadgets/layout.h>

#include <proto/button.h>
#include <gadgets/button.h>

#include <proto/window.h>
#include <classes/window.h>

#include <libraries/gadtools.h>
#include <libraries/asl.h>

#include "compilers.h"
#include "bdbprintf.h"

/* Declared in friendsh3ep.c; fs3eaction.c already calls this the same way. */
extern BOOL FS3EApp_NetSend(ULONG type, APTR data, ULONG dataLen);

/* Opened via libraryTable in friendsh3ep.c. */
extern struct Library *GadToolsBase;
extern struct Library *AslBase;

#define FS3EMV_TITLE          "FriendSh3ep Media"
#define FS3EMV_PLACEHOLDER_W  260
#define FS3EMV_PLACEHOLDER_H  80

/* -------------------------------------------------------------------------
 * FS3EMediaPic -- private gadgetclass that just blits whatever BitMap it's
 * currently told about (see fs3emediaview.h's header comment). Modeled on
 * fs3eemojibox.c's FSEBGrid class: MakeClass'd once, one persistent
 * instance, attribute-driven redraw instead of ever being removed/rebuilt.
 * ---------------------------------------------------------------------- */

#define MEDIAPIC_Dummy      (TAG_USER + 0x4D50)  /* 'MP' */
#define MEDIAPIC_BitMap     (MEDIAPIC_Dummy + 1) /* [IS] struct BitMap * or NULL */
#define MEDIAPIC_MaskPlane  (MEDIAPIC_Dummy + 2) /* [IS] PLANEPTR or NULL */
#define MEDIAPIC_Width      (MEDIAPIC_Dummy + 3) /* [IS] UWORD */
#define MEDIAPIC_Height     (MEDIAPIC_Dummy + 4) /* [IS] UWORD */
#define MEDIAPIC_Message    (MEDIAPIC_Dummy + 5) /* [IS] const char *; shown centered
                                                   * when BitMap is NULL (borrowed,
                                                   * must outlive the attribute set --
                                                   * callers always pass a static
                                                   * string literal, never a freed
                                                   * or stack buffer) */

/* Same masked-blit minterm as patch9.c/fs3etitlebar.c ((ABC|ABNC|ANBC): copy
 * source through the mask, leave the rest of the destination untouched). */
#define FS3EMV_MASK_MINTERM (ABC|ABNC|ANBC)

typedef struct {
    struct BitMap *bitmap;  /* borrowed -- owned by FS3EMediaView's BmImage */
    PLANEPTR       mask;    /* borrowed; NULL if no transparency */
    UWORD          width, height;
    const char    *message; /* borrowed static string; see MEDIAPIC_Message above */
} FS3EMediaPicInst;

#define MEDIAPIC_INST(cl, o) ((FS3EMediaPicInst *)INST_DATA((cl), (o)))

static ULONG FS3EMediaPic_OnRender(Class *cl, Object *o, struct gpRender *msg)
{
    FS3EMediaPicInst *inst = MEDIAPIC_INST(cl, o);
    struct RastPort  *rp   = msg->gpr_RPort;
    struct Gadget    *g    = (struct Gadget *)o;
    WORD gx = g->LeftEdge, gy = g->TopEdge;
    WORD gw = g->Width,    gh = g->Height;

    if (!rp || gw <= 0 || gh <= 0) return 0;

    SetAPen(rp, 0);
    SetDrMd(rp, JAM1);
    RectFill(rp, (LONG)gx, (LONG)gy, (LONG)(gx + gw - 1), (LONG)(gy + gh - 1));

    if (inst->bitmap && inst->width > 0 && inst->height > 0) {
        if (inst->mask) {
            BltMaskBitMapRastPort(inst->bitmap, 0, 0, rp, gx, gy,
                                  (LONG)inst->width, (LONG)inst->height,
                                  FS3EMV_MASK_MINTERM, inst->mask);
        } else {
            BltBitMapRastPort(inst->bitmap, 0, 0, rp, gx, gy,
                              (LONG)inst->width, (LONG)inst->height, 0xC0);
        }
    } else if (inst->message) {
        SetAPen(rp, 1);
        SetBPen(rp, 0);
        SetDrMd(rp, JAM2);
        Move(rp, (LONG)(gx + 8), (LONG)(gy + 20));
        Text(rp, (STRPTR)inst->message, (ULONG)strlen(inst->message));
    }

    return 0;
}

static ULONG FS3EMediaPic_OnDomain(Class *cl, Object *o, struct gpDomain *msg)
{
    FS3EMediaPicInst *inst   = MEDIAPIC_INST(cl, o);
    struct IBox      *domain = &msg->gpd_Domain;
    UWORD w = inst->width  > 0 ? inst->width  : FS3EMV_PLACEHOLDER_W;
    UWORD h = inst->height > 0 ? inst->height : FS3EMV_PLACEHOLDER_H;

    domain->Left = 0;
    domain->Top  = 0;

    switch (msg->gpd_Which) {
        case GDOMAIN_MAXIMUM:
            domain->Width  = 32767;
            domain->Height = 32767;
            break;
        case GDOMAIN_MINIMUM:
        case GDOMAIN_NOMINAL:
        default:
            domain->Width  = w;
            domain->Height = h;
            break;
    }
    return 1;
}

static ULONG FS3EMediaPic_OnNew(Class *cl, Object *o, struct opSet *msg)
{
    FS3EMediaPicInst *inst;
    Object *newObj;
    struct TagItem *ptag;

    newObj = (Object *)DoSuperMethodA(cl, o, (Msg)msg);
    if (!newObj) return 0;

    inst = MEDIAPIC_INST(cl, newObj);
    memset(inst, 0, sizeof(*inst));

    ptag = FindTagItem(MEDIAPIC_Message, msg->ops_AttrList);
    if (ptag) inst->message = (const char *)ptag->ti_Data;

    return (ULONG)newObj;
}

static ULONG FS3EMediaPic_OnSet(Class *cl, Object *o, struct opSet *msg)
{
    FS3EMediaPicInst *inst  = MEDIAPIC_INST(cl, o);
    struct TagItem   *state = msg->ops_AttrList;
    struct TagItem   *tag;
    BOOL redraw = FALSE;

    while ((tag = NextTagItem(&state)) != NULL) {
        switch (tag->ti_Tag) {
            case MEDIAPIC_BitMap:
                inst->bitmap = (struct BitMap *)tag->ti_Data;
                redraw = TRUE;
                break;
            case MEDIAPIC_MaskPlane:
                inst->mask = (PLANEPTR)tag->ti_Data;
                redraw = TRUE;
                break;
            case MEDIAPIC_Width:
                inst->width = (UWORD)tag->ti_Data;
                redraw = TRUE;
                break;
            case MEDIAPIC_Height:
                inst->height = (UWORD)tag->ti_Data;
                redraw = TRUE;
                break;
            case MEDIAPIC_Message:
                inst->message = (const char *)tag->ti_Data;
                redraw = TRUE;
                break;
            default:
                break;
        }
    }

    if (redraw && msg->ops_GInfo) {
        struct RastPort *rp = ObtainGIRPort(msg->ops_GInfo);
        if (rp) {
            DoMethod(o, GM_RENDER, msg->ops_GInfo, rp, GREDRAW_REDRAW);
            ReleaseGIRPort(rp);
        }
    }

    return DoSuperMethodA(cl, o, (Msg)msg);
}

static ULONG ASM SAVEDS FS3EMediaPic_Dispatch(
    REG(a0, Class *cl), REG(a2, Object *o), REG(a1, Msg msg))
{
    switch (msg->MethodID) {
        case OM_NEW:      return FS3EMediaPic_OnNew(cl, o, (struct opSet *)msg);
        case OM_SET:
        case OM_UPDATE:   return FS3EMediaPic_OnSet(cl, o, (struct opSet *)msg);
        case GM_DOMAIN:   return FS3EMediaPic_OnDomain(cl, o, (struct gpDomain *)msg);
        case GM_RENDER:   return FS3EMediaPic_OnRender(cl, o, (struct gpRender *)msg);
        default:          return DoSuperMethodA(cl, o, (Msg)msg);
    }
}

/* -------------------------------------------------------------------------
 * FS3EMediaView -- window management
 * ---------------------------------------------------------------------- */

static struct NewMenu s_mvMenuTemplate[] = {
    { NM_TITLE, (STRPTR)"Media",         NULL,        0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Close",         (STRPTR)"K", 0, 0, (APTR)1 },
    { NM_ITEM,  (STRPTR)"Save Media...", (STRPTR)"S", 0, 0, (APTR)2 },
    { NM_END,   NULL,                    NULL,        0, 0, NULL },
};
#define FS3EMV_MENU_CLOSE 1
#define FS3EMV_MENU_SAVE  2

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

/* Resizes the window's content area to exactly w x h -- WA_InnerWidth/
 * WA_InnerHeight are settable "at any time" per window_cl.doc (open or
 * closed) and already exclude window chrome. WM_RETHINK re-evaluates the
 * layout's domain (picGadget's GM_DOMAIN, see CHILD_CacheDomain FALSE
 * below) against the new size. */
static void mediaview_resize_to(FS3EMediaView *mv, WORD w, WORD h)
{
    if (!mv->windowObj) return;
    SetAttrs(mv->windowObj,
        WA_InnerWidth,  (ULONG)w,
        WA_InnerHeight, (ULONG)h,
        TAG_END);
    DoMethod(mv->windowObj, WM_RETHINK, NULL);
}

/* Pushes mv->image's current bitmap/mask/width/height (or all-NULL/0 plus
 * placeholderMsg, when mv->image isn't loaded) onto picGadget, which
 * repaints itself in place -- see FS3EMediaPic_OnSet. Then resizes the
 * window to match (or the placeholder box size, while loading/on error). */
static void mediaview_push_picture(FS3EMediaView *mv, const char *placeholderMsg)
{
    BOOL     loaded = BmImage_IsLoaded(&mv->image);
    UWORD    w      = loaded ? mv->image.width  : 0;
    UWORD    h      = loaded ? mv->image.height : 0;
    struct BitMap *bm   = loaded ? mv->image.bitmap : NULL;
    PLANEPTR       mask = loaded ? mv->image.mask   : NULL;

    if (!mv->picGadget) return;

    if (mv->window) {
        SetGadgetAttrs((struct Gadget *)mv->picGadget, mv->window, NULL,
            MEDIAPIC_BitMap,    (ULONG)bm,
            MEDIAPIC_MaskPlane, (ULONG)mask,
            MEDIAPIC_Width,     (ULONG)w,
            MEDIAPIC_Height,    (ULONG)h,
            MEDIAPIC_Message,   (ULONG)placeholderMsg,
            TAG_DONE);
    } else {
        SetAttrs(mv->picGadget,
            MEDIAPIC_BitMap,    (ULONG)bm,
            MEDIAPIC_MaskPlane, (ULONG)mask,
            MEDIAPIC_Width,     (ULONG)w,
            MEDIAPIC_Height,    (ULONG)h,
            MEDIAPIC_Message,   (ULONG)placeholderMsg,
            TAG_END);
    }

    mediaview_resize_to(mv, w > 0 ? (WORD)w : FS3EMV_PLACEHOLDER_W,
                             h > 0 ? (WORD)h : FS3EMV_PLACEHOLDER_H);
}

/* Builds the persistent picClass/picGadget/layout/windowObj the first time
 * ShowUrl is called (idempotent past that). */
static BOOL mediaview_ensure_window(FS3EMediaView *mv)
{
    if (mv->windowObj) return TRUE;

    mv->picClass = MakeClass(NULL, "gadgetclass", NULL, sizeof(FS3EMediaPicInst), 0);
    if (!mv->picClass) return FALSE;
    mv->picClass->cl_Dispatcher.h_Entry = (HOOKFUNC)FS3EMediaPic_Dispatch;
    bdbprintf_makeclass("FS3EMediaPicClass", mv->picClass);

    mv->picGadget = (Object *)NewObject(mv->picClass, NULL,
        MEDIAPIC_Message, (ULONG)"Loading media...",
        TAG_END);
    if (!mv->picGadget) {
        bdbprintf_freeclass("FS3EMediaPicClass", mv->picClass);
        FreeClass(mv->picClass);
        mv->picClass = NULL;
        return FALSE;
    }

    /* picGadget's GM_DOMAIN result changes every time a differently-sized
     * picture loads -- CHILD_CacheDomain FALSE tells layout.gadget to
     * re-query it on every relayout instead of trusting its first answer
     * forever (see layout_gc.doc's CHILD_CacheDomain entry). */
    mv->layout = (Object *)NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation,  LAYOUT_ORIENT_VERT,
        LAYOUT_BevelStyle,   BVS_NONE,
        LAYOUT_SpaceOuter,   FALSE,
        LAYOUT_SpaceInner,   FALSE,
        LAYOUT_AddChild,     (ULONG)mv->picGadget,
            CHILD_WeightedWidth,  0,
            CHILD_WeightedHeight, 0,
            CHILD_CacheDomain,    FALSE,
        TAG_END);
    if (!mv->layout) {
        DisposeObject(mv->picGadget); /* not yet attached -- plain dispose is fine */
        mv->picGadget = NULL;
        bdbprintf_freeclass("FS3EMediaPicClass", mv->picClass);
        FreeClass(mv->picClass);
        mv->picClass = NULL;
        return FALSE;
    }

    mv->windowObj = (Object *)NewObject(WINDOW_GetClass(), NULL,
        WA_Left,            mv->left > 0 ? mv->left : 100,
        WA_Top,             mv->top  > 0 ? mv->top  : 60,
        WA_InnerWidth,      FS3EMV_PLACEHOLDER_W,
        WA_InnerHeight,     FS3EMV_PLACEHOLDER_H,
        WA_Title,           (ULONG)FS3EMV_TITLE,
        WA_IDCMP,           IDCMP_CLOSEWINDOW | IDCMP_MENUPICK,
        WA_Flags,           WFLG_DRAGBAR | WFLG_DEPTHGADGET | WFLG_CLOSEGADGET |
                            WFLG_ACTIVATE | WFLG_SMART_REFRESH,
        WINDOW_ParentGroup, (ULONG)mv->layout,
        TAG_END);
    if (!mv->windowObj) {
        DisposeObject(mv->layout);   /* cascades: disposes picGadget too */
        mv->layout    = NULL;
        mv->picGadget = NULL;
        bdbprintf_freeclass("FS3EMediaPicClass", mv->picClass);
        FreeClass(mv->picClass);
        mv->picClass = NULL;
        return FALSE;
    }

    return TRUE;
}

/* Attaches the "Media" menu (Close / Save Media...) to mv->window. Called
 * once, right after the window is first opened -- see FS3EMediaView_
 * ShowUrl. Same pattern as fs3etootview.c's window-local "Toot" menu.
 * No-op (silently, same degrade-gracefully rule as everywhere else
 * optional libraries are involved in this app) if gadtools.library isn't
 * open. */
static void mediaview_create_menu(FS3EMediaView *mv)
{
    if (!mv->window || !GadToolsBase) return;

    mv->menuVisualInfo = GetVisualInfo(mv->window->WScreen, TAG_END);
    if (!mv->menuVisualInfo) return;

    mv->menu = CreateMenus(s_mvMenuTemplate, TAG_END);
    if (!mv->menu) {
        FreeVisualInfo(mv->menuVisualInfo);
        mv->menuVisualInfo = NULL;
        return;
    }

    if (!LayoutMenus(mv->menu, mv->menuVisualInfo, GTMN_NewLookMenus, TRUE, TAG_END)) {
        FreeMenus(mv->menu);
        FreeVisualInfo(mv->menuVisualInfo);
        mv->menu           = NULL;
        mv->menuVisualInfo = NULL;
        return;
    }

    SetMenuStrip(mv->window, mv->menu);
}

/* Reverse of mediaview_create_menu() -- call while mv->window is still
 * valid (ClearMenuStrip needs it), before WM_CLOSE. */
static void mediaview_dispose_menu(FS3EMediaView *mv)
{
    if (mv->menu) {
        if (mv->window) ClearMenuStrip(mv->window);
        FreeMenus(mv->menu);
        mv->menu = NULL;
    }
    if (mv->menuVisualInfo) {
        FreeVisualInfo(mv->menuVisualInfo);
        mv->menuVisualInfo = NULL;
    }
}

/* Builds the default "Save Media..." filename (no path, no directory):
 * mv->poster (sanitized) if set, else the cache file's own hash-id name,
 * plus the extension for whatever format BmImage_SniffFormat() actually
 * detects in the loaded file -- never trusted from the source URL, which
 * may have no extension at all (see fs3enet.h's FS3ENetFetchImageReply
 * doc comment: cache paths carry no extension either). */
static void mediaview_build_default_name(FS3EMediaView *mv, char *out, ULONG outSize)
{
    char sanitized[128];
    const char *base;
    const char *ext;

    if (mv->poster[0]) {
        const char *src = mv->poster;
        ULONG i = 0;
        if (*src == '@') src++;
        for (; *src && i < sizeof(sanitized) - 1; src++) {
            char c = *src;
            if (c == '/' || c == ':') c = '_';
            sanitized[i++] = c;
        }
        sanitized[i] = '\0';
        base = sanitized;
    } else {
        base = (const char *)FilePart((STRPTR)(mv->image.filePath ? mv->image.filePath : "media"));
    }

    switch (BmImage_SniffFormat(mv->image.filePath)) {
        case BMFMT_PNG:  ext = ".png";  break;
        case BMFMT_JPEG: ext = ".jpg";  break;
        case BMFMT_GIF:  ext = ".gif";  break;
        case BMFMT_WEBP: ext = ".webp"; break;
        case BMFMT_BMP:  ext = ".bmp";  break;
        default:         ext = ".dat";  break;
    }

    snprintf(out, (size_t)outSize, "%s%s", base, ext);
}

/* Straight buffered copy, srcPath -> dstPath. Used instead of a re-fetch --
 * srcPath is always the already-downloaded, already-persistent-cache file
 * mv->image was loaded from (FS3EMediaView_ShowUrl always requests
 * keepOriginal=TRUE, so it is never the RAM:T temp path that
 * FS3EMediaView_OnFetchReply deletes right after loading). Deletes a
 * partial dstPath on failure rather than leaving a truncated file behind. */
static BOOL mediaview_copy_file(const char *srcPath, const char *dstPath)
{
    BPTR  in, out;
    UBYTE buf[4096];
    LONG  n;
    BOOL  ok = TRUE;

    in = Open((STRPTR)srcPath, MODE_OLDFILE);
    if (!in) return FALSE;

    out = Open((STRPTR)dstPath, MODE_NEWFILE);
    if (!out) { Close(in); return FALSE; }

    while ((n = Read(in, buf, sizeof(buf))) > 0) {
        if (Write(out, buf, n) != n) { ok = FALSE; break; }
    }
    if (n < 0) ok = FALSE;

    Close(out);
    Close(in);
    if (!ok) DeleteFile((STRPTR)dstPath);
    return ok;
}

/* "Save Media..." action: ASL file requester defaulting to RAM: and the
 * name mediaview_build_default_name() derives, then a plain file copy from
 * the cache -- no re-fetch, no re-encode. Silently does nothing if no
 * image is currently loaded (still loading, or the fetch failed) or if
 * asl.library isn't open. */
static void mediaview_save_media(FS3EMediaView *mv)
{
    struct FileRequester *req;
    char defaultName[160];

    if (!mv->window || !AslBase) return;
    if (!BmImage_IsLoaded(&mv->image) || !mv->image.filePath) return;

    mediaview_build_default_name(mv, defaultName, sizeof(defaultName));

    req = (struct FileRequester *)AllocAslRequestTags(ASL_FileRequest,
        ASLFR_Window,        (ULONG)mv->window,
        ASLFR_TitleText,     (ULONG)"Save Media",
        ASLFR_DoSaveMode,    TRUE,
        ASLFR_RejectIcons,   TRUE,
        ASLFR_InitialDrawer, (ULONG)"RAM:",
        ASLFR_InitialFile,   (ULONG)defaultName,
        TAG_DONE);
    if (!req) return;

    if (AslRequest(req, NULL)) {
        char destPath[512];
        ULONG dirLen = (ULONG)strlen((char *)req->fr_Drawer);
        BOOL  needSlash = dirLen > 0 &&
                          req->fr_Drawer[dirLen - 1] != ':' &&
                          req->fr_Drawer[dirLen - 1] != '/';

        snprintf(destPath, sizeof(destPath), "%s%s%s",
                 req->fr_Drawer, needSlash ? "/" : "", req->fr_File);

        mediaview_copy_file(mv->image.filePath, destPath);
    }

    FreeAslRequest(req);
}

void FS3EMediaView_Init(FS3EMediaView *mv)
{
    if (!mv) return;
    memset(mv, 0, sizeof(*mv));
}

void FS3EMediaView_Dispose(FS3EMediaView *mv)
{
    if (!mv) return;

    if (mv->windowObj) {
        FS3EMediaView_Close(mv);
        /* Cascades: layout -> picGadget. */
        DisposeObject(mv->windowObj);
        mv->windowObj = NULL;
        mv->layout    = NULL;
        mv->picGadget = NULL;
    }

    if (mv->picClass) {
        bdbprintf_freeclass("FS3EMediaPicClass", mv->picClass);
        FreeClass(mv->picClass);
        mv->picClass = NULL;
    }

    BmImage_Free(&mv->image);
    if (mv->pendingUrl) { FreeVec(mv->pendingUrl); mv->pendingUrl = NULL; }
}

void FS3EMediaView_ShowUrl(FS3EMediaView *mv, const char *url, const char *posterAcct)
{
    FS3ENetFetchImageReq *req;

    if (!mv || !url || !url[0]) return;

    if (!mv->windowObj) {
        if (!CurrentMainScreen) return;
        if (!mediaview_ensure_window(mv)) return;
    }

    if (!mv->window) {
        if (CurrentMainScreen)
            SetAttrs(mv->windowObj, WA_CustomScreen, (ULONG)CurrentMainScreen, TAG_END);

        mv->window = (struct Window *)DoMethod(mv->windowObj, WM_OPEN, NULL);
        if (mv->window)
            mediaview_create_menu(mv);
    } else {
        WindowToFront(mv->window);
        ActivateWindow(mv->window);
    }

    if (posterAcct && posterAcct[0]) {
        strncpy(mv->poster, posterAcct, sizeof(mv->poster) - 1);
        mv->poster[sizeof(mv->poster) - 1] = '\0';
    } else {
        mv->poster[0] = '\0';
    }

    if (mv->pendingUrl) { FreeVec(mv->pendingUrl); mv->pendingUrl = NULL; }
    mv->pendingUrl = mediaview_strdup(url);

    BmImage_Free(&mv->image);
    mv->loading = TRUE;
    mediaview_push_picture(mv, "Loading media...");

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
            mediaview_push_picture(mv, NULL);
        } else {
            mediaview_push_picture(mv, "Couldn't display this image.");
        }

        /* fs3enf_IsTemp: a RAM:T download we're the last user of (see its
         * doc comment in fs3enet.h) -- the persistent-cache case (the
         * common one now that we always request keepOriginal=TRUE) needs
         * no cleanup, that copy is meant to stay, and is exactly what
         * mv->image.filePath/"Save Media..." expect to still be on disk. */
        if (reply->fs3enf_IsTemp)
            DeleteFile((STRPTR)reply->fs3enf_LocalPath);
    } else {
        BmImage_Free(&mv->image);
        mediaview_push_picture(mv, "Couldn't load media.");
    }
}

void FS3EMediaView_Close(FS3EMediaView *mv)
{
    if (!mv || !mv->windowObj || !mv->window) return;

    mediaview_dispose_menu(mv);

    GetAttr(WA_Left, mv->windowObj, (ULONG *)&mv->left);
    GetAttr(WA_Top,  mv->windowObj, (ULONG *)&mv->top);

    DoMethod(mv->windowObj, WM_CLOSE, NULL);
    mv->window = NULL;
}

BOOL FS3EMediaView_HandleInput(FS3EMediaView *mv)
{
    ULONG result;

    if (!mv || !mv->windowObj) return TRUE;
    if (!mv->window) return TRUE; /* closed, that's fine */

    while ((result = DoMethod(mv->windowObj, WM_HANDLEINPUT, NULL)) != WMHI_LASTMSG)
    {
        switch (result & WMHI_CLASSMASK)
        {
            case WMHI_CLOSEWINDOW:
                FS3EMediaView_Close(mv);
                return TRUE;

            case WMHI_MENUPICK: {
                /* Standard RKM chained-selection walk (right-mouse drag can
                 * select more than one item in one gesture). */
                UWORD menuCode = (UWORD)(result & WMHI_MENUMASK);
                while (menuCode != MENUNULL) {
                    struct MenuItem *item = ItemAddress(mv->menu, menuCode);
                    if (!item) break;
                    switch ((ULONG)GTMENUITEM_USERDATA(item)) {
                        case FS3EMV_MENU_CLOSE:
                            FS3EMediaView_Close(mv);
                            return TRUE;
                        case FS3EMV_MENU_SAVE:
                            mediaview_save_media(mv);
                            break;
                        default:
                            break;
                    }
                    menuCode = item->NextSelect;
                }
                break;
            }

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
