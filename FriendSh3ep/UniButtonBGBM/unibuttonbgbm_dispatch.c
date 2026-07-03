/* UniButtonBGBM – method dispatcher. */
#include <proto/intuition.h>
#include <proto/alib.h>
#include <intuition/gadgetclass.h>
#include "unibuttonbgbm_private.h"

ULONG ASM SAVEDS UniButtonBGBM_Dispatch(
    REG(a0, Class  *cl),
    REG(a2, Object *o),
    REG(a1, Msg     msg))
{
    switch (msg->MethodID)
    {
        case OM_NEW:
            return UniButtonBGBM_OnNew(cl, o, (struct opSet *)msg);

        case OM_DISPOSE:
            return UniButtonBGBM_OnDispose(cl, o, msg);

        case OM_SET:
        case OM_UPDATE:
        {
            ULONG result = DoSuperMethodA(cl, o, (APTR)msg);
            return result | UniButtonBGBM_OnSet(cl, o, (struct opSet *)msg);
        }

        case OM_GET:
            return UniButtonBGBM_OnGet(cl, o, (struct opGet *)msg);

        case GM_DOMAIN:
            return UniButtonBGBM_OnDomain(cl, o, (struct gpDomain *)msg);

        case GM_RENDER:
            return UniButtonBGBM_OnRender(cl, o, (struct gpRender *)msg);

        case GM_LAYOUT:
            return UniButtonBGBM_OnLayout(cl, o, (struct gpLayout *)msg);

        case GM_HITTEST:
            return GMR_GADGETHIT;

        case GM_GOACTIVE:
            if (UBGBM_DATA(cl, o)->readOnly)   return GMR_NOREUSE;
            if (G(o)->Flags & GFLG_DISABLED)   return GMR_NOREUSE;
            return UniButtonBGBM_OnGoActive(cl, o, (struct gpInput *)msg);

        case GM_HANDLEINPUT:
            if (UBGBM_DATA(cl, o)->readOnly) return GMR_NOREUSE;
            return UniButtonBGBM_OnHandleInput(cl, o, (struct gpInput *)msg);

        case GM_GOINACTIVE:
            return UniButtonBGBM_OnGoInactive(cl, o, (struct gpGoInactive *)msg);

        default:
            return DoSuperMethodA(cl, o, (APTR)msg);
    }
}
