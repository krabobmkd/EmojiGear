/*
 * FriendSh3ep - a Mastodon client for AmigaOS.
 *
 * Main application: library init, BOOPSI window+layout creation, event
 * loop, cleanup.  Structured after EmojiGear/emojigear.c: a struct App
 * holding every persistent BOOPSI object, an OpenLibrary() table, and a
 * Wait()/WM_HANDLEINPUT main loop.  fs3eboopsimainwindow.c handles
 * open/close/iconify of the window, fs3eboopsimessage.c redirects BOOPSI
 * notifications from gadgets (ICA_TARGET) to this main process.
 *
 * Layout structure:
 *   mainlayout (layout.gadget, VERT, borderless)
 *     Part A: titleBarLayout  (TitleBarLayoutClass)  — 2 × dpiH rows
 *     Part B: navBarLayout    (NavBarLayoutClass)    — 1 or 2 × dpiH rows
 *     Part C: tootTimeline (TootTimelineClass)              — fills rest
 *
 * dpiHeight (default 14 px) acts as the "DPI factor": every row in the
 * mobile-style UI is exactly one dpiHeight pixel tall.
 *
 * See ARCHITECTURE.md for the overall design and roadmap.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <clib/alib_protos.h>

#include <intuition/screens.h>
#include <intuition/icclass.h>
#include <intuition/gadgetclass.h>

#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/layers.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <proto/dos.h>
#include <devices/inputevent.h>
#include <graphics/clip.h>

#include <proto/window.h>
#include <classes/window.h>

#include <proto/layout.h>
#include <gadgets/layout.h>

#include <proto/button.h>
#include <gadgets/button.h>

#include <proto/label.h>
#include <images/label.h>

#include <proto/unitexteditor.h>
#include <gadgets/unitexteditor.h>

#include <libraries/utf8rastport.h>
#include <proto/utf8rastport.h>

#include <proto/chooser.h>
#include <gadgets/chooser.h>

#include <proto/locale.h>
#include <libraries/locale.h>

#include <workbench/startup.h>

#include "compilers.h"
#include "bdbprintf.h"
#include "fs3egadgetid.h"

#include "friendsh3ep.h"
#include "fs3eboopsimainwindow.h"
#include "fs3eboopsimessage.h"
#include "fs3eloginview.h"
#include "fs3etootview.h"
#include "fs3ethemeview.h"
#include "fs3elocale.h"
#include "fs3emenu.h"
#include "fs3eaction.h"
#include "fs3esettings.h"

#include "UniButtonP9/unibuttonp9.h"
#include "TitleBarLayout/fs3etitlebar.h"
#include "NavBarLayout/fs3enavbar.h"
#include "TootTimeline/fs3etoottimeline.h"

#include "network_fs3e/fs3enet.h"

const char *pVersion = "$VER: FriendSh3ep " FRIENDSH3EP_VERSION;

struct Task *myTask = NULL;

void wait2sec() {
int i;
    for(i=0;i<25;i++) WaitTOF();
}

/* Window drag state — written by TitleBarLayout GM_HITTEST (inside
 * WM_HANDLEINPUT), read by WMHI_MOUSEMOVE in the same drain loop. */
BOOL windowDragActive      = FALSE;
WORD windowDragLastScreenX = 0;
WORD windowDragLastScreenY = 0;

/* Window resize state — written by TootTimeline GM_HITTEST,
 * read by WMHI_MOUSEMOVE.  Width is snapped to multiples of 16.
 *
 * lastTargetW/H track the size we most recently *requested* via SizeWindow.
 * SizeWindow is asynchronous (posts to Intuition), so CurrentMainWindow->Width
 * lags behind; using our own last-requested value avoids the oscillation that
 * would otherwise occur at every 16-pixel snap boundary. */
BOOL windowResizeActive    = FALSE;
WORD windowResizeStartSX   = 0;   /* screen X at drag start */
WORD windowResizeStartSY   = 0;   /* screen Y at drag start */
WORD windowResizeStartW    = 0;   /* window width  at drag start */
WORD windowResizeStartH    = 0;   /* window height at drag start */
WORD windowResizeLastTargetW = 0; /* last width  we sent to SizeWindow */
WORD windowResizeLastTargetH = 0; /* last height we sent to SizeWindow */

/* Library bases */
struct GfxBase       *GfxBase       = NULL;
struct IntuitionBase *IntuitionBase = NULL;
struct Library       *UtilityBase   = NULL;
struct Library         *LayersBase    = NULL;
struct Library         *IconBase      = NULL;
struct Library         *AslBase       = NULL;
struct Library         *GadToolsBase  = NULL;

/* BOOPSI class libraries */
struct Library *WindowBase         = NULL;
struct Library *LayoutBase         = NULL;
struct Library *ButtonBase         = NULL;
struct Library *LabelBase          = NULL;
struct Library *CheckboxBase       = NULL;
struct Library *ChooserBase        = NULL;
struct Library *GetFileBase        = NULL;
struct Library *UniTextEditorBase  = NULL;
struct Library *UniButtonBase      = NULL;

/* utf8rastport.library – required by UniButtonP9 (private UniButton class) */
struct Library *URPBase  = NULL;
/* datatypes.library v44 – used by bmimage.c for picture.datatype image loading */
struct Library *DataTypesBase = NULL;
/* images/bevel.image – optional, used by UniButton bevel frames */
struct Library *BevelBase = NULL;


/* locale.library - soft failure (English fallback) */
struct LocaleBase *LocaleBase = NULL;

typedef struct {
    const char     *name;
    ULONG           version;
    struct Library **base;
} LibraryEntry;

static LibraryEntry libraryTable[] = {
    {"graphics.library",            40, (struct Library **)&GfxBase},
    {"intuition.library",           40, (struct Library **)&IntuitionBase},
    {"utility.library",             40, &UtilityBase},
    {"layers.library",    39, &LayersBase},
    {"icon.library",      39, &IconBase},
    {"asl.library",       39, &AslBase},
    {"gadtools.library",  39, &GadToolsBase},
    {"window.class",                42, &WindowBase},
    {"gadgets/layout.gadget",       42, &LayoutBase},
    {"gadgets/button.gadget",       42, &ButtonBase},
    {"images/label.image",          42, &LabelBase},
    {"gadgets/checkbox.gadget",      42, &CheckboxBase},
    {"gadgets/chooser.gadget",       44, &ChooserBase},
    {"gadgets/getfile.gadget",       42, &GetFileBase},
    {"gadgets/unitexteditor.gadget",  4, &UniTextEditorBase},
    {"gadgets/unibutton.gadget",     4, &UniButtonBase},
    {"utf8rastport.library",         4, &URPBase},
    {"datatypes.library",           44, &DataTypesBase},
    {NULL, 0, NULL}
};

struct App *app = NULL;

/* Default DPI factor: 1 row = 14 pixels. */
#define DEFAULT_DPI_HEIGHT 14

void exitclose(void);

void cleanexit(const char *pmessage)
{
    if (pmessage) printf("%s\n", pmessage);
    exit(0);
}

/* - - - - - - - - - - - - - - - - - - - HELPERS - - - - - - - - - - - - - */
/* note: this is reached when messages are not used by boopsi gadgets */
static ULONG IDCMPDispatch(
        REG(a0,struct Hook *hook),
        REG(a2,APTR object),
        REG(a1, struct IntuiMessage *IMsg))
{

    if(IMsg->Class == IDCMP_MOUSEBUTTONS)
    {
        if(IMsg->Code == (IECODE_LBUTTON | IECODE_UP_PREFIX))
        {
            windowDragActive   = FALSE;
            windowResizeActive = FALSE;
        }
    }/* else
    if(IMsg->Class == IDCMP_RAWKEY)
    {
        printf("rk\n");
    }*/

    return 0;
}
static struct Hook idcmpHook ={
    {NULL,NULL},
    (HOOKFUNC)&IDCMPDispatch,
    NULL,0
};
/*
 * FS3EApp_SetButtonFontSize – reload app->buttonDC fonts at a new point size,
 * then invalidate all UniButtonP9 button caches so they rebuild on next render.
 *
 * Default fonts:
 *   LiberationSans-Regular.ttf  – Latin / general alphabet
 *   OpenMoji-black-glyf.ttf     – Emoji glyphs
 *
 * Must be called after app and app->buttonDC are initialised.
 * Buttons may or may not exist yet; the null-checks below handle both cases.
 */
static void FS3EApp_SetButtonFontSize(ULONG pointSize)
{
    if (!app || !app->buttonDC) return;

    URPDC_FlushFonts(app->buttonDC);
    URPDC_AddFont(app->buttonDC,
        app->settings.primaryFontPath ? app->settings.primaryFontPath
                                      : "LiberationSans-Regular.ttf",
        (int)pointSize, 0);
    if (app->settings.fallback1FontPath)
        URPDC_AddFont(app->buttonDC, app->settings.fallback1FontPath, (int)pointSize, 0);
    if (app->settings.fallback2FontPath)
        URPDC_AddFont(app->buttonDC, app->settings.fallback2FontPath, (int)pointSize, 0);
    URPDC_AddFont(app->buttonDC,
        app->settings.emojiFontPath ? app->settings.emojiFontPath
                                    : "OpenMoji-black-glyf.ttf",
        (int)pointSize, 0);

    /* Notify existing buttons: UBTP9_PointSize triggers cache invalidation.
     * Gadget pointers are NULL before buttons are created, so this is safe.
     * titlebar buttons are now regular button.gadget — no UBTP9_PointSize. */
    {
        int i;
#define RESIZE_BTN(o) if (o) SetAttrs((Object *)(o), UBTP9_PointSize, pointSize, TAG_DONE)
        /* titlebar_postsLabel / newPostsLabel disabled */
        for (i = 0; i < 8; i++) RESIZE_BTN(app->nav_btns[i]);
#undef RESIZE_BTN
    }
}

/* Re-apply font settings from app->settings to all draw contexts.
 * Called by fs3ethemeview.c / fs3eaction.c after font or rendering options change. */
void FS3EApp_ApplyFontSettings(void)
{
    ULONG prefFlags;
    if (!app || !app->window_obj || !app->buttonDC) return;

    /* --- Button bar DC (nav bar, title bar) --- */
    prefFlags = URP_PREF_CLUTMODE_NOMASK;
    if (app->settings.antialias)    prefFlags |= URP_PREF_ANTIALIAS;
    if (app->settings.emojiQuality) prefFlags |= URP_PREF_HIGHFILTERING;
    URPDC_SetPreferenceFlags(app->buttonDC, prefFlags);
    FS3EApp_SetButtonFontSize((ULONG)app->settings.fontPointSize);

    /* --- Timeline style DCs (dcNormal, dcUsername, dcMini) ---
     * Sized proportionally: normal = base, username = base+1, mini = base-2.
     * Re-setting TTIMELINE_Style forces the gadget to re-cache line metrics
     * and do a full relayout + redraw. */
    FS3EStyle_SetFontSize(&app->style,
                          app->settings.fontPointSize,
                          app->settings.primaryFontPath,
                          app->settings.fallback1FontPath,
                          app->settings.fallback2FontPath,
                          app->settings.colorEmojiFontPath);

    if (CurrentMainWindow && app->tootTimeline)
        SetGadgetAttrs((struct Gadget *)app->tootTimeline,
                       CurrentMainWindow, NULL,
                       TTIMELINE_Style, (ULONG)&app->style,
                       TAG_DONE);

    /* Recompute minimum gadget sizes and relayout the whole window */
    DoMethod(app->window_obj, WM_RETHINK);
}

/* Create one UniButtonP9, click arrives via WMHI_GADGETUP.
 * dpiH is reserved for future use; font size is controlled by app->buttonDC. */
static Object *makeBtn(ULONG gadID, const char *label, UWORD dpiH)
{
    (void)dpiH;
    return (Object *)NewObject(UniButtonP9Class, NULL,
        GA_ID,                  gadID,
        GA_Text,                (ULONG)label,
        UBTP9_URPDrawContext,   (ULONG)app->buttonDC,
        UBTP9_BevelStyle,       BVS_BUTTON, //BVS_NONE,
        // UBTP9_TopMargin,        0,
        // UBTP9_BottomMargin,     0,
        TAG_END);
}

/* Create a read-only label using UniButtonP9. */
static Object *makeLabel(const char *text, UWORD dpiH)
{
    (void)dpiH;
    return (Object *)NewObject(UniButtonP9Class, NULL,
        GA_Text,                (ULONG)text,
        GA_ReadOnly,            TRUE,
        UBTP9_URPDrawContext,   (ULONG)app->buttonDC,
        UBTP9_BevelStyle,       BVS_NONE,
        UBTP9_Transparent,      TRUE,
        UBTP9_TopMargin,        0,
        UBTP9_BottomMargin,     0,
        TAG_END);
}

/* - - - - - - - - - - - - - - - - - - - MAIN - - - - - - - - - - - - - - - */

int main(int argc, char **argv)
{
    UWORD dpiH = DEFAULT_DPI_HEIGHT;

    if (SysBase->LibNode.lib_Version < 40) {
        printf("FriendSh3ep needs OS3.9 (v40) or OS3.2, you may upgrade.\n");
        return 1;
    }
    myTask = FindTask(NULL);
    atexit(&exitclose);

    {
        LibraryEntry *entry;
        for (entry = libraryTable; entry->name != NULL; entry++) {
            *(entry->base) = OpenLibrary(entry->name, entry->version);
            if (!*(entry->base)) {
                printf("Can't open %s v%u\n",
                       entry->name, (unsigned int)entry->version);
                return 1;
            }
        }
    }

    BevelBase = OpenLibrary("images/bevel.image", 32); /* optional, no check */

    LocaleBase = (struct LocaleBase *)OpenLibrary("locale.library", 38);
    FS3ELocale_Init("FriendSh3ep.catalog", 0);
    FS3EAction_Init();

    app = (struct App *)AllocVec(sizeof(struct App), MEMF_CLEAR);
    if (!app) cleanexit("Can't allocate app");

    FS3ESettings_Load(&app->settings);

    if (!FS3EMsg_Init()) cleanexit("Can't create BOOPSI message target");

    /* --- Private BOOPSI classes ---------------------------------------- */
    if (!UniButtonP9_Init())    cleanexit("Can't init UniButtonP9 class");
    if (!TitleBarLayout_Init()) cleanexit("Can't init TitleBarLayout class");
    if (!NavBarLayout_Init())   cleanexit("Can't init NavBarLayout class");
    if (!TootTimeline_Init())   cleanexit("Can't init TootTimeline class");

    /* --- Shared button draw context (utf8rastport, fonts, emoji) -------- */
    app->buttonDC = URPDC_Create(NULL);
    if (!app->buttonDC) cleanexit("Can't create button draw context");
    {
        ULONG prefFlags = URP_PREF_CLUTMODE_NOMASK;
        if (app->settings.antialias)    prefFlags |= URP_PREF_ANTIALIAS;
        if (app->settings.emojiQuality) prefFlags |= URP_PREF_HIGHFILTERING;
        URPDC_SetPreferenceFlags(app->buttonDC, prefFlags);
    }
    FS3EApp_SetButtonFontSize((ULONG)app->settings.fontPointSize);

    /* TODO: style/theme settings color and fonts have to be set with loaded settings
        (or from an external theme file ?)
    */
    FS3EStyle_InitDefaults(&app->style);
 printf("FS3ENet_Start\n");
    /* --- Network process ------------------------------------------------ */
    app->netRequestPort = FS3ENet_Start(app->settings.cachePath);
    if (!app->netRequestPort)
        printf("FriendSh3ep: network process failed - continuing without\n");
 printf("FS3ENet_Start end\n");
 printf("windows creates\n");
    /* --- Classic BOOPSI sub-windows ------------------------------------- */
    if (!FS3ELoginView_Create(&app->loginView, 14))
        cleanexit("Can't create login view");

    if (!FS3ETootView_Create(&app->tootView, 14))
        cleanexit("Can't create toot view");

    if (!FS3EThemeView_Create(&app->themeView, LOC(MSG_THEMEV_TITLE)))
        cleanexit("Can't create theme view");


    /* ================================================================== */
    /* Part A: title bar children (7 gadgets)                              */
    /* ================================================================== */

    app->titlebar_closeBtn   = (Object *)NewObject(BUTTON_GetClass(), NULL,
        GA_ID,   GID_TITLEBAR_CLOSE,
        //GA_Text, "X",
        GA_RelVerify, TRUE,
         TAG_DONE);
    app->titlebar_iconifyBtn = (Object *)NewObject(BUTTON_GetClass(), NULL,
        GA_ID,   GID_TITLEBAR_ICONIFY,
        //GA_Text, "-",
        GA_RelVerify, TRUE,
         TAG_DONE);
    app->titlebar_altposBtn  = (Object *)NewObject(BUTTON_GetClass(), NULL,
        GA_ID,   GID_TITLEBAR_ALTPOS,
        //GA_Text, "=",
        GA_RelVerify, TRUE,
         TAG_DONE);
    app->titlebar_depthBtn   = (Object *)NewObject(BUTTON_GetClass(), NULL,
        GA_ID,   GID_TITLEBAR_DEPTH,
        //GA_Text, "^",
        GA_RelVerify, TRUE,
         TAG_DONE);
    app->titlebar_userIcon   = (Object *)NewObject(BUTTON_GetClass(), NULL,
        GA_Width,  app->style.avatarSize,
        GA_Height, app->style.avatarSize,
        GA_RelVerify, TRUE,
        TAG_DONE);
    /* titlebar_postsLabel and titlebar_newPostsLabel disabled */
    /*
    app->titlebar_postsLabel    = makeLabel("Posts:0", dpiH);
    app->titlebar_newPostsLabel = makeLabel("New:0",   dpiH);
    */

    if (!app->titlebar_closeBtn   || !app->titlebar_iconifyBtn ||
        !app->titlebar_altposBtn  || !app->titlebar_depthBtn   ||
        !app->titlebar_userIcon)
        cleanexit("Can't create title bar gadgets");

    app->titleBarLayout = (Object *)NewObject(TitleBarLayoutClass, NULL,
        LAYOUT_BevelStyle, BVS_NONE,
        LAYOUT_SpaceOuter, FALSE,
        LAYOUT_SpaceInner, FALSE,
        TBLAYOUT_DpiHeight, (ULONG)dpiH,
        TBLAYOUT_Style,     (ULONG)&app->style,
        /* children in required order (see fs3etitlebar.h) */
        LAYOUT_AddChild, (ULONG)app->titlebar_closeBtn,
        LAYOUT_AddChild, (ULONG)app->titlebar_iconifyBtn,
        LAYOUT_AddChild, (ULONG)app->titlebar_altposBtn,
        LAYOUT_AddChild, (ULONG)app->titlebar_depthBtn,
        LAYOUT_AddChild, (ULONG)app->titlebar_userIcon,
        /* titlebar_postsLabel and titlebar_newPostsLabel disabled */
        TAG_END);
    if (!app->titleBarLayout) cleanexit("Can't create title bar layout");

    /* ================================================================== */
    /* Part B: navigation bar children (8 buttons)                         */
    /* ================================================================== */
// \xf0\x9f\x8f\xa0
// single silhouette 	%F0%9F%91%A4
    app->nav_btns[0] = makeBtn(GID_NAV_HOME,          "\xE2\x8C\x82 Home",      dpiH);
    app->nav_btns[1] = makeBtn(GID_NAV_NOTIFICATIONS, "\xF0\x9F\x9A\x80 Notif.",    dpiH);
    app->nav_btns[2] = makeBtn(GID_NAV_LOCAL,         "\xF0\x9F\x91\xA5 Local",     dpiH);
    app->nav_btns[3] = makeBtn(GID_NAV_FEDERATED,     "\xF0\x9F\x8C\x8E Fed.",      dpiH);
    app->nav_btns[4] = makeBtn(GID_NAV_NEWTOOT,       "\xF0\x9F\x97\xA3 Toot+",     dpiH);
    app->nav_btns[5] = makeBtn(GID_NAV_SEARCH,        "\xF0\x9F\x94\x8D Search",    dpiH);
    app->nav_btns[6] = makeBtn(GID_NAV_SETTINGS,      "\xE2\x9A\x99 Settings",  dpiH);
    app->nav_btns[7] = makeBtn(GID_NAV_ACCOUNTS,      "\xF0\x9F\x91\xA4 Accounts",  dpiH);

    {
        int i;
        for (i = 0; i < 8; i++)
            if (!app->nav_btns[i]) cleanexit("Can't create nav bar button");
    }

    app->navBarLayout = (Object *)NewObject(NavBarLayoutClass, NULL,
        LAYOUT_BevelStyle, BVS_NONE,
        LAYOUT_SpaceOuter, FALSE,
        LAYOUT_SpaceInner, FALSE,
        NBLAYOUT_DpiHeight, (ULONG)dpiH,
        /* children in required order (see fs3enavbar.h) */
        LAYOUT_AddChild, (ULONG)app->nav_btns[0],
        LAYOUT_AddChild, (ULONG)app->nav_btns[1],
        LAYOUT_AddChild, (ULONG)app->nav_btns[2],
        LAYOUT_AddChild, (ULONG)app->nav_btns[3],
        LAYOUT_AddChild, (ULONG)app->nav_btns[4],
        LAYOUT_AddChild, (ULONG)app->nav_btns[5],
        LAYOUT_AddChild, (ULONG)app->nav_btns[6],
        LAYOUT_AddChild, (ULONG)app->nav_btns[7],
        TAG_END);
    if (!app->navBarLayout) cleanexit("Can't create nav bar layout");

    /* ================================================================== */
    /* Part C: toot timeline                                               */
    /* ================================================================== */
 printf("create ttl &app->style:%08x\n",(int)&app->style);
    app->tootTimeline = (Object *)NewObject(TootTimelineClass, NULL,
        TTIMELINE_Style, (ULONG)(&app->style),
        TTIMELINE_DpiHeight, (ULONG)dpiH,
        ICA_TARGET, (ULONG)TargetInstance,
        GA_ID,      GID_TTIMELINE,
        TAG_END);
 printf("end create ttl\n");
    if (!app->tootTimeline) cleanexit("Can't create toot timeline");
 flushbdbprint();

    /* ---- Fake posts for layout testing (oldest first = prepended bottom-up) ---- */
    {
        static const TTLPostSetup fakePosts[] = {
            {
                "Krabob",
                "@krabob@amiga.social",
                "Just released EmojiGear 4.3 for AmigaOS \xf0\x9f\x8e\x89 "
                "Emoji now render on AGA screens using the new CLUT palette "
                "blending path. Grab it from Aminet!",
                "2h"
            },
            {
                "Boing Ball",
                "@boing@commodore.social",
                "Reminder: the original Amiga demo still runs faster than "
                "most modern web pages \xf0\x9f\x8f\x80\xf0\x9f\x94\xb4\xf0\x9f\x94\xb5",
                "5h"
            },
            {
                "Paula Chip",
                "@paula@demoscene.social",
                "Four-channel 8-bit audio, hardware sprites, blitter DMA "
                "and a cooperative multitasker - the Amiga was decades ahead. "
                "No wonder we never let go.",
                "9h"
            },
            {
                "Workbench Fan",
                "@wbfan@amiga.social",
                "Pro tip: you can drag the Workbench screen down to reveal "
                "a CLI behind it. Most people never discover this \xf0\x9f\x92\xbb",
                "1d"
            },
            {
                "Guru Meditation",
                "@guru@amiga.social",
                "Software failure. Press left mouse button to continue.\n"
                "Guru Meditation #00000003.00C0FFEE\n"
                "(just kidding, everything is fine)",
                "1d"
            },
            {
                "AmiNet Bot",
                "@aminetbot@fosstodon.org",
                "New upload: utf8rastport.library 1.2 - Unicode text rendering "
                "for AmigaOS RastPorts via FreeType2. Supports TrueType, "
                "OpenType and color emoji. Tested on OS3.1/3.2/3.9 AGA and RTG.",
                "2d"
            },
        };
        int i;
        /* Prepend oldest first so newest ends up at top */

        for (i = (int)(sizeof(fakePosts)/sizeof(fakePosts[0])) - 1; i >= 0; i--) {
            SetAttrs(app->tootTimeline,
                TTIMELINE_AddPost, (ULONG)&fakePosts[i],
                TAG_DONE);
        }

    }
 flushbdbprint();

    /* ================================================================== */
    /* Root layout (A + B + C, vertical, borderless, no gaps)             */
    /* ================================================================== */

    app->mainlayout = (Object *)NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_BackFill, NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
        LAYOUT_BevelStyle,  BVS_NONE,
        LAYOUT_SpaceOuter,  FALSE,
        LAYOUT_SpaceInner,  FALSE,

        LAYOUT_AddChild,    (ULONG)app->titleBarLayout,
            CHILD_WeightedHeight, 0,

        LAYOUT_AddChild,    (ULONG)app->navBarLayout,
            CHILD_WeightedHeight, 0,

        LAYOUT_AddChild,    (ULONG)app->tootTimeline,
            CHILD_WeightedHeight, 100,

        TAG_END);
    if (!app->mainlayout) cleanexit("Can't create main layout");

    /* ================================================================== */
    /* Window                                                               */
    /* ================================================================== */

    app->app_port = CreateMsgPort();

    app->window_obj = (Object *)NewObject(WINDOW_GetClass(), NULL,
        WA_Left,    40,
        WA_Top,     40,
        WA_Width,   400,
        WA_Height,  300,

        WA_IDCMP,   IDCMP_GADGETUP | IDCMP_NEWSIZE | IDCMP_RAWKEY
                    | IDCMP_MOUSEMOVE | IDCMP_MOUSEBUTTONS | IDCMP_MENUPICK,
        WA_Flags,
            WFLG_BORDERLESS |
            WFLG_ACTIVATE   |
            WFLG_SMART_REFRESH,
        WA_ReportMouse,TRUE, //test
        WA_Title,            NULL,
        WINDOW_ParentGroup,  (ULONG)app->mainlayout,
        WINDOW_IconTitle,    (ULONG)"FriendSh3ep",
        WINDOW_AppPort,      (ULONG)app->app_port,

        WINDOW_IDCMPHook,(ULONG)&idcmpHook,
        WINDOW_IDCMPHookBits, IDCMP_MOUSEBUTTONS | IDCMP_RAWKEY /*| IDCMP_VANILLAKEY*/,


        TAG_END);
    if (!app->window_obj) cleanexit("Can't create window");

    flushbdbprint();

    FS3EMain_Show(&app->mainwindow, app->window_obj);
    if (!CurrentMainWindow) cleanexit("Can't open window");

    FS3EMenu_Create(&app->menu, CurrentMainScreen, CurrentMainWindow);

    /* - - - Input Event Loop - - - */
    {
        ULONG winsignal;
        BOOL  ok = TRUE;
        ULONG refreshFlags = 0;

#define reflags_subjectEditor 1
#define reflags_bodyEditor    2
#define reflags_tootTimeLine    4
        GetAttr(WINDOW_SigMask, app->window_obj, &winsignal);

        while (ok)
        {
            ULONG result, waitedSignals, currentSignals;
            ULONG loginSig, tootSig;

            flushbdbprint();

            loginSig = FS3ELoginView_GetSignalMask(&app->loginView);
            tootSig  = FS3ETootView_GetSignalMask(&app->tootView);
            ULONG themeSig = FS3EThemeView_GetSignalMask(&app->themeView);

            waitedSignals = winsignal | loginSig | tootSig | themeSig |
                (1L << app->app_port->mp_SigBit) |
                SIGBREAKF_CTRL_C |
                SIGBREAKF_CTRL_F;

            currentSignals = Wait(waitedSignals);

            if (currentSignals & SIGBREAKF_CTRL_C) exit(0);

            while ((result = DoMethod(app->window_obj, WM_HANDLEINPUT, NULL))
                   != WMHI_LASTMSG)
            {
                flushbdbprint();
                switch (result & WMHI_CLASSMASK)
                {
                    case WMHI_CLOSEWINDOW:
                        ok = FALSE;
                        break;

                    case WMHI_GADGETUP:
                    {
                        /* UniButton gadgets use GMR_VERIFY + GACT_RELVERIFY so
                         * clicks arrive here with the gadget's GA_ID. */
                        ULONG senderId = result & WMHI_GADGETMASK;
                        printf("WMHI_GADGETUP senderId:%d\n",senderId);
                        BoopsiDelay_BeginMessage(DelayQueue, senderId);
                        BoopsiDelay_AddTag(DelayQueue, WMHI_GADGETUP, 1);
                        BoopsiDelay_EndMessage(DelayQueue);
                        break;
                    }

                    case WMHI_ICONIFY:
                        FS3EMenu_Close(&app->menu, CurrentMainWindow);
                        FS3EMain_Close(&app->mainwindow, app->window_obj, TRUE);
                        break;

                    case WMHI_UNICONIFY:
                        FS3EMain_Show(&app->mainwindow, app->window_obj);
                        if (!CurrentMainWindow) cleanexit("can't re-open window");
                        FS3EMenu_Create(&app->menu, CurrentMainScreen, CurrentMainWindow);
                        break;

                    case WMHI_RAWKEY:
                    {
                        ULONG key = result & 0x07f;
                        ULONG isUp = (result & 0x080);
                        ULONG qualifiers=0;
                        int keyUsed=0;
                        if (key == 0x45) ok = FALSE; /* Escape */

                        GetAttr(WINDOW_Qualifier,app->window_obj,&qualifiers);
                        /* ctrl- and ctrl+ change font size */
                        if((qualifiers & IEQUALIFIER_CONTROL) !=0 &&
                            !(qualifiers & IEQUALIFIER_REPEAT) && !isUp)
                        {
                            if(key == 0x4A) Action_FontSizeMinus(app); // "-"
                            else if(key == 0x5E) Action_FontSizePlus(app);  // "+"

                        }
                    }
                    break;
                    case WMHI_MENUPICK:
                    {
                        UWORD menuCode = (UWORD)(result & WMHI_MENUMASK);
                        if (menuCode != MENUNULL) {
                            LONG actionID = FS3EMenu_ToActionID(&app->menu, menuCode);
                            if (actionID >= 0)
                                FS3EAction_Execute((ULONG)actionID, app);
                        }
                        break;
                    }
                    case WMHI_MOUSEMOVE:
                    {
                        if (windowDragActive && CurrentMainWindow) {
                            struct Screen *scr = CurrentMainWindow->WScreen;
                            WORD sx = scr->MouseX;
                            WORD sy = scr->MouseY;
                            LONG dx = (LONG)sx - (LONG)windowDragLastScreenX;
                            LONG dy = (LONG)sy - (LONG)windowDragLastScreenY;
                            windowDragLastScreenX = sx;
                            windowDragLastScreenY = sy;
                            if (dx || dy)
                               MoveWindow(CurrentMainWindow, dx, dy);
                        }
                        else if (windowResizeActive && CurrentMainWindow) {
                            struct Screen *scr = CurrentMainWindow->WScreen;
                            LONG targetW, targetH, dx, dy;

                            targetW = (LONG)windowResizeStartW
                                      + (LONG)scr->MouseX - (LONG)windowResizeStartSX;
                            targetH = (LONG)windowResizeStartH
                                      + (LONG)scr->MouseY - (LONG)windowResizeStartSY;

                            /* Clamp to minimum window size */
                            if (targetW < 320) targetW = 320;
                            if (targetH < 240) targetH = 240;
                            /* Snap width to multiple of 16 (floor) */
                            targetW = (targetW / 16) * 16;

                            /* Compute delta against our last *requested* size, not
                             * CurrentMainWindow->Width/Height.  SizeWindow() is
                             * async — the struct lags, causing oscillation at snap
                             * boundaries. */
                            dx = targetW - (LONG)windowResizeLastTargetW;
                            dy = targetH - (LONG)windowResizeLastTargetH;
                            if (dx || dy) {
                                windowResizeLastTargetW = (WORD)targetW;
                                windowResizeLastTargetH = (WORD)targetH;
                                SizeWindow(CurrentMainWindow, dx, dy);
                            }
                        }
                    } break;

                    default:
                        break;
                }
            }

            FS3ELoginView_HandleInput(&app->loginView);
            FS3ETootView_HandleInput(&app->tootView);
            FS3EThemeView_HandleInput(&app->themeView);

            /* Drain the BOOPSI notification queue (OM_NOTIFY via ICA_TARGET). */
            if (DelayQueue && BoopsiDelay_HasMessages(DelayQueue)) {
                struct TagItem *msg;
                refreshFlags = 0;

                while ((msg = BoopsiDelay_NextMessage(DelayQueue)) != NULL) {
                    struct TagItem *ptag;
                    ULONG sender_ID = 0;

                    ptag = FindTagItem(GA_ID, msg);
                    if (ptag) sender_ID = ptag->ti_Data;

                    switch (sender_ID) {

                        /* ---- Title bar ---- */
                        case GID_TITLEBAR_CLOSE:
                            ok = FALSE;
                            break;

                        case GID_TITLEBAR_ICONIFY:
                        printf("GID_TITLEBAR_ICONIFY\n");
                            FS3EMain_Close(&app->mainwindow, app->window_obj, TRUE);
                            break;

                        case GID_TITLEBAR_ALTPOS:
                            if (CurrentMainWindow) {
                                LONG prevL = app->altWinLeft,  prevT = app->altWinTop;
                                LONG prevW = app->altWinWidth, prevH = app->altWinHeight;
                                /* Save current geometry as the new alternate */
                                app->altWinLeft   = CurrentMainWindow->LeftEdge;
                                app->altWinTop    = CurrentMainWindow->TopEdge;
                                app->altWinWidth  = CurrentMainWindow->Width;
                                app->altWinHeight = CurrentMainWindow->Height;
                                /* Move to previous alternate if valid */
                                if (prevW > 0 && prevH > 0)
                                    ChangeWindowBox(CurrentMainWindow,
                                                    prevL, prevT, prevW, prevH);
                            }
                            break;

                        case GID_TITLEBAR_DEPTH:
                            if (CurrentMainWindow && CurrentMainScreen) {
                                /* Walk layers front→back; first layer whose
                                 * Window is non-NULL and not a backdrop is the
                                 * true frontmost user window. */
                                BOOL isFront = FALSE;
                                struct Layer_Info *li = &CurrentMainScreen->LayerInfo;
                                struct Layer *lay;
                                LockLayerInfo(li);
                                lay = li->top_layer;
                                while (lay) {
                                    struct Window *w = (struct Window *)lay->Window;
                                    if (w && !(w->Flags & WFLG_BACKDROP)) {
                                        isFront = (w == CurrentMainWindow);
                                        break;
                                    }
                                    lay = lay->back;
                                }
                                UnlockLayerInfo(li);
                                if (isFront)
                                    WindowToBack(CurrentMainWindow);
                                else
                                    WindowToFront(CurrentMainWindow);
                            }
                            break;

                        /* ---- Navigation bar ---- */
                        case GID_NAV_HOME:
                        case GID_NAV_NOTIFICATIONS:
                        case GID_NAV_LOCAL:
                        case GID_NAV_FEDERATED:
                        case GID_NAV_SEARCH:
                        case GID_NAV_SETTINGS:
                            /* TODO: switch timeline / view */
                            break;

                        case GID_NAV_NEWTOOT:
                            FS3ETootView_Open(&app->tootView);
                            break;

                        case GID_NAV_ACCOUNTS:
                            FS3ELoginView_Open(&app->loginView);
                            break;

                        /* ---- Login sub-window ---- */
                        case GID_LOGIN_LOGIN_BUTTON:
                        {
                            const char *server = FS3ELoginView_GetUTF8Server(&app->loginView);
                            const char *user   = FS3ELoginView_GetUTF8User(&app->loginView);
                            const char *code   = FS3ELoginView_GetUTF8Code(&app->loginView);

                            bdbprintf("FriendSh3ep: login requested "
                                      "(server=%s user=%s code=%s)\n",
                                      server ? server : "",
                                      user   ? user   : "",
                                      code   ? code   : "");
                            /* TODO: FS3ENETQ_LOGIN_START / LOGIN_FINISH */
                            break;
                        }

                        /* ---- New toot sub-window ---- */
                        case GID_TOOT_SEND_BUTTON:
                        {
                            const char *subject = FS3ETootView_GetUTF8Subject(&app->tootView);
                            const char *body    = FS3ETootView_GetUTF8Body(&app->tootView);
                            LONG visibility     = FS3ETootView_GetVisibility(&app->tootView);

                            bdbprintf("FriendSh3ep: toot requested "
                                      "(visibility=%ld subject=%s body=%s)\n",
                                      (long)visibility,
                                      subject ? subject : "",
                                      body    ? body    : "");
                            /* TODO: FS3ENETQ_POST_STATUS */
                            break;
                        }

                        case GID_TOOT_BODY_EDITOR:
                            refreshFlags |= reflags_bodyEditor;
                            break;

                        case GID_TOOT_SUBJECT_EDITOR:
                            refreshFlags |= reflags_subjectEditor;
                            break;

                        case GID_TTIMELINE:
                            refreshFlags |= reflags_tootTimeLine;
                            break;

                        default:
                            break;
                    }
                }

                if ((refreshFlags & reflags_subjectEditor) && app->tootView.window)
                    RefreshGList((struct Gadget *)app->tootView.subjectEditor,
                                 app->tootView.window, NULL, 1);

                if ((refreshFlags & reflags_bodyEditor) && app->tootView.window)
                    RefreshGList((struct Gadget *)app->tootView.bodyEditor,
                                 app->tootView.window, NULL, 1);

                if ((refreshFlags & reflags_tootTimeLine) && CurrentMainWindow && app->tootTimeline )
                    RefreshGList((struct Gadget *)app->tootTimeline,
                                 CurrentMainWindow, NULL, 1);

            }
        }
    }

    return 0;
}

/* - - - - - - - - - - - - - - - - - CLEANUP - - - - - - - - - - - - - - - - */

void exitclose(void)
{
 printf("exitclose\n");
    if (app)
    {

        FS3ELoginView_Dispose(&app->loginView);
        FS3ETootView_Dispose(&app->tootView);
        FS3EThemeView_Dispose(&app->themeView);
//  printf("exitclose2\n");
// wait2sec();
        if (app->window_obj)
        {
            FS3EMenu_Close(&app->menu, CurrentMainWindow);
            FS3ESettings_Save(&app->settings);
            FS3EMain_Close(&app->mainwindow, app->window_obj, 0);
            /* Cascades: mainlayout → titleBarLayout/navBarLayout/placeholder
             * → all UniButtonP9 children. */
            DisposeObject(app->window_obj);
        }
//  printf("exitclose3\n");
// wait2sec();
        /* Free private classes AFTER all objects using them are disposed. */
        TootTimeline_Exit();
        NavBarLayout_Exit();
        TitleBarLayout_Exit();
        UniButtonP9_Exit();
//  printf("exitclose4\n");
// wait2sec();
        /* Release shared DCs after all gadgets using them are disposed. */
        if (app->buttonDC) { URPDC_Release(app->buttonDC); app->buttonDC = NULL; }

//  printf("exitclose5\n");
// wait2sec();
        FS3EStyle_ReleaseDrawContexts(&app->style);
//  printf("exitclose6\n");
// wait2sec();
        if (BevelBase) { CloseLibrary(BevelBase); BevelBase = NULL; }

        if (app->netRequestPort)
        {
            struct MsgPort *netReplyPort = CreateMsgPort();
            if (netReplyPort)
            {
                FS3ENet_Stop(app->netRequestPort, netReplyPort);
                DeleteMsgPort(netReplyPort);
            }
        }
//  printf("exitclose7\n");
// wait2sec();
        FS3EMsg_Close();

        if (app->app_port)
            DeleteMsgPort(app->app_port);

        FS3ESettings_Close(&app->settings);
        FreeVec(app);
        app = NULL;
    }

    FS3ELocale_Close();
    if (LocaleBase) {
        CloseLibrary((struct Library *)LocaleBase);
        LocaleBase = NULL;
    }

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
