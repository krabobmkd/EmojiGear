/*
 * TootTimeline – GM_HITTEST, GM_GOACTIVE, GM_HANDLEINPUT, GM_GOINACTIVE.
 *
 * Scroll interaction
 * ------------------
 * Button press   → GoActive: record dragStartGadY and dragStartScrollY.
 * Mouse move     → HandleInput: compute dy, set pendingScrollY, call
 *                  ttl_render_self() to blit at the new position.
 * Button release → HandleInput: end drag, return GMR_NOREUSE.
 *
 * Click detection (future)
 * -------------------------
 * On a click without drag, hit-test against post hot-spots and text
 * spans.  For now only the scroll path is implemented.
 *
 * Scroll-wheel (future)
 * ----------------------
 * IECODE_UP_PREFIX / IECODE_DOWN_PREFIX from IECLASS_RAWMOUSE wheel
 * events are handled here when the gadget is active.
 */

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/alib.h>
#include <intuition/gadgetclass.h>
#include <devices/inputevent.h>

#include "fs3etoottimeline_private.h"

/* ------------------------------------------------------------------ */
/* Scroll helper: clamp and store pending scroll                        */
/* ------------------------------------------------------------------ */

static void ttl_set_scroll(TTLData *inst, LONG newScrollY)
{
    LONG maxScroll = inst->contentBottomY - inst->gadHeight;
    if (maxScroll < inst->contentTopY) maxScroll = inst->contentTopY;
    if (newScrollY < inst->contentTopY) newScrollY = inst->contentTopY;
    if (newScrollY > maxScroll)         newScrollY = maxScroll;
    inst->pendingScroll  = TRUE;
    inst->pendingScrollY = newScrollY;
}

/* ------------------------------------------------------------------ */
/* Hit-test helpers (future click / text selection)                    */
/* ------------------------------------------------------------------ */

/* Find the post whose Y range contains timelineY, or NULL. */
static TTLPost *ttl_hit_post(TTLData *inst, LONG timelineY)
{
    TTLPost *post;
    for (post = (TTLPost *)inst->posts.mlh_Head;
         post->node.mln_Succ;
         post = (TTLPost *)post->node.mln_Succ)
    {
        if (timelineY >= post->timelineY &&
            timelineY <  post->timelineY + post->height)
            return post;
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* GM_HITTEST                                                           */
/*                                                                      */
/* Bottom-right TTL_RESIZE_HANDLE × TTL_RESIZE_HANDLE corner → prime  */
/* the resize globals and return 0 so the gadget is NOT activated and  */
/* WMHI_MOUSEMOVE events flow to the main loop unobstructed.           */
/* Any other position → GMR_GADGETHIT for normal scroll activation.    */
/* ------------------------------------------------------------------ */

ULONG TTL_OnHitTest(Class *cl, Object *o, struct gpHitTest *msg)
{
    struct GadgetInfo *gi  = msg->gpht_GInfo;
    WORD               relX = msg->gpht_Mouse.X;
    WORD               relY = msg->gpht_Mouse.Y;
    WORD               gadW = G(o)->Width;
    WORD               gadH = G(o)->Height;

    (void)cl;

    if (relX >= gadW - TTL_RESIZE_HANDLE &&
        relY >= gadH - TTL_RESIZE_HANDLE &&
        gi && gi->gi_Window && gi->gi_Screen)
    {
        windowResizeActive      = TRUE;
        windowResizeStartSX     = gi->gi_Screen->MouseX;
        windowResizeStartSY     = gi->gi_Screen->MouseY;
        windowResizeStartW      = gi->gi_Window->Width;
        windowResizeStartH      = gi->gi_Window->Height;
        windowResizeLastTargetW = gi->gi_Window->Width;
        windowResizeLastTargetH = gi->gi_Window->Height;
        return 0;   /* don't activate — let IDCMP see the mouse events */
    }

    return GMR_GADGETHIT;
}

/* ------------------------------------------------------------------ */
/* GM_GOACTIVE                                                          */
/* ------------------------------------------------------------------ */

ULONG TTL_OnGoActive(Class *cl, Object *o, struct gpInput *msg)
{
    TTLData *inst = TTL_DATA(cl, o);

    *msg->gpi_Termination = 0;

    if (msg->gpi_IEvent &&
        msg->gpi_IEvent->ie_Class == IECLASS_RAWMOUSE &&
        (msg->gpi_IEvent->ie_Code & ~IECODE_UP_PREFIX) == IECODE_LBUTTON)
    {
        inst->dragActive      = TRUE;
        inst->dragStartGadY   = msg->gpi_Mouse.Y;
        inst->dragStartScrollY = inst->scrollY;
        return GMR_MEACTIVE;
    }

    return GMR_NOREUSE;
}

/* ------------------------------------------------------------------ */
/* GM_HANDLEINPUT                                                       */
/* ------------------------------------------------------------------ */

ULONG TTL_OnHandleInput(Class *cl, Object *o, struct gpInput *msg)
{
    TTLData          *inst  = TTL_DATA(cl, o);
    struct InputEvent *ie   = msg->gpi_IEvent;

    *msg->gpi_Termination = 0;

    if (!ie) return GMR_MEACTIVE;

    if (ie->ie_Class == IECLASS_RAWMOUSE) {
        UWORD code = ie->ie_Code & ~IECODE_UP_PREFIX;

        if (code == IECODE_LBUTTON && (ie->ie_Code & IECODE_UP_PREFIX)) {
            /* Button released – end drag.  If displacement was tiny it's a click. */
            WORD dy = (WORD)(msg->gpi_Mouse.Y - inst->dragStartGadY);
            if (dy < 0) dy = -dy;

            inst->dragActive = FALSE;

            if (dy < 4) {
                /* Click: hit-test for hot-spot */
                LONG timelineY = inst->scrollY + msg->gpi_Mouse.Y;
                TTLPost *post = ttl_hit_post(inst, timelineY);
                if (post) {
                    /* Check hot-spots */
                    TTLHotSpot *hs;
                    WORD        relY = (WORD)(timelineY - post->timelineY);
                    WORD        relX = msg->gpi_Mouse.X;
                    for (hs = (TTLHotSpot *)post->hotSpots.mlh_Head;
                         hs->node.mln_Succ;
                         hs = (TTLHotSpot *)hs->node.mln_Succ)
                    {
                        if (relX >= hs->x && relX < hs->x + hs->w &&
                            relY >= hs->y && relY < hs->y + hs->h)
                        {
                            ttl_notify(cl, o, msg->gpi_GInfo,
                                       TTIMELINE_HotSpotActivated, (ULONG)hs);
                            break;
                        }
                    }
                }
            }
            return GMR_NOREUSE;
        }

        if (inst->dragActive && ie->ie_Code == IECODE_NOBUTTON) {
            /* Mouse move while dragging: scroll */
            WORD dy = (WORD)(msg->gpi_Mouse.Y - inst->dragStartGadY);
            ttl_set_scroll(inst, inst->dragStartScrollY - dy);

            ttl_notify(cl,o,msg->gpi_GInfo, TTIMELINE_ProcessRefresh,TRUE);
            //ttl_render_self(cl, o, msg->gpi_GInfo);
            return GMR_MEACTIVE;
        }
    }

    return GMR_MEACTIVE;
}

/* ------------------------------------------------------------------ */
/* GM_GOINACTIVE                                                        */
/* ------------------------------------------------------------------ */

ULONG TTL_OnGoInactive(Class *cl, Object *o, struct gpGoInactive *msg)
{
    TTLData *inst = TTL_DATA(cl, o);
    (void)msg;
    inst->dragActive = FALSE;
    return 0;
}
