/*
 * TitleBarLayout - BOOPSI layout.gadget subclass for the FriendSh3ep
 * title bar (Part A).
 *
 * Overrides OM_NEW, GM_DOMAIN, GM_LAYOUT, GM_RENDER, GM_HITTEST.
 * Does NOT call super in GM_LAYOUT or GM_RENDER — positions every child
 * directly so the two-row arrangement is fully under our control, and
 * since layout.gadget's own GM_RENDER only knows how to erase the gaps its
 * own GM_LAYOUT computed (which never runs here), we own erasing and child
 * rendering outright too (see TitleBarLayout_OnRender). All other methods
 * chain to layout.gadget via DoSuperMethodA.
 *
 * Row 1 (height = max(dpiH, button cell height)):
 *   close at far-left, iconify / altpos / depth packed at far-right.
 *   Each button is sized to TBLAYOUT_Style's tbButtonWidth x tbButtonHeight
 *   (derived from the theme's tbbuttons.png, see fs3estyle.c) when a theme
 *   image is loaded, else falls back to a dpiH x dpiH square.  Buttons
 *   smaller than the row are vertically centered.
 *   Empty space between them acts as the drag area.
 *
 * Row 2 (height = max(dpiH, avatarSize + 2×avatarGap)):
 *   user-icon at far-left, sized (iconSz × iconSz) with TBLAYOUT_Style's
 *   avatarGap as padding on the left, top and bottom (falls back to
 *   TBLAYOUT_ICON_MARGIN with no style); settings/accounts/toot+ buttons
 *   packed at far-right.
 *
 *   The user icon is NOT a child gadget -- it used to be a button.gadget
 *   wrapping an images/bitmap.image (BITMAP_BitMap/BITMAP_MaskPlane
 *   captured by value at NewObject() time), which needed a fresh wrapper
 *   object rebuilt and swapped in every time the underlying avatar bitmap
 *   changed (new download, font/DPI rescale, ...) -- consistently buggy
 *   (see git history) and just an unnecessary layer of BOOPSI object
 *   lifecycle on top of what TootTimeline already does directly. Instead,
 *   TitleBarLayout_OnRender() draws the avatar straight into the RastPort
 *   with RgbImage_DrawScaled(), reading fresh from TBLAYOUT_AvatarImages/
 *   TBLAYOUT_AccountAcct on every render -- same mechanism, same avatar
 *   cache, as toot avatars in TootTimeline. GM_LAYOUT still computes and
 *   stores the icon's rect (avatarRectX/Y/Size) exactly where the button
 *   used to sit, for GM_RENDER to draw into. It's not click-targetable --
 *   same as the button it replaces, which had no GA_ID/click handling
 *   wired up either -- clicks there now just start a window drag like the
 *   rest of the empty title bar strip (see TitleBarLayout_OnHitTest).
 *
 * Total minimum/maximum height = row1Height + row2Height (fixed — no
 * vertical stretch). Caller must DoMethod(window_obj, WM_RETHINK) after
 * loading/changing a theme so GM_DOMAIN/GM_LAYOUT re-run with the new
 * button cell size.
 */

/* Minimum size (pixels) for the user-icon image in row 2 */
#define TBLAYOUT_ICON_SIZE_MIN  32
/* Fallback padding around the icon (top, bottom, left) when no
 * TBLAYOUT_Style is set -- normally TBLAYOUT_Style's avatarGap is used. */
#define TBLAYOUT_ICON_MARGIN     2

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <proto/layers.h>
#include <proto/graphics.h>
#include <clib/alib_protos.h>
#include <intuition/gadgetclass.h>
#include <gadgets/layout.h>
#include <proto/layout.h>

#include <proto/utf8rastport.h>
#include <libraries/utf8rastport.h>
#include <string.h>

#include "../compilers.h"
#include "fs3etitlebar.h"

#include "../bdbprintf.h"
#include "../patch9.h"
#include "../avatarimages.h"

/* Drag state owned by the main loop — set here during GM_GOACTIVE so that
 * WMHI_MOUSEMOVE events in the same WM_HANDLEINPUT drain already see the flag. */
extern BOOL windowDragActive;
extern WORD windowDragLastScreenX;
extern WORD windowDragLastScreenY;

#ifndef G
#define G(o) ((struct Gadget *)(o))
#endif

/* Title bar button cell size: from the theme image when loaded
 * (FS3EStyle_LoadThemeImages sets tbButtonWidth/Height > 0), else a
 * dpiH x dpiH square. */
static void tbl_button_size(UWORD dpiH, FS3EStyle *style, WORD *btnW, WORD *btnH)
{
    *btnW = (style && style->tbButtonWidth  > 0) ? style->tbButtonWidth  : (WORD)dpiH;
    *btnH = (style && style->tbButtonHeight > 0) ? style->tbButtonHeight : (WORD)dpiH;
}

/* Height of row 1: the button cell height, never less than dpiH */
static WORD tbl_row1_height(UWORD dpiH, FS3EStyle *style)
{
    WORD btnW, btnH;
    tbl_button_size(dpiH, style, &btnW, &btnH);
    return (btnH > (WORD)dpiH) ? btnH : (WORD)dpiH;
}

/* Margin around the user icon (left, top, bottom): TBLAYOUT_Style's
 * avatarGap when available, else TBLAYOUT_ICON_MARGIN. */
static WORD tbl_avatar_gap(FS3EStyle *style)
{
    return (style && style->avatarGap > 0) ? style->avatarGap : TBLAYOUT_ICON_MARGIN;
}

/* Height of row 2: avatarSize + top/bottom avatarGap margins, never less
 * than dpiH */
static WORD tbl_row2_height(UWORD dpiH, UWORD avatarSize, FS3EStyle *style)
{
    WORD iconRow = (WORD)(avatarSize + 2 * tbl_avatar_gap(style));
    return (iconRow > (WORD)dpiH) ? iconRow : (WORD)dpiH;
}

/* Total fixed height of the title bar */
static WORD tbl_total_height(UWORD dpiH, FS3EStyle *style, UWORD avatarSize)
{
    return tbl_row1_height(dpiH, style) + tbl_row2_height(dpiH, avatarSize, style);
}

typedef struct {
    Object    *children[TBLAYOUT_NUMCHILDREN];
    UWORD      childCount;
    UWORD      dpiHeight;
    FS3EStyle *style;

    /* Row-2 user icon: not a child gadget -- drawn directly in
     * TitleBarLayout_OnRender() (see the file header comment). Rect
     * computed in GM_LAYOUT; avatarImages/accountAcct set via
     * TBLAYOUT_AvatarImages/TBLAYOUT_AccountAcct. */
    struct AvatarImages *avatarImages;   /* not owned */
    char                 *accountAcct;   /* AllocVec'd copy; NULL = no account */
    WORD                  avatarX, avatarY, avatarSize;
//    struct Task *allowedProcess;
} TitleBarLayoutData;

/* AllocVec'd copy of s, or NULL for NULL/"" (matches dup_str() elsewhere
 * in this codebase, kept local since this file doesn't share a header
 * with TootTimeline's copy). */
static char *tbl_dup_str(const char *s)
{
    ULONG len;
    char *copy;
    if (!s || !s[0]) return NULL;
    len  = (ULONG)strlen(s);
    copy = (char *)AllocVec(len + 1, MEMF_ANY);
    if (copy) CopyMem((APTR)s, copy, len + 1);
    return copy;
}

ULONG ASM SAVEDS TitleBarLayout_Dispatch(
    REG(a0, Class  *cl),
    REG(a2, Object *o),
    REG(a1, Msg     msg));

static void setBounds(Object *child, WORD l, WORD t, WORD w, WORD h)
{
    struct Gadget *gad = G(child);
    gad->LeftEdge = l;
    gad->TopEdge  = t;
    gad->Width    = (w > 0) ? w : 1;
    gad->Height   = (h > 0) ? h : 1;
}

/* ------------------------------------------------------------------ */
/* OM_NEW                                                               */
/* ------------------------------------------------------------------ */

static ULONG TitleBarLayout_OnNew(Class *cl, Object *o, struct opSet *msg)
{
    TitleBarLayoutData *inst;
    Object             *newObj;
    struct TagItem     *state;
    struct TagItem     *tag;

    newObj = (Object *)DoSuperMethodA(cl, o, (APTR)msg);
    if (!newObj) return 0;

    inst               = (TitleBarLayoutData *)INST_DATA(cl, newObj);
    inst->childCount   = 0;
    inst->dpiHeight    = 14;
    inst->style        = NULL;
    inst->avatarImages = NULL;
    inst->accountAcct  = NULL;

    tag = FindTagItem(TBLAYOUT_DpiHeight, msg->ops_AttrList);
    if (tag) inst->dpiHeight = (UWORD)tag->ti_Data;

    tag = FindTagItem(TBLAYOUT_Style, msg->ops_AttrList);
    if (tag) inst->style = (FS3EStyle *)tag->ti_Data;

    tag = FindTagItem(TBLAYOUT_AvatarImages, msg->ops_AttrList);
    if (tag) inst->avatarImages = (struct AvatarImages *)tag->ti_Data;

    tag = FindTagItem(TBLAYOUT_AccountAcct, msg->ops_AttrList);
    if (tag) inst->accountAcct = tbl_dup_str((const char *)tag->ti_Data);

    state = msg->ops_AttrList;
    while ((tag = NextTagItem(&state)) != NULL) {
        if (tag->ti_Tag == LAYOUT_AddChild &&
            inst->childCount < TBLAYOUT_NUMCHILDREN)
        {
            inst->children[inst->childCount++] = (Object *)tag->ti_Data;
        }
    }

//    inst->allowedProcess = FindTask(NULL);

    return (ULONG)newObj;
}

/* ------------------------------------------------------------------ */
/* OM_SET                                                               */
/* ------------------------------------------------------------------ */

static ULONG TitleBarLayout_OnSet(Class *cl, Object *o, struct opSet *msg)
{
    TitleBarLayoutData *inst = (TitleBarLayoutData *)INST_DATA(cl, o);
    struct TagItem *tstate = msg->ops_AttrList;
    struct TagItem *tag;

    while ((tag = NextTagItem(&tstate)) != NULL) {
        switch (tag->ti_Tag) {
            case TBLAYOUT_AvatarImages:
                inst->avatarImages = (struct AvatarImages *)tag->ti_Data;
                break;
            case TBLAYOUT_AccountAcct:
                if (inst->accountAcct) { FreeVec(inst->accountAcct); inst->accountAcct = NULL; }
                inst->accountAcct = tbl_dup_str((const char *)tag->ti_Data);
                break;
            default:
                break;
        }
    }
    return DoSuperMethodA(cl, o, (APTR)msg);
}

/* ------------------------------------------------------------------ */
/* OM_DISPOSE                                                           */
/* ------------------------------------------------------------------ */

static ULONG TitleBarLayout_OnDispose(Class *cl, Object *o, Msg msg)
{
    TitleBarLayoutData *inst = (TitleBarLayoutData *)INST_DATA(cl, o);
    if (inst->accountAcct) { FreeVec(inst->accountAcct); inst->accountAcct = NULL; }
    return DoSuperMethodA(cl, o, (APTR)msg);
}

/* ------------------------------------------------------------------ */
/* GM_DOMAIN                                                            */
/* ------------------------------------------------------------------ */

static void prepDomain( struct gpDomain *d, struct GadgetInfo *gi)
{
    d->MethodID        = GM_DOMAIN;
    d->gpd_GInfo       = gi;
    d->gpd_RPort       = NULL;
    d->gpd_Which       = GDOMAIN_MINIMUM;
    d->gpd_Domain.Left = 0;
    d->gpd_Domain.Top  = 0;
    d->gpd_Domain.Width  = 1;
    d->gpd_Domain.Height = 1;
    d->gpd_Attrs       = 0;
}

static ULONG TitleBarLayout_OnDomain(Class *cl, Object *o, struct gpDomain *msg)
{
    TitleBarLayoutData *inst     = (TitleBarLayoutData *)INST_DATA(cl, o);
    struct IBox        *domain   = &msg->gpd_Domain;
    UWORD               avatarSz = inst->style ? (UWORD)inst->style->avatarSize : TBLAYOUT_ICON_SIZE_MIN;
    WORD                fixedH   = tbl_total_height(inst->dpiHeight, inst->style, avatarSz);
    WORD                btnW, btnH;

    tbl_button_size(inst->dpiHeight, inst->style, &btnW, &btnH);

    switch (msg->gpd_Which) {
        case GDOMAIN_MINIMUM:
            /* close + (depth, altpos, iconify) with a quarter-button gap
             * between each of the right-hand three, see GM_LAYOUT. */
            domain->Width  = (WORD)(btnW * 4 + (btnW / 4) * 2);
            domain->Height = fixedH;
            break;
        case GDOMAIN_MAXIMUM:
            domain->Width  = 32767;
            domain->Height = fixedH;   /* fixed height — no vertical stretch */
            break;
        case GDOMAIN_NOMINAL:
        default:
            domain->Width  = 200;
            domain->Height = fixedH;
            break;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* GM_LAYOUT                                                            */
/* ------------------------------------------------------------------ */

static ULONG TitleBarLayout_OnLayout(Class *cl, Object *o, struct gpLayout *msg)
{
    TitleBarLayoutData *inst   = (TitleBarLayoutData *)INST_DATA(cl, o);
    UWORD               avSz   = inst->style ? (UWORD)inst->style->avatarSize : TBLAYOUT_ICON_SIZE_MIN;
    WORD  left  = G(o)->LeftEdge;
    WORD  top   = G(o)->TopEdge;
    WORD  w     = G(o)->Width;
    WORD  dpiH  = (WORD)inst->dpiHeight;
    WORD  btnW, btnH, row1H, gap;
    WORD  row2H = tbl_row2_height(inst->dpiHeight, avSz, inst->style);
    WORD  avGap = tbl_avatar_gap(inst->style);
    WORD  iconSz = (WORD)avSz;
    WORD  y1    = top;
    WORD  y2;
    struct gpLayout childMsg;
    UWORD i;

    (void)cl;

    tbl_button_size(inst->dpiHeight, inst->style, &btnW, &btnH);
    row1H = (btnH > dpiH) ? btnH : dpiH;
    y1    = top + (row1H - btnH) / 2;   /* center button in row 1 if shorter */
    y2    = top + row1H;
    gap   = btnW / 4;   /* space between depth/altpos/iconify */

    if (w < btnW * 4 + gap * 2) w = btnW * 4 + gap * 2;
    if (iconSz < 1)   iconSz = 1;

    /* Row 1 -------------------------------------------------------- */
    /* children[0] = close (far left) */
    if (inst->childCount > 0)
        setBounds(inst->children[0], left, y1, btnW, btnH);

    /* children[3..1] = depth, altpos, iconify — packed at far right,
     * a quarter-button-width gap between each pair. */
    if (inst->childCount > 3)
        setBounds(inst->children[3], left + w - btnW,                     y1, btnW, btnH);
    if (inst->childCount > 2)
        setBounds(inst->children[2], left + w - btnW*2 - gap,             y1, btnW, btnH);
    if (inst->childCount > 1)
        setBounds(inst->children[1], left + w - btnW*3 - gap*2,           y1, btnW, btnH);

    /* Row 2 -------------------------------------------------------- */
    /* User icon: iconSz × iconSz, avatarGap padding on the left and top
     * (see tbl_row2_height for the matching bottom margin). Not a child
     * gadget -- just remember where TitleBarLayout_OnRender should draw
     * it (see the file header comment). */
    inst->avatarX    = left + avGap;
    inst->avatarY    = y2   + avGap;
    inst->avatarSize = iconSz;

    /* settings and account buttons */
    if (inst->childCount > 6)
    {

        struct Gadget *gsettingsbt = (struct Gadget *) inst->children[4];
        struct Gadget *gaccbt = (struct Gadget *) inst->children[5];
        struct Gadget *gnewtbt = (struct Gadget *) inst->children[6];
        struct gpDomain dmsettingsBt,dmAccountBt,dmntbt;

        prepDomain(&dmsettingsBt,msg->gpl_GInfo);
        DoMethodA(inst->children[4], (Msg)&dmsettingsBt);

        prepDomain(&dmAccountBt,msg->gpl_GInfo);
        DoMethodA(inst->children[5], (Msg)&dmAccountBt);

        prepDomain(&dmntbt,msg->gpl_GInfo);
        DoMethodA(inst->children[6], (Msg)&dmntbt);
        // gsettingsbt->Width = dmAccountBt.gpd_Domain.Width;
        // gsettingsbt->Height = dmAccountBt.gpd_Domain.Height;
        // gsettingsbt->TopEdge = G(o)->TopEdge + G(o)->Height - gsettingsbt->Height - inst->style->avatarGap;
        // gsettingsbt->TopEdge =
        //     G(o)->LeftEdge + G(o)->Width - gsettingsbt->Height - inst->style->avatarGap;

        gnewtbt->Width = dmntbt.gpd_Domain.Width;
        gnewtbt->Height = dmntbt.gpd_Domain.Height;
        gnewtbt->TopEdge = G(o)->TopEdge + G(o)->Height - gnewtbt->Height - inst->style->avatarGap;
        gnewtbt->LeftEdge =
            G(o)->LeftEdge + G(o)->Width - gnewtbt->Width - inst->style->avatarGap;

        gaccbt->Width = dmAccountBt.gpd_Domain.Width;
        gaccbt->Height = dmAccountBt.gpd_Domain.Height;
        gaccbt->TopEdge = G(o)->TopEdge + G(o)->Height - gaccbt->Height - inst->style->avatarGap;
        gaccbt->LeftEdge =
            gnewtbt->LeftEdge - gaccbt->Width - inst->style->avatarGap;

        gsettingsbt->Width = dmsettingsBt.gpd_Domain.Width;
        gsettingsbt->Height = dmsettingsBt.gpd_Domain.Height;
        gsettingsbt->TopEdge = G(o)->TopEdge + G(o)->Height - gsettingsbt->Height - inst->style->avatarGap;
        gsettingsbt->LeftEdge =
            gaccbt->LeftEdge - gsettingsbt->Width - inst->style->avatarGap;

    }

    /* Recurse into each child so nested layout.gadgets re-layout too */
    childMsg.MethodID    = GM_LAYOUT;
    childMsg.gpl_GInfo   = msg->gpl_GInfo;
    childMsg.gpl_Initial = msg->gpl_Initial;
    for (i = 0; i < inst->childCount; i++)
        DoMethodA(inst->children[i], (Msg)&childMsg);

    return 0;
}

static ULONG TitleBarLayout_OnHitTest(Class *cl, Object *o, struct gpHitTest *msg)
{
   TitleBarLayoutData *inst = (TitleBarLayoutData *)INST_DATA(cl, o);
    struct GadgetInfo  *gi   = msg->gpht_GInfo;
    struct Window      *win  = gi->gi_Window;
    WORD  mx = win->MouseX;
    WORD  my = win->MouseY;
    UWORD i;
    for (i = 0; i < inst->childCount; i++) {
        struct Gadget *gad = G(inst->children[i]);
        if (mx >= gad->LeftEdge && mx < gad->LeftEdge + (WORD)gad->Width &&
            my >= gad->TopEdge  && my < gad->TopEdge  + (WORD)gad->Height)
        {
            /* Click on a child — let super route it normally */
            return DoSuperMethodA(cl, o, (APTR)msg);
        }
    }
    /* Click on empty drag strip: prime main-loop drag state NOW (still inside
     * WM_HANDLEINPUT), then refuse activation so MoveWindow() is never blocked
     * by an active gadget. GM_HITTEST can run on a different task than the
     * main loop that reads these globals on WMHI_MOUSEMOVE, so
     * windowDragActive is set LAST, after both companion fields, so the
     * main loop never observes Active==TRUE alongside a stale
     * LastScreenX/Y pair (same reasoning as TootTimeline's
     * windowResizeActive -- see fs3etoottimeline_input.c). */
    windowDragLastScreenX = gi->gi_Screen->MouseX;
    windowDragLastScreenY = gi->gi_Screen->MouseY;
    windowDragActive      = TRUE;
    return 0;
}

/* ------------------------------------------------------------------ */
/* GM_RENDER                                                            */
/* We must NOT chain to layout.gadget's own GM_RENDER here: it erases only
 * the gaps its own GM_LAYOUT computed, and we never call that (see the
 * file header) -- our children's bounds come entirely from our own
 * GM_LAYOUT math, which layout.gadget knows nothing about. Calling super
 * would erase the wrong regions (or none at all).
 *
 * So this class owns rendering outright: install GA_BackFill onto the
 * RastPort's Layer if not already done (layout.gadget normally does this
 * itself during its own GM_LAYOUT -- verified fixup against
 * amigapetmate/src/PetsciiCanvas/class_petsciicanvas_render.c, which needs
 * the same workaround for the same reason), erase the whole title bar rect
 * through it (or the OS default colour-0 clear if no hook), then render
 * every child on top at the bounds we already gave it in GM_LAYOUT. Every
 * child fully repaints its own bounds, so this leaves exactly the gaps
 * between/around children showing the backfill result, without having to
 * enumerate each gap rectangle by hand. */
/* ------------------------------------------------------------------ */

/* myTask is declared in friendsh3ep.c */
extern struct Task *myTask;
int refreshTitleBarLayout=0;

static ULONG TitleBarLayout_OnRender(Class *cl, Object *o, struct gpRender *msg)
{
    TitleBarLayoutData *inst = (TitleBarLayoutData *)INST_DATA(cl, o);
    struct RastPort     *rp  = msg->gpr_RPort;
    WORD  left = G(o)->LeftEdge;
    WORD  top  = G(o)->TopEdge;
    WORD  w    = G(o)->Width;
    WORD  h    = G(o)->Height;
    struct gpRender childMsg;
    UWORD i;

    (void)cl;

    // if(inst->allowedProcess != FindTask(NULL))
    // {
    //     /* ask correct draw from correct process */
    //     refreshTitleBarLayout = 1;
    //     if (myTask) Signal(myTask, SIGBREAKF_CTRL_F);
    //     return 1;
    // }

    if (rp && rp->Layer && rp->Layer->BackFill == NULL) {
        struct Hook *backFill = NULL;
        GetAttr(GA_BackFill, o, (ULONG *)&backFill);
        if (backFill)
            InstallLayerHook(rp->Layer, backFill);
    }

    if (rp && w > 0 && h > 0)
        EraseRect(rp, left, top, left + w - 1, top + h - 1);

    /* Row-2 user icon: drawn directly, not a child gadget (see the file
     * header comment) -- same cache, same box-fit-and-centre rule, same
     * RgbImage_DrawScaled() call TootTimeline uses for toot avatars, read
     * fresh every render so there's no stale-image lifecycle to manage. */
    if (rp && inst->accountAcct && inst->avatarSize > 0) {
        struct Screen *screen = msg->gpr_GInfo ? msg->gpr_GInfo->gi_Screen : NULL;
        RgbImage *avImg = inst->avatarImages
                         ? AvatarImages_Get(inst->avatarImages, inst->accountAcct)
                         : NULL;
        WORD ax = inst->avatarX, ay = inst->avatarY, as = inst->avatarSize;

        if (avImg && screen && inst->style) {
            ULONG dw, dh;
            WORD  bx, by;

            if (avImg->width >= avImg->height) {
                dw = as;
                dh = ((ULONG)avImg->height * (ULONG)as) / avImg->width;
            } else {
                dh = as;
                dw = ((ULONG)avImg->width * (ULONG)as) / avImg->height;
            }
            if (dw < 1) dw = 1;
            if (dh < 1) dh = 1;

            bx = (WORD)(ax + (as - (WORD)dw) / 2);
            by = (WORD)(ay + (as - (WORD)dh) / 2);

            RgbImage_DrawScaled(avImg, rp, screen, inst->style->dcNormal,
                                 bx, by, (UWORD)dw, (UWORD)dh);
        } else if (inst->style) {
            /* Not loaded yet -- plain placeholder box. Title bar
             * background is a tiled image (see tbBgHook), not a flat
             * pen, so there's no "bg" colour to draw a contrasting cross
             * against the way TootTimeline's avatar placeholder does. */
            SetAPen(rp, (LONG)FS3E_PEN(inst->style, FS3E_COLOR_ACCENT));
            RectFill(rp, ax, ay, ax + as - 1, ay + as - 1);
        }
    }

    /* Title bar title/logo image (titlebar.title in style.txt), masked on
     * color 0 -- drawn once here, before children, so it never overlaps a
     * child's own render. See FS3EStyle_LoadThemeImages. */
    if (rp && inst->style && BmImage_IsLoaded(&inst->style->titlebarTitle)) {
        BmImage *timg = &inst->style->titlebarTitle;
        WORD     dstX = left + inst->style->titlebarTitleX;
        WORD     dstY = top  + inst->style->titlebarTitleY;
        WORD blitheight = timg->height;
        if((dstY+blitheight)>(top+h)) blitheight =(top+h)-(dstY);

        if (timg->mask) {
            BltMaskBitMapRastPort(timg->bitmap, 0, 0, rp, dstX, dstY,
                                  (LONG)timg->width, (LONG)blitheight,
                                  PATCH9_MASK_MINTERM, timg->mask);
        } else {
            BltBitMapRastPort(timg->bitmap, 0, 0, rp, dstX, dstY,
                              (LONG)timg->width, (LONG)blitheight, 0xC0);
        }
    } else
    {
        if(inst->style && inst->style->dcUsername)
        {
            struct URPTextMetric tm;
            struct URPTextPos tpos;
            const char *ptext = "FriendSh3eep";
            tpos.x = left+50;
            tpos.y = top+2;
            if(inst->style->titlebarTitleX>0)  tpos.x = left + inst->style->titlebarTitleX;
            if(inst->style->titlebarTitleY>0)  tpos.y = top  + inst->style->titlebarTitleY;
    // VOID URPDrawTextUTF8(struct RastPort * rp, struct URPDrawContext * dc, struct URPTextPos * pos, CONST STRPTR utf8, ULONG maxChars);
            URPDC_TextSizeUTF8(inst->style->dcUsername,ptext,-1,&tm);
            tpos.y += tm.baseY;
            URPDrawTextUTF8(rp, inst->style->dcUsername,&tpos,ptext,-1);
        }
    }

    childMsg.MethodID   = GM_RENDER;
    childMsg.gpr_GInfo  = msg->gpr_GInfo;
    childMsg.gpr_RPort  = rp;
    childMsg.gpr_Redraw = msg->gpr_Redraw;
    for (i = 0; i < inst->childCount; i++)
        DoMethodA(inst->children[i], (Msg)&childMsg);

    return 0;
}

/* ------------------------------------------------------------------ */
/* GM_GOACTIVE                                                          */
/* Click on a child button → pass to super.                            */
/* Click on empty space (the drag strip in row 1) → start window move. */
/* ------------------------------------------------------------------ */

// static ULONG TitleBarLayout_OnGoActive(Class *cl, Object *o, struct gpInput *msg)
// {
//     TitleBarLayoutData *inst = (TitleBarLayoutData *)INST_DATA(cl, o);
//     struct GadgetInfo  *gi   = msg->gpi_GInfo;
//     struct Window      *win  = gi->gi_Window;
//     WORD  mx = win->MouseX;
//     WORD  my = win->MouseY;
//     UWORD i;
//  bdbprintf("OGA %d\n", inst->childCount);
//     for (i = 0; i < inst->childCount; i++) {
//         struct Gadget *gad = G(inst->children[i]);
//         if (mx >= gad->LeftEdge && mx < gad->LeftEdge + (WORD)gad->Width &&
//             my >= gad->TopEdge  && my < gad->TopEdge  + (WORD)gad->Height)
//         {
//          bdbprintf("child %d\n",i);
//             /* Click on a child — let super route it normally */
//             return DoSuperMethodA(cl, o, (APTR)msg);
//         }
//     }

//     /* Click on empty drag strip: prime main-loop drag state NOW (still inside
//      * WM_HANDLEINPUT), then refuse activation so MoveWindow() is never blocked
//      * by an active gadget. */
//     windowDragActive      = TRUE;
//     windowDragLastScreenX = gi->gi_Screen->MouseX;
//     windowDragLastScreenY = gi->gi_Screen->MouseY;
//     return GMR_NOREUSE;
// }

/* ------------------------------------------------------------------ */
/* GM_HANDLEINPUT                                                       */
/* Empty-space clicks never activate this gadget (GoActive returns     */
/* GMR_NOREUSE), so this is only reached for child-routed activation.  */
/* ------------------------------------------------------------------ */

// static ULONG TitleBarLayout_OnHandleInput(Class *cl, Object *o, struct gpInput *msg)
// {
//     return DoSuperMethodA(cl, o, (APTR)msg);
// }

/* ------------------------------------------------------------------ */
/* GM_GOINACTIVE                                                        */
/* ------------------------------------------------------------------ */

// static ULONG TitleBarLayout_OnGoInactive(Class *cl, Object *o, struct gpGoInactive *msg)
// {
//     return DoSuperMethodA(cl, o, (APTR)msg);
// }

/* ------------------------------------------------------------------ */
/* Dispatcher                                                           */
/* ------------------------------------------------------------------ */

ULONG ASM SAVEDS TitleBarLayout_Dispatch(
    REG(a0, Class  *cl),
    REG(a2, Object *o),
    REG(a1, Msg     msg))
{
    switch (msg->MethodID) {
        case OM_NEW:
            return TitleBarLayout_OnNew(cl, o, (struct opSet *)msg);
        case OM_SET:
            return TitleBarLayout_OnSet(cl, o, (struct opSet *)msg);
        case OM_DISPOSE:
            return TitleBarLayout_OnDispose(cl, o, msg);
        case GM_DOMAIN:
            return TitleBarLayout_OnDomain(cl, o, (struct gpDomain *)msg);
        case GM_LAYOUT:
            return TitleBarLayout_OnLayout(cl, o, (struct gpLayout *)msg);
        case GM_RENDER:
            return TitleBarLayout_OnRender(cl, o, (struct gpRender *)msg);
        case GM_HITTEST:
            return  TitleBarLayout_OnHitTest(cl, o, (struct gpHitTest *)msg);
            //GMR_GADGETHIT; /* by default layout.gadget doesnt propagate activation in empty space */
        // case GM_GOACTIVE:
        //     return TitleBarLayout_OnGoActive(cl, o, (struct gpInput *)msg);
        // case GM_HANDLEINPUT:
        // //     return TitleBarLayout_OnHandleInput(cl, o, (struct gpInput *)msg);
        // case GM_GOINACTIVE:
        //     return TitleBarLayout_OnGoInactive(cl, o, (struct gpGoInactive *)msg);
        default:
            return DoSuperMethodA(cl, o, (APTR)msg);
    }
}

/* ------------------------------------------------------------------ */
/* Class init / exit                                                    */
/* ------------------------------------------------------------------ */

Class *TitleBarLayoutClass = NULL;

int TitleBarLayout_Init(void)
{
    TitleBarLayoutClass = MakeClass(
        NULL, NULL, LAYOUT_GetClass(),
        sizeof(TitleBarLayoutData), 0);
    if (!TitleBarLayoutClass) return 0;

    TitleBarLayoutClass->cl_Dispatcher.h_Entry    = (HOOKFUNC)TitleBarLayout_Dispatch;
    TitleBarLayoutClass->cl_Dispatcher.h_SubEntry = NULL;
    TitleBarLayoutClass->cl_Dispatcher.h_Data     = NULL;

    return 1;
}

void TitleBarLayout_Exit(void)
{
    if (TitleBarLayoutClass) {
        FreeClass(TitleBarLayoutClass);
        TitleBarLayoutClass = NULL;
    }
}
