/*
 * TitleBarLayout - BOOPSI layout.gadget subclass for the FriendSh3ep
 * title bar (Part A).
 *
 * Overrides OM_NEW, GM_DOMAIN, GM_LAYOUT.
 * Does NOT call super in GM_LAYOUT — positions every child directly so
 * the two-row arrangement is fully under our control.
 * All other methods chain to layout.gadget via DoSuperMethodA.
 *
 * Row 1 (height = dpiH):
 *   close at far-left (square dpiH×dpiH).
 *   iconify / altpos / depth packed at far-right (each dpiH wide).
 *   Empty space between them acts as the drag area.
 *
 * Row 2 (height = max(dpiH, TBLAYOUT_ICON_SIZE_MIN + 2×TBLAYOUT_ICON_MARGIN)):
 *   user-icon at far-left, sized (iconSz × iconSz) with TBLAYOUT_ICON_MARGIN
 *   padding on all sides.  Will become a real 32 px bitmap; "[U]" for now.
 *   Remaining width split evenly between the two count labels.
 *
 * Total minimum/maximum height = dpiH + row2Height (fixed — no vertical stretch).
 */

/* Minimum size (pixels) for the user-icon image in row 2 */
#define TBLAYOUT_ICON_SIZE_MIN  32
/* Padding around the icon within row 2 (top, bottom, left) */
#define TBLAYOUT_ICON_MARGIN     2

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>
#include <intuition/gadgetclass.h>
#include <gadgets/layout.h>
#include <proto/layout.h>

#include "../compilers.h"
#include "fs3etitlebar.h"

#include "../bdbprintf.h"

/* Drag state owned by the main loop — set here during GM_GOACTIVE so that
 * WMHI_MOUSEMOVE events in the same WM_HANDLEINPUT drain already see the flag. */
extern BOOL windowDragActive;
extern WORD windowDragLastScreenX;
extern WORD windowDragLastScreenY;

#ifndef G
#define G(o) ((struct Gadget *)(o))
#endif

/* Height of row 2: avatarSize + two margins, never less than dpiH */
static WORD tbl_row2_height(UWORD dpiH, UWORD avatarSize)
{
    WORD iconRow = (WORD)(avatarSize + 2 * TBLAYOUT_ICON_MARGIN);
    return (iconRow > (WORD)dpiH) ? iconRow : (WORD)dpiH;
}

/* Total fixed height of the title bar */
static WORD tbl_total_height(UWORD dpiH, UWORD avatarSize)
{
    return (WORD)dpiH + tbl_row2_height(dpiH, avatarSize);
}

typedef struct {
    Object    *children[TBLAYOUT_NUMCHILDREN];
    UWORD      childCount;
    UWORD      dpiHeight;
    FS3EStyle *style;
} TitleBarLayoutData;

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

    inst             = (TitleBarLayoutData *)INST_DATA(cl, newObj);
    inst->childCount = 0;
    inst->dpiHeight  = 14;
    inst->style      = NULL;

    tag = FindTagItem(TBLAYOUT_DpiHeight, msg->ops_AttrList);
    if (tag) inst->dpiHeight = (UWORD)tag->ti_Data;

    tag = FindTagItem(TBLAYOUT_Style, msg->ops_AttrList);
    if (tag) inst->style = (FS3EStyle *)tag->ti_Data;

    state = msg->ops_AttrList;
    while ((tag = NextTagItem(&state)) != NULL) {
        if (tag->ti_Tag == LAYOUT_AddChild &&
            inst->childCount < TBLAYOUT_NUMCHILDREN)
        {
            inst->children[inst->childCount++] = (Object *)tag->ti_Data;
        }
    }

    return (ULONG)newObj;
}

/* ------------------------------------------------------------------ */
/* OM_DISPOSE                                                           */
/* ------------------------------------------------------------------ */

static ULONG TitleBarLayout_OnDispose(Class *cl, Object *o, Msg msg)
{
    return DoSuperMethodA(cl, o, (APTR)msg);
}

/* ------------------------------------------------------------------ */
/* GM_DOMAIN                                                            */
/* ------------------------------------------------------------------ */

static ULONG TitleBarLayout_OnDomain(Class *cl, Object *o, struct gpDomain *msg)
{
    TitleBarLayoutData *inst     = (TitleBarLayoutData *)INST_DATA(cl, o);
    struct IBox        *domain   = &msg->gpd_Domain;
    UWORD               avatarSz = inst->style ? (UWORD)inst->style->avatarSize : TBLAYOUT_ICON_SIZE_MIN;
    WORD                fixedH   = tbl_total_height(inst->dpiHeight, avatarSz);

    switch (msg->gpd_Which) {
        case GDOMAIN_MINIMUM:
            domain->Width  = (WORD)(inst->dpiHeight * 4);
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
    WORD  row2H = tbl_row2_height(inst->dpiHeight, avSz);
    WORD  iconSz = (WORD)avSz;
    WORD  y1    = top;
    WORD  y2    = top + dpiH;
    struct gpLayout childMsg;
    UWORD i;

    (void)cl;

    if (w < dpiH * 4) w = dpiH * 4;
    if (iconSz < 1)   iconSz = 1;

    /* Row 1 -------------------------------------------------------- */
    /* children[0] = close (far left, square dpiH×dpiH) */
    if (inst->childCount > 0)
        setBounds(inst->children[0], left, y1, dpiH, dpiH);

    /* children[3..1] = depth, altpos, iconify — packed at far right */
    if (inst->childCount > 3)
        setBounds(inst->children[3], left + w - dpiH,   y1, dpiH, dpiH);
    if (inst->childCount > 2)
        setBounds(inst->children[2], left + w - dpiH*2, y1, dpiH, dpiH);
    if (inst->childCount > 1)
        setBounds(inst->children[1], left + w - dpiH*3, y1, dpiH, dpiH);

    /* Row 2 -------------------------------------------------------- */
    /* children[4] = user icon: iconSz × iconSz with ICON_MARGIN padding */
    if (inst->childCount > 4)
        setBounds(inst->children[4],
                  left + TBLAYOUT_ICON_MARGIN,
                  y2   + TBLAYOUT_ICON_MARGIN,
                  iconSz, iconSz);

    /* children[5] and [6]: postsLabel / newPostsLabel — disabled */
    /*
    {
        WORD labLeft  = left + iconSz + 2 * TBLAYOUT_ICON_MARGIN + 2;
        WORD labW     = w - (labLeft - left);
        WORD halfLabW = labW / 2;

        if (labW < 1) labW = 1;
        if (halfLabW < 1) halfLabW = 1;

        if (inst->childCount > 5)
            setBounds(inst->children[5], labLeft, y2, halfLabW, row2H);
        if (inst->childCount > 6)
            setBounds(inst->children[6], labLeft + halfLabW, y2,
                      labW - halfLabW, row2H);
    }
    */

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
     * by an active gadget. */
    windowDragActive      = TRUE;
    windowDragLastScreenX = gi->gi_Screen->MouseX;
    windowDragLastScreenY = gi->gi_Screen->MouseY;
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
        case OM_DISPOSE:
            return TitleBarLayout_OnDispose(cl, o, msg);
        case GM_DOMAIN:
            return TitleBarLayout_OnDomain(cl, o, (struct gpDomain *)msg);
        case GM_LAYOUT:
            return TitleBarLayout_OnLayout(cl, o, (struct gpLayout *)msg);
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
