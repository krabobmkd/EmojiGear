/*
 * UniButtonBGBM – class registration (MakeClass / FreeClass).
 * Always private (no AddClass/RemoveClass, NULL class name).
 * URPBase and BevelBase are opened by friendsh3ep.c before Init is called.
 */
#include <proto/exec.h>
#include <proto/intuition.h>

#include "unibuttonbgbm_private.h"

Class *UniButtonBGBMClass = NULL;

void UniButtonBGBM_Exit(void);

int UniButtonBGBM_Init(void)
{
    if (UniButtonBGBMClass == NULL) {
        UniButtonBGBMClass = MakeClass(
            NULL,           /* private: no published name */
            "gadgetclass",
            NULL,
            sizeof(UniButtonBGBMData),
            0
        );
        if (!UniButtonBGBMClass) goto failinit;

        UniButtonBGBMClass->cl_Dispatcher.h_Entry    = (HOOKFUNC)UniButtonBGBM_Dispatch;
        UniButtonBGBMClass->cl_Dispatcher.h_SubEntry = NULL;
        UniButtonBGBMClass->cl_Dispatcher.h_Data     = NULL;
    }
    return 1;

failinit:
    UniButtonBGBM_Exit();
    return 0;
}

void UniButtonBGBM_Exit(void)
{
    if (UniButtonBGBMClass) {
        FreeClass(UniButtonBGBMClass);
        UniButtonBGBMClass = NULL;
    }
}
