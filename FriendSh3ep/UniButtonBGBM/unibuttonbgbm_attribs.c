/* UniButtonBGBM – attribute handlers: OM_NEW, OM_DISPOSE, OM_SET, OM_GET. */
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/alib.h>
#include <proto/bevel.h>
#include <images/bevel.h>

#include "unibuttonbgbm_private.h"
#include "../bdbprintf.h"

#ifndef GA_Text
#define GA_Text (GA_Dummy + 0x0039)
#endif

/* =========================================================================
 * Internal helpers
 * =========================================================================
 */

void ubgbm_free_cache(UniButtonBGBMData *inst)
{
    int i;
    for (i = 0; i < UBGBM_NUM_STATES; i++)
        OffscreenBitMap_Close(&inst->cacheBm[i]);
    inst->cacheValid = FALSE;
}

/* Blit a dstW x dstH crop of srcBm (srcW x srcH) starting at (shiftX,shiftY),
 * to (dstX,dstY) -- no mask (style-image backgrounds are always fully
 * opaque). Vertically the source is expected to be at least dstH tall, so
 * shiftY is just clamped down enough that the crop still fits (or the blit
 * itself is shrunk if even srcH < dstH). Horizontally the source is treated
 * as cyclic/tileable instead: shiftX is normalized modulo srcW, then the
 * source is blitted left-to-right in as many chunks as needed to cover the
 * whole dstW span, wrapping back to source x=0 each time a chunk runs off
 * the source's right edge -- this is what lets one narrow tileable pattern
 * cover every nav bar button's shiftX (see ubgbm_blit_state), including
 * buttons whose shiftX or dstW exceeds srcW. */
static void ubgbm_blit_bg(struct BitMap *srcBm, WORD srcW, WORD srcH,
                          WORD shiftX, WORD shiftY,
                          struct RastPort *rp,
                          WORD dstX, WORD dstY, WORD dstW, WORD dstH)
{
    WORD blitH;
    WORD srcX;
    WORD remaining;
    WORD curDstX;

    if (srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0) return;

    if (shiftY < 0) shiftY = 0;

    blitH = dstH;
    if (blitH > srcH) blitH = srcH;
    if (shiftY + blitH > srcH) shiftY = srcH - blitH;

    if (shiftX < 0) shiftX = 0;
    srcX = shiftX % srcW;

    remaining = dstW;
    curDstX   = dstX;
    while (remaining > 0) {
        WORD chunk = srcW - srcX;
        if (chunk > remaining) chunk = remaining;

        BltBitMapRastPort(srcBm, (LONG)srcX, (LONG)shiftY, rp,
                          (LONG)curDstX, (LONG)dstY, (LONG)chunk, (LONG)blitH, 0xC0);

        curDstX   += chunk;
        remaining -= chunk;
        srcX = 0;
    }
}

/* FS3E_COLOR_BTBGBM_TEXT_* role for a given UBGBM_STATE_*. */
static ULONG ubgbm_style_text_pen(UniButtonBGBMData *inst, int state)
{
    FS3EColorRole role = (state == UBGBM_STATE_SELECTED)
        ? FS3E_COLOR_BTBGBM_TEXT_SELECTED : FS3E_COLOR_BTBGBM_TEXT_ENABLED;
    return (ULONG)FS3E_PEN(inst->style, role);
}

void ubgbm_blit_state(UniButtonBGBMData *inst, struct Gadget *g,
                      struct RastPort *rp, int state)
{
    OffscreenBitMap *obm = &inst->cacheBm[state];
    WORD textX, textY;

    if (!inst->cacheValid) return;

    if (inst->patch9Mode) {
        /* Whole composited state (background + text) already baked into
         * obm by ubgbm_build_one_state() -- one opaque blit, no separate
         * fill/text steps needed (unlike either mode below). */
        if (!obm->_bm) return;
        BltBitMapRastPort(obm->_bm, 0, 0, rp,
                          (LONG)g->LeftEdge, (LONG)g->TopEdge,
                          (LONG)obm->_w, (LONG)obm->_h, 0xC0);
        return;
    }

    if (inst->style && BmImage_IsLoaded(&inst->style->btbgbmBitmap[state])) {
        BmImage *img = &inst->style->btbgbmBitmap[state];
        WORD shiftX = 0, shiftY = 0;

        /* The background image is shared across all 8 nav bar buttons and
         * deliberately oversized so each button crops its own region out of
         * it -- the crop offset is just this gadget's position relative to
         * navBarLayout (the layout all 8 live in), computed here from real
         * post-layout coordinates rather than a hand-maintained shift value
         * per button (see UBGBM_Style's doc comment). navBarLayout is a
         * layout.gadget subclass (NavBarLayoutClass), so its LeftEdge/
         * TopEdge are valid struct Gadget fields once laid out. */
        if (app && app->navBarLayout) {
            struct Gadget *navGad = (struct Gadget *)app->navBarLayout;
            shiftX = g->LeftEdge - navGad->LeftEdge;
            shiftY = g->TopEdge  - navGad->TopEdge;
        }

        /* Background: a cropped, fully opaque (no transparency/masking)
         * window into the (deliberately oversized) source image at
         * (shiftX,shiftY), directly on the real destination -- blitter-
         * only, safe from any context. Drawn live (never cached) because
         * the image isn't flat -- see the file header comment for why that
         * rules out pre-baking text against it in an offscreen cache. */
        ubgbm_blit_bg(img->bitmap, (WORD)img->width, (WORD)img->height,
                     shiftX, shiftY, rp,
                     g->LeftEdge, g->TopEdge, g->Width, g->Height);

        /* Text: also drawn live, directly on top of the image just drawn,
         * via URPDrawTextUTF8, using FS3EStyle's colour for this state
         * instead of UBGBM_TextPen. bgPen here is only the flat colour
         * assumed for glyph anti-aliasing blend, same role as
         * UBGBM_BgPen/SelBgPen play in flat-colour mode -- not read back
         * from the image, which may not be flat. */
        if (inst->text && inst->text[0] && inst->dc && inst->screen) {
            struct URPTextPos pos;
            ULONG txtPen = ubgbm_style_text_pen(inst, state);
            ULONG bgPen  = (state == UBGBM_STATE_SELECTED) ? inst->selBgPen : inst->bgPen;

            pos.x = g->LeftEdge + (g->Width  - inst->textWidth)  / 2;
            pos.y = g->TopEdge  + (g->Height - inst->textHeight) / 2 + inst->fontAscent;

            URPDC_SetDrawColorFromPen(inst->dc, inst->screen, (LONG)txtPen, (LONG)bgPen);
            SetAPen(rp, (LONG)txtPen);
            SetBPen(rp, (LONG)bgPen);
            SetDrMd(rp, JAM1); /* important for mono no aa fonts and BltTemplate */
            URPDrawTextUTF8(rp, inst->dc, &pos, inst->text, (ULONG)(-1));
        }
        return;
    }

    /* Flat-colour mode: background drawn live as a plain RectFill, then the
     * cached text bitmap (built in ubgbm_build_one_state, application-task
     * context, FreeType-safe) blitted on top. Safe from
     * GM_GOACTIVE/GM_HANDLEINPUT/GM_GOINACTIVE's input.device context too,
     * since nothing here touches FreeType. */
    SetAPen(rp, (LONG)obm->_bgpen);
    SetDrMd(rp, JAM1);
    RectFill(rp, (LONG)g->LeftEdge, (LONG)g->TopEdge,
                 (LONG)(g->LeftEdge + g->Width  - 1),
                 (LONG)(g->TopEdge  + g->Height - 1));

    if (!obm->_bm) return;

    textX = (g->Width  - obm->_w) / 2;
    textY = (g->Height - obm->_h) / 2;

    BltBitMapRastPort(obm->_bm, 0, 0,
                      rp,
                      (LONG)g->LeftEdge + textX, (LONG)g->TopEdge + textY,
                      (LONG)obm->_w, (LONG)obm->_h,
                      0xC0);
}

void ubgbm_render_self(Class *cl, Object *o, struct GadgetInfo *gi)
{
    struct RastPort *rp;
    if (!gi) return;
    rp = ObtainGIRPort(gi);
    if (rp) {
        DoMethod(o, GM_RENDER, gi, rp, GREDRAW_UPDATE);
        ReleaseGIRPort(rp);
    }
}

void ubgbm_notify_pressed(Class *cl, Object *o, struct GadgetInfo *gi)
{
    UniButtonBGBMData *inst = UBGBM_DATA(cl, o);
    struct opUpdate  nmsg;
    ULONG tags[5];

    if (!inst->target || !inst->ga_id) return;

    tags[0] = GA_ID;
    tags[1] = inst->ga_id;
    tags[2] = GA_Selected;
    tags[3] = TRUE;
    tags[4] = TAG_DONE;

    GetAttr(GA_Selected,o,&tags[3]);

    /* Direct DoMethodA to target: avoids the OS3.9 DoSuperMethodA/OM_NOTIFY bug */
    nmsg.MethodID     = OM_UPDATE;
    nmsg.opu_AttrList = (struct TagItem *)tags;
    nmsg.opu_GInfo    = gi;
    nmsg.opu_Flags    = 0;
    DoMethodA(inst->target, (Msg)&nmsg);
}

/* Same OM_UPDATE-to-target mechanism as ubgbm_notify_pressed() above --
 * the standard, task-safe way any BOOPSI gadget asks the main process for
 * something (see fs3eboopsimessage.c/.h: TargetInstance's dispatcher
 * queues the message and Signal()s SIGBREAKF_CTRL_F itself, so nothing
 * here needs to touch Signal() directly). Used by GM_RENDER
 * (unibuttonbgbm_render.c) when UBGBM_Patch9Mode's gadget-sized cache is
 * stale but this call isn't running on the object's owning task, so it
 * can't safely rebuild it here (FreeType) -- asks the main task to force
 * a real redraw instead. Sends this gadget's own real GA_ID (inst->ga_id)
 * -- GA_ID identifies the gadget, same as every other notify from this
 * class, NOT a made-up sentinel -- plus UBGBM_RefreshNeeded, TRUE (must
 * also be listed in fs3eboopsimessage.c's delayedAttribs[] to actually
 * reach the drained message). */
void ubgbm_notify_refresh_needed(UniButtonBGBMData *inst, struct GadgetInfo *gi)
{
    struct opUpdate nmsg;
    ULONG tags[5];

    if (!inst->target || !inst->ga_id) return;

    tags[0] = GA_ID;
    tags[1] = inst->ga_id;
    tags[2] = UBGBM_RefreshNeeded;
    tags[3] = TRUE;
    tags[4] = TAG_DONE;

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
ULONG UniButtonBGBM_OnNew(Class *cl, Object *o, struct opSet *msg)
{
    UniButtonBGBMData *inst;
    Object          *newObj;
    ULONG            mDispose;
    struct TagItem  *ptag;
    ULONG            urpflags;
    ULONG            bevelStyleVal;

    newObj = (Object *)DoSuperMethodA(cl, o, (APTR)msg);
    if (!newObj) return 0;

    inst = UBGBM_DATA(cl, newObj);
    memset(inst, 0, sizeof(UniButtonBGBMData));

    inst->callerTask   = FindTask(NULL);

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
        ptag = FindTagItem(UBGBM_URPDrawContext, msg->ops_AttrList);
        if (ptag) externalDc = (struct URPDrawContext *)ptag->ti_Data;

        if (externalDc) {
            URPDC_Retain(externalDc);
            inst->dc = externalDc;
        } else {
            inst->dc = URPDC_Create(NULL);
            urpflags = URP_PREF_ANTIALIAS |
                       URP_PREF_CLUTMODE_NOMASK |
                       URP_PREF_HIGHFILTERING;
            ptag = FindTagItem(UBGBM_URPPrefs, msg->ops_AttrList);
            if (ptag) urpflags = ptag->ti_Data;
            if (inst->dc)
                URPDC_SetPreferenceFlags(inst->dc, urpflags);
        }
    }
    if (!inst->dc) goto fail;

    /* Bevel */
    bevelStyleVal = BVS_BUTTON;
    ptag = FindTagItem(UBGBM_BevelStyle, msg->ops_AttrList);
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
    UniButtonBGBM_OnSet(cl, newObj, msg);

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
ULONG UniButtonBGBM_OnDispose(Class *cl, Object *o, Msg msg)
{
    UniButtonBGBMData *inst = UBGBM_DATA(cl, o);
    ubgbm_free_cache(inst);
    if (inst->text)  { FreeVec(inst->text);        inst->text  = NULL; }
    if (inst->bevel) { DisposeObject(inst->bevel);  inst->bevel = NULL; }
    if (inst->dc)    { URPDC_Release(inst->dc);     inst->dc    = NULL; }
    return DoSuperMethodA(cl, o, (APTR)msg);
}

/* =========================================================================
 * OM_SET / OM_UPDATE
 * =========================================================================
 */
ULONG UniButtonBGBM_OnSet(Class *cl, Object *o, struct opSet *msg)
{
    UniButtonBGBMData *inst     = UBGBM_DATA(cl, o);
    struct TagItem  *state    = msg->ops_AttrList;
    struct TagItem  *tag;
    ULONG            result   = 0;
    BOOL             redraw   = FALSE;
    BOOL             justBlit = FALSE;

    while ((tag = NextTagItem(&state)) != NULL) {
        switch (tag->ti_Tag)
        {
        case UBGBM_URPDrawContext:
        {
            struct URPDrawContext *newDc = (struct URPDrawContext *)tag->ti_Data;
            if (newDc != inst->dc) {
                if (inst->dc) URPDC_Release(inst->dc);
                if (newDc)    URPDC_Retain(newDc);
                inst->dc = newDc;
                ubgbm_free_cache(inst);
                redraw = TRUE;
            }
            result = 1;
            break;
        }

        case UBGBM_PointSize:
            if (inst->pointSize != (ULONG)tag->ti_Data) {
                inst->pointSize = (ULONG)tag->ti_Data;
                if (inst->dc)
                    URPDC_ChangeFontsSize(inst->dc, inst->pointSize, ~0UL);
                ubgbm_free_cache(inst);
                redraw = TRUE;
            }
            result = 1;
            break;

        case UBGBM_FontFlags:
            inst->fontFlags = (ULONG)tag->ti_Data;
            result = 1;
            break;

        case UBGBM_AddFont:
        {
            const char *p = (const char *)tag->ti_Data;
            if (p && p[0] && inst->dc) {
                URPDC_AddFont(inst->dc, p, (int)inst->pointSize, inst->fontFlags);
                ubgbm_free_cache(inst);
                redraw = TRUE;
            }
            result = 1;
            break;
        }

        case UBGBM_FlushFonts:
            if (inst->dc) {
                URPDC_FlushFonts(inst->dc);
                ubgbm_free_cache(inst);
                redraw = TRUE;
            }
            result = 1;
            break;

        case UBGBM_URPPrefs:
            if (inst->dc) {
                ULONG cur = URPDC_GetPreferenceFlags(inst->dc);
                if (cur != (ULONG)tag->ti_Data) {
                    URPDC_SetPreferenceFlags(inst->dc, tag->ti_Data);
                    ubgbm_free_cache(inst);
                    redraw = TRUE;
                }
            }
            result = 1;
            break;

        case UBGBM_TextPen:
            if (inst->txtPen != (ULONG)tag->ti_Data) {
                inst->txtPen = (ULONG)tag->ti_Data;
                ubgbm_free_cache(inst);
                redraw = TRUE;
            }
            result = 1;
            break;

        case UBGBM_BgPen:
            if (inst->bgPen != (ULONG)tag->ti_Data) {
                inst->bgPen = (ULONG)tag->ti_Data;
                ubgbm_free_cache(inst);
                redraw = TRUE;
            }
            result = 1;
            break;

        case UBGBM_SelBgPen:
            if (inst->selBgPen != (ULONG)tag->ti_Data) {
                inst->selBgPen = (ULONG)tag->ti_Data;
                ubgbm_free_cache(inst);
                redraw = TRUE;
            }
            result = 1;
            break;

        case UBGBM_Transparent:
            inst->transparent = tag->ti_Data ? TRUE : FALSE;
            ubgbm_free_cache(inst);
            redraw = TRUE;
            result = 1;
            break;

        case UBGBM_PushButton:
            inst->pushButton = tag->ti_Data ? TRUE : FALSE;
            result = 1;
            break;

        case UBGBM_Style:
            /* Always invalidate, even if the pointer is unchanged: an
             * FS3EStyle is an externally-owned, stable struct whose
             * btbgbmBitmap[] can be reloaded in place (e.g.
             * FS3EStyle_LoadThemeImages() on theme/screen change) without
             * the pointer value ever changing. */
            inst->style = (FS3EStyle *)tag->ti_Data;
            ubgbm_free_cache(inst);
            redraw = TRUE;
            result = 1;
            break;

        case UBGBM_Patch9Mode:
            inst->patch9Mode = tag->ti_Data ? TRUE : FALSE;
            ubgbm_free_cache(inst);
            redraw = TRUE;
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
            ubgbm_free_cache(inst);
            redraw = TRUE;
            result = 1;
            break;
        }

        case UBGBM_LeftMargin:
        {
            WORD v = (WORD)(ULONG)tag->ti_Data;
            if (v < 0) v = 0;
            v += inst->bevelV;
            if (v != inst->leftMargin) {
                inst->leftMargin = v;
                ubgbm_free_cache(inst);
                redraw = TRUE;
            }
            result = 1;
            break;
        }

        case UBGBM_RightMargin:
        {
            WORD v = (WORD)(ULONG)tag->ti_Data;
            if (v < 0) v = 0;
            v += inst->bevelV;
            if (v != inst->rightMargin) {
                inst->rightMargin = v;
                ubgbm_free_cache(inst);
                redraw = TRUE;
            }
            result = 1;
            break;
        }

        case UBGBM_TopMargin:
        {
            WORD v = (WORD)(ULONG)tag->ti_Data;
            if (v < 0) v = 0;
            v += inst->bevelH;
            if (v != inst->topMargin) {
                inst->topMargin = v;
                ubgbm_free_cache(inst);
                redraw = TRUE;
            }
            result = 1;
            break;
        }

        case UBGBM_BottomMargin:
        {
            WORD v = (WORD)(ULONG)tag->ti_Data;
            if (v < 0) v = 0;
            v += inst->bevelH;
            if (v != inst->bottomMargin) {
                inst->bottomMargin = v;
                ubgbm_free_cache(inst);
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

        case UBGBM_FlushDebugOutput:
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

        /* The cache is sized from textWidth/textHeight, not the gadget
         * size -- gadW/gadH are only used here as an "is this gadget laid
         * out yet" readiness check, same as GM_RENDER's. */
        if (gadW > 0 && gadH > 0 && inst->screen)
            ubgbm_rebuild_cache(cl, o, gadW, gadH, inst->drawInfo, inst->screen);
        else
            ubgbm_update_font_metrics(inst);
    }

    if ((redraw || justBlit) && msg->ops_GInfo)
        ubgbm_render_self(cl, o, msg->ops_GInfo);

    return result;
}

/* =========================================================================
 * OM_GET
 * =========================================================================
 */
ULONG UniButtonBGBM_OnGet(Class *cl, Object *o, struct opGet *msg)
{
    UniButtonBGBMData *inst = UBGBM_DATA(cl, o);

    switch (msg->opg_AttrID)
    {
    case UBGBM_URPDrawContext:
        *msg->opg_Storage = (ULONG)inst->dc;
        return TRUE;
    case UBGBM_BevelStyle:
        *msg->opg_Storage = inst->bevelStyle;
        return TRUE;
    case UBGBM_Transparent:
        *msg->opg_Storage = (ULONG)inst->transparent;
        return TRUE;
    case UBGBM_PushButton:
        *msg->opg_Storage = (ULONG)inst->pushButton;
        return TRUE;
    case UBGBM_Style:
        *msg->opg_Storage = (ULONG)inst->style;
        return TRUE;
    case UBGBM_Patch9Mode:
        *msg->opg_Storage = (ULONG)inst->patch9Mode;
        return TRUE;
    case GA_ReadOnly:
        *msg->opg_Storage = (ULONG)inst->readOnly;
        return TRUE;
    case GA_Text:
        *msg->opg_Storage = (ULONG)inst->text;
        return TRUE;
    case UBGBM_TextPen:
        *msg->opg_Storage = inst->txtPen;
        return TRUE;
    case UBGBM_BgPen:
        *msg->opg_Storage = inst->bgPen;
        return TRUE;
    case UBGBM_SelBgPen:
        *msg->opg_Storage = inst->selBgPen;
        return TRUE;
    case UBGBM_PointSize:
        *msg->opg_Storage = inst->pointSize;
        return TRUE;
    case UBGBM_LeftMargin:
        *msg->opg_Storage = (ULONG)(WORD)(inst->leftMargin  - inst->bevelV);
        return TRUE;
    case UBGBM_RightMargin:
        *msg->opg_Storage = (ULONG)(WORD)(inst->rightMargin - inst->bevelV);
        return TRUE;
    case UBGBM_TopMargin:
        *msg->opg_Storage = (ULONG)(WORD)(inst->topMargin   - inst->bevelH);
        return TRUE;
    case UBGBM_BottomMargin:
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
