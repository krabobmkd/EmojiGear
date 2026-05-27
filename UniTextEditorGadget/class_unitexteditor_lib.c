/*
 * TextEditor gadget – class registration (MakeClass / FreeClass).
 */



#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/keymap.h>
#include <proto/bevel.h>
#include <proto/dos.h>
#include <proto/utility.h>
#include <proto/layers.h>
#include <intuition/classes.h>
#include <intuition/classusr.h>

#include "unitexteditor_private.h"


/** this is the struct that is the extended struct Library
 * That is created with OpenLibrary().
 * But as it just manages a BOOPSI class there are just the open/close functions.
 * which themselves only manages registering the class with MakeClass()/AddClass()
 * versioning, and closing itself. This is *not* the boopsi class definition which is up there.
 * So it doesnt have to evolve, and can keep same name for each projects.
 * That said, layout.gadget has tool methods like any library.
 * must be mirrored to equivalent in classinit.s
 */
struct ExtClassLib
{
    struct ClassLibrary cb_ClassLibrary;

    APTR  cb_SysBase; // this is passed as LibInit
    APTR  cb_SegList; // this is passed at OpenLib and needed at expunge.
    // note: old libraries examples adds bases for graphics/intuition/utility after this
    // but C compiler will only search then in globals...
};


#ifndef UNITEXTEDITOR_STATIC
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

const char VersionString[] = "unitexteditor.gadget 2.3 (" __DATE__ ")";
const char Lib_ID[]= "unitexteditor.gadget";

#endif

Class *UniTextEditorClass = NULL;

void UniTextEditor_Exit(void);
// UNITEXTEDITOR_STATIC
int UniTextEditor_Init(void)
{
    if(UniTextEditorClass == NULL)
    {
#ifndef UNITEXTEDITOR_STATIC
        DOSBase = NULL;
        GfxBase = NULL;
        IntuitionBase = NULL;
        UtilityBase = NULL;
        LayersBase = NULL;
        KeymapBase = NULL;
        BevelBase = NULL;
        URPBase = NULL;
        UniTextEditorClass = NULL;

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

        KeymapBase = OpenLibrary("keymap.library", 36);
        if(!KeymapBase) goto failinit;

        URPBase = OpenLibrary("utf8rastport.library",1);
        if(!URPBase) goto failinit;
        /* accept failure over this one */
        BevelBase = OpenLibrary("images/bevel.image",32);

       UniTextEditorClass = MakeClass(
            "unitexteditor.gadget", /* published name */
            "gadgetclass",
            NULL,
            sizeof(UniTextEditorData),
            0
        );
#else
    /*static */
    UniTextEditorClass = MakeClass(
        NULL,
        "gadgetclass",
        NULL,
        sizeof(UniTextEditorData),
        0
    );
#endif

    if (!UniTextEditorClass) goto failinit;

    UniTextEditorClass->cl_Dispatcher.h_Entry    = (HOOKFUNC)UniTextEditor_Dispatch;
    UniTextEditorClass->cl_Dispatcher.h_SubEntry = NULL;
    UniTextEditorClass->cl_Dispatcher.h_Data     = NULL;
#ifndef UNITEXTEDITOR_STATIC
    /* when initing a public class, we publish it to intuition BOOPSI system */
      AddClass(UniTextEditorClass);
#endif
    }
    /* success is 0*/
    return 0;
failinit:
    UniTextEditor_Exit();
    return 1;
}

void UniTextEditor_Exit(void)
{
    if (UniTextEditorClass) {
#ifndef UNITEXTEDITOR_STATIC
        RemoveClass(UniTextEditorClass);
#endif
        FreeClass(UniTextEditorClass);
        UniTextEditorClass = NULL;
    }
#ifndef UNITEXTEDITOR_STATIC
    if(URPBase) {
        CloseLibrary(URPBase);
        URPBase = NULL;
    }

    if(BevelBase) {
        CloseLibrary(BevelBase);
        BevelBase = NULL;
    }
    if(KeymapBase) {
        CloseLibrary(KeymapBase);
        KeymapBase = NULL;
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
/* works for both static and dynamic */
Class *UNITEXTEDITOR_GetClass()
{
    return UniTextEditorClass;
}
