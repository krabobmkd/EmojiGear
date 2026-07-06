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

#include <proto/string.h>
#include <gadgets/string.h>

#include <proto/texteditor.h>
#include <gadgets/texteditor.h>

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
#include "UniButtonBGBM/unibuttonbgbm.h"
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
struct Library *StringBase         = NULL;
struct Library *TextFieldBase      = NULL;
struct Library *LabelBase          = NULL;
struct Library *CheckboxBase       = NULL;
struct Library *ChooserBase        = NULL;
struct Library *GetFileBase        = NULL;
struct Library *IntegerBase        = NULL;
struct Library *UniTextEditorBase  = NULL;
struct Library *UniButtonBase      = NULL;

/* utf8rastport.library – required by UniButtonP9 (private UniButton class) */
struct Library *URPBase  = NULL;
/* datatypes.library v44 – used by bmimage.c for picture.datatype image loading */
struct Library *DataTypesBase = NULL;
/* images/bevel.image – optional, used by UniButton bevel frames */
struct Library *BevelBase = NULL;
/* images/bitmap.image – optional, used by fs3estyle.c for themed title bar
 * button images (see FS3EStyle_LoadThemeImages) */
struct Library *BitMapBase = NULL;


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
    {"gadgets/texteditor.gadget",   42, &TextFieldBase},
    {"images/label.image",          42, &LabelBase},
    {"gadgets/checkbox.gadget",      42, &CheckboxBase},
    {"gadgets/chooser.gadget",       44, &ChooserBase},
    {"gadgets/getfile.gadget",       42, &GetFileBase},
    {"gadgets/integer.gadget",       44, &IntegerBase},
    {"gadgets/unitexteditor.gadget",  4, &UniTextEditorBase},
    {"gadgets/unibutton.gadget",     4, &UniButtonBase},
    {"utf8rastport.library",         4, &URPBase},
    {"datatypes.library",           44, &DataTypesBase},
    {NULL, 0, NULL}
};

struct App *app = NULL;

//extern int refreshTitleBarLayout;

/* Default DPI factor: 1 row = 14 pixels. */
#define DEFAULT_DPI_HEIGHT 14

void exitclose(void);

void cleanexit(const char *pmessage)
{
    if (pmessage) printf("%s\n", pmessage);
    exit(0);
}

/* - - - - - - - - - - - - - - - - NETWORK HELPERS - - - - - - - - - - - - - */

static char *NetStrDup(const char *s)
{
    ULONG len;
    char *copy;
    if (!s || !s[0]) return NULL;
    len  = (ULONG)strlen(s) + 1;
    copy = (char *)AllocVec(len, MEMF_ANY);
    if (copy) CopyMem(s, copy, len);
    return copy;
}

/* Send a pre-allocated request block to the network process asynchronously.
 * On failure, frees data and returns FALSE. */
static BOOL FS3EApp_NetSend(ULONG type, APTR data, ULONG dataLen)
{
    FS3ENetMessage *msg;
    if (!app->netRequestPort || !app->netReplyPort || !data) {
        if (data) FreeVec(data);
        return FALSE;
    }
    msg = (FS3ENetMessage *)AllocVec(sizeof(FS3ENetMessage), MEMF_CLEAR);
    if (!msg) { FreeVec(data); return FALSE; }
    msg->fs3em_Msg.mn_Length   = sizeof(*msg);
    msg->fs3em_Msg.mn_ReplyPort = app->netReplyPort;
    msg->fs3em_Type    = type;
    msg->fs3em_Data    = data;
    msg->fs3em_DataLen = dataLen;
    PutMsg(app->netRequestPort, &msg->fs3em_Msg);
    return TRUE;
}

/* Free login interim state (called on login error or completion). */
static void FS3EApp_FreeLoginState(void)
{
    if (app->loginApiBaseUrl)   { FreeVec(app->loginApiBaseUrl);   app->loginApiBaseUrl   = NULL; }
    if (app->loginClientId)     { FreeVec(app->loginClientId);     app->loginClientId     = NULL; }
    if (app->loginClientSecret) { FreeVec(app->loginClientSecret); app->loginClientSecret = NULL; }
    app->loginPhase = FS3ELOGIN_IDLE;
}

/* Free stored account credentials. */
static void FS3EApp_FreeAccount(void)
{
    if (app->accountApiBaseUrl)  { FreeVec(app->accountApiBaseUrl);  app->accountApiBaseUrl  = NULL; }
    if (app->accountAccessToken) { FreeVec(app->accountAccessToken); app->accountAccessToken = NULL; }
    if (app->accountDisplayName) { FreeVec(app->accountDisplayName); app->accountDisplayName = NULL; }
    if (app->accountAcct)        { FreeVec(app->accountAcct);        app->accountAcct        = NULL; }
    if (app->accountAvatarURL)   { FreeVec(app->accountAvatarURL);   app->accountAvatarURL   = NULL; }
}

/* Store account credentials from a LOGIN_FINISH reply. */
static void FS3EApp_SetAccount(const char *apiBaseUrl, const char *accessToken,
                               const char *displayName, const char *acct,
                               const char *avatarURL)
{
    FS3EApp_FreeAccount();
    app->accountApiBaseUrl  = NetStrDup(apiBaseUrl);
    app->accountAccessToken = NetStrDup(accessToken);
    app->accountDisplayName = NetStrDup(displayName);
    app->accountAcct        = NetStrDup(acct);
    app->accountAvatarURL   = NetStrDup(avatarURL);
}

/* Save credentials to PROGDIR:account.dat (5 lines). */
static void FS3EApp_SaveAccount(void)
{
    BPTR f;
    if (!app->accountApiBaseUrl || !app->accountAccessToken) return;
    f = Open("PROGDIR:account.dat", MODE_NEWFILE);
    if (!f) return;
    FPuts(f, app->accountApiBaseUrl);                              FPuts(f, "\n");
    FPuts(f, app->accountAccessToken);                             FPuts(f, "\n");
    FPuts(f, app->accountDisplayName ? app->accountDisplayName : ""); FPuts(f, "\n");
    FPuts(f, app->accountAcct        ? app->accountAcct        : ""); FPuts(f, "\n");
    FPuts(f, app->accountAvatarURL   ? app->accountAvatarURL   : ""); FPuts(f, "\n");
    Close(f);
    printf("FS3EApp_SaveAccount: saved %s @ %s\n",
           app->accountAcct ? app->accountAcct : "?",
           app->accountApiBaseUrl);
}

/* Load credentials from PROGDIR:account.dat. Returns TRUE if valid. */
static BOOL FS3EApp_LoadAccount(void)
{
    BPTR f;
    char apiBaseUrl[256], accessToken[512];
    char displayName[128], acct[128], avatarURL[512];
    ULONG n;

    f = Open("PROGDIR:account.dat", MODE_OLDFILE);
    if (!f) return FALSE;

    /* FGets includes the trailing '\n' — strip it. */
#define RLINE(buf) \
    (FGets(f, buf, sizeof(buf)) && (buf[0] != '\0') && \
     ((n = strlen(buf)) > 0) && (buf[n-1] == '\n' ? (buf[n-1] = '\0', 1) : 1))

    if (!RLINE(apiBaseUrl) || !RLINE(accessToken) || !apiBaseUrl[0] || !accessToken[0]) {
        Close(f);
        printf("FS3EApp_LoadAccount: no valid saved account\n");
        return FALSE;
    }
    if (!RLINE(displayName)) displayName[0] = '\0';
    if (!RLINE(acct))        acct[0]        = '\0';
    if (!RLINE(avatarURL))   avatarURL[0]   = '\0';
#undef RLINE

    Close(f);

    printf("FS3EApp_LoadAccount: loaded %s @ %s\n", acct, apiBaseUrl);
    FS3EApp_SetAccount(apiBaseUrl, accessToken, displayName, acct, avatarURL);
    app->loginPhase = FS3ELOGIN_DONE;
    return TRUE;
}

/* Timeline name for a given VIEWMODE_* value; NULL = no standard timeline. */
static const char *ViewModeTimeline(ULONG viewMode)
{
    switch (viewMode) {
        case VIEWMODE_Home:  return "home?limit=20";
        case VIEWMODE_Local: return "public?local=true&limit=20";
        case VIEWMODE_Fed:   return "public?limit=20";
        default:             return NULL;
    }
}

/* Ensure server URL has a scheme and no trailing slash.
 * "mastoot.fr" → "https://mastoot.fr"
 * "https://mastoot.fr/" → "https://mastoot.fr" */
static const char *NormalizeServerUrl(const char *server, char *buf, ULONG bufSize)
{
    ULONG len;
    if (!server || !server[0]) return server;
    if (strncmp(server, "http://", 7) == 0 || strncmp(server, "https://", 8) == 0)
        strncpy(buf, server, bufSize - 1);
    else
        snprintf(buf, bufSize, "https://%s", server);
    buf[bufSize - 1] = '\0';
    len = (ULONG)strlen(buf);
    while (len > 0 && buf[len - 1] == '/') buf[--len] = '\0';
    return buf;
}

/* Update TTIMELINE_WaitText to reflect current connection / login state.
 * Called whenever the state machine advances so the empty-channel placeholder
 * always shows a meaningful message. */
static void FS3EApp_CheckConnectionState(void)
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
        if (!app->accountAccessToken) {
            text = "No account.\n"
                   "Open the Login window to connect.";
        } else {
            bit = (1UL << app->viewMode);
            if (app->timelineErrorMask & bit) {
                switch (app->lastTimelineResult) {
                    case FS3ENETR_NETWORK_ERROR:
                        text = "No internet connection.";  break;
                    case FS3ENETR_HTTP_ERROR:
                        text = "Server error.";            break;
                    case FS3ENETR_AUTH_ERROR:
                        text = "Authentication failed.";   break;
                    default:
                        text = "Connection error.";        break;
                }
            } else if (app->timelineFetchedMask & bit) {
                text = "Updating...";
            } else {
                text = "Account connected.";
            }
        }
    }

    SetGdAttrs(app->tootTimeline, TTIMELINE_WaitText, (ULONG)text, TAG_END);
}

/* Send an async TIMELINE request for viewMode if credentials are available
 * and a fetch hasn't already been started for that channel. */
static void FS3EApp_FetchTimeline(ULONG viewMode)
{
    const char *tl;
    FS3ENetTimelineReq *req;
    ULONG bit = (1UL << viewMode);

    if (!app->accountApiBaseUrl || !app->accountAccessToken) return;
    if (app->timelineFetchedMask & bit) return;
    tl = ViewModeTimeline(viewMode);
    if (!tl) return;

    printf("FS3EApp_FetchTimeline: viewMode=%u timeline=%s\n", (unsigned)viewMode, tl);

    req = FS3ENetTimelineReq_Alloc(viewMode,
              app->accountApiBaseUrl, app->accountAccessToken,
              tl, NULL);
    if (!req) return;

    if (FS3EApp_NetSend(FS3ENETQ_TIMELINE, req,
            sizeof(FS3ENetTimelineReq) /* net process only reads char* fields */)) {
        app->timelineFetchedMask |= bit;
        app->timelineErrorMask   &= ~bit; /* clear any previous error for this channel */
        FS3EApp_CheckConnectionState();
    }
}

/* Visibility index (from FS3ETootView) → Mastodon API string. */
static const char *VisibilityString(LONG idx)
{
    static const char *const s[] = { "public", "unlisted", "private", "direct" };
    if (idx < 0 || idx > 3) return "public";
    return s[(ULONG)idx];
}

/* Handle one reply message from the network process. */
static void FS3EApp_HandleNetReply(FS3ENetMessage *msg)
{
    switch (msg->fs3em_Type)
    {
    case FS3ENETQ_LOGIN_START:
        if (msg->fs3em_Result == FS3ENETR_OK) {
            FS3ENetLoginStartReply *reply = (FS3ENetLoginStartReply *)msg->fs3em_Data;
            /* Save client credentials for phase 2 */
            if (app->loginClientId)     FreeVec(app->loginClientId);
            if (app->loginClientSecret) FreeVec(app->loginClientSecret);
            app->loginClientId     = NetStrDup(reply->fs3enl_ClientId);
            app->loginClientSecret = NetStrDup(reply->fs3enl_ClientSecret);
            app->loginPhase = FS3ELOGIN_WAITING_CODE;

            printf("login reply: LOGIN_START ok, clientId=%s url=%s\n",
                   reply->fs3enl_ClientId, reply->fs3enl_AuthorizeUrl);

            /* Show the URL in the login window and enable phase-2 widgets. */
            FS3ELoginView_SetAuthorizeUrl(&app->loginView, reply->fs3enl_AuthorizeUrl);
        } else {
            printf("login reply: LOGIN_START FAILED result=%u\n", (unsigned)msg->fs3em_Result);
            FS3EApp_FreeLoginState();
        }
        break;

    case FS3ENETQ_LOGIN_FINISH:
        if (msg->fs3em_Result == FS3ENETR_OK) {
            FS3ENetLoginFinishReply *reply = (FS3ENetLoginFinishReply *)msg->fs3em_Data;
            printf("login reply: LOGIN_FINISH ok, acct=%s displayName=%s\n",
                   reply->fs3enl_Account.fma_Acct    ? reply->fs3enl_Account.fma_Acct    : "?",
                   reply->fs3enl_Account.fma_DisplayName ? reply->fs3enl_Account.fma_DisplayName : "?");
            FS3EApp_SetAccount(app->loginApiBaseUrl,
                               reply->fs3enl_AccessToken,
                               reply->fs3enl_Account.fma_DisplayName,
                               reply->fs3enl_Account.fma_Acct,
                               reply->fs3enl_Account.fma_AvatarURL);
            FS3EApp_SaveAccount();
            FS3EApp_FreeLoginState();
            app->loginPhase = FS3ELOGIN_DONE;
            /* Fetch the current view mode timeline */
            app->timelineFetchedMask = 0; /* reset so new account fetches fresh */
            FS3EApp_FetchTimeline(app->viewMode);
            FS3ELoginView_Close(&app->loginView);
        } else {
            struct EasyStruct es = {
                sizeof(struct EasyStruct), 0,
                (UBYTE *)"FriendSh3ep - Login Error",
                (UBYTE *)"Could not exchange the authorization code.\nCheck the code and try again.",
                (UBYTE *)"OK"
            };
            printf("login reply: LOGIN_FINISH FAILED result=%u\n", (unsigned)msg->fs3em_Result);
            EasyRequestArgs(CurrentMainWindow, &es, NULL, NULL);
            app->loginPhase = FS3ELOGIN_WAITING_CODE; /* let user retry code */
        }
        break;

    case FS3ENETQ_TIMELINE:
        if (msg->fs3em_Result == FS3ENETR_OK && app->tootTimeline) {
            FS3ENetTimelineReply *reply = (FS3ENetTimelineReply *)msg->fs3em_Data;
            FS3ENetStatus *statuses = (FS3ENetStatus *)(reply + 1);
            ULONG i;
            printf("timeline reply: viewMode=%u count=%u\n",
                   (unsigned)reply->fs3et_ViewModeBit, (unsigned)reply->fs3et_Count);
            /* Fetch is complete — clear the in-flight bit so CheckConnectionState
             * can show the "connected" idle message instead of "Updating…". */
            app->timelineFetchedMask &= ~(1UL << reply->fs3et_ViewModeBit);
            /* Add newest-first (Mastodon returns newest first; prepend oldest first
             * so the newest ends up at top of the TootTimeline channel). */
            for (i = reply->fs3et_Count; i-- > 0; ) {
                TTLPostSetup post;
                post.username    = statuses[i].fmas_DisplayName[0]
                                   ? statuses[i].fmas_DisplayName
                                   : statuses[i].fmas_Acct;
                post.acct        = statuses[i].fmas_Acct;
                post.body        = statuses[i].fmas_Content;
                post.timestamp   = statuses[i].fmas_CreatedAt;
                post.boostBy     = statuses[i].fmas_BoostBy;
                post.avatarURL   = statuses[i].fmas_AvatarURL;

                /* Trigger avatar download for this user if not already requested. */
                if (app->avatarImages &&
                    statuses[i].fmas_AvatarURL &&
                    statuses[i].fmas_AvatarURL[0] &&
                    statuses[i].fmas_Acct &&
                    !AvatarImages_IsRequested(app->avatarImages,
                                              statuses[i].fmas_Acct))
                {
                    ULONG reqSize = sizeof(FS3ENetFetchImageReq)
                                  + strlen(statuses[i].fmas_AvatarURL) + 1
                                  + strlen(statuses[i].fmas_Acct) + 1;
                    FS3ENetFetchImageReq *req =
                        FS3ENetFetchImageReq_Alloc(statuses[i].fmas_AvatarURL,
                                                   statuses[i].fmas_Acct);
                    if (req) {
                        if (FS3EApp_NetSend(FS3ENETQ_FETCH_IMAGE, req, reqSize))
                            AvatarImages_MarkRequested(app->avatarImages,
                                                       statuses[i].fmas_Acct);
                        else
                            FreeVec(req);
                    }
                }
                post.viewModeBits = (1UL << reply->fs3et_ViewModeBit);
                SetAttrs(app->tootTimeline,
                         TTIMELINE_AddPost, (ULONG)&post, TAG_DONE);
            }
            if (CurrentMainWindow)
                RefreshGList((struct Gadget *)app->tootTimeline,
                             CurrentMainWindow, NULL, 1);
        } else if (msg->fs3em_Result != FS3ENETR_OK) {
            FS3ENetTimelineReq *req = (FS3ENetTimelineReq *)msg->fs3em_Data;
            ULONG bit = req ? (1UL << req->fs3et_ViewModeBit) : 0;
            app->timelineErrorMask   |= bit;
            app->timelineFetchedMask &= ~bit; /* allow retry on next view switch */
            app->lastTimelineResult   = msg->fs3em_Result;
            printf("timeline reply: FAILED viewMode=%u result=%u\n",
                   req ? (unsigned)req->fs3et_ViewModeBit : 0,
                   (unsigned)msg->fs3em_Result);
        }
        break;

    case FS3ENETQ_FETCH_IMAGE:
        if (msg->fs3em_Result == FS3ENETR_OK && app->avatarImages) {
            FS3ENetFetchImageReply *reply = (FS3ENetFetchImageReply *)msg->fs3em_Data;
            if (reply && reply->fs3enf_Key && reply->fs3enf_LocalPath) {
                AvatarImages_GotFile(app->avatarImages, reply->fs3enf_Key,
                                     reply->fs3enf_LocalPath,
                                     CurrentMainScreen,
                                     (UWORD)app->style.avatarSize);
                if (CurrentMainWindow)
                    RefreshGList((struct Gadget *)app->tootTimeline,
                                 CurrentMainWindow, NULL, 1);
            }
        }
        break;

    case FS3ENETQ_POST_STATUS:
        if (msg->fs3em_Result == FS3ENETR_OK) {
            printf("post reply: POST_STATUS ok\n");
            FS3ETootView_Close(&app->tootView);
        } else {
            printf("post reply: POST_STATUS FAILED result=%u\n", (unsigned)msg->fs3em_Result);
        }
        break;

    default:
        break;
    }

    FS3EApp_CheckConnectionState();
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
        RESIZE_P9_BTN(app->titlebar_settingsBtn);
        RESIZE_P9_BTN(app->titlebar_accountBtn);
        RESIZE_P9_BTN(app->titlebar_newtootBtn);
#undef RESIZE_BGBM_BTN
#undef RESIZE_P9_BTN

    }
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
- expect when used just before first window open  */
static void FS3EApp_ApplyFontSettings_Delayed()
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

    /* Reload all cached avatar bitmaps at the new size. */
    if (app->avatarImages && CurrentMainScreen)
        AvatarImages_Reload(app->avatarImages, CurrentMainScreen,
                            (UWORD)app->style.avatarSize);

    delayApplyFontSettings = FALSE;
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

        // UBGBM_TopMargin,        0,
        // UBGBM_BottomMargin,     0,
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
    if(app->viewMode == viewMode) return;
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
    app->viewMode = viewMode;

    /* tell TootTimeline we're to display that channel */
    if (app->tootTimeline)
        SetGdAttrs(app->tootTimeline, TTIMELINE_ViewMode, viewMode, TAG_END);

    /* If logged in and this channel hasn't been fetched yet, start a fetch. */
    FS3EApp_FetchTimeline(viewMode);

    /* Update WaitText for channels that don't trigger a fetch (Search, Notifs, …). */
    FS3EApp_CheckConnectionState();
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

    BevelBase  = OpenLibrary("images/bevel.image",  32); /* optional, no check */
    BitMapBase = OpenLibrary("images/bitmap.image", 44); /* optional, no check */

    LocaleBase = (struct LocaleBase *)OpenLibrary("locale.library", 38);
    FS3ELocale_Init("FriendSh3ep.catalog", 0);
    FS3EAction_Init();

    app = (struct App *)AllocVec(sizeof(struct App), MEMF_CLEAR);
    if (!app) cleanexit("Can't allocate app");

    FS3ESettings_Load(&app->settings);

    if (!FS3EMsg_Init()) cleanexit("Can't create BOOPSI message target");

    /* --- Private BOOPSI classes ---------------------------------------- */
    if (!UniButtonP9_Init())    cleanexit("Can't init UniButtonP9 class");
    if (!UniButtonBGBM_Init())  cleanexit("Can't init UniButtonBGBM class");
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

    app->avatarImages = AvatarImages_Create();

 printf("FS3ENet_Start\n");
    /* --- Network process ------------------------------------------------ */
    app->netReplyPort = CreateMsgPort();
    if (!app->netReplyPort) cleanexit("Can't create network reply port");

    app->netRequestPort = FS3ENet_Start(app->settings.cachePath);
    if (!app->netRequestPort)
        printf("FriendSh3ep: network process failed - continuing without\n");

    /* Try to load saved credentials; timeline fetch fires later in setViewMode */
    FS3EApp_LoadAccount();
 printf("FS3ENet_Start end\n");
 printf("windows creates\n");
    /* --- Classic BOOPSI sub-windows ------------------------------------- */
    if (!FS3ELoginView_Create(&app->loginView, 14))
        cleanexit("Can't create login view");

    /* Pre-fill login view from loaded account so the user sees they're connected. */
    if (app->accountApiBaseUrl && app->loginView.serverEditor)
        SetAttrs(app->loginView.serverEditor,
                 STRINGA_TextVal, (ULONG)app->accountApiBaseUrl, TAG_END);
    if (app->accountAcct && app->loginView.userEditor)
        SetAttrs(app->loginView.userEditor,
                 STRINGA_TextVal, (ULONG)app->accountAcct, TAG_END);

    if (!FS3ETootView_Create(&app->tootView, 14))
        cleanexit("Can't create toot view");

    if (!FS3EThemeView_Create(&app->themeView, LOC(MSG_THEMEV_TITLE)))
        cleanexit("Can't create theme view");

    if (!FS3ESettingsView_Create(&app->settingsView, LOC(MSG_SETTINGSV_TITLE)))
        cleanexit("Can't create settings view");


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
 //printf("p9bm:%08x\n",(int)app->style.bt1Patch9.img.bitmap);
    app->titlebar_settingsBtn = (Object *)NewObject(UniButtonP9Class, NULL,
        GA_ID,                  GID_TITLEBAR_SETTINGS,
        GA_Text,                (ULONG)"\xE2\x9A\x99 Settings",
        UBTP9_URPDrawContext,   (ULONG)app->buttonDC,
        UBTP9_BevelStyle,       BVS_NONE, //BVS_NONE,
        TAG_END);

    app->titlebar_accountBtn = (Object *)NewObject(UniButtonP9Class, NULL,
        GA_ID,                  GID_TITLEBAR_ACCOUNTS,
        GA_Text,                (ULONG)"\xF0\x9F\x91\xA4 Accounts",
        UBTP9_URPDrawContext,   (ULONG)app->buttonDC,
        UBTP9_BevelStyle,       BVS_NONE, //BVS_NONE,
        TAG_END);

    app->titlebar_newtootBtn = (Object *)NewObject(UniButtonP9Class, NULL,
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
        !app->titlebar_altposBtn  || !app->titlebar_depthBtn   ||
        !app->titlebar_userIcon)
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
        /* children in required order (see fs3etitlebar.h) */
        LAYOUT_AddChild, (ULONG)app->titlebar_closeBtn,
        LAYOUT_AddChild, (ULONG)app->titlebar_iconifyBtn,
        LAYOUT_AddChild, (ULONG)app->titlebar_altposBtn,
        LAYOUT_AddChild, (ULONG)app->titlebar_depthBtn,
        LAYOUT_AddChild, (ULONG)app->titlebar_userIcon,

        LAYOUT_AddChild, (ULONG)app->titlebar_settingsBtn,
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

    if (app->avatarImages)
        SetAttrs(app->tootTimeline,
                 TTIMELINE_AvatarImages, (ULONG)app->avatarImages,
                 TAG_DONE);

 flushbdbprint();

    /* (fake test posts removed — real data comes from FS3ENETQ_TIMELINE replies) */
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

    /* synchronize fonts against settings before first layout */
    FS3EApp_ApplyFontSettings_Delayed();

    /* home by default ? */
    fs3e_setViewMode(VIEWMODE_Home);

    flushbdbprint();


/*---*/




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
            ULONG settingsSig = FS3ESettingsView_GetSignalMask(&app->settingsView);

            waitedSignals = winsignal | loginSig | tootSig | themeSig | settingsSig |
                (1L << app->app_port->mp_SigBit) |
                (app->netReplyPort ? (1L << app->netReplyPort->mp_SigBit) : 0) |
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
                        closeExternalViews();
                        if (app->avatarImages)
                            AvatarImages_Unload(app->avatarImages);
                        FS3EMain_Close(&app->mainwindow, app->window_obj, TRUE);
                        break;

                    case WMHI_UNICONIFY:
                        FS3EMain_Show(&app->mainwindow, app->window_obj);
                        if (!CurrentMainWindow) cleanexit("can't re-open window");
                        FS3EMenu_Create(&app->menu, CurrentMainScreen, CurrentMainWindow);
                        if (app->avatarImages && CurrentMainScreen)
                            AvatarImages_Reload(app->avatarImages, CurrentMainScreen,
                                                (UWORD)app->style.avatarSize);
                        break;

                    case WMHI_RAWKEY:
                    {
                        ULONG key = result & 0x07f;
                        ULONG isUp = (result & 0x080);
                        ULONG qualifiers=0;
                        int keyUsed=0;
                        if (key == 0x45) ok = FALSE; /* Escape */

                        GetAttr(WINDOW_Qualifier,app->window_obj,&qualifiers);

                        /*keys F1-F8 are the view mode */
                        if(!isUp && key >=0x50 && key<= 0x57)
                        {
                            fs3e_setViewMode((ULONG)(key-0x50));
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

            /* Drain the BOOPSI notification queue (OM_NOTIFY via ICA_TARGET). */
            if (DelayQueue && BoopsiDelay_HasMessages(DelayQueue))
            {
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
                            closeExternalViews();
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
                            break;
                        case GID_TITLEBAR_NEWTOOT:
                            FS3ETootView_Open(&app->tootView);
                            break;

                        case GID_TITLEBAR_ACCOUNTS:
                            FS3ELoginView_Open(&app->loginView);
                            break;

                        /* ---- Login sub-window: phase 1 ---- */
                        case GID_LOGIN_LOGIN_BUTTON:
                        {
                            char serverBuf[256];
                            const char *server = NormalizeServerUrl(
                                FS3ELoginView_GetANSIServer(&app->loginView),
                                serverBuf, sizeof(serverBuf));
                            /* If already connected, clicking Connect starts a
                             * fresh re-authentication (clears the old account). */
                            if (app->loginPhase == FS3ELOGIN_DONE) {
                                FS3EApp_FreeAccount();
                                app->loginPhase = FS3ELOGIN_IDLE;
                                app->timelineFetchedMask = 0;
                                app->timelineErrorMask   = 0;
                            }
                            if (app->loginPhase == FS3ELOGIN_IDLE && server && server[0]) {
                                FS3ENetLoginStartReq *req = FS3ENetLoginStartReq_Alloc(server);
                                if (req) {
                                    printf("login: phase IDLE, sending LOGIN_START server=%s\n", server);
                                    if (app->loginApiBaseUrl) FreeVec(app->loginApiBaseUrl);
                                    app->loginApiBaseUrl = NetStrDup(server);
                                    if (FS3EApp_NetSend(FS3ENETQ_LOGIN_START, req, sizeof(*req))) {
                                        app->loginPhase = FS3ELOGIN_WAITING_START;
                                        FS3EApp_CheckConnectionState();
                                    }
                                }
                            }
                            break;
                        }

                        /* ---- Login sub-window: phase 2 ---- */
                        case GID_LOGIN_SUBMIT_CODE_BUTTON:
                        {
                            const char *code = FS3ELoginView_GetANSICode(&app->loginView);
                            if (app->loginPhase == FS3ELOGIN_WAITING_CODE &&
                                code && code[0] &&
                                app->loginApiBaseUrl &&
                                app->loginClientId && app->loginClientSecret)
                            {
                                FS3ENetLoginFinishReq *req =
                                    FS3ENetLoginFinishReq_Alloc(
                                        app->loginApiBaseUrl,
                                        app->loginClientId,
                                        app->loginClientSecret,
                                        code);
                                if (req) {
                                    printf("login: phase WAITING_CODE, sending LOGIN_FINISH\n");
                                    if (FS3EApp_NetSend(FS3ENETQ_LOGIN_FINISH, req, sizeof(*req))) {
                                        app->loginPhase = FS3ELOGIN_WAITING_FINISH;
                                        FS3EApp_CheckConnectionState();
                                    }
                                }
                            }
                            break;
                        }

                        /* ---- New toot sub-window ---- */
                        case GID_TOOT_SEND_BUTTON:
                        {
                            const char *subject = FS3ETootView_GetUTF8Subject(&app->tootView);
                            const char *body    = FS3ETootView_GetUTF8Body(&app->tootView);
                            LONG visibility     = FS3ETootView_GetVisibility(&app->tootView);
                            if (body && body[0] && app->accountAccessToken) {
                                FS3ENetPostStatusReq *req =
                                    FS3ENetPostStatusReq_Alloc(
                                        app->accountApiBaseUrl,
                                        app->accountAccessToken,
                                        body,
                                        VisibilityString(visibility),
                                        subject ? subject : "");
                                FS3EApp_NetSend(FS3ENETQ_POST_STATUS, req, sizeof(*req));
                            }
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
                } // end while boopsimessage

            } // end if has boopsimessage

            if ((refreshFlags & reflags_subjectEditor) && app->tootView.window)
                RefreshGList((struct Gadget *)app->tootView.subjectEditor,
                             app->tootView.window, NULL, 1);

            if ((refreshFlags & reflags_bodyEditor) && app->tootView.window)
                RefreshGList((struct Gadget *)app->tootView.bodyEditor,
                             app->tootView.window, NULL, 1);

            if ((refreshFlags & reflags_tootTimeLine) && CurrentMainWindow && app->tootTimeline )
                RefreshGList((struct Gadget *)app->tootTimeline,
                             CurrentMainWindow, NULL, 1);
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
        }  // ed while events
    }// end paragraph

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
        FS3ESettingsView_Dispose(&app->settingsView);
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
        UniButtonBGBM_Exit();
//  printf("exitclose4\n");
// wait2sec();
        /* Release shared DCs after all gadgets using them are disposed. */
        if (app->buttonDC) { URPDC_Release(app->buttonDC); app->buttonDC = NULL; }

        if (app->avatarImages) {
            AvatarImages_Dispose(app->avatarImages);
            app->avatarImages = NULL;
        }

//  printf("exitclose5\n");
// wait2sec();
        FS3EStyle_ReleaseDrawContexts(&app->style);
        FS3EStyle_FreeThemeImages(&app->style);
//  printf("exitclose6\n");
// wait2sec();
        if (BevelBase)  { CloseLibrary(BevelBase);  BevelBase  = NULL; }
        if (BitMapBase) { CloseLibrary(BitMapBase); BitMapBase = NULL; }

        if (app->netRequestPort)
        {
            /* Stop the network process using our persistent reply port if
             * available, otherwise create a temporary one. */
            struct MsgPort *stopReplyPort = app->netReplyPort
                ? app->netReplyPort : CreateMsgPort();
            if (stopReplyPort) {
                FS3ENet_Stop(app->netRequestPort, stopReplyPort);
                if (stopReplyPort != app->netReplyPort)
                    DeleteMsgPort(stopReplyPort);
            }
        }
        /* Drain and free any remaining async replies */
        if (app->netReplyPort) {
            FS3ENetMessage *netMsg;
            while ((netMsg = (FS3ENetMessage *)GetMsg(app->netReplyPort)) != NULL) {
                FreeVec(netMsg->fs3em_Data);
                FreeVec(netMsg);
            }
            DeleteMsgPort(app->netReplyPort);
            app->netReplyPort = NULL;
        }

        FS3EApp_FreeLoginState();
        FS3EApp_FreeAccount();

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
