/* UniButtonP9 – attribute handlers: OM_NEW, OM_DISPOSE, OM_SET, OM_GET. */
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/alib.h>
#include <proto/bevel.h>
#include <images/bevel.h>

#include "unibuttonp9_private.h"
#include "../bdbprintf.h"

#ifndef GA_Text
#define GA_Text (GA_Dummy + 0x0039)
#endif

/* =========================================================================
 * Internal helpers
 * =========================================================================
 */

void ubtp9_free_cache(UniButtonP9Data *inst)
{
    int i;
    for (i = 0; i < UBTP9_NUM_STATES; i++)
        OffscreenBitMap_Close(&inst->cacheBm[i]);
    inst->cacheValid = FALSE;
}

void ubtp9_blit_state(UniButtonP9Data *inst, struct Gadget *g,
                      struct RastPort *rp, int state)
{
    WORD textX, textY;
    OffscreenBitMap *obm = &inst->cacheBm[state];

    if (!inst->cacheValid) return;
    if (!obm->_bm) return;

    textX = (g->Width  - obm->_w) / 2;
    textY = (g->Height - obm->_h) / 2;

    BltBitMapRastPort(obm->_bm, 0, 0,
                      rp,
                      (LONG)g->LeftEdge + textX, (LONG)g->TopEdge + textY,
                      (LONG)obm->_w, (LONG)obm->_h,
                      0xC0);

    SetAPen(rp, (LONG)obm->_bgpen);
    SetDrMd(rp, JAM1);
    if (textY > 0)
        RectFill(rp, (LONG)g->LeftEdge, (LONG)g->TopEdge,
                     (LONG)(g->LeftEdge + g->Width - 1),
                     (LONG)(g->TopEdge  + textY - 1));
    if ((textY + obm->_h) < g->Height)
        RectFill(rp, (LONG)g->LeftEdge, (LONG)(g->TopEdge + textY + obm->_h),
                     (LONG)(g->LeftEdge + g->Width - 1),
                     (LONG)(g->TopEdge  + g->Height - 1));
    if (textX > 0)
        RectFill(rp, (LONG)g->LeftEdge, (LONG)(g->TopEdge + textY),
                     (LONG)(g->LeftEdge + textX - 1),
                     (LONG)(g->TopEdge  + textY + obm->_h - 1));
    if ((textX + obm->_w) < g->Width)
        RectFill(rp, (LONG)(g->LeftEdge + textX + obm->_w), (LONG)(g->TopEdge + textY),
                     (LONG)(g->LeftEdge + g->Width - 1),
                     (LONG)(g->TopEdge  + textY + obm->_h - 1));
}

void ubtp9_render_self(Class *cl, Object *o, struct GadgetInfo *gi)
{
    struct RastPort *rp;
    if (!gi) return;
    rp = ObtainGIRPort(gi);
    if (rp) {
        DoMethod(o, GM_RENDER, gi, rp, GREDRAW_UPDATE);
        ReleaseGIRPort(rp);
    }
}

void ubtp9_notify_pressed(Class *cl, Object *o, struct GadgetInfo *gi)
{
    UniButtonP9Data *inst = UBTP9_DATA(cl, o);
    struct opUpdate  nmsg;
    ULONG tags[5];

    if (!inst->target || !inst->ga_id) return;

    tags[0] = GA_ID;
    tags[1] = inst->ga_id;
    tags[2] = GA_Selected;
    tags[3] = TRUE;
    tags[4] = TAG_DONE;

    /* Direct DoMethodA to target: avoids the OS3.9 DoSuperMethodA/OM_NOTIFY bug */
    nmsg.MethodID     = OM_UPDATE;
    nmsg.opu_AttrList = (struct TagItem *)tags;
    nmsg.opu_GInfo    = gi;
    nmsg.opu_Flags    = 0;
    DoMethodA(inst->target, (Msg)&nmsg);
}

/* =========================================================================
 * OM_NEW
 * =========================================================================
 */
ULONG UniButtonP9_OnNew(Class *cl, Object *o, struct opSet *msg)
{
    UniButtonP9Data *inst;
    Object          *newObj;
    ULONG            mDispose;
    struct TagItem  *ptag;
    ULONG            urpflags;
    ULONG            bevelStyleVal;

    newObj = (Object *)DoSuperMethodA(cl, o, (APTR)msg);
    if (!newObj) return 0;

    inst = UBTP9_DATA(cl, newObj);
    memset(inst, 0, sizeof(UniButtonP9Data));

    inst->pointSize    = 14;
    inst->fontFlags    = 0;
    inst->txtPen       = 1;
    inst->bgPen        = 0;
    inst->selBgPen     = 3;  /* FILLPEN */
    inst->transparent  = FALSE;
    inst->readOnly     = FALSE;
    inst->pushButton   = FALSE;
    inst->leftMargin   = 4;
    inst->rightMargin  = 4;
    inst->topMargin    = 2;
    inst->bottomMargin = 2;

    /* URPDrawContext: shared or private */
    {
        struct URPDrawContext *externalDc = NULL;
        ptag = FindTagItem(UBTP9_URPDrawContext, msg->ops_AttrList);
        if (ptag) externalDc = (struct URPDrawContext *)ptag->ti_Data;

        if (externalDc) {
            URPDC_Retain(externalDc);
            inst->dc = externalDc;
        } else {
            inst->dc = URPDC_Create(NULL);
            urpflags = URP_PREF_ANTIALIAS |
                       URP_PREF_CLUTMODE_NOMASK |
                       URP_PREF_HIGHFILTERING;
            ptag = FindTagItem(UBTP9_URPPrefs, msg->ops_AttrList);
            if (ptag) urpflags = ptag->ti_Data;
            if (inst->dc)
                URPDC_SetPreferenceFlags(inst->dc, urpflags);
        }
    }
    if (!inst->dc) goto fail;

    /* Bevel */
    bevelStyleVal = BVS_BUTTON;
    ptag = FindTagItem(UBTP9_BevelStyle, msg->ops_AttrList);
    if (ptag) bevelStyleVal = ptag->ti_Data;
    inst->bevelStyle = bevelStyleVal;

    if (bevelStyleVal != BVS_NONE && BevelBase) {
        inst->bevel = NewObject(BEVEL_GetClass(), NULL,
            BEVEL_Style,       bevelStyleVal,
            BEVEL_Transparent, TRUE,
            TAG_END);
        if (inst->bevel) {
            ULONG v = 0;
            GetAttr(BEVEL_VertSize,  inst->bevel, &v); inst->bevelV = (WORD)v;
            v = 0;
            GetAttr(BEVEL_HorizSize, inst->bevel, &v); inst->bevelH = (WORD)v;
            inst->leftMargin   += inst->bevelV;
            inst->rightMargin  += inst->bevelV;
            inst->topMargin    += inst->bevelH;
            inst->bottomMargin += inst->bevelH;
        }
    }

    /* Apply remaining tags */
    UniButtonP9_OnSet(cl, newObj, msg);

    /* Enable WMHI_GADGETUP via GACT_RELVERIFY */
    {
        ULONG setatr[] = { GA_RelVerify, TRUE, TAG_END };
        struct opSet msgset;
        msgset.MethodID     = OM_SET;
        msgset.ops_GInfo    = msg->ops_GInfo;
        msgset.ops_AttrList = &setatr[0];
        DoSuperMethod(cl, newObj, (APTR)&msgset);
    }
    G(newObj)->Activation |= GACT_RELVERIFY;

    return (ULONG)newObj;

fail:
    mDispose = OM_DISPOSE;
    DoSuperMethodA(cl, newObj, (APTR)&mDispose);
    return 0;
}

/* =========================================================================
 * OM_DISPOSE
 * =========================================================================
 */
ULONG UniButtonP9_OnDispose(Class *cl, Object *o, Msg msg)
{
    UniButtonP9Data *inst = UBTP9_DATA(cl, o);
    bdbprintf("UniButtonP9_OnDispose\n");
    ubtp9_free_cache(inst);
    if (inst->text)  { FreeVec(inst->text);        inst->text  = NULL; }
    if (inst->bevel) { DisposeObject(inst->bevel);  inst->bevel = NULL; }
    if (inst->dc)    { URPDC_Release(inst->dc);     inst->dc    = NULL; }
    return DoSuperMethodA(cl, o, (APTR)msg);
}

/* =========================================================================
 * OM_SET / OM_UPDATE
 * =========================================================================
 */
ULONG UniButtonP9_OnSet(Class *cl, Object *o, struct opSet *msg)
{
    UniButtonP9Data *inst     = UBTP9_DATA(cl, o);
    struct TagItem  *state    = msg->ops_AttrList;
    struct TagItem  *tag;
    ULONG            result   = 0;
    BOOL             redraw   = FALSE;
    BOOL             justBlit = FALSE;

    while ((tag = NextTagItem(&state)) != NULL) {
        switch (tag->ti_Tag)
        {
        case UBTP9_URPDrawContext:
        {
            struct URPDrawContext *newDc = (struct URPDrawContext *)tag->ti_Data;
            if (newDc != inst->dc) {
                if (inst->dc) URPDC_Release(inst->dc);
                if (newDc)    URPDC_Retain(newDc);
                inst->dc = newDc;
                ubtp9_free_cache(inst);
                redraw = TRUE;
            }
            result = 1;
            break;
        }

        case UBTP9_PointSize:
            if (inst->pointSize != (ULONG)tag->ti_Data) {
                inst->pointSize = (ULONG)tag->ti_Data;
                if (inst->dc)
                    URPDC_ChangeFontsSize(inst->dc, inst->pointSize, ~0UL);
                ubtp9_free_cache(inst);
                redraw = TRUE;
            }
            result = 1;
            break;

        case UBTP9_FontFlags:
            inst->fontFlags = (ULONG)tag->ti_Data;
            result = 1;
            break;

        case UBTP9_AddFont:
        {
            const char *p = (const char *)tag->ti_Data;
            if (p && p[0] && inst->dc) {
                URPDC_AddFont(inst->dc, p, (int)inst->pointSize, inst->fontFlags);
                ubtp9_free_cache(inst);
                redraw = TRUE;
            }
            result = 1;
            break;
        }

        case UBTP9_FlushFonts:
            if (inst->dc) {
                URPDC_FlushFonts(inst->dc);
                ubtp9_free_cache(inst);
                redraw = TRUE;
            }
            result = 1;
            break;

        case UBTP9_URPPrefs:
            if (inst->dc) {
                ULONG cur = URPDC_GetPreferenceFlags(inst->dc);
                if (cur != (ULONG)tag->ti_Data) {
                    URPDC_SetPreferenceFlags(inst->dc, tag->ti_Data);
                    ubtp9_free_cache(inst);
                    redraw = TRUE;
                }
            }
            result = 1;
            break;

        case UBTP9_TextPen:
            if (inst->txtPen != (ULONG)tag->ti_Data) {
                inst->txtPen = (ULONG)tag->ti_Data;
                ubtp9_free_cache(inst);
                redraw = TRUE;
            }
            result = 1;
            break;

        case UBTP9_BgPen:
            if (inst->bgPen != (ULONG)tag->ti_Data) {
                inst->bgPen = (ULONG)tag->ti_Data;
                ubtp9_free_cache(inst);
                redraw = TRUE;
            }
            result = 1;
            break;

        case UBTP9_SelBgPen:
            if (inst->selBgPen != (ULONG)tag->ti_Data) {
                inst->selBgPen = (ULONG)tag->ti_Data;
                ubtp9_free_cache(inst);
                redraw = TRUE;
            }
            result = 1;
            break;

        case UBTP9_Transparent:
            inst->transparent = tag->ti_Data ? TRUE : FALSE;
            ubtp9_free_cache(inst);
            redraw = TRUE;
            result = 1;
            break;

        case UBTP9_PushButton:
            inst->pushButton = tag->ti_Data ? TRUE : FALSE;
            result = 1;
            break;

        case GA_ReadOnly:
            inst->readOnly = tag->ti_Data ? TRUE : FALSE;
            result = 1;
            break;

        case GA_Text:
        {
            const char *newText = (const char *)tag->ti_Data;
            if (inst->text) { FreeVec(inst->text); inst->text = NULL; }
            if (newText) {
                ULONG len = (ULONG)strlen(newText);
                inst->text = (char *)AllocVec(len + 1, MEMF_ANY);
                if (inst->text) CopyMem((APTR)newText, inst->text, len + 1);
            }
            ubtp9_free_cache(inst);
            redraw = TRUE;
            result = 1;
            break;
        }

        case UBTP9_LeftMargin:
        {
            WORD v = (WORD)(ULONG)tag->ti_Data;
            if (v < 0) v = 0;
            v += inst->bevelV;
            if (v != inst->leftMargin) {
                inst->leftMargin = v;
                ubtp9_free_cache(inst);
                redraw = TRUE;
            }
            result = 1;
            break;
        }

        case UBTP9_RightMargin:
        {
            WORD v = (WORD)(ULONG)tag->ti_Data;
            if (v < 0) v = 0;
            v += inst->bevelV;
            if (v != inst->rightMargin) {
                inst->rightMargin = v;
                ubtp9_free_cache(inst);
                redraw = TRUE;
            }
            result = 1;
            break;
        }

        case UBTP9_TopMargin:
        {
            WORD v = (WORD)(ULONG)tag->ti_Data;
            if (v < 0) v = 0;
            v += inst->bevelH;
            if (v != inst->topMargin) {
                inst->topMargin = v;
                ubtp9_free_cache(inst);
                redraw = TRUE;
            }
            result = 1;
            break;
        }

        case UBTP9_BottomMargin:
        {
            WORD v = (WORD)(ULONG)tag->ti_Data;
            if (v < 0) v = 0;
            v += inst->bevelH;
            if (v != inst->bottomMargin) {
                inst->bottomMargin = v;
                ubtp9_free_cache(inst);
                redraw = TRUE;
            }
            result = 1;
            break;
        }

        case GA_Selected:
            justBlit = TRUE;
            result = 1;
            break;

        case GA_Disabled:
            justBlit = TRUE;
            result = 1;
            break;

        case UBTP9_FlushDebugOutput:
            flushbdbprint();
            break;

        /* OS3.9 workaround: store ICA_TARGET and GA_ID in instance data */
        case ICA_TARGET:
            inst->target = (Object *)tag->ti_Data;
            result = 1;
            break;

        case GA_ID:
            inst->ga_id = tag->ti_Data;
            break;

        default:
            break;
        }
    }

    if (redraw && inst->dc) {
        struct URPTextMetric m;
        WORD gadW = G(o)->Width;
        WORD gadH = G(o)->Height;

        if (inst->text && inst->text[0]) {
            URPDC_TextSizeUTF8(inst->dc, inst->text, -1, &m);
            inst->textWidth  = (m.width  > 0) ? (WORD)m.width  : 16;
            inst->textHeight = (m.height > 0) ? (WORD)m.height : 8;
        } else {
            inst->textWidth  = 16;
            inst->textHeight = 8;
        }

        if (gadW > 0 && gadH > 0 && inst->screen)
            ubtp9_rebuild_cache(cl, o, gadW, gadH, inst->drawInfo, inst->screen);
        else
            ubtp9_update_font_metrics(inst);
    }

    if ((redraw || justBlit) && msg->ops_GInfo)
        ubtp9_render_self(cl, o, msg->ops_GInfo);

    return result;
}

/* =========================================================================
 * OM_GET
 * =========================================================================
 */
ULONG UniButtonP9_OnGet(Class *cl, Object *o, struct opGet *msg)
{
    UniButtonP9Data *inst = UBTP9_DATA(cl, o);

    switch (msg->opg_AttrID)
    {
    case UBTP9_URPDrawContext:
        *msg->opg_Storage = (ULONG)inst->dc;
        return TRUE;
    case UBTP9_BevelStyle:
        *msg->opg_Storage = inst->bevelStyle;
        return TRUE;
    case UBTP9_Transparent:
        *msg->opg_Storage = (ULONG)inst->transparent;
        return TRUE;
    case UBTP9_PushButton:
        *msg->opg_Storage = (ULONG)inst->pushButton;
        return TRUE;
    case GA_ReadOnly:
        *msg->opg_Storage = (ULONG)inst->readOnly;
        return TRUE;
    case GA_Text:
        *msg->opg_Storage = (ULONG)inst->text;
        return TRUE;
    case UBTP9_TextPen:
        *msg->opg_Storage = inst->txtPen;
        return TRUE;
    case UBTP9_BgPen:
        *msg->opg_Storage = inst->bgPen;
        return TRUE;
    case UBTP9_SelBgPen:
        *msg->opg_Storage = inst->selBgPen;
        return TRUE;
    case UBTP9_PointSize:
        *msg->opg_Storage = inst->pointSize;
        return TRUE;
    case UBTP9_LeftMargin:
        *msg->opg_Storage = (ULONG)(WORD)(inst->leftMargin  - inst->bevelV);
        return TRUE;
    case UBTP9_RightMargin:
        *msg->opg_Storage = (ULONG)(WORD)(inst->rightMargin - inst->bevelV);
        return TRUE;
    case UBTP9_TopMargin:
        *msg->opg_Storage = (ULONG)(WORD)(inst->topMargin   - inst->bevelH);
        return TRUE;
    case UBTP9_BottomMargin:
        *msg->opg_Storage = (ULONG)(WORD)(inst->bottomMargin - inst->bevelH);
        return TRUE;
    /* OS3.9 DoSuperMethodA workaround */
    case GA_Width:
        *msg->opg_Storage = (ULONG)(LONG)G(o)->Width;
        return TRUE;
    case GA_Height:
        *msg->opg_Storage = (ULONG)(LONG)G(o)->Height;
        return TRUE;
    case GA_Top:
        *msg->opg_Storage = (ULONG)(LONG)G(o)->TopEdge;
        return TRUE;
    case GA_Left:
        *msg->opg_Storage = (ULONG)(LONG)G(o)->LeftEdge;
        return TRUE;
    default:
        return DoSuperMethodA(cl, o, (APTR)msg);
    }
}
