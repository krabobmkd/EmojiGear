/*
 * UniButton gadget – GM_LAYOUT, GM_RENDER, GM_DOMAIN.
 *
 * GM_RENDER strategy
 * ------------------
 * Three OffscreenBitMaps are maintained as a cache:
 *   [0] normal   – bgPen background, bevel IDS_NORMAL,   text centred
 *   [1] selected – selBgPen background, bevel IDS_SELECTED, text centred
 *   [2] disabled – dimmed text over normal background
 *
 * All three are rebuilt whenever the gadget size changes or the cache is
 * marked dirty (text/font/pen change).  Each rebuild uses FreeType via
 * URPDrawTextUTF8, which is safe only in the application-task context
 * (i.e. inside GM_RENDER).
 *
 * Once the cache is valid, GM_GOACTIVE / GM_GOINACTIVE can blit the
 * appropriate bitmap using BltBitMapRastPort – a plain graphics call
 * that is safe in the input.device context.
 */
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/alib.h>
#include <proto/bevel.h>
#include <images/bevel.h>
#include <intuition/gadgetclass.h>
#include <graphics/gfx.h>
#include "bdbprintf.h"
#include "unibutton_private.h"

/* =========================================================================
 * ubt_build_one_state
 *
 * Render one cached state bitmap into cacheBm[state].
 * Called only from GM_RENDER (application task – FreeType calls are safe).
 *
 * The offscreen bitmap is gadget-sized.  Content:
 *   1. Fill entire area with the appropriate background pen.
 *   2. Draw the bevel frame (with DrawImageState so shine/shadow flip for
 *      IDS_SELECTED).
 *   3. Render the UTF-8 label centred within the content area.
 *
 * For the disabled state, the text is rendered in bgPen colour (invisible),
 * then a fine dot ghost pattern is overlaid.
 * =========================================================================
 */
static void ubt_build_one_state(UniButtonData *inst, WORD gadW, WORD gadH,
                                 int state, struct DrawInfo *dri,
                                 struct Screen *scr)
{
    OffscreenBitMap *obm = &inst->cacheBm[state];
    struct RastPort *rp;
    ULONG            bgPen;
    UWORD            imageState;
    UWORD            ghostPen;

    OffscreenBitMap_Close(obm);
    OffscreenBitMap_Init(obm, (int)gadW, (int)gadH, 0, BMF_CLEAR,
                         scr ? scr->RastPort.BitMap : NULL);
    if (!obm->_rp) return;

    bdbprintf("buildonestate: %d %08x %08x %08x %08x\n",
            state,
            (int)obm->_bm,
            (int)obm->_rp,
            (int)obm->_layerinfo,
            (int)obm->_layer
            );


    rp = obm->_rp;

    switch (state) {
    case UBT_STATE_SELECTED:
        bgPen      = inst->selBgPen;
        imageState = IDS_SELECTED;
        break;
    case UBT_STATE_DISABLED:
        bgPen      = inst->bgPen;
        imageState = IDS_NORMAL;
        break;
    default: /* UBT_STATE_NORMAL */
        bgPen      = inst->bgPen;
        imageState = IDS_NORMAL;
        break;
    }

    /* 1. Fill background */
    if (!inst->transparent || state == UBT_STATE_SELECTED) {
        SetAPen(rp, (LONG)bgPen);
        SetDrMd(rp, JAM1);
        RectFill(rp, 0L, 0L, (LONG)(gadW - 1), (LONG)(gadH - 1));
    }

    /* 2. Draw bevel frame */
    if (inst->bevel && dri) {
        SetAttrs((Object *)inst->bevel,
            IA_Left,   0UL,
            IA_Top,    0UL,
            IA_Width,  (ULONG)gadW,
            IA_Height, (ULONG)gadH,
            TAG_DONE);
        DrawImageState(rp, (struct Image *)inst->bevel, 0L, 0L,
                       (ULONG)imageState, dri);
    }

    /* 3. Render UTF-8 label centred in content area */
    if (inst->text && inst->text[0] && inst->dc && scr) {
        WORD contentLeft  = inst->leftMargin;
        WORD contentTop   = inst->topMargin;
        WORD contentW     = gadW - inst->leftMargin - inst->rightMargin;
        WORD contentH     = gadH - inst->topMargin  - inst->bottomMargin;
        struct URPTextMetric metric;
        struct URPTextPos    pos;
        WORD textX, textY;

        if (contentW <= 0 || contentH <= 0) return;

        URPDC_TextSizeUTF8(inst->dc, inst->text, -1, &metric);

        textX = contentLeft + (contentW - metric.width) / 2;
        textY = contentTop  + (contentH - inst->fontHeight) / 2
                + inst->fontAscent;

        if (textX < contentLeft) textX = contentLeft;
        if (textY < contentTop + inst->fontAscent)
            textY = contentTop + inst->fontAscent;

        pos.x = textX;
        pos.y = textY;

        if (state == UBT_STATE_DISABLED) {
            /* render with background pen to make it invisible, then ghost */
            URPDC_SetDrawColorFromPen(inst->dc, scr,
                                      (LONG)bgPen, (LONG)bgPen);
            SetAPen(rp, (LONG)bgPen);
            SetBPen(rp, (LONG)bgPen);
        } else {
            URPDC_SetDrawColorFromPen(inst->dc, scr,
                                      (LONG)inst->txtPen, (LONG)bgPen);
            SetAPen(rp, (LONG)inst->txtPen);
            SetBPen(rp, (LONG)bgPen);
        }
        SetDrMd(rp, JAM2);
        URPDrawTextUTF8(rp, inst->dc, &pos, inst->text, (ULONG)(-1));
    }

    /* Ghost pattern overlay for disabled state */
    if (state == UBT_STATE_DISABLED && dri) {
        /*TODO redo that with one blit,
         *  this code is a joke - WritePixel calls are slowest-est-ests
        WORD x, y;
        ghostPen = dri->dri_Pens[BACKGROUNDPEN];
        SetAPen(rp, (LONG)ghostPen);
        SetDrMd(rp, JAM1);
         fine 2-pixel checkerboard: set every other pixel in a 2x2 tile
        for (y = 0; y < gadH; y += 2) {
            for (x = (WORD)(y & 2 ? 0 : 1); x < gadW; x += 2) {
                WritePixel(rp, (LONG)x, (LONG)y);
            }
        }
        */
    }
}

/* =========================================================================
 * ubt_update_font_metrics
 *
 * Query the draw context for ascender + full line height (ascender +
 * descender) across ALL loaded fonts, exactly like UniTextEditor's
 * updateLineHeight().  The fallback string "Agpqj" is intentional: it
 * contains both tall ascenders and deep descenders so the measured height
 * covers the full glyph cell including pixels below the baseline.
 *
 * Safe to call any time dc is non-NULL.
 * =========================================================================
 */
void ubt_update_font_metrics(UniButtonData *inst)
{
    struct URPTextMetric metric;
    if (!inst->dc) return;
    URPDC_GetFontLineMetrics(inst->dc, &metric);
    if (metric.height <= 0)
        URPDC_TextSizeUTF8(inst->dc, "Agpqj", -1, &metric);
    inst->fontHeight = (metric.height > 0) ? metric.height
                                           : (WORD)inst->pointSize;
    inst->fontAscent = (metric.baseY  > 0) ? metric.baseY
                                           : inst->fontHeight;
}

/* =========================================================================
 * ubt_rebuild_cache
 *
 * Rebuild all three state bitmaps.  Called from GM_RENDER when size changes
 * or the cache has been invalidated.
 * =========================================================================
 */
void ubt_rebuild_cache(Class *cl, Object *o,
                               WORD gadW, WORD gadH,
                               struct DrawInfo *dri,
                               struct Screen   *scr)
{
    UniButtonData *inst = UBT_DATA(cl, o);
    int i;

    ubt_update_font_metrics(inst);

    for (i = 0; i < UBT_NUM_STATES; i++)
        ubt_build_one_state(inst, gadW, gadH, i, dri, scr);

    inst->cacheValid  = TRUE;
    inst->cacheWidth  = gadW;
    inst->cacheHeight = gadH;
}

/* =========================================================================
 * GM_LAYOUT
 * =========================================================================
 */
ULONG UniButton_OnLayout(Class *cl, Object *o, struct gpLayout *msg)
{
    (void)cl; (void)o;
    return DoSuperMethodA(cl, o, (APTR)msg);
}

/* =========================================================================
 * GM_RENDER
 * =========================================================================
 */
ULONG UniButton_OnRender(Class *cl, Object *o, struct gpRender *msg)
{
    UniButtonData  *inst   = UBT_DATA(cl, o);
    struct RastPort *rp    = msg->gpr_RPort;
    struct Gadget   *g     = G(o);
    WORD             gadW  = g->Width;
    WORD             gadH  = g->Height;
    struct Screen   *scr   = msg->gpr_GInfo ? msg->gpr_GInfo->gi_Screen : NULL;
    struct DrawInfo *dri   = msg->gpr_GInfo ? msg->gpr_GInfo->gi_DrInfo : NULL;
    BOOL             needRebuild;
    int              state;

    if (gadW <= 0 || gadH <= 0) return 0;

    /* Cache the screen and DrawInfo for use in render helpers */
    if (scr)  inst->screen   = scr;
    if (dri)  inst->drawInfo = dri;

    /* Update the screen reference in the draw context */
    if (scr && inst->dc)
        URPDC_SetDrawScreen(inst->dc, scr);

    /* When selBgPen hasn't been explicitly set, use FILLPEN from DrawInfo */
    if (inst->selBgPen == 3 && dri)
        inst->selBgPen = (ULONG)dri->dri_Pens[FILLPEN];

    /* Rebuild cache if size changed or cache was invalidated */
    needRebuild = (!inst->cacheValid ||
                   inst->cacheWidth  != gadW ||
                   inst->cacheHeight != gadH);

    if (needRebuild && scr) {
        ubt_rebuild_cache(cl, o, gadW, gadH, dri, scr);
    }

    /* Pick the correct state bitmap */
    if (g->Flags & GFLG_DISABLED)
        state = UBT_STATE_DISABLED;
    else if (g->Flags & GFLG_SELECTED)
        state = UBT_STATE_SELECTED;
    else
        state = UBT_STATE_NORMAL;

    if (inst->cacheValid && inst->cacheBm[state]._bm) {
        ubt_blit_state(inst, g, rp, state);
    } else {
        /* Fallback: fill with background pen */
        ULONG bgPen = (state == UBT_STATE_SELECTED) ? inst->selBgPen : inst->bgPen;
        SetAPen(rp, (LONG)bgPen);
        SetDrMd(rp, JAM1);
        RectFill(rp, (LONG)g->LeftEdge, (LONG)g->TopEdge,
                 (LONG)(g->LeftEdge + gadW - 1),
                 (LONG)(g->TopEdge  + gadH - 1));
    }

    return 0;
}

/* =========================================================================
 * GM_DOMAIN
 * =========================================================================
 */
ULONG UniButton_OnDomain(Class *cl, Object *o, struct gpDomain *msg)
{
    UniButtonData  *inst   = UBT_DATA(cl, o);
    struct IBox    *domain = &msg->gpd_Domain;
    WORD            minW, minH;

    /* fontHeight, fontAscent and textWidth are pre-computed in OM_SET so that
     * GM_DOMAIN never needs FreeType calls (it may run on the input.device task
     * on some OS versions, where FreeType is unsafe, and it is called repeatedly). */

    /* Minimum height must fit the full glyph cell (ascender + descender) */
    minH = inst->fontHeight > 0 ? inst->fontHeight
                                : (inst->pointSize > 0 ? (WORD)inst->pointSize : 16);

    /* Minimum width from cached text advance width */
    minW = (inst->textWidth > 0) ? inst->textWidth : 32;

    minW += inst->leftMargin + inst->rightMargin;
    minH += inst->topMargin  + inst->bottomMargin;

    domain->Left   = 0;
    domain->Top    = 0;
    domain->Width  = minW;
    domain->Height = minH;

    switch (msg->gpd_Which) {
    case GDOMAIN_MINIMUM:
        break;
    case GDOMAIN_MAXIMUM:
        domain->Width  = 32767;
        domain->Height = 32767;
        break;
    case GDOMAIN_NOMINAL:
    default:
        break;
    }

    return DoSuperMethodA(cl, o, (APTR)msg);
}
