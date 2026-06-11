/*
 * EmojiGear Amiga UTF-8 Unicode Text Editor
 * Main application: library init, window creation, event loop, cleanup.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <clib/alib_protos.h>

#include <intuition/screens.h>
#include <intuition/icclass.h>

#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <proto/dos.h>
#include <proto/icon.h>
#include <proto/amigaguide.h>
#include <proto/locale.h>
#include <libraries/locale.h>

#include <proto/window.h>
#include <classes/window.h>

#include <proto/layout.h>
#include <gadgets/layout.h>

#include <proto/button.h>
#include <gadgets/button.h>

#include <proto/scroller.h>
#include <gadgets/scroller.h>

#include <intuition/gadgetclass.h>  /* PGA_Top, PGA_Total, PGA_Visible */
#include <devices/inputevent.h>     /* IEQUALIFIER_* for IDCMPHook */

#include <proto/checkbox.h>
#include <gadgets/checkbox.h>

#include <proto/clicktab.h>
#include <gadgets/clicktab.h>

#include <proto/label.h>
#include <images/label.h>

#include <proto/asl.h>
#include <libraries/asl.h>

#include "compilers.h"
#include "bdbprintf.h"
#include "gadgetid.h"

#include "emojigear.h"

#include "eglocale.h"
#include "egaction.h"
#include "egmenu.h"
#include "egpipeinput.h"

#include "boopsimessage.h"
#include "boopsimainwindow.h"
#include "borderscrollers.h"
#include "emojigearsettings.h"

#include "tooltypepref.h"
#include "emojibox.h"
#include "egsearchbox.h"
#include "egfontsview.h"
#include "egtabs.h"
#include "egtabs_fallbackclass.h"

#include <workbench/startup.h>

#include <stdio.h>

/* our beloved gadgets */

#include <proto/unitexteditor.h>
#include <gadgets/unitexteditor.h>

#include <proto/unibutton.h>
#include <gadgets/unibutton.h>
/* our beloved utf8 freetype rendering engine */
#include <libraries/utf8rastport.h>
#include <proto/utf8rastport.h>


#include <dos/dosextens.h>

//#define HACKOS39SUPPORT 1

struct Task *myTask = NULL;


const char *pVersion = "$VER: EmojiGear " EMOJIGEAR_VERSION;

/* Library bases */
struct IntuitionBase   *IntuitionBase = NULL;
struct GfxBase         *GfxBase       = NULL;
struct Library         *UtilityBase   = NULL;
struct Library         *LayersBase    = NULL;
struct Library         *IconBase      = NULL;
struct Library         *AslBase       = NULL;
struct Library         *GadToolsBase  = NULL;
/* libraries we consider optional and accept NULL values */
struct LocaleBase      *LocaleBase    = NULL;
struct Library         *DataTypesBase = NULL;
struct Library          *CyberGfxBase = NULL;
struct Library          *URPBase = NULL; /* Unicode rastport lib */

/* BOOPSI class bases */
struct Library *WindowBase   = NULL;
struct Library *LayoutBase   = NULL;
struct Library *ButtonBase   = NULL;
struct Library *LabelBase    = NULL;
struct Library *CheckBoxBase = NULL;
struct Library *GetFileBase  = NULL;
struct Library *RequesterBase= NULL;
struct Library *PaletteBase  = NULL;
struct Library *IntegerBase  = NULL;
struct Library *ClickTabBase = NULL;
struct Library *ChooserBase  = NULL;
struct Library *PenMapBase   = NULL;
/* Our beloved gadgets bases */
struct Library *UniTextEditorBase = NULL;
struct Library *UniButtonBase  = NULL;

/* Library table for automated opening/closing */
typedef struct {
    const char    *name;
    ULONG          version;
    struct Library **base;
} LibraryEntry;

/* Note, for library we need a minimal version number.
 *  It's the library OS number which goes more or less like that:
 * v34 -> OS2.x (Commodore)
 * v39 ->OS3.0
 * v40 ->OS3.1 (1993, commodore)
 * v44 ->OS3.5 (1998, Haage&p)
 * v45 ->OS3.9 (1999, Haage&p)  -> nowadays the base for some free distributions. (Coffin?)
 * v46 ->OS3.14 (2019, Hyperion)
 * v47 ->OS3.2 (2021, Hyperion) -note you can always upgrade -
 * (v50>= are meant to beOS4 PowerPC)

 But watch out, if we want to have BOOPSI classes OS3.9 compatible:
 OS3.9 consider it just upgrades OS3.5, so a few BOOPSI class version are still "44".
*/
/* In this table are "mandatory" library and minimal versions */
static LibraryEntry libraryTable[] = {
    /* System libraries */
    {"graphics.library",  47, (struct Library **)&GfxBase},
    {"intuition.library", 47, (struct Library **)&IntuitionBase},
    {"utility.library",   39, &UtilityBase},
    {"layers.library",    39, &LayersBase},
    {"icon.library",      39, &IconBase},
    {"asl.library",       39, &AslBase},
    {"gadtools.library",  39, &GadToolsBase},
    {"utf8rastport.library",3, &URPBase},

    /* BOOPSI class libraries - with minimal version of OS3.9 (not related to os!) */
    {"window.class",           42, &WindowBase},
    {"images/label.image",     42, &LabelBase},
    {"gadgets/layout.gadget",  42, &LayoutBase},
    {"gadgets/button.gadget",  42, &ButtonBase},
    {"gadgets/checkbox.gadget",  42, &CheckBoxBase},
    {"gadgets/getfile.gadget", 42, &GetFileBase},
    {"requester.class", 42, &RequesterBase},
    {"gadgets/palette.gadget",  44, &PaletteBase},
    {"gadgets/integer.gadget", 44, &IntegerBase},

    {"gadgets/clicktab.gadget",47, &ClickTabBase},
    {"gadgets/chooser.gadget", 44, &ChooserBase},
    {"images/penmap.image",    47, &PenMapBase},
    /* ... and the one that are starred in this app */
    {"gadgets/unitexteditor.gadget",3, &UniTextEditorBase},
    {"gadgets/unibutton.gadget", 3, &UniButtonBase},

    {NULL, 0, NULL} /* Terminator */
};

/* from tooltypepref.c use both for tooltipes and appicon */
extern struct DiskObject *AppDiskObject;

struct App *app = NULL;

/* Forward declarations */

void exitclose(void);
void refreshUI(void);

void cleanexit(const char *pmessage)
{
    if (pmessage) printf("%s\n", pmessage);
    exit(0);
}


/*
 * Probe locale.library and the "Charset" env variable to decide which
 * UTED_VANILLAKEY_* encoding the system keymap uses for bytes 0xA0–0xFF.
 *
 * Priority:
 *  1. loc_CodeSet from the default Locale (IANA MIB number):
 *       4 or 0 → Latin-1 (ISO 8859-1)   5 → Latin-2 (ISO 8859-2)
 *  2. If loc_CodeSet == 0 (unset), check ENV:Charset string:
 *       "ISO-8859-1" → Latin-1           "ISO-8859-2" → Latin-2
 *  3. Default: Latin-1.
 *
 * Must be called after LocaleBase has been opened.
 */
static ULONG detect_vanilla_ansi_code(void)
{
    ULONG ansiCode = UTED_VANILLAKEY_LATIN1;

    if (LocaleBase) {
        struct Locale *loc = OpenLocale(NULL);
        if (loc) {
            ULONG cs = loc->loc_CodeSet;
            if (cs == 5) {
                ansiCode = UTED_VANILLAKEY_LATIN2;
            } else if (cs == 0) {
                /* loc_CodeSet unset: fall back to ENV:Charset */
                char buf[32];

                if (GetVar("Charset", buf, (LONG)sizeof(buf), GVF_GLOBAL_ONLY) > 0) {
                    if (strstr(buf, "8859-2") != NULL ||
                        strcmp(buf, "latin2")     == 0 ||
                        strcmp(buf, "LATIN2")     == 0)
                    {
                        ansiCode = UTED_VANILLAKEY_LATIN2;
                    }
                }
            }
            /* cs == 4 → Latin-1, already the default */
            CloseLocale(loc);
        }
    }
    return ansiCode;
}

 extern const int fontSizeTable[];
 extern const int fontSizeTableCount;

/* Configure the UniButton dc with screen-appropriate point size and the same
 * font paths as the editor.  Must be called after CurrentMainScreen is set
 * (i.e. from GenericOpenWindow, before WM_OPEN). */
void UpdateButtonFontsFromSettings(void)
{
    ULONG btnPointSize = 14;
    ULONG btnFlags;
    if (!app || !app->statusBarEmojiBtn) return;
    if (CurrentMainScreen && CurrentMainScreen->Font)
        btnPointSize = (ULONG)CurrentMainScreen->Font->ta_YSize;

    /* a little bit larger */
    btnPointSize += 2;

    btnFlags = (app->appSettings.antialias    ? URP_PREF_ANTIALIAS     : 0)
             | (app->appSettings.emojiQuality ? URP_PREF_HIGHFILTERING : 0)
             | URP_PREF_CLUTMODE_NOMASK;

    SetAttrs(app->statusBarEmojiBtn,
        UBT_FlushFonts,  TRUE,
        UBT_URPPrefs,    btnFlags,
        UBT_PointSize,   btnPointSize,
        UBT_AddFont,     (ULONG)app->appSettings.primaryFontPath,
        UBT_AddFont,     (ULONG)app->appSettings.fallback1FontPath,
        UBT_AddFont,     (ULONG)app->appSettings.fallback2FontPath,
        UBT_AddFont,     (ULONG)app->appSettings.emojiFontPath,
        GA_Text,         (ULONG)"\xF0\x9F\x98\x8A", // have to flush bitmap cache to change font
        TAG_END);
     if(CurrentMainWindow)
        RefreshGList(app->statusBarEmojiBtn, CurrentMainWindow, NULL, 1);
    /* if emojibox open refresh */
    if(app->emojiBoxWindow.window && app->emojiBoxWindow.gridGadget)
    {
        RefreshGList(app->emojiBoxWindow.gridGadget, app->emojiBoxWindow.window, NULL, 1);
    }

}

void UpdateEditorFontsFromSettings()
{
    ULONG flags;
    if(!app || !app->textEditorObj) return;
    flags = ((app->appSettings.antialias)?URP_PREF_ANTIALIAS:0)
            | ((app->appSettings.monospace)?URP_PREF_FORCE_MONOSPACE:0)
            | ((app->appSettings.emojiQuality)?URP_PREF_HIGHFILTERING:0)
            | URP_PREF_CLUTMODE_NOMASK
            ;

    SetGdAttrs(app->textEditorObj,
                UTED_FlushFonts,TRUE,
                UTED_URPPrefs,  flags,
                UTED_WordWrap,  (ULONG)app->appSettings.wordWrap,
                UTED_ApplyAnsiEscapes, (ULONG)app->appSettings.applyAnsi,
                UTED_PointSize, (ULONG)fontSizeTable[app->appSettings.currentFontSizeIndex],
                UTED_AddFont,app->appSettings.primaryFontPath,
                UTED_AddFont,app->appSettings.fallback1FontPath,
                UTED_AddFont,app->appSettings.fallback2FontPath,
                UTED_AddFont,app->appSettings.emojiFontPath,
                TAG_END
                );
    /* also update the button fonts */
    UpdateButtonFontsFromSettings();

}


void OpenSearchBox();
void CloseSearchBox();

/* - - - - - - - - - MAIN - - - - - - - - - */

/* #define STACK_WATCH 1 */
/* measured stack use 18926b */
#define EG_MIN_STACK (28 * 1024)

int main(int argc, char **argv)
{
    if(SysBase->LibNode.lib_Version<47) {
        printf("need OS3.2 (v47), you may upgrade, or try MUImojiGear\n");
        return 1;
    }

    atexit(&exitclose);
    /* Open all required libraries via table */
    {
        LibraryEntry *entry;

        for (entry = libraryTable; entry->name != NULL; entry++) {

            *(entry->base) = OpenLibrary(entry->name, entry->version);

            if (!*(entry->base)) {
                printf( "Can't open %s v%d", entry->name,entry->version);
                return 1;
            }
        }
    }

    /* optional libs, can return NULL */
//    DataTypesBase = OpenLibrary("datatypes.library",39);
//    CyberGfxBase  = OpenLibrary("cybergraphics.library", 1);

    myTask = FindTask(NULL);
    {
        int _stacksize   = (int)((int)myTask->tc_SPReg - (int)myTask->tc_SPLower);
        if (_stacksize < EG_MIN_STACK) {
            printf("EmojiGear needs at least 32k stack. Use \"stack 32768\" or set it in the icon properties.\n");
            return 1;
        }
#ifdef STACK_WATCH
        /* Fill the unused portion of the stack with a sentinel value so we
         * can measure real high-water use at exit.  _sw_near leaves a 64-byte
         * safety margin below the current frame before the fill starts. */
        {
            int   _sw_anchor = 0;
            int  *_sw_near   = (int *)((int)&_sw_anchor - 64);
            int  *_sw_far    = (int *)((int)myTask->tc_SPLower + 4);
            int   _sw_i;
            for (_sw_i = 0; _sw_i < ((int)_sw_near - (int)_sw_far) / (int)sizeof(int); _sw_i++)
                _sw_far[_sw_i] = (int)0xCAFEBABE;
        }
#endif
    }


    /* Open locale.library (optional - soft failure) */
    LocaleBase = (struct LocaleBase *)OpenLibrary("locale.library", 38);

    /* Allocate app struct (zero-initialized so disposeQ.count = 0 etc.) */
    app = (struct App *)AllocVec(sizeof(struct App), MEMF_CLEAR);
    if (!app) cleanexit("Can't allocate app");

    /* read settings from icon tooltypes (very Amigaish thing)
     *  if launch from command line */
    if(argc>0)
    {
        /*int result =*/ ToolTypePrefs_Init(argv[0]);
    } else
    if(argc ==0 && argv != 0)
    { /*  if launch from Workbench, Amiga C startup must give us that struct that way: */
        int i;
        struct WBStartup *WBenchMsg = (struct WBStartup *)argv; // Amiga C startup magic.
        struct WBArg *wbarg=WBenchMsg->sm_ArgList;
        for( i=0 ;
            i < WBenchMsg->sm_NumArgs;
            i++, wbarg++)
        {
            if((*wbarg->wa_Name) && (ToolTypePrefs_Init(wbarg->wa_Name))) break;
        } // end loop by wbarg
    }

    /* Initialize locale (English built-in) */
    EgLocale_Init("EmojiGear.catalog", 0);

    /* Initialize action system (localizes action names) */
    EgAction_Init();

    /* Load application settings from icon tooltypes and sync to settings view */
    AppSettings_Load(&app->appSettings);


    /* Register the TextEditor BOOPSI class */
#ifdef USE_STATIC_UNITEXTEDITOR
    if (!UniTextEditor_Init() || !UniTextEditorClass) {
        cleanexit("Can't create UniTextEditor class");
    }
#else
 // TODO regular published  gadget init.
#endif

    /* Register the UniButton BOOPSI class */

#ifdef USE_STATIC_UNIBUTTON
    if (!UniButton_Init() || !UniButtonClass) {
        cleanexit("Can't create UniButton class");
    }
#endif

    /* Initialize the BOOPSI message target model */
    if (!initMessageTargetModel()) cleanexit("Can't create BOOPSI message target");

    /* ---- Create text editor to fill GZZ inner area minus status bar ---- */
    app->textEditorObj = (Object *)NewObject(UNITEXTEDITOR_GetClass(), NULL,
        GA_ID,           (ULONG)ID_TEXTEDITOR,
        ICA_TARGET,      (ULONG)TargetInstance, /*ICTARGET_IDCMP*/

//UTED_LeftMargin,2,
 // UTED_TopMargin,0,
 // UTED_RightMargin,0,
 // UTED_BottomMargin,0,

        UTED_TabSpaces,app->appSettings.tabSpaces,
        UTED_TabsAreSpaces,app->appSettings.tabsAreSpaces,
        UTED_VisibleTabs,app->appSettings.visualizeTabs,

    UTED_BevelStyle, BVS_FIELD,
    UTED_WordWrap,FALSE,
    UTED_DisplayInternalVScroll, FALSE,/* we have external scrollers */

        /* rawkey and vanilla key will be sent from window message
          with PutRawKey and PutVanillaKey.
          if UKM_Internal, keys are managed only with boopsi messages,
          but it will "hog" the keys for everything and we'll have
          to Activate() back the gadget now and then.
          That way text receive keys in any cases.
        */
        UTED_KeyMessageMode,UKM_External,
        UTED_VanillaKeyAnsiCode, detect_vanilla_ansi_code(),
        TAG_DONE);

    if (!app->textEditorObj) cleanexit("Can't create UniTextEditor gadget");

    /* Main editor starts with focus */
    app->activeEditorObj = app->textEditorObj;

    UpdateEditorFontsFromSettings();

    /* Create status bar */
    app->statusBarLabel = (Object *)NewObject(BUTTON_GetClass(), NULL,
        GA_ReadOnly, TRUE,
        BUTTON_BevelStyle, BVS_NONE,
        BUTTON_Transparent, TRUE,
        BUTTON_Justification, BCJ_LEFT,
        GA_Text, (ULONG)LOC(MSG_STATUS_READY),
        TAG_END);

    /* Emoji button at the right of the status bar.
     * Gets its own URPDrawContext (independent from the editor's).
     * Fonts are loaded in GenericOpenWindow once the screen is known. */

    app->statusBarEmojiBtn = (Object *)NewObject(UNIBUTTON_GetClass(), NULL,
        GA_ID,           GID_EMOJI_BUTTON,
        ICA_TARGET,      (ULONG)TargetInstance,
        GA_ReadOnly,     FALSE,
        UBT_BevelStyle,  BVS_BUTTON,
        UBT_Transparent, FALSE,
        UBT_LeftMargin,  3UL,
        UBT_RightMargin, 3UL,
        UBT_TopMargin,   0UL,
        UBT_BottomMargin,2UL,
        GA_Text,         (ULONG)"\xF0\x9F\x98\x8A", // smiley
        TAG_END);

    app->statusBarLayout = (Object *)NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
        LAYOUT_BevelStyle, BVS_SBAR_VERT,
        LAYOUT_AddChild, (ULONG)app->statusBarLabel,
        TAG_END);

    /* Append the emoji button as a fixed-width child if it was created */
    if (app->statusBarLayout && app->statusBarEmojiBtn) {
        SetAttrs(app->statusBarLayout,
            LAYOUT_AddChild,    (ULONG)app->statusBarEmojiBtn,
            CHILD_WeightedWidth, 0,
            TAG_END);
    }

    /*
     * Create tab bar
     */

    {
       // struct Node *firstNode;

        app->tabList = (struct List *)AllocVec(sizeof(struct List),
                                               MEMF_ANY | MEMF_CLEAR);
        if (!app->tabList) cleanexit("Tab list alloc failed");
        NewList(app->tabList);

        /* We have to fill one tab and one "text stash context" */
        EgTabs_NewTab();

        // if(ClickTabBase)
        // {

            app->tabGadget = NewObject(CLICKTAB_GetClass(), NULL,
                GA_ID,              GID_TABBAR,
                GA_RelVerify,       TRUE,
                ICA_TARGET,         (ULONG)TargetInstance,
                CLICKTAB_Labels,    (ULONG)app->tabList,
                CLICKTAB_Current,   0,
                    // (app->tabGadget_closeImage)?(CLICKTAB_CloseImage):(TAG_IGNORE),
                    // (ULONG)app->tabGadget_closeImage,
                TAG_END);
            if (!app->tabGadget) cleanexit("ClickTab gadget creation failed");
        // } else
        // {
        //     app->tabGadget = NewObject(EgFakeTab_GetClass(), NULL,
        //         GA_ID,              GID_TABBAR,
        //         GA_RelVerify,       TRUE,
        //         CLICKTAB_Labels,    (ULONG)app->tabList,
        //         CLICKTAB_Current,   0,
        //         TAG_END);
        //     if (!app->tabGadget) cleanexit("FakeTab gadget creation failed");
        // }
    }

    /*
     * Create main layout
     */
    {
        /* Main vertical layout: tab bar | text editor | status bar */
        app->mainvlayout = (Object *)NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_DeferLayout,   TRUE,
            LAYOUT_SpaceOuter,    FALSE,
            LAYOUT_BottomSpacing, 0,
            LAYOUT_TopSpacing,    0,
            LAYOUT_LeftSpacing,   0,
            LAYOUT_RightSpacing,  0,
            LAYOUT_InnerSpacing,  0,
            LAYOUT_Orientation,   LAYOUT_ORIENT_VERT,

            LAYOUT_AddChild, (ULONG)app->tabGadget,
                CHILD_WeightedHeight, 0,

            LAYOUT_AddChild,(ULONG)app->textEditorObj,
               CHILD_WeightedHeight,1,

            LAYOUT_AddChild, (ULONG)app->statusBarLayout,
                CHILD_WeightedHeight, 0,

            TAG_END);
    }

    if (!app->mainvlayout) cleanexit("Layout error");

    /* Create message port */
    app->app_port = CreateMsgPort();

    /* Create window object */
    {
        ULONG lasttag = TAG_END;
        ULONG lastValue = 0;
        if(AppDiskObject)
        {
            lasttag = WINDOW_Icon;
            lastValue = (ULONG)AppDiskObject;
        }

    app->window_obj = (Object *)NewObject(WINDOW_GetClass(), NULL,
        WA_Left, 40,
        WA_Top, 40,
        WA_Width, 640,
        WA_Height, 400,
    /*given at opening    WA_CustomScreen, (ULONG)app->lockedscreen, */
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_MENUPICK | IDCMP_RAWKEY
        | IDCMP_VANILLAKEY
        | IDCMP_GADGETDOWN | IDCMP_GADGETUP /*| IDCMP_MOUSEMOVE*/ | IDCMP_NEWSIZE,
        WA_Flags, WFLG_DRAGBAR | WFLG_DEPTHGADGET | WFLG_CLOSEGADGET |
                  WFLG_SIZEGADGET | WFLG_SIZEBRIGHT | WFLG_ACTIVATE | WFLG_SMART_REFRESH
                  ,
        //was for a trick WA_ReportMouse,TRUE, //test
        WA_Title, (ULONG)LOC(MSG_WINDOW_TITLE),

        // WINDOW_IDCMPHook,(ULONG)&idcmpHook,
        // WINDOW_IDCMPHookBits, IDCMP_RAWKEY /*| IDCMP_VANILLAKEY*/,

        WINDOW_ParentGroup, (ULONG)app->mainvlayout,
        WINDOW_IconifyGadget, TRUE,
        WINDOW_IconTitle, (ULONG)"EmojiGear",
        WINDOW_AppPort, (ULONG)app->app_port, /* needed for iconizing */
        lasttag,lastValue,
        TAG_END);

    }
    if (!app->window_obj) cleanexit("Can't create window");

    /* Initialize Project Settings window */
    if(!EgSettingsView_Init(&app->settingsView,
                                  LOC(MSG_SETTINGS)))
    {

      //  printf("Warning: Could not create Project Settings window\n");
    }

    if(!EgFontsView_Init(&app->fontsView,
                               LOC(MSG_FONTSETTINGS)))
    {
        //  printf("Warning: Could not create Font Settings window\n");
    }

    EmojiBoxWindow_Init(&app->emojiBoxWindow);

    /* Initialize Help window (loads PetMate.guide via datatypes) */
#ifdef HELP_USE_DATATYPE_AND_WINDOW
   PmHelpView_Init(&app->helpView, "PetMate Help", "PROGDIR:PetMate.guide");
#endif

    /* Open the window */
    //BMainWindow_SetTitle(&app->mainwindow,"EmojiGear v" EMOJIGEAR_VERSION);
    BMainWindow_SetTitleLocS(&app->mainwindow,MSG_WINDOW_TITLE_WITHFILE, "Unicode Text Editor v" EMOJIGEAR_VERSION);
    /*  Open the window or screen accoring to last prefs. */
    BMainWindow_Show(&app->mainwindow,app->window_obj,&app->appSettings);

    /* try refresh close button of clicktabs */
    if(app->tabGadget)
        SetGadgetAttrs(app->tabGadget,CurrentMainWindow,NULL,
                    CLICKTAB_Labels, (ULONG)app->tabList, TAG_END);

    /* Load files passed on CLI: EmojiGear <path> [<path2> ...] [Latin1|Latin2] */
    if (argc > 1) {
        int encoding = 0;
        int i;
        for (i = 1; i < argc; i++) {
            if (argv[i]) {
                if (strcasecmp(argv[i], "latin1") == 0) { encoding = 1; }
                else if (strcasecmp(argv[i], "latin2") == 0) { encoding = 2; }
            }
        }
        for (i = 1; i < argc; i++) {
            if (!argv[i] || argv[i][0] == '\0') continue;
            if (strcasecmp(argv[i], "latin1") == 0 ||
                strcasecmp(argv[i], "latin2") == 0) continue;
            if (encoding == 0)
                load_utf8_from_path(app, argv[i], NULL);
            else
                load_encoded_from_path(app, argv[i], encoding);
        }
    }

    /* - - - Input Event Loop - - - */
    {
        ULONG winsignal;
        BOOL  ok = TRUE;

        /* Persistent buffer for pipe / stdin UTF-8 input.
         * Accumulates raw bytes across iterations so that multi-byte UTF-8
         * sequences are never split when passed to UTED_InsertText.
         * pipeBufUsed tracks how many bytes are currently in the buffer;
         * incomplete sequences at the tail are kept for the next iteration. */

        char *pipeBuf = &app->pipeBuf[0];
        LONG pipeBufUsed = 0;

        GetAttr(WINDOW_SigMask, app->window_obj, &winsignal);

        while (ok)
        {

            ULONG editorNeedRefresh=0;
            ULONG vscrollNeedRefresh=0;
            ULONG hscrollNeedRefresh=0;
            ULONG result, waitedSignals, currentSignals;
            ULONG settingsSig, fontsSig, searchBoxSig, emojiBoxSig;

            flushbdbprint();
            if(app->textEditorObj)
            {
                SetAttrs(app->textEditorObj,UTED_FlushDebugOutput,TRUE,TAG_END);
            }
            URPDC_FlushGlyphCache(NULL);

            settingsSig  = EgSettingsView_GetSignalMask(&app->settingsView);
            fontsSig     = EgFontsView_GetSignalMask(&app->fontsView);
            searchBoxSig = EgSearchBox_GetSignalMask(&app->searchBox);
            emojiBoxSig  = EmojiBoxWindow_GetSignalMask(&app->emojiBoxWindow);

            waitedSignals = winsignal |
                settingsSig | fontsSig | searchBoxSig | emojiBoxSig |
                (1L << app->app_port->mp_SigBit) |
                SIGBREAKF_CTRL_C |
                SIGBREAKF_CTRL_F;

            currentSignals = Wait(waitedSignals);

        /* ---- Pipe / stdin input with UTF-8 boundary protection ----------------
         * WaitForChar() does not work reliably on AmigaOS pipes/redirections,
         * so it is not used.  EgPipeInput_Poll() relies on IsInteractive()
         * instead: PIPE: and file redirections return 0 bytes immediately
         * from Read() when no data is available (a real CON: handle would
         * block), and it keeps multi-byte UTF-8 sequences from being split
         * across iterations, converting plain 8-bit Latin-1 input to UTF-8
         * on the fly. */
        {
            BOOL needsFree;
            const char *chunk = EgPipeInput_Poll(pipeBuf, PIPE_INPUT_BUF, &pipeBufUsed, &needsFree);
            if (chunk) {
                SetGdAttrs(app->textEditorObj, UTED_InsertText, (ULONG)chunk, TAG_END);
                if (needsFree) FreeVec((char *)chunk);
            }
        }


            /* exit app at any moment from Ctrl-C signal, atexit() magic does anything needed. */
            if(currentSignals & SIGBREAKF_CTRL_C) exit(0);

            /* Flush any deferred emoji-grid render (wrong-process guard in GM_RENDER). */
            if (currentSignals & SIGBREAKF_CTRL_F)
                EmojiBoxWindow_FlushPendingRender(&app->emojiBoxWindow);

            /* Handle settings window input when its signal fires */
            if (currentSignals & settingsSig) {
                EgSettingsView_HandleInput(&app->settingsView);
            }
            if (currentSignals & fontsSig) {
                EgFontsView_HandleInput(&app->fontsView);
            }
            if (currentSignals & searchBoxSig) {
                EgSearchBox_HandleInput(&app->searchBox);
            }
            if (currentSignals & emojiBoxSig) {
                EmojiBoxWindow_HandleInput(&app->emojiBoxWindow);
            }

            while ((result = DoMethod(app->window_obj, WM_HANDLEINPUT, NULL))
                   != WMHI_LASTMSG)
            {
                flushbdbprint();
                switch (result & WMHI_CLASSMASK)
                {
                    case WMHI_ACTIVE:
                     //printf("WMHI_ACTIVE\n");
                     app->activeEditorObj = app->textEditorObj;
                     // if(CurrentMainWindow)
                     // {
                     //    ActivateGadget(app->textEditorObj,CurrentMainWindow,NULL);
                     // }
                     break;
                    case WMHI_RAWKEY:
                    {
                        ULONG key = (result & 0x07f);
                        ULONG isUp = (result & 0x080);
                        ULONG qualifiers=0;
                        int keyUsed=0;

                        GetAttr(WINDOW_Qualifier,app->window_obj,&qualifiers);
 //printf("WMHI_RAWKEY: %08x %08x\n",result,qualifiers);
                      //  if (isUp) break;
                        /* Tab rawkey (0x42):
                         *   Ctrl+Shift → previous tab (app-level).
                         *   Shift only  → backtab forwarded to gadget via UTED_PutRawKey
                         *                 (Shift+Tab does not generate WMHI_VANILLAKEY).
                         *   Plain Tab / Ctrl+Tab: fall through; handled by WMHI_VANILLAKEY. */
                        if (!isUp && key == 0x42 &&  !(qualifiers & IEQUALIFIER_REPEAT))
                        {
                            ULONG isCtrl  = qualifiers & IEQUALIFIER_CONTROL;
                            ULONG isShift = qualifiers &
                                            (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT);
                            if (isCtrl && isShift && app->tabCount > 1) {
                                EgTabs_SwitchTo((app->tabCurrentIndex - 1 +
                                               app->tabCount) % app->tabCount);
                                keyUsed = 1;
                            }
                        }
                        if(!isUp && !keyUsed)
                        {
                            keyUsed = (int) EmojiBox_HandleFKey(app, key, qualifiers,CurrentMainWindow);
                        }
                        if (!keyUsed && app->activeEditorObj == app->textEditorObj)
                        {
                            SetGdAttrs(app->textEditorObj,
                                UTED_PutRawKey,(result & 0x0ff)|(qualifiers<<16),TAG_END);
                        }
                    }
                    break;
                    case WMHI_VANILLAKEY:
                    {
                        ULONG key = (result & 0x00FF);
                        ULONG qualifiers=0;
                        GetAttr(WINDOW_Qualifier,app->window_obj,&qualifiers);
                        //printf("vk:%08x q:%08x\n",key,qualifiers);
                        /* Intuition already applied dead-key/shift composition;
                         * the low byte is the final character in the keymap encoding. */

                        /*os3.2 completly not apply capslock when mapRawKey does it with correct dead keys !!!*/
                        if((qualifiers & IEQUALIFIER_CAPSLOCK) &&
                            (key >=(ULONG)'a') && (key <=(ULONG)'z'))
                        {
                            key -= 32;
                        }
                        /* Ctrl+Tab: cycle to next tab (Ctrl+Shift+Tab arrives as rawkey) */
                        if (key == 0x09 &&
                            (qualifiers & IEQUALIFIER_CONTROL) &&
                            !(qualifiers & IEQUALIFIER_REPEAT) &&
                            app->tabCount > 1)
                        {
                            EgTabs_SwitchTo((app->tabCurrentIndex + 1) % app->tabCount);
                        }
                        else
                        if(key == 0x2d && (qualifiers & IEQUALIFIER_CONTROL)!=0 &&
                               (qualifiers & IEQUALIFIER_REPEAT)==0 )
                        {   /* yet - and + keys are received here */
                            Action_SettingsFontSizeMinus(app);
                        } else
                        if( key == 0x2b && (qualifiers & IEQUALIFIER_CONTROL)!=0 &&
                               (qualifiers &IEQUALIFIER_REPEAT)==0)
                        {
                            Action_SettingsFontSizePlus(app);
                        } else if(app->activeEditorObj == app->textEditorObj)
                        {
                            SetGdAttrs(app->textEditorObj, UTED_PutVanillaKey, key|(qualifiers<<16), TAG_END);
                        }
                    }
                    break;

                    case WMHI_CLOSEWINDOW:
                        /* now close button would just close the current text context */
                        EgTabs_CloseCurrentTab();
                        /* old: direct quitting ok = FALSE; */

                        break;

                    case WMHI_GADGETUP:
                    {
                        /* Queue gadget-up event for dispatch on main task context,
                         * alongside OM_NOTIFY messages from ICA_TARGET gadgets. */
                        ULONG senderId = result & WMHI_GADGETMASK;

                        BoopsiDelay_BeginMessage(DelayQueue, senderId);
                        BoopsiDelay_AddTag(DelayQueue, WMHI_GADGETUP, 1);
                        BoopsiDelay_EndMessage(DelayQueue);
                        break;
                    }
                    case WMHI_ICONIFY:
                        {
                            #define DO_ICONIFY 1
                            BMainWindow_Close(&app->mainwindow,app->window_obj,DO_ICONIFY);
                        }
                        break;
                    case WMHI_UNICONIFY:
                        {
                           BMainWindow_Show(&app->mainwindow,app->window_obj,&app->appSettings);
                            if (!CurrentMainWindow) cleanexit("can't re-open window");
                            UpdateStatusBar();
                        }
                        break;

                    case WMHI_MENUPICK:
                    {
                        /* Dispatch menu selection to action system */
                        UWORD menuCode = (UWORD)(result & WMHI_MENUMASK);
                        if (menuCode != MENUNULL) {
                            LONG actionID = EgMenu_ToActionID(&app->mainwindow.menu,
                                                              menuCode);
                            if (actionID >= 0) {
                                EgAction_Execute((ULONG)actionID,app);
                            }
                        }
                        /* reactivate gadget after menu use*/
                        // if(CurrentMainWindow)
                        // {
                        //     ActivateGadget(app->textEditorObj,CurrentMainWindow,NULL);
                        // }
                        break;
                    }
                    case WMHI_NEWSIZE:
                    {
                        SyncVScroller();
                        SyncHScroller();
                    }
                    break;

                    default:
                        break;
                }
            } /* end WM_HANDLEINPUT loop */

            /* Process delayed BOOPSI notifications.
             * Both WMHI_GADGETUP events (queued above) and OM_NOTIFY from
             * gadgets that have ICA_TARGET=TargetInstance land here, serialised
             * on the main task so it is safe to call any Intuition/gadget API.
             *
             * Prop-gadget drag fires many messages; pendingVTop / pendingHLeft
             * coalesce them so only one SetGadgetAttrs + RefreshGList per cycle. */
            if (DelayQueue && BoopsiDelay_HasMessages(DelayQueue)) {
                struct TagItem *msg;
                ULONG pendingVTop  = ~0UL;
                ULONG pendingHLeft = ~0UL;

                while ((msg = BoopsiDelay_NextMessage(DelayQueue)) != NULL) {
                    struct TagItem *ptag;
                    ULONG sender_ID = 0;

                    ptag = FindTagItem(GA_ID, msg);
                    if (ptag) sender_ID = ptag->ti_Data;

#ifdef HACKOS39SUPPORT
                    if (!ClickTabBase)
                    {
                        if (sender_ID >= GID_TABBAR_SAFE_0 &&
                            sender_ID < (ULONG)(GID_TABBAR_SAFE_0 + EG_MAX_TABS))
                        {
                            /* Fake tab button click — index encoded in GA_ID */
                            int newIdx = (int)(sender_ID - GID_TABBAR_SAFE_0);
                            SetAttrs(app->tabGadget,
                                     CLICKTAB_Current, (ULONG)newIdx, TAG_END);
                            EgTabs_SwitchTo(newIdx);
                            continue;
                        }
                        else if (sender_ID == GID_TABBAR_SAFE_PREV)
                        {
                        printf("got GID_TABBAR_SAFE_PREV\n");
                            SetGdAttrs(app->tabGadget,
                                     FAKETAB_ScrollBy, (ULONG)-1, TAG_END);
                            DoMethod(app->window_obj, WM_RETHINK);
                            continue;
                        }
                        else if (sender_ID == GID_TABBAR_SAFE_NEXT)
                        {
                     printf("got GID_TABBAR_SAFE_NEXT\n");
                            SetGdAttrs(app->tabGadget,
                                     FAKETAB_ScrollBy, (ULONG)1, TAG_END);
                            DoMethod(app->window_obj, WM_RETHINK);
                            continue;
                        }
                    }
#endif

                    switch (sender_ID) {

                        case GID_TABBAR:
                        {
                            ptag = FindTagItem(CLICKTAB_NodeClosed, msg);
                            if(ptag)
                            {
                                struct Node *tabToClose=(struct Node *)ptag->ti_Data;
                                if(tabToClose)
                                {   /* this is the tabs close button action ! */
                                    EgTabs_CloseTabByNode(tabToClose);
                                }
                            } else {
                                /*other clicktab interaction means: change current tab */
                                ULONG newIdx = 0;
                                GetAttr(CLICKTAB_Current, app->tabGadget, &newIdx);
                                EgTabs_SwitchTo((int)newIdx);
                            }
                            break;
                        }

                        case ID_TEXTEDITOR:
                            editorNeedRefresh = 1;
                            ptag = FindTagItem(UTEDN_ScrollChanged, msg);
                            if(ptag)
                            {
                                vscrollNeedRefresh = 1;
                                hscrollNeedRefresh = 1;
                            }
                            ptag = FindTagItem(UTED_SetPrivateActivation, msg);
                            if (ptag && ptag->ti_Data) {
                             // printf(" ID_TEXTEDITOR activated\n");
                                app->activeEditorObj = app->textEditorObj;
                                /*watch out SetGdAttrs() is only for main window */
                                SetAttrs(app->searchBox.searchEditor,
                                    UTED_SetPrivateActivation, FALSE, TAG_END);
                                SetAttrs(app->searchBox.replaceEditor,
                                    UTED_SetPrivateActivation, FALSE, TAG_END);
                            }
                            break;

                        case ID_SEARCH_EDITOR:
                            ptag = FindTagItem(UTED_SetPrivateActivation, msg);
                            if (ptag && ptag->ti_Data) {
                             // printf(" ID_SEARCH_EDITOR activated\n");
                                app->activeEditorObj = app->searchBox.searchEditor;
                                SetGdAttrs(app->textEditorObj,
                                    UTED_SetPrivateActivation, FALSE, TAG_END);
                                SetAttrs(app->searchBox.replaceEditor,
                                    UTED_SetPrivateActivation, FALSE, TAG_END);
                            }
                            break;

                        case ID_REPLACE_EDITOR:
                            ptag = FindTagItem(UTED_SetPrivateActivation, msg);
                            if (ptag && ptag->ti_Data) {
                            //  printf(" ID_REPLACE_EDITOR activated\n");
                                app->activeEditorObj = app->searchBox.replaceEditor;
                                SetGdAttrs(app->textEditorObj,
                                    UTED_SetPrivateActivation, FALSE, TAG_END);
                                SetAttrs(app->searchBox.searchEditor,
                                    UTED_SetPrivateActivation, FALSE, TAG_END);
                            }
                            break;

                        case GID_EMOJI_BUTTON:
                            ptag = FindTagItem(GA_Selected, msg);
                            if (ptag && ptag->ti_Data) {
                                OpenEmojiBoxWindow();
                            }
                            break;

                        case GID_UARROW: {
                            ULONG scrollTop = 0;
                            GetAttr(UTED_ScrollTop, app->textEditorObj, &scrollTop);
                            if (scrollTop > 0) scrollTop--;
                            SetGadgetAttrs((struct Gadget *)app->textEditorObj,
                                CurrentMainWindow, NULL,
                                UTED_ScrollTop, scrollTop, TAG_END);
                            SyncVScroller();
                            editorNeedRefresh = 1;
                            break;
                        }

                        case GID_DARROW: {
                            ULONG scrollTop = 0, lineCount = 1, vis = 1;
                            GetAttr(UTED_ScrollTop,         app->textEditorObj, &scrollTop);
                            GetAttr(UTED_RenderedLineCount, app->textEditorObj, &lineCount);
                            GetAttr(UTED_VisibleLines,      app->textEditorObj, &vis);
                            if (scrollTop + vis < lineCount) scrollTop++;
                            SetGadgetAttrs((struct Gadget *)app->textEditorObj,
                                CurrentMainWindow, NULL,
                                UTED_ScrollTop, scrollTop, TAG_END);
                            SyncVScroller();
                            editorNeedRefresh = 1;
                            break;
                        }

                        case GID_YPROP:
                            /* Coalesce: remember last dragged position, apply after loop */
                            ptag = FindTagItem(PGA_Top, msg);
                            if (ptag) pendingVTop = ptag->ti_Data;
                            break;

                        case GID_LARROW: {
                            ULONG scrollLeft = 0, step = 16;
                            GetAttr(UTED_ScrollLeft, app->textEditorObj, &scrollLeft);
                            if (scrollLeft > step) scrollLeft -= step; else scrollLeft = 0;
                            SetGadgetAttrs((struct Gadget *)app->textEditorObj,
                                CurrentMainWindow, NULL,
                                UTED_ScrollLeft, scrollLeft, TAG_END);
                            SyncHScroller();
                            editorNeedRefresh = 1;
                            break;
                        }

                        case GID_RARROW: {
                            ULONG scrollLeft = 0, maxWidth = 0, visible = 1, step = 16;
                            GetAttr(UTED_ScrollLeft,   app->textEditorObj, &scrollLeft);
                            GetAttr(UTED_MaxLineWidth, app->textEditorObj, &maxWidth);
                            GetAttr(GA_Width,          app->textEditorObj, &visible);
                            if (visible < 1) visible = 1;
                            if (maxWidth > visible && scrollLeft + step < maxWidth - visible)
                                scrollLeft += step;
                            else if (maxWidth > visible)
                                scrollLeft = maxWidth - visible;
                            SetGadgetAttrs((struct Gadget *)app->textEditorObj,
                                CurrentMainWindow, NULL,
                                UTED_ScrollLeft, scrollLeft, TAG_END);
                            SyncHScroller();
                            editorNeedRefresh = 1;
                            break;
                        }

                        case GID_XPROP:
                            /* Coalesce: remember last dragged position, apply after loop */
                            ptag = FindTagItem(PGA_Top, msg);
                            if (ptag) pendingHLeft = ptag->ti_Data;
                            break;

                        default:
                            break;

                    } /* end switch sender_ID */

                } /* end while BoopsiDelay_NextMessage */

                /* Apply coalesced prop-drag positions (last message wins) */
                if (pendingVTop != ~0UL) {
                    SetGadgetAttrs((struct Gadget *)app->textEditorObj,
                        CurrentMainWindow, NULL,
                        UTED_ScrollTop, pendingVTop, TAG_END);
                    vscrollNeedRefresh = 1;
                    editorNeedRefresh = 1;
                }
                if (pendingHLeft != ~0UL) {
                    SetGadgetAttrs((struct Gadget *)app->textEditorObj,
                        CurrentMainWindow, NULL,
                        UTED_ScrollLeft, pendingHLeft, TAG_END);
                    hscrollNeedRefresh = 1;
                    editorNeedRefresh = 1;
                }

            } /* end if BoopsiDelay_HasMessages */

            if(editorNeedRefresh && CurrentMainWindow)
            {
                RefreshGList(app->textEditorObj, CurrentMainWindow, NULL, 1);
                UpdateStatusBar();
            } else
            {
                if(vscrollNeedRefresh)
                {
                    SyncVScroller();
                }
                if(hscrollNeedRefresh)
                {
                    SyncHScroller();
                }
            }
        // if(app->doRefreshUI>0)
        // {
        //     //printf("app->doRefreshUI:%d\n",app->doRefreshUI);
        //     refreshUI_Apply();
        //     app->doRefreshUI = 0;
        // }


        } /* end main loop */
    }

#ifdef STACK_WATCH
    {
        struct Task *_sw_task = FindTask(NULL);
        int _sw_stacksize = (int)((int)_sw_task->tc_SPReg - (int)_sw_task->tc_SPLower);
        int *_sw_p = (int *)((int)_sw_task->tc_SPLower + 4);
        while (*_sw_p == (int)0xCAFEBABE && _sw_p < (int *)_sw_task->tc_SPReg)
            _sw_p++;
        printf("**** STACK_WATCH: total=%d  real use=%d\n",
               _sw_stacksize,
               (int)((int)_sw_task->tc_SPReg - (int)_sw_p));
    }
#endif

    return 0;
}

/* - - - - - - - - - CLEANUP - - - - - - - - - */

void exitclose(void)
{
 //   flushbdbprint();

//   printf("exitclose() app:%08x\n",(int)app);

    if (app)
    {

        EgSettingsView_Close(&app->settingsView);
        EgFontsView_Close(&app->fontsView);
        EmojiBoxWindow_Close(&app->emojiBoxWindow);
        EgSearchBox_Close(&app->searchBox);

        /* save settings*/
        AppSettings_Save(&app->appSettings);
        AppSettings_Close(&app->appSettings);



        /* Free tab bar resources */
        if (app->tabList) {
            struct Node *n, *next;
            for (n = app->tabList->lh_Head; (next = n->ln_Succ) != NULL; n = next) {
                if (ClickTabBase)
                    FreeClickTabNode(n);  /* real clicktab-allocated nodes */
                else
                    FreeVec(n);           /* plain AllocVec'd nodes (safe path) */
            }
            FreeVec(app->tabList);
            app->tabList = NULL;
        }
        {
            int i;
            for (i = 0; i < app->tabCount; i++) {
                FreeVec(app->tabContextNames[i]);
                app->tabContextNames[i] = NULL;
            }
        }

        EgSettingsView_Dispose(&app->settingsView);
        EgFontsView_Dispose(&app->fontsView);
        EgSearchBox_Dispose(&app->searchBox);
        EmojiBoxWindow_Dispose(&app->emojiBoxWindow);

        if(app->aboutRequester)
        {
            DisposeObject(app->aboutRequester);
            app->aboutRequester = NULL;
        }
        if(app->gotoLineRequester)
        {
            DisposeObject(app->gotoLineRequester);
            app->gotoLineRequester = NULL;
        }

        if(app->window_obj)
        {
            BMainWindow_Close(&app->mainwindow,app->window_obj,0);
            /* this should cascade all OM_DISPOSE: */
            DisposeObject(app->window_obj);
        }

        /* Close the BOOPSI message target */
        closeMessageTargetModel();


        /* Free BOOPSI classes (instances disposed by window cascade above) */
    #ifdef USE_STATIC_UNIBUTTON
        UniButton_Exit();
    #endif
    #ifdef USE_STATIC_UNITEXTEDITOR
        UniTextEditor_Exit();
    #else
     // TODO regular published  gadget exit.
    #endif
    #ifdef HACKOS39SUPPORT
        EgFakeTab_Free();
    #endif

        /* Delete message port */
        if (app->app_port)
            DeleteMsgPort(app->app_port);

        FreeVec(app);
        app = NULL;
    }

    /* Close locale */
// printf("locale things\n");

    EgLocale_Close();
    if (LocaleBase) {
        CloseLibrary((struct Library *)LocaleBase);
        LocaleBase = NULL;
    }
// printf("aft locale things\n");


    // if(CyberGfxBase)  { CloseLibrary(CyberGfxBase);  CyberGfxBase  = NULL; }
    // if(DataTypesBase) { CloseLibrary(DataTypesBase); DataTypesBase = NULL; }
// printf("bef closing\n");
    /* Close all other libraries in reverse order */
    {
        LibraryEntry *entry;
        int i;

        for (i = 0; libraryTable[i].name != NULL; i++)
            ;

        for (i = i - 1; i >= 0; i--) {
            entry = &libraryTable[i];
            if (*(entry->base)) {
                CloseLibrary(*(entry->base));
                *(entry->base) = NULL;
            }
        }
    }
}


/* =========================================================================
 * SyncVScroller / SyncHScroller
 * Push the current editor scroll state to the border prop gadgets.
 * =========================================================================
 */
void SyncVScroller(void)
{
    ULONG top = 0, total = 1, visible = 1;
    struct Gadget *yprop = app->borderScroll.yprop;
    if (!yprop || !app->textEditorObj || !CurrentMainWindow) return;
    GetAttr(UTED_ScrollTop,         app->textEditorObj, &top);
    GetAttr(UTED_RenderedLineCount, app->textEditorObj, &total);
    GetAttr(UTED_VisibleLines,      app->textEditorObj, &visible);
    if (total   < 1) total   = 1;
    if (visible < 1) visible = 1;
    SetGadgetAttrs(yprop, CurrentMainWindow, NULL,
        PGA_Top,     top,
        PGA_Total,   total,
        PGA_Visible, visible,
        TAG_END);
}

void SyncHScroller(void)
{
    ULONG scrollLeft = 0, maxWidth = 0, visible = 1;
    struct Gadget *xprop = app->borderScroll.xprop;
    if (!xprop || !app->textEditorObj || !CurrentMainWindow) return;
    GetAttr(UTED_ScrollLeft,   app->textEditorObj, &scrollLeft);
    GetAttr(UTED_MaxLineWidth, app->textEditorObj, &maxWidth);
    GetAttr(GA_Width,          app->textEditorObj, &visible);
    if (visible < 1) visible = 1;
    if (maxWidth < visible) maxWidth = visible;
    SetGadgetAttrs(xprop, CurrentMainWindow, NULL,
        PGA_Top,     scrollLeft,
        PGA_Total,   maxWidth,
        PGA_Visible, visible,
        TAG_END);
}

/* =========================================================================
 * UpdateStatusBar
 * =========================================================================
 */
void UpdateStatusBar()
{
    char buf[128];
    ULONG curLine = 0, curChar = 0, lineCount = 0;

    if (!app->textEditorObj || !app->statusBarLabel) return;

    GetAttr(UTED_CursorLine, app->textEditorObj, &curLine);
    GetAttr(UTED_CursorChar, app->textEditorObj, &curChar);
    GetAttr(UTED_LineCount,  app->textEditorObj, &lineCount);

    snprintf(buf, sizeof(buf)-1, " Line %lu, Col %lu  |  %lu lines",
             curLine + 1, curChar + 1, lineCount);


    SetGdAttrs(app->statusBarLabel, GA_Text, &buf[0],TAG_END);

    SyncVScroller();
    SyncHScroller();
}

void OpenSearchBox()
{
    if(!app) return;
    if(!CurrentMainScreen) return;

    if(!app->searchBox.windowObj)
    {
        ULONG pointSize = 14;
        if(CurrentMainScreen->Font) pointSize = CurrentMainScreen->Font->ta_YSize;
        EgSearchBox_Create(&app->searchBox,pointSize);
    }
    if(app->searchBox.windowObj /* no because refocus && !app->searchBox.window*/)
    {
        EgSearchBox_Open(&app->searchBox);
    }
}
void CloseSearchBox()
{
   if(!app) return;

    if(app->searchBox.window)
    {
        EgSearchBox_Close(&app->searchBox);
    }
}

void OpenEmojiBoxWindow(void)
{
    if (!app || !CurrentMainScreen) return;
    EmojiBoxWindow_Open(&app->emojiBoxWindow);
}

void CloseEmojiBoxWindow(void)
{
    if (!app) return;
    EmojiBoxWindow_Close(&app->emojiBoxWindow);
}

