/*
 * UniButton gadget – method dispatcher.
 */
#include <proto/intuition.h>
#include <proto/alib.h>
#include <intuition/gadgetclass.h>
#include "unibutton_private.h"

ULONG ASM SAVEDS UniButton_Dispatch(
    REG(a0, Class  *cl),
    REG(a2, Object *o),
    REG(a1, Msg     msg))
{
    switch (msg->MethodID)
    {
        case OM_NEW:
            return UniButton_OnNew(cl, o, (struct opSet *)msg);

        case OM_DISPOSE:
            return UniButton_OnDispose(cl, o, msg);

        case OM_SET:
        case OM_UPDATE:
        {
            ULONG result = DoSuperMethodA(cl, o, (APTR)msg);
            return result | UniButton_OnSet(cl, o, (struct opSet *)msg);
        }

        case OM_GET:
            return UniButton_OnGet(cl, o, (struct opGet *)msg);

        case GM_DOMAIN:
            return UniButton_OnDomain(cl, o, (struct gpDomain *)msg);

        case GM_RENDER:
            return UniButton_OnRender(cl, o, (struct gpRender *)msg);

        case GM_LAYOUT:
            return UniButton_OnLayout(cl, o, (struct gpLayout *)msg);

        case GM_HITTEST:
            return GMR_GADGETHIT;

        case GM_GOACTIVE:
            if (UBT_DATA(cl, o)->readOnly) return GMR_NOREUSE;
            return UniButton_OnGoActive(cl, o, (struct gpInput *)msg);

        case GM_HANDLEINPUT:
            if (UBT_DATA(cl, o)->readOnly) return GMR_NOREUSE;
            return UniButton_OnHandleInput(cl, o, (struct gpInput *)msg);

        case GM_GOINACTIVE:
            return UniButton_OnGoInactive(cl, o, (struct gpGoInactive *)msg);

        default:
            return DoSuperMethodA(cl, o, (APTR)msg);
    }
}
