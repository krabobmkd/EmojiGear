/*
 * UniButton gadget – class registration (MakeClass / FreeClass).
 *
 * BevelBase is opened/closed by UniTextEditor_Init/Exit in the static-link
 * build.  UniButton_Init/Exit only manage the gadget class itself.
 */
#include <proto/exec.h>
#include <proto/intuition.h>

#include "unibutton_private.h"


#ifndef UNIBUTTON_STATIC
struct GfxBase        *GfxBase=NULL;
struct IntuitionBase  *IntuitionBase=NULL;
struct Library        *LayersBase=NULL;
struct DosLibrary     *DOSBase=NULL;
struct Library        *UtilityBase=NULL;
struct Library *KeymapBase = NULL;
struct Library *BevelBase=NULL;
struct Library *URPBase=NULL;
    #if defined(__GNUC__) && (__GNUC__ < 3)
        struct Library        *__UtilityBase=NULL; // amiga gcc2.95 with noixemul and 68000, and our gadget startup needs that.
    #endif

const char VersionString[] = "unibutton.gadget 4.3 (" __DATE__ ")";
const char Lib_ID[]= "unibutton.gadget";

#endif


Class *UniButtonClass = NULL;
void UniButton_Exit();
int UniButton_Init(void)
{
    if(UniButtonClass == NULL)
    {
#ifndef UNIBUTTON_STATIC

        DOSBase = NULL;
        GfxBase = NULL;
        IntuitionBase = NULL;
        UtilityBase = NULL;
        LayersBase = NULL;
        BevelBase = NULL;
        URPBase = NULL;
        UniButtonClass = NULL;

        DOSBase = (struct DosLibrary*)OpenLibrary("dos.library",36);
        if(!DOSBase) goto failinit;
        GfxBase = (struct GfxBase*) OpenLibrary("graphics.library",39);
        if(!GfxBase) goto failinit;
        IntuitionBase = (struct IntuitionBase*)OpenLibrary("intuition.library",39);
        if(!IntuitionBase) goto failinit;
        UtilityBase = OpenLibrary("utility.library",39);
        if(!UtilityBase) goto failinit;
        LayersBase = OpenLibrary("layers.library",39);
        if(!LayersBase) goto failinit;

        URPBase = OpenLibrary("utf8rastport.library",4);
        if(!URPBase) goto failinit;

        /* accept failure over this one */
        BevelBase = OpenLibrary("images/bevel.image",32);

       UniButtonClass = MakeClass(
            "unibutton.gadget", /* published name */
            "gadgetclass",
            NULL,
            sizeof(UniButtonData),
            0
        );
#else
    UniButtonClass = MakeClass(
        NULL,
        "gadgetclass",
        NULL,
        sizeof(UniButtonData),
        0
    );
#endif

    if (!UniButtonClass) goto failinit;

    UniButtonClass->cl_Dispatcher.h_Entry    = (HOOKFUNC)UniButton_Dispatch;
    UniButtonClass->cl_Dispatcher.h_SubEntry = NULL;
    UniButtonClass->cl_Dispatcher.h_Data     = NULL;

#ifndef UNIBUTTON_STATIC
    /* when initing a public class, we publish it to intuition BOOPSI system */
      AddClass(UniButtonClass);
#endif
    }
    /* success is 0*/
    return 0;
failinit:
    UniButton_Exit();
    return 1;
}

void UniButton_Exit(void)
{
    if (UniButtonClass) {
#ifndef UNIBUTTON_STATIC
    /* when initing a public class, we publish it to intuition BOOPSI system */
      RemoveClass(UniButtonClass);
#endif
        FreeClass(UniButtonClass);
        UniButtonClass = NULL;
    }
 #ifndef UNIBUTTON_STATIC
    if(URPBase) {
        CloseLibrary(URPBase);
        URPBase = NULL;
    }

    if(BevelBase) {
        CloseLibrary(BevelBase);
        BevelBase = NULL;
    }

    if(LayersBase) {
        CloseLibrary(LayersBase);
        LayersBase = NULL;
    }
    if(UtilityBase) {
        CloseLibrary(UtilityBase);
        UtilityBase = NULL;
    }
    if(IntuitionBase) {
        CloseLibrary(IntuitionBase);
        IntuitionBase = NULL;
    }
    if(GfxBase) {
        CloseLibrary(GfxBase);
        GfxBase = NULL;
    }
    if(DOSBase) {
        CloseLibrary(DOSBase);
        DOSBase = NULL;
    }
#endif
}

Class *UNIBUTTON_GetClass()
{
    return UniButtonClass;
}
