/*
 * TextEditor gadget – method dispatcher.
 */

#include <proto/intuition.h>
#include <proto/alib.h>
#include <intuition/gadgetclass.h>
#include "unitexteditor_private.h"
ULONG ASM SAVEDS UniTextEditor_Dispatch(
    REG(a0, Class  *cl),
    REG(a2, Object *o),
    REG(a1, Msg     msg))
{

    switch (msg->MethodID)
    {
        case OM_NEW:
            return UniTextEditor_OnNew(cl, o, (struct opSet *)msg);

        case OM_DISPOSE:
            return UniTextEditor_OnDispose(cl, o, msg);

        case OM_SET:
        case OM_UPDATE:
        {
            ULONG result = DoSuperMethodA(cl, o, (APTR)msg);
            return result | UniTextEditor_OnSet(cl, o, (struct opSet *)msg);
        }

        case OM_GET:
            return UniTextEditor_OnGet(cl, o, (struct opGet *)msg);

        case GM_DOMAIN:
            return UniTextEditor_OnDomain(cl, o, (struct gpDomain *)msg);

        case GM_RENDER:
            return UniTextEditor_OnRender(cl, o, (struct gpRender *)msg);

        case GM_LAYOUT:
            return UniTextEditor_OnLayout(cl, o, (struct gpLayout *)msg);

        case GM_HITTEST:
            return GMR_GADGETHIT;

        case GM_GOACTIVE:
            if (UTED_DATA(cl, o)->readOnly) return GMR_NOREUSE;
            return UniTextEditor_OnGoActive(cl, o, (struct gpInput *)msg);

        case GM_HANDLEINPUT:
            if (UTED_DATA(cl, o)->readOnly) return GMR_NOREUSE;
            return UniTextEditor_OnHandleInput(cl, o, (struct gpInput *)msg);

        case GM_GOINACTIVE:
            return UniTextEditor_OnGoInactive(cl, o, (struct gpGoInactive *)msg);

        default:
            return DoSuperMethodA(cl, o, (APTR)msg);
    }
}
