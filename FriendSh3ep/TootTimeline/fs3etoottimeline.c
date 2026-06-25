/*
 * TootTimeline – private BOOPSI gadget: Mastodon timeline scroll area.
 *
 * Inherits from gadgetclass.  Content is rendered into pre-allocated
 * bitmap tile strips (gadWidth × TTL_TILE_HEIGHT) that are blitted into
 * the gadget RastPort via BltBitMapRastPort.  New posts prepended at
 * the top grow the scroll domain upward without moving the current view.
 *
 * See fs3etoottimeline_private.h for data-structure documentation.
 */

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/alib.h>
#include <intuition/gadgetclass.h>

#include "fs3etoottimeline_private.h"

/* ------------------------------------------------------------------ */
/* Dispatcher                                                           */
/* ------------------------------------------------------------------ */

ULONG ASM SAVEDS TootTimeline_Dispatch(
    REG(a0, Class  *cl),
    REG(a2, Object *o),
    REG(a1, Msg     msg))
{
    switch (msg->MethodID) {
        case OM_NEW:
            return TTL_OnNew    (cl, o, (struct opSet *)msg);
        case OM_DISPOSE:
            return TTL_OnDispose(cl, o, msg);
        case OM_SET:
        case OM_UPDATE:
            return TTL_OnSet    (cl, o, (struct opSet *)msg);
        case OM_GET:
            return TTL_OnGet    (cl, o, (struct opGet *)msg);

        case GM_LAYOUT:
            return TTL_OnLayout (cl, o, (struct gpLayout *)msg);
        case GM_RENDER:
            return TTL_OnRender (cl, o, (struct gpRender *)msg);
        case GM_DOMAIN:
            return TTL_OnDomain (cl, o, (struct gpDomain *)msg);

        case GM_HITTEST:
            return TTL_OnHitTest    (cl, o, (struct gpHitTest *)msg);
        case GM_GOACTIVE:
            return TTL_OnGoActive   (cl, o, (struct gpInput *)msg);
        case GM_HANDLEINPUT:
            return TTL_OnHandleInput(cl, o, (struct gpInput *)msg);
        case GM_GOINACTIVE:
            return TTL_OnGoInactive (cl, o, (struct gpGoInactive *)msg);

        default:
            return DoSuperMethodA(cl, o, (APTR)msg);
    }
}

/* ------------------------------------------------------------------ */
/* Class init / exit                                                    */
/* ------------------------------------------------------------------ */

Class *TootTimelineClass = NULL;

int TootTimeline_Init(void)
{
    if (TootTimelineClass) return 1;

    TootTimelineClass = MakeClass(NULL, "gadgetclass", NULL,
                                  sizeof(TTLData), 0);
    if (!TootTimelineClass) return 0;

    TootTimelineClass->cl_Dispatcher.h_Entry    = (HOOKFUNC)TootTimeline_Dispatch;
    TootTimelineClass->cl_Dispatcher.h_SubEntry = NULL;
    TootTimelineClass->cl_Dispatcher.h_Data     = NULL;

    return 1;
}

void TootTimeline_Exit(void)
{
    if (TootTimelineClass) {
        FreeClass(TootTimelineClass);
        TootTimelineClass = NULL;
    }
}
