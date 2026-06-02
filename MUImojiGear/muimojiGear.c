/*
 * muimojiGear.c – MUImojiGear: OS3.9-compatible unicode text editor
 *
 * Uses MUI (muimaster.library v19 / MUI 3.8+) for the UI, making it
 * compatible with Amiga OS3.9 where the ReAction BOOPSI classes used by
 * EmojiGear (window.class, layout.gadget, clicktab.gadget v47+) are not
 * available.
 *
 * Window layout:
 *   MUIC_Application
 *     MUIC_Window
 *       MUIC_Group  (vertical, outer)
 *         MUIC_Register  ← tab bar; one MUIC_Rectangle page per open file
 *         MUIC_Boopsi    ← UniTextEditor gadget; fills remaining space
 *         MUIC_Text      ← single-line status bar
 *
 *
 * BOOPSI notifications (ICA_TARGET = MmgTargetInstance):
 *   mmgboopsimessage.c queues UTEDN_* attributes and signals
 *   SIGBREAKF_CTRL_F.  The CTRL_F handler in the event loop drains the
 *   queue via MmgBoopsi_NextMessage() and calls RefreshGList / updateStatus
 *   in the safe application-task context.
 *
 * Font preferences are read from icon tooltypes:
 *   FONT=<path>          primary font (default: LiberationSans-Regular.ttf)
 *   EMOJIFONT=<path>     emoji font   (default: NotoColorEmoji32.ttf)
 *   POINTSIZE=<n>        point size   (default: 16)
 *
 * Keyboard:
 *   Editor uses UKM_Internal so MUI routes keyboard events directly to the
 *   BOOPSI gadget's GM_HANDLEINPUT – no manual key forwarding needed.
 *   Amiga+key shortcuts reach the menu system unchanged (GM_HANDLEINPUT
 *   replays LCOMMAND/RCOMMAND events so Intuition sees them).
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/tasks.h>
#include <intuition/intuition.h>
#include <intuition/classusr.h>
#include <intuition/gadgetclass.h>
#include <intuition/icclass.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/dos.h>
#include <proto/icon.h>
#include <proto/locale.h>
#include <proto/muimaster.h>
#include <proto/alib.h>
#include <libraries/locale.h>
#include <libraries/mui.h>
#include <libraries/gadtools.h>
#include <workbench/workbench.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gadgets/unitexteditor.h>
#include <proto/unitexteditor.h>

#include "../EmojiGear/eglocale.h"
#include "../EmojiGear/tooltypepref.h"
#include "mmgboopsimessage.h"

/* =========================================================================
 * Library bases – must be global for the SDK's proto/inline headers
 * =========================================================================
 */
struct Library       *MUIMasterBase     = NULL;
struct GfxBase       *GfxBase           = NULL;
struct IntuitionBase *IntuitionBase     = NULL;
struct Library       *UtilityBase       = NULL;
struct LocaleBase    *LocaleBase        = NULL;  /* optional – for eglocale  */
struct Library       *IconBase          = NULL;  /* optional – for tooltypes */
struct Library       *UniTextEditorBase = NULL;

/* Exposed to mmgboopsimessage.c */
struct Task *mmgMainTask = NULL;

/* =========================================================================
 * GCC-safe MUI_NewObject wrapper
 * GCC O1+ inlines the SDK's __inline MUI_NewObject and DCE's the varargs.
 * noinline forces a real m68k stack frame so &tags addresses the full list.
 * (See sdk/examples/testMUIUniTextEditor.c – MUI_NewObject_GCC_bug.txt)
 * =========================================================================
 */
static Object * __attribute__((noinline))
MUI_NewObjectB(const char *cl, Tag tags, ...)
{
    return MUI_NewObjectA(cl, (struct TagItem *) &tags);
}

/* =========================================================================
 * Application constants
 * =========================================================================
 */
#define MMG_APPBASE   "MUIMOJIGEAIR"
#define MMG_VERSION   "$VER: MUImojiGear 0.1 (2026)"

/* GA_ID for the editor BOOPSI gadget */
#define GID_EDITOR    1

/* MUIM_Application_ReturnID values */
#define RID_QUIT        10
#define RID_NEW_FILE    12  /* clears editor text */
#define RID_CUT         13
#define RID_COPY        14
#define RID_PASTE       15
#define RID_UNDO        16
#define RID_REDO        17
#define RID_VSCROLL     18
#define RID_HSCROLL     19

/* =========================================================================
 * MUI objects
 * =========================================================================
 */
static Object *editorAndVScrollerGroup = NULL;
static Object *editorObj   = NULL;
static Object *statusObj   = NULL;
static Object *winObj      = NULL;
static Object *appObj      = NULL;

static Object *vScrollBar      = NULL;
static Object *hScrollBar      = NULL;

/* Menu items we need for notification setup */
static Object *miNewFile   = NULL;
static Object *miQuit      = NULL;
static Object *miCut       = NULL;
static Object *miCopy      = NULL;
static Object *miPaste     = NULL;
static Object *miUndo      = NULL;
static Object *miRedo      = NULL;

/* =========================================================================
 * Helpers
 * =========================================================================
 */

/* Retrieve the raw Intuition gadget and window pointers from MUI objects.
 * Required for SetGadgetAttrs() / RefreshGList() on the editor. */
static void getRawEditorWin(struct Gadget **gOut, struct Window **wOut)
{
    *gOut = NULL; *wOut = NULL;
    if (editorObj) GetAttr(MUIA_Boopsi_Object, editorObj, (ULONG *)gOut);
    if (winObj)    GetAttr(MUIA_Window_Window,  winObj,   (ULONG *)wOut);
}

/* Refresh the status bar from the editor's current UTED_Modified state. */
static void updateStatus(void)
{
    static char buf[128];
    struct Gadget *g = NULL; struct Window *w = NULL;
    ULONG modified = 0;
    getRawEditorWin(&g, &w);
    if (g) GetAttr(UTED_Modified, (Object *)g, &modified);
    strncpy(buf, modified ? LOC(MSG_STATUS_MODIFIED)
                          : LOC(MSG_STATUS_READY), sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    if (statusObj) SetAttrs(statusObj, MUIA_Text_Contents, (ULONG)buf, TAG_DONE);
}

/* Push the current editor vertical scroll state to the MUI scrollbar.
 * MUIA_Prop_First  = current top line
 * MUIA_Prop_Entries= total rendered rows (word-wrap aware via RenderedLineCount)
 * MUIA_Prop_Visible= visible lines
 * MUI won't re-notify when the value is already current, so this is
 * safe to call unconditionally from the CTRL_F handler. */
static void syncVScrollbar(void)
{
    ULONG top = 0, total = 1, visible = 1;
    struct Gadget *g = NULL; struct Window *w = NULL;
    getRawEditorWin(&g, &w);
    if (!g || !vScrollBar) return;
    GetAttr(UTED_ScrollTop,          (Object *)g, &top);
    GetAttr(UTED_RenderedLineCount,  (Object *)g, &total);
    GetAttr(UTED_VisibleLines,       (Object *)g, &visible);
    if (total   < 1) total   = 1;
    if (visible < 1) visible = 1;
    SetAttrs(vScrollBar,
        MUIA_Prop_First,   top,
        MUIA_Prop_Entries, total,
        MUIA_Prop_Visible, visible,
        TAG_DONE);
}

/* Push the current editor horizontal scroll state to the MUI scrollbar.
 * MUIA_Prop_First  = current left pixel
 * MUIA_Prop_Entries= widest rendered line in pixels
 * MUIA_Prop_Visible= visible width of the gadget in pixels */
static void syncHScrollbar(void)
{
    ULONG left = 0, maxWidth = 0, visible = 1;
    struct Gadget *g = NULL; struct Window *w = NULL;
    getRawEditorWin(&g, &w);
    if (!g || !hScrollBar) return;
    GetAttr(UTED_ScrollLeft,    (Object *)g, &left);
    GetAttr(UTED_MaxLineWidth,  (Object *)g, &maxWidth);
    GetAttr(GA_Width,           (Object *)g, &visible);
    if(visible == 0) return;

  //  printf("syncHScrollbar: utedml:%d gw:%d\n",maxWidth,visible);

    if (maxWidth < visible) maxWidth = visible;
    SetAttrs(hScrollBar,
        MUIA_Prop_First,   left,
        MUIA_Prop_Entries, maxWidth,
        MUIA_Prop_Visible, visible,
        TAG_DONE);
}

/* =========================================================================
 * Actions
 * =========================================================================
 */

/* Dispatch one of the UTED_Apply* edit actions to the underlying gadget. */
static void doEditAction(ULONG attr)
{
    struct Gadget *g; struct Window *w;
    getRawEditorWin(&g, &w);
    if (g && w) {
        SetGadgetAttrs(g, w, NULL, attr, (ULONG)TRUE, TAG_DONE);
        /* Restore keyboard focus to the editor after the action */
        SetAttrs(winObj, MUIA_Window_ActiveObject, (ULONG)editorObj, TAG_DONE);
    }
}

/* =========================================================================
 * main
 * =========================================================================
 */
int main(int argc, char *argv[])
{
    int         exitCode       = 0;
    const char *fontPath       = "LiberationSans-Regular.ttf";
    const char *emojiFontPath  = "NotoColorEmoji32.ttf";
    int         pointSize      = 16;
    const char *tp;

    Object *menustrip  = NULL;
    Object *menuProj   = NULL;
    Object *menuEdit   = NULL;

    (void)argc; (void)argv;

    /* ------------------------------------------------------------------ */
    /* Open mandatory libraries                                             */
    /* ------------------------------------------------------------------ */
    UtilityBase = OpenLibrary("utility.library", 37L);
    if (!UtilityBase) { puts("ERROR: utility.library v37+"); exitCode=1; goto cleanup; }

    GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 39L);
    if (!GfxBase) { puts("ERROR: graphics.library v39+"); exitCode=1; goto cleanup; }

    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39L);
    if (!IntuitionBase) { puts("ERROR: intuition.library v39+"); exitCode=1; goto cleanup; }

    UniTextEditorBase = OpenLibrary("gadgets/unitexteditor.gadget", 2L);
    if (!UniTextEditorBase) { puts("ERROR: unitexteditor.gadget v2+"); exitCode=1; goto cleanup; }

    MUIMasterBase = OpenLibrary("muimaster.library", 19L);
    if (!MUIMasterBase) { puts("ERROR: muimaster.library v19+ (MUI 3.8+)"); exitCode=1; goto cleanup; }

    /* ------------------------------------------------------------------ */
    /* Optional libraries (locale for translations, icon for tooltypes)    */
    /* ------------------------------------------------------------------ */
    LocaleBase = (struct LocaleBase *)OpenLibrary("locale.library", 38L);
    IconBase   = OpenLibrary("icon.library", 37L);

    /* ------------------------------------------------------------------ */
    /* Locale (falls back to built-in English if catalog absent)           */
    /* ------------------------------------------------------------------ */
    EgLocale_Init("muimojiGear.catalog", 1);

    /* ------------------------------------------------------------------ */
    /* Font preferences from icon tooltypes                                */
    /* ------------------------------------------------------------------ */
    if (IconBase && ToolTypePrefs_Init("MUImojiGear")) {
        tp = ToolTypePrefs_Get("FONT");
        if (tp && tp[0]) fontPath = tp;
        tp = ToolTypePrefs_Get("EMOJIFONT");
        if (tp && tp[0]) emojiFontPath = tp;
        tp = ToolTypePrefs_Get("POINTSIZE");
        if (tp && tp[0]) {
            int ps = atoi(tp);
            if (ps >= 8 && ps <= 72) pointSize = ps;
        }
    }

    /* ------------------------------------------------------------------ */
    /* BOOPSI notification target                                          */
    /* ------------------------------------------------------------------ */
    mmgMainTask = FindTask(NULL);
    if (!MmgBoopsi_Init()) {
        puts("ERROR: MmgBoopsi_Init failed");
        exitCode = 1; goto cleanup;
    }

    /* ------------------------------------------------------------------ */
    /* MUI menus – create items first so we can hook notifications later   */
    /* ------------------------------------------------------------------ */
    miNewFile = MUI_NewObjectB(MUIC_Menuitem,
        MUIA_Menuitem_Title,    (ULONG)LOC(MSG_FILE_NEW),
        MUIA_Menuitem_Shortcut, (ULONG)"N",
        TAG_DONE);
    miQuit = MUI_NewObjectB(MUIC_Menuitem,
        MUIA_Menuitem_Title,    (ULONG)LOC(MSG_MENU_QUIT),
        MUIA_Menuitem_Shortcut, (ULONG)"Q",
        TAG_DONE);
    menuProj = MUI_NewObjectB(MUIC_Menu,
        MUIA_Menu_Title,   (ULONG)LOC(MSG_MENU_PROJECT),
        MUIA_Family_Child, (ULONG)miNewFile,
        MUIA_Family_Child, (ULONG)MUI_NewObjectB(MUIC_Menuitem,
            MUIA_Menuitem_Title, (ULONG)NM_BARLABEL, TAG_DONE),
        MUIA_Family_Child, (ULONG)miQuit,
        TAG_DONE);

    miCut   = MUI_NewObjectB(MUIC_Menuitem, MUIA_Menuitem_Title, (ULONG)LOC(MSG_EDIT_CUT),
                              MUIA_Menuitem_Shortcut, (ULONG)"X", TAG_DONE);
    miCopy  = MUI_NewObjectB(MUIC_Menuitem, MUIA_Menuitem_Title, (ULONG)LOC(MSG_EDIT_COPY),
                              MUIA_Menuitem_Shortcut, (ULONG)"C", TAG_DONE);
    miPaste = MUI_NewObjectB(MUIC_Menuitem, MUIA_Menuitem_Title, (ULONG)LOC(MSG_EDIT_PASTE),
                              MUIA_Menuitem_Shortcut, (ULONG)"V", TAG_DONE);
    miUndo  = MUI_NewObjectB(MUIC_Menuitem, MUIA_Menuitem_Title, (ULONG)LOC(MSG_EDIT_UNDO),
                              MUIA_Menuitem_Shortcut, (ULONG)"Z", TAG_DONE);
    miRedo  = MUI_NewObjectB(MUIC_Menuitem, MUIA_Menuitem_Title, (ULONG)LOC(MSG_EDIT_REDO),
                              MUIA_Menuitem_Shortcut, (ULONG)"Y", TAG_DONE);
    menuEdit = MUI_NewObjectB(MUIC_Menu,
        MUIA_Menu_Title,   (ULONG)LOC(MSG_MENU_EDIT),
        MUIA_Family_Child, (ULONG)miCut,
        MUIA_Family_Child, (ULONG)miCopy,
        MUIA_Family_Child, (ULONG)miPaste,
        MUIA_Family_Child, (ULONG)MUI_NewObjectB(MUIC_Menuitem,
            MUIA_Menuitem_Title, (ULONG)NM_BARLABEL, TAG_DONE),
        MUIA_Family_Child, (ULONG)miUndo,
        MUIA_Family_Child, (ULONG)miRedo,
        TAG_DONE);

    menustrip = MUI_NewObjectB(MUIC_Menustrip,
        MUIA_Family_Child, (ULONG)menuProj,
        MUIA_Family_Child, (ULONG)menuEdit,
        TAG_DONE);

    /* ------------------------------------------------------------------ */
    /* UniTextEditor via MUIC_Boopsi                                       */
    /* ------------------------------------------------------------------ */
    editorObj = MUI_NewObjectB(MUIC_Boopsi,
        MUIA_Boopsi_Class,            (ULONG)UNITEXTEDITOR_GetClass(),
        MUIA_Boopsi_MinWidth,         200,
        MUIA_Boopsi_MinHeight,        100,
        MUIA_Boopsi_Smart,            TRUE,
        MUIA_Boopsi_Remember,         UTED_PointSize,
        MUIA_Boopsi_Remember,         UTED_KeyMessageMode,
        MUIA_Frame,                   MUIV_Frame_InputList,
        GA_Left,   0, GA_Top, 0, GA_Width, 0, GA_Height, 0,
        GA_ID,                        GID_EDITOR,
        ICA_TARGET,                   (ULONG)MmgTargetInstance,
        UTED_PointSize,               (ULONG)pointSize,
        UTED_AddFont,                 (ULONG)fontPath,
        UTED_AddFont,                 (ULONG)emojiFontPath,
        UTED_KeyMessageMode,          UKM_Internal,
        UTED_BevelStyle,              BVS_NONE,
        UTED_LeftMargin,              2,
        UTED_RightMargin,             0,
        UTED_TopMargin,               0,
        UTED_BottomMargin,            0,
        UTED_DisplayInternalVScroll,  FALSE, /* external MUI scrollbars used */
        TAG_DONE);

    /* ------------------------------------------------------------------ */
    /* Status bar                                                           */
    /* ------------------------------------------------------------------ */
    statusObj = MUI_NewObjectB(MUIC_Text,
        MUIA_Text_Contents, (ULONG)LOC(MSG_STATUS_READY),
        MUIA_Text_PreParse, (ULONG)"\033l",          /* left-align */
        MUIA_Frame,         MUIV_Frame_None,
        TAG_DONE);

    /* ------------------------------------------------------------------ */
    /* scrollers                                                           */
    /* ------------------------------------------------------------------ */

    hScrollBar = MUI_NewObjectB(MUIC_Scrollbar,
        MUIA_Group_Horiz,TRUE,
        TAG_DONE);

     vScrollBar = MUI_NewObjectB(MUIC_Scrollbar,
         TAG_DONE);


    editorAndVScrollerGroup = MUI_NewObjectB(MUIC_Group,
            MUIA_Group_Horiz,TRUE,
            MUIA_Group_Spacing,1,
            MUIA_Group_Child, (ULONG)editorObj,
            MUIA_Group_Child, (ULONG)vScrollBar,
            TAG_DONE);

    /* ------------------------------------------------------------------ */
    /* Window: editor+vscroll | hscroll | status                          */
    /* ------------------------------------------------------------------ */
    winObj = MUI_NewObjectB(MUIC_Window,
        MUIA_Window_Title,      (ULONG)"MUImojiGear",
        MUIA_Window_ID,         MAKE_ID('M','M','G','R'),
        MUIA_Window_RootObject, (ULONG)MUI_NewObjectB(MUIC_Group,
            MUIA_Group_Spacing, 0,
            MUIA_Group_Child,   (ULONG)editorAndVScrollerGroup,
            MUIA_Group_Child,   (ULONG)hScrollBar,
            MUIA_Group_Child,   (ULONG)statusObj,
            TAG_DONE),
        TAG_DONE);

    /* ------------------------------------------------------------------ */
    /* Application                                                          */
    /* ------------------------------------------------------------------ */
    appObj = MUI_NewObjectB(MUIC_Application,
        MUIA_Application_Title,       (ULONG)"MUImojiGear",
        MUIA_Application_Version,     (ULONG)MMG_VERSION,
        MUIA_Application_Description, (ULONG)"OS3.9-compatible unicode text editor",
        MUIA_Application_Base,        (ULONG)MMG_APPBASE,
        MUIA_Application_Window,      (ULONG)winObj,
        MUIA_Application_Menustrip,   (ULONG)menustrip,
        TAG_DONE);

    if (!editorObj || !statusObj || !winObj || !appObj) {
        puts("ERROR: failed to build MUI object hierarchy");
        exitCode = 1; goto cleanup;
    }

    /* ------------------------------------------------------------------ */
    /* Notifications                                                        */
    /* ------------------------------------------------------------------ */

    /* Window close button */
    DoMethod(winObj, MUIM_Notify, MUIA_Window_CloseRequest, TRUE,
             appObj, 2, MUIM_Application_ReturnID, RID_QUIT);

    /* Menu items */
    if (miNewFile) DoMethod(miNewFile, MUIM_Notify, MUIA_Menuitem_Trigger, MUIV_EveryTime,
                            appObj, 2, MUIM_Application_ReturnID, RID_NEW_FILE);
    if (miQuit)    DoMethod(miQuit,    MUIM_Notify, MUIA_Menuitem_Trigger, MUIV_EveryTime,
                            appObj, 2, MUIM_Application_ReturnID, RID_QUIT);
    if (miCut)     DoMethod(miCut,     MUIM_Notify, MUIA_Menuitem_Trigger, MUIV_EveryTime,
                            appObj, 2, MUIM_Application_ReturnID, RID_CUT);
    if (miCopy)    DoMethod(miCopy,    MUIM_Notify, MUIA_Menuitem_Trigger, MUIV_EveryTime,
                            appObj, 2, MUIM_Application_ReturnID, RID_COPY);
    if (miPaste)   DoMethod(miPaste,   MUIM_Notify, MUIA_Menuitem_Trigger, MUIV_EveryTime,
                            appObj, 2, MUIM_Application_ReturnID, RID_PASTE);
    if (miUndo)    DoMethod(miUndo,    MUIM_Notify, MUIA_Menuitem_Trigger, MUIV_EveryTime,
                            appObj, 2, MUIM_Application_ReturnID, RID_UNDO);
    if (miRedo)    DoMethod(miRedo,    MUIM_Notify, MUIA_Menuitem_Trigger, MUIV_EveryTime,
                            appObj, 2, MUIM_Application_ReturnID, RID_REDO);

    /* Scrollbars: MUIA_Prop_First change → apply scroll position to editor.
     * ReturnID routes through the main event loop so SetGadgetAttrs is safe. */
    if (vScrollBar) DoMethod(vScrollBar, MUIM_Notify, MUIA_Prop_First, MUIV_EveryTime,
                             appObj, 2, MUIM_Application_ReturnID, RID_VSCROLL);
    if (hScrollBar) DoMethod(hScrollBar, MUIM_Notify, MUIA_Prop_First, MUIV_EveryTime,
                             appObj, 2, MUIM_Application_ReturnID, RID_HSCROLL);

    /* Initial sync: populate scrollbar domains once the window is open. */
    syncVScrollbar();
    syncHScrollbar();

    /* ------------------------------------------------------------------ */
    /* Open window and give the editor initial keyboard focus              */
    /* ------------------------------------------------------------------ */
    SetAttrs(winObj,
        MUIA_Window_Open,         TRUE,
        MUIA_Window_ActiveObject, (ULONG)editorObj,
        TAG_DONE);

    /* ------------------------------------------------------------------ */
    /* Event loop                                                          */
    /* ------------------------------------------------------------------ */
    {
        ULONG sigs    = 0;
        BOOL  running = TRUE;

        while (running) {
            ULONG r = DoMethod(appObj, MUIM_Application_NewInput, &sigs);

            switch (r) {
            case RID_QUIT:
            case MUIV_Application_ReturnID_Quit:
                running = FALSE;
                break;

            case RID_NEW_FILE: {
                /* Clear the editor for a fresh document */
                struct Gadget *g; struct Window *w;
                getRawEditorWin(&g, &w);
                if (g && w) {
                    SetGadgetAttrs(g, w, NULL, UTED_Text, (ULONG)"", TAG_DONE);
                    RefreshGList(g, w, NULL, 1);
                    updateStatus();
                }
                break;
            }
            case RID_CUT:       doEditAction(UTED_ApplyCut);   break;
            case RID_COPY:      doEditAction(UTED_ApplyCopy);  break;
            case RID_PASTE:     doEditAction(UTED_ApplyPaste); break;
            case RID_UNDO:      doEditAction(UTED_Undo);  break;
            case RID_REDO:      doEditAction(UTED_Redo);  break;

            case RID_VSCROLL: {
                /* User moved the vertical scrollbar: apply to editor */
                ULONG newTop = 0;
                struct Gadget *g; struct Window *w;
                GetAttr(MUIA_Prop_First, vScrollBar, &newTop);
                getRawEditorWin(&g, &w);
                if (g && w) {
                    SetGadgetAttrs(g, w, NULL, UTED_ScrollTop, newTop, TAG_DONE);
                    RefreshGList(g, w, NULL, 1);
                }
                break;
            }

            case RID_HSCROLL: {
                /* User moved the horizontal scrollbar: apply to editor */
                ULONG newLeft = 0;
                struct Gadget *g; struct Window *w;
                GetAttr(MUIA_Prop_First, hScrollBar, &newLeft);
                getRawEditorWin(&g, &w);
                if (g && w) {
                    SetGadgetAttrs(g, w, NULL, UTED_ScrollLeft, newLeft, TAG_DONE);
                    RefreshGList(g, w, NULL, 1);
                }
                break;
            }
            }

            if (!running) break;

            if (sigs) {
                sigs = Wait(sigs | SIGBREAKF_CTRL_C | SIGBREAKF_CTRL_F);

                if (sigs & SIGBREAKF_CTRL_C) {
                    running = FALSE;
                    break;
                }

                /* Drain BOOPSI notification queue from mmgboopsimessage */
                if (sigs & SIGBREAKF_CTRL_F) {
                    struct TagItem *msg;
                    struct Gadget  *g = NULL;
                    struct Window  *w = NULL;
                    BOOL needRedraw   = FALSE;
                    BOOL needStatus   = FALSE;

                    getRawEditorWin(&g, &w);

                    while ((msg = MmgBoopsi_NextMessage()) != NULL) {
                        if (FindTagItem(UTEDN_CursorMoved,  msg) ||
                            FindTagItem(UTEDN_ScrollChanged, msg) ||
                            FindTagItem(UTED_ScrollLeft,     msg))
                            needRedraw = TRUE;

                        if (FindTagItem(UTEDN_TextChanged, msg) ||
                            FindTagItem(UTED_Modified,     msg))
                        {
                            needRedraw = TRUE;
                            needStatus = TRUE;
                        }
                    }

                    if (needRedraw && g && w) {
                        RefreshGList(g, w, NULL, 1);
                        /* Sync scrollbar positions after any scroll/cursor change.
                         * MUI suppresses the Prop_First notification when the value
                         * is unchanged, so this won't feed back into RID_VSCROLL. */
                        syncVScrollbar();
                        syncHScrollbar();
                    }

                    if (needStatus)
                        updateStatus();
                }
            }
        }
    }

    /* ------------------------------------------------------------------ */
cleanup:
    /* ------------------------------------------------------------------ */

    if (appObj)
        MUI_DisposeObject(appObj);  /* cascades through window → all children */
    else if (winObj)
        MUI_DisposeObject(winObj);

    MmgBoopsi_Close();

    EgLocale_Close();
    ToolTypePrefs_Close();

    if (MUIMasterBase)     CloseLibrary(MUIMasterBase);
    if (UniTextEditorBase) CloseLibrary(UniTextEditorBase);
    if (IconBase)          CloseLibrary(IconBase);
    if (LocaleBase)        CloseLibrary((struct Library *)LocaleBase);
    if (IntuitionBase)     CloseLibrary((struct Library *)IntuitionBase);
    if (GfxBase)           CloseLibrary((struct Library *)GfxBase);
    if (UtilityBase)       CloseLibrary(UtilityBase);

    return exitCode;
}
