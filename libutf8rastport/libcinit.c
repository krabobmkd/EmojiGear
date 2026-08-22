#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/layers.h>
#include <proto/cybergraphics.h>
#include <workbench/startup.h>

/* libnix20 ADD2INIT/ADD2EXIT registrations are walked by ncrt0.S callfuncs,
   which is never linked (-nostartfiles).  We must call the critical ones
   explicitly.  Functions we need to call:
     __initmalloc  (-50)  sets up libnix malloc pool semaphore
     __initstdio   (-30)  initialises __sF[0..2] (stdin/stdout/stderr)
   Without __initstdio, __sF[2] (stderr) stays NULL.  libpng's default error
   handler calls fprintf(stderr,...) → NULL deref → crash on any PNG error.
   Note: this BSS is library-local, completely separate from the host process. */
extern void __initmalloc(void);
extern void __initstdio(void);
extern void __exitstdio(void);
extern void __exitmalloc(void);

/* Required by __initstdio (_WBenchMsg == NULL means CLI context). */
struct WBStartup *_WBenchMsg = NULL;

struct DosLibrary      *DOSBase       ;
struct GfxBase         *GfxBase       ;
struct IntuitionBase   *IntuitionBase ;
struct Library         *UtilityBase   ;
struct Library         *LayersBase    ;
struct Library         *CyberGfxBase  ;

const char VersionString[] = "utf8rastport.library 5.2 ("__DATE__")";
const char Lib_ID[]= "utf8rastport.library";


void CLibClose();
void CLibExpunge();

int CLibInit()
{
    DOSBase = NULL;
    GfxBase = NULL;
    IntuitionBase = NULL;
    UtilityBase = NULL;
    LayersBase = NULL;
    CyberGfxBase = NULL;

    DOSBase = (struct DosLibrary*)OpenLibrary("dos.library",32);
    if(!DOSBase) goto failinit;
    GfxBase = (struct GfxBase*) OpenLibrary("graphics.library",39);
    if(!GfxBase) goto failinit;
    IntuitionBase = (struct IntuitionBase*)OpenLibrary("intuition.library",39);
    if(!IntuitionBase) goto failinit;
    UtilityBase = OpenLibrary("utility.library",39);
    if(!UtilityBase) goto failinit;
    LayersBase = OpenLibrary("layers.library",39);
    if(!LayersBase) goto failinit;
        /* CyberGfxBase NULL accepted */
    CyberGfxBase = OpenLibrary("cybergraphics.library", 1);

    /* success is 0 */
    return 0;
failinit:
    CLibExpunge();
    return 1;
}
void CLibClose()
{
    // actually keep open
}
void CLibExpunge()
{

    if(CyberGfxBase) CloseLibrary(CyberGfxBase);
    CyberGfxBase = NULL;
    if(LayersBase) CloseLibrary(LayersBase);
    LayersBase = NULL;
    if(UtilityBase) CloseLibrary(UtilityBase);
    UtilityBase = NULL;
    if(IntuitionBase) CloseLibrary(IntuitionBase);
    IntuitionBase = NULL;
    if(GfxBase) CloseLibrary(GfxBase);
    GfxBase = NULL;
    if(DOSBase) CloseLibrary(DOSBase);
    DOSBase = NULL;


}

/* stubs for missing startup */
void exit(int c)
{

}
