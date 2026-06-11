/*
 * egtabs_fallbackclass.c – private BOOPSI clicktab replacement for OS3.9.
 *
 * Extends layout.gadget (horizontal).  Shows up to FAKETAB_VIS_MAX (4) tab
 * buttons at a time.  When tabTotal > 4 a "<" and ">" button are added at
 * each end so the user can scroll the visible window.
 *
 * Button GA_IDs:
 *   tab  i  → GID_TABBAR_SAFE_0 + i   (encodes absolute tab index directly)
 *   "<"     → GID_TABBAR_SAFE_PREV
 *   ">"     → GID_TABBAR_SAFE_NEXT
 *
 * Attributes handled:
 *   CLICKTAB_Current  (OM_GET / OM_SET) – active tab index
 *   CLICKTAB_Labels   (OM_SET)          – full label list → rebuild buttons
 *   FAKETAB_ScrollBy  (OM_SET)          – LONG delta, scrolls visible window
 *
 * GM_LAYOUT and GM_RENDER are intentionally not overridden (fall to super).
 * GM_DOMAIN is overridden only to enforce a minimum height when empty.
 *
 * Note: LAYOUT_RemoveChild both removes AND disposes the child gadget, so
 * DisposeObject must NOT be called afterwards.
 */

#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <proto/alib.h>
#include <proto/layout.h>
#include <proto/button.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <gadgets/clicktab.h>
#include <intuition/classusr.h>
#include <intuition/classes.h>
#include <intuition/gadgetclass.h>
#include <intuition/cghooks.h>   /* struct gpDomain */

#include "compilers.h"
#include "egtabs_fallbackclass.h"
#include "emojigear.h"
#include "gadgetid.h"

extern struct Library *LayoutBase;
extern struct Library *ButtonBase;

#ifndef GA_Text
#define GA_Text (GA_Dummy + 0x0039)
#endif

/* -------------------------------------------------------------------------
 * Instance data
 * -------------------------------------------------------------------------*/
#define FAKETAB_VIS_MAX  4

struct FakeTabData {
    ULONG         current;                   /* active tab index (global) */
    int           scrollOff;                 /* index of first visible tab */
    int           tabTotal;                  /* total number of tabs */
    struct List  *labelList;                 /* borrowed ptr to current label list */
    Object       *tabBtns[FAKETAB_VIS_MAX];  /* visible tab button objects */
    int           visibleCount;              /* how many tab buttons are shown */
    Object       *btnPrev;                   /* "<" button or NULL */
    Object       *btnNext;                   /* ">" button or NULL */
};

#define FAKETAB_DATA(cl, o) ((struct FakeTabData *)INST_DATA(cl, o))
#define FAKETAB_MIN_HEIGHT  16

/* -------------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------------*/

/* Remove one child via super OM_SET LAYOUT_RemoveChild.
 * LAYOUT_RemoveChild also disposes the object — do NOT DisposeObject after. */
static void faketab_remove_one(Class *cl, Object *obj, Object *child)
{
    struct TagItem rmtag[2];
    struct opSet   rmops;
    rmtag[0].ti_Tag  = LAYOUT_RemoveChild;
    rmtag[0].ti_Data = (ULONG)child;
    rmtag[1].ti_Tag  = TAG_END;
    rmtag[1].ti_Data = 0;
    rmops.MethodID     = OM_SET;
    rmops.ops_AttrList = rmtag;
    rmops.ops_GInfo    = NULL;
    DoSuperMethodA(cl, obj, (Msg)&rmops);
}

/* Remove and forget all currently tracked buttons. */
static void faketab_remove_all(Class *cl, Object *obj, struct FakeTabData *inst)
{
    int i;
    if (inst->btnPrev) { faketab_remove_one(cl, obj, inst->btnPrev); inst->btnPrev = NULL; }
    for (i = 0; i < inst->visibleCount; i++) {
        if (inst->tabBtns[i]) { faketab_remove_one(cl, obj, inst->tabBtns[i]); inst->tabBtns[i] = NULL; }
    }
    inst->visibleCount = 0;
    if (inst->btnNext) { faketab_remove_one(cl, obj, inst->btnNext); inst->btnNext = NULL; }
}

/* Create one button and add it to the layout via super OM_SET LAYOUT_AddChild.
 * minWidth=TRUE → CHILD_WeightedWidth 0 (minimum / fixed width). */
static Object *faketab_add_btn(Class *cl, Object *obj,
                               const char *label, ULONG gadId, BOOL minWidth)
{
    Object *btn = NewObject(BUTTON_GetClass(), NULL,
        GA_Text,      (ULONG)(label ? label : ""),
        GA_ID,        gadId,
        GA_RelVerify, TRUE,
        TAG_END);
    if (btn)
    {
        struct TagItem addtag[3];
        struct opSet   addops;
        addtag[0].ti_Tag  = LAYOUT_AddChild;
        addtag[0].ti_Data = (ULONG)btn;
        if (minWidth) {
            addtag[1].ti_Tag  = CHILD_WeightedWidth;
            addtag[1].ti_Data = 0;
            addtag[2].ti_Tag  = TAG_END;
            addtag[2].ti_Data = 0;
        } else {
            addtag[1].ti_Tag  = TAG_END;
            addtag[1].ti_Data = 0;
        }
        addops.MethodID     = OM_SET;
        addops.ops_AttrList = addtag;
        addops.ops_GInfo    = NULL;
        DoSuperMethodA(cl, obj, (Msg)&addops);
    }
    return btn;
}

/* Clamp scrollOff so the active tab is visible and range is valid. */
static void faketab_clamp_scroll(struct FakeTabData *inst)
{
    int maxScroll = (inst->tabTotal > FAKETAB_VIS_MAX)
                    ? (inst->tabTotal - FAKETAB_VIS_MAX) : 0;
    int cur = (int)inst->current;

    if (cur < inst->scrollOff)
        inst->scrollOff = cur;
    if (cur >= inst->scrollOff + FAKETAB_VIS_MAX)
        inst->scrollOff = cur - FAKETAB_VIS_MAX + 1;

    if (inst->scrollOff < 0)         inst->scrollOff = 0;
    if (inst->scrollOff > maxScroll) inst->scrollOff = maxScroll;
}

/* Rebuild the visible button set from the stored labelList and scrollOff.
 * Only does a plain range-clamp — does NOT force scrollOff to track current.
 * faketab_clamp_scroll() must be called by the caller when that is wanted. */
static void faketab_build_visible(Class *cl, Object *obj, struct FakeTabData *inst)
{
    struct Node *node;
    int i, vis;
    int maxScroll = (inst->tabTotal > FAKETAB_VIS_MAX)
                    ? (inst->tabTotal - FAKETAB_VIS_MAX) : 0;

    /* Plain range clamp only — explicit scroll from < / > must not be overridden */
    if (inst->scrollOff < 0)         inst->scrollOff = 0;
    if (inst->scrollOff > maxScroll) inst->scrollOff = maxScroll;

    /* "<" navigation button */
    if (inst->tabTotal > FAKETAB_VIS_MAX)
        inst->btnPrev = faketab_add_btn(cl, obj, "<", GID_TABBAR_SAFE_PREV, TRUE);

    /* Advance list pointer to scrollOff */
    node = (inst->labelList) ? inst->labelList->lh_Head : NULL;
    for (i = 0; i < inst->scrollOff && node && node->ln_Succ; i++)
        node = node->ln_Succ;

    /* Tab buttons for the visible window */
    vis = 0;
    while (vis < FAKETAB_VIS_MAX && node && node->ln_Succ)
    {
        ULONG gadId = (ULONG)(GID_TABBAR_SAFE_0 + inst->scrollOff + vis);
        inst->tabBtns[vis] = faketab_add_btn(cl, obj, node->ln_Name, gadId, FALSE);
        node = node->ln_Succ;
        vis++;
    }
    inst->visibleCount = vis;

    /* ">" navigation button */
    if (inst->tabTotal > FAKETAB_VIS_MAX)
        inst->btnNext = faketab_add_btn(cl, obj, ">", GID_TABBAR_SAFE_NEXT, TRUE);
}

/* Store a new label list, recount, adjust scroll, rebuild. */
static void faketab_set_labels(Class *cl, Object *obj, struct FakeTabData *inst,
                               struct List *list)
{
    struct Node *n;
    int count = 0;

    if (list)
        for (n = list->lh_Head; n->ln_Succ; n = n->ln_Succ)
            count++;

    inst->labelList = list;
    inst->tabTotal  = count;

    /* Auto-scroll so the active tab stays visible when the label set changes */
    faketab_clamp_scroll(inst);

    faketab_remove_all(cl, obj, inst);
    faketab_build_visible(cl, obj, inst);
}

/* -------------------------------------------------------------------------
 * Class dispatcher
 * -------------------------------------------------------------------------*/
static ULONG ASM SAVEDS FakeTabDispatch(
    REG(a0, Class  *cl),
    REG(a2, Object *o),
    REG(a1, Msg     msg))
{
    switch (msg->MethodID)
    {
        case OM_NEW:
        {
            struct opSet      *ops = (struct opSet *)msg;
            struct FakeTabData *inst;
            Object            *obj;
            struct TagItem     init[9];
            struct opSet       new_ops;

            init[0].ti_Tag  = LAYOUT_Orientation;  init[0].ti_Data = LAYOUT_ORIENT_HORIZ;
            init[1].ti_Tag  = LAYOUT_DeferLayout;   init[1].ti_Data = TRUE;
            init[2].ti_Tag  = LAYOUT_SpaceOuter;    init[2].ti_Data = FALSE;
            init[3].ti_Tag  = LAYOUT_InnerSpacing;  init[3].ti_Data = 0;
            init[4].ti_Tag  = LAYOUT_TopSpacing;    init[4].ti_Data = 0;
            init[5].ti_Tag  = LAYOUT_BottomSpacing; init[5].ti_Data = 0;
            init[6].ti_Tag  = LAYOUT_LeftSpacing;   init[6].ti_Data = 0;
            init[7].ti_Tag  = TAG_MORE;             init[7].ti_Data = (ULONG)ops->ops_AttrList;
            init[8].ti_Tag  = TAG_END;              init[8].ti_Data = 0;
            new_ops.MethodID     = OM_NEW;
            new_ops.ops_AttrList = init;
            new_ops.ops_GInfo    = ops->ops_GInfo;

            obj = (Object *)DoSuperMethodA(cl, o, (Msg)&new_ops);
            if (!obj) return 0;

            inst = FAKETAB_DATA(cl, obj);
            inst->current      = 0;
            inst->scrollOff    = 0;
            inst->tabTotal     = 0;
            inst->labelList    = NULL;
            inst->visibleCount = 0;
            inst->btnPrev      = NULL;
            inst->btnNext      = NULL;
            {
                int i;
                for (i = 0; i < FAKETAB_VIS_MAX; i++) inst->tabBtns[i] = NULL;
            }
            {
                struct TagItem *ti = FindTagItem(CLICKTAB_Current, ops->ops_AttrList);
                if (ti) inst->current = (ULONG)ti->ti_Data;
            }
            {
                struct TagItem *ti = FindTagItem(CLICKTAB_Labels, ops->ops_AttrList);
                if (ti && ti->ti_Data)
                    faketab_set_labels(cl, obj, inst, (struct List *)ti->ti_Data);
            }
            return (ULONG)obj;
        }

        case OM_DISPOSE:
            /* Super disposes layout and all children (via LAYOUT_RemoveChild chain) */
            return DoSuperMethodA(cl, o, msg);

        case OM_SET:
        case OM_UPDATE:
        {
            struct opSet       *ops  = (struct opSet *)msg;
            struct FakeTabData *inst = FAKETAB_DATA(cl, o);
            ULONG               result;
            struct TagItem     *ti;

            result = DoSuperMethodA(cl, o, msg);

            ti = FindTagItem(CLICKTAB_Current, ops->ops_AttrList);
            if (ti)
                inst->current = (ULONG)ti->ti_Data;

            ti = FindTagItem(CLICKTAB_Labels, ops->ops_AttrList);
            if (ti && ti->ti_Data)
                faketab_set_labels(cl, o, inst, (struct List *)ti->ti_Data);

            ti = FindTagItem(FAKETAB_ScrollBy, ops->ops_AttrList);
            if (ti)
            {
                LONG delta = (LONG)ti->ti_Data;
                int maxScroll = (inst->tabTotal > FAKETAB_VIS_MAX)
                                ? (inst->tabTotal - FAKETAB_VIS_MAX) : 0;
                int newOff = inst->scrollOff + (int)delta;
                if (newOff < 0)         newOff = 0;
                if (newOff > maxScroll) newOff = maxScroll;
                if (newOff != inst->scrollOff) {
                    inst->scrollOff = newOff;
                    faketab_remove_all(cl, o, inst);
                    faketab_build_visible(cl, o, inst);
                }
            }

            return result;
        }

        case OM_GET:
        {
            struct opGet *opg = (struct opGet *)msg;
            if (opg->opg_AttrID == CLICKTAB_Current)
            {
                *opg->opg_Storage = FAKETAB_DATA(cl, o)->current;
                return TRUE;
            }
            return DoSuperMethodA(cl, o, msg);
        }

        case GM_DOMAIN:
        {
            struct gpDomain *gpd = (struct gpDomain *)msg;
            ULONG result = DoSuperMethodA(cl, o, msg);
            if (gpd->gpd_Domain.Height < FAKETAB_MIN_HEIGHT)
                gpd->gpd_Domain.Height = FAKETAB_MIN_HEIGHT;
            return result;
        }

        default:
            /* GM_LAYOUT, GM_RENDER: super-called here, not extended */
            return DoSuperMethodA(cl, o, msg);
    }
}

/* -------------------------------------------------------------------------
 * Class lifecycle
 * -------------------------------------------------------------------------*/
static Class *s_fakeTabClass = NULL;

void EgFakeTab_Init(void)
{
    s_fakeTabClass = MakeClass(NULL, NULL, LAYOUT_GetClass(),
                               sizeof(struct FakeTabData), 0);
    if (s_fakeTabClass)
    {
        s_fakeTabClass->cl_Dispatcher.h_Entry    = (HOOKFUNC)FakeTabDispatch;
        s_fakeTabClass->cl_Dispatcher.h_SubEntry = NULL;
        s_fakeTabClass->cl_Dispatcher.h_Data     = NULL;
    }
}

void EgFakeTab_Free(void)
{
    if (s_fakeTabClass) { FreeClass(s_fakeTabClass); s_fakeTabClass = NULL; }
}

Class *EgFakeTab_GetClass(void)
{
    return s_fakeTabClass;
}
