/*
 * UniButton gadget – mouse input handling.
 *
 * GM_GOACTIVE / GM_HANDLEINPUT / GM_GOINACTIVE
 *
 * These methods run in the input.device context.  The only operations
 * allowed are:
 *   - Reading/writing instance data fields.
 *   - BltBitMapRastPort on the pre-built cached bitmaps.
 *   - ObtainGIRPort / ReleaseGIRPort.
 *   - DoSuperMethod for OM_NOTIFY.
 *
 * NO FreeType calls, NO AllocVec, NO disk I/O.
 */
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/alib.h>
#include <devices/inputevent.h>
#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include "unibutton_private.h"

/* Check whether gpi_Mouse coords are inside the gadget boundaries */
static BOOL mouse_over_gadget(struct Gadget *g, struct gpInput *gpi)
{
    WORD mx = gpi->gpi_Mouse.X;
    WORD my = gpi->gpi_Mouse.Y;
    /* gpi_Mouse is relative to the top-left of the gadget */
    return (BOOL)(mx >= 0 && mx < g->Width &&
                  my >= 0 && my < g->Height);
}
ULONG UniButton_OnRender(Class *cl, Object *o, struct gpRender *msg);
static void render(Class *cl, Object *o,struct GadgetInfo *gi)
{
    struct RastPort *rp;
    if(!gi) return;
    rp = ObtainGIRPort(gi);
    if (rp) {
         struct gpRender msg;
        msg.MethodID = GM_RENDER;
        msg.gpr_GInfo = gi;
        msg.gpr_RPort = rp;
        UniButton_OnRender(cl,o,&msg);
        ReleaseGIRPort(rp);
    }


}

/* =========================================================================
 * GM_GOACTIVE  (left mouse button pressed over the gadget)
 * =========================================================================
 */
ULONG UniButton_OnGoActive(Class *cl, Object *o, struct gpInput *msg)
{
    UniButtonData  *inst = UBT_DATA(cl, o);
    struct RastPort *rp;

    /* Synthetic activation (ActivateGadget() or TAB cycling, ie=NULL):
     * only accept it when GA_TabCycle is set, so the gadget can be tabbed
     * into; otherwise refuse, as before. No visual change here - GFLG_SELECTED
     * is the "pressed" look, not a focus ring, so it stays untouched. */
    if (!msg->gpi_IEvent) return inst->tabCycle ? GMR_MEACTIVE : GMR_NOREUSE;

    /* For push buttons, remember current latch state before showing feedback */
    if (inst->pushButton)
        inst->prevSelected = (BOOL)((G(o)->Flags & GFLG_SELECTED) != 0);

    G(o)->Flags |= GFLG_SELECTED;

    render(cl,o,msg->gpi_GInfo);

    return GMR_MEACTIVE;
}

/* =========================================================================
 * GM_HANDLEINPUT  (mouse moves / button release while active)
 * =========================================================================
 */
ULONG UniButton_OnHandleInput(Class *cl, Object *o, struct gpInput *msg)
{
    UniButtonData  *inst = UBT_DATA(cl, o);
    struct InputEvent *ie = msg->gpi_IEvent;
    BOOL               over;

    if (!ie) return GMR_MEACTIVE;

    if (inst->tabCycle && ie->ie_Class == IECLASS_RAWKEY &&
        ie->ie_Code == UBT_RAWKEY_TAB) {
        /* Tab / Shift+Tab: release activation to the next/prev
         * GFLG_TABCYCLE gadget instead of consuming the key silently. */
        BOOL shift = (ie->ie_Qualifier &
                      (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) ? TRUE : FALSE;
        return GMR_NOREUSE | (shift ? GMR_PREVACTIVE : GMR_NEXTACTIVE);
    }

    if (ie->ie_Class == IECLASS_RAWMOUSE) {

        if (ie->ie_Code == SELECTUP) {
            over = mouse_over_gadget(G(o), msg);

            if (inst->pushButton) {
                if (over) {
                    /* Toggle: keep selected iff was unselected before the click */
                    if (inst->prevSelected)
                        G(o)->Flags &= ~GFLG_SELECTED;
                    /* else GFLG_SELECTED stays set from GM_GOACTIVE */
                } else {
                    /* Released outside: restore pre-click state */
                    if (!inst->prevSelected)
                        G(o)->Flags &= ~GFLG_SELECTED;
                }

                render(cl,o,msg->gpi_GInfo);

                if (over) {
                    ubt_notify_pressed(cl, o, msg->gpi_GInfo);
                    *msg->gpi_Termination = 0;
                    return GMR_NOREUSE | GMR_VERIFY;
                }
                return GMR_NOREUSE;
            }

            /* Normal momentary button */
            G(o)->Flags &= ~GFLG_SELECTED;

            render(cl,o,msg->gpi_GInfo);

            if (over) {
                ubt_notify_pressed(cl, o, msg->gpi_GInfo);
                *msg->gpi_Termination = 0;
                return GMR_NOREUSE | GMR_VERIFY;
            }
            return GMR_NOREUSE;
        }

        if (ie->ie_Code == MENUDOWN) {
            /* Right/menu button: cancel and reuse event */
            if (inst->pushButton) {
                /* Restore pre-click state */
                if (!inst->prevSelected)
                    G(o)->Flags &= ~GFLG_SELECTED;
            } else {
                G(o)->Flags &= ~GFLG_SELECTED;
            }
            render(cl,o,msg->gpi_GInfo);
            return GMR_REUSE;
        }

        /* Mouse moved while held – only track hover visual for normal buttons */
        if (!inst->pushButton) {
            over = mouse_over_gadget(G(o), msg);
            {
                BOOL wasSelected = (BOOL)((G(o)->Flags & GFLG_SELECTED) != 0);
                if (over && !wasSelected) {
                    G(o)->Flags |= GFLG_SELECTED;
                    render(cl,o,msg->gpi_GInfo);
                } else if (!over && wasSelected) {
                    G(o)->Flags &= ~GFLG_SELECTED;
                    render(cl,o,msg->gpi_GInfo);
                }
            }
        }
    }

    return GMR_MEACTIVE;
}

/* =========================================================================
 * GM_GOINACTIVE  (Intuition deactivated the gadget)
 * =========================================================================
 */
ULONG UniButton_OnGoInactive(Class *cl, Object *o, struct gpGoInactive *msg)
{
    UniButtonData  *inst = UBT_DATA(cl, o);
    struct RastPort *rp;
    int              state;

    /* Push buttons keep their latched GFLG_SELECTED; normal buttons always clear it */
    if (!inst->pushButton)
        G(o)->Flags &= ~GFLG_SELECTED;

    render(cl,o,msg->gpgi_GInfo);

    return 0;
}
