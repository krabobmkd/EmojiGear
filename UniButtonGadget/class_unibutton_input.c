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

/* =========================================================================
 * GM_GOACTIVE  (left mouse button pressed over the gadget)
 * =========================================================================
 */
ULONG UniButton_OnGoActive(Class *cl, Object *o, struct gpInput *msg)
{
    UniButtonData  *inst = UBT_DATA(cl, o);
    struct RastPort *rp;

    /* Only activate on a real user click, not a synthetic activation */
    if (!msg->gpi_IEvent) return GMR_NOREUSE;

    G(o)->Flags |= GFLG_SELECTED;

    rp = ObtainGIRPort(msg->gpi_GInfo);
    if (rp) {
        ubt_blit_state(inst, G(o), rp, UBT_STATE_SELECTED);
        ReleaseGIRPort(rp);
    }

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
    struct RastPort   *rp;
    BOOL               over;

    if (!ie) return GMR_MEACTIVE;

    if (ie->ie_Class == IECLASS_RAWMOUSE) {

        if (ie->ie_Code == SELECTUP) {
            /* Button released */
            over = mouse_over_gadget(G(o), msg);

            G(o)->Flags &= ~GFLG_SELECTED;

            rp = ObtainGIRPort(msg->gpi_GInfo);
            if (rp) {
                ubt_blit_state(inst, G(o), rp, UBT_STATE_NORMAL);
                ReleaseGIRPort(rp);
            }

            if (over) {
                /* Clicked: notify ICA_TARGET and ask Intuition for GADGETUP */
                ubt_notify_pressed(cl, o, msg->gpi_GInfo);
                *msg->gpi_Termination = 0;
                return GMR_NOREUSE | GMR_VERIFY;
            }
            return GMR_NOREUSE;
        }

        if (ie->ie_Code == MENUDOWN) {
            /* Right/menu button: go inactive and let Intuition reuse the event */
            G(o)->Flags &= ~GFLG_SELECTED;
            rp = ObtainGIRPort(msg->gpi_GInfo);
            if (rp) {
                ubt_blit_state(inst, G(o), rp, UBT_STATE_NORMAL);
                ReleaseGIRPort(rp);
            }
            return GMR_REUSE;
        }

        /* Mouse moved while held: track selected/deselected visual */
        over = mouse_over_gadget(G(o), msg);
        {
            BOOL wasSelected = (BOOL)((G(o)->Flags & GFLG_SELECTED) != 0);
            if (over && !wasSelected) {
                G(o)->Flags |= GFLG_SELECTED;
                rp = ObtainGIRPort(msg->gpi_GInfo);
                if (rp) {
                    ubt_blit_state(inst, G(o), rp, UBT_STATE_SELECTED);
                    ReleaseGIRPort(rp);
                }
            } else if (!over && wasSelected) {
                G(o)->Flags &= ~GFLG_SELECTED;
                rp = ObtainGIRPort(msg->gpi_GInfo);
                if (rp) {
                    ubt_blit_state(inst, G(o), rp, UBT_STATE_NORMAL);
                    ReleaseGIRPort(rp);
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

    G(o)->Flags &= ~GFLG_SELECTED;

    rp = ObtainGIRPort(msg->gpgi_GInfo);
    if (rp) {
        int state = (G(o)->Flags & GFLG_DISABLED)
                    ? UBT_STATE_DISABLED : UBT_STATE_NORMAL;
        ubt_blit_state(inst, G(o), rp, state);
        ReleaseGIRPort(rp);
    }

    return 0;
}
