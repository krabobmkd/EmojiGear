/*
 * UniButtonP9 – mouse input handling.
 * GM_GOACTIVE / GM_HANDLEINPUT / GM_GOINACTIVE
 *
 * These run in the input.device context: only BltBitMapRastPort and
 * ObtainGIRPort/ReleaseGIRPort are safe here.  No FreeType calls.
 */
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/alib.h>
#include <devices/inputevent.h>
#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include "unibuttonp9_private.h"

#include "../bdbprintf.h"
static BOOL mouse_over(struct Gadget *g, struct gpInput *gpi)
{
    WORD mx = gpi->gpi_Mouse.X;
    WORD my = gpi->gpi_Mouse.Y;
    return (BOOL)(mx >= 0 && mx < g->Width &&
                  my >= 0 && my < g->Height);
}

ULONG UniButtonP9_OnRender(Class *cl, Object *o, struct gpRender *msg);

static void render(Class *cl, Object *o, struct GadgetInfo *gi)
{
    struct RastPort *rp;
    if (!gi) return;
    rp = ObtainGIRPort(gi);
    if (rp) {
        struct gpRender msg;
        msg.MethodID  = GM_RENDER;
        msg.gpr_GInfo = gi;
        msg.gpr_RPort = rp;
        UniButtonP9_OnRender(cl, o, &msg);
        ReleaseGIRPort(rp);
    }
}

/* =========================================================================
 * GM_GOACTIVE
 * =========================================================================
 */
ULONG UniButtonP9_OnGoActive(Class *cl, Object *o, struct gpInput *msg)
{
    UniButtonP9Data *inst = UBTP9_DATA(cl, o);

    if (!msg->gpi_IEvent) return GMR_NOREUSE;

//bdbprintf("p9ongoac:inst->pushButton %d\n",inst->pushButton);
    if (inst->pushButton)
        inst->prevSelected = (BOOL)((G(o)->Flags & GFLG_SELECTED) != 0);

    G(o)->Flags |= GFLG_SELECTED;
    render(cl, o, msg->gpi_GInfo);
    return GMR_MEACTIVE;
}

/* =========================================================================
 * GM_HANDLEINPUT
 * =========================================================================
 */
ULONG UniButtonP9_OnHandleInput(Class *cl, Object *o, struct gpInput *msg)
{
    UniButtonP9Data    *inst = UBTP9_DATA(cl, o);
    struct InputEvent  *ie   = msg->gpi_IEvent;
    BOOL                over;

    if (!ie) return GMR_MEACTIVE;

    if (ie->ie_Class == IECLASS_RAWMOUSE) {

        if (ie->ie_Code == SELECTUP) {
            over = mouse_over(G(o), msg);

            if (inst->pushButton) {
                if (over) {
                    if (inst->prevSelected)
                        G(o)->Flags &= ~GFLG_SELECTED;
                } else {
                    if (!inst->prevSelected)
                        G(o)->Flags &= ~GFLG_SELECTED;
                }
                render(cl, o, msg->gpi_GInfo);
                if (over) {
                    ubtp9_notify_pressed(cl, o, msg->gpi_GInfo);
                    *msg->gpi_Termination = 0;
                    return GMR_NOREUSE | GMR_VERIFY;
                }
                return GMR_NOREUSE;
            }

            G(o)->Flags &= ~GFLG_SELECTED;
            render(cl, o, msg->gpi_GInfo);

            if (over) {
             /*no, because auto managed by intuition WMHI_GadgetUp
              *    ubtp9_notify_pressed(cl, o, msg->gpi_GInfo);
              *    */
                *msg->gpi_Termination = 0;
                return GMR_NOREUSE | GMR_VERIFY;
            }
            return GMR_NOREUSE;
        }

        if (ie->ie_Code == MENUDOWN) {
            if (inst->pushButton) {
                if (!inst->prevSelected)
                    G(o)->Flags &= ~GFLG_SELECTED;
            } else {
                G(o)->Flags &= ~GFLG_SELECTED;
            }
            render(cl, o, msg->gpi_GInfo);
            return GMR_REUSE;
        }

        if (!inst->pushButton) {
            over = mouse_over(G(o), msg);
            {
                BOOL wasSelected = (BOOL)((G(o)->Flags & GFLG_SELECTED) != 0);
                if (over && !wasSelected) {
                    G(o)->Flags |= GFLG_SELECTED;
                    render(cl, o, msg->gpi_GInfo);
                } else if (!over && wasSelected) {
                    G(o)->Flags &= ~GFLG_SELECTED;
                    render(cl, o, msg->gpi_GInfo);
                }
            }
        }
    }

    return GMR_MEACTIVE;
}

/* =========================================================================
 * GM_GOINACTIVE
 * =========================================================================
 */
ULONG UniButtonP9_OnGoInactive(Class *cl, Object *o, struct gpGoInactive *msg)
{
    UniButtonP9Data *inst = UBTP9_DATA(cl, o);

    if (!inst->pushButton)
        G(o)->Flags &= ~GFLG_SELECTED;

    render(cl, o, msg->gpgi_GInfo);
    return 0;
}
