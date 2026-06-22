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
#include <proto/intuition.h>
#include <proto/utility.h>
#include <proto/dos.h>

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
#include "fs3elocale.h"

#include "UniButtonP9/unibuttonp9.h"
#include "TitleBarLayout/fs3etitlebar.h"
#include "NavBarLayout/fs3enavbar.h"
#include "TootTimeline/fs3etoottimeline.h"

#include "network_fs3e/fs3enet.h"

const char *pVersion = "$VER: FriendSh3ep " FRIENDSH3EP_VERSION;

struct Task *myTask = NULL;

/* Library bases */
struct GfxBase       *GfxBase       = NULL;
struct IntuitionBase *IntuitionBase = NULL;
struct Library       *UtilityBase   = NULL;

/* BOOPSI class libraries */
struct Library *WindowBase         = NULL;
struct Library *LayoutBase         = NULL;
struct Library *ButtonBase         = NULL;
struct Library *LabelBase          = NULL;
struct Library *ChooserBase        = NULL;
struct Library *UniTextEditorBase  = NULL;
struct Library *UniButtonBase      = NULL;

/* utf8rastport.library – required by UniButtonP9 (private UniButton class) */
struct Library *URPBase  = NULL;
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
    {"window.class",                42, &WindowBase},
    {"gadgets/layout.gadget",       42, &LayoutBase},
    {"gadgets/button.gadget",       42, &ButtonBase},
    {"images/label.image",          42, &LabelBase},
    {"gadgets/chooser.gadget",      44, &ChooserBase},
    {"gadgets/unitexteditor.gadget", 4, &UniTextEditorBase},
    {"gadgets/unibutton.gadget",     4, &UniButtonBase},
    {"utf8rastport.library",         4, &URPBase},
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
    URPDC_AddFont(app->buttonDC, "LiberationSans-Regular.ttf", (int)pointSize, 0);
    URPDC_AddFont(app->buttonDC, "OpenMoji-black-glyf.ttf",    (int)pointSize, 0);

    /* Notify existing buttons: UBTP9_PointSize triggers cache invalidation.
     * Gadget pointers are NULL before buttons are created, so this is safe. */
    {
        int i;
#define RESIZE_BTN(o) if (o) SetAttrs((Object *)(o), UBTP9_PointSize, pointSize, TAG_DONE)
        RESIZE_BTN(app->titlebar_closeBtn);
        RESIZE_BTN(app->titlebar_iconifyBtn);
        RESIZE_BTN(app->titlebar_altposBtn);
        RESIZE_BTN(app->titlebar_depthBtn);
        RESIZE_BTN(app->titlebar_userIcon);
        RESIZE_BTN(app->titlebar_postsLabel);
        RESIZE_BTN(app->titlebar_newPostsLabel);
        for (i = 0; i < 8; i++) RESIZE_BTN(app->nav_btns[i]);
#undef RESIZE_BTN
    }
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
        UBTP9_PointSize,        12,
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
        UBTP9_PointSize,        12,
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

    myTask = FindTask(NULL);

    LocaleBase = (struct LocaleBase *)OpenLibrary("locale.library", 38);
    FS3ELocale_Init("FriendSh3ep.catalog", 0);

    app = (struct App *)AllocVec(sizeof(struct App), MEMF_CLEAR);
    if (!app) cleanexit("Can't allocate app");

    if (!FS3EMsg_Init()) cleanexit("Can't create BOOPSI message target");

    /* --- Private BOOPSI classes ---------------------------------------- */
    if (!UniButtonP9_Init())    cleanexit("Can't init UniButtonP9 class");
    if (!TitleBarLayout_Init()) cleanexit("Can't init TitleBarLayout class");
    if (!NavBarLayout_Init())   cleanexit("Can't init NavBarLayout class");
    if (!TootTimeline_Init())   cleanexit("Can't init TootTimeline class");

    /* --- Shared button draw context (utf8rastport, fonts, emoji) -------- */
    app->buttonDC = URPDC_Create(NULL);
    if (!app->buttonDC) cleanexit("Can't create button draw context");
    URPDC_SetPreferenceFlags(app->buttonDC,
        URP_PREF_ANTIALIAS | URP_PREF_CLUTMODE_NOMASK | URP_PREF_HIGHFILTERING);
    FS3EApp_SetButtonFontSize(12); /* load initial fonts at point size 12 */

    /* --- Network process ------------------------------------------------ */
    app->netRequestPort = FS3ENet_Start();
    if (!app->netRequestPort)
        printf("FriendSh3ep: network process failed - continuing without\n");

    /* --- Classic BOOPSI sub-windows ------------------------------------- */
    if (!FS3ELoginView_Create(&app->loginView, 14))
        cleanexit("Can't create login view");

    if (!FS3ETootView_Create(&app->tootView, 14))
        cleanexit("Can't create toot view");

    /* ================================================================== */
    /* Part A: title bar children (7 gadgets)                              */
    /* ================================================================== */

    app->titlebar_closeBtn   = makeBtn(GID_TITLEBAR_CLOSE,   "X",  dpiH);
    app->titlebar_iconifyBtn = makeBtn(GID_TITLEBAR_ICONIFY, "-",  dpiH);
    app->titlebar_altposBtn  = makeBtn(GID_TITLEBAR_ALTPOS,  "=",  dpiH);
    app->titlebar_depthBtn   = makeBtn(GID_TITLEBAR_DEPTH,   "^",  dpiH);
    app->titlebar_userIcon   = makeLabel("[U]",   dpiH);
    app->titlebar_postsLabel = makeLabel("Posts:0", dpiH);
    app->titlebar_newPostsLabel = makeLabel("New:0", dpiH);

    if (!app->titlebar_closeBtn   || !app->titlebar_iconifyBtn ||
        !app->titlebar_altposBtn  || !app->titlebar_depthBtn   ||
        !app->titlebar_userIcon   || !app->titlebar_postsLabel ||
        !app->titlebar_newPostsLabel)
        cleanexit("Can't create title bar gadgets");

    app->titleBarLayout = (Object *)NewObject(TitleBarLayoutClass, NULL,
        LAYOUT_BevelStyle, BVS_NONE,
        LAYOUT_SpaceOuter, FALSE,
        LAYOUT_SpaceInner, FALSE,
        TBLAYOUT_DpiHeight, (ULONG)dpiH,
        /* children in required order (see fs3etitlebar.h) */
        LAYOUT_AddChild, (ULONG)app->titlebar_closeBtn,
        LAYOUT_AddChild, (ULONG)app->titlebar_iconifyBtn,
        LAYOUT_AddChild, (ULONG)app->titlebar_altposBtn,
        LAYOUT_AddChild, (ULONG)app->titlebar_depthBtn,
        LAYOUT_AddChild, (ULONG)app->titlebar_userIcon,
        LAYOUT_AddChild, (ULONG)app->titlebar_postsLabel,
        LAYOUT_AddChild, (ULONG)app->titlebar_newPostsLabel,
        TAG_END);
    if (!app->titleBarLayout) cleanexit("Can't create title bar layout");

    /* ================================================================== */
    /* Part B: navigation bar children (8 buttons)                         */
    /* ================================================================== */

    app->nav_btns[0] = makeBtn(GID_NAV_HOME,          "Home",      dpiH);
    app->nav_btns[1] = makeBtn(GID_NAV_NOTIFICATIONS, "Notif.",    dpiH);
    app->nav_btns[2] = makeBtn(GID_NAV_LOCAL,         "Local",     dpiH);
    app->nav_btns[3] = makeBtn(GID_NAV_FEDERATED,     "Fed.",      dpiH);
    app->nav_btns[4] = makeBtn(GID_NAV_NEWTOOT,       "Toot+",     dpiH);
    app->nav_btns[5] = makeBtn(GID_NAV_SEARCH,        "Search",    dpiH);
    app->nav_btns[6] = makeBtn(GID_NAV_SETTINGS,      "Settings",  dpiH);
    app->nav_btns[7] = makeBtn(GID_NAV_ACCOUNTS,      "Accounts",  dpiH);

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

    app->tootTimeline = (Object *)NewObject(TootTimelineClass, NULL,
        TTIMELINE_DpiHeight, (ULONG)dpiH,
        TAG_END);
    if (!app->tootTimeline) cleanexit("Can't create toot timeline");

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
        WA_IDCMP,   IDCMP_GADGETUP | IDCMP_NEWSIZE | IDCMP_RAWKEY,
        WA_Flags,
            WFLG_BORDERLESS |
            WFLG_DRAGBAR    |
            WFLG_ACTIVATE   |
            WFLG_SMART_REFRESH,
        WA_Title,            NULL,
        WINDOW_ParentGroup,  (ULONG)app->mainlayout,
        WINDOW_IconTitle,    (ULONG)"FriendSh3ep",
        WINDOW_AppPort,      (ULONG)app->app_port,
        TAG_END);
    if (!app->window_obj) cleanexit("Can't create window");

    FS3EMain_Show(&app->mainwindow, app->window_obj);
    if (!CurrentMainWindow) cleanexit("Can't open window");

    /* - - - Input Event Loop - - - */
    {
        ULONG winsignal;
        BOOL  ok = TRUE;
        ULONG refreshFlags = 0;

#define reflags_subjectEditor 1
#define reflags_bodyEditor    2

        GetAttr(WINDOW_SigMask, app->window_obj, &winsignal);

        while (ok)
        {
            ULONG result, waitedSignals, currentSignals;
            ULONG loginSig, tootSig;

            flushbdbprint();

            loginSig = FS3ELoginView_GetSignalMask(&app->loginView);
            tootSig  = FS3ETootView_GetSignalMask(&app->tootView);

            waitedSignals = winsignal | loginSig | tootSig |
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
                        BoopsiDelay_BeginMessage(DelayQueue, senderId);
                        BoopsiDelay_AddTag(DelayQueue, WMHI_GADGETUP, 1);
                        BoopsiDelay_EndMessage(DelayQueue);
                        break;
                    }

                    case WMHI_ICONIFY:
                        FS3EMain_Close(&app->mainwindow, app->window_obj, TRUE);
                        break;

                    case WMHI_UNICONIFY:
                        FS3EMain_Show(&app->mainwindow, app->window_obj);
                        if (!CurrentMainWindow) cleanexit("can't re-open window");
                        break;

                    case WMHI_RAWKEY:
                    {
                        ULONG key = result & 0x07f;
                        if (key == 0x45) ok = FALSE; /* Escape */
                        break;
                    }

                    default:
                        break;
                }
            }

            FS3ELoginView_HandleInput(&app->loginView);
            FS3ETootView_HandleInput(&app->tootView);

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
                            FS3EMain_Close(&app->mainwindow, app->window_obj, TRUE);
                            break;

                        case GID_TITLEBAR_ALTPOS:
                            /* TODO: alternative window position / sizing */
                            break;

                        case GID_TITLEBAR_DEPTH:
                            /* TODO: bring window to front / back */
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
            }
        }
    }

    return 0;
}

/* - - - - - - - - - - - - - - - - - CLEANUP - - - - - - - - - - - - - - - - */

void exitclose(void)
{
    if (app)
    {
        FS3ELoginView_Dispose(&app->loginView);
        FS3ETootView_Dispose(&app->tootView);

        if (app->window_obj)
        {
            FS3EMain_Close(&app->mainwindow, app->window_obj, 0);
            /* Cascades: mainlayout → titleBarLayout/navBarLayout/placeholder
             * → all UniButtonP9 children. */
            DisposeObject(app->window_obj);
        }

        /* Free private classes AFTER all objects using them are disposed. */
        TootTimeline_Exit();
        NavBarLayout_Exit();
        TitleBarLayout_Exit();
        UniButtonP9_Exit();

        /* Release shared DC after all gadgets using it are disposed. */
        if (app->buttonDC) { URPDC_Release(app->buttonDC); app->buttonDC = NULL; }

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

        FS3EMsg_Close();

        if (app->app_port)
            DeleteMsgPort(app->app_port);

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
