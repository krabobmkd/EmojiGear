/* unibuttonp9_private.h – internal data structures and prototypes. */

#ifndef UNIBUTTONP9_PRIVATE_H
#define UNIBUTTONP9_PRIVATE_H

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

#include "unibuttonp9.h"
#include "../patch9.h"

#include <libraries/utf8rastport.h>
#include <proto/utf8rastport.h>

#include "../compilers.h"

/* Render states (bevel IDS_* selection, disabled-state dimming) */
#define UBTP9_STATE_NORMAL    0
#define UBTP9_STATE_SELECTED  1
#define UBTP9_STATE_DISABLED  2
#define UBTP9_NUM_STATES      3

typedef struct UniButtonP9Data {

    struct URPDrawContext *dc;
    ULONG  pointSize;
    ULONG  fontFlags;

    Object *bevel;
    ULONG   bevelStyle;
    WORD    bevelV;
    WORD    bevelH;

    BOOL    readOnly;

    ULONG   txtPen;
    ULONG   bgPen;
    ULONG   selBgPen;

    WORD    leftMargin;
    WORD    rightMargin;
    WORD    topMargin;
    WORD    bottomMargin;

    char   *text;

    /* Required (see UBTP9_Patch9); borrowed, not owned. Background and
     * text are always drawn live against it -- there is no bitmap cache
     * (see UniButtonBGBM for the class that has one). */
    Patch9 *patch9;

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

} UniButtonP9Data;

#define UBTP9_DATA(cl, o)  ((UniButtonP9Data *)INST_DATA((cl), (o)))
#define G(o)               ((struct Gadget *)(o))

/* =========================================================================
 * Internal helpers (defined in unibuttonp9_attribs.c)
 * =========================================================================
 */
void ubtp9_update_font_metrics(UniButtonP9Data *inst);
int  ubtp9_patch9_state(int state);
void ubtp9_state_pens(UniButtonP9Data *inst, int state,
                      ULONG *outBgPen, ULONG *outTxtPen, UWORD *outImageState);
void ubtp9_blit_state(UniButtonP9Data *inst, struct Gadget *g,
                      struct RastPort *rp, int state, UWORD *outImageState);
void ubtp9_notify_pressed(Class *cl, Object *o, struct GadgetInfo *gi);
void ubtp9_render_self(Class *cl, Object *o, struct GadgetInfo *gi);

/* =========================================================================
 * Method prototypes
 * =========================================================================
 */

/* unibuttonp9.c */
extern Class *UniButtonP9Class;

/* unibuttonp9_dispatch.c */
ULONG ASM SAVEDS UniButtonP9_Dispatch(
    REG(a0, Class *cl), REG(a2, Object *o), REG(a1, Msg msg));

/* unibuttonp9_attribs.c */
ULONG UniButtonP9_OnNew    (Class *cl, Object *o, struct opSet *msg);
ULONG UniButtonP9_OnDispose(Class *cl, Object *o, Msg msg);
ULONG UniButtonP9_OnSet    (Class *cl, Object *o, struct opSet *msg);
ULONG UniButtonP9_OnGet    (Class *cl, Object *o, struct opGet *msg);

/* unibuttonp9_render.c */
ULONG UniButtonP9_OnLayout (Class *cl, Object *o, struct gpLayout *msg);
ULONG UniButtonP9_OnRender (Class *cl, Object *o, struct gpRender *msg);
ULONG UniButtonP9_OnDomain (Class *cl, Object *o, struct gpDomain *msg);

/* unibuttonp9_input.c */
ULONG UniButtonP9_OnGoActive    (Class *cl, Object *o, struct gpInput *msg);
ULONG UniButtonP9_OnHandleInput (Class *cl, Object *o, struct gpInput *msg);
ULONG UniButtonP9_OnGoInactive  (Class *cl, Object *o, struct gpGoInactive *msg);

#endif /* UNIBUTTONP9_PRIVATE_H */
