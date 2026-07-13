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
#include "fs3esettings.h"
#include "fs3emachineid.h"

#include "UniButtonP9/unibuttonp9.h"
#include "UniButtonBGBM/unibuttonbgbm.h"
#include "TitleBarLayout/fs3etitlebar.h"
#include "NavBarLayout/fs3enavbar.h"
#include "SearchBarLayout/fs3esearchbar.h"
#include "TootTimeline/fs3etoottimeline.h"

#include "network_fs3e/fs3enet.h"
#include "fs3ethumb.h"

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
 * On failure, frees data and returns FALSE.
 *
 * PutMsg() enqueues via Exec's Enqueue(), which is priority-ordered (FIFO
 * within equal priority) -- so a bulk FETCH_IMAGE backlog (one avatar plus
 * up to TTL_POST_MAX_MEDIA thumbnails per status, times a whole timeline
 * page) doesn't make an interactive request like switching timelines wait
 * behind dozens of queued downloads: FETCH_IMAGE goes in at a lower
 * priority than everything else, so a fresh TIMELINE/LOGIN/POST_STATUS
 * request cuts to the front of that backlog instead of queuing after it.
 *
 * Not static: fs3eaction.c's toot actions (e.g. Action_ToggleFavorite)
 * call this directly too -- see the extern declaration there. */
BOOL FS3EApp_NetSend(ULONG type, APTR data, ULONG dataLen)
{
    FS3ENetMessage *msg;
    if (!app->netRequestPort || !app->netReplyPort || !data) {
        if (data) FreeVec(data);
        return FALSE;
    }
    msg = (FS3ENetMessage *)AllocVec(sizeof(FS3ENetMessage), MEMF_CLEAR | MEMF_PUBLIC );
    if (!msg) { FreeVec(data); return FALSE; }
    msg->fs3em_Msg.mn_Length   = sizeof(*msg);
    msg->fs3em_Msg.mn_ReplyPort = app->netReplyPort;
    msg->fs3em_Msg.mn_Node.ln_Pri = (type == FS3ENETQ_FETCH_IMAGE) ? -5 : 0;

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
    if (app->accountId)          { FreeVec(app->accountId);          app->accountId          = NULL; }
}

/* Store account credentials from a LOGIN_FINISH reply. */
static void FS3EApp_SetAccount(const char *apiBaseUrl, const char *accessToken,
                               const char *displayName, const char *acct,
                               const char *avatarURL, const char *accountId)
{
    /* Only a genuine account change bumps accountGeneration -- a same-
     * account re-confirm (e.g. FS3ENETQ_VERIFY_ACCOUNT's backfill, which
     * calls this with the same apiBaseUrl+acct just to refresh
     * displayName/avatarURL/accountId) must NOT invalidate a TIMELINE
     * request already in flight for this very account, or its reply would
     * be wrongly discarded as stale and leave timelineFetchedMask's bit
     * permanently stuck set with no content ever applied. */
    BOOL sameAccount = app->accountApiBaseUrl && app->accountAcct &&
                        apiBaseUrl && acct &&
                        !strcmp(app->accountApiBaseUrl, apiBaseUrl) &&
                        !strcmp(app->accountAcct, acct);

    FS3EApp_FreeAccount();
    app->accountApiBaseUrl  = NetStrDup(apiBaseUrl);
    app->accountAccessToken = NetStrDup(accessToken);
    app->accountDisplayName = NetStrDup(displayName);
    app->accountAcct        = NetStrDup(acct);
    app->accountAvatarURL   = NetStrDup(avatarURL);
    app->accountId          = NetStrDup(accountId);

    /* Toot button has nothing to post to without a connected account --
     * safe to call before the toot window exists yet (checked inside). */
    FS3ETootView_UpdateSendEnabled(&app->tootView);

    /* The active account just changed -- any FS3ENETQ_TIMELINE request
     * still in flight from before this point now belongs to a stale
     * generation; see accountGeneration's comment in friendsh3ep.h. */
    if (!sameAccount) {
        app->accountGeneration++;

        /* A different server may have a different per-toot character
         * limit (or none confirmed at all yet) -- the old value belongs
         * to the account being left, so it must not keep showing as if
         * confirmed for this one. Refresh once per real account change
         * (an instance's limit essentially never changes mid-session, so
         * there's no reason to re-ask on every same-account re-confirm).
         * FS3ETootView_UpdateCharCount reflects the reset immediately
         * ("Max: -") rather than lagging until the async reply arrives. */
        app->accountMaxChars = 0;
        FS3ETootView_UpdateCharCount(&app->tootView);

        if (app->accountApiBaseUrl) {
            FS3ENetInstanceInfoReq *iiReq =
                FS3ENetInstanceInfoReq_Alloc(app->accountApiBaseUrl);
            if (iiReq)
                FS3EApp_NetSend(FS3ENETQ_INSTANCE_INFO, iiReq, sizeof(*iiReq));
        }
    }

    /* Tell the title bar which account to draw the row-2 icon for -- see
     * TBLAYOUT_AccountAcct. Safe to call before titleBarLayout exists
     * (SetAttrs on a NULL object is a no-op) -- FS3EApp_LoadAccount()
     * runs before window creation, which passes app->accountAcct again
     * as an initial NewObject tag at that point (see main()). A redraw
     * right away shows the (likely blank/placeholder) icon promptly on a
     * fresh login or re-login rather than waiting for some unrelated
     * event to repaint the title bar; FS3EApp_UpdateUserIcon() covers the
     * separate "the avatar bitmap itself just finished loading" redraw. */
    if (app->titleBarLayout) {
        SetAttrs(app->titleBarLayout, TBLAYOUT_AccountAcct,
                 (ULONG)app->accountAcct, TAG_DONE);
        if (CurrentMainWindow)
            RefreshGList((struct Gadget *)app->titleBarLayout, CurrentMainWindow, NULL, 1);
    }

    /* Fetch our own avatar the same way timeline posts do (see
     * FS3ENETQ_FETCH_IMAGE handling in FS3EApp_HandleNetReply) --
     * AvatarImages is keyed by acct, so this reuses the exact same cache
     * entry/scale pipeline; FS3EApp_UpdateUserIcon() picks up the result
     * once the reply arrives. (Previously disabled: the trashed/garbage
     * image data this used to show was the titlebar_userIcon
     * button.gadget's images/bitmap.image wrapper going stale, not this
     * fetch -- see TitleBarLayout_OnRender, which now draws the avatar
     * directly instead.) */
    if (app->avatarImages && app->accountAcct &&
        app->accountAvatarURL && app->accountAvatarURL[0] &&
        !AvatarImages_IsRequested(app->avatarImages, app->accountAcct))
    {
        ULONG reqSize = sizeof(FS3ENetFetchImageReq)
                      + strlen(app->accountAvatarURL) + 1
                      + strlen(app->accountAcct) + 1
                      + strlen(FS3E_CACHE_SUBDIR_USERICONS) + 1;
        FS3ENetFetchImageReq *req =
            FS3ENetFetchImageReq_Alloc(app->accountAvatarURL, app->accountAcct,
                                       FS3E_CACHE_SUBDIR_USERICONS,
                                       (BOOL)app->settings.keepBigUserIcons);
        if (req) {
            if (FS3EApp_NetSend(FS3ENETQ_FETCH_IMAGE, req, reqSize))
                AvatarImages_MarkRequested(app->avatarImages, app->accountAcct);
            else
                FreeVec(req);
        }
    }
}

/* The connected account's avatar (row 2 of the title bar) just became
 * available or changed -- TitleBarLayout_OnRender reads it fresh from
 * AvatarImages_Get() on every render (no cached bitmap/wrapper object of
 * its own to go stale), so all that's needed here is asking for a
 * redraw. */
static void FS3EApp_UpdateUserIcon(void)
{
    if (CurrentMainWindow && app->titleBarLayout)
        RefreshGList((struct Gadget *)app->titleBarLayout, CurrentMainWindow, NULL, 1);
}

/* -------------------------------------------------------------------------
 * Recursive directory creation (AmigaOS mkdir -p equivalent). Mirrors
 * FS3ECache_MakeDir() in network_fs3e/fs3enet_cache.c -- that one is
 * `static` and private to the network process, so it can't be called from
 * here; same AmigaOS path-splitting rules apply:
 *   "VOL:dir/sub"  -> last '/'  splits "VOL:dir" / "sub"
 *   "VOL:leaf"     -> no '/',   ':' splits volume root "VOL:" / "leaf"
 *   "VOL:"         -> volume/assign root; Lock() tells us if it exists
 * ---------------------------------------------------------------------- */
static BOOL FS3EApp_MakeDirRecursive(const char *path)
{
    BPTR        lock;
    const char *sep;
    char        parent[512];
    ULONG       parentLen;

    lock = Lock(path, SHARED_LOCK);
    if (lock) { UnLock(lock); return TRUE; }

    sep = strrchr(path, '/');
    if (sep) {
        /* Parent is everything before the last '/'. */
        parentLen = (ULONG)(sep - path);
        if (parentLen == 0 || parentLen >= sizeof(parent)) return FALSE;
        memcpy(parent, path, parentLen);
        parent[parentLen] = '\0';
        if (!FS3EApp_MakeDirRecursive(parent)) return FALSE;
    } else {
        /* No slash: path is "VOL:leaf". Parent is the volume/assign root
         * "VOL:" which must already exist (we can't create a volume). */
        sep = strchr(path, ':');
        if (!sep) return FALSE;  /* relative path with no drive — refuse */
        parentLen = (ULONG)(sep - path + 1);   /* include ':' */
        if (parentLen >= sizeof(parent)) return FALSE;
        memcpy(parent, path, parentLen);
        parent[parentLen] = '\0';
        lock = Lock(parent, SHARED_LOCK);
        if (!lock) return FALSE;   /* volume/assign offline */
        UnLock(lock);
    }

    lock = CreateDir(path);
    if (!lock) {
        /* Another process may have created it between our Lock check and
         * CreateDir — verify before reporting failure. */
        lock = Lock(path, SHARED_LOCK);
        if (!lock) return FALSE;
    }
    UnLock(lock);
    return TRUE;
}

/* Builds "<userDataPath>/account.dat" into buf, creating userDataPath
 * (recursively) first if it doesn't exist yet. userDataPath is anything
 * user-related that isn't disposable cache (unlike settings.cachePath,
 * which FS3ECache_Flush is free to empty) -- see FS3ESettings.userDataPath,
 * defaults to "PROGDIR:.user". Returns FALSE if the directory couldn't be
 * created/reached. */
static BOOL FS3EApp_AccountDatPath(char *buf, ULONG bufSize)
{
    const char *dir = (app->settings.userDataPath && app->settings.userDataPath[0])
                     ? app->settings.userDataPath : "PROGDIR:.user";
    if (!FS3EApp_MakeDirRecursive(dir)) return FALSE;
    snprintf(buf, bufSize, "%s/account.dat", dir);
    return TRUE;
}

/* -------------------------------------------------------------------------
 * Multi-account list -- every account the user has ever logged into, all
 * kept in the single account.dat (see FS3EAPP_ACCOUNTS_MAGIC below), so
 * fs3eloginview.c's acclistGroup can offer one-click switching
 * (FS3EApp_SwitchAccount) without a fresh OAuth round-trip, and so the app
 * reconnects to whichever one was active when it last quit. The currently
 * active account (app->accountXXX fields) is always kept mirrored into
 * this array too -- see FS3EApp_UpsertAccountsList, called from
 * FS3EApp_SaveAccount() below.
 * ---------------------------------------------------------------------- */

static void FS3EApp_FreeAccountEntry(FS3EAccount *a)
{
    if (a->apiBaseUrl)  { FreeVec(a->apiBaseUrl);  a->apiBaseUrl  = NULL; }
    if (a->accessToken) { FreeVec(a->accessToken); a->accessToken = NULL; }
    if (a->displayName) { FreeVec(a->displayName); a->displayName = NULL; }
    if (a->acct)        { FreeVec(a->acct);        a->acct        = NULL; }
    if (a->avatarURL)   { FreeVec(a->avatarURL);   a->avatarURL   = NULL; }
    if (a->accountId)   { FreeVec(a->accountId);   a->accountId   = NULL; }
}

/* Index of the account matching apiBaseUrl+acct, or -1. Matches on both
 * since the same acct name could theoretically exist on two servers. */
static LONG FS3EApp_FindAccountIndex(const char *apiBaseUrl, const char *acct)
{
    ULONG i;
    if (!apiBaseUrl || !acct) return -1;
    for (i = 0; i < app->accountCount; i++) {
        if (app->accounts[i].apiBaseUrl && app->accounts[i].acct &&
            !strcmp(app->accounts[i].apiBaseUrl, apiBaseUrl) &&
            !strcmp(app->accounts[i].acct, acct))
            return (LONG)i;
    }
    return -1;
}

/* Add-or-update one account in app->accounts[] (matched by apiBaseUrl+
 * acct). Called every time FS3EApp_SetAccount() is confirmed/saved, so
 * the list is always current -- a re-login to an already-known account
 * just refreshes its token/displayName/avatar in place rather than
 * growing the array. Silently drops the update if the array is full and
 * this would be a genuinely new account (FS3E_MAX_ACCOUNTS is generous
 * for a personal client, so this is not expected in practice). */
static void FS3EApp_UpsertAccountsList(const char *apiBaseUrl, const char *accessToken,
                                        const char *displayName, const char *acct,
                                        const char *avatarURL, const char *accountId)
{
    LONG idx = FS3EApp_FindAccountIndex(apiBaseUrl, acct);
    FS3EAccount *a;

    if (idx < 0) {
        if (app->accountCount >= FS3E_MAX_ACCOUNTS) {
            printf("FS3EApp_UpsertAccountsList: account.dat full (%d), dropping %s@%s\n",
                   FS3E_MAX_ACCOUNTS, acct ? acct : "?", apiBaseUrl ? apiBaseUrl : "?");
            return;
        }
        idx = (LONG)app->accountCount++;
    }

    a = &app->accounts[idx];
    FS3EApp_FreeAccountEntry(a);
    a->apiBaseUrl  = NetStrDup(apiBaseUrl);
    a->accessToken = NetStrDup(accessToken);
    a->displayName = NetStrDup(displayName);
    a->acct        = NetStrDup(acct);
    a->avatarURL   = NetStrDup(avatarURL);
    a->accountId   = NetStrDup(accountId);
}

/* First line of the multi-account account.dat format, distinguishing it
 * from every older on-disk format (the original single-account 6-line
 * format, and "FS3EACCOUNTS1", a since-abandoned multi-account format
 * that still wrote accessToken in plaintext) -- all of which stored a
 * token with no machine-key binding at all, and are therefore refused
 * rather than read, see FS3EApp_LoadAccount. This format's accessToken
 * is instead hex+XOR-encoded with FS3EMachineId_GetKey() (see below). */
#define FS3EAPP_ACCOUNTS_MAGIC "FS3EACCOUNTS2"

/* Machine-derived XOR key (see fs3emachineid.h -- NOT cryptography, just a
 * deterrent against a copied account.dat's tokens working verbatim on a
 * different machine), cached for the process lifetime so the underlying
 * device I/O only ever runs once no matter how many accounts get saved. */
static BOOL  s_machineKeyValid = FALSE;
static UBYTE s_machineKey[FS3EMACHINEID_KEYLEN];

/* Arbitrary fixed constant, folded on top of the raw geometry/RDB-derived
 * bytes below. Those source fields are mostly small numbers (sector size,
 * cylinder/head counts, ...) with long runs of zero high-order bytes, so
 * left alone the derived key would visibly encode "which byte came from
 * which geometry field" (an attacker familiar with typical Amiga geometry
 * could guess large chunks of it), and any two machines with the same
 * common sector size would share those same zero bytes. XORing with this
 * constant is just as cheap and removes that structure -- it adds no real
 * secrecy (anyone reading this source has the constant too), it just
 * stops the key from looking meaningful. */
static const UBYTE s_machineKeySalt[FS3EMACHINEID_KEYLEN] = {
    0x4b, 0x92, 0xd1, 0x07, 0x6a, 0xf3, 0x1c, 0x85,
    0x3e, 0xb0, 0x59, 0xc4, 0x2d, 0x97, 0x60, 0xfa
};

static const UBYTE *FS3EApp_MachineKey(void)
{
    if (!s_machineKeyValid) {
        ULONG i;
        FS3EMachineId_GetKey(s_machineKey);
        for (i = 0; i < FS3EMACHINEID_KEYLEN; i++)
            s_machineKey[i] ^= s_machineKeySalt[i];
        s_machineKeyValid = TRUE;
        printf("FS3EApp_MachineKey: ");
        for (i = 0; i < FS3EMACHINEID_KEYLEN; i++)
            printf("%02lx", (unsigned long)s_machineKey[i]);
        printf("\n");
    }
    return s_machineKey;
}

static int FS3EApp_HexNibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Encodes rawToken as hex(XOR(rawToken, machine key)) into out (a plain
 * printable ASCII string, safe for the line-based account.dat format --
 * XORing a token's raw bytes directly would produce arbitrary bytes,
 * including embedded '\n'/'\0', that RLINE's line-at-a-time FGets() can't
 * round-trip). Truncates rather than overflows if rawToken is implausibly
 * longer than outCap allows. */
static void FS3EApp_EncodeToken(const char *rawToken, char *out, ULONG outCap)
{
    static const char hexDigits[] = "0123456789abcdef";
    const UBYTE *key = FS3EApp_MachineKey();
    ULONG len    = rawToken ? (ULONG)strlen(rawToken) : 0;
    ULONG maxLen = (outCap >= 1) ? (outCap - 1) / 2 : 0;
    ULONG i;

    if (len > maxLen) len = maxLen;

    for (i = 0; i < len; i++) {
        UBYTE b = (UBYTE)rawToken[i] ^ key[i % FS3EMACHINEID_KEYLEN];
        out[i * 2]     = hexDigits[b >> 4];
        out[i * 2 + 1] = hexDigits[b & 0xF];
    }
    out[len * 2] = '\0';
}

/* Reverses FS3EApp_EncodeToken. Tolerant of malformed input (a non-hex
 * character stops decoding right there rather than reading garbage) --
 * this only ever runs on account.dat's own previously-written content, but
 * a hand-edited or corrupted file should degrade to a truncated/wrong
 * token (which then just fails to authenticate) rather than misbehave. */
static void FS3EApp_DecodeToken(const char *hexIn, char *out, ULONG outCap)
{
    const UBYTE *key = FS3EApp_MachineKey();
    ULONG hexLen = hexIn ? (ULONG)strlen(hexIn) : 0;
    ULONG rawLen = hexLen / 2;
    ULONG maxLen = (outCap >= 1) ? outCap - 1 : 0;
    ULONG i;

    if (rawLen > maxLen) rawLen = maxLen;

    for (i = 0; i < rawLen; i++) {
        int hi = FS3EApp_HexNibble(hexIn[i * 2]);
        int lo = FS3EApp_HexNibble(hexIn[i * 2 + 1]);
        if (hi < 0 || lo < 0) { rawLen = i; break; }
        out[i] = (char)(((UBYTE)((hi << 4) | lo)) ^ key[i % FS3EMACHINEID_KEYLEN]);
    }
    out[rawLen] = '\0';
}

/* Save every entry in app->accounts[] (mirroring the currently active
 * account into it first) to <userDataPath>/account.dat: magic line, count,
 * index of the active account (so relaunching reconnects to the same one
 * -- see FS3EApp_LoadAccount), then 6 lines per account. */
static void FS3EApp_SaveAccount(void)
{
    BPTR f;
    char path[300];
    LONG activeIdx;
    ULONG i;

    if (!app->accountApiBaseUrl || !app->accountAccessToken) return;

    /* Keep app->accounts[] current before writing it out. */
    FS3EApp_UpsertAccountsList(app->accountApiBaseUrl, app->accountAccessToken,
                                app->accountDisplayName, app->accountAcct,
                                app->accountAvatarURL, app->accountId);
    activeIdx = FS3EApp_FindAccountIndex(app->accountApiBaseUrl, app->accountAcct);

    if (!FS3EApp_AccountDatPath(path, sizeof(path))) {
        printf("FS3EApp_SaveAccount: can't create user data dir %s\n",
               app->settings.userDataPath ? app->settings.userDataPath : "?");
        return;
    }
    f = Open(path, MODE_NEWFILE);
    if (!f) return;

    FPuts(f, FS3EAPP_ACCOUNTS_MAGIC); FPuts(f, "\n");
    {
        char numLine[16];
        snprintf(numLine, sizeof(numLine), "%lu", (unsigned long)app->accountCount);
        FPuts(f, numLine); FPuts(f, "\n");
        snprintf(numLine, sizeof(numLine), "%ld", (long)activeIdx);
        FPuts(f, numLine); FPuts(f, "\n");
    }
    for (i = 0; i < app->accountCount; i++) {
        FS3EAccount *a = &app->accounts[i];
        char encTok[1025];
        FS3EApp_EncodeToken(a->accessToken ? a->accessToken : "", encTok, sizeof(encTok));
        FPuts(f, a->apiBaseUrl  ? a->apiBaseUrl  : ""); FPuts(f, "\n");
        FPuts(f, encTok); FPuts(f, "\n");
        FPuts(f, a->displayName ? a->displayName : ""); FPuts(f, "\n");
        FPuts(f, a->acct        ? a->acct        : ""); FPuts(f, "\n");
        FPuts(f, a->avatarURL   ? a->avatarURL   : ""); FPuts(f, "\n");
        FPuts(f, a->accountId   ? a->accountId   : ""); FPuts(f, "\n");
    }
    Close(f);
    printf("FS3EApp_SaveAccount: saved %lu account(s), active=%s @ %s\n",
           (unsigned long)app->accountCount,
           app->accountAcct ? app->accountAcct : "?",
           app->accountApiBaseUrl);
}

/* Load <userDataPath>/account.dat into app->accounts[]/accountCount, and
 * reconnect to whichever one was active when the app last quit (so a
 * relaunch resumes the same session, not just "the last thing that was
 * ever written"). Returns TRUE if an account is now active.
 *
 * Only the current format (FS3EAPP_ACCOUNTS_MAGIC, token hex+XOR-encoded
 * with the machine key -- see FS3EApp_EncodeToken/FS3EMachineId_GetKey) is
 * ever auto-connected. Anything older -- FS3EAPP_ACCOUNTS_MAGIC_V1 (an
 * earlier multi-account format that still wrote the token in plaintext)
 * or the original single-account 6-line format -- stored its token with
 * no machine binding at all, which is exactly the "an account.dat copied
 * off this machine still works elsewhere" risk the encoding exists to
 * close. Trusting such a file "just this once, then upgrade it" (an
 * earlier version of this function did that) defeats the whole point: it
 * still authenticates with the bare, unbound token before ever
 * re-encoding it. So an unrecognized/older format is refused outright --
 * logged, not connected, not migrated -- and the user has to log back in,
 * which produces a freshly machine-bound entry. */
static BOOL FS3EApp_LoadAccount(void)
{
    BPTR f;
    char path[300];
    char apiBaseUrl[256], tokLine[1025], accessToken[512];
    char displayName[128], acct[128], avatarURL[512], accountId[64];
    ULONG n;

    if (!FS3EApp_AccountDatPath(path, sizeof(path))) return FALSE;
    f = Open(path, MODE_OLDFILE);
    if (!f) return FALSE;

    /* FGets includes the trailing '\n' — strip it. */
#define RLINE(buf) \
    (FGets(f, buf, sizeof(buf)) && (buf[0] != '\0') && \
     ((n = strlen(buf)) > 0) && (buf[n-1] == '\n' ? (buf[n-1] = '\0', 1) : 1))

    if (!RLINE(apiBaseUrl)) { Close(f); return FALSE; }

    if (strcmp(apiBaseUrl, FS3EAPP_ACCOUNTS_MAGIC) != 0) {
        Close(f);
        printf("FS3EApp_LoadAccount: account.dat is an older, un-hashed format "
               "(no machine-key binding) -- refusing to auto-connect; "
               "please log in again to re-secure this account\n");
        return FALSE;
    }

    {
        char  numLine[16];
        ULONG count, i;
        LONG  activeIdx;

        if (!RLINE(numLine)) { Close(f); return FALSE; }
        count = (ULONG)atol(numLine);
        if (count > FS3E_MAX_ACCOUNTS) count = FS3E_MAX_ACCOUNTS;

        if (!RLINE(numLine)) { Close(f); return FALSE; }
        activeIdx = (LONG)atol(numLine);

        for (i = 0; i < count; i++) {
            if (!RLINE(apiBaseUrl) || !RLINE(tokLine) || !apiBaseUrl[0] || !tokLine[0])
                break;
            FS3EApp_DecodeToken(tokLine, accessToken, sizeof(accessToken));
            if (!RLINE(displayName)) displayName[0] = '\0';
            if (!RLINE(acct))        acct[0]        = '\0';
            if (!RLINE(avatarURL))   avatarURL[0]   = '\0';
            if (!RLINE(accountId))   accountId[0]   = '\0';

            FS3EApp_UpsertAccountsList(apiBaseUrl, accessToken, displayName,
                                        acct, avatarURL, accountId);
        }
        Close(f);
#undef RLINE

        printf("FS3EApp_LoadAccount: loaded %lu account(s), active=%ld\n",
               (unsigned long)app->accountCount, (long)activeIdx);

        if (activeIdx < 0 || (ULONG)activeIdx >= app->accountCount) return FALSE;
        {
            FS3EAccount *a = &app->accounts[activeIdx];
            FS3EApp_SetAccount(a->apiBaseUrl, a->accessToken, a->displayName,
                                a->acct, a->avatarURL, a->accountId);
        }
        app->loginPhase = FS3ELOGIN_DONE;
        return TRUE;
    }
}

/* account.dat files saved before accountId existed load with an empty id,
 * which silently disables VIEWMODE_User's fetch (ViewModeTimeline returns
 * FALSE without it). Re-verify the existing access token to backfill it
 * instead of requiring the user to log out and back in -- see
 * FS3ENETQ_VERIFY_ACCOUNT's reply case in FS3EApp_HandleNetReply(). */
static void FS3EApp_BackfillAccountId(void)
{
    FS3ENetVerifyAccountReq *req;

    if (!app->accountApiBaseUrl || !app->accountAccessToken) return;
    if (app->accountId && app->accountId[0]) return;

    req = FS3ENetVerifyAccountReq_Alloc(app->accountApiBaseUrl, app->accountAccessToken);
    if (!req) return;

    FS3EApp_NetSend(FS3ENETQ_VERIFY_ACCOUNT, req,
        sizeof(FS3ENetVerifyAccountReq) /* net process only reads char* fields */);
}

/* Builds the /api/v1/-relative path to fetch for a given VIEWMODE_* value
 * into buf (see FS3EMastodon_GetTimeline, which just appends this onto
 * "apiBaseUrl/api/v1/"). Returns FALSE if there's nothing to fetch yet
 * (unknown view mode, or VIEWMODE_User before accountId is known). */
static BOOL ViewModeTimeline(ULONG viewMode, char *buf, ULONG bufSize)
{
    /* Initial page is kept small on purpose: a big first page (Home used to
     * ask for 35) enqueues one low-priority FETCH_IMAGE request per avatar/
     * thumbnail, and since the network process is single-threaded/serialized
     * (see FS3ENet_ProcEntry), that backlog is what actually stalls switching
     * to another view right after startup -- not the timeline fetch itself.
     * Scrolling triggers incremental older/newer pages on top of this. */
    switch (viewMode) {
        case VIEWMODE_Home:
            snprintf(buf, bufSize, "timelines/home?limit=4");
            return TRUE;
        case VIEWMODE_Local:
            snprintf(buf, bufSize, "timelines/public?local=true&limit=4");
            return TRUE;
        case VIEWMODE_Fed:
            snprintf(buf, bufSize, "timelines/public?limit=4");
            return TRUE;
        case VIEWMODE_User:
            if (!app->accountId || !app->accountId[0]) return FALSE;
            /* exclude_reblogs=false is Mastodon's own documented default,
             * but made explicit rather than left implicit -- some servers/
             * versions could differ, and a profile that only ever boosts
             * (never posts originally) would otherwise show as empty. */
            snprintf(buf, bufSize, "accounts/%s/statuses?limit=4&exclude_reblogs=false", app->accountId);
            return TRUE;
        case VIEWMODE_Search:
            /* Only the FS3ESEARCH_USER_PROFILE sub-mode fetches anything
             * today (word/user search are future sub-modes of this same
             * channel -- see FS3ESearchMode). Mirrors VIEWMODE_User
             * above exactly, just for whichever account is currently
             * open instead of always our own. */
            if (app->searchMode != FS3ESEARCH_USER_PROFILE ||
                !app->searchProfileAccountId || !app->searchProfileAccountId[0])
                return FALSE;
            /* exclude_reblogs=false -- see the matching comment on
             * VIEWMODE_User above; a profile that only ever boosts is
             * exactly the case that prompted making this explicit. */
            snprintf(buf, bufSize, "accounts/%s/statuses?limit=4&exclude_reblogs=false", app->searchProfileAccountId);
            return TRUE;
        default:
            return FALSE;
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

/* Index into the MSG_SEARCHV_WAIT1..4 rotation -- bumped once per search
 * fired (see FS3EApp_SearchWord), NOT on every FS3EApp_CheckConnectionState
 * call (that runs on all sorts of unrelated state changes too, e.g. login
 * phase or account changes; incrementing here would rotate the message
 * mid-search for no reason instead of picking one per search). */
static ULONG s_searchWaitMsgIdx = 0;

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
                        text = "Server error.";            break;
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
    char tl[128];
    FS3ENetTimelineReq *req;
    ULONG bit = (1UL << viewMode);

    /* Search is never driven by this "fetch once per session the first
     * time a channel is viewed" mechanism -- FS3EApp_OpenProfile() owns
     * its fetch entirely (fired once per profile opened, not once per
     * view switch), always as an FS3ENETPAGE_OLDER page so it correctly
     * lands via TIMELINE_AppendPost below the profile header (see
     * TTIMELINE_ShowProfile) instead of through the AddPost/prepend path
     * this function's FS3ENETPAGE_INITIAL request below would take.
     * Without this guard, merely switching back to an already-open
     * profile (fs3e_setViewMode calls this unconditionally) would fire a
     * second, wrongly-directed fetch. */
    if (viewMode == VIEWMODE_Search) return;

    /* Notifications aren't Status objects and don't have a /api/v1/-
     * relative timeline path the way every other channel does (see
     * ViewModeTimeline, which has no case for VIEWMODE_Notifs at all) --
     * FS3ENETQ_NOTIFICATIONS instead, reusing the exact same
     * timelineFetchedMask/timelineErrorMask bookkeeping so
     * FS3EApp_CheckConnectionState's waiting/idle text keeps working here
     * with no special-casing there. */
    if (viewMode == VIEWMODE_Notifs) {
        FS3ENetNotificationsReq *nreq;

        if (!app->accountApiBaseUrl || !app->accountAccessToken) return;
        if (app->channelPopulatedMask & bit) return; /* already has its first page -- see the field comment */
        if (app->timelineFetchedMask & bit) return;

        printf("FS3EApp_FetchTimeline: viewMode=Notifs\n");

        nreq = FS3ENetNotificationsReq_Alloc(FS3ENETPAGE_INITIAL,
                   app->accountGeneration, app->accountApiBaseUrl,
                   app->accountAccessToken, NULL, NULL);
        if (!nreq) return;

        if (FS3EApp_NetSend(FS3ENETQ_NOTIFICATIONS, nreq, sizeof(*nreq))) {
            app->timelineFetchedMask |= bit;
            app->timelineErrorMask   &= ~bit;
            FS3EApp_CheckConnectionState();
        }
        return;
    }

    if (!app->accountApiBaseUrl || !app->accountAccessToken) return;
    if (app->channelPopulatedMask & bit) return; /* already has its first page -- see the field comment */
    if (app->timelineFetchedMask & bit) return;
    if (!ViewModeTimeline(viewMode, tl, sizeof(tl))) return;

    printf("FS3EApp_FetchTimeline: viewMode=%u timeline=%s\n", (unsigned)viewMode, tl);

    req = FS3ENetTimelineReq_Alloc(viewMode, FS3ENETPAGE_INITIAL,
              app->accountGeneration, FS3ENET_TLSHAPE_ARRAY,
              app->accountApiBaseUrl, app->accountAccessToken,
              tl, NULL, NULL, NULL);
    if (!req) return;

    if (FS3EApp_NetSend(FS3ENETQ_TIMELINE, req,
            sizeof(FS3ENetTimelineReq) /* net process only reads char* fields */)) {
        app->timelineFetchedMask |= bit;
        app->timelineErrorMask   &= ~bit; /* clear any previous error for this channel */
        FS3EApp_CheckConnectionState();
    }
}

/* Rebuild the login window's accounts list from app->accounts[], marking
 * whichever entry matches the currently active account as "current" (see
 * FS3ELoginAccountRow.current). Safe to call any time -- before the
 * window has ever been created it's a no-op (FS3ELoginView_SetAccountsList
 * bails on a NULL acclistBrowser). Call this any time app->accounts[] or
 * the active account changes: after loading accounts.dat at startup,
 * after a successful login/switch, and right before opening the login
 * window (GID_TITLEBAR_ACCOUNTS) so it's never stale. */
static void FS3EApp_RefreshLoginAccountsList(void)
{
    FS3ELoginAccountRow rows[FS3E_MAX_ACCOUNTS];
    ULONG i;

    for (i = 0; i < app->accountCount; i++) {
        rows[i].server  = app->accounts[i].apiBaseUrl;
        rows[i].user    = app->accounts[i].acct;
        rows[i].current = (app->accountApiBaseUrl && app->accountAcct &&
                           app->accounts[i].apiBaseUrl && app->accounts[i].acct &&
                           !strcmp(app->accounts[i].apiBaseUrl, app->accountApiBaseUrl) &&
                           !strcmp(app->accounts[i].acct, app->accountAcct));
    }
    FS3ELoginView_SetAccountsList(&app->loginView, rows, app->accountCount);
}

/* Switch the connected account to app->accounts[index] (a row click in
 * fs3eloginview.c's acclistGroup -- see GID_LOGIN_ACCOUNTS_LIST). No-op if
 * index is out of range or already the active account. Every toot
 * timeline channel is wiped (they hold the outgoing account's posts) and
 * the current view mode's channel is fetched fresh under the new
 * account, exactly like a brand new login does (FS3ENETQ_LOGIN_FINISH's
 * reply handler below). */
static void FS3EApp_SwitchAccount(LONG index)
{
    FS3EAccount *a;
    /* Reentrancy guard: FS3ELoginView_SetAccountsList() no longer sets
     * LISTBROWSER_Selected on reattach (that was the confirmed root cause
     * of an infinite switch-A/switch-B loop -- see its comment in
     * fs3eloginview.c), but this guard stays as defense in depth against
     * any *other* BOOPSI notify this function's own side effects might
     * someday trigger back into GID_LOGIN_ACCOUNTS_LIST: the BoopsiDelay
     * queue drains in a single while loop (fs3eboopsimessage.c), so a
     * notify enqueued from inside this call is picked up by that same
     * loop before this call returns, not on some later, safely-separate
     * iteration. */
    static BOOL s_switching = FALSE;

    if (s_switching) {
        printf("FS3EApp_SwitchAccount: reentrant call ignored (index=%ld)\n", (long)index);
        return;
    }

    if (index < 0 || (ULONG)index >= app->accountCount) return;
    a = &app->accounts[index];
    if (!a->apiBaseUrl || !a->acct) return;

    if (app->accountApiBaseUrl && app->accountAcct &&
        !strcmp(a->apiBaseUrl, app->accountApiBaseUrl) &&
        !strcmp(a->acct, app->accountAcct))
        return; /* already connected -- see the caller's "not connected yet" gate */

    s_switching = TRUE;

    printf("FS3EApp_SwitchAccount: switching to %s @ %s\n", a->acct, a->apiBaseUrl);

    /* Abandon any fresh-login flow in progress (e.g. the user had pasted
     * a server/started OAuth for yet another account, then changed their
     * mind and clicked an existing row instead). */
    FS3EApp_FreeLoginState();

    FS3EApp_SetAccount(a->apiBaseUrl, a->accessToken, a->displayName,
                        a->acct, a->avatarURL, a->accountId);
    FS3EApp_SaveAccount(); /* account.dat now points at this account too */
    app->loginPhase = FS3ELOGIN_DONE;

    /* Every channel's posts belong to the account being left. */
    if (app->tootTimeline) {
        SetAttrs(app->tootTimeline, TTIMELINE_ClearAllChannels, TRUE, TAG_DONE);
        if (CurrentMainWindow)
            RefreshGList((struct Gadget *)app->tootTimeline, CurrentMainWindow, NULL, 1);
    }

    /* Search-channel profile/discussion state belonged to the outgoing account too. */
    if (app->searchProfileAcct)       { FreeVec(app->searchProfileAcct);       app->searchProfileAcct       = NULL; }
    if (app->searchProfileAccountId)  { FreeVec(app->searchProfileAccountId);  app->searchProfileAccountId  = NULL; }
    if (app->searchDiscussionStatusId){ FreeVec(app->searchDiscussionStatusId);app->searchDiscussionStatusId= NULL; }
    app->searchMode = FS3ESEARCH_NONE;

    app->timelineFetchedMask   = 0;
    app->channelPopulatedMask  = 0;
    app->timelineErrorMask     = 0;
    app->olderPageInFlightMask = 0;
    app->newerPageInFlightMask = 0;
    FS3EApp_FetchTimeline(app->viewMode);

    FS3EApp_RefreshLoginAccountsList();

    s_switching = FALSE;
}

/* Send an async TIMELINE request for viewMode paginating in `direction`
 * (FS3ENETPAGE_OLDER/NEWER) from whatever status id the gadget currently
 * has at that end of its list (TTIMELINE_OldestPostId/NewestPostId) --
 * no-op if a page in that direction is already in flight for this
 * channel, or there's no known id to paginate from yet. Separate from
 * FS3EApp_FetchTimeline's one-shot-per-session initial fetch: this fires
 * repeatedly, driven by scroll position/clicks on the pinned boundary
 * rows -- see the TTL_HOT_LOAD_OLDER/NEWER handling in
 * FS3EApp_HandleNetReply's GID_TTIMELINE case. */
static void FS3EApp_FetchTimelinePage(ULONG viewMode, ULONG direction)
{
    char tl[128];
    FS3ENetTimelineReq *req;
    ULONG  bit = (1UL << viewMode);
    ULONG *inFlightMask = (direction == FS3ENETPAGE_OLDER)
                         ? &app->olderPageInFlightMask
                         : &app->newerPageInFlightMask;
    ULONG  attrTag = (direction == FS3ENETPAGE_OLDER)
                    ? TTIMELINE_OldestPostId : TTIMELINE_NewestPostId;
    ULONG  fromIdVal = 0;
    const char *fromId;

    if (!app->accountApiBaseUrl || !app->accountAccessToken) return;
    if (*inFlightMask & bit) return;
    if (!app->tootTimeline) return;

    /* Notifications: same FS3ENETQ_NOTIFICATIONS/postId-as-notification-id
     * reasoning as FS3EApp_FetchTimeline's VIEWMODE_Notifs branch --
     * TTIMELINE_OldestPostId/NewestPostId already read post->postId
     * generically, which for a notification row holds the notification's
     * own id (not the embedded status' id -- see
     * TTLPostSetup.notifStatusId), exactly what /api/v1/notifications'
     * max_id/min_id pagination needs. */
    if (viewMode == VIEWMODE_Notifs) {
        FS3ENetNotificationsReq *nreq;

        GetAttr(attrTag, app->tootTimeline, &fromIdVal);
        fromId = (const char *)fromIdVal;
        if (!fromId || !fromId[0]) return;

        printf("FS3EApp_FetchTimelinePage: viewMode=Notifs dir=%u from=%s\n",
               (unsigned)direction, fromId);

        nreq = FS3ENetNotificationsReq_Alloc(direction,
                   app->accountGeneration, app->accountApiBaseUrl, app->accountAccessToken,
                   (direction == FS3ENETPAGE_OLDER) ? fromId : NULL,
                   (direction == FS3ENETPAGE_NEWER) ? fromId : NULL);
        if (!nreq) return;

        if (FS3EApp_NetSend(FS3ENETQ_NOTIFICATIONS, nreq, sizeof(*nreq)))
            *inFlightMask |= bit;
        return;
    }

    if (!ViewModeTimeline(viewMode, tl, sizeof(tl))) return;

    GetAttr(attrTag, app->tootTimeline, &fromIdVal);
    fromId = (const char *)fromIdVal;
    if (!fromId || !fromId[0]) return; /* nothing loaded yet to paginate from */

    printf("FS3EApp_FetchTimelinePage: viewMode=%u dir=%u from=%s\n",
           (unsigned)viewMode, (unsigned)direction, fromId);

    req = FS3ENetTimelineReq_Alloc(viewMode, direction,
              app->accountGeneration, FS3ENET_TLSHAPE_ARRAY,
              app->accountApiBaseUrl, app->accountAccessToken, tl,
              (direction == FS3ENETPAGE_OLDER) ? fromId : NULL,
              (direction == FS3ENETPAGE_NEWER) ? fromId : NULL,
              NULL);
    if (!req) return;

    if (FS3EApp_NetSend(FS3ENETQ_TIMELINE, req, sizeof(FS3ENetTimelineReq)))
        *inFlightMask |= bit;
}

/* Opens (or re-opens) a user's profile in the Search channel -- the
 * shared entry point for both TTL_HOT_AVATAR and TTL_HOT_MENTION clicks
 * (a toot author's avatar, or an @mention inside any toot/bio body).
 * acctOrHandle may or may not have a leading '@' (avatar-click data
 * never does, mention-click data always does -- see TTL_HOT_MENTION's
 * comment in fs3etoottimeline.h) -- stripped here so both paths converge
 * on the same acct string /api/v1/accounts/lookup expects.
 *
 * Only starts the lookup; the rest of the flow (header population,
 * avatar fetch, relationship + first toot page) continues in the
 * FS3ENETQ_ACCOUNT_LOOKUP reply handler once the account id is known. */
static void FS3EApp_OpenProfile(const char *acctOrHandle)
{
    const char *acct = acctOrHandle;
    FS3ENetAccountLookupReq *req;

    if (!acct || !acct[0]) return;
    if (acct[0] == '@') acct++;
    if (!acct[0]) return;
    if (!app->accountApiBaseUrl) return;

    if (app->searchProfileAcct)      { FreeVec(app->searchProfileAcct);      app->searchProfileAcct      = NULL; }
    if (app->searchProfileAccountId) { FreeVec(app->searchProfileAccountId); app->searchProfileAccountId = NULL; }
    app->searchProfileAcct = NetStrDup(acct);
    app->searchMode        = FS3ESEARCH_USER_PROFILE;

    fs3e_setViewMode(VIEWMODE_Search);

    /* Mirror the opened profile into the search editor as "user@server" --
     * Mastodon's own "acct" field already has "@server" for remote users,
     * but is bare "user" for local ones (same instance as us), so fill in
     * our own instance's domain in that case. */
    if (app->searchWordEditor) {
        char handle[256];
        if (strchr(acct, '@')) {
            snprintf(handle, sizeof(handle), "%s", acct);
        } else {
            const char *domain = app->accountApiBaseUrl ? app->accountApiBaseUrl : "";
            if (strncmp(domain, "https://", 8) == 0) domain += 8;
            else if (strncmp(domain, "http://", 7) == 0) domain += 7;
            snprintf(handle, sizeof(handle), "%s@%s", acct, domain);
        }
        SetGdAttrs(app->searchWordEditor, UTED_Text, (ULONG)handle, TAG_END);
    }

    if (!app->searchProfileAcct) return;

    req = FS3ENetAccountLookupReq_Alloc(app->accountApiBaseUrl,
              app->accountAccessToken ? app->accountAccessToken : "",
              app->searchProfileAcct);
    if (!req) return;

    FS3EApp_NetSend(FS3ENETQ_ACCOUNT_LOOKUP, req, sizeof(*req));
}

/* "Discussion mode" -- shows statusId's toot as the first item in the
 * Search channel, followed by its replies (Mastodon's "descendants"),
 * each flagged isThreadReply (see TTLPostSetup.isThreadReply). Deliberately
 * simple, mirroring the user's own description: no ancestors (the chain
 * this toot itself replied to), no nested/threaded indentation, no
 * pagination for long discussions -- just "main toot, then its answers
 * below," flat, in server order. Unlike FS3EApp_OpenProfile there's no
 * single atomic "clear + seed" tag, so the channel is cleared proactively
 * here, before either fetch's reply can land.
 *
 * Two FS3ENETQ_TIMELINE requests, fired back to back: the main toot
 * (FS3ENET_TLSHAPE_SINGLE, lands via AddPost as item 1 since the channel
 * is already empty) then its replies (FS3ENET_TLSHAPE_CONTEXT_DESCENDANTS,
 * FS3ENETPAGE_OLDER so it lands via AppendPost below -- same trick
 * FS3EApp_OpenProfile's second request uses for its header). Relies on
 * the network process being a single serialized task (see the
 * accountGeneration comment in FS3EApp_HandleNetReply's FS3ENETQ_TIMELINE
 * case) so reply #1 always lands before reply #2 is even attempted. */
static void FS3EApp_OpenDiscussion(const char *statusId)
{
    char tl[128];
    FS3ENetTimelineReq *req;

    if (!statusId || !statusId[0]) return;
    if (!app->accountApiBaseUrl) return;

    if (app->searchDiscussionStatusId) { FreeVec(app->searchDiscussionStatusId); app->searchDiscussionStatusId = NULL; }
    app->searchDiscussionStatusId = NetStrDup(statusId);
    app->searchMode = FS3ESEARCH_DISCUSSION;

    fs3e_setViewMode(VIEWMODE_Search);

    if (app->tootTimeline)
        SetAttrs(app->tootTimeline, TTIMELINE_ClearPosts, TRUE, TAG_DONE);

    if (!app->searchDiscussionStatusId) return;

    snprintf(tl, sizeof(tl), "statuses/%s", app->searchDiscussionStatusId);
    req = FS3ENetTimelineReq_Alloc(VIEWMODE_Search, FS3ENETPAGE_INITIAL,
              app->accountGeneration, FS3ENET_TLSHAPE_SINGLE,
              app->accountApiBaseUrl,
              app->accountAccessToken ? app->accountAccessToken : "",
              tl, "", "", NULL);
    if (req) FS3EApp_NetSend(FS3ENETQ_TIMELINE, req, sizeof(*req));

    snprintf(tl, sizeof(tl), "statuses/%s/context", app->searchDiscussionStatusId);
    req = FS3ENetTimelineReq_Alloc(VIEWMODE_Search, FS3ENETPAGE_OLDER,
              app->accountGeneration, FS3ENET_TLSHAPE_CONTEXT_DESCENDANTS,
              app->accountApiBaseUrl,
              app->accountAccessToken ? app->accountAccessToken : "",
              tl, "", "", NULL);
    if (req) FS3EApp_NetSend(FS3ENETQ_TIMELINE, req, sizeof(*req));
}

/* Word/hashtag search -- the Enter-pressed handler on the search line (see
 * GID_SEARCH_WORD_EDITOR in the main event loop) calls this for any query
 * that doesn't look like a "name@server" user handle. Mirrors
 * FS3EApp_OpenDiscussion() exactly: clears the Search channel synchronously
 * (no profile header, just a flat status list, same as discussion mode),
 * then fires one FS3ENETQ_TIMELINE request through the
 * FS3ENET_TLSHAPE_SEARCH_STATUSES shape (GET /api/v2/search, see
 * FS3EMastodon_GetTimeline). query is raw UTF-8 text, NOT URL-encoded --
 * FS3ENet_HandleTimeline (network process) does that itself from
 * fs3et_SearchQuery. No pagination yet (single page, FS3ENETPAGE_INITIAL
 * only): TTL_HOT_LOAD_OLDER/NEWER route through ViewModeTimeline(), which
 * has no FS3ESEARCH_WORD case, so they're a harmless no-op for now. */
static void FS3EApp_SearchWord(const char *query)
{
    char tl[64];
    FS3ENetTimelineReq *req;

    if (!query || !query[0]) return;
    if (!app->accountApiBaseUrl) return;

    if (app->searchProfileAcct)        { FreeVec(app->searchProfileAcct);        app->searchProfileAcct        = NULL; }
    if (app->searchProfileAccountId)   { FreeVec(app->searchProfileAccountId);   app->searchProfileAccountId   = NULL; }
    if (app->searchDiscussionStatusId) { FreeVec(app->searchDiscussionStatusId); app->searchDiscussionStatusId = NULL; }
    app->searchMode = FS3ESEARCH_WORD;

    fs3e_setViewMode(VIEWMODE_Search);

    if (app->tootTimeline)
        SetAttrs(app->tootTimeline, TTIMELINE_ClearPosts, TRUE, TAG_DONE);

    snprintf(tl, sizeof(tl), "search?type=statuses&limit=20");
    req = FS3ENetTimelineReq_Alloc(VIEWMODE_Search, FS3ENETPAGE_INITIAL,
              app->accountGeneration, FS3ENET_TLSHAPE_SEARCH_STATUSES,
              app->accountApiBaseUrl,
              app->accountAccessToken ? app->accountAccessToken : "",
              tl, "", "", query);
    if (req && FS3EApp_NetSend(FS3ENETQ_TIMELINE, req, sizeof(*req))) {
        /* Marks the Search channel "fetching" so
         * FS3EApp_CheckConnectionState shows one of the search wait
         * messages instead of "Updating..." -- cleared the same way every
         * other channel's INITIAL fetch already is, in the generic
         * FS3ENETQ_TIMELINE reply handler. */
        app->timelineFetchedMask |= (1UL << VIEWMODE_Search);
        s_searchWaitMsgIdx++;
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
                               reply->fs3enl_Account.fma_AvatarURL,
                               reply->fs3enl_Account.fma_Id);
            FS3EApp_SaveAccount();
            FS3EApp_FreeLoginState();
            app->loginPhase = FS3ELOGIN_DONE;
            /* Fetch the current view mode timeline */
            app->timelineFetchedMask  = 0; /* reset so new account fetches fresh */
            app->channelPopulatedMask = 0;
            FS3EApp_FetchTimeline(app->viewMode);
            /* Credentials just confirmed -- empty the fields so there's
             * nothing to accidentally resubmit later (see the matching
             * clear at startup, right after FS3ELoginView_Create). */
            FS3ELoginView_ClearFields(&app->loginView);
            FS3EApp_RefreshLoginAccountsList(); /* new/refreshed row, mark current */
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

    case FS3ENETQ_VERIFY_ACCOUNT:
        /* Backfill for an account.dat saved before accountId existed (see
         * FS3EApp_BackfillAccountId()). Re-runs the same "resync from
         * server" FS3EApp_SetAccount() does at login, just without a fresh
         * access token -- apiBaseUrl/accessToken are unchanged, only the
         * account fields (chiefly fma_Id) are new. */
        if (msg->fs3em_Result == FS3ENETR_OK) {
            FS3ENetVerifyAccountReply *reply = (FS3ENetVerifyAccountReply *)msg->fs3em_Data;
            printf("verify-account reply: ok, acct=%s id=%s\n",
                   reply->fs3eva_Account.fma_Acct ? reply->fs3eva_Account.fma_Acct : "?",
                   reply->fs3eva_Account.fma_Id   ? reply->fs3eva_Account.fma_Id   : "?");
            FS3EApp_SetAccount(app->accountApiBaseUrl,
                               app->accountAccessToken,
                               reply->fs3eva_Account.fma_DisplayName,
                               reply->fs3eva_Account.fma_Acct,
                               reply->fs3eva_Account.fma_AvatarURL,
                               reply->fs3eva_Account.fma_Id);
            FS3EApp_SaveAccount();
            FS3EApp_RefreshLoginAccountsList();
            /* In case the user is already sitting on a channel that
             * couldn't fetch without an id (VIEWMODE_User). */
            FS3EApp_FetchTimeline(app->viewMode);
        } else {
            printf("verify-account reply: FAILED result=%u (keeping existing account fields)\n",
                   (unsigned)msg->fs3em_Result);
        }
        break;

    case FS3ENETQ_INSTANCE_INFO:
        /* Always OK (see FS3ENet_HandleInstanceInfo -- a fetch failure on
         * the network side is reported via fs3eii_Known, not fs3em_Result),
         * but guard anyway rather than assume. Only a server-confirmed
         * value (fs3eii_Known) is shown -- a failed fetch leaves
         * accountMaxChars at 0 ("Max: -"), never presenting
         * FS3EMastodon_GetInstanceInfo's internal fallback guess as fact. */
        if (msg->fs3em_Result == FS3ENETR_OK) {
            FS3ENetInstanceInfoReply *reply = (FS3ENetInstanceInfoReply *)msg->fs3em_Data;
            printf("instance-info reply: ok, maxChars=%lu known=%d\n",
                   (unsigned long)reply->fs3eii_MaxChars, (int)reply->fs3eii_Known);
            if (reply->fs3eii_Known) {
                app->accountMaxChars = reply->fs3eii_MaxChars;
                FS3ETootView_UpdateCharCount(&app->tootView);
            }
        } else {
            printf("instance-info reply: FAILED result=%u (keeping previous limit)\n",
                   (unsigned)msg->fs3em_Result);
        }
        break;

    case FS3ENETQ_TIMELINE:
        if (msg->fs3em_Result == FS3ENETR_OK && app->tootTimeline) {
            FS3ENetTimelineReply *reply = (FS3ENetTimelineReply *)msg->fs3em_Data;
            FS3ENetStatus *statuses = (FS3ENetStatus *)(reply + 1);
            BOOL  older = (reply->fs3et_PageDirection == FS3ENETPAGE_OLDER);
            ULONG addAttr = older ? TTIMELINE_AppendPost : TTIMELINE_AddPost;
            ULONG i;

            /* This request was sent for whichever account was active back
             * when it went out -- the network process is one serialized
             * task, so it can still reply after the user has since
             * switched accounts (see accountGeneration's comment in
             * friendsh3ep.h). Applying it now would splice a stale
             * account's posts into the *new* account's channel and/or
             * re-clear timelineFetchedMask, restarting a fetch that
             * belongs to no one currently active -- discard outright
             * without touching any mask, exactly as if it was never sent;
             * the request that actually matches the current account
             * (fired by FS3EApp_SwitchAccount/SetAccount) owns this
             * channel's bookkeeping instead. */
            if (reply->fs3et_AccountGeneration != app->accountGeneration) {
                printf("timeline reply: stale generation (got %u, current %u) — discarded\n",
                       (unsigned)reply->fs3et_AccountGeneration,
                       (unsigned)app->accountGeneration);
                break;
            }

            printf("timeline reply: viewMode=%u dir=%u count=%u\n",
                   (unsigned)reply->fs3et_ViewModeBit,
                   (unsigned)reply->fs3et_PageDirection, (unsigned)reply->fs3et_Count);

            switch (reply->fs3et_PageDirection) {
                case FS3ENETPAGE_OLDER:
                    app->olderPageInFlightMask &= ~(1UL << reply->fs3et_ViewModeBit);
                    break;
                case FS3ENETPAGE_NEWER:
                    app->newerPageInFlightMask &= ~(1UL << reply->fs3et_ViewModeBit);
                    break;
                default:
                    /* Fetch is complete — clear the in-flight bit so
                     * CheckConnectionState can show the "connected" idle
                     * message instead of "Updating…", AND mark the channel
                     * as populated so FS3EApp_FetchTimeline won't fire
                     * another INITIAL fetch (and re-insert a duplicate
                     * first page) next time this channel is entered --
                     * see channelPopulatedMask's doc comment for the bug
                     * this fixes. */
                    app->timelineFetchedMask   &= ~(1UL << reply->fs3et_ViewModeBit);
                    app->channelPopulatedMask  |=  (1UL << reply->fs3et_ViewModeBit);
                    break;
            }

            /* A profile can be replaced (see FS3EApp_OpenProfile) while an
             * older/newer page fetched for the PREVIOUS profile is still
             * in flight -- this reply has no field saying "which profile"
             * it was for beyond the statuses themselves, so for Search
             * specifically, cross-check the first status's own OWNER
             * against whichever profile is CURRENTLY loaded and discard
             * the whole page if it doesn't match. accounts/{id}/statuses
             * only ever returns that one account's own statuses, so this
             * is a reliable check, not a heuristic -- but "own statuses"
             * includes boosts, and fmas_Acct is always the CONTENT's
             * author (src), not who posted it into this timeline -- for a
             * boost those differ (fmas_Acct is the ORIGINAL author,
             * fmas_BoostByAcct is the booster). Using fmas_Acct alone here
             * made every reply for a boost-only profile look "stale" and
             * get silently discarded, even on the very first, entirely
             * legitimate page -- see fmas_BoostByAcct's own comment in
             * fs3enet.h. This is exactly the kind of stale transient data
             * (a pointer into a reply block for a profile the user already
             * navigated away from) that must never be applied to whatever
             * now-different content actually occupies the Search channel,
             * so the check itself still needs to stay -- it just needs
             * the right field. The in-flight bookkeeping above still runs
             * unconditionally either way, so a stale reply doesn't leave
             * olderPageInFlightMask/etc. permanently stuck for the new
             * profile. Gated on FS3ESEARCH_USER_PROFILE specifically --
             * discussion mode (see FS3EApp_OpenDiscussion) also uses the
             * Search channel but never sets searchProfileAcct, so without
             * this guard every discussion-mode reply would look "stale"
             * (searchProfileAcct NULL) and get discarded outright. */
            if (reply->fs3et_ViewModeBit == VIEWMODE_Search &&
                app->searchMode == FS3ESEARCH_USER_PROFILE) {
                const char *statusOwnerAcct = NULL;
                if (reply->fs3et_Count > 0) {
                    statusOwnerAcct = (statuses[0].fmas_BoostByAcct && statuses[0].fmas_BoostByAcct[0])
                                    ? statuses[0].fmas_BoostByAcct : statuses[0].fmas_Acct;
                }
                if (!app->searchProfileAcct ||
                    (reply->fs3et_Count > 0 &&
                     (!statusOwnerAcct || strcmp(statusOwnerAcct, app->searchProfileAcct) != 0)))
                {
                    printf("timeline reply: stale Search page discarded (acct=%s current=%s)\n",
                           statusOwnerAcct ? statusOwnerAcct : "?",
                           app->searchProfileAcct ? app->searchProfileAcct : "(none)");
                    break;
                }
            }

            /* Mastodon always returns each page newest-first. An older
             * page (max_id) is appended below existing content, so it
             * must walk forward (newest-of-page lands right below what's
             * already there, oldest-of-page ends up at the very bottom).
             * An initial/newer page is prepended above existing content,
             * so it walks in reverse (oldest-of-page first, so the
             * overall newest -- index 0 -- ends up prepended last, at the
             * very top) -- see TTIMELINE_AddPost/AppendPost. */
            for (i = 0; i < reply->fs3et_Count; i++) {
                ULONG idx = older ? i : (reply->fs3et_Count - 1 - i);
                TTLPostSetup post;
                memset(&post, 0, sizeof(post));
                post.username    = statuses[idx].fmas_DisplayName[0]
                                   ? statuses[idx].fmas_DisplayName
                                   : statuses[idx].fmas_Acct;
                post.acct        = statuses[idx].fmas_Acct;
                /* Compares the ORIGINAL author's acct, not the booster's
                 * (fmas_BoostByAcct) -- boosting someone else's toot must
                 * not grant Modify/Delete on it. See TTLPostSetup.isOwn. */
                post.isOwn       = (statuses[idx].fmas_Acct && app->accountAcct &&
                                     !strcmp(statuses[idx].fmas_Acct, app->accountAcct));
                /* Every status in a CONTEXT_DESCENDANTS reply is, by
                 * definition, one of the discussion's replies -- see
                 * TTLPostSetup.isThreadReply / FS3EApp_OpenDiscussion. */
                post.isThreadReply = (reply->fs3et_ResponseShape == FS3ENET_TLSHAPE_CONTEXT_DESCENDANTS);
                post.body        = statuses[idx].fmas_Content;
                post.timestamp   = statuses[idx].fmas_CreatedAt;
                post.boostBy     = statuses[idx].fmas_BoostBy;
                post.boostByAcct = statuses[idx].fmas_BoostByAcct;
                post.avatarURL   = statuses[idx].fmas_AvatarURL;
                post.postId      = statuses[idx].fmas_Id;
                post.repliesCount    = statuses[idx].fmas_RepliesCount;
                post.reblogsCount    = statuses[idx].fmas_ReblogsCount;
                post.favouritesCount = statuses[idx].fmas_FavouritesCount;
                post.favourited      = statuses[idx].fmas_Favourited;
                post.reblogged       = statuses[idx].fmas_Reblogged;
                {
                    ULONG mi;
                    for (mi = 0; mi < statuses[idx].fmas_MediaCount && mi < TTL_POST_MAX_MEDIA; mi++) {
                        post.mediaUrls[mi] = statuses[idx].fmas_MediaUrls[mi];
                        post.mediaIds[mi]  = statuses[idx].fmas_MediaIds[mi];
                        switch (statuses[idx].fmas_MediaKind[mi]) {
                            case FS3ENET_MEDIAKIND_IMAGE: post.mediaKinds[mi] = TTL_MEDIA_KIND_IMAGE; break;
                            case FS3ENET_MEDIAKIND_VIDEO: post.mediaKinds[mi] = TTL_MEDIA_KIND_VIDEO; break;
                            case FS3ENET_MEDIAKIND_GIFV:  post.mediaKinds[mi] = TTL_MEDIA_KIND_GIFV;  break;
                            case FS3ENET_MEDIAKIND_AUDIO: post.mediaKinds[mi] = TTL_MEDIA_KIND_AUDIO; break;
                            default:                       post.mediaKinds[mi] = TTL_MEDIA_KIND_UNKNOWN; break;
                        }
                    }
                    post.mediaCount = statuses[idx].fmas_MediaCount;
                }
                {
                    ULONG oi;
                    for (oi = 0; oi < statuses[idx].fmas_PollOptionCount && oi < TTL_POST_MAX_POLL_OPTIONS; oi++) {
                        post.pollOptionTitles[oi] = statuses[idx].fmas_PollOptionTitles[oi];
                        post.pollOptionVotes[oi]  = statuses[idx].fmas_PollOptionVotes[oi];
                    }
                    post.pollOptionCount = statuses[idx].fmas_PollOptionCount;
                    post.pollVotesCount  = statuses[idx].fmas_PollVotesCount;
                    post.pollExpired     = statuses[idx].fmas_PollExpired;
                    post.pollMultiple    = statuses[idx].fmas_PollMultiple;
                }

                /* Trigger avatar download for this user if not already requested. */
                if (app->avatarImages &&
                    statuses[idx].fmas_AvatarURL &&
                    statuses[idx].fmas_AvatarURL[0] &&
                    statuses[idx].fmas_Acct &&
                    !AvatarImages_IsRequested(app->avatarImages,
                                              statuses[idx].fmas_Acct))
                {
                    ULONG reqSize = sizeof(FS3ENetFetchImageReq)
                                  + strlen(statuses[idx].fmas_AvatarURL) + 1
                                  + strlen(statuses[idx].fmas_Acct) + 1
                                  + strlen(FS3E_CACHE_SUBDIR_USERICONS) + 1;
                    FS3ENetFetchImageReq *req =
                        FS3ENetFetchImageReq_Alloc(statuses[idx].fmas_AvatarURL,
                                                   statuses[idx].fmas_Acct,
                                                   FS3E_CACHE_SUBDIR_USERICONS,
                                                   (BOOL)app->settings.keepBigUserIcons);
                    if (req) {
                        if (FS3EApp_NetSend(FS3ENETQ_FETCH_IMAGE, req, reqSize))
                            AvatarImages_MarkRequested(app->avatarImages,
                                                       statuses[idx].fmas_Acct);
                        else
                            FreeVec(req);
                    }
                }

                /* Trigger a thumbnail download for each attachment not
                 * already requested -- same pipeline as avatars above,
                 * just a different cache subdir/pool (see
                 * AvatarImages_IsMediaRequested and the file header
                 * comment in avatarimages.h). */
                if (app->avatarImages) {
                    ULONG mi;
                    for (mi = 0; mi < statuses[idx].fmas_MediaCount && mi < TTL_POST_MAX_MEDIA; mi++) {
                        const char *url = statuses[idx].fmas_MediaUrls[mi];
                        if (!url || !url[0]) continue;
                        /* Audio has no thumbnail to fetch -- TootTimeline
                         * draws a play button for it instead (see
                         * TTL_HOT_PLAY_AUDIO); its (fallback, no-preview)
                         * URL here is the actual media file, not a
                         * picture. */
                        if (statuses[idx].fmas_MediaKind[mi] == FS3ENET_MEDIAKIND_AUDIO) continue;
                        if (AvatarImages_IsMediaRequested(app->avatarImages, url)) continue;
                        {
                            ULONG reqSize = sizeof(FS3ENetFetchImageReq)
                                          + strlen(url) + 1
                                          + strlen(url) + 1
                                          + strlen(FS3E_CACHE_SUBDIR_THUMBNAILS) + 1;
                            FS3ENetFetchImageReq *req =
                                FS3ENetFetchImageReq_Alloc(url, url,
                                                           FS3E_CACHE_SUBDIR_THUMBNAILS,
                                                           (BOOL)app->settings.keepBigThumbnails);
                            if (req) {
                                if (FS3EApp_NetSend(FS3ENETQ_FETCH_IMAGE, req, reqSize))
                                    AvatarImages_MarkMediaRequested(app->avatarImages, url);
                                else
                                    FreeVec(req);
                            }
                        }
                    }
                }
                post.viewModeBits = (1UL << reply->fs3et_ViewModeBit);
                SetAttrs(app->tootTimeline, addAttr, (ULONG)&post, TAG_DONE);
            }

            /* Open a channel scrolled to its newest post, not wherever it
             * happened to be after AddHead's "scrollY stays fixed"
             * behavior -- only for the very first page, pagination must
             * never move the user's scroll position. */
            if (reply->fs3et_PageDirection == FS3ENETPAGE_INITIAL)
                SetAttrs(app->tootTimeline, TTIMELINE_ScrollToNewest, TRUE, TAG_DONE);

            if (CurrentMainWindow)
                RefreshGList((struct Gadget *)app->tootTimeline,
                             CurrentMainWindow, NULL, 1);
        } else if (msg->fs3em_Result != FS3ENETR_OK) {
            FS3ENetTimelineReq *req = (FS3ENetTimelineReq *)msg->fs3em_Data;
            ULONG bit = req ? (1UL << req->fs3et_ViewModeBit) : 0;

            /* Same stale-generation guard as the success branch above --
             * a failure for an account we've since switched away from
             * must not touch the current account's in-flight/error masks
             * (see accountGeneration's comment in friendsh3ep.h). */
            if (req && req->fs3et_AccountGeneration != app->accountGeneration) {
                printf("timeline reply: FAILED for stale generation (got %u, current %u) — ignored\n",
                       (unsigned)req->fs3et_AccountGeneration,
                       (unsigned)app->accountGeneration);
                break;
            }

            if (req && req->fs3et_PageDirection == FS3ENETPAGE_OLDER) {
                app->olderPageInFlightMask &= ~bit; /* allow retry next time the user hits bottom */
            } else if (req && req->fs3et_PageDirection == FS3ENETPAGE_NEWER) {
                app->newerPageInFlightMask &= ~bit; /* allow retry on next click */
            } else {
                app->timelineErrorMask   |= bit;
                app->timelineFetchedMask &= ~bit; /* allow retry on next view switch */
                app->lastTimelineResult   = msg->fs3em_Result;
            }
            printf("timeline reply: FAILED viewMode=%u dir=%u result=%u\n",
                   req ? (unsigned)req->fs3et_ViewModeBit : 0,
                   req ? (unsigned)req->fs3et_PageDirection : 0,
                   (unsigned)msg->fs3em_Result);
        }
        break;

    case FS3ENETQ_NOTIFICATIONS:
        /* Mirrors FS3ENETQ_TIMELINE's reply handling closely (same
         * accountGeneration staleness guard, same older/AddPost-vs-
         * AppendPost dispatch, same avatar/thumbnail prefetch pipeline) --
         * see that case's comments for the reasoning, not repeated here.
         * Genuinely different: no fs3et_ViewModeBit to read (this queue
         * only ever targets VIEWMODE_Notifs, see FS3ENetNotificationsReq's
         * doc comment), and each entry is a Notification, not a bare
         * Status -- FOLLOW/FOLLOW_REQUEST carry no status at all
         * (fen_HasStatus FALSE), routed to TTLNotifFollow_Class purely by
         * TTLPostSetup.notifType (see fs3etoottimeline_attribs.c's
         * TTIMELINE_AddPost/AppendPost). */
        if (msg->fs3em_Result == FS3ENETR_OK && app->tootTimeline) {
            FS3ENetNotificationsReply *reply = (FS3ENetNotificationsReply *)msg->fs3em_Data;
            FS3ENetNotification *notifs = (FS3ENetNotification *)(reply + 1);
            BOOL  older = (reply->fs3en_PageDirection == FS3ENETPAGE_OLDER);
            ULONG addAttr = older ? TTIMELINE_AppendPost : TTIMELINE_AddPost;
            ULONG bit = (1UL << VIEWMODE_Notifs);
            ULONG i;

            if (reply->fs3en_AccountGeneration != app->accountGeneration) {
                printf("notifications reply: stale generation (got %u, current %u) — discarded\n",
                       (unsigned)reply->fs3en_AccountGeneration,
                       (unsigned)app->accountGeneration);
                break;
            }

            printf("notifications reply: dir=%u count=%u\n",
                   (unsigned)reply->fs3en_PageDirection, (unsigned)reply->fs3en_Count);

            switch (reply->fs3en_PageDirection) {
                case FS3ENETPAGE_OLDER:
                    app->olderPageInFlightMask &= ~bit;
                    break;
                case FS3ENETPAGE_NEWER:
                    app->newerPageInFlightMask &= ~bit;
                    break;
                default:
                    /* See channelPopulatedMask's doc comment -- same fix
                     * as FS3ENETQ_TIMELINE's equivalent switch. */
                    app->timelineFetchedMask  &= ~bit;
                    app->channelPopulatedMask |=  bit;
                    break;
            }

            /* Same walk-order reasoning as FS3ENETQ_TIMELINE -- see its
             * comment just above its own equivalent loop. */
            for (i = 0; i < reply->fs3en_Count; i++) {
                ULONG idx = older ? i : (reply->fs3en_Count - 1 - i);
                FS3ENetNotification *n = &notifs[idx];
                TTLPostSetup post;
                memset(&post, 0, sizeof(post));

                switch (n->fen_Type) {
                    case FS3ENOTIF_MENTION:        post.notifType = TTL_NOTIF_MENTION;        break;
                    case FS3ENOTIF_REBLOG:         post.notifType = TTL_NOTIF_REBLOG;         break;
                    case FS3ENOTIF_FAVOURITE:      post.notifType = TTL_NOTIF_FAVOURITE;      break;
                    case FS3ENOTIF_FOLLOW:         post.notifType = TTL_NOTIF_FOLLOW;         break;
                    case FS3ENOTIF_FOLLOW_REQUEST: post.notifType = TTL_NOTIF_FOLLOW_REQUEST; break;
                    case FS3ENOTIF_POLL:           post.notifType = TTL_NOTIF_POLL;           break;
                    case FS3ENOTIF_UPDATE:         post.notifType = TTL_NOTIF_UPDATE;         break;
                    default:
                        /* Admin-only types (sign-up, report, severed
                         * relationships, moderation warning) and anything
                         * else this app doesn't specifically handle --
                         * skip rather than render a broken row (see
                         * FS3ENetNotifType's comment in fs3enet.h). */
                        continue;
                }
                /* The NOTIFICATION's own id, NOT the embedded status' --
                 * see TTLPostSetup.notifStatusId's comment on why this
                 * distinction matters for pagination. */
                post.postId = n->fen_Id;

                if (!n->fen_HasStatus) {
                    post.username  = n->fen_ActorDisplayName[0] ? n->fen_ActorDisplayName : n->fen_ActorAcct;
                    post.acct      = n->fen_ActorAcct;
                    post.avatarURL = n->fen_ActorAvatarURL;
                } else {
                    FS3ENetStatus *st = &n->fen_Status;
                    post.username = st->fmas_DisplayName[0] ? st->fmas_DisplayName : st->fmas_Acct;
                    post.acct      = st->fmas_Acct;
                    post.isOwn     = (st->fmas_Acct && app->accountAcct &&
                                       !strcmp(st->fmas_Acct, app->accountAcct));
                    post.body      = st->fmas_Content;
                    post.timestamp = st->fmas_CreatedAt;
                    post.avatarURL = st->fmas_AvatarURL;
                    post.repliesCount    = st->fmas_RepliesCount;
                    post.reblogsCount    = st->fmas_ReblogsCount;
                    post.favouritesCount = st->fmas_FavouritesCount;
                    post.favourited      = st->fmas_Favourited;
                    post.reblogged       = st->fmas_Reblogged;
                    post.notifActorName  = n->fen_ActorDisplayName;
                    post.notifActorAcct  = n->fen_ActorAcct;
                    post.notifStatusId   = st->fmas_Id;
                    {
                        ULONG mi;
                        for (mi = 0; mi < st->fmas_MediaCount && mi < TTL_POST_MAX_MEDIA; mi++) {
                            post.mediaUrls[mi] = st->fmas_MediaUrls[mi];
                            post.mediaIds[mi]  = st->fmas_MediaIds[mi];
                            switch (st->fmas_MediaKind[mi]) {
                                case FS3ENET_MEDIAKIND_IMAGE: post.mediaKinds[mi] = TTL_MEDIA_KIND_IMAGE; break;
                                case FS3ENET_MEDIAKIND_VIDEO: post.mediaKinds[mi] = TTL_MEDIA_KIND_VIDEO; break;
                                case FS3ENET_MEDIAKIND_GIFV:  post.mediaKinds[mi] = TTL_MEDIA_KIND_GIFV;  break;
                                case FS3ENET_MEDIAKIND_AUDIO: post.mediaKinds[mi] = TTL_MEDIA_KIND_AUDIO; break;
                                default:                       post.mediaKinds[mi] = TTL_MEDIA_KIND_UNKNOWN; break;
                            }
                        }
                        post.mediaCount = st->fmas_MediaCount;
                    }
                    {
                        ULONG oi;
                        for (oi = 0; oi < st->fmas_PollOptionCount && oi < TTL_POST_MAX_POLL_OPTIONS; oi++) {
                            post.pollOptionTitles[oi] = st->fmas_PollOptionTitles[oi];
                            post.pollOptionVotes[oi]  = st->fmas_PollOptionVotes[oi];
                        }
                        post.pollOptionCount = st->fmas_PollOptionCount;
                        post.pollVotesCount  = st->fmas_PollVotesCount;
                        post.pollExpired     = st->fmas_PollExpired;
                        post.pollMultiple    = st->fmas_PollMultiple;
                    }

                    /* Media thumbnail prefetch -- same pipeline as
                     * FS3ENETQ_TIMELINE's. */
                    if (app->avatarImages) {
                        ULONG mi;
                        for (mi = 0; mi < st->fmas_MediaCount && mi < TTL_POST_MAX_MEDIA; mi++) {
                            const char *url = st->fmas_MediaUrls[mi];
                            if (!url || !url[0]) continue;
                            if (st->fmas_MediaKind[mi] == FS3ENET_MEDIAKIND_AUDIO) continue;
                            if (AvatarImages_IsMediaRequested(app->avatarImages, url)) continue;
                            {
                                ULONG reqSize = sizeof(FS3ENetFetchImageReq)
                                              + strlen(url) + 1 + strlen(url) + 1
                                              + strlen(FS3E_CACHE_SUBDIR_THUMBNAILS) + 1;
                                FS3ENetFetchImageReq *req =
                                    FS3ENetFetchImageReq_Alloc(url, url,
                                                               FS3E_CACHE_SUBDIR_THUMBNAILS,
                                                               (BOOL)app->settings.keepBigThumbnails);
                                if (req) {
                                    if (FS3EApp_NetSend(FS3ENETQ_FETCH_IMAGE, req, reqSize))
                                        AvatarImages_MarkMediaRequested(app->avatarImages, url);
                                    else
                                        FreeVec(req);
                                }
                            }
                        }
                    }
                }

                /* Avatar download prefetch, keyed by post.acct regardless
                 * of which branch set it (toot author for status-bearing
                 * rows, the actor themselves for follow rows) -- same
                 * pipeline as FS3ENETQ_TIMELINE's. */
                if (app->avatarImages && post.avatarURL && post.avatarURL[0] &&
                    post.acct && post.acct[0] &&
                    !AvatarImages_IsRequested(app->avatarImages, post.acct))
                {
                    ULONG reqSize = sizeof(FS3ENetFetchImageReq)
                                  + strlen(post.avatarURL) + 1
                                  + strlen(post.acct) + 1
                                  + strlen(FS3E_CACHE_SUBDIR_USERICONS) + 1;
                    FS3ENetFetchImageReq *req =
                        FS3ENetFetchImageReq_Alloc(post.avatarURL, post.acct,
                                                   FS3E_CACHE_SUBDIR_USERICONS,
                                                   (BOOL)app->settings.keepBigUserIcons);
                    if (req) {
                        if (FS3EApp_NetSend(FS3ENETQ_FETCH_IMAGE, req, reqSize))
                            AvatarImages_MarkRequested(app->avatarImages, post.acct);
                        else
                            FreeVec(req);
                    }
                }

                post.viewModeBits = bit;
                SetAttrs(app->tootTimeline, addAttr, (ULONG)&post, TAG_DONE);
            }

            if (reply->fs3en_PageDirection == FS3ENETPAGE_INITIAL)
                SetAttrs(app->tootTimeline, TTIMELINE_ScrollToNewest, TRUE, TAG_DONE);

            if (CurrentMainWindow)
                RefreshGList((struct Gadget *)app->tootTimeline,
                             CurrentMainWindow, NULL, 1);
        } else if (msg->fs3em_Result != FS3ENETR_OK) {
            FS3ENetNotificationsReq *req = (FS3ENetNotificationsReq *)msg->fs3em_Data;
            ULONG bit = (1UL << VIEWMODE_Notifs);

            if (req && req->fs3en_AccountGeneration != app->accountGeneration) {
                printf("notifications reply: FAILED for stale generation (got %u, current %u) — ignored\n",
                       (unsigned)req->fs3en_AccountGeneration,
                       (unsigned)app->accountGeneration);
                break;
            }

            if (req && req->fs3en_PageDirection == FS3ENETPAGE_OLDER) {
                app->olderPageInFlightMask &= ~bit;
            } else if (req && req->fs3en_PageDirection == FS3ENETPAGE_NEWER) {
                app->newerPageInFlightMask &= ~bit;
            } else {
                app->timelineErrorMask   |= bit;
                app->timelineFetchedMask &= ~bit;
                app->lastTimelineResult   = msg->fs3em_Result;
            }
            printf("notifications reply: FAILED dir=%u result=%u\n",
                   req ? (unsigned)req->fs3en_PageDirection : 0,
                   (unsigned)msg->fs3em_Result);
        }
        break;

    case FS3ENETQ_FETCH_IMAGE:
        if (msg->fs3em_Result == FS3ENETR_OK && app->avatarImages) {
            FS3ENetFetchImageReply *reply = (FS3ENetFetchImageReply *)msg->fs3em_Data;
            BOOL isMedia = reply && reply->fs3enf_Subdir &&
                           strcmp(reply->fs3enf_Subdir, FS3E_CACHE_SUBDIR_THUMBNAILS) == 0;

            if (reply && reply->fs3enf_Key && reply->fs3enf_LocalPath &&
                app->thumbRequestPort && app->thumbReplyPort)
            {
                /* Hand the (possibly large, original-size) downloaded file
                 * to the thumbnail process instead of decoding/scaling it
                 * here -- see fs3ethumb.h. Its reply lands in
                 * FS3EApp_HandleThumbReply(), which uses fs3etm_Kind to
                 * tell the two apart again. fs3enf_CachePath/IsTemp (see
                 * their doc comments in fs3enet.h) make sure the resized
                 * thumbnail always lands under a name stable across runs
                 * and that a RAM:T download gets cleaned up afterwards,
                 * regardless of whether the original itself was kept. */
                if (isMedia) {
                    if (!AvatarImages_IsMediaThumbRequested(app->avatarImages, reply->fs3enf_Key) &&
                        FS3EThumb_Request(app->thumbRequestPort, app->thumbReplyPort,
                            reply->fs3enf_LocalPath, reply->fs3enf_Key, FS3ETHUMB_KIND_MEDIA,
                            reply->fs3enf_CachePath, reply->fs3enf_IsTemp,
                            FS3ETHUMB_MEDIA_WIDTH, FS3ETHUMB_MEDIA_HEIGHT_CAP))
                        AvatarImages_MarkMediaThumbRequested(app->avatarImages, reply->fs3enf_Key);
                } else {
                    if (!AvatarImages_IsThumbRequested(app->avatarImages, reply->fs3enf_Key) &&
                        FS3EThumb_Request(app->thumbRequestPort, app->thumbReplyPort,
                            reply->fs3enf_LocalPath, reply->fs3enf_Key, FS3ETHUMB_KIND_AVATAR,
                            reply->fs3enf_CachePath, reply->fs3enf_IsTemp,
                            FS3ETHUMB_AVATAR_SIZE, FS3ETHUMB_AVATAR_SIZE))
                        AvatarImages_MarkThumbRequested(app->avatarImages, reply->fs3enf_Key);
                }
            }
        }

        /* Same reply, independent consumer: FS3EMediaView_ShowUrl() may be
         * waiting on this exact URL (see fs3emediaview.h) -- ignores it if
         * not. No-op on failure beyond clearing its own pending/loading
         * state (msg->fs3em_Data on failure is the original request block,
         * not a reply -- fs3enf_Key sits at the same offset in both, see
         * FS3ENetFetchImageReq/Reply in fs3enet.h, so this is still safe). */
        FS3EMediaView_OnFetchReply(&app->mediaView, msg->fs3em_Result,
                                    (const FS3ENetFetchImageReply *)msg->fs3em_Data);
        break;

    case FS3ENETQ_POST_STATUS:
        if (msg->fs3em_Result == FS3ENETR_OK) {
            printf("post reply: POST_STATUS ok\n");
            FS3ETootView_Close(&app->tootView);
        } else {
            printf("post reply: POST_STATUS FAILED result=%u\n", (unsigned)msg->fs3em_Result);
        }
        break;

    case FS3ENETQ_EDIT_STATUS:
        /* Mirrors POST_STATUS's reply handling exactly -- deliberately not
         * patching the edited text into the live timeline in place (see
         * fs3etoottimeline.h's TTLPostUpdate, which only carries
         * favourited/reblogged today, no body/content field); the edited
         * text will just show correctly whenever the timeline next
         * naturally refreshes, same as a freshly-POSTed toot already
         * behaves (no local insert either). */
        if (msg->fs3em_Result == FS3ENETR_OK) {
            printf("post reply: EDIT_STATUS ok\n");
            FS3ETootView_Close(&app->tootView);
        } else {
            printf("post reply: EDIT_STATUS FAILED result=%u\n", (unsigned)msg->fs3em_Result);
        }
        break;

    case FS3ENETQ_DELETE_STATUS:
        /* Unlike EDIT_STATUS, a delete's effect on the timeline IS worth
         * reflecting immediately -- the toot is simply gone, no relayout-
         * height-cascade risk the way patching edited body text in place
         * would have (see EDIT_STATUS's comment above): TTIMELINE_RemovePost
         * unlinks+frees the post and rebuilds Y positions itself. Same
         * "may have already scrolled out of every channel" race as
         * FS3ENETQ_FAVORITE -- silently a no-op then. */
        if (msg->fs3em_Result == FS3ENETR_OK && app->tootTimeline) {
            FS3ENetDeleteStatusReply *reply = (FS3ENetDeleteStatusReply *)msg->fs3em_Data;
            printf("post reply: DELETE_STATUS ok statusId=%s\n",
                   reply && reply->fs3ed_StatusId ? reply->fs3ed_StatusId : "?");
            if (reply && reply->fs3ed_StatusId) {
                SetAttrs(app->tootTimeline, TTIMELINE_RemovePost,
                         (ULONG)reply->fs3ed_StatusId, TAG_DONE);
                if (CurrentMainWindow)
                    RefreshGList((struct Gadget *)app->tootTimeline,
                                 CurrentMainWindow, NULL, 1);
            }
        } else if (msg->fs3em_Result != FS3ENETR_OK) {
            printf("post reply: DELETE_STATUS FAILED result=%u\n", (unsigned)msg->fs3em_Result);
        }
        break;

    case FS3ENETQ_FAVORITE:
        /* The toot may have scrolled out of every channel (evicted, or the
         * user cleared/switched away) by the time this reply lands --
         * TTIMELINE_UpdatePost is a silent no-op in that case, same as any
         * other async reply racing a list change. */
        if (msg->fs3em_Result == FS3ENETR_OK && app->tootTimeline) {
            FS3ENetFavouriteReply *reply = (FS3ENetFavouriteReply *)msg->fs3em_Data;
            TTLPostUpdate upd;
            memset(&upd, 0, sizeof(upd));
            upd.postId     = reply->fs3efa_StatusId;
            upd.flags      = TTL_POSTUPD_FAVOURITED;
            upd.favourited = reply->fs3efa_Favourited;
            SetAttrs(app->tootTimeline, TTIMELINE_UpdatePost, (ULONG)&upd, TAG_DONE);
            if (CurrentMainWindow)
                RefreshGList((struct Gadget *)app->tootTimeline,
                             CurrentMainWindow, NULL, 1);
            printf("favorite reply: ok, statusId=%s favourited=%ld\n",
                   upd.postId ? upd.postId : "?", (long)upd.favourited);
        } else if (msg->fs3em_Result != FS3ENETR_OK) {
            printf("favorite reply: FAILED result=%u\n", (unsigned)msg->fs3em_Result);
        }
        break;

    case FS3ENETQ_ACCOUNT_LOOKUP:
        /* Stale-reply guard: the user may have clicked a second
         * avatar/mention before this lookup came back for the first
         * one -- only apply it if it's still the profile we last asked
         * for. */
        if (msg->fs3em_Result == FS3ENETR_OK && app->tootTimeline &&
            app->searchProfileAcct)
        {
            FS3ENetAccountLookupReply *reply = (FS3ENetAccountLookupReply *)msg->fs3em_Data;
            FS3EMastodonAccount *acc = &reply->fs3eal_Account;

            if (strcmp(app->searchProfileAcct, acc->fma_Acct) == 0) {
                TTLProfileHeaderSetup setup;
                BOOL isSelf = (app->accountId && acc->fma_Id[0] &&
                               strcmp(app->accountId, acc->fma_Id) == 0);

                if (app->searchProfileAccountId) FreeVec(app->searchProfileAccountId);
                app->searchProfileAccountId = NetStrDup(acc->fma_Id);

                memset(&setup, 0, sizeof(setup));
                setup.accountId      = acc->fma_Id;
                setup.username       = acc->fma_DisplayName[0] ? acc->fma_DisplayName : acc->fma_Acct;
                setup.acct           = acc->fma_Acct;
                setup.avatarURL      = acc->fma_AvatarURL;
                setup.bio            = acc->fma_Note;
                setup.followersCount = acc->fma_FollowersCount;
                setup.followingCount = acc->fma_FollowingCount;
                setup.following      = FALSE; /* unknown until the FS3ENETQ_RELATIONSHIP reply */
                setup.showFollow     = !isSelf;

                SetAttrs(app->tootTimeline, TTIMELINE_ShowProfile, (ULONG)&setup, TAG_DONE);

                /* Avatar fetch -- same cache/pipeline as a toot's own
                 * avatar, mirrors FS3EApp_SetAccount's own-avatar fetch
                 * block; a transparent cache hit if this acct's avatar
                 * was already fetched while scrolling a timeline. */
                if (app->avatarImages && acc->fma_Acct[0] &&
                    acc->fma_AvatarURL[0] &&
                    !AvatarImages_IsRequested(app->avatarImages, acc->fma_Acct))
                {
                    ULONG reqSize = sizeof(FS3ENetFetchImageReq)
                                  + strlen(acc->fma_AvatarURL) + 1
                                  + strlen(acc->fma_Acct) + 1
                                  + strlen(FS3E_CACHE_SUBDIR_USERICONS) + 1;
                    FS3ENetFetchImageReq *imgReq =
                        FS3ENetFetchImageReq_Alloc(acc->fma_AvatarURL, acc->fma_Acct,
                                                   FS3E_CACHE_SUBDIR_USERICONS,
                                                   (BOOL)app->settings.keepBigUserIcons);
                    if (imgReq) {
                        if (FS3EApp_NetSend(FS3ENETQ_FETCH_IMAGE, imgReq, reqSize))
                            AvatarImages_MarkRequested(app->avatarImages, acc->fma_Acct);
                        else
                            FreeVec(imgReq);
                    }
                }

                /* Relationship (skipped for a self-profile -- you can't
                 * follow yourself) + the profile's first toot page,
                 * always requested as an "older" page (even though it's
                 * the first one) so it lands via TTIMELINE_AppendPost
                 * below the header instead of the AddPost/prepend path
                 * FS3ENETPAGE_INITIAL would take -- see
                 * TTIMELINE_ShowProfile's comment in fs3etoottimeline.h. */
                if (!isSelf && app->accountAccessToken && app->accountAccessToken[0]) {
                    FS3ENetRelationshipReq *relReq = FS3ENetRelationshipReq_Alloc(
                        app->accountApiBaseUrl, app->accountAccessToken,
                        app->searchProfileAccountId);
                    if (relReq)
                        FS3EApp_NetSend(FS3ENETQ_RELATIONSHIP, relReq, sizeof(*relReq));
                }
                {
                    char tl[128];
                    if (ViewModeTimeline(VIEWMODE_Search, tl, sizeof(tl))) {
                        FS3ENetTimelineReq *tlReq = FS3ENetTimelineReq_Alloc(
                            VIEWMODE_Search, FS3ENETPAGE_OLDER,
                            app->accountGeneration, FS3ENET_TLSHAPE_ARRAY,
                            app->accountApiBaseUrl,
                            app->accountAccessToken ? app->accountAccessToken : "",
                            tl, "", "", NULL);
                        if (tlReq)
                            FS3EApp_NetSend(FS3ENETQ_TIMELINE, tlReq, sizeof(FS3ENetTimelineReq));
                    }
                }

                if (CurrentMainWindow)
                    RefreshGList((struct Gadget *)app->tootTimeline,
                                 CurrentMainWindow, NULL, 1);
                printf("account lookup reply: ok, id=%s acct=%s\n", acc->fma_Id, acc->fma_Acct);
            }
        } else if (msg->fs3em_Result != FS3ENETR_OK) {
            printf("account lookup reply: FAILED result=%u\n", (unsigned)msg->fs3em_Result);
        }
        break;

    case FS3ENETQ_RELATIONSHIP:
        if (msg->fs3em_Result == FS3ENETR_OK && app->tootTimeline &&
            app->searchProfileAccountId)
        {
            FS3ENetRelationshipReply *reply = (FS3ENetRelationshipReply *)msg->fs3em_Data;
            if (strcmp(app->searchProfileAccountId, reply->fs3erl_AccountId) == 0) {
                TTLProfileFollowUpdate upd;
                upd.accountId = reply->fs3erl_AccountId;
                upd.following = reply->fs3erl_Following;
                SetAttrs(app->tootTimeline, TTIMELINE_UpdateProfileFollow, (ULONG)&upd, TAG_DONE);
                if (CurrentMainWindow)
                    RefreshGList((struct Gadget *)app->tootTimeline,
                                 CurrentMainWindow, NULL, 1);
            }
        }
        break;

    case FS3ENETQ_FOLLOW:
        if (msg->fs3em_Result == FS3ENETR_OK && app->tootTimeline &&
            app->searchProfileAccountId)
        {
            FS3ENetFollowReply *reply = (FS3ENetFollowReply *)msg->fs3em_Data;
            if (strcmp(app->searchProfileAccountId, reply->fs3efo_AccountId) == 0) {
                TTLProfileFollowUpdate upd;
                upd.accountId = reply->fs3efo_AccountId;
                upd.following = reply->fs3efo_Following;
                SetAttrs(app->tootTimeline, TTIMELINE_UpdateProfileFollow, (ULONG)&upd, TAG_DONE);
                if (CurrentMainWindow)
                    RefreshGList((struct Gadget *)app->tootTimeline,
                                 CurrentMainWindow, NULL, 1);
            }
        } else if (msg->fs3em_Result != FS3ENETR_OK) {
            printf("follow reply: FAILED result=%u\n", (unsigned)msg->fs3em_Result);
        }
        break;

    default:
        break;
    }

    FS3EApp_CheckConnectionState();
}

/* FS3ETHUMBQ_MAKE reply -- the thumbnail process has finished decoding and
 * box-fit-scaling one avatar's or media attachment's original download
 * down to a small BMP (see fs3ethumb.h); fs3etm_Kind says which. All that
 * remains on the GUI task is a cheap direct read of that already-small
 * file's pixels; scaling to the live display size happens at draw time
 * (RgbImage_DrawScaled), not here. */
static void FS3EApp_HandleThumbReply(FS3EThumbMessage *msg)
{
    if (msg->fs3etm_Result == FS3ETHUMBR_OK && app->avatarImages &&
        msg->fs3etm_Key[0] && msg->fs3etm_ThumbPath[0])
    {
        if (msg->fs3etm_Kind == FS3ETHUMB_KIND_MEDIA) {
            AvatarImages_MediaThumbReady(app->avatarImages, msg->fs3etm_Key,
                                          msg->fs3etm_ThumbPath);
        } else {
            AvatarImages_ThumbReady(app->avatarImages, msg->fs3etm_Key,
                                     msg->fs3etm_ThumbPath);
            if (app->accountAcct && strcmp(msg->fs3etm_Key, app->accountAcct) == 0)
                FS3EApp_UpdateUserIcon();
        }
        if (app->tootTimeline) {
            /* This image just became available in the cache -- one-shot
             * event, not a poll: tell the timeline to invalidate its
             * currently rendered tiles so whichever of them drew a
             * placeholder for this avatar/thumbnail get redrawn with the
             * real image on the next render (see TTIMELINE_InvalidateImages). */
            SetAttrs(app->tootTimeline, TTIMELINE_InvalidateImages, TRUE, TAG_DONE);
        }
        if (CurrentMainWindow)
            RefreshGList((struct Gadget *)app->tootTimeline,
                         CurrentMainWindow, NULL, 1);
    }
    else if (msg->fs3etm_Result == FS3ETHUMBR_ERROR && app->avatarImages &&
             msg->fs3etm_Key[0])
    {
        /* Previously silently dropped: a failed decode left the entry's
         * .requested flag latched forever with no distinction from
         * "still pending", so AvatarImages_Get(Media)() returned NULL
         * forever and the placeholder redrew with nothing to explain why.
         * Latch it explicitly instead, with the sniffed format (see
         * FS3EThumb_HandleMake/BmImage_SniffFormat) so the tile renderer
         * can show e.g. "webp" instead of a bare box. */
        UBYTE fmt = (UBYTE)msg->fs3etm_DetectedFormat;
        if (msg->fs3etm_Kind == FS3ETHUMB_KIND_MEDIA)
            AvatarImages_MarkMediaFailed(app->avatarImages, msg->fs3etm_Key, fmt);
        else
            AvatarImages_MarkFailed(app->avatarImages, msg->fs3etm_Key, fmt);

        if (app->tootTimeline)
            SetAttrs(app->tootTimeline, TTIMELINE_InvalidateImages, TRUE, TAG_DONE);
        if (CurrentMainWindow)
            RefreshGList((struct Gadget *)app->tootTimeline,
                         CurrentMainWindow, NULL, 1);
    }
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
    ULONG oldViewMode;
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

    if(!app || !app->searchWordEditor) return;
    SetAttrs(app->searchWordEditor, UTED_LineTextToGet, 0, TAG_END);
    GetAttr(UTED_LineUTF8TextBuffer, app->searchWordEditor, (ULONG *)&text);
    if(!text || *text == 0) return;

    if (text && strchr(text, '@')) {
        /* "name@server"-shaped text -> user
         * search: a different kind of request,
         * returning an account instead of a
         * list of toots. Not wired up yet. */
        printf("search line: user search (TODO): %s\n", text);
    } else if (text && text[0]) {
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

    FS3ESettings_Load(&app->settings);

    if (!FS3EMsg_Init()) cleanexit("Can't create BOOPSI message target");

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

    app->avatarImages = AvatarImages_Create();

 printf("FS3ENet_Start\n");
    /* --- Network process ------------------------------------------------ */
    app->netReplyPort = CreateMsgPort();
    if (!app->netReplyPort) cleanexit("Can't create network reply port");

    app->netRequestPort = FS3ENet_Start(app->settings.cachePath);
    if (!app->netRequestPort)
        printf("FriendSh3ep: network process failed - continuing without\n");

    /* --- Thumbnail process ------------------------------------------------ */
    app->thumbReplyPort = CreateMsgPort();
    if (!app->thumbReplyPort) cleanexit("Can't create thumbnail reply port");

    app->thumbRequestPort = FS3EThumb_Start();
    if (!app->thumbRequestPort)
        printf("FriendSh3ep: thumbnail process failed - continuing without\n");

    /* Debug: print the derived machine key unconditionally, even before any
     * account.dat exists to trigger it lazily via FS3EApp_MachineKey() --
     * so it's visible on every launch for comparing across machines. */
    FS3EApp_MachineKey();

    /* Try to load saved credentials (and the rest of the accounts list --
     * see FS3EApp_LoadAccount); timeline fetch fires later in setViewMode */
    FS3EApp_LoadAccount();
    FS3EApp_BackfillAccountId();
 printf("FS3ENet_Start end\n");
 printf("windows creates\n");
    /* --- Classic BOOPSI sub-windows ------------------------------------- */
    if (!FS3ELoginView_Create(&app->loginView,  app->style.dcNormal))
        cleanexit("Can't create login view");

    /* Credentials already confirmed (loaded from account.dat above) --
     * leave server/user entries empty rather than pre-filling them, so
     * there's nothing to accidentally resubmit and trigger a fresh
     * re-auth (see GID_LOGIN_LOGIN_BUTTON's "already connected" branch). */
    if (app->accountApiBaseUrl)
        FS3ELoginView_ClearFields(&app->loginView);

    FS3EApp_RefreshLoginAccountsList();



    if (!FS3ETootView_Create(&app->tootView, app->style.dcNormal))
        cleanexit("Can't create toot view");

    if (!FS3EEmojiBoxWindow_Create(&app->emojiBoxWindow, app->style.dcNormal))
        cleanexit("Can't create emoji box");

    if (!FS3EThemeView_Create(&app->themeView, LOC(MSG_THEMEV_TITLE)))
        cleanexit("Can't create theme view");

    if (!FS3ESettingsView_Create(&app->settingsView, LOC(MSG_SETTINGSV_TITLE)))
        cleanexit("Can't create settings view");

    FS3EMediaView_Init(&app->mediaView);


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
 //printf("p9bm:%08x\n",(int)app->style.bt1Patch9.img.bitmap);
    app->titlebar_settingsBtn = (Object *)NewObject(UniButtonP9Class, NULL,
        ICA_TARGET, (ULONG)TargetInstance,
        GA_ID,                  GID_TITLEBAR_SETTINGS,
        GA_Text,                (ULONG)"\xE2\x9A\x99 Settings",
        UBTP9_URPDrawContext,   (ULONG)app->buttonDC,
        UBTP9_BevelStyle,       BVS_NONE, //BVS_NONE,
        TAG_END);

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
 printf("create ttl &app->style:%08x\n",(int)&app->style);
    app->tootTimeline = (Object *)NewObject(TootTimelineClass, NULL,
        TTIMELINE_Style, (ULONG)(&app->style),
        TTIMELINE_DpiHeight, (ULONG)dpiH,
        ICA_TARGET, (ULONG)TargetInstance,
        GA_ID,      GID_TTIMELINE,
        GA_BackFill, NULL,
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

    app->searchBarLayout = (Object *)NewObject(SearchBarLayoutClass, NULL,
        LAYOUT_BevelStyle, BVS_NONE,
        LAYOUT_SpaceOuter, FALSE,
        LAYOUT_SpaceInner, FALSE,
        LAYOUT_BackFill,   NULL,
        SBLAYOUT_Visible,  FALSE,
        /* children in required order (see fs3esearchbar.h) */
        LAYOUT_AddChild,   (ULONG)app->searchWordEditor,
        LAYOUT_AddChild,   (ULONG)app->tootTimeline,
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

#define reflags_bodyEditor    2
#define reflags_tootTimeLine    4
#define reflags_searchworduted 8
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
            ULONG emojiSig = FS3EEmojiBoxWindow_GetSignalMask(&app->emojiBoxWindow);
            ULONG mediaSig = FS3EMediaView_GetSignalMask(&app->mediaView);

            waitedSignals = winsignal | loginSig | tootSig | themeSig | settingsSig | emojiSig | mediaSig |
                (1L << app->app_port->mp_SigBit) |
                (app->netReplyPort ? (1L << app->netReplyPort->mp_SigBit) : 0) |
                (app->thumbReplyPort ? (1L << app->thumbReplyPort->mp_SigBit) : 0) |
                (SIPCPort ? (1L << SIPCPort->mp_SigBit) : 0) |
                SIGBREAKF_CTRL_C |
                SIGBREAKF_CTRL_F;

            currentSignals = Wait(waitedSignals);

            if (currentSignals & SIGBREAKF_CTRL_C) exit(0);

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
                            break;
                        case GID_TITLEBAR_NEWTOOT:
                        printf("GID_TITLEBAR_NEWTOOT\n");
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

                        /* ---- Login sub-window: phase 1 ---- */
                        case GID_LOGIN_LOGIN_BUTTON:
                        {
                            ptag = FindTagItem(GA_Selected, msg);
                            if (ptag && ptag->ti_Data)  /* when push button down (selected true) */
                            {
                                char serverBuf[256];

                                printf(" **** GOT GID_LOGIN_LOGIN_BUTTON\n");
                                const char *server = NormalizeServerUrl(
                                    FS3ELoginView_GetANSIServer(&app->loginView),
                                    serverBuf, sizeof(serverBuf));
                                /* Starting a fresh OAuth flow -- with multi-account
                                 * support this means "add another account", not
                                 * "log out of the current one", so only the interim
                                 * login state (any half-finished previous flow) is
                                 * discarded here; the currently active account
                                 * (app->accountXXX) is left untouched. Switching to
                                 * an already-known account is the acclistGroup
                                 * listbrowser's job (FS3EApp_SwitchAccount), not
                                 * this button. */
                                FS3EApp_FreeLoginState(); /* also sets loginPhase = IDLE */
                                if (server && server[0]) {
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
                            }
                            break;
                        }

                        /* ---- Login sub-window: phase 2 ---- */
                        case GID_LOGIN_SUBMIT_CODE_BUTTON:
                        {
                            ptag = FindTagItem(GA_Selected, msg);
                            if (ptag && ptag->ti_Data)  /* when push button down (selected true) */
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
                            }
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
                                                inReplyToId);
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
                                    BOOL hotSpotFavourited = FALSE;
                                    BOOL hotSpotFollowing = FALSE;

                                    ptag = FindTagItem(TTIMELINE_LastHotSpotString, msg);
                                    if(ptag) hotSpotString  =(const char *)ptag->ti_Data;

                                    ptag = FindTagItem(TTIMELINE_LastHotSpotPostId, msg);
                                    if(ptag) hotSpotId  =(const char *)ptag->ti_Data;

                                    ptag = FindTagItem(TTIMELINE_LastHotSpotMediaIds, msg);
                                    if(ptag) hotSpotMediaIds = (const char *)ptag->ti_Data;

                                    ptag = FindTagItem(TTIMELINE_LastHotSpotFavourited, msg);
                                    if(ptag) hotSpotFavourited = (BOOL)ptag->ti_Data;

                                    ptag = FindTagItem(TTIMELINE_LastHotSpotFollowing, msg);
                                    if(ptag) hotSpotFollowing = (BOOL)ptag->ti_Data;

                                 printf("Main process TTL_HotSpotNotify type:%lu str=%s id=%s\n",
                                        hotSpotType,
                                        hotSpotString ? hotSpotString : "(null)",
                                        hotSpotId     ? hotSpotId     : "(null)");

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
                                             * so the bar is visible, and
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
                                             * Opens/reuses the "FriendSh3ep
                                             * Media" viewer window. */
                                            FS3EMediaView_ShowUrl(&app->mediaView, hotSpotString);
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
                                            printf("Play audio requested: %s\n",
                                                   hotSpotString ? hotSpotString : "(null)");
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
                                            printf("main: TTL_HOT_MODIFY hotSpotMediaIds=%s\n",
                                                   hotSpotMediaIds ? hotSpotMediaIds : "(null)");
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
 printf("exitclose2\n");
        if (app->window_obj)
        {
            FS3EMenu_Close(&app->menu, CurrentMainWindow);
            FS3ESettings_Save(&app->settings);
            FS3EMain_Close(&app->mainwindow, app->window_obj, 0);
            /* Cascades: mainlayout → titleBarLayout/navBarLayout/placeholder
             * → all UniButtonP9 children. */
            DisposeObject(app->window_obj);
        }
 printf("exitclose3\n");
        /* Free private classes AFTER all objects using them are disposed. */
        TootTimeline_Exit();
        SearchBarLayout_Exit();
        NavBarLayout_Exit();
        TitleBarLayout_Exit();
        UniButtonP9_Exit();
        UniButtonBGBM_Exit();
 printf("exitclose4\n");
        /* Release shared DCs after all gadgets using them are disposed. */
        if (app->buttonDC) { URPDC_Release(app->buttonDC); app->buttonDC = NULL; }

        if (app->avatarImages) {
            AvatarImages_Dispose(app->avatarImages);
            app->avatarImages = NULL;
        }

 printf("exitclose5\n");
        FS3EStyle_ReleaseDrawContexts(&app->style);
        FS3EStyle_FreeThemeImages(&app->style);
 printf("exitclose6\n");
wait2sec();
        if (BevelBase)  { CloseLibrary(BevelBase);  BevelBase  = NULL; }
        if (BitMapBase) { CloseLibrary(BitMapBase); BitMapBase = NULL; }

 printf("exitclose: about to FS3ENet_Stop, netRequestPort=%08lx\n", (unsigned long)app->netRequestPort);
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
 printf("exitclose: calling FS3ENet_Stop (blocks until net process replies shutdown)...\n");
wait2sec();
                FS3ENet_Stop(app->netRequestPort, stopReplyPort);
 printf("exitclose: FS3ENet_Stop returned\n");
wait2sec();
                DeleteMsgPort(stopReplyPort);
            }
        }
 printf("exitclose: draining netReplyPort...\n");
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
 printf("exitclose: net side done, about to FS3EThumb_Stop, thumbRequestPort=%08lx\n",
        (unsigned long)app->thumbRequestPort);
wait2sec();
        if (app->thumbRequestPort)
        {
            /* Same dedicated-port reasoning as netRequestPort above. */
            struct MsgPort *stopReplyPort = CreateMsgPort();
            if (stopReplyPort) {
 printf("exitclose: calling FS3EThumb_Stop (blocks until thumb process replies shutdown)...\n");
wait2sec();
                FS3EThumb_Stop(app->thumbRequestPort, stopReplyPort);
 printf("exitclose: FS3EThumb_Stop returned\n");
wait2sec();
                DeleteMsgPort(stopReplyPort);
            }
        }
 printf("exitclose: draining thumbReplyPort...\n");
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
 printf("exitclose: thumb side done\n");
wait2sec();
        FS3EApp_FreeLoginState();
        FS3EApp_FreeAccount();
        if (app->searchProfileAcct)      { FreeVec(app->searchProfileAcct);      app->searchProfileAcct      = NULL; }
        if (app->searchProfileAccountId) { FreeVec(app->searchProfileAccountId); app->searchProfileAccountId = NULL; }
        if (app->searchDiscussionStatusId) { FreeVec(app->searchDiscussionStatusId); app->searchDiscussionStatusId = NULL; }

 printf("exitclose7\n");
wait2sec();
        FS3EMsg_Close();
 printf("exitclose8: FS3EMsg_Close returned\n");
wait2sec();
        if (app->app_port)
            DeleteMsgPort(app->app_port);

        FS3ESettings_Close(&app->settings);
        FreeVec(app);
        app = NULL;
    }
 printf("exitclose9: leaving the if(app) block\n");
wait2sec();
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
