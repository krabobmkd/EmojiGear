/*
 * UniButtonP9 – GM_LAYOUT, GM_RENDER, GM_DOMAIN.
 *
 * No bitmap cache: background (a Patch9 skin) and text are both drawn
 * live, directly onto the real destination RastPort, on every single blit
 * -- see ubtp9_blit_state in unibuttonp9_attribs.c. A Patch9 skin's
 * background can be anything (including transparent, masked corners
 * revealing whatever is really behind the button), so there's nothing
 * flat/known to pre-bake text against; URPDrawTextUTF8 itself handles
 * blending against the real destination pixels correctly, which a
 * cached-bitmap-then-blit approach cannot. (This does mean text glyph
 * rasterisation, i.e. FreeType, can run from GM_GOACTIVE/GM_HANDLEINPUT/
 * GM_GOINACTIVE's input.device context -- acceptable here since
 * utf8rastport's own glyph cache means that's only a real FreeType call
 * on a genuine cache miss.)
 *
 * See UniButtonBGBM for the sibling class that never uses a Patch9 and
 * instead caches each state as a small offscreen bitmap.
 */
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/alib.h>
#include <proto/bevel.h>
#include <images/bevel.h>
#include <intuition/gadgetclass.h>
#include <graphics/gfx.h>
#include "unibuttonp9_private.h"
#include "../bdbprintf.h"

/* =========================================================================
 * ubtp9_update_font_metrics
 * =========================================================================
 */
void ubtp9_update_font_metrics(UniButtonP9Data *inst)
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
 * ubtp9_state_pens
 * =========================================================================
 * Shared with ubtp9_blit_state (unibuttonp9_attribs.c), which needs the
 * same disabled-state dimming for its live text draw, and GM_RENDER below,
 * which needs outImageState for the bevel. Pure pen/RGB math -- no
 * FreeType, safe from any context.
 */
void ubtp9_state_pens(UniButtonP9Data *inst, int state, struct Screen *scr,
                      ULONG *outBgPen, ULONG *outTxtPen, UWORD *outImageState)
{
    if (state == UBTP9_STATE_DISABLED && scr && scr->ViewPort.ColorMap) {
        struct ColorMap *cm = scr->ViewPort.ColorMap;
        ULONG txtRGB[3];
        ULONG dimR, dimG, dimB;

        *outBgPen = (ULONG)FindColor(cm,
                    0x88888888UL, 0x88888888UL, 0x88888888UL, (ULONG)-1);

        GetRGB32(cm, inst->txtPen, 1UL, txtRGB);
        dimR = (txtRGB[0] >> 1) + 0x44444444UL;
        dimG = (txtRGB[1] >> 1) + 0x44444444UL;
        dimB = (txtRGB[2] >> 1) + 0x44444444UL;
        *outTxtPen = (ULONG)FindColor(cm, dimR, dimG, dimB, (ULONG)-1);

        *outImageState = IDS_NORMAL;
    } else {
        switch (state) {
        case UBTP9_STATE_SELECTED:
            *outBgPen      = inst->selBgPen;
            *outTxtPen     = inst->txtPen;
            *outImageState = IDS_SELECTED;
            break;
        default:
            *outBgPen      = inst->bgPen;
            *outTxtPen     = inst->txtPen;
            *outImageState = IDS_NORMAL;
            break;
        }
    }
}

/* =========================================================================
 * GM_LAYOUT
 * =========================================================================
 */
ULONG UniButtonP9_OnLayout(Class *cl, Object *o, struct gpLayout *msg)
{
    (void)cl; (void)o;
    return DoSuperMethodA(cl, o, (APTR)msg);
}

/* =========================================================================
 * GM_RENDER
 * =========================================================================
 */
ULONG UniButtonP9_OnRender(Class *cl, Object *o, struct gpRender *msg)
{
    UniButtonP9Data *inst  = UBTP9_DATA(cl, o);
    struct RastPort *rp    = msg->gpr_RPort;
    struct Gadget   *g     = G(o);
    WORD             gadW  = g->Width;
    WORD             gadH  = g->Height;
    struct Screen   *scr   = msg->gpr_GInfo ? msg->gpr_GInfo->gi_Screen : NULL;
    struct DrawInfo *dri   = msg->gpr_GInfo ? msg->gpr_GInfo->gi_DrInfo : NULL;
    UWORD            imageState;
    int              state;

    if (gadW <= 0 || gadH <= 0) return 0;

    if (scr) inst->screen   = scr;
    if (dri) inst->drawInfo = dri;

    if (scr && inst->dc)
        URPDC_SetDrawScreen(inst->dc, scr);

    if (inst->selBgPen == 3 && dri)
        inst->selBgPen = (ULONG)dri->dri_Pens[FILLPEN];

    if (g->Flags & GFLG_DISABLED)
        state = UBTP9_STATE_DISABLED;
    else if (g->Flags & GFLG_SELECTED)
        state = UBTP9_STATE_SELECTED;
    else
        state = UBTP9_STATE_NORMAL;

    ubtp9_blit_state(inst, g, rp, state, &imageState);

    if (inst->bevel && dri) {
        SetAttrs((Object *)inst->bevel,
            IA_Left,   g->LeftEdge,
            IA_Top,    g->TopEdge,
            IA_Width,  (ULONG)gadW,
            IA_Height, (ULONG)gadH,
            TAG_DONE);
        DrawImageState(rp, (struct Image *)inst->bevel, 0L, 0L,
                       (ULONG)imageState, dri);
    }

    return 0;
}

/* =========================================================================
 * GM_DOMAIN
 * =========================================================================
 */
ULONG UniButtonP9_OnDomain(Class *cl, Object *o, struct gpDomain *msg)
{
    UniButtonP9Data *inst   = UBTP9_DATA(cl, o);
    struct IBox     *domain = &msg->gpd_Domain;
    WORD             minW, minH;

    minH = inst->textHeight > 0 ? inst->textHeight
                                : (inst->pointSize > 0 ? (WORD)inst->pointSize : 16);
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
