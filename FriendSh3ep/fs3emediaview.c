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
#include "fs3eboopsimessage.h"
#include <intuition/icclass.h>
#include <string.h>
#include <stdio.h>

/* <proto/mpega.h> does NOT compile with this toolchain (unconditionally
 * #includes pragmas/mpega_pragmas.h, which it doesn't ship) -- same issue
 * and same fix fs3eaudio.h/.c already document/use: clib/mpega_protos.h +
 * inline/mpega.h directly. This file doesn't call mpega.library itself
 * (all decode happens in fs3eaudio.c's own process); MPEGA_STREAM's fields
 * are read here only for duration/format info tied to the currently shown
 * audio attachment. */
#include <libraries/mpega.h>
#include <clib/mpega_protos.h>
#include <inline/mpega.h>

#include "fs3eaudio.h"

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

#include <gadgets/tapedeck.h>

#include <proto/slider.h>
#include <gadgets/slider.h>

#include <libraries/gadtools.h>
#include <libraries/asl.h>

#include "compilers.h"
#include "bdbprintf.h"
#include "fs3egadgetid.h"

/* Declared in friendsh3ep.c; fs3eaction.c already calls this the same way. */
extern BOOL FS3EApp_NetSend(ULONG type, APTR data, ULONG dataLen);

/* Opened via libraryTable in friendsh3ep.c. */
extern struct Library *GadToolsBase;
extern struct Library *AslBase;

#define FS3EMV_TITLE          "FriendSh3ep Media"
#define FS3EMV_PLACEHOLDER_W  260
#define FS3EMV_PLACEHOLDER_H  80
#define FS3EMV_MSG_LOADING_AUDIO "Loading audio..." /* shared between
                                                       * FS3EMediaView_ShowAudioUrl
                                                       * and the image-channel's
                                                       * own reply handler, see
                                                       * FS3EMediaView_OnFetchReply */

/* -------------------------------------------------------------------------
 * FS3EMediaPic -- private gadgetclass that just blits whatever BitMap it's
 * currently told about (see fs3emediaview.h's header comment). Modeled on
 * fs3eemojibox.c's FSEBGrid class: MakeClass'd once, one persistent
 * instance, attribute-driven redraw instead of ever being removed/rebuilt.
 * ---------------------------------------------------------------------- */

#define MEDIAPIC_Dummy      (TAG_USER + 0x4D50)  /* 'MP' */
#define MEDIAPIC_BitMap     (MEDIAPIC_Dummy + 1) /* [IS] struct BitMap * or NULL */
//no #define MEDIAPIC_MaskPlane  (MEDIAPIC_Dummy + 2) /* [IS] PLANEPTR or NULL */
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
   // PLANEPTR       mask;    /* borrowed; NULL if no transparency */
    UWORD          width, height;
    const char    *message; /* borrowed static string; see MEDIAPIC_Message above */
    struct Region *clipRegion;
} FS3EMediaPicInst;

#define MEDIAPIC_INST(cl, o) ((FS3EMediaPicInst *)INST_DATA((cl), (o)))
#define G(o) ((struct Gadget *)(o))

static ULONG FS3EMediaPic_OnRender(Class *cl, Object *o, struct gpRender *msg)
{
    FS3EMediaPicInst *inst = MEDIAPIC_INST(cl, o);
    struct RastPort  *rp   = msg->gpr_RPort;
    struct Gadget    *g    = (struct Gadget *)o;
    WORD gx = g->LeftEdge, gy = g->TopEdge;
    WORD gw = g->Width,    gh = g->Height;
    struct Region *oldClipRegion=NULL;

    if (!rp || gw <= 0 || gh <= 0) return 0;

    if (inst->clipRegion)
    {
        struct Rectangle framerect;
        framerect.MinX = G(o)->LeftEdge ;
        framerect.MinY = G(o)->TopEdge ;
        framerect.MaxX = G(o)->LeftEdge  + G(o)->Width -1 ;
        framerect.MaxY = G(o)->TopEdge + G(o)->Height -1 ;

        /* note you can go wild with Or/And boolean operation on geometry.
           But a single rect (should be) enough at Gadget level.
           Note there are issues on "Virtual.class" up to OS3.2.2 with this.
           One of the reason we don't use Virtual.class.
           */
        ClearRegion(inst->clipRegion);
        OrRectRegion(inst->clipRegion, &framerect);

       oldClipRegion = InstallClipRegion( rp->Layer, inst->clipRegion);
    }

    SetAPen(rp, 1);
    SetDrMd(rp, JAM1);
    if (inst->bitmap && inst->width > 0 && inst->height > 0) {
        /* no need for transparency in that case
        if (inst->mask) {
            BltMaskBitMapRastPort(inst->bitmap, 0, 0, rp, gx, gy,
                                  (LONG)inst->width, (LONG)inst->height,
                                  FS3EMV_MASK_MINTERM, inst->mask);
        } else {
        */
            BltBitMapRastPort(inst->bitmap, 0, 0, rp, gx, gy,
                              (LONG)inst->width, (LONG)inst->height, 0xC0);
       /* }*/
    } else
    {
        RectFill(rp, (LONG)gx, (LONG)gy, (LONG)(gx + gw - 1), (LONG)(gy + gh - 1));
    }
    if (inst->message) {

      //

        SetAPen(rp, 2);
        SetBPen(rp, 1);
        SetDrMd(rp, JAM1);
        Move(rp, (LONG)(gx + 8), (LONG)(gy + 20));
        Text(rp, (STRPTR)inst->message, (ULONG)strlen(inst->message));
    }
    InstallClipRegion( rp->Layer,oldClipRegion);

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


    inst->clipRegion = NewRegion();

    return (ULONG)newObj;
}


static ULONG EmojiGrid_OnDispose(Class *cl, Object *o, Msg msg)
{
    FS3EMediaPicInst *inst  = MEDIAPIC_INST(cl, o);
    if(inst->clipRegion) {
        DisposeRegion( inst->clipRegion );
        inst->clipRegion = NULL;
    }
    return DoSuperMethodA(cl, o, (APTR)msg);
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
            // case MEDIAPIC_MaskPlane:
            //     inst->mask = (PLANEPTR)tag->ti_Data;
            //     redraw = TRUE;
            //     break;
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
        case OM_DISPOSE: return EmojiGrid_OnDispose(cl, o, (struct gpRender *)msg);
        default:          return DoSuperMethodA(cl, o, (Msg)msg);
    }
}

/* -------------------------------------------------------------------------
 * FS3EMediaView -- window management
 * ---------------------------------------------------------------------- */

static struct NewMenu s_mvMenuTemplate[] = {
    { NM_TITLE, (STRPTR)"Media",         NULL,        0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Close",         (STRPTR)"K", 0, 0, (APTR)1 },
    { NM_ITEM,  (STRPTR)"Save Image...", (STRPTR)"S", 0, 0, (APTR)2 },
    { NM_ITEM,  (STRPTR)"Save Audio...", (STRPTR)"A", 0, 0, (APTR)3 },
    { NM_END,   NULL,                    NULL,        0, 0, NULL },
};
#define FS3EMV_MENU_CLOSE       1
#define FS3EMV_MENU_SAVE_IMAGE  2
#define FS3EMV_MENU_SAVE_AUDIO  3

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
    if(mv->last_w == w && mv->last_h == h) return;
    mv->last_w = w;
    mv->last_h = h;

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
    //PLANEPTR       mask = loaded ? mv->image.mask   : NULL;

    if (!mv->picGadget) return;

    if (mv->window) {
        SetGadgetAttrs((struct Gadget *)mv->picGadget, mv->window, NULL,
            MEDIAPIC_BitMap,    (ULONG)bm,
       //     MEDIAPIC_MaskPlane, (ULONG)mask,
            MEDIAPIC_Width,     (ULONG)w,
            MEDIAPIC_Height,    (ULONG)h,
            MEDIAPIC_Message,   (ULONG)placeholderMsg,
            TAG_DONE);
    } else {
        SetAttrs(mv->picGadget,
            MEDIAPIC_BitMap,    (ULONG)bm,
       //     MEDIAPIC_MaskPlane, (ULONG)mask,
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

    /* Stock tapedeck.gadget class, looked up by public name -- no
     * TAPEDECK_GetClass() macro ships with this NDK (see TapeDeckBase's
     * comment in friendsh3ep.c). TDECK_Tape TRUE selects the classic 5-
     * button tape controls (rewind/play/fast-forward/stop/pause) over the
     * 4-button animation-frame variant. WMHI_GADGETUP drives play/pause/stop
     * for audio via FS3EMediaView_TapeDeckPressed(); rewind/forward/begin/
     * frame/end are still unimplemented no-ops there, and video playback
     * isn't wired at all yet. */
    mv->tapeDeckGadget = (Object *)NewObject(NULL, "tapedeck.gadget",
        GA_ID,        GID_MEDIAVIEW_TAPEDECK,
        GA_RelVerify, TRUE,
        TDECK_Tape,   TRUE,
        TAG_END);

    if (!mv->tapeDeckGadget) {
        DisposeObject(mv->picGadget); /* not yet attached -- plain dispose is fine */
        mv->picGadget = NULL;
        bdbprintf_freeclass("FS3EMediaPicClass", mv->picClass);
        FreeClass(mv->picClass);
        mv->picClass = NULL;
        return FALSE;
    }

    /* Stock slider.gadget class, SLIDER_GetClass() -- unlike tapedeck.gadget
     * this one DOES export a real GetClass() stub (see SliderBase's comment
     * in friendsh3ep.c), same convention as layout/button/chooser/getfile.
     * Horizontal, to the right of tapeDeckGadget in transportRow below.
     * Level/Min/Max are placeholders (0..100) until this is wired to actual
     * playback position -- see GID_MEDIAVIEW_SLIDER. */
    mv->sliderGadget = (Object *)NewObject(SLIDER_GetClass(), NULL,
        GA_ID,              GID_MEDIAVIEW_SLIDER,
        ICA_TARGET,        (ULONG)TargetInstance,
        GA_RelVerify,        TRUE,
        SLIDER_Orientation,  SLIDER_HORIZONTAL,
        SLIDER_Min,          0,
        SLIDER_Max,          100,
        SLIDER_Level,        0,
        TAG_END);
    if (!mv->sliderGadget) {
        DisposeObject(mv->tapeDeckGadget); /* not yet attached -- plain dispose is fine */
        mv->tapeDeckGadget = NULL;
        DisposeObject(mv->picGadget);
        mv->picGadget = NULL;
        bdbprintf_freeclass("FS3EMediaPicClass", mv->picClass);
        FreeClass(mv->picClass);
        mv->picClass = NULL;
        return FALSE;
    }

    /* transportRow: tapeDeckGadget (its own natural/minimum width) then
     * sliderGadget filling the rest of the row -- first line of the outer
     * vertical layout, above picGadget. */
    {
        Object *transportRow = (Object *)NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation,  LAYOUT_ORIENT_HORIZ,
            LAYOUT_BevelStyle,   BVS_NONE,
            LAYOUT_SpaceOuter,   FALSE,
            LAYOUT_SpaceInner,   FALSE,
            LAYOUT_AddChild,     (ULONG)mv->tapeDeckGadget,
                CHILD_WeightedWidth,  0,
              //  CHILD_WeightedHeight, 0,
            LAYOUT_AddChild,     (ULONG)mv->sliderGadget,
                CHILD_WeightedWidth,  1,
               // CHILD_WeightedHeight, 0,
            TAG_END);
        if (!transportRow) {
            DisposeObject(mv->sliderGadget);
            mv->sliderGadget = NULL;
            DisposeObject(mv->tapeDeckGadget);
            mv->tapeDeckGadget = NULL;
            DisposeObject(mv->picGadget);
            mv->picGadget = NULL;
            bdbprintf_freeclass("FS3EMediaPicClass", mv->picClass);
            FreeClass(mv->picClass);
            mv->picClass = NULL;
            return FALSE;
        }

        /* picGadget's GM_DOMAIN result changes every time a differently-
         * sized picture loads -- CHILD_CacheDomain FALSE tells layout.gadget
         * to re-query it on every relayout instead of trusting its first
         * answer forever (see layout_gc.doc's CHILD_CacheDomain entry). */
        mv->layout = (Object *)NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation,  LAYOUT_ORIENT_VERT,
            LAYOUT_BevelStyle,   BVS_NONE,
            LAYOUT_SpaceOuter,   FALSE,
            LAYOUT_SpaceInner,   FALSE,
            LAYOUT_AddChild,     (ULONG)transportRow,
                CHILD_WeightedHeight, 0,
            LAYOUT_AddChild,     (ULONG)mv->picGadget,
                CHILD_WeightedHeight, 0,
                CHILD_CacheDomain,    FALSE,
            TAG_END);
        if (!mv->layout) {
            DisposeObject(transportRow); /* cascades: disposes tapeDeckGadget/sliderGadget too */
            mv->tapeDeckGadget = NULL;
            mv->sliderGadget   = NULL;
            DisposeObject(mv->picGadget);
            mv->picGadget = NULL;
            bdbprintf_freeclass("FS3EMediaPicClass", mv->picClass);
            FreeClass(mv->picClass);
            mv->picClass = NULL;
            return FALSE;
        }
    }

    mv->windowObj = (Object *)NewObject(WINDOW_GetClass(), NULL,
        WA_Left,            mv->left > 0 ? mv->left : 100,
        WA_Top,             mv->top  > 0 ? mv->top  : 60,
        WA_InnerWidth,      FS3EMV_PLACEHOLDER_W,
        WA_InnerHeight,     FS3EMV_PLACEHOLDER_H,
        WA_Title,           (ULONG)FS3EMV_TITLE,
        WA_IDCMP,           IDCMP_CLOSEWINDOW | IDCMP_MENUPICK | IDCMP_GADGETUP,
        WA_Flags,           WFLG_DRAGBAR | WFLG_DEPTHGADGET | WFLG_CLOSEGADGET |
                            WFLG_ACTIVATE | WFLG_SMART_REFRESH,
        WINDOW_ParentGroup, (ULONG)mv->layout,
        TAG_END);
    if (!mv->windowObj) {
        DisposeObject(mv->layout);   /* cascades: disposes transportRow/tapeDeckGadget/sliderGadget/picGadget too */
        mv->layout         = NULL;
        mv->tapeDeckGadget = NULL;
        mv->sliderGadget   = NULL;
        mv->picGadget      = NULL;
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

/* Sanitizes an @user@instance poster string for use as a filename base:
 * strips a leading '@', replaces '/' and ':' (path separators) with '_'.
 * Shared by both channels' default-name builders below. */
static void mediaview_sanitize_poster(const char *poster, char *out, ULONG outSize)
{
    const char *src = poster;
    ULONG i = 0;
    if (*src == '@') src++;
    for (; *src && i < outSize - 1; src++) {
        char c = *src;
        if (c == '/' || c == ':') c = '_';
        out[i++] = c;
    }
    out[i] = '\0';
}

/* Builds the default "Save Image..." filename (no path, no directory):
 * mv->imagePoster (sanitized) if set, else the cache file's own hash-id
 * name, plus the extension for whatever format BmImage_SniffFormat()
 * actually detects in the loaded file -- never trusted from the source
 * URL, which may have no extension at all (see fs3enet.h's
 * FS3ENetFetchImageReply doc comment: cache paths carry no extension
 * either). */
static void mediaview_build_image_default_name(FS3EMediaView *mv, char *out, ULONG outSize)
{
    char sanitized[128];
    const char *base;
    const char *ext;

    if (mv->imagePoster[0]) {
        mediaview_sanitize_poster(mv->imagePoster, sanitized, sizeof(sanitized));
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

/* Builds the default "Save Audio..." filename -- same rationale as
 * mediaview_build_image_default_name() above, but mv->audioPoster/
 * mv->audioLocalPath/mv->audioKey (this channel's own fields) and the
 * extension taken from the source URL instead of sniffed magic bytes:
 * audio cache files are hash-named, no extension, and there's no
 * BmImage_SniffFormat equivalent for audio -- the URL is the only source
 * left (same one FS3EMediaView_ShowAudioUrl's backend detection already
 * reads). */
static void mediaview_build_audio_default_name(FS3EMediaView *mv, char *out, ULONG outSize)
{
    char sanitized[128];
    char extBuf[16];
    const char *base;
    const char *ext;
    const char *dot;

    if (mv->audioPoster[0]) {
        mediaview_sanitize_poster(mv->audioPoster, sanitized, sizeof(sanitized));
        base = sanitized;
    } else {
        base = (const char *)FilePart((STRPTR)(mv->audioLocalPath[0] ? mv->audioLocalPath : "audio"));
    }

    dot = strrchr(mv->audioKey, '.');
    if (dot && strlen(dot) < sizeof(extBuf)) {
        strcpy(extBuf, dot);
        ext = extBuf;
    } else {
        ext = ".dat";
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

/* "Save Image..." action: ASL file requester defaulting to RAM: and the
 * name mediaview_build_image_default_name() derives, then a plain file
 * copy from the cache -- no re-fetch, no re-encode. Silently does nothing
 * if no image is currently loaded (still loading, or the fetch failed) or
 * if asl.library isn't open. Independent of the audio channel -- see
 * mediaview_save_audio() below. */
static void mediaview_save_image(FS3EMediaView *mv)
{
    struct FileRequester *req;
    char defaultName[160];

    if (!mv->window || !AslBase) return;
    if (!BmImage_IsLoaded(&mv->image) || !mv->image.filePath) return;

    mediaview_build_image_default_name(mv, defaultName, sizeof(defaultName));

    req = (struct FileRequester *)AllocAslRequestTags(ASL_FileRequest,
        ASLFR_Window,        (ULONG)mv->window,
        ASLFR_TitleText,     (ULONG)"Save Image",
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

/* "Save Audio..." action -- same rationale as mediaview_save_image()
 * above, but this channel's own fields (mv->hasAudio/audioLocalPath/
 * mediaview_build_audio_default_name()). Silently does nothing if no
 * audio attachment is currently loaded. Independent of the image
 * channel. */
static void mediaview_save_audio(FS3EMediaView *mv)
{
    struct FileRequester *req;
    char defaultName[160];

    if (!mv->window || !AslBase) return;
    if (!mv->hasAudio || !mv->audioLocalPath[0]) return;

    mediaview_build_audio_default_name(mv, defaultName, sizeof(defaultName));

    req = (struct FileRequester *)AllocAslRequestTags(ASL_FileRequest,
        ASLFR_Window,        (ULONG)mv->window,
        ASLFR_TitleText,     (ULONG)"Save Audio",
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

        mediaview_copy_file(mv->audioLocalPath, destPath);
    }

    FreeAslRequest(req);
}

/* Stops whatever audio attachment mv is currently playing/paused (if any)
 * and clears the audio channel's state back to inert -- shared by
 * Dispose()/Close() and ShowAudioUrl() (switching to a different audio
 * attachment; the audio channel only ever holds one track at a time,
 * unlike image-vs-audio which coexist -- see fs3emediaview.h's "two
 * channels" note). Never touches the image channel (mv->image). Leaves
 * audioRequestPort/audioReplyPort as-is; they're either about to be
 * overwritten by ShowAudioUrl, or harmless-but-unused while hasAudio is
 * FALSE otherwise. */
static void mediaview_stop_audio(FS3EMediaView *mv)
{
    if (mv->hasAudio && (mv->audioPlaying || mv->audioPaused))
        FS3EAudio_PlayStop(mv->audioRequestPort, mv->audioReplyPort);
    mv->hasAudio         = FALSE;
    mv->audioBackend     = FS3EMV_AUDIO_NONE;
    mv->audioPlaying     = FALSE;
    mv->audioPaused      = FALSE;
    mv->audioLocalPath[0] = '\0';
    mv->audioKey[0]      = '\0';
    mv->audioSliderProgLevel = 0;
    mv->audioTotalMs         = 0;
}

void FS3EMediaView_Init(FS3EMediaView *mv)
{
    if (!mv) return;
    memset(mv, 0, sizeof(*mv));
}

void FS3EMediaView_Dispose(FS3EMediaView *mv)
{
    if (!mv) return;

    mediaview_stop_audio(mv);

    if (mv->windowObj) {
        FS3EMediaView_Close(mv);
        /* Cascades: layout -> transportRow -> tapeDeckGadget, sliderGadget;
         * layout -> picGadget. */
        DisposeObject(mv->windowObj);
        mv->windowObj      = NULL;
        mv->layout         = NULL;
        mv->tapeDeckGadget = NULL;
        mv->sliderGadget   = NULL;
        mv->picGadget      = NULL;
    }

    if (mv->picClass) {
        bdbprintf_freeclass("FS3EMediaPicClass", mv->picClass);
        FreeClass(mv->picClass);
        mv->picClass = NULL;
    }

    BmImage_Free(&mv->image);
    if (mv->pendingImageUrl) { FreeVec(mv->pendingImageUrl); mv->pendingImageUrl = NULL; }
    if (mv->pendingAudioUrl) { FreeVec(mv->pendingAudioUrl); mv->pendingAudioUrl = NULL; }
}

/* Keeps tapedeck.gadget's two independent attributes -- TDECK_Mode (the
 * Play/Stop radio) and TDECK_Paused (a free-standing toggle, unrelated to
 * Mode) -- in sync with mv->audioPlaying/mv->audioPaused. Called after
 * every state change that isn't itself a click the gadget already applied
 * to its own attributes (a new track loading, a PLAY/STOP/PAUSE ack,
 * FINISHED, ERROR), so the gadget never shows stale state left over from
 * a previous track. */
static void mediaview_sync_tapedeck(FS3EMediaView *mv)
{
    if (!mv->tapeDeckGadget || !mv->window) return;
    SetGadgetAttrs((struct Gadget *)mv->tapeDeckGadget, mv->window, NULL,
        TDECK_Mode,   mv->audioPlaying ? BUT_PLAY : BUT_STOP,
        TDECK_Paused, (ULONG)mv->audioPaused,
        TAG_END);
}

void FS3EMediaView_Open(FS3EMediaView *mv)
{
    if (!mv->windowObj) {
        return;
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
}


void FS3EMediaView_ShowUrl(FS3EMediaView *mv, const char *url, const char *posterAcct)
{
    FS3ENetFetchImageReq *req;

    if (!mv || !url || !url[0]) return;

    if (!mv->windowObj) {
        if (!CurrentMainScreen) return;
        if (!mediaview_ensure_window(mv)) return;
    }

    FS3EMediaView_Open(mv);


    if (posterAcct && posterAcct[0]) {
        strncpy(mv->imagePoster, posterAcct, sizeof(mv->imagePoster) - 1);
        mv->imagePoster[sizeof(mv->imagePoster) - 1] = '\0';
    } else {
        mv->imagePoster[0] = '\0';
    }

    if (mv->pendingImageUrl && strcmp(url, mv->pendingImageUrl) == 0) return;

    /* The audio channel, if any, is left exactly as it was -- see
     * fs3emediaview.h's "two channels" note. An earlier version of this
     * function stopped/cleared audio here too. */

    if (mv->pendingImageUrl) { FreeVec(mv->pendingImageUrl); mv->pendingImageUrl = NULL; }
    mv->pendingImageUrl = mediaview_strdup(url);
    mv->progressBytesSoFar = 0;
    mv->progressTotalBytes = 0;

    BmImage_Free(&mv->image);
    mv->imageLoading = TRUE;
    mediaview_push_picture(mv, "Loading media...");

    /* keepOriginal=TRUE: an explicit click means the user wants this image,
     * worth persisting from now on even if "Keep big thumbnails" is off --
     * see fs3emediaview.h's header comment. */
    req = FS3ENetFetchImageReq_Alloc(url, url, FS3E_CACHE_SUBDIR_THUMBNAILS, TRUE, TRUE);
    if (req) FS3EApp_NetSend(FS3ENETQ_FETCH_IMAGE, req, sizeof(*req));
}

/* Extension -> playback backend, matched against url (not the cache's
 * hash-named local file, which carries no extension) -- see
 * FS3EMVAudioBackend's doc comment in fs3emediaview.h for why only
 * FS3EMV_AUDIO_MPEGA is actually playable today. */
static ULONG mediaview_audio_backend_for_url(const char *url)
{
    const char *dot = url ? strrchr(url, '.') : NULL;
    if (!dot) return FS3EMV_AUDIO_UNSUPPORTED;
    if (Stricmp((STRPTR)dot, (STRPTR)".mp3") == 0) return FS3EMV_AUDIO_MPEGA;
    return FS3EMV_AUDIO_UNSUPPORTED; /* .wav, .ogg, anything else */
}

/* FS3EMediaView_OnFetchReply()'s audio branch -- the fetched file is now
 * on disk at reply->fs3enf_LocalPath; decide a backend from the URL
 * (mv->audioKey) and, for MPEGA, hand the local path to fs3eaudio.c. */
static void mediaview_start_audio(FS3EMediaView *mv, ULONG result,
                                   const FS3ENetFetchImageReply *reply)
{
    if (result != FS3ENETR_OK || !reply || !reply->fs3enf_LocalPath) {
        mediaview_push_picture(mv, "Couldn't load audio.");
        return;
    }

    strncpy(mv->audioLocalPath, reply->fs3enf_LocalPath, sizeof(mv->audioLocalPath) - 1);
    mv->audioLocalPath[sizeof(mv->audioLocalPath) - 1] = '\0';

    mv->audioBackend = mediaview_audio_backend_for_url(mv->audioKey);

    if (mv->audioBackend == FS3EMV_AUDIO_MPEGA) {
        /* FALSE here just means the request couldn't even be queued (no
         * audio process/port) -- a missing mpega.library/ahi.device
         * instead fails asynchronously, handled by
         * FS3EMediaView_OnAudioReply()'s FS3EAUDIOR_ERROR case. */
        if (FS3EAudio_Play(mv->audioRequestPort, mv->audioReplyPort,
                            mv->audioLocalPath, mv->audioKey))
        {
            mediaview_push_picture(mv, "Starting playback...");
        } else {
            mediaview_push_picture(mv, "Couldn't start playback.");
        }
    } else {
        mediaview_push_picture(mv, "This audio format isn't supported yet.");
    }

    /* fs3enf_IsTemp can't happen here -- FS3E_CACHE_SUBDIR_AUDIO is always
     * fetched keepOriginal=TRUE (see ShowAudioUrl), same reasoning
     * FS3EMediaView_OnFetchReply's picture branch already documents. */
}

void FS3EMediaView_ShowAudioUrl(FS3EMediaView *mv, const char *url,
                                 const char *posterAcct,
                                 struct MsgPort *audioRequestPort,
                                 struct MsgPort *audioReplyPort)
{
    FS3ENetFetchImageReq *req;

    if (!mv || !url || !url[0]) return;

    if (!mv->windowObj) {
        if (!CurrentMainScreen) return;
        if (!mediaview_ensure_window(mv)) return;
    }

    FS3EMediaView_Open(mv);

    if (posterAcct && posterAcct[0]) {
        strncpy(mv->audioPoster, posterAcct, sizeof(mv->audioPoster) - 1);
        mv->audioPoster[sizeof(mv->audioPoster) - 1] = '\0';
    } else {
        mv->audioPoster[0] = '\0';
    }

    /* Stop whatever audio attachment was previously playing in this same
     * window before switching to a new one -- the audio channel only ever
     * holds one track at a time. The image channel, if any, is left
     * exactly as it was -- see fs3emediaview.h's "two channels" note. An
     * earlier version of this function also cleared mv->image here. */
    mediaview_stop_audio(mv);

    if (mv->pendingAudioUrl) { FreeVec(mv->pendingAudioUrl); mv->pendingAudioUrl = NULL; }
    mv->pendingAudioUrl = mediaview_strdup(url);

    mv->hasAudio           = TRUE;
    mv->audioRequestPort  = audioRequestPort;
    mv->audioReplyPort    = audioReplyPort;
    strncpy(mv->audioKey, url, sizeof(mv->audioKey) - 1);
    mv->audioKey[sizeof(mv->audioKey) - 1] = '\0';

    mv->audioLoading = TRUE;
    mediaview_push_picture(mv, FS3EMV_MSG_LOADING_AUDIO);
    mediaview_sync_tapedeck(mv);

    /* Own subdir, always kept -- see FS3E_CACHE_SUBDIR_AUDIO's doc comment
     * in fs3enet.h. No progress pings (FALSE) -- unlike a big picture
     * download, wiring FS3EMediaView_OnFetchProgress for this path too
     * isn't worth it until audio files turn out to matter here. */
    req = FS3ENetFetchImageReq_Alloc(url, url, FS3E_CACHE_SUBDIR_AUDIO, TRUE, FALSE);
    if (req) FS3EApp_NetSend(FS3ENETQ_FETCH_IMAGE, req, sizeof(*req));
}

void FS3EMediaView_OnFetchReply(FS3EMediaView *mv, ULONG result,
                                 const FS3ENetFetchImageReply *reply)
{
    if (!mv || !reply || !reply->fs3enf_Key) return;

    /* Two independent channels, two independent pending fetches -- see
     * fs3emediaview.h's "two channels" note. Checked in turn rather than a
     * single shared pendingUrl (this function's earlier, single-channel
     * design), since both can have a download in flight at once. */
    if (mv->pendingImageUrl && strcmp(reply->fs3enf_Key, mv->pendingImageUrl) == 0) {
        FreeVec(mv->pendingImageUrl);
        mv->pendingImageUrl = NULL;
        mv->imageLoading    = FALSE;

        if (result == FS3ENETR_OK && reply->fs3enf_LocalPath) {
            BOOL   ok;
            UWORD  maxW = 0, maxH = 0;

            /* Cap at the screen size (minus a margin) rather than always
             * showing a toot attachment at its full native resolution --
             * PDTM_SCALE halves it once, in-datatype, if it doesn't fit
             * (see BmImage_LoadFitScreen's own doc comment in bmimage.h
             * for why this is a single halving, not a fit-to-bounds loop,
             * and why it's not the same pipeline BmImage_LoadScaled()
             * uses for thumbnails). */
            if (CurrentMainScreen) {
                maxW = (UWORD)(CurrentMainScreen->Width  > 64 ? CurrentMainScreen->Width  - 64 : CurrentMainScreen->Width);
                maxH = (UWORD)(CurrentMainScreen->Height > 64 ? CurrentMainScreen->Height - 64 : CurrentMainScreen->Height);
            }

            BmImage_Free(&mv->image);
            ok = BmImage_Init(&mv->image, reply->fs3enf_LocalPath) &&
                 BmImage_LoadFitScreen(&mv->image, CurrentMainScreen, maxW, maxH);
            if (ok) {
                /* The image is only one of up to two channels this window
                 * can be showing at once (see fs3emediaview.h's "two
                 * channels" note) -- if the audio channel is still
                 * in-flight, its "Loading audio..." message (set by
                 * FS3EMediaView_ShowAudioUrl) must stay up until THAT
                 * reply lands too (FS3EMediaView_OnFetchReply's audio
                 * branch below, or FS3EMediaView_OnAudioReply once
                 * playback actually starts) instead of being clobbered
                 * back to NULL just because the image happened to finish
                 * first. */
                mediaview_push_picture(mv,
                    (mv->hasAudio && mv->audioLoading) ? FS3EMV_MSG_LOADING_AUDIO : NULL);
            } else {
                mediaview_push_picture(mv, "Couldn't display this image.");
            }

            /* fs3enf_IsTemp: a RAM:T download we're the last user of (see its
             * doc comment in fs3enet.h) -- the persistent-cache case (the
             * common one now that we always request keepOriginal=TRUE) needs
             * no cleanup, that copy is meant to stay, and is exactly what
             * mv->image.filePath/"Save Image..." expect to still be on disk. */
            if (reply->fs3enf_IsTemp)
                DeleteFile((STRPTR)reply->fs3enf_LocalPath);
        } else {
            BmImage_Free(&mv->image);
            mediaview_push_picture(mv, "Couldn't load media.");
        }
        return;
    }

    if (mv->pendingAudioUrl && strcmp(reply->fs3enf_Key, mv->pendingAudioUrl) == 0) {
        FreeVec(mv->pendingAudioUrl);
        mv->pendingAudioUrl = NULL;
        mv->audioLoading    = FALSE;

        mediaview_start_audio(mv, result, reply);
        return;
    }
}

void FS3EMediaView_OnFetchProgress(FS3EMediaView *mv, const char *key,
                                    ULONG bytesSoFar, ULONG totalBytes)
{
    if (!mv || !mv->pendingImageUrl || !key) return;
    if (strcmp(key, mv->pendingImageUrl) != 0) return;

    mv->progressBytesSoFar = bytesSoFar;
    mv->progressTotalBytes = totalBytes;
}

void FS3EMediaView_OnAudioReply(FS3EMediaView *mv, const FS3EAudioMessage *msg)
{
    if (!mv || !mv->hasAudio || !msg) return;
    if (strcmp(msg->fs3eam_Key, mv->audioKey) != 0) return;

    switch (msg->fs3eam_Result) {
        case FS3EAUDIOR_OK:
            switch (msg->fs3eam_Type) {
                case FS3EAUDIOQ_PLAY:
                    mv->audioPlaying = TRUE;
                    mv->audioPaused  = FALSE;
                    mediaview_push_picture(mv, "Playing...");
                    mediaview_sync_tapedeck(mv);
                    break;
                case FS3EAUDIOQ_STOP:
                    mv->audioPlaying = FALSE;
                    mv->audioPaused  = FALSE;
                    mediaview_push_picture(mv, "Stopped.");
                    mediaview_sync_tapedeck(mv);
                    break;
                case FS3EAUDIOQ_PAUSE:
                    /* mv->audioPaused is already accurate -- set optimistically
                     * by FS3EMediaView_TapeDeckPressed() at click time, not
                     * from this ack. This ack can arrive late enough for a
                     * later click to have already moved mv->audioPaused on
                     * again; blindly trusting fs3eam_Pause (an echo of
                     * whatever THIS particular request asked for) here would
                     * stomp that newer state with a stale one. */
                    mediaview_push_picture(mv, mv->audioPaused ? "Paused." : "Playing...");
                    mediaview_sync_tapedeck(mv);
                    break;
                default:
                    break;
            }
            break;

        case FS3EAUDIOR_ERROR:
            if (msg->fs3eam_Type == FS3EAUDIOQ_SEEK) {
                /* Seek target outside the stream (MPEGA_seek() returning
                 * MPEGA_ERR_EOF) -- playback position/state is unaffected,
                 * nothing to reset here; don't pretend the whole track
                 * failed over a bad drag target. */
                break;
            }
            mv->audioPlaying = FALSE;
            mv->audioPaused  = FALSE;
            /* mv->audioLocalPath is already filled in (set before this
             * track was ever handed to FS3EAudio_Play(), see
             * mediaview_start_audio()) and stays that way -- "Save
             * Media..." still works even though playback failed, same as
             * a picture that failed to decode still lets you save the
             * original bytes. */
            mediaview_push_picture(mv, mv->audioBackend == FS3EMV_AUDIO_MPEGA
                ? "Needs AHI & mpega.library to play mp3."
                : "Couldn't play this audio.");
            mediaview_sync_tapedeck(mv);
            break;

        case FS3EAUDIOR_FINISHED:
            mv->audioPlaying = FALSE;
            mv->audioPaused  = FALSE;
            mediaview_push_picture(mv, "Finished.");
            mediaview_sync_tapedeck(mv);
            if (mv->sliderGadget)
                SetGadgetAttrs((struct Gadget *)mv->sliderGadget, mv->window, NULL,
                    SLIDER_Level, 0,
                    TAG_END);
            mv->audioSliderProgLevel = 0;
            break;

        case FS3EAUDIOR_PROGRESS: {
            ULONG level;
            if (!mv->sliderGadget || msg->fs3eam_TotalMs == 0) break;
            level = (msg->fs3eam_ElapsedMs * 100) / msg->fs3eam_TotalMs;
            if (level > 100) level = 100;
            SetGadgetAttrs((struct Gadget *)mv->sliderGadget, mv->window, NULL,
                SLIDER_Level, level,
                TAG_END);
            mv->audioSliderProgLevel = level;
            mv->audioTotalMs         = msg->fs3eam_TotalMs;
            break;
        }

        default:
            break;
    }
}

/* GID_MEDIAVIEW_TAPEDECK's WMHI_GADGETUP handler. tapedeck.gadget exposes
 * two independent attributes: TDECK_Mode (Play/Stop, mutually exclusive --
 * the gadget itself updates this on a Play or Stop click before GADGETUP
 * fires) and TDECK_Paused (a free-standing toggle the gadget flips on its
 * own on a Pause click, unrelated to Mode -- pressing Pause does NOT
 * change Mode). Since a Pause click never shows up in Mode, it's detected
 * here by noticing TDECK_Paused no longer matches mv->audioPaused (the
 * last value we know is actually true, kept in sync via
 * mediaview_sync_tapedeck()). A no-op for anything but the currently-
 * showing MPEGA audio attachment -- pictures don't have a tapedeck to
 * press, and unsupported formats (.wav/.ogg) have nothing to command yet. */
void FS3EMediaView_TapeDeckPressed(FS3EMediaView *mv)
{
    ULONG mode;
    ULONG paused = FALSE;

    if (!mv || !mv->hasAudio || !mv->tapeDeckGadget) return;
    if (mv->audioBackend != FS3EMV_AUDIO_MPEGA) return;

    if (!GetAttr(TDECK_Mode, mv->tapeDeckGadget, &mode)) return;
    GetAttr(TDECK_Paused, mv->tapeDeckGadget, &paused);

    if ((BOOL)paused != mv->audioPaused) {
        BOOL hadSomethingToPause = mv->audioPlaying || mv->audioPaused;

        /* Optimistic -- set mv->audioPaused NOW rather than waiting for the
         * async ack: fs3eaudio.c's process can take up to a buffer's worth
         * of playback to even see this request (it may be blocked in
         * WaitIO), so a quick second click can land before the first one's
         * ack ever arrives. Waiting for the ack left mv->audioPaused stale
         * across that window, which made this exact comparison see "no
         * change" on the second click and fall through to BUT_PLAY's
         * fresh-restart branch below instead of resuming. */
        mv->audioPaused = hadSomethingToPause ? (BOOL)paused : FALSE;

        if (hadSomethingToPause)
            FS3EAudio_Pause(mv->audioRequestPort, mv->audioReplyPort, (BOOL)paused);
        else
            mediaview_sync_tapedeck(mv); /* nothing to pause -- snap the gadget back */
        return;
    }

    switch (mode) {
        case BUT_PLAY:
            /* Always (re)start unless resuming from pause -- never gated
             * on mv->audioPlaying, which can still be stale (a Stop just
             * sent hasn't been acked yet): a fresh PLAY request is safe
             * regardless, fs3eaudio.c replaces whatever was playing. */
            if (mv->audioPaused)
                FS3EAudio_Pause(mv->audioRequestPort, mv->audioReplyPort, FALSE);
            else
                FS3EAudio_Play(mv->audioRequestPort, mv->audioReplyPort,
                                mv->audioLocalPath, mv->audioKey);
            break;

        case BUT_STOP:
            /* Unconditional, same reasoning as BUT_PLAY above -- always
             * safe, fs3eaudio.c's STOP handler is a no-op if nothing is
             * loaded. */
            FS3EAudio_PlayStop(mv->audioRequestPort, mv->audioReplyPort);
            break;

        default:
            /* rewind/forward/begin/frame/end -- unimplemented, no-op. */
            break;
    }
}

void FS3EMediaView_SliderMoved(FS3EMediaView *mv, ULONG newLevel)
{
    ULONG seekMs;

    if (!mv || !mv->hasAudio || mv->audioBackend != FS3EMV_AUDIO_MPEGA) return;
    if (mv->audioTotalMs == 0) return; /* nothing seekable known yet */
    if (newLevel > 100) newLevel = 100;

    /* Echo of our own last PROGRESS/FINISHED update, not a user drag --
     * see mv's own field comment in fs3emediaview.h for why the two can't
     * be told apart any other way (slider.gadget's OM_NOTIFY doesn't say
     * who moved it). */
    if (newLevel == mv->audioSliderProgLevel) return;

    /* newLevel<=100 and audioTotalMs is a toot audio attachment's duration
     * (seconds to a few minutes in practice) -- nowhere near the ~42.9M ms
     * (~11.9h) this multiply could handle before overflowing a 32-bit
     * ULONG, so no split-safe math needed here unlike the ms<->samples
     * conversions in fs3eaudio.c. */
    seekMs = (newLevel * mv->audioTotalMs) / 100;

    FS3EAudio_Seek(mv->audioRequestPort, mv->audioReplyPort, seekMs);

    /* Optimistic, same reasoning as the BUT_PLAY/paused branch above --
     * update now rather than waiting for the async ack, so a second drag
     * before the first SEEK is acked still compares against the right
     * "last known" level instead of stale state. */
    mv->audioSliderProgLevel = newLevel;
}

void FS3EMediaView_Close(FS3EMediaView *mv)
{
    if (!mv || !mv->windowObj || !mv->window) return;

    /* Closing the window with a track still playing/paused would otherwise
     * leave fs3eaudio.c happily streaming to a window that's no longer
     * there to show its state -- same "stop whatever the window was doing"
     * reasoning ShowUrl()/ShowAudioUrl() already apply when switching to a
     * different attachment. */
    mediaview_stop_audio(mv);

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

            case WMHI_GADGETUP: {
                ULONG gadId = result & WMHI_GADGETMASK;
                if (gadId == GID_MEDIAVIEW_TAPEDECK)
                    FS3EMediaView_TapeDeckPressed(mv);
                break;
            }

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
                        case FS3EMV_MENU_SAVE_IMAGE:
                            mediaview_save_image(mv);
                            break;
                        case FS3EMV_MENU_SAVE_AUDIO:
                            mediaview_save_audio(mv);
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
