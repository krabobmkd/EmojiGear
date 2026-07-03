/*
 * UniButtonBGBM – GM_LAYOUT, GM_RENDER, GM_DOMAIN.
 *
 * Three OffscreenBitMaps cache the normal, selected, and disabled states'
 * TEXT (sized to the text bounding box), rebuilt in GM_RENDER
 * (application-task context – FreeType calls safe). The background is a
 * flat colour, drawn live with a plain RectFill, then the cached text is
 * blitted on top -- see ubgbm_blit_state in unibuttonbgbm_attribs.c.
 * GM_GOACTIVE / GM_HANDLEINPUT / GM_GOINACTIVE only blit the cached bitmap
 * (safe in input device context; they must never trigger a rebuild).
 *
 * See UniButtonP9 for the sibling class that always uses a Patch9 9-slice
 * skin and draws both skin and text live instead of caching anything.
 */
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/alib.h>
#include <proto/bevel.h>
#include <images/bevel.h>
#include <intuition/gadgetclass.h>
#include <graphics/gfx.h>

#include "unibuttonbgbm_private.h"
#include "../bdbprintf.h"

/* =========================================================================
 * ubgbm_update_font_metrics
 * =========================================================================
 */
void ubgbm_update_font_metrics(UniButtonBGBMData *inst)
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
 * ubgbm_build_one_state
 * =========================================================================
 */
static void ubgbm_build_one_state(UniButtonBGBMData *inst, WORD gadW, WORD gadH,
                                  int state, struct DrawInfo *dri,
                                  struct Screen *scr)
{
    OffscreenBitMap *obm = &inst->cacheBm[state];
    struct RastPort *rp;
    ULONG            bgPen;
    ULONG            txtPen;
    UWORD            imageState;
    int w, h;

    if (state == UBGBM_STATE_DISABLED && scr && scr->ViewPort.ColorMap) {
        struct ColorMap *cm = scr->ViewPort.ColorMap;
        ULONG txtRGB[3];
        ULONG dimR, dimG, dimB;

        bgPen = (ULONG)FindColor(cm,
                    0x88888888UL, 0x88888888UL, 0x88888888UL, (ULONG)-1);

        GetRGB32(cm, inst->txtPen, 1UL, txtRGB);
        dimR = (txtRGB[0] >> 1) + 0x44444444UL;
        dimG = (txtRGB[1] >> 1) + 0x44444444UL;
        dimB = (txtRGB[2] >> 1) + 0x44444444UL;
        txtPen = (ULONG)FindColor(cm, dimR, dimG, dimB, (ULONG)-1);

        imageState = IDS_NORMAL;
    } else {
        switch (state) {
        case UBGBM_STATE_SELECTED:
            bgPen      = inst->selBgPen;
            txtPen     = inst->txtPen;
            imageState = IDS_SELECTED;
            break;
        default:
            bgPen      = inst->bgPen;
            txtPen     = inst->txtPen;
            imageState = IDS_NORMAL;
            break;
        }
    }

    w = inst->textWidth;
    h = inst->textHeight;
    if (w < 16) w = 16;
    if (h < 8)  h = 8;

    if (!obm->_bm || obm->_w != w || obm->_h != h) {
        OffscreenBitMap_Close(obm);
        OffscreenBitMap_Init(obm, w, h, 0, BMF_CLEAR,
                             scr ? scr->RastPort.BitMap : NULL);
    }
    if (!obm->_bm) return;
    rp = &obm->_srp;

    if (!inst->transparent || state != UBGBM_STATE_NORMAL) {
        SetAPen(rp, (LONG)bgPen);
        SetDrMd(rp, JAM1);
        RectFill(rp, 0L, 0L, (LONG)(w - 1), (LONG)(h - 1));
    }
    obm->_imageState = imageState;
    obm->_bgpen      = bgPen;

    if (inst->text && inst->text[0] && inst->dc && scr) {
        struct URPTextPos pos;
        pos.x = 0;
        pos.y = inst->fontAscent;

        URPDC_SetDrawColorFromPen(inst->dc, scr, (LONG)txtPen, (LONG)bgPen);
        SetAPen(rp, (LONG)txtPen);
        SetBPen(rp, (LONG)bgPen);
        SetDrMd(rp, JAM2);
        URPDrawTextUTF8(rp, inst->dc, &pos, inst->text, (ULONG)(-1));
    }
}

/* =========================================================================
 * ubgbm_rebuild_cache
 * =========================================================================
 */
void ubgbm_rebuild_cache(Class *cl, Object *o,
                         WORD gadW, WORD gadH,
                         struct DrawInfo *dri,
                         struct Screen   *scr)
{
    UniButtonBGBMData *inst = UBGBM_DATA(cl, o);
    int i;

    ubgbm_update_font_metrics(inst);

    for (i = 0; i < UBGBM_NUM_STATES; i++)
        ubgbm_build_one_state(inst, gadW, gadH, i, dri, scr);

    inst->cacheValid = TRUE;
}

/* =========================================================================
 * GM_LAYOUT
 * =========================================================================
 */
ULONG UniButtonBGBM_OnLayout(Class *cl, Object *o, struct gpLayout *msg)
{
    (void)cl; (void)o;
    return DoSuperMethodA(cl, o, (APTR)msg);
}

/* =========================================================================
 * GM_RENDER
 * =========================================================================
 */
ULONG UniButtonBGBM_OnRender(Class *cl, Object *o, struct gpRender *msg)
{
    UniButtonBGBMData *inst  = UBGBM_DATA(cl, o);
    struct RastPort *rp    = msg->gpr_RPort;
    struct Gadget   *g     = G(o);
    WORD             gadW  = g->Width;
    WORD             gadH  = g->Height;
    struct Screen   *scr   = msg->gpr_GInfo ? msg->gpr_GInfo->gi_Screen : NULL;
    struct DrawInfo *dri   = msg->gpr_GInfo ? msg->gpr_GInfo->gi_DrInfo : NULL;
    BOOL             needRebuild;
    int              state;

    if (gadW <= 0 || gadH <= 0) return 0;

    if (scr) inst->screen   = scr;
    if (dri) inst->drawInfo = dri;

    if (scr && inst->dc)
        URPDC_SetDrawScreen(inst->dc, scr);

    if (inst->selBgPen == 3 && dri)
        inst->selBgPen = (ULONG)dri->dri_Pens[FILLPEN];

    needRebuild = (!inst->cacheValid);

    if (needRebuild && scr)
        ubgbm_rebuild_cache(cl, o, gadW, gadH, dri, scr);

    if (g->Flags & GFLG_DISABLED)
        state = UBGBM_STATE_DISABLED;
    else if (g->Flags & GFLG_SELECTED)
        state = UBGBM_STATE_SELECTED;
    else
        state = UBGBM_STATE_NORMAL;

    if (inst->cacheValid && inst->cacheBm[state]._bm) {
        ubgbm_blit_state(inst, g, rp, state);
    } else {
        SetAPen(rp, (LONG)inst->cacheBm[state]._bgpen);
        SetDrMd(rp, JAM1);
        RectFill(rp, (LONG)g->LeftEdge, (LONG)g->TopEdge,
                     (LONG)(g->LeftEdge + gadW - 1),
                     (LONG)(g->TopEdge  + gadH - 1));
    }

    if (inst->bevel && dri) {
        SetAttrs((Object *)inst->bevel,
            IA_Left,   g->LeftEdge,
            IA_Top,    g->TopEdge,
            IA_Width,  (ULONG)gadW,
            IA_Height, (ULONG)gadH,
            TAG_DONE);
        DrawImageState(rp, (struct Image *)inst->bevel, 0L, 0L,
                       (ULONG)inst->cacheBm[state]._imageState, dri);
    }

    return 0;
}

/* =========================================================================
 * GM_DOMAIN
 * =========================================================================
 */
ULONG UniButtonBGBM_OnDomain(Class *cl, Object *o, struct gpDomain *msg)
{
    UniButtonBGBMData *inst   = UBGBM_DATA(cl, o);
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
