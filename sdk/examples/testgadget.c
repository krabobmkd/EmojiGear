/*
 * testgadget.c  –  unit test for the UniButton BOOPSI gadget.
 *
 * Opens an Intuition window, instantiates a UniButton with a Unicode/Emoji
 * label via the FreeType-backed utf8rastport engine, attaches it with
 * AddGList()/RefreshGList(), and runs a simple IDCMP event loop.
 *
 * Usage:
 *   testgadget [font.ttf] [pointsize]
 *
 * Defaults: first argument = "LiberationSans-Regular.ttf", second = 16.
 *
 * Click the button or close the window to quit.
 *
 * Written in C89 style for maximum Amiga compiler compatibility.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* You may need -I=EmojiGear:sdk/include or copy sdk/include into your
   compiler's include dir to find these headers. */
#include <gadgets/unibutton.h>
#include <proto/unibutton.h>

/* -------------------------------------------------------------------------
 * Amiga library bases (must be global for proto/ inline calls)
 * ------------------------------------------------------------------------- */
struct GfxBase       *GfxBase       = NULL;
struct IntuitionBase *IntuitionBase = NULL;
struct Library       *UniButtonBase = NULL;

#define GAD_BUTTON_ID  1

/* -------------------------------------------------------------------------
 * Button label: smiley + text + frog emoji (UTF-8 inline)
 * ------------------------------------------------------------------------- */
static const char *BUTTON_LABEL =
    "\xF0\x9F\x98\x8A Hello, Amiga! \xF0\x9F\x90\xB8";

/* -------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */
int main(int argc, char *argv[])
{
    const char *fontPath  = "LiberationSans-Regular.ttf";
    int         pointSize = 16;
    struct Window       *win = NULL;
    struct Gadget       *bt  = NULL;
    struct IntuiMessage *imsg;
    int    running = 1;

    /* --- parse arguments --- */
    if (argc > 1) fontPath  = argv[1];
    if (argc > 2) pointSize = atoi(argv[2]);
    if (pointSize < 4)  pointSize = 4;
    if (pointSize > 72) pointSize = 72;

    /* --- open libraries --- */
    GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 39L);
    if (!GfxBase) {
        puts("ERROR: cannot open graphics.library v39+");
        goto cleanup;
    }

    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39L);
    if (!IntuitionBase) {
        puts("ERROR: cannot open intuition.library v39+");
        goto cleanup;
    }

    UniButtonBase = OpenLibrary("gadgets/unibutton.gadget", 1);
    if (!UniButtonBase) {
        puts("ERROR: cannot open gadgets/unibutton.gadget");
        goto cleanup;
    }

    /* --- open window --- */
    win = OpenWindowTags(NULL,
        WA_Left,    50L,
        WA_Top,     50L,
        WA_Width,   480L,
        WA_Height,  200L,
        WA_Title,   (ULONG)"testgadget - UniButton",
        WA_Flags,   (ULONG)(WFLG_CLOSEGADGET | WFLG_DRAGBAR |
                             WFLG_DEPTHGADGET | WFLG_ACTIVATE),
        WA_IDCMP,   (ULONG)(IDCMP_CLOSEWINDOW | IDCMP_RAWKEY |
                             IDCMP_GADGETDOWN  | IDCMP_GADGETUP),
        TAG_DONE);
    if (!win) {
        puts("ERROR: cannot open window");
        goto cleanup;
    }

    /* --- create UniButton gadget ---
     * GA_Left/GA_Top are relative to the inner window origin (below title bar).
     * UBT_AddFont loads the TrueType font for rendering the label.            */
    bt = (struct Gadget *)NewObject(UNIBUTTON_GetClass(), NULL,
        GA_ID,             (ULONG)GAD_BUTTON_ID,
        GA_Left,           (ULONG)20,
        GA_Top,            (ULONG)20,
        GA_Width,          (ULONG)300,
        GA_Height,         (ULONG)50,
        UBT_BevelStyle,    BVS_BUTTON,
        UBT_PointSize,     (ULONG)pointSize,
        UBT_AddFont,       (ULONG)fontPath,
        UBT_AddFont,       (ULONG)"NotoColorEmoji32.ttf", /* to have emojis */
        GA_Text,           (ULONG)BUTTON_LABEL,
        TAG_DONE);
    if (!bt) {
        puts("ERROR: cannot create UniButton gadget");
        goto cleanup;
    }

    /* --- attach gadget to window and render it --- */
    AddGList(win, bt, (UWORD)-1, 1, NULL);
    RefreshGList(bt, win, NULL, 1);

    /* --- event loop: close gadget, any key, or button click quits --- */
    while (running) {
        WaitPort(win->UserPort);
        while ((imsg = (struct IntuiMessage *)GetMsg(win->UserPort)) != NULL) {
            ULONG          cls = imsg->Class;
            struct Gadget *gad = (struct Gadget *)imsg->IAddress;
            ReplyMsg((struct Message *)imsg);

            switch (cls) {
            case IDCMP_CLOSEWINDOW:
                running = 0;
                break;
            case IDCMP_GADGETUP:
                if (gad && gad->GadgetID == GAD_BUTTON_ID) {
                    puts("UniButton clicked!");
                    running = 0;
                }
                break;
            default:
                break;
            }
        }
    }
    SetAttrs(bt,
        UBT_FlushDebugOutput,TRUE,TAG_END);

cleanup:
    if (win && bt) RemoveGList(win, bt, 1);
    if (bt)        DisposeObject((Object *)bt);
    if (win)       CloseWindow(win);

    if (UniButtonBase) CloseLibrary(UniButtonBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    if (GfxBase)       CloseLibrary((struct Library *)GfxBase);

    return 0;
}
