/*
 * UniButtonP9 – GM_LAYOUT, GM_RENDER, GM_DOMAIN.
 *
 * Three OffscreenBitMaps cache the normal, selected, and disabled states,
 * each the full button surface (background + border-free skin + text).
 * Rebuilt in GM_LAYOUT and GM_RENDER (application-task context – FreeType
 * calls safe). g->Width/Height only ever change in GM_LAYOUT, so a resize
 * is caught and rebuilt there, proactively, ahead of any later blit.
 * GM_GOACTIVE / GM_HANDLEINPUT / GM_GOINACTIVE only blit the cached bitmap
 * (safe in input device context; they must never trigger a rebuild).
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
 * ubtp9_build_one_state
 * =========================================================================
 */
static void ubtp9_build_one_state(UniButtonP9Data *inst, WORD gadW, WORD gadH,
                                  int state, struct DrawInfo *dri,
                                  struct Screen *scr)
{
    OffscreenBitMap *obm = &inst->cacheBm[state];
    struct RastPort *rp;
    ULONG            bgPen;
    ULONG            txtPen;
    UWORD            imageState;
    int w, h;

    if (state == UBTP9_STATE_DISABLED && scr && scr->ViewPort.ColorMap) {
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
        case UBTP9_STATE_SELECTED:
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

    w = gadW;
    h = gadH;
    if (w < 1) w = 1;
    if (h < 1) h = 1;

    if (!obm->_bm || obm->_w != w || obm->_h != h) {
        OffscreenBitMap_Close(obm);
        OffscreenBitMap_Init(obm, w, h, 0, BMF_CLEAR,
                             scr ? scr->RastPort.BitMap : NULL);
    }
    if (!obm->_bm) return;
    rp = &obm->_srp;

    if (!inst->transparent || state != UBTP9_STATE_NORMAL) {
        SetAPen(rp, (LONG)bgPen);
        SetDrMd(rp, JAM1);
        RectFill(rp, 0L, 0L, (LONG)(w - 1), (LONG)(h - 1));
    }
    obm->_imageState = imageState;
    obm->_bgpen      = bgPen;

    if (inst->text && inst->text[0] && inst->dc && scr) {
        struct URPTextPos pos;
        pos.x = (w - inst->textWidth)  / 2;
        pos.y = (h - inst->textHeight) / 2 + inst->fontAscent;

        URPDC_SetDrawColorFromPen(inst->dc, scr, (LONG)txtPen, (LONG)bgPen);
        SetAPen(rp, (LONG)txtPen);
        SetBPen(rp, (LONG)bgPen);
        SetDrMd(rp, JAM2);
        URPDrawTextUTF8(rp, inst->dc, &pos, inst->text, (ULONG)(-1));
    }
}

/* =========================================================================
 * ubtp9_rebuild_cache
 * =========================================================================
 */
void ubtp9_rebuild_cache(Class *cl, Object *o,
                         WORD gadW, WORD gadH,
                         struct DrawInfo *dri,
                         struct Screen   *scr)
{
    UniButtonP9Data *inst = UBTP9_DATA(cl, o);
    int i;

    ubtp9_update_font_metrics(inst);

    for (i = 0; i < UBTP9_NUM_STATES; i++)
        ubtp9_build_one_state(inst, gadW, gadH, i, dri, scr);

    inst->cacheValid = TRUE;
}

/* =========================================================================
 * GM_LAYOUT
 * =========================================================================
 */
ULONG UniButtonP9_OnLayout(Class *cl, Object *o, struct gpLayout *msg)
{
    UniButtonP9Data *inst = UBTP9_DATA(cl, o);
    ULONG            ret  = DoSuperMethodA(cl, o, (APTR)msg);
    struct Gadget   *g    = G(o);
    WORD             gadW = g->Width;
    WORD             gadH = g->Height;
    struct Screen   *scr  = msg->gpl_GInfo ? msg->gpl_GInfo->gi_Screen : inst->screen;
    struct DrawInfo *dri  = msg->gpl_GInfo ? msg->gpl_GInfo->gi_DrInfo : inst->drawInfo;

    if (scr) inst->screen   = scr;
    if (dri) inst->drawInfo = dri;

    /* g->Width/Height only ever change here, in GM_LAYOUT (always
     * application-task context) — never from GM_GOACTIVE/GM_HANDLEINPUT
     * (input.device context, where FreeType calls are unsafe). Since the
     * cache is now the full button surface, its size (and the text
     * centering baked into it) depends on the gadget size, so a resize
     * must rebuild it here, proactively, rather than leaving GM_RENDER's
     * lazy rebuild to possibly fire from an input.device-context blit. */
    if (gadW > 0 && gadH > 0 && scr &&
        (!inst->cacheValid ||
         inst->cacheBm[UBTP9_STATE_NORMAL]._w != gadW ||
         inst->cacheBm[UBTP9_STATE_NORMAL]._h != gadH))
        ubtp9_rebuild_cache(cl, o, gadW, gadH, dri, scr);

    return ret;
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
    BOOL             needRebuild;
    int              state;

    if (gadW <= 0 || gadH <= 0) return 0;

    if (scr) inst->screen   = scr;
    if (dri) inst->drawInfo = dri;

    if (scr && inst->dc)
        URPDC_SetDrawScreen(inst->dc, scr);

    if (inst->selBgPen == 3 && dri)
        inst->selBgPen = (ULONG)dri->dri_Pens[FILLPEN];

    needRebuild = (!inst->cacheValid) ||
                  (inst->cacheBm[UBTP9_STATE_NORMAL]._w != gadW) ||
                  (inst->cacheBm[UBTP9_STATE_NORMAL]._h != gadH);

    if (needRebuild && scr)
        ubtp9_rebuild_cache(cl, o, gadW, gadH, dri, scr);

    if (g->Flags & GFLG_DISABLED)
        state = UBTP9_STATE_DISABLED;
    else if (g->Flags & GFLG_SELECTED)
        state = UBTP9_STATE_SELECTED;
    else
        state = UBTP9_STATE_NORMAL;

    if (inst->cacheValid && inst->cacheBm[state]._bm) {
        ubtp9_blit_state(inst, g, rp, state);
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
