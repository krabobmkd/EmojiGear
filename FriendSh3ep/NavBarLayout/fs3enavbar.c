/*
 * NavBarLayout - BOOPSI layout.gadget subclass for the FriendSh3ep
 * navigation bar (Part B).
 *
 * Overrides OM_NEW, GM_DOMAIN, GM_LAYOUT.
 * Does NOT call super in GM_LAYOUT — positions all 8 buttons directly.
 * All other methods chain to layout.gadget via DoSuperMethodA.
 *
 * Row-mode threshold: all 8 children's GM_DOMAIN(MINIMUM) widths are
 * queried; the maximum is generalMinW.  If the available width is at
 * least 8 * generalMinW we use a single row; otherwise two rows of 4.
 * Single-row mode: buttons fill the full height given by the parent.
 * Two-row mode: each row gets half the height.
 */

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>
#include <intuition/gadgetclass.h>
#include <gadgets/layout.h>
#include <proto/layout.h>

#include "../compilers.h"
#include "fs3enavbar.h"

#ifndef G
#define G(o) ((struct Gadget *)(o))
#endif

typedef struct {
    Object *children[NBLAYOUT_NUMBUTTONS];
    UWORD   childCount;
    UWORD   dpiHeight;
    BOOL    twoRowMode;   /* TRUE = 2 rows of 4; updated in GM_LAYOUT */
} NavBarLayoutData;

ULONG ASM SAVEDS NavBarLayout_Dispatch(
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

/*
 * Query all children's GM_DOMAIN(MINIMUM) and return the maximum width
 * and height across all of them.  UniButtonP9 returns pre-computed text
 * metrics here so no RastPort is needed.
 */
static void getChildMinSize(NavBarLayoutData *inst,
                            struct GadgetInfo *gi,
                            WORD *outMinW, WORD *outMinH)
{
    WORD  maxW = 1, maxH = 1;
    UWORD i;

    for (i = 0; i < inst->childCount; i++) {
        struct gpDomain dm;
        dm.MethodID        = GM_DOMAIN;
        dm.gpd_GInfo       = gi;
        dm.gpd_RPort       = NULL;
        dm.gpd_Which       = GDOMAIN_MINIMUM;
        dm.gpd_Domain.Left = 0;
        dm.gpd_Domain.Top  = 0;
        dm.gpd_Domain.Width  = 1;
        dm.gpd_Domain.Height = 1;
        dm.gpd_Attrs       = 0;
        DoMethodA(inst->children[i], (Msg)&dm);
        if (dm.gpd_Domain.Width  > maxW) maxW = dm.gpd_Domain.Width;
        if (dm.gpd_Domain.Height > maxH) maxH = dm.gpd_Domain.Height;
    }

    *outMinW = maxW;
    *outMinH = maxH;
}

/* ------------------------------------------------------------------ */
/* OM_NEW                                                               */
/* ------------------------------------------------------------------ */

static ULONG NavBarLayout_OnNew(Class *cl, Object *o, struct opSet *msg)
{
    NavBarLayoutData *inst;
    Object           *newObj;
    struct TagItem   *state;
    struct TagItem   *tag;

    newObj = (Object *)DoSuperMethodA(cl, o, (APTR)msg);
    if (!newObj) return 0;

    inst             = (NavBarLayoutData *)INST_DATA(cl, newObj);
    inst->childCount = 0;
    inst->dpiHeight  = 14;
    inst->twoRowMode = TRUE;   /* conservative default until first GM_LAYOUT */

    tag = FindTagItem(NBLAYOUT_DpiHeight, msg->ops_AttrList);
    if (tag) inst->dpiHeight = (UWORD)tag->ti_Data;

    state = msg->ops_AttrList;
    while ((tag = NextTagItem(&state)) != NULL) {
        if (tag->ti_Tag == LAYOUT_AddChild &&
            inst->childCount < NBLAYOUT_NUMBUTTONS)
        {
            inst->children[inst->childCount++] = (Object *)tag->ti_Data;
        }
    }

    return (ULONG)newObj;
}

/* ------------------------------------------------------------------ */
/* OM_DISPOSE                                                           */
/* ------------------------------------------------------------------ */

static ULONG NavBarLayout_OnDispose(Class *cl, Object *o, Msg msg)
{
    return DoSuperMethodA(cl, o, (APTR)msg);
}

/* ------------------------------------------------------------------ */
/* GM_DOMAIN                                                            */
/* ------------------------------------------------------------------ */

static ULONG NavBarLayout_OnDomain(Class *cl, Object *o, struct gpDomain *msg)
{
    NavBarLayoutData *inst   = (NavBarLayoutData *)INST_DATA(cl, o);
    struct IBox      *domain = &msg->gpd_Domain;
    WORD              minW, minH;
    WORD              fixedH;

    (void)cl;

    getChildMinSize(inst, msg->gpd_GInfo, &minW, &minH);

    /* Determine row mode from the best width estimate we have:
     *   1. G(o)->Width  — set after the first GM_LAYOUT, most accurate.
     *   2. domain->Width — the width layout.gadget proposes for this query.
     *   3. inst->twoRowMode — cached from the last GM_LAYOUT pass.
     * The height is FIXED (MINIMUM == MAXIMUM) so layout.gadget does not
     * stretch the nav bar vertically. */
    {
        WORD availW = G(o)->Width;
        if (availW <= 0) availW = (WORD)domain->Width;

        if (availW > 0)
            inst->twoRowMode = (availW < (WORD)(NBLAYOUT_NUMBUTTONS * minW));
        /* else keep inst->twoRowMode from last GM_LAYOUT */
    }

    fixedH = inst->twoRowMode ? (WORD)(minH * 2) : minH;

    switch (msg->gpd_Which) {
        case GDOMAIN_MINIMUM:
            domain->Width  = (WORD)(minW * 4);   /* 4 buttons minimum */
            domain->Height = fixedH;
            break;
        case GDOMAIN_MAXIMUM:
            domain->Width  = 32767;
            domain->Height = fixedH;              /* same: height does not stretch */
            break;
        case GDOMAIN_NOMINAL:
        default:
            domain->Width  = (WORD)(NBLAYOUT_NUMBUTTONS * minW);
            domain->Height = fixedH;
            break;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* GM_LAYOUT                                                            */
/* ------------------------------------------------------------------ */

static ULONG NavBarLayout_OnLayout(Class *cl, Object *o, struct gpLayout *msg)
{
    NavBarLayoutData *inst  = (NavBarLayoutData *)INST_DATA(cl, o);
    WORD              left  = G(o)->LeftEdge;
    WORD              top   = G(o)->TopEdge;
    WORD              w     = G(o)->Width;
    WORD              h     = G(o)->Height;
    WORD              minW, minH;
    struct gpLayout   childMsg;
    UWORD i;

    (void)cl;

    if (inst->childCount == 0) return 0;

    getChildMinSize(inst, msg->gpl_GInfo, &minW, &minH);

    /* Update cached row mode so GM_DOMAIN returns consistent heights */
    inst->twoRowMode = (w < (WORD)(NBLAYOUT_NUMBUTTONS * minW));

    childMsg.MethodID    = GM_LAYOUT;
    childMsg.gpl_GInfo   = msg->gpl_GInfo;
    childMsg.gpl_Initial = msg->gpl_Initial;

    if (!inst->twoRowMode) {
        /* Single row: 8 buttons, each taking the full layout height */
        WORD btnW = w / NBLAYOUT_NUMBUTTONS;
        WORD xpos = left;
        for (i = 0; i < inst->childCount; i++) {
            WORD bw = (i == inst->childCount - 1U)
                      ? (left + w - xpos) : btnW;
            setBounds(inst->children[i], xpos, top, bw, h);
            DoMethodA(inst->children[i], (Msg)&childMsg);
            xpos += bw;
        }
    } else {
        /* Two rows of 4 buttons, each row gets half the layout height */
        WORD rowH = h / 2;
        WORD btnW = (w > 0) ? (w / 4) : minW;
        for (i = 0; i < inst->childCount; i++) {
            WORD col  = (WORD)(i % 4);
            WORD row  = (WORD)(i / 4);
            WORD xpos = left + col * btnW;
            WORD ypos = top  + row * rowH;
            WORD bw   = (col == 3) ? (left + w - xpos) : btnW;
            WORD bh   = (row == 1) ? (top + h - ypos)  : rowH;
            setBounds(inst->children[i], xpos, ypos, bw, bh);
            DoMethodA(inst->children[i], (Msg)&childMsg);
        }
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* Dispatcher                                                           */
/* ------------------------------------------------------------------ */

ULONG ASM SAVEDS NavBarLayout_Dispatch(
    REG(a0, Class  *cl),
    REG(a2, Object *o),
    REG(a1, Msg     msg))
{
    switch (msg->MethodID) {
        case OM_NEW:
            return NavBarLayout_OnNew(cl, o, (struct opSet *)msg);
        case OM_DISPOSE:
            return NavBarLayout_OnDispose(cl, o, msg);
        case GM_DOMAIN:
            return NavBarLayout_OnDomain(cl, o, (struct gpDomain *)msg);
        case GM_LAYOUT:
            return NavBarLayout_OnLayout(cl, o, (struct gpLayout *)msg);
        default:
            return DoSuperMethodA(cl, o, (APTR)msg);
    }
}

/* ------------------------------------------------------------------ */
/* Class init / exit                                                    */
/* ------------------------------------------------------------------ */

Class *NavBarLayoutClass = NULL;

int NavBarLayout_Init(void)
{
    NavBarLayoutClass = MakeClass(
        NULL, NULL, LAYOUT_GetClass(),
        sizeof(NavBarLayoutData), 0);
    if (!NavBarLayoutClass) return 0;

    NavBarLayoutClass->cl_Dispatcher.h_Entry    = (HOOKFUNC)NavBarLayout_Dispatch;
    NavBarLayoutClass->cl_Dispatcher.h_SubEntry = NULL;
    NavBarLayoutClass->cl_Dispatcher.h_Data     = NULL;

    return 1;
}

void NavBarLayout_Exit(void)
{
    if (NavBarLayoutClass) {
        FreeClass(NavBarLayoutClass);
        NavBarLayoutClass = NULL;
    }
}
