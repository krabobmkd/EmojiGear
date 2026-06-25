/* UniButtonP9 – method dispatcher. */
#include <proto/intuition.h>
#include <proto/alib.h>
#include <intuition/gadgetclass.h>
#include "unibuttonp9_private.h"

ULONG ASM SAVEDS UniButtonP9_Dispatch(
    REG(a0, Class  *cl),
    REG(a2, Object *o),
    REG(a1, Msg     msg))
{
    switch (msg->MethodID)
    {
        case OM_NEW:
            return UniButtonP9_OnNew(cl, o, (struct opSet *)msg);

        case OM_DISPOSE:
            return UniButtonP9_OnDispose(cl, o, msg);

        case OM_SET:
        case OM_UPDATE:
        {
            ULONG result = DoSuperMethodA(cl, o, (APTR)msg);
            return result | UniButtonP9_OnSet(cl, o, (struct opSet *)msg);
        }

        case OM_GET:
            return UniButtonP9_OnGet(cl, o, (struct opGet *)msg);

        case GM_DOMAIN:
            return UniButtonP9_OnDomain(cl, o, (struct gpDomain *)msg);

        case GM_RENDER:
            return UniButtonP9_OnRender(cl, o, (struct gpRender *)msg);

        case GM_LAYOUT:
            return UniButtonP9_OnLayout(cl, o, (struct gpLayout *)msg);

        case GM_HITTEST:
            return GMR_GADGETHIT;

        case GM_GOACTIVE:
            if (UBTP9_DATA(cl, o)->readOnly)   return GMR_NOREUSE;
            if (G(o)->Flags & GFLG_DISABLED)   return GMR_NOREUSE;
            return UniButtonP9_OnGoActive(cl, o, (struct gpInput *)msg);

        case GM_HANDLEINPUT:
            if (UBTP9_DATA(cl, o)->readOnly) return GMR_NOREUSE;
            return UniButtonP9_OnHandleInput(cl, o, (struct gpInput *)msg);

        case GM_GOINACTIVE:
            return UniButtonP9_OnGoInactive(cl, o, (struct gpGoInactive *)msg);

        default:
            return DoSuperMethodA(cl, o, (APTR)msg);
    }
}
