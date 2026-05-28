/*
 * testunibutton.c – boopsi window test for UniButton gadget.
 *
 * Opens a proper boopsi window (window.class + layout.gadget) containing:
 *
 *   vertical layout
 *   ├─ mainbutton     – main UniButton (fills the upper area)
 *   └─ horizontal layout  (fixed-height bottom bar)
 *        ├─ changetextbtn  – cycles through label strings on mainbutton
 *        └─ togglebtn      – UBT_PushButton latch that enables/disables mainbutton
 *
 * Usage:
 *   testunibutton [font.ttf] [pointsize]
 *
 * Defaults: "LiberationSans-Regular.ttf", 16.
 *
 * Press ESC or close the window to quit.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/classusr.h>
#include <intuition/gadgetclass.h>
#include <graphics/rastport.h>
#include <graphics/gfx.h>

#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/layout.h>
#include <proto/window.h>
#include <proto/alib.h>

#include <gadgets/layout.h>
#include <classes/window.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gadgets/unibutton.h>
#include <proto/unibutton.h>

/* -------------------------------------------------------------------------
 * Library bases (global – required by proto/ inline headers)
 * ------------------------------------------------------------------------- */
struct GfxBase        *GfxBase        = NULL;
struct IntuitionBase  *IntuitionBase  = NULL;
struct Library        *UniButtonBase  = NULL;
struct Library        *WindowBase     = NULL;
struct Library        *LayoutBase     = NULL;

/* -------------------------------------------------------------------------
 * Gadget IDs
 * ------------------------------------------------------------------------- */
#define GID_MAINBUTTON    1
#define GID_CHANGETEXT    2
#define GID_TOGGLEENABLE  3

/* -------------------------------------------------------------------------
 * Label pool cycled by the "Change Text" button
 * ------------------------------------------------------------------------- */
static const char *labels[] = {
    "\xF0\x9F\x98\x8A Hello,\n Amiga! \xF0\x9F\x90\xB8",            /* smiley + frog – single line  */
    "Changed! \xF0\x9F\x8E\x89\nSecond line here",                 /* party popper – two lines     */
    "Line one \xF0\x9F\x94\xA5\nLine two \xF0\x9F\x8C\x9F\nLine three \xF0\x9F\x90\xB8", /* fire / star / frog – three lines */
};
#define NUM_LABELS  3

/* -------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */
int main(int argc, char *argv[])
{
    const char *fontPath  = "LiberationSans-Regular.ttf";
    int         pointSize = 18;

    Object *window_obj    = NULL;
    Object *mainlayout    = NULL;
    Object *hbarlayout    = NULL;
    Object *mainbutton    = NULL;
    Object *changetextbtn = NULL;
    Object *togglebtn     = NULL;
    struct Window *win    = NULL;

    int  labelIndex  = 0;
    BOOL mainEnabled = TRUE;
    ULONG winsignal  = 0;
    ULONG urpDrawContext = 0;
    ULONG result;

    /* --- parse arguments --- */
    if (argc > 1) fontPath  = argv[1];
    if (argc > 2) pointSize = atoi(argv[2]);
    if (pointSize < 4)  pointSize = 4;
    if (pointSize > 72) pointSize = 72;

    /* --- open libraries --- */
    GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 39L);
    if (!GfxBase) { puts("ERROR: graphics.library v39+"); goto cleanup; }

    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39L);
    if (!IntuitionBase) { puts("ERROR: intuition.library v39+"); goto cleanup; }

    UniButtonBase = OpenLibrary("gadgets/unibutton.gadget", 1L);
    if (!UniButtonBase) { puts("ERROR: gadgets/unibutton.gadget"); goto cleanup; }

    WindowBase = OpenLibrary("window.class", 42L);
    if (!WindowBase) { puts("ERROR: window.class v42+"); goto cleanup; }

    LayoutBase = OpenLibrary("gadgets/layout.gadget", 42L);
    if (!LayoutBase) { puts("ERROR: gadgets/layout.gadget v42+"); goto cleanup; }

    /* --- create gadgets ---
     * Each button gets its own draw context (simplest setup).
     * For multiple buttons sharing one font cache, pass UBT_URPDrawContext. */

    mainbutton = (Object *)NewObject(UNIBUTTON_GetClass(), NULL,
        GA_ID,          (ULONG)GID_MAINBUTTON,
        UBT_BevelStyle, (ULONG)BVS_BUTTON,
        UBT_PointSize,  (ULONG)pointSize,
        UBT_AddFont,    (ULONG)fontPath,
        UBT_AddFont,    (ULONG)"NotoColorEmoji32.ttf",
        GA_Text,        (ULONG)labels[0],
        TAG_DONE);
    if (!mainbutton) { puts("ERROR: mainbutton creation failed"); goto cleanup; }

    /* by default each unibutton.gadget instances, like
    also all unitexteditor.gadget instances,
    create their own "UniCode RastPort Draw Context",
    which handles loading and rendering the multiple fonts and
    the whole system to draw utf8 strings with utf8rastport.library.
    It's a good idea to reuse the same URPDrawContext with
    multiple elements that may share same fonts, so there
    is only one font loaded and the glyph bitmap cache is shared.
    This is just done by getting UBT_URPDrawContext from the first
    button, and reuse it with other elements.
    (also this can be shared with UniTextEditor with UTED_URPDrawContext.)
    If UBT_URPDrawContext is given with a unibutton init,
    no need to set UBT_PointSize/UBT_AddFont.
    Note the shared UBT_URPDrawContext will be closed when the last
    element that uses it is disposed.
     */
    GetAttr(UBT_URPDrawContext,mainbutton,&urpDrawContext);

    changetextbtn = (Object *)NewObject(UNIBUTTON_GetClass(), NULL,
        GA_ID,          (ULONG)GID_CHANGETEXT,
        UBT_BevelStyle, (ULONG)BVS_BUTTON,
        UBT_URPDrawContext,urpDrawContext,
        GA_Text,        (ULONG)"Change Text",
        TAG_DONE);
    if (!changetextbtn) { puts("ERROR: changetextbtn creation failed"); goto cleanup; }

    togglebtn = (Object *)NewObject(UNIBUTTON_GetClass(), NULL,
        GA_ID,          (ULONG)GID_TOGGLEENABLE,
        UBT_BevelStyle, (ULONG)BVS_BUTTON,
        UBT_PushButton, (ULONG)TRUE,
        UBT_URPDrawContext,urpDrawContext,
        GA_Text,        (ULONG)"Disable Button",
        TAG_DONE);
    if (!togglebtn) { puts("ERROR: togglebtn creation failed"); goto cleanup; }

    /* --- build layout hierarchy --- */

    hbarlayout = (Object *)NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, (ULONG)LAYOUT_ORIENT_HORIZ,
        LAYOUT_AddChild,    (ULONG)changetextbtn,
        LAYOUT_AddChild,    (ULONG)togglebtn,
        TAG_DONE);
    if (!hbarlayout) { puts("ERROR: hbarlayout creation failed"); goto cleanup; }

    mainlayout = (Object *)NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation,   (ULONG)LAYOUT_ORIENT_VERT,
        LAYOUT_AddChild,      (ULONG)mainbutton,
            CHILD_WeightedHeight, 1,    /* grows to fill available space */
        LAYOUT_AddChild,      (ULONG)hbarlayout,
            CHILD_WeightedHeight, 0,    /* fixed – uses nominal (minimum) height */
        TAG_DONE);
    if (!mainlayout) { puts("ERROR: mainlayout creation failed"); goto cleanup; }

    /* --- create and open boopsi window --- */

    window_obj = (Object *)NewObject(WINDOW_GetClass(), NULL,
        WA_Left,            50L,
        WA_Top,             50L,
        WA_Width,           500L,
        WA_Height,          220L,
        WA_Title,           (ULONG)"testunibutton",
        WA_Flags,           (ULONG)(WFLG_CLOSEGADGET | WFLG_DRAGBAR |
                                    WFLG_DEPTHGADGET | WFLG_SIZEGADGET |
                                    WFLG_ACTIVATE    | WFLG_SMART_REFRESH),
        WA_IDCMP,           (ULONG)(IDCMP_CLOSEWINDOW | IDCMP_RAWKEY),
        WINDOW_ParentGroup, (ULONG)mainlayout,
        TAG_DONE);
    if (!window_obj) { puts("ERROR: window object creation failed"); goto cleanup; }

    win = (struct Window *)DoMethod(window_obj, WM_OPEN, NULL);
    if (!win) { puts("ERROR: WM_OPEN failed"); goto cleanup; }

    GetAttr(WINDOW_SigMask, window_obj, &winsignal);

    /* --- event loop --- */
    for (;;) {
        ULONG sigs = Wait(winsignal | SIGBREAKF_CTRL_C);

        if (sigs & SIGBREAKF_CTRL_C) break;

        while ((result = DoMethod(window_obj, WM_HANDLEINPUT, NULL))
               != WMHI_LASTMSG)
        {
            switch (result & WMHI_CLASSMASK)
            {
            case WMHI_CLOSEWINDOW:
                goto done;

            case WMHI_RAWKEY:
                /* ESC key (raw code 0x45) quits */
                if ((result & 0x7F) == 0x45)
                    goto done;
                break;

            case WMHI_GADGETUP:
                switch (result & WMHI_GADGETMASK)
                {
                case GID_MAINBUTTON:
                    puts("mainbutton clicked");
                    break;

                case GID_CHANGETEXT:
                    labelIndex = (labelIndex + 1) % NUM_LABELS;
                    SetGadgetAttrs((struct Gadget *)mainbutton, win, NULL,
                        GA_Text, (ULONG)labels[labelIndex],
                        TAG_DONE);
                    break;

                case GID_TOGGLEENABLE:
                    mainEnabled = !mainEnabled;
                    SetGadgetAttrs((struct Gadget *)mainbutton, win, NULL,
                        GA_Disabled, (ULONG)!mainEnabled,
                        TAG_DONE);
                    break;
                }
                break;
            }
        }
    }
done:

cleanup:
    if (win && window_obj) DoMethod(window_obj, WM_CLOSE);

    /* DisposeObject on the window cascades through layout to all child gadgets */
    if (window_obj) DisposeObject(window_obj);

    if (LayoutBase)    CloseLibrary(LayoutBase);
    if (WindowBase)    CloseLibrary(WindowBase);
    if (UniButtonBase) CloseLibrary(UniButtonBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    if (GfxBase)       CloseLibrary((struct Library *)GfxBase);

    return 0;
}
