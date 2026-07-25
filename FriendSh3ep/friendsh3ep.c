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
 *     Part C: searchBarLayout (SearchBarLayoutClass) — fills rest
 *               searchWordEditor (one-line UniTextEditor, VIEWMODE_Search only)
 *               tootTimeline     (TootTimelineClass)
 *
 * dpiHeight (default 14 px) acts as the "DPI factor": every row in the
 * mobile-style UI is exactly one dpiHeight pixel tall.
 *
 * Scope of this file (watch out for this growing back into a God Object --
 * it used to be one, split apart into fs3erequests.c/fs3eaccounts.c/
 * fs3enetworkhelper.c/etc.): creating the main window's BOOPSI objects,
 * initializing every other subsystem, running the main Wait()/event loop,
 * and closing everything down on exit. Anything that isn't one of those
 * four -- a new network request/reply case, account persistence, a self-
 * contained UI action, ... -- belongs in its own file, not here. A few
 * pre-existing out-of-scope functions are tolerated; don't add more.
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

#include <proto/bitmap.h>
#include <images/bitmap.h>

#include <proto/string.h>
#include <gadgets/string.h>

#include <proto/texteditor.h>
#include <gadgets/texteditor.h>

#include <proto/label.h>
#include <images/label.h>

#include <proto/unitexteditor.h>
#include <gadgets/unitexteditor.h>

#include <proto/unibutton.h>
#include <gadgets/unibutton.h>

#include <libraries/utf8rastport.h>
#include <proto/utf8rastport.h>

#include <proto/chooser.h>
#include <gadgets/chooser.h>

#include <proto/listbrowser.h>
#include <gadgets/listbrowser.h>

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
#include "fs3eemojibox.h"
#include "fs3elocale.h"
#include "fs3emenu.h"
#include "fs3eaction.h"
#include "fs3etimer.h"
#include "fs3esettings.h"
#include "fs3emachineid.h"
#include "fs3erequests.h"
#include "fs3eaccounts.h"
#include "fs3enetworkhelper.h"

#include "UniButtonP9/unibuttonp9.h"
#include "UniButtonBGBM/unibuttonbgbm.h"
#include "TitleBarLayout/fs3etitlebar.h"
#include "NavBarLayout/fs3enavbar.h"
#include "SearchBarLayout/fs3esearchbar.h"
#include "TootTimeline/fs3etoottimeline.h"

#include "network_fs3e/fs3enet.h"
#include "fs3ethumb.h"
#include "fs3eaudio.h"

const char *pVersion = "$VER: FriendSh3ep " FRIENDSH3EP_VERSION;

/* FS3E_CACHE_SUBDIR_USERICONS/THUMBNAILS now live in network_fs3e/fs3enet.h
 * (already #included above) -- shared with fs3emediaview.c. */

struct Task *myTask = NULL;

/* Single-instance IPC: named public port checked/created in main() before
 * any resources (esp. the disk cache -- see fs3enet_cache.h) are touched.
 * See FS3E_CheckSingleInstance(). */
#define FS3E_SIPC_PORTNAME "FRIENDSH3EP_SIPC_PORT"
static struct MsgPort *SIPCPort = NULL;

void wait2sec() {
int i;
 //re   for(i=0;i<75;i++) WaitTOF();
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
struct Library *StringBase         = NULL;
struct Library *TextFieldBase      = NULL;
struct Library *LabelBase          = NULL;
struct Library *CheckboxBase       = NULL;
struct Library *ChooserBase        = NULL;
struct Library *GetFileBase        = NULL;
struct Library *IntegerBase        = NULL;
struct Library *UniTextEditorBase  = NULL;
struct Library *UniButtonBase      = NULL;
struct Library *ListBrowserBase    = NULL;  /* proto/listbrowser.h's inline stubs need this exact name */

/* utf8rastport.library – required by UniButtonP9 (private UniButton class) */
struct Library *URPBase  = NULL;
/* datatypes.library v44 – used by bmimage.c for picture.datatype image loading */
struct Library *DataTypesBase = NULL;
/* images/bevel.image – optional, used by UniButton bevel frames */
struct Library *BevelBase = NULL;
/* images/bitmap.image – optional, used by fs3estyle.c for themed title bar
 * button images (see FS3EStyle_LoadThemeImages) */
struct Library *BitMapBase = NULL;
/* images/penmap.image – optional, used by fs3etbdefaultbtn.c for the 4
 * built-in title bar button glyphs (see FS3EStyle_CreateDefaultButtonImages) */
struct Library *PenMapBase = NULL;

/* this one is optional and can be NULL */
struct Library *CyberGfxBase = NULL;

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
    {"gadgets/string.gadget",       42, &StringBase},
    {"gadgets/texteditor.gadget",   15, &TextFieldBase}, /* os3.9 is 15 */
    {"images/label.image",          42, &LabelBase},
    {"images/penmap.image",          42, &PenMapBase},
    {"gadgets/checkbox.gadget",      42, &CheckboxBase},
    {"gadgets/chooser.gadget",       44, &ChooserBase},
    {"gadgets/getfile.gadget",       42, &GetFileBase},
    {"gadgets/integer.gadget",       44, &IntegerBase},
    {"gadgets/unitexteditor.gadget",  4, &UniTextEditorBase},
    {"gadgets/unibutton.gadget",     4, &UniButtonBase},
    {"gadgets/listbrowser.gadget",  40, &ListBrowserBase},
    {"utf8rastport.library",         5, &URPBase},
    {"datatypes.library",           44, &DataTypesBase},

    {NULL, 0, NULL}
};

struct App *app = NULL;

//extern int refreshTitleBarLayout;

/* Default DPI factor: 1 row = 14 pixels. */
#define DEFAULT_DPI_HEIGHT 14

/* Pixels TTIMELINE_ScrollBy moves per wheel-mouse notch (WMHI_RAWKEY keys
 * 0x7A/0x7B, see the main event loop below) -- a plain fixed amount, not
 * tied to row/font height, same "good enough default" spirit as
 * DEFAULT_DPI_HEIGHT above. */
#define FS3E_WHEEL_SCROLL_PIXELS 48

void exitclose(void);

void cleanexit(const char *pmessage)
{
    if (pmessage) printf("%s\n", pmessage);
    exit(0);
}

/* The connected account's avatar (row 2 of the title bar) just became
 * available or changed -- TitleBarLayout_OnRender reads it fresh from
 * AvatarImages_Get() on every render (no cached bitmap/wrapper object of
 * its own to go stale), so all that's needed here is asking for a
 * redraw.
 * Not static: fs3erequests.c's FS3EApp_HandleThumbReply calls this too --
 * see the extern declaration there. */
void FS3EApp_UpdateUserIcon(void)
{
    if (CurrentMainWindow && app->titleBarLayout)
        RefreshGList((struct Gadget *)app->titleBarLayout, CurrentMainWindow, NULL, 1);
}

/* s_searchWaitMsgIdx lives in fs3erequests.c (bumped by FS3EApp_SearchWord);
 * read-only here to pick a wait message below. */
extern ULONG s_searchWaitMsgIdx;

/* Update TTIMELINE_WaitText to reflect current connection / login state.
 * Called whenever the state machine advances so the empty-channel placeholder
 * always shows a meaningful message.
 * Not static: fs3erequests.c's request/reply functions call this too --
 * see the extern declaration there. */
void FS3EApp_CheckConnectionState(void)
{
    const char *text = NULL;
    ULONG bit;

    if (!app->tootTimeline) return;

    /* Login phase takes priority over everything else. */
    switch ((FS3ELoginPhase)app->loginPhase) {
        case FS3ELOGIN_WAITING_START:
            text = "Connecting to server...";
            break;
        case FS3ELOGIN_WAITING_CODE:
            text = "Open the URL in your browser,\n"
                   "then paste the code and click Login.";
            break;
        case FS3ELOGIN_WAITING_FINISH:
            text = "Verifying account...";
            break;
        default:
            break;
    }

    if (!text) {
        if (!app->netRequestPort) {
            text = "No connection.\n"
                   "Need internet and AmiSSLv5.";
        } else if (!app->accountAccessToken) {
            text = "No account.\n"
                   "Open the Login window to connect.";
        } else {
            bit = (1UL << app->viewMode);
            if (app->timelineErrorMask & bit) {
                switch (app->lastTimelineResult) {
                    case FS3ENETR_NETWORK_ERROR:
                        text = "No internet connection.";  break;
                    case FS3ENETR_HTTP_ERROR:
                        /* This codebase's HTTP layer never exposes a real
                         * status code for GET/POST (see FS3EHttpResponse.
                         * fhr_StatusCode's own comment), so this can't be a
                         * true "was it 403" check -- but a followers/
                         * following fetch failing at all is disproportionately
                         * likely to be exactly that (a hide_collections
                         * account returns a JSON error object, which
                         * FS3EMastodon_GetTimeline already treats as failure
                         * the same as any other non-array response) rather
                         * than a real outage, so hedge the wording instead
                         * of asserting it as fact. */
                        if (app->viewMode == VIEWMODE_Search &&
                            (app->searchMode == FS3ESEARCH_FOLLOWERS ||
                             app->searchMode == FS3ESEARCH_FOLLOWING))
                            text = "This list may not be available (server error, or hidden by privacy settings).";
                        else
                            text = "Server error.";
                        break;
                    case FS3ENETR_AUTH_ERROR:
                        text = "Authentication failed.";   break;
                    default:
                        text = "Connection error.";        break;
                }
            } else if (app->timelineFetchedMask & bit) {
                /* Word/hashtag search in flight (see FS3EApp_SearchWord)
                 * gets one of a few fediverse-flavored wait messages
                 * instead of the generic "Updating..." every other
                 * channel shows here -- purely cosmetic, no meaning
                 * attached to which one shows (see s_searchWaitMsgIdx). */
                if (app->viewMode == VIEWMODE_Search && app->searchMode == FS3ESEARCH_WORD) {
                    static const ULONG searchWaitMsgs[4] = {
                        MSG_SEARCHV_WAIT1, MSG_SEARCHV_WAIT2,
                        MSG_SEARCHV_WAIT3, MSG_SEARCHV_WAIT4
                    };
                    text = LOC(searchWaitMsgs[s_searchWaitMsgIdx % 4]);
                } else {
                    text = "Updating...";
                }
            } else if (app->channelEmptyMask & bit) {
                /* See channelEmptyMask's doc comment -- a completed fetch
                 * that genuinely returned nothing (typically a word/
                 * account search with no matches) previously fell through
                 * to the generic "Account connected." text below, which
                 * read as if the search hadn't actually run. */
                text = "Nothing found.";
            } else {
                text = "Account connected.";
            }
        }
    }

    SetGdAttrs(app->tootTimeline, TTIMELINE_WaitText, (ULONG)text, TAG_END);

    /* User menu (see fs3eaction.h's FS3EACTION_USER_* group) is only
     * meaningful while the Search view is showing SOMEONE ELSE's profile
     * -- greyed out otherwise, including while viewing your own profile
     * (nothing to Follow/Block/etc. about yourself). Re-derived here,
     * the one place this function already runs after every state change
     * that could affect the answer (view switches via fs3e_setViewMode,
     * FS3EApp_OpenProfile, and -- critically -- the FS3ENETQ_ACCOUNT_LOOKUP
     * reply that's the only place searchProfileAccountId/isSelf actually
     * become known), rather than scattering this check across every
     * individual call site. */
    if (CurrentMainWindow) {
        BOOL userMenuEnabled =
            (app->viewMode == VIEWMODE_Search) &&
            (app->searchMode == FS3ESEARCH_USER_PROFILE) &&
            app->searchProfileAccountId && app->searchProfileAccountId[0] &&
            app->accountId && app->accountId[0] &&
            (strcmp(app->searchProfileAccountId, app->accountId) != 0);
        FS3EMenu_SetUserMenuEnabled(&app->menu, CurrentMainWindow, userMenuEnabled);
    }
}


/* Visibility index (from FS3ETootView) → Mastodon API string. */
static const char *VisibilityString(LONG idx)
{
    static const char *const s[] = { "public", "unlisted", "private", "direct" };
    if (idx < 0 || idx > 3) return "public";
    return s[(ULONG)idx];
}

/* Quote-policy index (from FS3ETootView) → Mastodon quote_approval_policy
 * API string. */
static const char *QuotePolicyString(LONG idx)
{
    static const char *const s[] = { "public", "followers", "nobody" };
    if (idx < 0 || idx > 2) return "public";
    return s[(ULONG)idx];
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

    /* Notify existing buttons, one macro per real class so each object is
     * only ever sent tags its own OM_SET understands -- nav_btns[] are
     * UniButtonBGBM (UBGBM_PointSize triggers its cache invalidation);
     * titlebar_settingsBtn/accountBtn/newtootBtn are UniButtonP9
     * (UBTP9_PointSize just retriggers live metrics -- no cache there).
     * Gadget pointers are NULL before buttons are created, so this is safe. */
    {
        int i;
#define RESIZE_BGBM_BTN(o) if (o) SetAttrs((Object *)(o), \
            UBGBM_PointSize, pointSize, TAG_DONE)
#define RESIZE_P9_BTN(o) if (o) SetAttrs((Object *)(o), \
            UBTP9_PointSize, pointSize, TAG_DONE)
        for (i = 0; i < 8; i++) RESIZE_BGBM_BTN(app->nav_btns[i]);
    //olde    RESIZE_P9_BTN(app->titlebar_settingsBtn);
        RESIZE_P9_BTN(app->titlebar_accountBtn);
        RESIZE_P9_BTN(app->titlebar_newtootBtn);
#undef RESIZE_BGBM_BTN
#undef RESIZE_P9_BTN

    }
    /* we must tell to all gadgets using those URPDrawContext that the
     * font we share with them have their size changed so they update their internal layout.
      all these gadgets share dcNormal
     */
    if(app->tootView.contextMessage) SetAttrs(app->tootView.contextMessage,UBT_PointSize,pointSize,TAG_END);
    if(app->tootView.bodyEditor) SetAttrs(app->tootView.bodyEditor,UTED_PointSize,pointSize,TAG_END);
    if(app->searchWordEditor) SetAttrs(app->searchWordEditor,UTED_PointSize,pointSize,TAG_END);
    if(app->tootView.emojiBtn) SetAttrs(app->tootView.emojiBtn,UBT_PointSize,pointSize,TAG_END);
    if(app->loginView.urlInstructLabel) SetAttrs(app->loginView.urlInstructLabel,UBT_PointSize,pointSize,TAG_END);
    if(app->tootView.window) DoMethod(app->tootView.windowObj, WM_RETHINK);
    if(app->loginView.window) DoMethod(app->loginView.windowObj, WM_RETHINK);
}
/* avoid flooding reloading font and remaking all layout when changing font fast */
static int delayApplyFontSettings = FALSE;
/* Re-apply font settings from app->settings to all draw contexts.
 * Called by fs3ethemeview.c / fs3eaction.c after font or rendering options change.
  THIS is ithe public one
 */
void FS3EApp_ApplyFontSettings(void)
{
    if(!delayApplyFontSettings)
    {
        delayApplyFontSettings = TRUE;
        if (myTask) Signal(myTask, SIGBREAKF_CTRL_F);
    }
}
/* this is the private delayed version .
This has to be followed by a WM_RETHINK to recompute whole layout against font size*
- expect when used just before first window open.
 *
 * Not static: GenericOpenWindow() (fs3eboopsimainwindow.c) calls this a
 * second time right after FS3EStyle_ApplyColors() binds a real screen to
 * app->style's draw contexts (URPDC_SetDrawScreen). FS3EStyle_SetFontSize
 * -> compute_layout() below reads line-height metrics (URPDC_
 * GetFontLineMetrics) from those same draw contexts, and until a screen is
 * bound that reads back wrong -- this function's first call, from main()
 * before the window/screen exist at all, computes app->style.avatarSize
 * (and TootTimeline's own cached line-height fields, re-cached from the
 * TTIMELINE_Style SetAttrs below) from an unscreened context and gets it
 * wrong (avatar thumbnails render far too big). Nothing recomputes it
 * again until the user changes a font setting by hand -- hence the bug
 * only ever showing up before that first manual "resize". Re-running this
 * once a screen exists fixes it before the very first paint. */
void FS3EApp_ApplyFontSettings_Delayed(void)
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

    /* SetAttrs (not SetGadgetAttrs) so this reaches the gadget even before
     * the window is open (CurrentMainWindow is still NULL at startup, just
     * before FS3EMain_Show()) -- OM_SET's redraw is already gated on
     * msg->ops_GInfo, so no window is required to re-cache line metrics. */
    if (app->tootTimeline)
        SetAttrs(app->tootTimeline,
                 TTIMELINE_Style, (ULONG)&app->style,
                 TAG_DONE);

    /* Avatar bitmaps are plain Fast-RAM RGB pixel arrays now (see
     * rgbimage.h), rescaled at draw time -- a DPI/font-size change needs no
     * avatar reload or rescale at all, just a titlebar user-icon rebuild. */
    if (app->avatarImages)
        FS3EApp_UpdateUserIcon();

    delayApplyFontSettings = FALSE;
}

/* avoid tearing down/reloading every theme image and doing a synchronous
 * WM_RETHINK on every single chooser click -- two fast clicks on the theme
 * chooser (GID_THEMEV_THEME_CHOOSER, fs3ethemeview.c) each called the old
 * unconditional FS3EApp_LoadTheme() body directly and re-entrantly from
 * inside WM_HANDLEINPUT dispatch, which crashed. Same debounce shape as
 * delayApplyFontSettings above: coalesce to the last-requested theme and
 * do the real work once, on the next Wait() wakeup. */
static int delayLoadTheme = FALSE;

/* Public, debounced trigger. Every caller (fs3ethemeview.c's chooser
 * handler, and this file's own startup call below) already writes the
 * requested theme name into app->settings.themeName before calling this,
 * so themeName here is only a reminder of that contract, not a value this
 * function itself needs to store anywhere. */
void FS3EApp_LoadTheme(const char *themeName)
{
    (void)themeName;
    if (!delayLoadTheme)
    {
        delayLoadTheme = TRUE;
        if (myTask) Signal(myTask, SIGBREAKF_CTRL_F);
    }
}

void free_tb_images(FS3EStyle *st);
/* need to be done more early */
void flushThemeImagesFromButtons()
{
    if (app->titlebar_closeBtn)   SetGdAttrs(app->titlebar_closeBtn,   GA_Image, 0UL,BUTTON_BevelStyle,BVS_BUTTON,BUTTON_Transparent,FALSE, TAG_END);
    if (app->titlebar_iconifyBtn) SetGdAttrs(app->titlebar_iconifyBtn, GA_Image, 0UL,BUTTON_BevelStyle,BVS_BUTTON,BUTTON_Transparent,FALSE, TAG_END);
    if (app->titlebar_altposBtn)  SetGdAttrs(app->titlebar_altposBtn,  GA_Image, 0UL,BUTTON_BevelStyle,BVS_BUTTON,BUTTON_Transparent,FALSE, TAG_END);
    if (app->titlebar_depthBtn)   SetGdAttrs(app->titlebar_depthBtn,   GA_Image, 0UL,BUTTON_BevelStyle,BVS_BUTTON,BUTTON_Transparent,FALSE, TAG_END);

    // NO free_tb_images(&app->style);

}
/* Private, does the real work -- see FS3EApp_ApplyFontSettings_Delayed's
 * doc comment just above for why this is split from the public trigger.
 * Not static: none needed yet, but kept a plain function (not inlined
 * into main()) so it mirrors FS3EApp_ApplyFontSettings_Delayed's shape. */
static void FS3EApp_LoadTheme_Delayed(void)
{
    const char *themeName = app ? app->settings.themeName : NULL;

    if (!app) return;

    /* Detach GA_Image on the 4 title bar buttons first thing, before
     * anything below disposes whatever they're currently pointing at --
     * otherwise there's a brief window where a live gadget still
     * references a just-freed bitmap, and a redraw landing in it blits
     * freed memory (visible as a "trashed" button image for a frame when
     * switching away from a theme with button images, e.g. mouton, to
     * "-"). FS3EStyle_SyncTitleBarButtons() below re-attaches a new
     * theme's images if one gets loaded. */
     //test flushThemeImagesFromButtons();


    /* Dispose whatever the previously active theme (or nothing, if none
     * was active) loaded, before deciding what replaces it.
     * FS3EStyle_LoadThemeImages() below would do this itself, but the "no
     * theme" branch skips that call entirely, so it has to happen
     * unconditionally here instead. Gadgets that reference these images
     * (title bar buttons, patch9-skinned buttons) already degrade to a
     * flat colour fill when the image is missing -- see
     * FS3EStyle_SyncTitleBarButtons and Patch9_IsLoaded -- so closing them
     * here with nothing to replace them is a fully supported end state,
     * not a transient one. */
    FS3EStyle_UnloadThemeImages(&app->style);

    if (themeName && themeName[0]) {
        char path[300];
        snprintf(path, sizeof(path), "%s/%s", FS3ESTYLE_THEMES_ROOT, themeName);
        FS3EStyle_SetThemePath(&app->style, path);
        FS3EStyle_LoadThemeImages(&app->style, CurrentMainScreen);
    } else {
        /* "-" in the theme chooser, or nothing ever activated -- no
         * theme: no images (already closed above, and deliberately not
         * reloaded), every managed colour (FS3EColorRole roles and both
         * Patch9 skins' bgcolors/textcolors) back to hardcoded defaults.
         * Clearing themePath to NULL, not just leaving it stale, matters:
         * GenericOpenWindow (fs3eboopsimainwindow.c) re-runs on every
         * uniconize/reopen and only reloads theme images when themePath
         * is actually set -- leaving a previous theme's path here would
         * make it come back on the next uniconize regardless of this
         * "no theme" choice. */
        FS3EStyle_SetThemePath(&app->style, NULL);
        FS3EStyle_ResetColors(&app->style);
        FS3EStyle_ResetPatch9Colors(&app->style);
    }

    FS3EStyle_ApplyColors(&app->style, CurrentMainScreen);

    /* Propagates the (re)loaded/reset style onto every widget that caches
     * its own rendering from it -- title bar buttons, patch9-skinned
     * buttons, the toot timeline's colors, nav bar buttons. Same function
     * GenericOpenWindow (fs3eboopsimainwindow.c) uses on every window
     * open/uniconize, so a live theme switch here can't drift out of sync
     * with what a fresh open already does correctly. */
    FS3EMain_SyncStyleToWidgets();

    /* Recompute minimum gadget sizes and relayout the whole main window --
     * same call main()'s own font-size-change handling uses (see
     * delayApplyFontSettings below). Themeview/fs3ethemeview.h's own doc
     * comment: this affects the main window only, not the theme settings
     * window itself. */
    if (app->window_obj) DoMethod(app->window_obj, WM_RETHINK);

    delayLoadTheme = FALSE;
}

/* Create one UniButtonBGBM (flat-colour, cached-by-state), click arrives
 * via WMHI_GADGETUP. dpiH is reserved for future use; font size is
 * controlled by app->buttonDC. */
static Object *makeBtn(ULONG gadID, const char *label, UWORD dpiH, int shiftx, int shifty, int pushbutton)
{
    (void)dpiH;
    return (Object *)NewObject(UniButtonBGBMClass, NULL,
        GA_ID,                  gadID,
        ICA_TARGET,             (ULONG)TargetInstance,
        GA_Text,                (ULONG)label,
        UBGBM_PushButton,       pushbutton,
        UBGBM_URPDrawContext,   (ULONG)app->buttonDC,
        UBGBM_BevelStyle,       BVS_BUTTON, //BVS_NONE,
        UBGBM_BgShiftX,         shiftx,
        UBGBM_BgShiftY,         shifty,
        TAG_END);
}
static Object *makeBtnGadgetUpOnly(ULONG gadID, const char *label, UWORD dpiH, int shiftx, int shifty, int pushbutton)
{
    (void)dpiH;
    return (Object *)NewObject(UNIBUTTON_GetClass(), NULL,
        GA_ID,                  gadID,
      //  ICA_TARGET,             (ULONG)TargetInstance,
        GA_Text,                (ULONG)label,
        UBT_PushButton,       pushbutton,
        UBT_URPDrawContext,   (ULONG)app->buttonDC,
        UBT_BevelStyle,       BVS_BUTTON, //BVS_NONE,
        TAG_END);
}

/* Create a read-only label using UniButtonBGBM. */
static Object *makeLabel(const char *text, UWORD dpiH)
{
    (void)dpiH;
    return (Object *)NewObject(UniButtonBGBMClass, NULL,
        GA_Text,                (ULONG)text,
        GA_ReadOnly,            TRUE,
        UBGBM_URPDrawContext,   (ULONG)app->buttonDC,
        UBGBM_BevelStyle,       BVS_NONE,
        UBGBM_Transparent,      TRUE,
        UBGBM_TopMargin,        0,
        UBGBM_BottomMargin,     0,
        TAG_END);
}
/* this is the unique entry to manage changing viewmode (from button, keys, menu) */
void fs3e_setViewMode(ULONG viewMode)
{
    int i;
    ULONG oldViewMode;
    if(app->viewMode == viewMode)
    {
        /* if we jump to search view, give keyboard focus to search */
        if(viewMode == VIEWMODE_Search && app->searchWordEditor && CurrentMainWindow)
        {
            ActivateGadget(app->searchWordEditor,CurrentMainWindow,NULL);
        }
        return;
    }
    if(viewMode >=VIEWMODE_NumberOf) return;
    /* synchronize buttons states with no drama */
    for(i=0;i<8;i++)
    {
        int btstate=0;
        int wantedstate = (int)(i==viewMode);
        if(!app->nav_btns[i]) continue;
        GetAttr(GA_Selected,app->nav_btns[i],&btstate);
        if(btstate != wantedstate)
            SetGdAttrs(app->nav_btns[i],GA_Selected,wantedstate,TAG_END);

    }

    /* now, it's official */
    oldViewMode = app->viewMode;
    app->viewMode = viewMode;

    /* Show the search word editor only in VIEWMODE_Search, and only touch
     * it (SetGdAttrs + RethinkLayout) when actually entering/leaving that
     * channel -- switching between two non-Search channels must not fire
     * a relayout of this subtree at all. RethinkLayout is called at the
     * searchBarLayout level on purpose: it recomputes only that gadget's
     * own GM_LAYOUT (search editor + tootTimeline), not the whole window
     * (titleBarLayout/navBarLayout are untouched). */
    if (app->searchBarLayout &&
        (oldViewMode == VIEWMODE_Search) != (viewMode == VIEWMODE_Search))
    {
        BOOL wantVisible = (viewMode == VIEWMODE_Search);
        SetGdAttrs(app->searchBarLayout, SBLAYOUT_Visible, (ULONG)wantVisible, TAG_END);
        if (CurrentMainWindow)
            RethinkLayout((struct Gadget *)app->searchBarLayout,
                          CurrentMainWindow, NULL, TRUE);
    }

    /* tell TootTimeline we're to display that channel */
    if (app->tootTimeline)
        SetGdAttrs(app->tootTimeline, TTIMELINE_ViewMode, viewMode, TAG_END);

    /* If logged in and this channel hasn't been fetched yet, start a fetch. */
    FS3EApp_FetchTimeline(viewMode);

    /* Update WaitText for channels that don't trigger a fetch (Search, Notifs, …). */
    FS3EApp_CheckConnectionState();

    /* if we jump to search view, give keyboard focus to search */
    if(viewMode == VIEWMODE_Search && app->searchWordEditor && CurrentMainWindow)
    {
        ActivateGadget(app->searchWordEditor,CurrentMainWindow,NULL);
    }
}

/* Close every classic BOOPSI sub-window (but don't dispose them -- they
 * stay alive for the next open). Called just before the main window is
 * iconified: none of these belong on the Workbench screen once the main
 * window is gone, and CurrentMainScreen may be closed/reopened across an
 * iconify/uniconify cycle. */
static void closeExternalViews(void)
{
    FS3ELoginView_Close(&app->loginView);
    FS3ETootView_Close(&app->tootView);
    FS3EThemeView_Close(&app->themeView);
    FS3ESettingsView_Close(&app->settingsView);
    FS3EEmojiBoxWindow_Close(&app->emojiBoxWindow);
}

/* If another FriendSh3ep is already running, ask it to activate itself and
 * return FALSE (caller must quit immediately, before touching the cache).
 * Otherwise create+register our own public port and return TRUE.
 *
 * Classic exec.library idiom (RKM Exec "Ports" chapter, port1.c/port2.c):
 * FindPort() must be paired with PutMsg() inside the *same* Forbid(), since
 * once Permit() runs the target port's owner could exit and free it out
 * from under us -- see the comment in port2.c's SafePutToPort(). */
static BOOL FS3E_CheckSingleInstance(void)
{
    struct MsgPort *existing;

    Forbid();
    existing = FindPort(FS3E_SIPC_PORTNAME);
    if (existing)
    {
        struct MsgPort *replyPort = CreateMsgPort();
        if (replyPort)
        {
            struct Message *msg =
                AllocMem(sizeof(struct Message), MEMF_PUBLIC | MEMF_CLEAR);
            if (msg)
            {
                msg->mn_Node.ln_Type = NT_MESSAGE;
                msg->mn_Length       = sizeof(struct Message);
                msg->mn_ReplyPort    = replyPort;
                PutMsg(existing, msg);   /* still Forbid()'d -- existing can't vanish yet */
                Permit();
                WaitPort(replyPort);
                GetMsg(replyPort);
                FreeMem(msg, sizeof(struct Message));
            }
            else Permit();
            DeleteMsgPort(replyPort);
        }
        else Permit();
        return FALSE;
    }

    SIPCPort = CreateMsgPort();
    if (SIPCPort)
    {
        SIPCPort->mp_Node.ln_Name = FS3E_SIPC_PORTNAME;
        SIPCPort->mp_Node.ln_Pri  = 0;
        AddPort(SIPCPort);   /* nested inside the outer Forbid() -- Forbid/Permit nest via a counter */
    }
    Permit();
    return TRUE;
}

void StartSearchFromLine()
{
    const char *text = NULL;
    ULONG active = FS3ESEARCHTYPE_WORD;

    if(!app || !app->searchWordEditor) return;
    SetAttrs(app->searchWordEditor, UTED_LineTextToGet, 0, TAG_END);
    GetAttr(UTED_LineUTF8TextBuffer, app->searchWordEditor, (ULONG *)&text);
    if(!text || *text == 0) return;

    if (app->searchWordTypeChooser)
        GetAttr(CHOOSER_Active, app->searchWordTypeChooser, &active);

    if (active == FS3ESEARCHTYPE_PEOPLE) {
        FS3EApp_SearchAccount(text);
    } else {
        /* Hashtag ("#tag", '#' kept as typed/
         * prefilled -- see TTL_HOT_HASHTAG
         * above) and plain word searches act
         * the same: a word search on the
         * server. */
        FS3EApp_SearchWord(text);
    }
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

    /* Only one FriendSh3ep may run at a time (shared, non-concurrency-safe
     * disk cache -- see fs3enet_cache.h). Check/register before opening any
     * library or touching the cache. */
    if (!FS3E_CheckSingleInstance())
        return 0;

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



    /* CyberGfxBase NULL accepted */
    CyberGfxBase = OpenLibrary("cybergraphics.library", 1);


    BevelBase  = OpenLibrary("images/bevel.image",  32); /* optional, no check */
    BitMapBase = OpenLibrary("images/bitmap.image", 44); /* optional, no check */

    LocaleBase = (struct LocaleBase *)OpenLibrary("locale.library", 38);
    FS3ELocale_Init("FriendSh3ep.catalog", 0);
    FS3EAction_Init();

    app = (struct App *)AllocVec(sizeof(struct App), MEMF_CLEAR);
    if (!app) cleanexit("Can't allocate app");

    /* app->accountMaxChars starts at 0 (MEMF_CLEAR) == "not confirmed by
     * the server yet" -- see its comment in friendsh3ep.h. */

    /* -1 == "no scroll restore pending" -- MEMF_CLEAR left it 0, which is
     * a valid scroll position, so it must be set explicitly here. */
    app->searchPendingScrollY = -1;

    FS3ESettings_Load(&app->settings);

    if (!FS3EMsg_Init()) cleanexit("Can't create BOOPSI message target");

    /* Must be called from the task that will run the main Wait() loop
     * below (FS3ETimer_Init() captures FindTask(NULL) as the task its
     * VBlank interrupt Signal()s) -- see fs3etimer.h. */
    if (!FS3ETimer_Init()) cleanexit("Can't install VBlank timer service");

    /* --- Private BOOPSI classes ---------------------------------------- */
    if (!UniButtonP9_Init())    cleanexit("Can't init UniButtonP9 class");
    if (!UniButtonBGBM_Init())  cleanexit("Can't init UniButtonBGBM class");
    if (!TitleBarLayout_Init()) cleanexit("Can't init TitleBarLayout class");
    if (!NavBarLayout_Init())   cleanexit("Can't init NavBarLayout class");
    if (!SearchBarLayout_Init()) cleanexit("Can't init SearchBarLayout class");
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

// printf("FS3EStyle_InitDefaults done\n");

    app->avatarImages = AvatarImages_Create();


// printf("FS3ENet_Start\n");
    /* --- Network process ------------------------------------------------ */
    app->netReplyPort = CreateMsgPort();
    if (!app->netReplyPort) cleanexit("Can't create network reply port");

    app->netRequestPort = FS3ENet_Start(app->settings.cachePath,
                                         (ULONG)app->settings.maxCacheSizeMB);

// printf("FS3EThumb_Start\n");
    /* --- Thumbnail process ------------------------------------------------ */
    app->thumbReplyPort = CreateMsgPort();
    if (!app->thumbReplyPort) cleanexit("Can't create thumbnail reply port");

    app->thumbRequestPort = FS3EThumb_Start();

// printf("FS3EAudio_Start\n");
    /* --- Audio (MP3/AHI) process ------------------------------------------ */
    app->audioReplyPort = CreateMsgPort();
    if (!app->audioReplyPort) cleanexit("Can't create audio reply port");

    app->audioRequestPort = FS3EAudio_Start();

// printf("FS3EApp_MachineKey\n");
    /* Debug: print the derived machine key unconditionally, even before any
     * account.dat exists to trigger it lazily via FS3EApp_MachineKey() --
     * so it's visible on every launch for comparing across machines. */
    FS3EApp_MachineKey();


// printf("FS3EApp_LoadAccount\n");
    /* Try to load saved credentials (and the rest of the accounts list --
     * see FS3EApp_LoadAccount); timeline fetch fires later in setViewMode */
    FS3EApp_LoadAccount();
    FS3EApp_BackfillAccountId();



 printf("FS3ELoginView_Create\n");
    /* --- Classic BOOPSI sub-windows ------------------------------------- */
    if (!FS3ELoginView_Create(&app->loginView,  app->style.dcNormal))
        cleanexit("Can't create login view");

    /* Credentials already confirmed (loaded from account.dat above) --
     * leave server/user entries empty rather than pre-filling them, so
     * there's nothing to accidentally resubmit and trigger a fresh
     * re-auth (see GID_LOGIN_LOGIN_BUTTON's "already connected" branch). */
    if (app->accountApiBaseUrl)
        FS3ELoginView_ClearFields(&app->loginView);

 printf("FS3EApp_RefreshLoginAccountsList\n");
    FS3EApp_RefreshLoginAccountsList();
 printf("FS3EApp_RefreshLoginAccountsList done\n");

 printf(" FS3ETootView_Create\n");
    if (!FS3ETootView_Create(&app->tootView, app->style.dcNormal))
        cleanexit("Can't create toot view");

    if (!FS3EEmojiBoxWindow_Create(&app->emojiBoxWindow, app->style.dcNormal))
        cleanexit("Can't create emoji box");

    if (!FS3EThemeView_Create(&app->themeView, LOC(MSG_THEMEV_TITLE)))
        cleanexit("Can't create theme view");

    if (!FS3ESettingsView_Create(&app->settingsView, LOC(MSG_SETTINGSV_TITLE)))
        cleanexit("Can't create settings view");

    FS3EMediaView_Init(&app->mediaView);

 printf(" ext window created\n");
    /* ================================================================== */
    /* Part A: title bar children (7 gadgets)                              */
    /* ================================================================== */

    app->titlebar_closeBtn   = (Object *)NewObject(BUTTON_GetClass(), NULL,
    // because just WMHI_GadgetUp    ICA_TARGET, (ULONG)TargetInstance,
        GA_ID,   GID_TITLEBAR_CLOSE,
        //GA_Text, "X",
        GA_RelVerify, TRUE,
         TAG_DONE);
    app->titlebar_iconifyBtn = (Object *)NewObject(BUTTON_GetClass(), NULL,
     // because just WMHI_GadgetUp    ICA_TARGET, (ULONG)TargetInstance,
        GA_ID,   GID_TITLEBAR_ICONIFY,
        //GA_Text, "-",
        GA_RelVerify, TRUE,
         TAG_DONE);
    app->titlebar_altposBtn  = (Object *)NewObject(BUTTON_GetClass(), NULL,
      // because just WMHI_GadgetUp   ICA_TARGET, (ULONG)TargetInstance,
        GA_ID,   GID_TITLEBAR_ALTPOS,
        //GA_Text, "=",
        GA_RelVerify, TRUE,
         TAG_DONE);
    app->titlebar_depthBtn   = (Object *)NewObject(BUTTON_GetClass(), NULL,
     // because just WMHI_GadgetUp    ICA_TARGET, (ULONG)TargetInstance,
        GA_ID,   GID_TITLEBAR_DEPTH,
        //GA_Text, "^",
        GA_RelVerify, TRUE,
         TAG_DONE);
         /*olde
    app->titlebar_settingsBtn = (Object *)NewObject(UniButtonP9Class, NULL,
        ICA_TARGET, (ULONG)TargetInstance,
        GA_ID,                  GID_TITLEBAR_SETTINGS,
        GA_Text,                (ULONG)"\xE2\x9A\x99 Settings",
        UBTP9_URPDrawContext,   (ULONG)app->buttonDC,
        UBTP9_BevelStyle,       BVS_NONE, //BVS_NONE,
        TAG_END);
    */
    app->titlebar_accountBtn = (Object *)NewObject(UniButtonP9Class, NULL,
        ICA_TARGET, (ULONG)TargetInstance,
        GA_ID,                  GID_TITLEBAR_ACCOUNTS,
        GA_Text,                (ULONG)"\xF0\x9F\x91\xA4 Accounts",
        UBTP9_URPDrawContext,   (ULONG)app->buttonDC,
        UBTP9_BevelStyle,       BVS_NONE, //BVS_NONE,
        TAG_END);

    app->titlebar_newtootBtn = (Object *)NewObject(UniButtonP9Class, NULL,
        ICA_TARGET, (ULONG)TargetInstance,
        GA_ID,                  GID_TITLEBAR_NEWTOOT,
        GA_Text,                (ULONG)"\xE2\x9C\x8D Toot+",
        UBTP9_URPDrawContext,   (ULONG)app->buttonDC,
        UBTP9_BevelStyle,       BVS_NONE, //BVS_NONE,
        TAG_END);
    /* titlebar_postsLabel and titlebar_newPostsLabel disabled */
    /*
    app->titlebar_postsLabel    = makeLabel("Posts:0", dpiH);
    app->titlebar_newPostsLabel = makeLabel("New:0",   dpiH);
    */

    if (!app->titlebar_closeBtn   || !app->titlebar_iconifyBtn ||
        !app->titlebar_altposBtn  || !app->titlebar_depthBtn)
        cleanexit("Can't create title bar gadgets");

    app->titleBarLayout = (Object *)NewObject(TitleBarLayoutClass, NULL,
        LAYOUT_BevelStyle, BVS_NONE,
        LAYOUT_SpaceOuter, FALSE,
        LAYOUT_SpaceInner, FALSE,
        /* GA_BackFill is applicability (OM_NEW) only -- must be installed
         * here, not via a later SetAttrs. The hook (tiling tbbg.png) reads
         * app->style's currently loaded background each time it runs, so
         * it works correctly even though no theme is loaded yet at this
         * point (see FS3EStyle_TitleBarBackFillFunc in fs3estyle.c). */
        LAYOUT_BackFill,    (ULONG)&app->style.tbBgHook,
        TBLAYOUT_DpiHeight, (ULONG)dpiH,
        TBLAYOUT_Style,     (ULONG)&app->style,
        /* Row-2 user icon -- drawn directly by TitleBarLayout_OnRender(),
         * not a child gadget (see fs3etitlebar.c). accountAcct may
         * already be non-NULL here (FS3EApp_LoadAccount() ran earlier in
         * main() and calls FS3EApp_SetAccount(), which also SetAttrs's
         * this same tag -- but titleBarLayout didn't exist yet then). */
        TBLAYOUT_AvatarImages, (ULONG)app->avatarImages,
        TBLAYOUT_AccountAcct,  (ULONG)app->accountAcct,
        /* children in required order (see fs3etitlebar.h) */
        LAYOUT_AddChild, (ULONG)app->titlebar_closeBtn,
        LAYOUT_AddChild, (ULONG)app->titlebar_iconifyBtn,
        LAYOUT_AddChild, (ULONG)app->titlebar_altposBtn,
        LAYOUT_AddChild, (ULONG)app->titlebar_depthBtn,

    /*olde    LAYOUT_AddChild, (ULONG)app->titlebar_settingsBtn,*/
        LAYOUT_AddChild, (ULONG)app->titlebar_accountBtn,
        LAYOUT_AddChild, (ULONG)app->titlebar_newtootBtn,
        /* titlebar_postsLabel and titlebar_newPostsLabel disabled */
        TAG_END);
    if (!app->titleBarLayout) cleanexit("Can't create title bar layout");

    /* ================================================================== */
    /* Part B: navigation bar children (8 buttons)                         */
    /* ================================================================== */
// \xf0\x9f\x8f\xa0
{
    const int basew=96;
// single silhouette 	%F0%9F%91%A4
    app->nav_btns[0] = makeBtn(GID_NAV_USER,          "\xF0\x9F\x97\xA3 User",      dpiH,0,0,TRUE);
    app->nav_btns[1] = makeBtn(GID_NAV_HOME,          "\xE2\x8C\x82 Home",      dpiH,basew,0,TRUE);
    app->nav_btns[2] = makeBtn(GID_NAV_LOCAL,         "\xF0\x9F\x91\xA5 Local",     dpiH,basew*2,0,TRUE);
    app->nav_btns[3] = makeBtn(GID_NAV_FEDERATED,     "\xF0\x9F\x8C\x8E Fed.",      dpiH,basew*3,0,TRUE);

    app->nav_btns[4] = makeBtn(GID_NAV_SEARCH,        "\xF0\x9F\x94\x8D Search",    dpiH,0,32,TRUE);
    app->nav_btns[5] = makeBtn(GID_NAV_NOTIFICATIONS, "\xF0\x9F\x9A\x80 Notif.",    dpiH,basew,32,TRUE);
    app->nav_btns[6] = makeBtn(GID_NAV_BOOKMARKS, "\xF0\x9F\x94\x96 Bookmark",    dpiH,basew*2,32,TRUE);
    app->nav_btns[7] = makeBtn(GID_NAV_NEWS, "\xF0\x9F\x93\xB0 News",    dpiH,basew*3,32,TRUE);
//    app->nav_btns[7] = makeBtn(GID_NAV_NEWTOOT,       "\xF0\x9F\x97\xA3 Toot+",     dpiH,basew*3,32,FALSE);
}
//    app->nav_btns[6] = makeBtn(GID_NAV_SETTINGS,      "\xE2\x9A\x99 Settings",  dpiH);
//    app->nav_btns[7] = makeBtn(GID_NAV_ACCOUNTS,      "\xF0\x9F\x91\xA4 Accounts",  dpiH);

    {
        int i;
        for (i = 0; i < 8; i++)
            if (!app->nav_btns[i]) cleanexit("Can't create nav bar button");
    }

    app->navBarLayout = (Object *)NewObject(NavBarLayoutClass, NULL,
        LAYOUT_BevelStyle, BVS_NONE,
        LAYOUT_SpaceOuter, FALSE,
        LAYOUT_SpaceInner, FALSE,
        LAYOUT_BackFill,NULL,
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
        TTIMELINE_Style, (ULONG)(&app->style),
        TTIMELINE_DpiHeight, (ULONG)dpiH,
        ICA_TARGET, (ULONG)TargetInstance,
        GA_ID,      GID_TTIMELINE,
        GA_BackFill, NULL,
        TAG_END);
    if (!app->tootTimeline) cleanexit("Can't create toot timeline");

    if (app->avatarImages)
        SetAttrs(app->tootTimeline,
                 TTIMELINE_AvatarImages, (ULONG)app->avatarImages,
                 TAG_DONE);

 flushbdbprint();

    /* (fake test posts removed — real data comes from FS3ENETQ_TIMELINE replies) */
 flushbdbprint();

    /* ================================================================== */
    /* Part C: search word editor, wrapped with tootTimeline in           */
    /* SearchBarLayoutClass (see fs3esearchbar.h). One-line UniTextEditor, */
    /* same one-line setup as EmojiGear/egsearchbox.c's searchEditor.     */
    /* Hidden by default -- fs3e_setViewMode toggles SBLAYOUT_Visible and  */
    /* RethinkLayout()s just this subtree when VIEWMODE_Search is entered/ */
    /* left, instead of relaying out the whole window.                    */
    /* ================================================================== */
    app->searchWordEditor = (Object *)NewObject(UNITEXTEDITOR_GetClass(), NULL,
        GA_ID,                  (ULONG)GID_SEARCH_WORD_EDITOR,
        ICA_TARGET,             (ULONG)TargetInstance,
        UTED_KeyMessageMode,    UKM_Internal,
        UTED_BevelStyle,        BVS_FIELD,
        UTED_URPDrawContext,    (ULONG)app->buttonDC,
        UTED_TextPen,           1UL,
        UTED_BgPen,             0UL,
        UTED_MaxDisplayLines,   1UL,
        UTED_NoLineFeed,        TRUE,
        UTED_WordWrap,          FALSE,
        UTED_LeftMargin,        2,
        UTED_TopMargin,         3,
        UTED_BottomMargin,      1,
        UTED_LineSpacing,       0,
        TAG_END);
    if (!app->searchWordEditor) cleanexit("Can't create search word editor");

    /* "Word"/"People" mode picker, far right of the search row (see
     * StartSearchFromLine below and fs3esearchbar.h). Same
     * AllocChooserNode/NewObject(CHOOSER_GetClass()) pattern as
     * fs3etootview.c's visibilityChooser. */
    NewList(&app->searchWordTypeList);
    {
        static const ULONG searchTypeMsgIds[2] = { MSG_SEARCH_TYPE_WORD, MSG_SEARCH_TYPE_PEOPLE };
        int i;
        for (i = 0; i < 2; i++) {
            struct Node *node = NULL;
            if (ChooserBase)
                node = AllocChooserNode(CNA_Text, (ULONG)LOC(searchTypeMsgIds[i]), TAG_END);
            app->searchWordTypeNodes[i] = node;
            if (node) AddTail(&app->searchWordTypeList, node);
        }
    }

    app->searchWordTypeChooser = (Object *)NewObject(CHOOSER_GetClass(), NULL,
        GA_ID,          (ULONG)GID_SEARCH_WORD_TYPE_CHOOSER,
        GA_RelVerify,   TRUE,
        ICA_TARGET,     (ULONG)TargetInstance,
        CHOOSER_PopUp,  TRUE,
        CHOOSER_Labels, (ULONG)&app->searchWordTypeList,
        CHOOSER_Active, (ULONG)FS3ESEARCHTYPE_WORD,
        TAG_END);
    if (!app->searchWordTypeChooser) cleanexit("Can't create search type chooser");

    /* Back button, far right of the search row (see FS3EApp_SearchGoBack
     * in fs3erequests.c and fs3esearchbar.h) -- same makeBtn() helper as
     * every nav/titlebar button, U+1F519 BACK arrow emoji matching that
     * house style. */
     // \xF0\x9F\x94\x99
    app->searchBackButton = makeBtnGadgetUpOnly(GID_SEARCH_BACK_BUTTON, " \xE2\x97\x80 ", dpiH, 0, 0, FALSE);
    if (!app->searchBackButton) cleanexit("Can't create search back button");

    app->searchBarLayout = (Object *)NewObject(SearchBarLayoutClass, NULL,
        LAYOUT_BevelStyle, BVS_NONE,
        LAYOUT_SpaceOuter, FALSE,
        LAYOUT_SpaceInner, FALSE,
        LAYOUT_BackFill,   NULL,
        SBLAYOUT_Visible,  FALSE,
        /* children in required order (see fs3esearchbar.h) */
        LAYOUT_AddChild,   (ULONG)app->searchWordEditor,
        LAYOUT_AddChild,   (ULONG)app->tootTimeline,
        LAYOUT_AddChild,   (ULONG)app->searchWordTypeChooser,
        LAYOUT_AddChild,   (ULONG)app->searchBackButton,
        TAG_END);
    if (!app->searchBarLayout) cleanexit("Can't create search bar layout");

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

        LAYOUT_AddChild,    (ULONG)app->searchBarLayout,
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
// printf("FS3EApp_ApplyFontSettings_Delayed\n");
    /* synchronize fonts against settings before first layout */
    FS3EApp_ApplyFontSettings_Delayed();
// printf("fs3e_setViewMode\n");
    /* home by default ? */
    fs3e_setViewMode(VIEWMODE_Home);

    flushbdbprint();


/*---*/


// printf("FS3EMain_Show\n");

    FS3EMain_Show(&app->mainwindow, app->window_obj);
    if (!CurrentMainWindow) cleanexit("Can't open window");

    /* Restore the last activated theme (app->settings.themeName, NULL/""
     * = no theme) now that a real screen is bound -- GenericOpenWindow
     * already loaded the startup-default theme path moments earlier
     * inside FS3EMain_Show(), this overrides it with the user's actual
     * persisted choice. See FS3EApp_LoadTheme's doc comment. Calls the
     * _Delayed worker directly (like FS3EApp_ApplyFontSettings_Delayed()
     * above) rather than through the debounced public trigger, since
     * there's no event loop running yet to deliver the deferred signal. */
    FS3EApp_LoadTheme_Delayed();

    FS3EMenu_Create(&app->menu, CurrentMainScreen, CurrentMainWindow);
// printf("loop\n");
    /* - - - Input Event Loop - - - */
    {
        ULONG winsignal;
        BOOL  ok = TRUE;

#define reflags_bodyEditor    2
#define reflags_tootTimeLine    4
#define reflags_searchworduted 8
#define reflags_navBtns        16
        GetAttr(WINDOW_SigMask, app->window_obj, &winsignal);

        while (ok)
        {
            ULONG result, waitedSignals, currentSignals;
            ULONG loginSig, tootSig;
            ULONG refreshFlags = 0;
            flushbdbprint();

            loginSig = FS3ELoginView_GetSignalMask(&app->loginView);
            tootSig  = FS3ETootView_GetSignalMask(&app->tootView);
            ULONG themeSig = FS3EThemeView_GetSignalMask(&app->themeView);
            ULONG settingsSig = FS3ESettingsView_GetSignalMask(&app->settingsView);
            ULONG emojiSig = FS3EEmojiBoxWindow_GetSignalMask(&app->emojiBoxWindow);
            ULONG mediaSig = FS3EMediaView_GetSignalMask(&app->mediaView);

            waitedSignals = winsignal | loginSig | tootSig | themeSig | settingsSig | emojiSig | mediaSig |
                (1L << app->app_port->mp_SigBit) |
                (app->netReplyPort ? (1L << app->netReplyPort->mp_SigBit) : 0) |
                (app->thumbReplyPort ? (1L << app->thumbReplyPort->mp_SigBit) : 0) |
                (app->audioReplyPort ? (1L << app->audioReplyPort->mp_SigBit) : 0) |
                (SIPCPort ? (1L << SIPCPort->mp_SigBit) : 0) |
                SIGBREAKF_CTRL_C |
                SIGBREAKF_CTRL_F |
                FS3ETIMER_SIGNAL;

            currentSignals = Wait(waitedSignals);

            if (currentSignals & SIGBREAKF_CTRL_C) exit(0);

            if (currentSignals & FS3ETIMER_SIGNAL)
                FS3ETimer_Process();

            /* A second FriendSh3ep launch asked us to activate -- see
             * FS3E_CheckSingleInstance(). Re-open if iconified, otherwise
             * just bring the window forward. */
            if (SIPCPort && (currentSignals & (1L << SIPCPort->mp_SigBit)))
            {
                struct Message *sipcMsg;
                while ((sipcMsg = GetMsg(SIPCPort)) != NULL)
                {
                    if (!CurrentMainWindow)
                    {
                        FS3EMain_Show(&app->mainwindow, app->window_obj);
                        if (CurrentMainWindow)
                        {
                            FS3EMenu_Create(&app->menu, CurrentMainScreen, CurrentMainWindow);
                            if (app->avatarImages)
                                FS3EApp_UpdateUserIcon();
                        }
                    }
                    if (CurrentMainWindow)
                    {
                        WindowToFront(CurrentMainWindow);
                        ActivateWindow(CurrentMainWindow);
                        ScreenToFront(CurrentMainWindow->WScreen);
                    }
                    ReplyMsg(sipcMsg);
                }
            }

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

                        // test: do not redirect those
                        BoopsiDelay_BeginMessage(DelayQueue, senderId);
                        BoopsiDelay_AddTag(DelayQueue, GA_Selected, 0);
                        BoopsiDelay_EndMessage(DelayQueue);
                        break;
                    }

                    case WMHI_ICONIFY:
                        FS3EMenu_Close(&app->menu, CurrentMainWindow);
                        closeExternalViews();
                        /* Avatar bitmaps are plain Fast-RAM RGB pixel arrays now
                         * (see rgbimage.h) -- no screen-bound resource to free
                         * across an iconify/uniconify cycle. */
                        FS3EMain_Close(&app->mainwindow, app->window_obj, TRUE);
                        break;

                    case WMHI_UNICONIFY:
                        FS3EMain_Show(&app->mainwindow, app->window_obj);
                        if (!CurrentMainWindow) cleanexit("can't re-open window");
                        FS3EMenu_Create(&app->menu, CurrentMainScreen, CurrentMainWindow);
                        if (app->avatarImages)
                            FS3EApp_UpdateUserIcon();
                        break;

                    case WMHI_RAWKEY:
                    {
                        ULONG key = result & 0x07f;
                        ULONG isUp = (result & 0x080);
                        ULONG qualifiers=0;
                        int keyUsed=0;
                        if (key == 0x45) ok = FALSE; /* Escape */

// printf("WMHI_RAWKEY %08x\n",key);
                        GetAttr(WINDOW_Qualifier,app->window_obj,&qualifiers);

                        if(!isUp )
                        {
                            /*keys F1-F4 are the 4 view mode */
                            if( (key >=0x50 && key<= 0x53) || (key >=0x55 && key<= 0x57))
                            {
                                fs3e_setViewMode((ULONG)(key-0x50));
                            } else
                            /* let's have F5 being "refresh" */
                            if( key == 0x54)
                            {
                                FS3EApp_RefreshVisibleToots();
                            } else
                            /* Delete = "back" in the Search view's history
                             * (see FS3EApp_SearchGoBack). Like F1-F8/F5
                             * above, this only actually fires while the
                             * search word editor does NOT hold BOOPSI
                             * keyboard activation -- its UKM_Internal mode
                             * fully consumes RAWKEY (including Delete, for
                             * its own text editing) while it's the active
                             * gadget, so this case is simply unreached
                             * then, same pre-existing constraint every
                             * other shortcut here already has. No-op
                             * itself if there's no history to go back to. */
                            if( key == 0x46)
                            {
                                FS3EApp_SearchGoBack();
                            }
                            if( key == 0x7a || key == 0x4c) /* mouse wheel up / Up arrow -- scroll toward newer content */
                            {
                                if (app->tootTimeline)

                                    SetGdAttrs(app->tootTimeline, TTIMELINE_ScrollBy,
                                             (ULONG)(LONG)-FS3E_WHEEL_SCROLL_PIXELS, TAG_DONE);
                                /* Manual scroll always wins over a "Next
                                 * toot" animation still in flight from a
                                 * previous Space press -- see this
                                 * function's own doc comment in
                                 * fs3eaction.h. */
                                Action_TimelineStopScrollAnimation();
                            }
                            if( key == 0x7b|| key == 0x4d ) /* mouse wheel down / Down arrow -- scroll toward older content */
                            {
                                if (app->tootTimeline)
                                    SetGdAttrs(app->tootTimeline, TTIMELINE_ScrollBy,
                                             (ULONG)(LONG)FS3E_WHEEL_SCROLL_PIXELS, TAG_DONE);
                                Action_TimelineStopScrollAnimation();
                            }
                            /* Timeline menu shortcuts that can't be real
                             * GadTools CommKeys (see fs3eaction.c's
                             * Action_Timeline* -- all stubs today, this is
                             * just the key plumbing): Space can't
                             * sensibly be one, and P is already Settings
                             * -> General's CommKey, so Autoscroll Play
                             * stays on a bare keypress instead of
                             * colliding with it -- see fs3emenu.c's
                             * Timeline section comment. U and C (Move to
                             * Top / Copy Toot Text) do NOT need handling
                             * here -- they're real Amiga+U/Amiga+C
                             * CommKeys now, fired via the normal
                             * WMHI_MENUPICK path instead. Same "plain
                             * unmodified key, only reachable while no
                             * text-entry gadget holds BOOPSI focus"
                             * caveat as Delete/wheel above applies to
                             * Space/P here. Space is context-dependent on
                             * timelineAutoscrollActive (Next toot when
                             * idle, Stop autoscroll when running) -- see
                             * that field's comment in friendsh3ep.h. */
                            if( key == 0x40 ) /* Space */
                            {
                                if (app->timelineAutoscrollActive)
                                    Action_TimelineAutoscrollStop(app);
                                else
                                    Action_TimelineNextToot(app);
                            }
                            if( key == 0x19 ) /* P -- autoscroll play */
                            {
                            // printf("Action_TimelineAutoscrollPlay\n");
                                Action_TimelineAutoscrollPlay(app);
                            }
                         }
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
            FS3ESettingsView_HandleInput(&app->settingsView);
            FS3EEmojiBoxWindow_HandleInput(&app->emojiBoxWindow);
            FS3EEmojiBoxWindow_FlushPendingRender(&app->emojiBoxWindow);
            FS3EMediaView_HandleInput(&app->mediaView);

            /* Drain async network replies */
            if (app->netReplyPort &&
                (currentSignals & (1L << app->netReplyPort->mp_SigBit)))
            {
                FS3ENetMessage *netMsg;
                while ((netMsg = (FS3ENetMessage *)GetMsg(app->netReplyPort)) != NULL) {
                    FS3EApp_HandleNetReply(netMsg);
                    FreeVec(netMsg->fs3em_Data);
                    FreeVec(netMsg);
                }
            }

            /* Drain async thumbnail-process replies */
            if (app->thumbReplyPort &&
                (currentSignals & (1L << app->thumbReplyPort->mp_SigBit)))
            {
                FS3EThumbMessage *thumbMsg;
                while ((thumbMsg = (FS3EThumbMessage *)GetMsg(app->thumbReplyPort)) != NULL) {
                    FS3EApp_HandleThumbReply(thumbMsg);
                    FreeVec(thumbMsg);
                }
            }

            /* Drain audio-process replies/notifies -- generic player only
             * for now (see fs3eaudio.h), nothing sends FS3EAUDIOQ_PLAY yet,
             * so there is nothing to act on here besides not leaking the
             * message; still drained unconditionally the same way the other
             * processes' reply ports are, ready for toot-audio wiring later. */
            if (app->audioReplyPort &&
                (currentSignals & (1L << app->audioReplyPort->mp_SigBit)))
            {
                FS3EAudioMessage *audioMsg;
                while ((audioMsg = (FS3EAudioMessage *)GetMsg(app->audioReplyPort)) != NULL) {
                    FreeVec(audioMsg);
                }
            }

            /* Drain the BOOPSI notification queue (OM_NOTIFY via ICA_TARGET). */
            if (DelayQueue && BoopsiDelay_HasMessages(DelayQueue))
            {
                struct TagItem *msg;


                while ((msg = BoopsiDelay_NextMessage(DelayQueue)) != NULL) {
                    struct TagItem *ptag;
                    ULONG sender_ID = 0;

                    ptag = FindTagItem(GA_ID, msg);
                    if (ptag) sender_ID = ptag->ti_Data;

                    switch (sender_ID) {

                        /* ---- Title bar ---- */
                        case GID_TITLEBAR_CLOSE:
                          //  ptag = FindTagItem(GA_Selected, msg);
                          //  if (ptag && ptag->ti_Data)  /* when push button down (selected true) */
                            {
                                ok = FALSE;
                            }
                            break;

                        case GID_TITLEBAR_ICONIFY:
                           // ptag = FindTagItem(GA_Selected, msg);
                           // if (ptag && ptag->ti_Data)  /* when push button down (selected true) */
                            {
                                closeExternalViews();
                                FS3EMain_Close(&app->mainwindow, app->window_obj, TRUE);
                            }
                            break;

                        case GID_TITLEBAR_ALTPOS:
                            // ptag = FindTagItem(GA_Selected, msg);
                            // if (ptag && ptag->ti_Data==0)  /* when push button down (selected true) */
                             {
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
                            }
                            break;

                        case GID_TITLEBAR_DEPTH:
                           // ptag = FindTagItem(GA_Selected, msg);
                           // if (ptag && ptag->ti_Data)  /* when push button down (selected true) */
                            {
                                if (CurrentMainWindow && CurrentMainScreen) {
                                    /* Walk layers front→back; first layer whose
                                     * Window is non-NULL and not a backdrop is the
                                     * true frontmost user window. */
                                    BOOL isFront = FALSE;
                                    struct Layer_Info *li = &CurrentMainScreen->LayerInfo;
                                    struct Layer *lay;
                                    LockLayerInfo(li);
                                    lay = li->top_layer;
                                   if(lay)
                                   //while (lay)
                                   {
                                        struct Window *w = (struct Window *)lay->Window;
                                        if (w && !(w->Flags & WFLG_BACKDROP)) {
                                            isFront = (w == CurrentMainWindow);
                                           // break;
                                        }
                                     //  lay = lay->back;
                                    }
                                    UnlockLayerInfo(li);
                                    if (isFront)
                                        WindowToBack(CurrentMainWindow);
                                    else
                                        WindowToFront(CurrentMainWindow);
                                }
                            }
                            break;

                        /* ---- Navigation bar ---- */
                        case GID_NAV_USER:
                        case GID_NAV_HOME:
                        case GID_NAV_LOCAL:
                        case GID_NAV_FEDERATED:
                        case GID_NAV_SEARCH:
                        case GID_NAV_NOTIFICATIONS:
                        case GID_NAV_BOOKMARKS:
                        case GID_NAV_NEWS:
                            /* switch timeline / view */
                            ptag = FindTagItem(GA_Selected, msg);
                            if (ptag)  /* when push button down (selected true) */
                            {
                                if (ptag && ptag->ti_Data)
                                {
                                    /* possible because the GID order and the view enum match */
                                    fs3e_setViewMode((ULONG)(sender_ID-GID_NAV_USER));
                                }
                                else
                                {   /* if up but is current viewmode, put selection back. */
                                    if(app->viewMode == (ULONG)(sender_ID-GID_NAV_USER))
                                    {
                                        SetGdAttrs(app->nav_btns[app->viewMode],
                                                    GA_Selected,TRUE,TAG_END);
                                    }
                                }

                            }

                            /* One of nav_btns[] (UniButtonBGBM) found its
                             * UBGBM_Patch9Mode cache stale while GM_RENDER
                             * ran off this task (e.g. Intuition driving a
                             * live window resize) and can't safely rebuild
                             * it there (FreeType) -- see
                             * ubgbm_notify_refresh_needed()'s doc comment.
                             * OR a flag instead of refreshing right here:
                             * every one of the 8 buttons can report this
                             * in the same drain pass, and a single
                             * RefreshGList() of navBarLayout below (once
                             * per loop iteration, alongside the other
                             * reflags_* checks) already redraws all of
                             * them -- doing it per-message would mean up
                             * to 8 redundant full navBarLayout redraws. */
                            ptag = FindTagItem(UBGBM_RefreshNeeded, msg);
                            if (ptag && ptag->ti_Data)
                            {
                                refreshFlags |= reflags_navBtns;
                            }
                            break;

                        case GID_TITLEBAR_NEWTOOT:
                            ptag = FindTagItem(GA_Selected, msg);
                            if (ptag /*&& ptag->ti_Data*/)  /* when push button down (selected true) */
                            {
                                /* Resets any leftover MODIFY/REPLY compose
                                 * state from a previous open (postId, title)
                                 * -- doesn't touch bodyEditor's text itself,
                                 * so an in-progress draft still survives a
                                 * close/reopen the way it always has. */
                                FS3ETootView_SetComposeContext(&app->tootView, FS3ETOOT_KIND_NEW, NULL);
                                FS3ETootView_Open(&app->tootView);
                            }
                            break;

                        case GID_TITLEBAR_ACCOUNTS:
                            ptag = FindTagItem(GA_Selected, msg);
                            if (ptag /*&& ptag->ti_Data*/)  /* when push button down (selected true) */
                            {
                                FS3EApp_RefreshLoginAccountsList(); /* never stale when shown */
                                FS3ELoginView_Open(&app->loginView);
                            }
                            break;

                        case GID_SEARCH_BACK_BUTTON:
                            ptag = FindTagItem(GA_Selected, msg);
                            if (ptag /*&& ptag->ti_Data*/)  /* when push button down (selected true) */
                            {
                                FS3EApp_SearchGoBack();
                            }
                            break;

                        /* ---- Login sub-window: phase 1 ---- */
                        case GID_LOGIN_LOGIN_BUTTON:
                        {
                            ptag = FindTagItem(GA_Selected, msg);
                            if (ptag && ptag->ti_Data)  /* when push button down (selected true) */
                                FS3EApp_LoginStart();
                            break;
                        }

                        /* ---- Login sub-window: phase 2 ---- */
                        case GID_LOGIN_SUBMIT_CODE_BUTTON:
                        {
                            ptag = FindTagItem(GA_Selected, msg);
                            if (ptag && ptag->ti_Data)  /* when push button down (selected true) */
                                FS3EApp_LoginSubmitCode();
                            break;
                        }

                        /* ---- Login sub-window: accounts list (click a row to switch) ---- */
                        case GID_LOGIN_ACCOUNTS_LIST:
                        {
                            ptag = FindTagItem(LISTBROWSER_Selected, msg);
                            if (ptag && (LONG)ptag->ti_Data >= 0)
                                FS3EApp_SwitchAccount((LONG)ptag->ti_Data);
                            break;
                        }

                        /* ---- New toot sub-window ---- */
                        case GID_TOOT_SEND_BUTTON:
                        {
                            ptag = FindTagItem(GA_Selected, msg);
                            if (ptag && ptag->ti_Data==0)  /* when push button UP */
                            {
                                /* watch out, return of FS3ETootView_GetUTF8Body() must be freevec() by us */
                                const char *body    = FS3ETootView_GetUTF8Body(&app->tootView);
                                LONG visibility     = FS3ETootView_GetVisibility(&app->tootView);
                                LONG quotePolicy    = FS3ETootView_GetQuotePolicy(&app->tootView);
                                if (body && body[0] && app->accountAccessToken) {
                                    if (app->tootView.composeKind == FS3ETOOT_KIND_MODIFY &&
                                        app->tootView.composePostId)
                                    {
                                        /* Editing an existing toot -- PUT, not
                                         * POST. Resends the media ids captured
                                         * when Modify was opened
                                         * (composeMediaIds[]/composeMediaCount)
                                         * so the edit doesn't strip attached
                                         * media -- see FS3EMastodon_EditStatus.
                                         * No visibility/spoiler: Mastodon's
                                         * edit endpoint doesn't accept either. */
                                        FS3ENetEditStatusReq *req =
                                            FS3ENetEditStatusReq_Alloc(
                                                app->accountApiBaseUrl,
                                                app->accountAccessToken,
                                                app->tootView.composePostId,
                                                body,
                                                (const char *const *)app->tootView.composeMediaIds,
                                                app->tootView.composeMediaCount);
                                        if(body) FreeVec(body);
                                        FS3EApp_NetSend(FS3ENETQ_EDIT_STATUS, req, sizeof(*req));
                                    }
                                    else
                                    {
                                        /* No subject/CW field in this window (see
                                         * fs3etootview.h's contextMessage) -- Mastodon
                                         * toots have no subject concept, so spoiler
                                         * text is always empty. REPLY's composePostId
                                         * is the status being replied to -- see
                                         * FS3EMastodon_PostStatus's inReplyToId. */
                                        const char *inReplyToId =
                                            (app->tootView.composeKind == FS3ETOOT_KIND_REPLY)
                                            ? app->tootView.composePostId : "";
                                        FS3ENetPostStatusReq *req =
                                            FS3ENetPostStatusReq_Alloc(
                                                app->accountApiBaseUrl,
                                                app->accountAccessToken,
                                                body,
                                                VisibilityString(visibility),
                                                "",
                                                inReplyToId,
                                                QuotePolicyString(quotePolicy));
                                        if(body) FreeVec(body);
                                        FS3EApp_NetSend(FS3ENETQ_POST_STATUS, req, sizeof(*req));
                                    }
                                }
                            }
                            break;
                        }

                        case GID_TOOT_BODY_EDITOR:
                            refreshFlags |= reflags_bodyEditor;

                        /* we asked unitexteditor, in UKM_Internal mode,
                         * to notify us back rawkey codes and qualifiers */
                         if((ptag = FindTagItem(UTED_InternalRawKey_Code, msg))!=NULL)
                         {
                            ULONG qulkey = ptag->ti_Data;
                            int isUp = 0x0080 & qulkey;
                            UWORD key = (UWORD)(0x007f & qulkey);
                            UWORD qualifiers = (UWORD)(qulkey>>16);

                            if(!isUp && key>=0x50 && key<=0x59 && app->tootView.window)
                            {
                                FS3EEmojiBox_HandleFKey(&app->emojiBoxWindow,
                                        app->tootView.bodyEditor,key, qualifiers, app->tootView.window);
                            }
                            /* if edidtor has focus (activation), escape key to close the window is here */
                            if(isUp && key == 0x45)
                            {
                                FS3ETootView_Close(&app->tootView);
                            }

                         }
                         if((ptag = FindTagItem(UTEDN_CursorMoved, msg))!=NULL)
                         {
                            FS3ETootView_UpdateCharCount(&app->tootView);
                         }
                            break;

                        case GID_TOOT_EMOJI_BUTTON:
                            ptag = FindTagItem(GA_Selected, msg);
                            if (ptag /*&& ptag->ti_Data*/)  /* when push button down (selected true) */
                            {
                                FS3EEmojiBoxWindow_Open(&app->emojiBoxWindow);
                            }
                            break;



                        /* ---- Emoji box: a grid cell was clicked -- insert it
                         * into the toot body (the only compose field FriendSh3ep
                         * offers this from; see fs3eemojibox.h). ---- */
                        case GID_EMOJIBOX_GRID:
                        {
                            const char *emoji =
                                FS3EEmojiBoxWindow_GetClickedUTF8(&app->emojiBoxWindow);
                            if (emoji && app->tootView.bodyEditor) {
                                if (app->tootView.window)
                                    SetGadgetAttrs(
                                        (struct Gadget *)app->tootView.bodyEditor,
                                        app->tootView.window, NULL,
                                        UTED_InsertText, (ULONG)emoji,
                                        TAG_DONE);
                                else
                                    SetAttrs(app->tootView.bodyEditor,
                                        UTED_InsertText, (ULONG)emoji,
                                        TAG_END);
                                FS3ETootView_UpdateCharCount(&app->tootView);
                            }
                            break;
                        }
                        case GID_SEARCH_WORD_EDITOR:
                        refreshFlags |= reflags_searchworduted;
                        {
                            ptag = FindTagItem(UTEDN_EnterPressed, msg);
                            if (ptag)
                            {   /* enter pressed on the search line */
                                StartSearchFromLine();

                                /*good idea to send stop having focus to editor so usual keys works */
                                if(CurrentMainWindow && app->tootTimeline)
                                {
                                    ActivateGadget(app->tootTimeline,CurrentMainWindow,NULL);
                                }
                            }
                        }
                        break;
                        case GID_TTIMELINE:
                            refreshFlags |= reflags_tootTimeLine;
                            {
                                ptag = FindTagItem(TTIMELINE_HotSpotNotify, msg);
                                if (ptag)
                                {   /* a hot spot in a toot were clicked: */
                                    ULONG hotSpotType = ptag->ti_Data;
                                    const char *hotSpotString =NULL,*hotSpotId=NULL;
                                    const char *hotSpotMediaIds = NULL;
                                    const char *hotSpotAcct = NULL;
                                    BOOL hotSpotFavourited = FALSE;
                                    BOOL hotSpotFollowing = FALSE;

                                    ptag = FindTagItem(TTIMELINE_LastHotSpotString, msg);
                                    if(ptag) hotSpotString  =(const char *)ptag->ti_Data;

                                    ptag = FindTagItem(TTIMELINE_LastHotSpotPostId, msg);
                                    if(ptag) hotSpotId  =(const char *)ptag->ti_Data;

                                    ptag = FindTagItem(TTIMELINE_LastHotSpotMediaIds, msg);
                                    if(ptag) hotSpotMediaIds = (const char *)ptag->ti_Data;

                                    ptag = FindTagItem(TTIMELINE_LastHotSpotAcct, msg);
                                    if(ptag) hotSpotAcct = (const char *)ptag->ti_Data;

                                    ptag = FindTagItem(TTIMELINE_LastHotSpotFavourited, msg);
                                    if(ptag) hotSpotFavourited = (BOOL)ptag->ti_Data;

                                    ptag = FindTagItem(TTIMELINE_LastHotSpotFollowing, msg);
                                    if(ptag) hotSpotFollowing = (BOOL)ptag->ti_Data;


                                    switch (hotSpotType)
                                    {
                                        case TTL_HOT_AVATAR:
                                        case TTL_HOT_MENTION:
                                            /* Both open the same profile
                                             * view -- an avatar click
                                             * carries the author's bare
                                             * acct, a mention click
                                             * carries "@handle" as it
                                             * appears in the text (bio or
                                             * toot body alike, since bio
                                             * spans get the identical
                                             * mention scan -- see
                                             * ttl_scan_span_tokens). */
                                            FS3EApp_OpenProfile(hotSpotString);
                                            break;

                                        case TTL_HOT_HASHTAG:
                                            /* hotSpotString already carries
                                             * the "#tag" token as it appears
                                             * in the text (see
                                             * TTL_HOT_HASHTAG's comment in
                                             * fs3etoottimeline.h) -- prefill
                                             * the search line with it
                                             * unchanged (keep the '#'),
                                             * switch to the Search channel
                                             * so the bar is visible, force
                                             * the type chooser back to
                                             * "Word" (a hashtag click always
                                             * means a word/hashtag search,
                                             * regardless of whatever the
                                             * user had last picked), and
                                             * fire the search right away
                                             * (StartSearchFromLine reads
                                             * the line back from
                                             * app->searchWordEditor, so the
                                             * SetGdAttrs above must land
                                             * first) -- one click gets both
                                             * the right view and the actual
                                             * results, no separate Enter
                                             * needed. */
                                            fs3e_setViewMode(VIEWMODE_Search);
                                            if (app->searchWordEditor && hotSpotString)
                                                SetGdAttrs(app->searchWordEditor,
                                                    UTED_Text, (ULONG)hotSpotString, TAG_END);
                                            if (app->searchWordTypeChooser)
                                                SetGdAttrs(app->searchWordTypeChooser,
                                                    CHOOSER_Active, (ULONG)FS3ESEARCHTYPE_WORD, TAG_END);
                                            StartSearchFromLine();
                                            break;

                                        case TTL_HOT_THREAD:
                                            /* hotSpotId is the status whose
                                             * discussion to open -- see
                                             * FS3EApp_OpenDiscussion. */
                                            FS3EApp_OpenDiscussion(hotSpotId);
                                            break;

                                        case TTL_HOT_NOTIF_STATUS:
                                            /* Notifications view's actor/verb
                                             * prefix line -- unlike
                                             * TTL_HOT_THREAD, the status id is
                                             * carried as hotSpotString (data),
                                             * not hotSpotId (postId, which for
                                             * a notification row is the
                                             * *notification's* own id -- see
                                             * TTLPostSetup.notifStatusId). */
                                            FS3EApp_OpenDiscussion(hotSpotString);
                                            break;

                                        case TTL_HOT_FOLLOW:
                                            /* Reply/count update happens
                                             * once the server confirms --
                                             * see the FS3ENETQ_FOLLOW
                                             * reply handler, which sends
                                             * TTIMELINE_UpdateProfileFollow.
                                             * Shared with a future "User"
                                             * menu entry, same reasoning
                                             * as Action_ToggleFavorite. */
                                            Action_ToggleFollow(app, app->searchProfileAccountId, hotSpotFollowing);
                                            break;

                                        case TTL_HOT_FOLLOWERS_LIST:
                                            FS3EApp_ShowFollowers();
                                            break;

                                        case TTL_HOT_FOLLOWING_LIST:
                                            FS3EApp_ShowFollowing();
                                            break;

                                        case TTL_HOT_MEDIA_PREV:
                                        case TTL_HOT_MEDIA_NEXT:
                                            /* The gadget already advanced
                                             * mediaCurrentIndex, invalidated
                                             * the tile, and asked itself for
                                             * a redraw before sending this
                                             * notification -- nothing left
                                             * for the main loop to do. */
                                            break;

                                        case TTL_HOT_IMAGE:
                                            /* Media preview rectangle
                                             * clicked -- hotSpotString
                                             * carries the currently-shown
                                             * attachment's URL (see
                                             * ttl_hs_add's TTL_HOT_IMAGE
                                             * call in fs3etoottimeline_posts.c).
                                             * hotSpotAcct (TTIMELINE_LastHotSpotAcct)
                                             * is the poster's @user@instance,
                                             * used by "Save Media..." to
                                             * build a meaningful filename --
                                             * see fs3emediaview.c. Opens/
                                             * reuses the "FriendSh3ep
                                             * Media" viewer window. */
                                            FS3EMediaView_ShowUrl(&app->mediaView, hotSpotString, hotSpotAcct);
                                            break;

                                        case TTL_HOT_LOAD_NEWER:
                                            /* Pinned "Look for something
                                             * new" row clicked. */
                                            FS3EApp_FetchTimelinePage(app->viewMode, FS3ENETPAGE_NEWER);
                                            break;

                                        case TTL_HOT_LOAD_OLDER:
                                            /* Pinned "Load more…" row
                                             * reached the bottom of the
                                             * viewport (see TTL_OnRender's
                                             * proximity check). */
                                            FS3EApp_FetchTimelinePage(app->viewMode, FS3ENETPAGE_OLDER);
                                            break;

                                        case TTL_HOT_PLAY_AUDIO:
                                            /* TODO: actual MP3 playback
                                             * (deliberately not via
                                             * datatypes.library) is a
                                             * separate follow-up; for now
                                             * just surface the click.
                                             * hotSpotString carries the
                                             * attachment URL. */
                                            break;

                                        case TTL_HOT_CARD:
                                            /* TODO: clipboard copy, and
                                             * later an optional AmigaOS
                                             * browser-launch library --
                                             * deferred, same as TTL_HOT_URL
                                             * (body-text links). hotSpotString
                                             * carries the card's article URL. */
                                            break;

                                        case TTL_HOT_FAVORITE:
                                            /* Reply/count update happens
                                             * once the server confirms --
                                             * see the FS3ENETQ_FAVORITE
                                             * reply handler, which sends
                                             * TTIMELINE_UpdatePost. Shared
                                             * with the future "This Toot"
                                             * menu entry -- see
                                             * fs3eaction.c. */
                                            Action_ToggleFavorite(app, hotSpotId, hotSpotFavourited);
                                            break;

                                        case TTL_HOT_MODIFY:
                                            /* hotSpotString carries the toot's
                                             * raw body (see
                                             * ttl_toot_build_hotspots's data
                                             * for TTL_HOT_MODIFY), hotSpotId
                                             * its status id, hotSpotMediaIds
                                             * its comma-joined attachment ids
                                             * (TTIMELINE_LastHotSpotMediaIds)
                                             * -- all handed to the compose
                                             * window so it opens prefilled
                                             * with no separate lookup, and so
                                             * a future edit-submit can resend
                                             * the same media_ids and not lose
                                             * the attachments. */
                                            {
                                                FS3ETootComposeParams params;
                                                char mediaIdsBuf[96];
                                                ULONG mcount = 0;

                                                memset(&params, 0, sizeof(params));
                                                params.postId = hotSpotId;
                                                params.body   = hotSpotString;

                                                if (hotSpotMediaIds && hotSpotMediaIds[0]) {
                                                    char *p;
                                                    strncpy(mediaIdsBuf, hotSpotMediaIds, sizeof(mediaIdsBuf) - 1);
                                                    mediaIdsBuf[sizeof(mediaIdsBuf) - 1] = '\0';
                                                    params.mediaIds[mcount++] = mediaIdsBuf;
                                                    for (p = mediaIdsBuf; *p; p++) {
                                                        if (*p == ',') {
                                                            *p = '\0';
                                                            if (mcount < FS3ETOOT_MAX_MEDIA)
                                                                params.mediaIds[mcount++] = p + 1;
                                                        }
                                                    }
                                                }
                                                params.mediaCount = mcount;

                                                FS3ETootView_SetComposeContext(&app->tootView,
                                                    FS3ETOOT_KIND_MODIFY, &params);
                                                FS3ETootView_Open(&app->tootView);
                                            }
                                            break;

                                        case TTL_HOT_DELETE:
                                            /* hotSpotId is the status to
                                             * delete -- confirm first,
                                             * Mastodon offers no undo.
                                             * "Delete|Cancel": leftmost
                                             * gadget (Delete) returns
                                             * non-zero, rightmost/Escape
                                             * (Cancel) returns 0, standard
                                             * AmigaOS EasyRequest
                                             * convention. */
                                            if (hotSpotId && hotSpotId[0]) {
                                                struct EasyStruct es = {
                                                    sizeof(struct EasyStruct), 0,
                                                    (UBYTE *)"FriendSh3ep - Delete Toot",
                                                    (UBYTE *)"Delete this toot?\nThis cannot be undone.",
                                                    (UBYTE *)"Delete|Cancel"
                                                };
                                                if (EasyRequestArgs(CurrentMainWindow, &es, NULL, NULL)) {
                                                    FS3ENetDeleteStatusReq *req =
                                                        FS3ENetDeleteStatusReq_Alloc(
                                                            app->accountApiBaseUrl,
                                                            app->accountAccessToken,
                                                            hotSpotId);
                                                    FS3EApp_NetSend(FS3ENETQ_DELETE_STATUS, req, sizeof(*req));
                                                }
                                            }
                                            break;

                                        case TTL_HOT_REPLY:
                                            /* hotSpotId is the status being
                                             * replied to, hotSpotString its
                                             * original author's acct (see
                                             * ttl_toot_build_hotspots's data
                                             * for TTL_HOT_REPLY) -- handed
                                             * to the compose window so it
                                             * opens in Reply mode with the
                                             * title/mention-prefix filled
                                             * in with no separate lookup. */
                                            {
                                                FS3ETootComposeParams params;
                                                memset(&params, 0, sizeof(params));
                                                params.postId = hotSpotId;
                                                params.acct   = hotSpotString;
                                                FS3ETootView_SetComposeContext(&app->tootView,
                                                    FS3ETOOT_KIND_REPLY, &params);
                                                FS3ETootView_Open(&app->tootView);
                                            }
                                            break;

                                        /* TTL_HOT_BOOST: left for after
                                         * toot composing/editing is in
                                         * place (see todo.txt). */

                                        default:
                                            break;
                                    }
                                }


                            }

                            /* TootTimeline's own internal mouse-drag
                             * scroll just moved scrollY (see
                             * TTIMELINE_ScrollStarted's doc comment in
                             * fs3etoottimeline.h) -- a manual scroll
                             * always wins over the "Next toot" animation
                             * still in flight from a previous Space
                             * press, same reasoning the arrow keys below
                             * apply. */
                            ptag = FindTagItem(TTIMELINE_ScrollStarted, msg);
                            if (ptag)
                                Action_TimelineStopScrollAnimation();

                            break;

                        default:
                            break;
                    }
                } // end while boopsimessage

            } // end if has boopsimessage

            if((refreshFlags & reflags_searchworduted) && app->searchWordEditor && CurrentMainWindow
             && app->viewMode == VIEWMODE_Search)
            {
                RefreshGList((struct Gadget *)app->searchWordEditor,
                             CurrentMainWindow, NULL, 1);
            }

            if ((refreshFlags & reflags_bodyEditor) && app->tootView.window)
                RefreshGList((struct Gadget *)app->tootView.bodyEditor,
                             app->tootView.window, NULL, 1);

            if ((refreshFlags & reflags_tootTimeLine) && CurrentMainWindow && app->tootTimeline )
                RefreshGList((struct Gadget *)app->tootTimeline,
                             CurrentMainWindow, NULL, 1);

            /* One redraw for however many of the 8 nav_btns[] reported
             * UBGBM_RefreshNeeded in this drain pass -- see the
             * GID_NAV_USER..GID_NAV_NEWS case above. */
            if ((refreshFlags & reflags_navBtns) && CurrentMainWindow )
            {
                int i;
                for(i=0;i<8;i++)
                {
                    RefreshGList((struct Gadget *)app->nav_btns[i],
                             CurrentMainWindow, NULL, 1);
                }
            }


            // test, doesnt work:
            // if(refreshTitleBarLayout  && CurrentMainWindow)
            // {
            //     RefreshGList((struct Gadget *)app->titleBarLayout,
            //                  CurrentMainWindow, NULL, 1);
            //     refreshTitleBarLayout = 0;
            // }
            // test trick

            if(delayApplyFontSettings)
            {
                FS3EApp_ApplyFontSettings_Delayed();
                /* Recompute minimum gadget sizes and relayout the whole window */
                DoMethod(app->window_obj, WM_RETHINK);
            }

            if(delayLoadTheme)
            {
                /* does its own WM_RETHINK internally -- see its doc comment */
                FS3EApp_LoadTheme_Delayed();
            }
        }  // ed while events
    }// end paragraph

    return 0;
}

/* - - - - - - - - - - - - - - - - - CLEANUP - - - - - - - - - - - - - - - - */

void exitclose(void)
{
    if (SIPCPort)
    {
        RemPort(SIPCPort);
        DeleteMsgPort(SIPCPort);
        SIPCPort = NULL;
    }
    if (app)
    {

        FS3ELoginView_Dispose(&app->loginView);
        FS3ETootView_Dispose(&app->tootView);
        FS3EThemeView_Dispose(&app->themeView);
        FS3ESettingsView_Dispose(&app->settingsView);
        FS3EEmojiBoxWindow_Dispose(&app->emojiBoxWindow);
        FS3EMediaView_Dispose(&app->mediaView);
        if (app->window_obj)
        {
            FS3EMenu_Close(&app->menu, CurrentMainWindow);
            FS3ESettings_Save(&app->settings);
            FS3EMain_Close(&app->mainwindow, app->window_obj, 0);
            /* Cascades: mainlayout → titleBarLayout/navBarLayout/placeholder
             * → all UniButtonP9 children. */
            DisposeObject(app->window_obj);
        }
        FS3ETimer_Exit();
        /* Free private classes AFTER all objects using them are disposed. */
        TootTimeline_Exit();
        SearchBarLayout_Exit();
        NavBarLayout_Exit();
        TitleBarLayout_Exit();
        UniButtonP9_Exit();
        UniButtonBGBM_Exit();
        /* Release shared DCs after all gadgets using them are disposed. */
        if (app->buttonDC) { URPDC_Release(app->buttonDC); app->buttonDC = NULL; }

        if (app->avatarImages) {
            AvatarImages_Dispose(app->avatarImages);
            app->avatarImages = NULL;
        }

        FS3EStyle_ReleaseDrawContexts(&app->style);
        FS3EStyle_FreeThemeImages(&app->style);

wait2sec();
        if (app->netRequestPort)
        {
            /* Always a fresh, dedicated port for the shutdown handshake --
             * never app->netReplyPort. That port can still have ordinary
             * async replies (e.g. FETCH_IMAGE) queued ahead of the
             * shutdown ack (now routine: real RAM:T downloads + rescales
             * take real time, unlike the near-instant failures before
             * that bug fix), and FS3ENet_Stop's WaitPort/GetMsg blindly
             * takes whatever's at the head of the queue. Grabbing the
             * wrong message here means: (a) that reply leaks (never
             * freed), and (b) we wrongly believe the process has stopped
             * and tear down netReplyPort while the still-running process
             * later tries to ReplyMsg() the real shutdown message back to
             * a port that no longer exists -- the crash. A port only this
             * handshake ever touches makes the ambiguity impossible. */
            struct MsgPort *stopReplyPort = CreateMsgPort();
            if (stopReplyPort) {
wait2sec();
                FS3ENet_Stop(app->netRequestPort, stopReplyPort);
wait2sec();
                DeleteMsgPort(stopReplyPort);
            }
        }
wait2sec();
        /* Drain and free any remaining async replies -- safe now: the
         * network process only replies to the dedicated port above once
         * every earlier request already sitting in its queue has been
         * fully processed and replied to netReplyPort (it handles
         * messages strictly in arrival order), so nothing further can
         * land here after this point. */
        if (app->netReplyPort) {
            FS3ENetMessage *netMsg;
            while ((netMsg = (FS3ENetMessage *)GetMsg(app->netReplyPort)) != NULL) {
                FreeVec(netMsg->fs3em_Data);
                FreeVec(netMsg);
            }
            DeleteMsgPort(app->netReplyPort);
            app->netReplyPort = NULL;
        }
wait2sec();
        if (app->thumbRequestPort)
        {
            /* Same dedicated-port reasoning as netRequestPort above. */
            struct MsgPort *stopReplyPort = CreateMsgPort();
            if (stopReplyPort) {
wait2sec();
                FS3EThumb_Stop(app->thumbRequestPort, stopReplyPort);
wait2sec();
                DeleteMsgPort(stopReplyPort);
            }
        }
wait2sec();
        /* Drain and free any remaining async replies */
        if (app->thumbReplyPort) {
            FS3EThumbMessage *thumbMsg;
            while ((thumbMsg = (FS3EThumbMessage *)GetMsg(app->thumbReplyPort)) != NULL) {
                FreeVec(thumbMsg);
            }
            DeleteMsgPort(app->thumbReplyPort);
            app->thumbReplyPort = NULL;
        }
wait2sec();
        if (app->audioRequestPort)
        {
            /* Same dedicated-port reasoning as netRequestPort/thumbRequestPort
             * above -- FS3EAudio_Stop() also stops any in-progress playback
             * before the process actually exits (see FS3EAudio_ProcEntry's
             * FS3EAUDIOQ_SHUTDOWN case). */
            struct MsgPort *stopReplyPort = CreateMsgPort();
            if (stopReplyPort) {
wait2sec();
                FS3EAudio_Stop(app->audioRequestPort, stopReplyPort);
wait2sec();
                DeleteMsgPort(stopReplyPort);
            }
        }
wait2sec();
        /* Drain and free any remaining async replies */
        if (app->audioReplyPort) {
            FS3EAudioMessage *audioMsg;
            while ((audioMsg = (FS3EAudioMessage *)GetMsg(app->audioReplyPort)) != NULL) {
                FreeVec(audioMsg);
            }
            DeleteMsgPort(app->audioReplyPort);
            app->audioReplyPort = NULL;
        }
wait2sec();
        FS3EApp_FreeLoginState();
        FS3EApp_FreeAccount();
        if (app->searchProfileAcct)      { FreeVec(app->searchProfileAcct);      app->searchProfileAcct      = NULL; }
        if (app->searchProfileAccountId) { FreeVec(app->searchProfileAccountId); app->searchProfileAccountId = NULL; }
        if (app->searchDiscussionStatusId) { FreeVec(app->searchDiscussionStatusId); app->searchDiscussionStatusId = NULL; }
        if (app->searchLastQueryText)    { FreeVec(app->searchLastQueryText);    app->searchLastQueryText    = NULL; }
        FS3EApp_SearchStackClear();

wait2sec();
        FS3EMsg_Close();
wait2sec();
        if (app->app_port)
            DeleteMsgPort(app->app_port);

        FS3ESettings_Close(&app->settings);
        FreeVec(app);
        app = NULL;
    }
wait2sec();
    FS3ELocale_Close();
    if (LocaleBase) {
        CloseLibrary((struct Library *)LocaleBase);
        LocaleBase = NULL;
    }

    if (BevelBase)  { CloseLibrary(BevelBase);  BevelBase  = NULL; }
    if (BitMapBase) { CloseLibrary(BitMapBase); BitMapBase = NULL; }

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
