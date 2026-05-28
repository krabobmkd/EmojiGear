/*
 * unibutton_private.h – internal data structures and prototypes.
 * Do not include outside UniButtonGadget/.
 *
 * BevelBase is declared by proto/bevel.h as extern; in the static-link
 * build it is defined and opened/closed by UniTextEditor's lib module.
 * A future dynamic (.gadget) build will need its own definition here.
 */

#ifndef UNIBUTTON_PRIVATE_H
#define UNIBUTTON_PRIVATE_H

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

#include <gadgets/unibutton.h>
#include "offscreenbm.h"

#ifdef STATIC_UTF8RASTPORT
    #include "utf8rastport.h"
#else
    #include <libraries/utf8rastport.h>
    #include <proto/utf8rastport.h>
#endif
#include "compilers.h"

/* Cached bitmap states */
#define UBT_STATE_NORMAL    0   /* unselected, enabled */
#define UBT_STATE_SELECTED  1   /* pressed/active */
#define UBT_STATE_DISABLED  2   /* GA_Disabled */
#define UBT_NUM_STATES      3

/* =========================================================================
 * UniButtonData – per-instance data (INST_DATA block)
 * =========================================================================
 */
typedef struct UniButtonData {

    /* Draw context (owned or shared) */
    struct URPDrawContext *dc;
    ULONG  pointSize;
    ULONG  fontFlags;

    /* Bevel image object (NULL when BVS_NONE) */
    Object *bevel;
    ULONG   bevelStyle;
    WORD    bevelV;     /* BEVEL_VertSize  – left/right frame px */
    WORD    bevelH;     /* BEVEL_HorizSize – top/bottom frame px */

    /* Appearance */
    BOOL    transparent;
    BOOL    readOnly;

    /* Pens */
    ULONG   txtPen;
    ULONG   bgPen;
    ULONG   selBgPen;

    /* Inner margins (user value + bevel thickness stored together) */
    WORD    leftMargin;
    WORD    rightMargin;
    WORD    topMargin;
    WORD    bottomMargin;

    /* Label – AllocVec'd, NUL-terminated UTF-8; NULL = no label */
    char   *text;

    /* Bitmap cache – rebuilt in GM_RENDER when cacheDirty or size changes */
    OffscreenBitMap cacheBm[UBT_NUM_STATES];
    BOOL            cacheValid;

    /* Font metrics – kept current by OM_SET; read by GM_DOMAIN without FreeType */
    WORD    fontHeight;
    WORD    fontAscent;
    WORD    textWidth;   /* advance width of inst->text at current font/size */
    WORD    textHeight;   /* advance width of inst->text at current font/size */

    /* Screen reference and cached DrawInfo (from gi_DrInfo in GM_RENDER) */
    struct Screen   *screen;
    struct DrawInfo *drawInfo;

    /* Push/latch toggle mode (UBT_PushButton) */
    BOOL    pushButton;
    BOOL    prevSelected;   /* GFLG_SELECTED state captured at GM_GOACTIVE */

    /* OS3.9 intuition can't manage OM_NOTIFY with DoSuperMethod... */
    Object *target;
    ULONG  ga_id;

} UniButtonData;

#define UBT_DATA(cl, o)  ((UniButtonData *)INST_DATA((cl), (o)))
#define G(o)             ((struct Gadget *)(o))

/* =========================================================================
 * Internal helpers
 * =========================================================================
 */

/* Update fontHeight / fontAscent from the draw context (safe to call any time) */
void ubt_update_font_metrics(UniButtonData *inst);

/* Free all cached bitmaps and mark cache invalid */
void ubt_free_cache(UniButtonData *inst);

/* Rebuild all three state bitmaps (application-task context only) */
void ubt_rebuild_cache(Class *cl, Object *o,
                       WORD gadW, WORD gadH,
                       struct DrawInfo *dri,
                       struct Screen   *scr);

/* Blit state bitmap to rastport at gadget position; no-op if cache invalid */
void ubt_blit_state(UniButtonData *inst, struct Gadget *g,
                    struct RastPort *rp, int state);

/* DoSuperMethod OM_NOTIFY: button pressed, carry GA_ID */
void ubt_notify_pressed(Class *cl, Object *o, struct GadgetInfo *gi);

/* Trigger GM_RENDER on self via ObtainGIRPort */
void ubt_render_self(Class *cl, Object *o, struct GadgetInfo *gi);

/* =========================================================================
 * Prototypes
 * =========================================================================
 */
/* class_unibutton_lib.c */
extern Class *UniButtonClass;

/* class_unibutton_dispatch.c */
ULONG ASM SAVEDS UniButton_Dispatch(
    REG(a0, Class *cl), REG(a2, Object *o), REG(a1, Msg msg));

/* class_unibutton_attribs.c */
ULONG UniButton_OnNew    (Class *cl, Object *o, struct opSet *msg);
ULONG UniButton_OnDispose(Class *cl, Object *o, Msg msg);
ULONG UniButton_OnSet    (Class *cl, Object *o, struct opSet *msg);
ULONG UniButton_OnGet    (Class *cl, Object *o, struct opGet *msg);

/* class_unibutton_render.c */
ULONG UniButton_OnLayout (Class *cl, Object *o, struct gpLayout *msg);
ULONG UniButton_OnRender (Class *cl, Object *o, struct gpRender *msg);
ULONG UniButton_OnDomain (Class *cl, Object *o, struct gpDomain *msg);

/* class_unibutton_input.c */
ULONG UniButton_OnGoActive    (Class *cl, Object *o, struct gpInput *msg);
ULONG UniButton_OnHandleInput (Class *cl, Object *o, struct gpInput *msg);
ULONG UniButton_OnGoInactive  (Class *cl, Object *o, struct gpGoInactive *msg);

#endif /* UNIBUTTON_PRIVATE_H */
