/*
 * UniButtonP9 – class registration (MakeClass / FreeClass).
 * Always private (no AddClass/RemoveClass, NULL class name).
 * URPBase and BevelBase are opened by friendsh3ep.c before Init is called.
 */
#include <proto/exec.h>
#include <proto/intuition.h>

#include "unibuttonp9_private.h"

Class *UniButtonP9Class = NULL;

void UniButtonP9_Exit(void);

int UniButtonP9_Init(void)
{
    if (UniButtonP9Class == NULL) {
        UniButtonP9Class = MakeClass(
            NULL,           /* private: no published name */
            "gadgetclass",
            NULL,
            sizeof(UniButtonP9Data),
            0
        );
        if (!UniButtonP9Class) goto failinit;

        UniButtonP9Class->cl_Dispatcher.h_Entry    = (HOOKFUNC)UniButtonP9_Dispatch;
        UniButtonP9Class->cl_Dispatcher.h_SubEntry = NULL;
        UniButtonP9Class->cl_Dispatcher.h_Data     = NULL;
    }
    return 1;

failinit:
    UniButtonP9_Exit();
    return 0;
}

void UniButtonP9_Exit(void)
{
    if (UniButtonP9Class) {
        FreeClass(UniButtonP9Class);
        UniButtonP9Class = NULL;
    }
}
