/*
 * muimojiGear.c – MUImojiGear: OS3.9-compatible unicode text editor
 *
 * Uses MUI (muimaster.library v19 / MUI 3.8+) for the UI.
 *
 * Window layout:
 *   MUIC_Application
 *     MUIC_Window
 *       MUIC_Group  (vertical, spacing=0)
 *         MUIC_Group  (horizontal, editor + vScrollBar)
 *         MUIC_Scrollbar  (hScrollBar)
 *         MUIC_Text       (status bar)
 *
 * All application state lives in the single `app` struct (muimojiGear.h).
 * Other modules (mmgaction.c, mmgsettings.c) access it via `extern struct App *app`.
 *
 * BOOPSI notifications → mmgboopsimessage.c (ICA_TARGET = MmgTargetInstance)
 * File load / save     → mmgaction.c
 * Settings             → mmgsettings.c
 * Localisation         → EmojiGear/eglocale.c
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
#include <proto/asl.h>
#include <proto/muimaster.h>
#include <proto/alib.h>
#include <libraries/locale.h>
#include <libraries/mui.h>
#include <libraries/gadtools.h>   /* NM_BARLABEL */
#include <workbench/workbench.h>
#include <workbench/startup.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gadgets/unitexteditor.h>
#include <proto/unitexteditor.h>
#include <gadgets/unibutton.h>
#include <proto/unibutton.h>

#include "../EmojiGear/eglocale.h"
#include "../EmojiGear/tooltypepref.h"
#include "../EmojiGear/egpipeinput.h"
#include "../EmojiGear/unicodeset.h"
#include "mmgboopsimessage.h"
#include "mmgemojibox.h"
#include "mmgfontsview.h"
#include "mmgaction.h"
#include "muimojiGear.h"   /* struct App, app, App_GetRawEditorWin, App_UpdateStatus */

//was a test #define DISABLE_EMOJIBOX 1

/* =========================================================================
 * Library bases – must be global for the SDK proto/inline headers
 * =========================================================================
 */
struct Library       *MUIMasterBase     = NULL;
struct GfxBase       *GfxBase           = NULL;
struct IntuitionBase *IntuitionBase     = NULL;
struct Library       *UtilityBase       = NULL;
struct LocaleBase    *LocaleBase        = NULL;
struct Library       *IconBase          = NULL;
struct Library       *UniTextEditorBase = NULL;
struct Library       *UniButtonBase     = NULL;
struct Library       *AslBase           = NULL;

/* Exposed to mmgboopsimessage.c */
struct Task *mmgMainTask = NULL;

/* Global application state – allocated in main(), accessed by all modules */
struct App *app = NULL;

/* =========================================================================
 * GCC-safe MUI_NewObject wrapper (noinline forces a real m68k stack frame)
 * =========================================================================
 */
Object * __attribute__((noinline))
MUI_NewObjectB(const char *cl, Tag tags, ...)
{
    return MUI_NewObjectA(cl, (struct TagItem *) &tags);
}

/* =========================================================================
 * Application constants
 * =========================================================================
 */
#define MMG_APPBASE       "MUIMOJIGEAR"
#define PIPE_INPUT_BUF    1024
#define MMG_VERSION  "$VER: MUImojiGear 4.3 (2026)"
#define GID_EDITOR        1
#define GID_EMOJI_BUTTON  2
#define GID_EMOJI_GRID    3

/* MUIM_Application_ReturnID values */
#define RID_QUIT        10
#define RID_NEW_FILE    12
#define RID_CUT         13
#define RID_COPY        14
#define RID_PASTE       15
#define RID_UNDO        16
#define RID_REDO        17
#define RID_VSCROLL     18
#define RID_HSCROLL     19
#define RID_LOAD_UTF8   20
#define RID_LOAD_LATIN1 21
#define RID_LOAD_LATIN2 22
#define RID_SAVE_UTF8   23
#define RID_SAVE_LATIN1 24
#define RID_SAVE_LATIN2 25
#define RID_RESIZE      26
#define RID_COPY_LAT1   27
#define RID_COPY_LAT2   28
#define RID_PASTE_LAT1  29
#define RID_PASTE_LAT2  30
#define RID_FONTSIZE_P         31   /* Font size +  (Amiga++) */
#define RID_FONTSIZE_M         32   /* Font size -  (Amiga+-) */
#define RID_TOGGLE_ANTIALIAS   33
#define RID_TOGGLE_WORDWRAP    34
#define RID_TOGGLE_APPLYANSI   35
#define RID_TOGGLE_VIZTABS     36
#define RID_TOGGLE_TABSSPACES  37
#define RID_TOGGLE_MONOSPACE   81
#define RID_TOGGLE_DISPLAYUNICODEINFO 82
#define RID_COLOR_BASE         38   /* 38..43: one per color preset (0=System colors) */
#define MMG_NUM_COLOR_PRESETS   6
#define RID_RECENT_BASE        44   /* 44..51: one per recent slot  */

void App_GetRawEditorWin(struct Gadget **gOut, struct Window **wOut)
{
    *gOut = NULL; *wOut = NULL;
    if (app && app->editorObj) GetAttr(MUIA_Boopsi_Object, app->editorObj, (ULONG *)gOut);
    if (app && app->winObj)    GetAttr(MUIA_Window_Window,  app->winObj,   (ULONG *)wOut);
}

void App_UpdateStatus(void)
{
    static char buf[128];
    struct Gadget *g = NULL; struct Window *w = NULL;
    ULONG modified = 0, curLine = 0, curCol = 0, curChar = 0, lineCount = 0;
    App_GetRawEditorWin(&g, &w);
    if (g) {
        GetAttr(UTED_Modified,    (Object *)g, &modified);
        GetAttr(UTED_CursorLine,  (Object *)g, &curLine);
        GetAttr(UTED_CursorColumn,(Object *)g, &curCol);
        GetAttr(UTED_LineCount,   (Object *)g, &lineCount);
        if (app && app->settings.displayUnicodeInfo)
            GetAttr(UTED_CursorChar, (Object *)g, &curChar);
    }
    if (app && app->settings.displayUnicodeInfo) {
        snprintf(buf, sizeof(buf) - 1, " Line %lu, Col %lu  |  %lu lines  |  U+%06lX (%s)",
            /*no need     modified ? LOC(MSG_STATUS_MODIFIED) : LOC(MSG_STATUS_READY),*/
                 curLine + 1, curCol + 1, lineCount, curChar, find_unicode_set(curChar));
    } else {
        snprintf(buf, sizeof(buf) - 1, " Line %lu, Col %lu  |  %lu lines",
                 curLine + 1, curCol + 1, lineCount);
    }
    buf[sizeof(buf) - 1] = '\0';
    if (app && app->statusObj)
        SetAttrs(app->statusObj, MUIA_Text_Contents, (ULONG)buf, TAG_DONE);
}

/* =========================================================================
 * Scrollbar sync helpers
 * =========================================================================
 */

static void syncVScrollbar(void)
{
    ULONG top = 0, total = 1, visible = 1;
    struct Gadget *g = NULL; struct Window *w = NULL;
    App_GetRawEditorWin(&g, &w);
    if (!g || !app->vScrollBar) return;
    GetAttr(UTED_ScrollTop,         (Object *)g, &top);
    GetAttr(UTED_RenderedLineCount, (Object *)g, &total);
    GetAttr(UTED_VisibleLines,      (Object *)g, &visible);
    if (total   < 1) total   = 1;
    if (visible < 1) visible = 1;
    SetAttrs(app->vScrollBar,
        MUIA_Prop_First,   top,
        MUIA_Prop_Entries, total,
        MUIA_Prop_Visible, visible,
        TAG_DONE);
}

static void syncHScrollbar(void)
{
    ULONG left = 0, maxWidth = 0, visible = 1;
    struct Gadget *g = NULL; struct Window *w = NULL;
    App_GetRawEditorWin(&g, &w);
    if (!g || !app->hScrollBar) return;
    GetAttr(UTED_ScrollLeft,   (Object *)g, &left);
    GetAttr(UTED_MaxLineWidth, (Object *)g, &maxWidth);
    GetAttr(GA_Width,          (Object *)g, &visible);

  //  printf("ml:%d w:%d gw:%d\n",maxWidth,visible,(int)g->Width);

    if (visible == 0) return;
    if (maxWidth < visible) maxWidth = visible;
    SetAttrs(app->hScrollBar,
        MUIA_Prop_First,   left,
        MUIA_Prop_Entries, maxWidth,
        MUIA_Prop_Visible, visible,
        TAG_DONE);
}

void App_SyncScrollbars(void) { syncVScrollbar(); syncHScrollbar(); }

/* =========================================================================
 * Edit dispatch
 * =========================================================================
 */

/* Re-activate the editor after a menu action stripped BOOPSI focus.
 * MUIA_Window_ActiveObject requires MUIA_CycleChain=1 on the editor (set at
 * creation) to work; it lets MUI handle the activation at the right moment,
 * after it has finished processing any other pending IDCMP events. */
void App_ReactivateEditor(void)
{
    if (app && app->winObj && app->editorObj)
        SetAttrs(app->winObj, MUIA_Window_ActiveObject,
                 (ULONG)app->editorObj, TAG_DONE);
}

static void doEditAction(ULONG attr)
{
    struct Gadget *g; struct Window *w;
    App_GetRawEditorWin(&g, &w);
    if (g && w) {
        SetGadgetAttrs(g, w, NULL, attr, (ULONG)TRUE, TAG_DONE);
       // App_ReactivateEditor();
    }
}

/* #define STACK_WATCH 1 */
#define MMG_MIN_STACK (28 * 1024)

/* =========================================================================
 * main
 * =========================================================================
 */
int main(int argc, char *argv[])
{
    int    exitCode = 0;

    /* Intermediate layout objects (not stored in app – not needed after init) */
    Object *editorAndVGroup = NULL;
    Object *statusBarGroup  = NULL;
    Object *menustrip       = NULL;
    Object *menuProj        = NULL;
    Object *menuEdit        = NULL;
    Object *menuSettings    = NULL;
    Object *menuColors      = NULL;

    /* ------------------------------------------------------------------ */
    /* Stack size check                                                     */
    /* ------------------------------------------------------------------ */
    {
        struct Task *_t      = FindTask(NULL);
        int          _stacksize = (int)((int)_t->tc_SPReg - (int)_t->tc_SPLower);
        if (_stacksize < MMG_MIN_STACK) {
            puts("MUImojiGear needs at least 32k stack. Use \"stack 32768\" or set it in the icon properties.");
            return 1;
        }
#ifdef STACK_WATCH
        {
            int   _sw_anchor = 0;
            int  *_sw_near   = (int *)((int)&_sw_anchor - 64);
            int  *_sw_far    = (int *)((int)_t->tc_SPLower + 4);
            int   _sw_i;
            for (_sw_i = 0; _sw_i < ((int)_sw_near - (int)_sw_far) / (int)sizeof(int); _sw_i++)
                _sw_far[_sw_i] = (int)0xCAFEBABE;
        }
#endif
    }

    /* ------------------------------------------------------------------ */
    /* Libraries                                                            */
    /* ------------------------------------------------------------------ */

    GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 39L);
    if (!GfxBase) { puts("ERROR: graphics.library v39+"); exitCode=1; goto cleanup; }

    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39L);
    if (!IntuitionBase) { puts("ERROR: intuition.library v39+"); exitCode=1; goto cleanup; }

    UtilityBase = OpenLibrary("utility.library", 37L);
    if (!UtilityBase) { puts("ERROR: utility.library v37+"); exitCode=1; goto cleanup; }

    MUIMasterBase = OpenLibrary("muimaster.library", 19L);
    if (!MUIMasterBase) { puts("ERROR: muimaster.library v19+ (MUI 3.8+)"); exitCode=1; goto cleanup; }

    UniTextEditorBase = OpenLibrary("gadgets/unitexteditor.gadget", 4L);
    if (!UniTextEditorBase) { puts("ERROR: unitexteditor.gadget v4+"); exitCode=1; goto cleanup; }

    UniButtonBase = OpenLibrary("gadgets/unibutton.gadget", 4L);
    if (!UniButtonBase) { puts("ERROR: unibutton.gadget v4+"); exitCode=1; goto cleanup; }

    AslBase = OpenLibrary("asl.library", 39UL);
    if (!AslBase) { puts("ERROR: asl.library v39"); exitCode=1; goto cleanup; }

    LocaleBase = (struct LocaleBase *)OpenLibrary("locale.library", 38L);
    if (!LocaleBase) { puts("ERROR: locale.library v38"); exitCode=1; goto cleanup; }

    IconBase   = OpenLibrary("icon.library", 37L);

    /* ------------------------------------------------------------------ */
    /* Allocate app struct                                                  */
    /* ------------------------------------------------------------------ */
    app = (struct App *)AllocVec(sizeof(struct App), MEMF_ANY | MEMF_CLEAR);
    if (!app) { puts("ERROR: out of memory"); exitCode=1; goto cleanup; }

    /* ------------------------------------------------------------------ */
    /* Locale + settings                                                    */
    /* ------------------------------------------------------------------ */
    EgLocale_Init("muimojiGear.catalog", 1);

    if (IconBase) {
        if (argc > 0) {
            ToolTypePrefs_Init(argv[0]);
        } else if (argc == 0 && argv != NULL) {
            /* Launched from Workbench: argv is actually a WBStartup pointer */
            struct WBStartup *wbmsg = (struct WBStartup *)argv;
            struct WBArg     *wbarg = wbmsg->sm_ArgList;
            int i;
            for (i = 0; i < (int)wbmsg->sm_NumArgs; i++, wbarg++)
                if (*wbarg->wa_Name && ToolTypePrefs_Init(wbarg->wa_Name)) break;
        }
    }
    AppSettings_Load(&app->settings);

    /* ------------------------------------------------------------------ */
    /* BOOPSI notification target                                           */
    /* ------------------------------------------------------------------ */
    mmgMainTask = FindTask(NULL);
    if (!MmgBoopsi_Init()) {
        puts("ERROR: MmgBoopsi_Init failed");
        exitCode = 1; goto cleanup;
    }

    /* ------------------------------------------------------------------ */
    /* Emoji box window (built before Application so it can be SubWindow)  */
    /* ------------------------------------------------------------------ */
#ifndef DISABLE_EMOJIBOX
    MmgEmojiBox_BuildWindow();   /* stores result in app->emojiBoxWinObj */
#endif
    MmgFontsView_BuildWindow();  /* stores result in app->fontViewWinObj  */
    /* ------------------------------------------------------------------ */
    /* MUI menus                                                            */
    /* ------------------------------------------------------------------ */
    app->miNewFile  = MUI_NewObjectB(MUIC_Menuitem, MUIA_Menuitem_Title, (ULONG)LOC(MSG_FILE_NEW),
                                     MUIA_Menuitem_Shortcut, (ULONG)"N", TAG_DONE);
    app->miLoadUTF8 = MUI_NewObjectB(MUIC_Menuitem, MUIA_Menuitem_Title, (ULONG)LOC(MSG_FILE_LOAD_UTF8),
                                     MUIA_Menuitem_Shortcut, (ULONG)"O", TAG_DONE);
    app->miLoadLat1 = MUI_NewObjectB(MUIC_Menuitem, MUIA_Menuitem_Title, (ULONG)LOC(MSG_FILE_LOAD_LATIN1), TAG_DONE);
    app->miLoadLat2 = MUI_NewObjectB(MUIC_Menuitem, MUIA_Menuitem_Title, (ULONG)LOC(MSG_FILE_LOAD_LATIN2), TAG_DONE);
    app->miSaveUTF8 = MUI_NewObjectB(MUIC_Menuitem, MUIA_Menuitem_Title, (ULONG)LOC(MSG_FILE_SAVE_UTF8),
                                     MUIA_Menuitem_Shortcut, (ULONG)"S", TAG_DONE);
    app->miSaveLat1 = MUI_NewObjectB(MUIC_Menuitem, MUIA_Menuitem_Title, (ULONG)LOC(MSG_FILE_SAVE_LATIN1), TAG_DONE);
    app->miSaveLat2 = MUI_NewObjectB(MUIC_Menuitem, MUIA_Menuitem_Title, (ULONG)LOC(MSG_FILE_SAVE_LATIN2), TAG_DONE);
    app->miQuit     = MUI_NewObjectB(MUIC_Menuitem, MUIA_Menuitem_Title, (ULONG)LOC(MSG_MENU_QUIT),
                                     MUIA_Menuitem_Shortcut, (ULONG)"Q", TAG_DONE);

    /* Open Recent submenu – pre-allocate APPSETTINGS_MAX_RECENT slots,
     * all disabled initially; MmgAction_RebuildRecentMenu() fills them. */
    {
        int i;
        for (i = 0; i < APPSETTINGS_MAX_RECENT; i++) {
            app->recentItemLabels[i][0] = '\0';
            app->miRecentItem[i] = MUI_NewObjectB(MUIC_Menuitem,
                MUIA_Menuitem_Title,   (ULONG)app->recentItemLabels[i],
                MUIA_Menuitem_Enabled, FALSE,
                TAG_DONE);
        }
        app->miOpenRecent = MUI_NewObjectB(MUIC_Menuitem,
            MUIA_Menuitem_Title,   (ULONG)LOC(MSG_OPEN_RECENT),
            MUIA_Menuitem_Enabled, FALSE,
            MUIA_Family_Child, (ULONG)app->miRecentItem[0],
            MUIA_Family_Child, (ULONG)app->miRecentItem[1],
            MUIA_Family_Child, (ULONG)app->miRecentItem[2],
            MUIA_Family_Child, (ULONG)app->miRecentItem[3],
            MUIA_Family_Child, (ULONG)app->miRecentItem[4],
            MUIA_Family_Child, (ULONG)app->miRecentItem[5],
            MUIA_Family_Child, (ULONG)app->miRecentItem[6],
            MUIA_Family_Child, (ULONG)app->miRecentItem[7],
            TAG_DONE);
    }

    menuProj = MUI_NewObjectB(MUIC_Menu,
        MUIA_Menu_Title,   (ULONG)LOC(MSG_MENU_PROJECT),
        MUIA_Family_Child, (ULONG)app->miNewFile,
        MUIA_Family_Child, (ULONG)MUI_NewObjectB(MUIC_Menuitem, MUIA_Menuitem_Title, (ULONG)NM_BARLABEL, TAG_DONE),
        MUIA_Family_Child, (ULONG)app->miLoadUTF8,
        MUIA_Family_Child, (ULONG)app->miLoadLat1,
        MUIA_Family_Child, (ULONG)app->miLoadLat2,
        MUIA_Family_Child, (ULONG)app->miOpenRecent,
        MUIA_Family_Child, (ULONG)MUI_NewObjectB(MUIC_Menuitem, MUIA_Menuitem_Title, (ULONG)NM_BARLABEL, TAG_DONE),
        MUIA_Family_Child, (ULONG)app->miSaveUTF8,
        MUIA_Family_Child, (ULONG)app->miSaveLat1,
        MUIA_Family_Child, (ULONG)app->miSaveLat2,
        MUIA_Family_Child, (ULONG)MUI_NewObjectB(MUIC_Menuitem, MUIA_Menuitem_Title, (ULONG)NM_BARLABEL, TAG_DONE),
        MUIA_Family_Child, (ULONG)app->miQuit,
        TAG_DONE);

    app->miCut       = MUI_NewObjectB(MUIC_Menuitem, MUIA_Menuitem_Title, (ULONG)LOC(MSG_EDIT_CUT),
                                      MUIA_Menuitem_Shortcut, (ULONG)"X", TAG_DONE);
    app->miCopy      = MUI_NewObjectB(MUIC_Menuitem, MUIA_Menuitem_Title, (ULONG)LOC(MSG_EDIT_COPY),
                                      MUIA_Menuitem_Shortcut, (ULONG)"C", TAG_DONE);
    app->miCopyLat1  = MUI_NewObjectB(MUIC_Menuitem, MUIA_Menuitem_Title, (ULONG)LOC(MSG_EDIT_COPY_LATIN1), TAG_DONE);
    app->miCopyLat2  = MUI_NewObjectB(MUIC_Menuitem, MUIA_Menuitem_Title, (ULONG)LOC(MSG_EDIT_COPY_LATIN2), TAG_DONE);
    app->miPaste     = MUI_NewObjectB(MUIC_Menuitem, MUIA_Menuitem_Title, (ULONG)LOC(MSG_EDIT_PASTE),
                                      MUIA_Menuitem_Shortcut, (ULONG)"V", TAG_DONE);
    app->miPasteLat1 = MUI_NewObjectB(MUIC_Menuitem, MUIA_Menuitem_Title, (ULONG)LOC(MSG_EDIT_PASTE_LATIN1), TAG_DONE);
    app->miPasteLat2 = MUI_NewObjectB(MUIC_Menuitem, MUIA_Menuitem_Title, (ULONG)LOC(MSG_EDIT_PASTE_LATIN2), TAG_DONE);
    app->miUndo      = MUI_NewObjectB(MUIC_Menuitem, MUIA_Menuitem_Title, (ULONG)LOC(MSG_EDIT_UNDO),
                                      MUIA_Menuitem_Shortcut, (ULONG)"Z", TAG_DONE);
    app->miRedo      = MUI_NewObjectB(MUIC_Menuitem, MUIA_Menuitem_Title, (ULONG)LOC(MSG_EDIT_REDO),
                                      MUIA_Menuitem_Shortcut, (ULONG)"Y", TAG_DONE);
    app->miEmojiBox  = MUI_NewObjectB(MUIC_Menuitem, MUIA_Menuitem_Title, (ULONG)"Emoji Box",
                                      MUIA_Menuitem_Shortcut, (ULONG)"E", TAG_DONE);
    app->miFontSettings = MUI_NewObjectB(MUIC_Menuitem, MUIA_Menuitem_Title, (ULONG)"Font Settings",
                                      MUIA_Menuitem_Shortcut, (ULONG)"F", TAG_DONE);
    menuEdit = MUI_NewObjectB(MUIC_Menu,
        MUIA_Menu_Title,   (ULONG)LOC(MSG_MENU_EDIT),
        MUIA_Family_Child, (ULONG)app->miCut,
        MUIA_Family_Child, (ULONG)app->miCopy,
        MUIA_Family_Child, (ULONG)app->miCopyLat1,
        MUIA_Family_Child, (ULONG)app->miCopyLat2,
        MUIA_Family_Child, (ULONG)MUI_NewObjectB(MUIC_Menuitem, MUIA_Menuitem_Title, (ULONG)NM_BARLABEL, TAG_DONE),
        MUIA_Family_Child, (ULONG)app->miPaste,
        MUIA_Family_Child, (ULONG)app->miPasteLat1,
        MUIA_Family_Child, (ULONG)app->miPasteLat2,
        MUIA_Family_Child, (ULONG)MUI_NewObjectB(MUIC_Menuitem, MUIA_Menuitem_Title, (ULONG)NM_BARLABEL, TAG_DONE),
        MUIA_Family_Child, (ULONG)app->miUndo,
        MUIA_Family_Child, (ULONG)app->miRedo,
        MUIA_Family_Child, (ULONG)MUI_NewObjectB(MUIC_Menuitem, MUIA_Menuitem_Title, (ULONG)NM_BARLABEL, TAG_DONE),
        MUIA_Family_Child, (ULONG)app->miEmojiBox,
        TAG_DONE);
    app->miSettFontSizePlus  = MUI_NewObjectB(MUIC_Menuitem,
        MUIA_Menuitem_Title,    (ULONG)LOC(MSG_SETTING_FONTSIZEP),
        MUIA_Menuitem_Shortcut, (ULONG)"+",
        TAG_DONE);
    app->miSettFontSizeMinus = MUI_NewObjectB(MUIC_Menuitem,
        MUIA_Menuitem_Title,    (ULONG)LOC(MSG_SETTING_FONTSIZEM),
        MUIA_Menuitem_Shortcut, (ULONG)"-",
        TAG_DONE);

    /* Checkit + Toggle items: Checkit shows the checkmark area, Toggle sets
     * the Intuition MENUTOGGLE flag so the checked state flips on every
     * selection.  Without Toggle, Intuition only ever sets the mark, never
     * clears it.  Initial state comes from the loaded AppSettings. */
    app->miToggleAntialias = MUI_NewObjectB(MUIC_Menuitem,
        MUIA_Menuitem_Title,   (ULONG)LOC(MSG_FONTSETTINGS_ANTIALIAS),
        MUIA_Menuitem_Checkit, TRUE,
        MUIA_Menuitem_Toggle,  TRUE,
        MUIA_Menuitem_Checked, (ULONG)(app->settings.antialias    ? TRUE : FALSE),
        TAG_DONE);
    app->miToggleMonospace = MUI_NewObjectB(MUIC_Menuitem,
        MUIA_Menuitem_Title,   (ULONG)"Force Monospace",
        MUIA_Menuitem_Checkit, TRUE,
        MUIA_Menuitem_Toggle,  TRUE,
        MUIA_Menuitem_Checked, (ULONG)(app->settings.monospace ? TRUE : FALSE),
        TAG_DONE);
    app->miToggleWordWrap = MUI_NewObjectB(MUIC_Menuitem,
        MUIA_Menuitem_Title,   (ULONG)LOC(MSG_SETTINGS_WORDWRAP),
        MUIA_Menuitem_Checkit, TRUE,
        MUIA_Menuitem_Toggle,  TRUE,
        MUIA_Menuitem_Checked, (ULONG)(app->settings.wordWrap     ? TRUE : FALSE),
        TAG_DONE);
    app->miToggleApplyAnsi = MUI_NewObjectB(MUIC_Menuitem,
        MUIA_Menuitem_Title,   (ULONG)LOC(MSG_SETTINGS_APPLYANSI),
        MUIA_Menuitem_Checkit, TRUE,
        MUIA_Menuitem_Toggle,  TRUE,
        MUIA_Menuitem_Checked, (ULONG)(app->settings.applyAnsi    ? TRUE : FALSE),
        TAG_DONE);
    app->miToggleVisualizeTabs = MUI_NewObjectB(MUIC_Menuitem,
        MUIA_Menuitem_Title,   (ULONG)LOC(MSG_SETTINGS_VISUALIZETABS),
        MUIA_Menuitem_Checkit, TRUE,
        MUIA_Menuitem_Toggle,  TRUE,
        MUIA_Menuitem_Checked, (ULONG)(app->settings.visualizeTabs ? TRUE : FALSE),
        TAG_DONE);
    app->miToggleTabsAreSpaces = MUI_NewObjectB(MUIC_Menuitem,
        MUIA_Menuitem_Title,   (ULONG)LOC(MSG_SETTINGS_TABSARESPACES),
        MUIA_Menuitem_Checkit, TRUE,
        MUIA_Menuitem_Toggle,  TRUE,
        MUIA_Menuitem_Checked, (ULONG)(app->settings.tabsAreSpaces ? TRUE : FALSE),
        TAG_DONE);
    app->miToggleDisplayUnicodeInfo = MUI_NewObjectB(MUIC_Menuitem,
        MUIA_Menuitem_Title,   (ULONG)LOC(MSG_SETTINGS_DISPLAYUNICODEINFO),
        MUIA_Menuitem_Checkit, TRUE,
        MUIA_Menuitem_Toggle,  TRUE,
        MUIA_Menuitem_Checked, (ULONG)(app->settings.displayUnicodeInfo ? TRUE : FALSE),
        TAG_DONE);

    menuSettings = MUI_NewObjectB(MUIC_Menu,
        MUIA_Menu_Title,   (ULONG)LOC(MSG_SETTINGS),
        MUIA_Family_Child, (ULONG)app->miSettFontSizePlus,
        MUIA_Family_Child, (ULONG)app->miSettFontSizeMinus,
        MUIA_Family_Child, (ULONG)MUI_NewObjectB(MUIC_Menuitem, MUIA_Menuitem_Title, (ULONG)NM_BARLABEL, TAG_DONE),
        MUIA_Family_Child, (ULONG)app->miToggleAntialias,
        MUIA_Family_Child, (ULONG)app->miToggleMonospace,
        MUIA_Family_Child, (ULONG)app->miToggleWordWrap,
        MUIA_Family_Child, (ULONG)app->miToggleApplyAnsi,
        MUIA_Family_Child, (ULONG)app->miToggleVisualizeTabs,
        MUIA_Family_Child, (ULONG)app->miToggleTabsAreSpaces,
        MUIA_Family_Child, (ULONG)app->miToggleDisplayUnicodeInfo,
        MUIA_Family_Child, (ULONG)MUI_NewObjectB(MUIC_Menuitem, MUIA_Menuitem_Title, (ULONG)NM_BARLABEL, TAG_DONE),
        MUIA_Family_Child, (ULONG)app->miFontSettings,
        TAG_DONE);

    /* Colors menu – label strings are the preset descriptions themselves */
    {
        static const char * const presetNames[MMG_NUM_COLOR_PRESETS] = {
            "System colors",        /* idx 0: reads pens 0+1 via FindColor */
            "Grey / Black",
            "Black / White",
            "White / Black",
            "Dark Purple / White",
            "Dark Blue / White",
        };
        int i;
        for (i = 0; i < MMG_NUM_COLOR_PRESETS; i++)
            app->miColorPreset[i] = MUI_NewObjectB(MUIC_Menuitem,
                MUIA_Menuitem_Title, (ULONG)presetNames[i],
                TAG_DONE);

        menuColors = MUI_NewObjectB(MUIC_Menu,
            MUIA_Menu_Title,   (ULONG)"Colors",
            MUIA_Family_Child, (ULONG)app->miColorPreset[0],
            MUIA_Family_Child, (ULONG)MUI_NewObjectB(MUIC_Menuitem, MUIA_Menuitem_Title, (ULONG)NM_BARLABEL, TAG_DONE),
            MUIA_Family_Child, (ULONG)app->miColorPreset[1],
            MUIA_Family_Child, (ULONG)app->miColorPreset[2],
            MUIA_Family_Child, (ULONG)app->miColorPreset[3],
            MUIA_Family_Child, (ULONG)app->miColorPreset[4],
            MUIA_Family_Child, (ULONG)app->miColorPreset[5],
            TAG_DONE);
    }

    menustrip = MUI_NewObjectB(MUIC_Menustrip,
        MUIA_Family_Child, (ULONG)menuProj,
        MUIA_Family_Child, (ULONG)menuEdit,
        MUIA_Family_Child, (ULONG)menuSettings,
        MUIA_Family_Child, (ULONG)menuColors,
        TAG_DONE);

    /* ------------------------------------------------------------------ */
    /* UniTextEditor                                                        */
    /* ------------------------------------------------------------------ */
    app->editorObj = MUI_NewObjectB(MUIC_Boopsi,
        MUIA_Boopsi_Class,            (ULONG)UNITEXTEDITOR_GetClass(),
        MUIA_Boopsi_MinWidth,         200,
        MUIA_Boopsi_MinHeight,        100,
        MUIA_Boopsi_Smart,            TRUE,
        MUIA_Boopsi_Remember,         UTED_PointSize,
        MUIA_Boopsi_Remember,         UTED_KeyMessageMode,
        MUIA_Frame,                   MUIV_Frame_InputList,
//        GA_Left,   0, GA_Top, 0, GA_Width, 0, GA_Height, 0,
        GA_ID,                        GID_EDITOR,
        ICA_TARGET,                   (ULONG)MmgTargetInstance,
        MUIA_CycleChain,              1,
        UTED_KeyMessageMode,          UKM_Internal,
        UTED_InternalRawKey_SendBack, TRUE, /* to get rawkey for function keys */
        UTED_BevelStyle,              BVS_NONE,
        UTED_LeftMargin,              2,
        UTED_RightMargin,             0,
        UTED_TopMargin,               0,
        UTED_BottomMargin,            0,
        UTED_DisplayInternalVScroll,  FALSE,
        TAG_DONE);
    AppSettings_ApplyToEditor(&app->settings, app->editorObj, NULL, NULL);

    /* ------------------------------------------------------------------ */
    /* Status bar + scrollbars + layout                                    */
    /* ------------------------------------------------------------------ */
    app->statusObj = MUI_NewObjectB(MUIC_Text,
        MUIA_Text_Contents, (ULONG)LOC(MSG_STATUS_READY),
        MUIA_Text_PreParse, (ULONG)"\033l",
        MUIA_Frame,         MUIV_Frame_None,
        TAG_DONE);

    app->hScrollBar = MUI_NewObjectB(MUIC_Scrollbar, MUIA_Group_Horiz, TRUE, TAG_DONE);
    app->vScrollBar = MUI_NewObjectB(MUIC_Scrollbar, TAG_DONE);

    /* Emoji button – compact, fixed to minimum size, anchored at right of status bar */
    app->emojiBtnObj = MUI_NewObjectB(MUIC_Boopsi,
        MUIA_Boopsi_Class,     (ULONG)UNIBUTTON_GetClass(),
        MUIA_Boopsi_MinWidth,  24,
        MUIA_Boopsi_MinHeight, 24,
        MUIA_Boopsi_Smart,     TRUE,
        MUIA_Boopsi_Remember,  UBT_PointSize,
        MUIA_Frame,            MUIV_Frame_None,
        MUIA_Weight,           0,
       //no GA_Left,   0, GA_Top, 0, GA_Width, 0, GA_Height, 0,
        GA_ID,                 GID_EMOJI_BUTTON,
        ICA_TARGET,            (ULONG)MmgTargetInstance,
        UBT_BevelStyle,        BVS_BUTTON,
        UBT_PointSize,         16UL,
        UBT_Transparent,       FALSE,
        UBT_LeftMargin,        3UL,
        UBT_RightMargin,       3UL,
        UBT_TopMargin,         0UL,
        UBT_BottomMargin,      3UL,
        GA_Text,               (ULONG)"\xF0\x9F\x98\x80",
        TAG_DONE);

    /* Load emoji + fallback fonts into the button draw context */
    if (app->emojiBtnObj) {
        if (app->settings.emojiFontPath)
            SetAttrs(app->emojiBtnObj, UBT_AddFont, (ULONG)app->settings.emojiFontPath, TAG_DONE);
        if (app->settings.primaryFontPath)
            SetAttrs(app->emojiBtnObj, UBT_AddFont, (ULONG)app->settings.primaryFontPath, TAG_DONE);
    }

    editorAndVGroup = MUI_NewObjectB(MUIC_Group,
        MUIA_Group_Horiz,    TRUE,
        MUIA_Group_Spacing,  1,
        MUIA_Group_Child,    (ULONG)app->editorObj,
        MUIA_Group_Child,    (ULONG)app->vScrollBar,
        TAG_DONE);

    /* Status bar: [status text, fills space] [emoji button, compact right] */
    if (app->emojiBtnObj) {
        statusBarGroup = MUI_NewObjectB(MUIC_Group,
            MUIA_Group_Horiz,   TRUE,
            MUIA_Group_Spacing, 2,
            MUIA_Group_Child,   (ULONG)app->statusObj,
            MUIA_Group_Child,   (ULONG)app->emojiBtnObj,
            TAG_DONE);
    } else {
        statusBarGroup = app->statusObj;
    }

    app->winObj = MUI_NewObjectB(MUIC_Window,
        MUIA_Window_Title,      (ULONG)"MUImojiGear",
        MUIA_Window_ID,         MAKE_ID('M','M','G','R'),
        MUIA_Window_RootObject, (ULONG)MUI_NewObjectB(MUIC_Group,
            MUIA_Group_Spacing, 0,
            MUIA_Group_Child,   (ULONG)editorAndVGroup,
            MUIA_Group_Child,   (ULONG)app->hScrollBar,
            MUIA_Group_Child,   (ULONG)statusBarGroup,
            TAG_DONE),
        TAG_DONE);

    app->appObj = MUI_NewObjectB(MUIC_Application,
        MUIA_Application_Title,       (ULONG)"MUImojiGear",
        MUIA_Application_Version,     (ULONG)MMG_VERSION,
        MUIA_Application_Description, (ULONG)"OS3.9-compatible unicode text editor",
        MUIA_Application_Base,        (ULONG)MMG_APPBASE,
        MUIA_Application_Window,      (ULONG)app->winObj,
 #ifndef DISABLE_EMOJIBOX
        MUIA_Application_Window,      (ULONG)app->emojiBoxWinObj,
 #endif
        MUIA_Application_Window,      (ULONG)app->fontViewWinObj,
        MUIA_Application_Menustrip,   (ULONG)menustrip,
        TAG_DONE);

    if (!app->editorObj || !app->winObj || !app->appObj) {
        puts("ERROR: failed to build MUI object hierarchy");
        exitCode = 1; goto cleanup;
    }

    /* Wire emoji box MUI notifications now that appObj exists */
#ifndef DISABLE_EMOJIBOX
    MmgEmojiBox_Init();
#endif
    MmgFontsView_Init();
    /* ------------------------------------------------------------------ */
    /* Notifications                                                        */
    /* ------------------------------------------------------------------ */

    DoMethod(app->winObj, MUIM_Notify, MUIA_Window_CloseRequest, TRUE,
             app->appObj, 2, MUIM_Application_ReturnID, RID_QUIT);

#define NOTIFY(obj, rid) \
    if (app->obj) DoMethod(app->obj, MUIM_Notify, MUIA_Menuitem_Trigger, MUIV_EveryTime, \
                           app->appObj, 2, MUIM_Application_ReturnID, rid)

    NOTIFY(miNewFile,  RID_NEW_FILE);
    NOTIFY(miLoadUTF8, RID_LOAD_UTF8);
    NOTIFY(miLoadLat1, RID_LOAD_LATIN1);
    NOTIFY(miLoadLat2, RID_LOAD_LATIN2);
    NOTIFY(miSaveUTF8, RID_SAVE_UTF8);
    NOTIFY(miSaveLat1, RID_SAVE_LATIN1);
    NOTIFY(miSaveLat2, RID_SAVE_LATIN2);
    NOTIFY(miQuit,     RID_QUIT);
    NOTIFY(miCut,       RID_CUT);
    NOTIFY(miCopy,      RID_COPY);
    NOTIFY(miCopyLat1,  RID_COPY_LAT1);
    NOTIFY(miCopyLat2,  RID_COPY_LAT2);
    NOTIFY(miPaste,     RID_PASTE);
    NOTIFY(miPasteLat1, RID_PASTE_LAT1);
    NOTIFY(miPasteLat2, RID_PASTE_LAT2);
    NOTIFY(miUndo,             RID_UNDO);
    NOTIFY(miRedo,             RID_REDO);
    NOTIFY(miEmojiBox,         RID_EMOJI_BOX_MENU);
    NOTIFY(miFontSettings,     RID_FONTS_WIN);
    NOTIFY(miSettFontSizePlus,    RID_FONTSIZE_P);
    NOTIFY(miSettFontSizeMinus,   RID_FONTSIZE_M);
    NOTIFY(miToggleAntialias,     RID_TOGGLE_ANTIALIAS);
    NOTIFY(miToggleMonospace,     RID_TOGGLE_MONOSPACE);
    NOTIFY(miToggleWordWrap,      RID_TOGGLE_WORDWRAP);
    NOTIFY(miToggleApplyAnsi,     RID_TOGGLE_APPLYANSI);
    NOTIFY(miToggleVisualizeTabs, RID_TOGGLE_VIZTABS);
    NOTIFY(miToggleTabsAreSpaces, RID_TOGGLE_TABSSPACES);
    NOTIFY(miToggleDisplayUnicodeInfo, RID_TOGGLE_DISPLAYUNICODEINFO);

    {
        int i;
        for (i = 0; i < APPSETTINGS_MAX_RECENT; i++)
            if (app->miRecentItem[i])
                DoMethod(app->miRecentItem[i], MUIM_Notify,
                         MUIA_Menuitem_Trigger, MUIV_EveryTime,
                         app->appObj, 2, MUIM_Application_ReturnID,
                         (ULONG)(RID_RECENT_BASE + i));
    }

    {
        int i;
        for (i = 0; i < MMG_NUM_COLOR_PRESETS; i++)
            if (app->miColorPreset[i])
                DoMethod(app->miColorPreset[i], MUIM_Notify,
                         MUIA_Menuitem_Trigger, MUIV_EveryTime,
                         app->appObj, 2, MUIM_Application_ReturnID,
                         (ULONG)(RID_COLOR_BASE + i));
    }
#undef NOTIFY

    if (app->vScrollBar) DoMethod(app->vScrollBar, MUIM_Notify, MUIA_Prop_First, MUIV_EveryTime,
                                  app->appObj, 2, MUIM_Application_ReturnID, RID_VSCROLL);
    if (app->hScrollBar) DoMethod(app->hScrollBar, MUIM_Notify, MUIA_Prop_First, MUIV_EveryTime,
                                  app->appObj, 2, MUIM_Application_ReturnID, RID_HSCROLL);



    /* Window resize: re-sync scrollbar domains (Entries/Visible) when the
     * editor's visible area changes.  Both Width and Height are needed:
     * a taller window changes VisibleLines; a wider one changes the visible
     * pixel width used by the horizontal bar. */
/* no too early, get resize from editor.
    DoMethod(app->winObj, MUIM_Notify, MUIA_Window_Width,  MUIV_EveryTime,
             app->appObj, 2, MUIM_Application_ReturnID, RID_RESIZE);
    DoMethod(app->winObj, MUIM_Notify, MUIA_Window_Height, MUIV_EveryTime,
             app->appObj, 2, MUIM_Application_ReturnID, RID_RESIZE);
*/
    /*,not now
     syncVScrollbar();
     syncHScrollbar();
     */

    /* Populate recent files menu from loaded settings */
    MmgAction_RebuildRecentMenu();

    /* ------------------------------------------------------------------ */
    /* Open window                                                          */
    /* ------------------------------------------------------------------ */
    SetAttrs(app->winObj,
        MUIA_Window_Open,         TRUE,
        MUIA_Window_ActiveObject, (ULONG)app->editorObj,
        TAG_DONE);

    /* colors from settiongs need to be applied when window have a screen */
    if(app->settings.colorsWereLoaded)
    {
        MmgAction_ApplyColorFromSettings();
    }

    /* ------------------------------------------------------------------ */
    /* Load file passed on CLI: MUImojiGear [Latin1|Latin2] <path>        */
    /* ------------------------------------------------------------------ */
    if (argc > 1) {
        int   encoding = 0;
        const char *filePath = NULL;
        int i;
        for (i = 1; i < argc; i++) {
            if (!argv[i] || argv[i][0] == '\0') continue;
            if      (strcasecmp(argv[i], "latin1") == 0) encoding = 1;
            else if (strcasecmp(argv[i], "latin2") == 0) encoding = 2;
            else if (!filePath)                          filePath = argv[i];
        }
        if (filePath) MmgAction_LoadFromPath(filePath, encoding);
    }

    /* ------------------------------------------------------------------ */
    /* Event loop                                                          */
    /* ------------------------------------------------------------------ */
    {
        ULONG muisigs    = 0;
        BOOL  running = TRUE;
        BOOL reactivateEditor=FALSE;
        char  pipeBuf[PIPE_INPUT_BUF];
        LONG  pipeBufUsed = 0;

        while (running) {
            ULONG r;
           while(muisigs == 0)
            {
           // printf("bef MUIM_Application_NewInput\n");
            r = DoMethod(app->appObj, MUIM_Application_NewInput, &muisigs);
         //   if(!muisigs) printf("actual no sig\n");
           // printf("aft MUIM_Application_NewInput\n");
            switch (r) {
            case RID_QUIT:
            case MUIV_Application_ReturnID_Quit:
                running = FALSE;
                break;

            case RID_NEW_FILE: {
                struct Gadget *g; struct Window *w;
                App_GetRawEditorWin(&g, &w);
                if (g && w) {
                    SetGadgetAttrs(g, w, NULL, UTED_Text, (ULONG)"", TAG_DONE);
                    RefreshGList(g, w, NULL, 1);
                    App_UpdateStatus();
                }
                break;
            }

            case RID_CUT:        doEditAction(UTED_ApplyCut); reactivateEditor = TRUE;   break;
            case RID_COPY:       doEditAction(UTED_ApplyCopy);  reactivateEditor = TRUE;   break;
            case RID_COPY_LAT1:  MmgAction_CopyLatin1();  reactivateEditor = TRUE;         break;
            case RID_COPY_LAT2:  MmgAction_CopyLatin2();   reactivateEditor = TRUE;        break;
            case RID_PASTE:      doEditAction(UTED_ApplyPaste); reactivateEditor = TRUE;   break;
            case RID_PASTE_LAT1: MmgAction_PasteLatin1();    reactivateEditor = TRUE;      break;
            case RID_PASTE_LAT2: MmgAction_PasteLatin2();   reactivateEditor = TRUE;       break;
            case RID_UNDO:       doEditAction(UTED_Undo);   reactivateEditor = TRUE;       break;
            case RID_REDO:       doEditAction(UTED_Redo);    reactivateEditor = TRUE;      break;
            case RID_FONTSIZE_P:        MmgAction_FontSizePlus();  reactivateEditor = TRUE;        break;
            case RID_FONTSIZE_M:        MmgAction_FontSizeMinus();  reactivateEditor = TRUE;       break;
            case RID_TOGGLE_ANTIALIAS:  MmgAction_ToggleAntialias();  reactivateEditor = TRUE;     break;
            case RID_TOGGLE_MONOSPACE:  MmgAction_ToggleMonospace();  reactivateEditor = TRUE;     break;
            case RID_TOGGLE_WORDWRAP:   MmgAction_ToggleWordWrap();  reactivateEditor = TRUE;      break;
            case RID_TOGGLE_APPLYANSI:  MmgAction_ToggleApplyAnsi();   reactivateEditor = TRUE;    break;
            case RID_TOGGLE_VIZTABS:    MmgAction_ToggleVisualizeTabs();  reactivateEditor = TRUE;  break;
            case RID_TOGGLE_TABSSPACES: MmgAction_ToggleTabsAreSpaces(); reactivateEditor = TRUE;  break;
            case RID_TOGGLE_DISPLAYUNICODEINFO: MmgAction_ToggleDisplayUnicodeInfo(); break;

            case RID_FONTS_WIN: {
                ULONG isOpen = 0;
                if (app->fontViewWinObj)
                    GetAttr(MUIA_Window_Open, app->fontViewWinObj, &isOpen);
                if (isOpen) MmgFontsView_Close(); else MmgFontsView_Open();
                break;
            }

            case RID_EMOJI_SET_CHANGE:
#ifndef DISABLE_EMOJIBOX
                MmgEmojiBox_HandleSetChange();
#endif
                break;

            case RID_EMOJI_BOX_MENU: {
                ULONG isOpen = 0;
#ifndef DISABLE_EMOJIBOX
                if (app->emojiBoxWinObj)
                    GetAttr(MUIA_Window_Open, app->emojiBoxWinObj, &isOpen);

                if (isOpen) MmgEmojiBox_Close();
                else        MmgEmojiBox_Open();
#endif
                break;
            }

            default:
                if (r >= RID_RECENT_BASE &&
                    r <  RID_RECENT_BASE + APPSETTINGS_MAX_RECENT)
                    {
                    MmgAction_OpenRecentFile((int)(r - RID_RECENT_BASE));
                    reactivateEditor = TRUE;
                    }
                else if (r >= RID_COLOR_BASE &&
                         r <  RID_COLOR_BASE + MMG_NUM_COLOR_PRESETS)
                    {
                    MmgAction_ApplyColorPreset((int)(r - RID_COLOR_BASE));
                    reactivateEditor = TRUE;
                    }
                else if (r >= (ULONG)RID_FONTS_BROWSE_BASE &&
                         r <  (ULONG)(RID_FONTS_BROWSE_BASE + MMG_FONTS_SLOTS))
                    MmgFontsView_HandleBrowse((int)(r - RID_FONTS_BROWSE_BASE));
                else if (r >= (ULONG)RID_FONTS_CLEAR_BASE &&
                         r <  (ULONG)(RID_FONTS_CLEAR_BASE + MMG_FONTS_SLOTS))
                    MmgFontsView_HandleClear((int)(r - RID_FONTS_CLEAR_BASE));
                else if (r >= (ULONG)RID_FONTS_PRESET_BASE &&
                         r <  (ULONG)(RID_FONTS_PRESET_BASE + MMG_FONTS_PRESETS))
                    MmgFontsView_HandlePreset((int)(r - RID_FONTS_PRESET_BASE));
                else if (r >= (ULONG)RID_FONTS_STR_BASE &&
                         r <  (ULONG)(RID_FONTS_STR_BASE + MMG_FONTS_SLOTS))
                    MmgFontsView_HandleStrEdit((int)(r - RID_FONTS_STR_BASE));
                else if (r >= (ULONG)RID_EMOJI_ANSI_BASE &&
                         r <  (ULONG)(RID_EMOJI_ANSI_BASE + MMG_NUM_ANSI_BTNS))
                    {
#ifndef DISABLE_EMOJIBOX
                    const char *seq = MmgEmojiBox_GetAnsiString(
                                          (int)(r - RID_EMOJI_ANSI_BASE));
                    if (seq) {
                        struct Gadget *g; struct Window *w;
                        App_GetRawEditorWin(&g, &w);
                        if (g && w)
                            SetGadgetAttrs(g, w, NULL,
                                           UTED_InsertText, (ULONG)seq, TAG_DONE);
                    }
#endif
                    }
                break;

            case RID_LOAD_UTF8:   MmgAction_LoadUTF8();  reactivateEditor = TRUE;   break;
            case RID_LOAD_LATIN1: MmgAction_LoadLatin1();  reactivateEditor = TRUE; break;
            case RID_LOAD_LATIN2: MmgAction_LoadLatin2(); reactivateEditor = TRUE;  break;
            case RID_SAVE_UTF8:   MmgAction_SaveUTF8();   reactivateEditor = TRUE;  break;
            case RID_SAVE_LATIN1: MmgAction_SaveLatin1(); reactivateEditor = TRUE;  break;
            case RID_SAVE_LATIN2: MmgAction_SaveLatin2();  reactivateEditor = TRUE; break;

            case RID_VSCROLL: {
                ULONG newTop = 0;
                struct Gadget *g; struct Window *w;
                GetAttr(MUIA_Prop_First, app->vScrollBar, &newTop);
                App_GetRawEditorWin(&g, &w);
                if (g && w) {
                    SetGadgetAttrs(g, w, NULL, UTED_ScrollTop, newTop, TAG_DONE);
                    RefreshGList(g, w, NULL, 1);
                }
                break;
            }
            case RID_HSCROLL: {
                ULONG newLeft = 0;
                struct Gadget *g; struct Window *w;
                GetAttr(MUIA_Prop_First, app->hScrollBar, &newLeft);
                App_GetRawEditorWin(&g, &w);
                if (g && w) {
                    SetGadgetAttrs(g, w, NULL, UTED_ScrollLeft, newLeft, TAG_DONE);
                    RefreshGList(g, w, NULL, 1);
                }
                break;
            }
            /* finaly not, need layout resize, not window */
            // case RID_RESIZE:
            //     /* Window was resized: update scrollbar domains to reflect
            //      * the new visible area without waiting for a BOOPSI notification. */
            //      printf("RID_RESIZE\n");
            //     syncVScrollbar();
            //     syncHScrollbar();
            //     break;
            }
                if (!running) break;
            } // end while muisigs == 0
            if (!running) break;

            /* ---- Pipe / stdin UTF-8/ANSI input (non-blocking) ---- */
            {
                BPTR inpt = Input();
                if (inpt && !IsInteractive(inpt))
                {
                    BOOL needsFree;
                    const char *chunk = EgPipeInput_Poll(pipeBuf, PIPE_INPUT_BUF, &pipeBufUsed, &needsFree);
                    if (chunk) {
                        struct Gadget *pg = NULL; struct Window *pw = NULL;
                        App_GetRawEditorWin(&pg, &pw);
                        if (pg && pw) {
                            SetGadgetAttrs(pg, pw, NULL,
                                           UTED_InsertText, (ULONG)chunk, TAG_DONE);
                            RefreshGList(pg, pw, NULL, 1);
                        }
                        if (needsFree) FreeVec((char *)chunk);
                    }
                }
            }

            if(muisigs)
            {
                ULONG sigs;
                struct Gadget  *g = NULL;
                struct Window  *w = NULL;

                App_GetRawEditorWin(&g, &w);

                sigs = Wait(muisigs | SIGBREAKF_CTRL_C | SIGBREAKF_CTRL_F);

                if (sigs & SIGBREAKF_CTRL_C) { running = FALSE; break; }

                if (sigs & SIGBREAKF_CTRL_F) {
                    struct TagItem *msg;
                     struct TagItem *tag;

                    BOOL needRedraw = FALSE, needStatus = FALSE;

                    while ((msg = MmgBoopsi_NextMessage()) != NULL) {

                        if (FindTagItem(UTEDN_CursorMoved,       msg) ||
                            FindTagItem(UTEDN_ScrollChanged,      msg) ||
                            FindTagItem(UTED_ScrollLeft,          msg) ||
                            FindTagItem(UTED_SetPrivateActivation, msg))
                        { needRedraw = TRUE; needStatus = TRUE; }
                        if (FindTagItem(UTEDN_TextChanged, msg) ||
                            FindTagItem(UTED_Modified,     msg))
                        { needRedraw = TRUE; needStatus = TRUE; }

                        /* we asked unitexteditor, in UKM_Internal mode,
                         * to notify us back rawkey codes and qualifiers */
                         if((tag = FindTagItem(UTED_InternalRawKey_Code, msg))!=NULL)
                         {
                            ULONG qulkey = tag->ti_Data;
                            int isUp = 0x0080 & qulkey;
                            UWORD key = (UWORD)(0x007f & qulkey);
                            UWORD qualifiers = (UWORD)(qulkey>>16);
#ifndef DISABLE_EMOJIBOX
                            if(!isUp && key>=0x50 && key<=0x59)
                                MmgEmojiBox_HandleFKey(key, qualifiers);
#endif
                         }


                        /* Emoji button click: toggle emoji box */
#ifndef DISABLE_EMOJIBOX
                        {
                            struct TagItem *idTag = FindTagItem(GA_ID, msg);
                            if (idTag && idTag->ti_Data == GID_EMOJI_BUTTON &&
                                FindTagItem(GA_Selected, msg))
                                {
                                ULONG isOpen = 0;
                                if (app->emojiBoxWinObj)
                                    GetAttr(MUIA_Window_Open, app->emojiBoxWinObj, &isOpen);

                                if (isOpen) MmgEmojiBox_Close();
                                else        MmgEmojiBox_Open();

                                }
                        }
#endif
                        /* Grid click: emoji cell selected */
#ifndef DISABLE_EMOJIBOX
                        {
                            struct TagItem *idTag = FindTagItem(GA_ID, msg);
                            if (idTag && idTag->ti_Data == GID_EMOJI_GRID) {
                                struct TagItem *cidxTag = FindTagItem(EGRID_ClickedIdx, msg);
                                if (cidxTag)
                                    MmgEmojiBox_HandleGridClick((int)cidxTag->ti_Data);
                            }
                        }
#endif
                    }

                    if (needRedraw && g && w)
                    {
                        RefreshGList(g, w, NULL, 1);
                        syncVScrollbar();
                        syncHScrollbar();
                    }
                    if (needStatus) App_UpdateStatus();
                    MmgEmojiBox_FlushPendingRender();
                }
                if( reactivateEditor && w && g)
                {
                    if((g->Activation & GACT_ACTIVEGADGET)==0 )
                    {
                        /* This will make the canva receive mouse move. */
                        //App_ReactivateEditor();
                        ActivateGadget(g,w,NULL );
                    }
                    reactivateEditor = NULL;
                }

            } /* if ever MUI messages */
            muisigs = 0;

        }/* end while running*/
    }

cleanup:
    if (app) {

        if (app->appObj)
            MUI_DisposeObject(app->appObj);
        else if (app->winObj)
            MUI_DisposeObject(app->winObj);

#ifndef DISABLE_EMOJIBOX
        MmgEmojiBox_Dispose();  /* free grid class + DC after MUI objects are gone */
        MmgFontsView_Dispose();
#endif
        AppSettings_Save(&app->settings);
        AppSettings_Close(&app->settings);

        FreeVec(app);
        app = NULL;
    }

    MmgBoopsi_Close();
    EgLocale_Close();

   if (LocaleBase)        CloseLibrary((struct Library *)LocaleBase);

    if (AslBase)           CloseLibrary(AslBase);

   if (UniButtonBase)     CloseLibrary(UniButtonBase);

    if (UniTextEditorBase) CloseLibrary(UniTextEditorBase);

    if (MUIMasterBase)     CloseLibrary(MUIMasterBase);

    if (UtilityBase)       CloseLibrary(UtilityBase);

    if (IconBase)          CloseLibrary(IconBase);

    if (IntuitionBase)     CloseLibrary((struct Library *)IntuitionBase);
    if (GfxBase)           CloseLibrary((struct Library *)GfxBase);


    return exitCode;
}
