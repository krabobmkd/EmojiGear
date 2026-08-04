/*
 * UniButtonBGBM – GM_LAYOUT, GM_RENDER, GM_DOMAIN.
 *
 * Flat-colour mode (default, or UBGBM_Style unset/that state's image not
 * loaded): three OffscreenBitMaps cache the normal, selected, and disabled
 * states' TEXT (sized to the text bounding box), rebuilt in GM_RENDER
 * (application-task context – FreeType calls safe). The background is a
 * flat colour, drawn live with a plain RectFill, then the cached text is
 * blitted on top -- see ubgbm_blit_state in unibuttonbgbm_attribs.c.
 * GM_GOACTIVE / GM_HANDLEINPUT / GM_GOINACTIVE only blit the cached bitmap
 * (safe in input device context; they must never trigger a rebuild).
 *
 * Style-image mode (UBGBM_Style set and style->btbgbmBitmap[state]
 * loaded): no cache for that state -- ubgbm_build_one_state() skips it
 * entirely. Background (a full-opaque image, no transparency) and text
 * are both drawn live in ubgbm_blit_state, same reasoning as UniButtonP9's
 * Patch9 mode: the image isn't flat, so pre-baking text against an assumed
 * flat bgPen and blitting it on top would show as a visible mismatched box.
 *
 * See UniButtonP9 for the sibling class that always uses a Patch9 9-slice
 * skin and always draws everything live.
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
#include "../patch9.h"
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

    obm->_imageState = imageState;
    obm->_bgpen      = bgPen;

    /* Patch9 mode: unlike style-image mode below, this whole composited
     * state (background + text) CAN be cached, since Patch9_DrawOpaque
     * forces the skin fully opaque (no transparency mask to react to
     * whatever's really behind the button) -- see UBGBM_Patch9Mode's doc
     * comment. Cache is sized to the gadget itself (gadW/gadH), not the
     * text bounding box, since the background must cover the whole
     * button. */
    if (inst->patch9Mode) {
        Patch9 *p9 = inst->style ? &inst->style->patch9[FS3ESTYLE_PATCH9_BTBGBM] : NULL;

        w = gadW > 0 ? gadW : 1;
        h = gadH > 0 ? gadH : 1;

        if (!obm->_bm || obm->_w != w || obm->_h != h) {
            OffscreenBitMap_Close(obm);
            OffscreenBitMap_Init(obm, w, h, 0, BMF_CLEAR,
                                 scr ? scr->RastPort.BitMap : NULL);
        }
        if (!obm->_bm) return;
        rp = &obm->_srp;

        if (p9) {
            /* Per-state colours come from the Patch9 slot itself, same as
             * UniButtonP9's ubtp9_state_pens -- not the dynamic disabled-
             * dimming computed above, since the theme already authors a
             * distinct colour for every PATCH9_* state. This is
             * independent of whether the skin image itself loaded: p9's
             * bgcolors[]/textcolors[] are always populated from style.txt
             * regardless (see fs3estyle_load_patch9_slot's doc comment),
             * so a slot whose image is merely missing still needs its own
             * authored colour here, not the generic flat-mode bgPen/txtPen
             * computed above. Also update obm->_bgpen to match -- GM_RENDER's
             * fallback path reads obm->_bgpen whenever the cache is
             * momentarily invalid (e.g. mid-resize, before a same-task
             * rebuild has run), and it must show the Patch9 slot's own
             * colour there, not a leftover flat-mode one. */
            bgPen  = (ULONG)p9->bgcolors[state].pen;
            txtPen = (ULONG)p9->textcolors[state].pen;
            obm->_bgpen = bgPen;
        }

        if (p9 && Patch9_IsLoaded(p9)) {
            Patch9_DrawOpaque(p9, state, rp, 0, 0, w, h);
        } else {
            SetAPen(rp, (LONG)bgPen);
            SetDrMd(rp, JAM1);
            RectFill(rp, 0L, 0L, (LONG)(w - 1), (LONG)(h - 1));
        }

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
        return;
    }

    /* Style-image mode: no cache at all for this state -- both the
     * (non-flat) background image and text are drawn live in
     * ubgbm_blit_state. Drop any stale bitmap from a previous flat-colour
     * state (e.g. UBGBM_Style was just set, or that state's image just
     * finished loading). */
    if (inst->style && BmImage_IsLoaded(&inst->style->btbgbmBitmap[state])) {
        OffscreenBitMap_Close(obm);
        return;
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
    UniButtonBGBMData *inst = UBGBM_DATA(cl, o);
    ULONG rc = TRUE; // DoSuperMethodA(cl, o, (APTR)msg);

    /* changing size means recomputing the image caches */
    {
        struct Gadget *g = G(o);
        if (inst->cacheBm[UBGBM_STATE_NORMAL]._w != g->Width ||
            inst->cacheBm[UBGBM_STATE_NORMAL]._h != g->Height)
            inst->cacheValid = FALSE;
    }

    return rc;
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

    if(FindTask(NULL) != inst->callerTask)
    {
        ubgbm_notify_refresh_needed(inst, msg->gpr_GInfo);
        return TRUE;
    }

    if (scr) inst->screen   = scr;
    if (dri) inst->drawInfo = dri;

    if (scr && inst->dc)
        URPDC_SetDrawScreen(inst->dc, scr);

    if (inst->selBgPen == 3 && dri)
        inst->selBgPen = (ULONG)dri->dri_Pens[FILLPEN];



    /* Only rebuild from this object's own owning task: GM_RENDER can also
     * be reached from GM_GOACTIVE/GM_HANDLEINPUT/GM_GOINACTIVE's
     * ubgbm_render_self() (input.device context, not necessarily
     * callerTask) -- see the callerTask comment in unibuttonbgbm_
     * private.h. Rebuilding calls FreeType (via URPDrawContext) and can
     * race OM_SET's ubgbm_free_cache() on the real owning task (e.g. a
     * live theme change); off-task, just fall back to the flat-fill path
     * below, same as "cache not ready yet" -- a real app-task render
     * will rebuild it properly the next time this gadget is refreshed. */
    needRebuild = (!inst->cacheValid );

    if (needRebuild && scr) {
        ubgbm_rebuild_cache(cl, o, gadW, gadH, dri, scr);
    }

    if (g->Flags & GFLG_DISABLED)
        state = UBGBM_STATE_DISABLED;
    else if (g->Flags & GFLG_SELECTED)
        state = UBGBM_STATE_SELECTED;
    else
        state = UBGBM_STATE_NORMAL;

    if (inst->cacheValid &&
        (inst->cacheBm[state]._bm ||
         (inst->style && BmImage_IsLoaded(&inst->style->btbgbmBitmap[state])))) {
        ubgbm_blit_state(inst, g, rp, state);
    } else {
        // SetAPen(rp, (LONG)inst->cacheBm[state]._bgpen);
        // SetDrMd(rp, JAM1);
        // RectFill(rp, (LONG)g->LeftEdge, (LONG)g->TopEdge,
        //              (LONG)(g->LeftEdge + gadW - 1),
        //              (LONG)(g->TopEdge  + gadH - 1));
    }

    /* No bevel in Patch9 mode -- the button border lives in the Patch9
     * image itself (see UBGBM_Patch9Mode's doc comment). */
    if (inst->bevel && dri && !inst->patch9Mode) {
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
