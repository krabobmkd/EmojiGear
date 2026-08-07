/* unibuttonbgbm_private.h – internal data structures and prototypes. */

#ifndef UNIBUTTONBGBM_PRIVATE_H
#define UNIBUTTONBGBM_PRIVATE_H

#include <exec/types.h>
#include <exec/memory.h>
#include <graphics/rastport.h>
#include <graphics/gfx.h>
#include <intuition/screens.h>
#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <intuition/classusr.h>
#include <intuition/cghooks.h>
#include <intuition/icclass.h>
#include <string.h>

#include "unibuttonbgbm.h"
#include "offscreenbm.h"
#include "../fs3estyle.h"

#include <libraries/utf8rastport.h>
#include <proto/utf8rastport.h>

#include "../compilers.h"

/* Cached bitmap states. Only NORMAL/SELECTED are tracked -- GA_Disabled
 * still blocks input (GFLG_DISABLED, see GM_HITTEST in
 * unibuttonbgbm_dispatch.c) but no longer gets its own cached bitmap/
 * colour: that cache slot was pure chip RAM nobody actually looked at. */
#define UBGBM_STATE_NORMAL    0
#define UBGBM_STATE_SELECTED  1
#define UBGBM_NUM_STATES      2

typedef struct UniButtonBGBMData {

    /* Set once in OM_NEW. GM_RENDER can be reached from GM_GOACTIVE/
     * GM_HANDLEINPUT/GM_GOINACTIVE's ubgbm_render_self() calls, which run
     * in input.device context, not necessarily this task -- ubgbm_
     * rebuild_cache() calls FreeType (via URPDrawContext) and mutates
     * cacheBm[]/cacheValid, which OM_SET's ubgbm_free_cache() can also be
     * doing concurrently on this (the owning) task, e.g. a live theme
     * change. GM_RENDER only rebuilds when FindTask(NULL) == callerTask;
     * off-task it falls back to the flat-fill path instead, same as
     * "cache not ready yet" -- see UniButtonBGBM_OnRender. */
    struct Task *callerTask;

    struct URPDrawContext *dc;
    ULONG  pointSize;
    ULONG  fontFlags;

    Object *bevel;
    ULONG   bevelStyle;
    WORD    bevelV;
    WORD    bevelH;

    BOOL    transparent;
    BOOL    readOnly;

    ULONG   txtPen;
    ULONG   bgPen;
    ULONG   selBgPen;

    WORD    leftMargin;
    WORD    rightMargin;
    WORD    topMargin;
    WORD    bottomMargin;

    char   *text;

    /* Optional (see UBGBM_Style); borrowed, not owned. When set and a
     * state's style->btbgbmBitmap[] loaded, that state draws its
     * background image and text live instead of using cacheBm[]. */
    FS3EStyle *style;

    /* Crop offset into the style-image background source bitmap (see
     * UBGBM_BgShiftX/Y); clamped at draw time. */
    WORD    bgShiftX;
    WORD    bgShiftY;

    /* See UBGBM_Patch9Mode. */
    BOOL    patch9Mode;

    OffscreenBitMap cacheBm[UBGBM_NUM_STATES];
    BOOL            cacheValid;

    WORD    fontHeight;
    WORD    fontAscent;
    WORD    textWidth;
    WORD    textHeight;

    struct Screen   *screen;
    struct DrawInfo *drawInfo;

    BOOL    pushButton;
    BOOL    prevSelected;

    /* OS3.9 ICA_TARGET workaround: store target/id manually */
    Object *target;
    ULONG   ga_id;

} UniButtonBGBMData;

#define UBGBM_DATA(cl, o)  ((UniButtonBGBMData *)INST_DATA((cl), (o)))
#define G(o)               ((struct Gadget *)(o))

/* =========================================================================
 * Internal helpers (defined in unibuttonbgbm_attribs.c)
 * =========================================================================
 */
void ubgbm_update_font_metrics(UniButtonBGBMData *inst);
void ubgbm_free_cache(UniButtonBGBMData *inst);
void ubgbm_rebuild_cache(Class *cl, Object *o,
                         WORD gadW, WORD gadH,
                         struct DrawInfo *dri,
                         struct Screen   *scr);
void ubgbm_blit_state(UniButtonBGBMData *inst, struct Gadget *g,
                      struct RastPort *rp, int state);
void ubgbm_notify_pressed(Class *cl, Object *o, struct GadgetInfo *gi);
void ubgbm_notify_refresh_needed(UniButtonBGBMData *inst, struct GadgetInfo *gi);
void ubgbm_render_self(Class *cl, Object *o, struct GadgetInfo *gi);

/* =========================================================================
 * Method prototypes
 * =========================================================================
 */

/* unibuttonbgbm.c */
extern Class *UniButtonBGBMClass;

/* unibuttonbgbm_dispatch.c */
ULONG ASM SAVEDS UniButtonBGBM_Dispatch(
    REG(a0, Class *cl), REG(a2, Object *o), REG(a1, Msg msg));

/* unibuttonbgbm_attribs.c */
ULONG UniButtonBGBM_OnNew    (Class *cl, Object *o, struct opSet *msg);
ULONG UniButtonBGBM_OnDispose(Class *cl, Object *o, Msg msg);
ULONG UniButtonBGBM_OnSet    (Class *cl, Object *o, struct opSet *msg);
ULONG UniButtonBGBM_OnGet    (Class *cl, Object *o, struct opGet *msg);

/* unibuttonbgbm_render.c */
ULONG UniButtonBGBM_OnLayout (Class *cl, Object *o, struct gpLayout *msg);
ULONG UniButtonBGBM_OnRender (Class *cl, Object *o, struct gpRender *msg);
ULONG UniButtonBGBM_OnDomain (Class *cl, Object *o, struct gpDomain *msg);

/* unibuttonbgbm_input.c */
ULONG UniButtonBGBM_OnGoActive    (Class *cl, Object *o, struct gpInput *msg);
ULONG UniButtonBGBM_OnHandleInput (Class *cl, Object *o, struct gpInput *msg);
ULONG UniButtonBGBM_OnGoInactive  (Class *cl, Object *o, struct gpGoInactive *msg);

#endif /* UNIBUTTONBGBM_PRIVATE_H */
