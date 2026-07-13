#ifndef FRIENDSH3EP_H
#define FRIENDSH3EP_H

/*
 * FriendSh3ep - main application header: struct App and global state.
 * See ARCHITECTURE.md for the overall design.
 */

#include <exec/types.h>
#include <exec/ports.h>
#include <intuition/classusr.h>
#include <libraries/utf8rastport.h>

#include "fs3eboopsimainwindow.h"
#include "fs3eloginview.h"
#include "fs3etootview.h"
#include "fs3ethemeview.h"
#include "fs3esettingsview.h"
#include "fs3eemojibox.h"
#include "fs3estyle.h"
#include "fs3emenu.h"
#include "fs3esettings.h"
#include "avatarimages.h"

#define FRIENDSH3EP_VERSION "0.2"

/* Login two-phase OAuth state machine */
typedef enum {
    FS3ELOGIN_IDLE = 0,
    FS3ELOGIN_WAITING_START,   /* LOGIN_START sent, awaiting async reply */
    FS3ELOGIN_WAITING_CODE,    /* authorize URL shown; waiting for code paste */
    FS3ELOGIN_WAITING_FINISH,  /* LOGIN_FINISH sent, awaiting async reply */
    FS3ELOGIN_DONE
} FS3ELoginPhase;

typedef enum {
    VIEWMODE_User    = 0,
    VIEWMODE_Home,
    VIEWMODE_Local,
    VIEWMODE_Fed,
    VIEWMODE_Search,
    VIEWMODE_Notifs,
    VIEWMODE_Bookmarks,
    VIEWMODE_News,
    VIEWMODE_NumberOf
} fs3eViewMode;

/* What the Search channel (VIEWMODE_Search) is currently showing --
 * TootTimeline itself only knows "does this channel have a profile
 * header" (see TTLChannel.headerPost); this is the app-level policy of
 * *why*, driving which requests to fire when Search is (re-)entered.
 * Word search (see todo.txt) is a future sub-mode of this same channel. */
typedef enum {
    FS3ESEARCH_NONE = 0,
    FS3ESEARCH_USER_PROFILE,
    FS3ESEARCH_DISCUSSION /* see FS3EApp_OpenDiscussion(), searchDiscussionStatusId */
} FS3ESearchMode;

/* Max number of accounts kept in App.accounts[] / persisted to
 * accounts.dat (see FS3EApp_SwitchAccount and fs3eloginview.c's
 * acclistGroup). Plenty for a personal multi-instance/multi-account
 * client; raise if that ever isn't true. */
#define FS3E_MAX_ACCOUNTS 8

/* One known/logged account -- same shape as the App.accountXXX fields
 * below, which always mirror accounts[N] for whichever one is currently
 * active. All fields NetStrDup'd (AllocVec'd), NULL when unset. */
typedef struct FS3EAccount {
    char *apiBaseUrl;
    char *accessToken;
    char *displayName;
    char *acct;
    char *avatarURL;
    char *accountId;
} FS3EAccount;

/* Application struct: holds every persistent BOOPSI object and IPC handle. */
struct App {
    Object *window_obj;        /* window.class object (persistent) */
    struct MsgPort *app_port;  /* needed to keep receiving messages while iconified */

    FS3EMainWindow mainwindow;
    FS3EMenu       menu;
    FS3ESettings   settings;

    /* Root vertical layout (Part A + B + C) */
    Object *mainlayout;

    /* Alternate window position for the altpos button.
     * Swapped with the current position on each button press via ChangeWindowBox.
     * altWinWidth == 0 means not yet set (first press only saves, doesn't move). */
    LONG altWinLeft, altWinTop, altWinWidth, altWinHeight;

    /* Part A: title bar (TitleBarLayoutClass).
     * Children owned by titleBarLayout and disposed with it. */
    Object *titleBarLayout;
    Object *titlebar_closeBtn;       /* GID_TITLEBAR_CLOSE   */
    Object *titlebar_iconifyBtn;     /* GID_TITLEBAR_ICONIFY */
    Object *titlebar_altposBtn;      /* GID_TITLEBAR_ALTPOS  */
    Object *titlebar_depthBtn;       /* GID_TITLEBAR_DEPTH   */
    /* Row-2 user icon is drawn directly by TitleBarLayout_OnRender() from
     * TBLAYOUT_AvatarImages/TBLAYOUT_AccountAcct -- no gadget/image object
     * of its own anymore (see fs3etitlebar.c's file header comment). */
    Object *titlebar_settingsBtn;       /*  */
    Object *titlebar_accountBtn;       /*  */
    Object *titlebar_newtootBtn;       /*  */

    /* Part B: navigation bar (NavBarLayoutClass).
     * nav_btns[0..7] correspond to GID_NAV_HOME..GID_NAV_ACCOUNTS. */
    Object *navBarLayout;
    Object *nav_btns[8];

    /* Part C: toot timeline (TootTimelineClass) */
    Object *tootTimeline;

    /* Shared draw context for all UniButtonP9 buttons */
    struct URPDrawContext *buttonDC;

    /* Classic BOOPSI sub-windows, opened on demand */
    FS3ELoginView  loginView;
    FS3ETootView   tootView;
    FS3EThemeView  themeView;
    FS3ESettingsView settingsView;
    FS3EEmojiBoxWindow emojiBoxWindow;

    /* fs3enet ports: requestPort send-only; replyPort receives async replies */
    struct MsgPort *netRequestPort;
    struct MsgPort *netReplyPort;

    /* fs3ethumb ports: same shape as the fs3enet ports above, but talking
     * to the thumbnail process (see fs3ethumb.h) instead of the network
     * process. */
    struct MsgPort *thumbRequestPort;
    struct MsgPort *thumbReplyPort;

    /* Login two-phase state machine (FS3ELoginPhase) */
    ULONG  loginPhase;
    char  *loginApiBaseUrl;    /* saved between LOGIN_START reply and LOGIN_FINISH send */
    char  *loginClientId;
    char  *loginClientSecret;

    /* Logged-in account — all NULL when not logged in */
    char  *accountApiBaseUrl;
    char  *accountAccessToken;
    char  *accountDisplayName;
    char  *accountAcct;
    char  *accountAvatarURL;
    char  *accountId;         /* Mastodon numeric account id (fma_Id); used
                                * for VIEWMODE_User's accounts/{id}/statuses
                                * fetch (see ViewModeTimeline). */

    /* Active account's per-toot character limit (instance-specific --
     * varies a lot across the Fediverse, not just Mastodon's own 500
     * default). 0 means "not confirmed by the server yet" (no account
     * connected, or its FS3ENETQ_INSTANCE_INFO reply hasn't arrived/came
     * back with fs3eii_Known FALSE) -- fs3etootview.c's charCountLabel
     * shows "Max: -" in that case rather than presenting a guessed number
     * as if it were real. Reset to 0 on every real account change (see
     * FS3EApp_SetAccount) and only ever set to a nonzero value from a
     * FS3ENetInstanceInfoReply with fs3eii_Known TRUE.
     * See FS3EMastodon_GetInstanceInfo in network_fs3e/fs3enet_mastodon.h. */
    ULONG  accountMaxChars;

    /* Bumped by FS3EApp_SetAccount() every time the active account changes
     * (fresh login, saved-account load, or a multi-account switch).
     * Stamped into every FS3ENETQ_TIMELINE request's fs3et_AccountGeneration
     * and echoed back unchanged in the reply -- the network process is a
     * single serialized task, so a request sent for the outgoing account
     * right before a switch can still reply afterwards; comparing this
     * against the reply's stamp is how FS3EApp_HandleNetReply tells that
     * stale reply apart from a fresh one for the now-active account and
     * discards it outright, instead of intermixing two accounts' posts or
     * re-clearing timelineFetchedMask and causing a request/reply ping-pong
     * between the two accounts. */
    ULONG  accountGeneration;

    /* Every known/logged account (accounts[0..accountCount-1]), persisted
     * to <userDataPath>/accounts.dat -- lets the accounts list in
     * fs3eloginview.c's acclistGroup offer one-click switching without a
     * fresh OAuth round-trip. The currently active account (accountXXX
     * fields above) is always also present in this array once logged in
     * -- see FS3EApp_UpsertAccountsList/FS3EApp_SwitchAccount. */
    FS3EAccount accounts[FS3E_MAX_ACCOUNTS];
    ULONG       accountCount;

    /* Bitmask of VIEWMODE channels with an INITIAL fetch currently in
     * flight -- set the moment the request is sent, cleared the moment
     * its reply (success or failure) lands. Purely a "busy" indicator for
     * FS3EApp_CheckConnectionState's "Updating..." text; does NOT mean
     * "already has data" (see channelPopulatedMask below for that --
     * conflating the two here was a real bug: clearing this bit on
     * success used to also be the only thing gating FS3EApp_FetchTimeline
     * against re-fetching, so switching away from a channel and back
     * after its reply had already landed re-fired the initial fetch and
     * re-inserted the same first page on top of itself via AddPost). */
    ULONG  timelineFetchedMask;

    /* Bitmask of VIEWMODE channels that have received their first page at
     * least once this session -- set only on a successful INITIAL fetch,
     * never cleared except on account switch/logout (see the reset next
     * to timelineFetchedMask=0 in those paths). This is what
     * FS3EApp_FetchTimeline actually checks before firing an initial
     * fetch, so re-entering an already-populated channel (e.g. switching
     * views and back) doesn't insert a duplicate first page -- see
     * timelineFetchedMask's comment for the bug this fixes. */
    ULONG  channelPopulatedMask;

    /* Bitmask of channels whose last fetch returned an error.
     * Cleared for a channel when a new fetch starts for it. */
    ULONG  timelineErrorMask;
    /* FS3ENetResult of the last timeline fetch that failed (for message text). */
    ULONG  lastTimelineResult;

    /* Bitmasks of channels with an older/newer pagination page currently
     * in flight (see FS3EApp_FetchTimelinePage) -- separate from
     * timelineFetchedMask, which only ever guards the one initial fetch
     * per channel. Bit i set → do not start another page fetch in that
     * direction for channel i until the current one replies. */
    ULONG  olderPageInFlightMask;
    ULONG  newerPageInFlightMask;

    /* Avatar bitmap cache — one scaled BmImage per @user@instance */
    struct AvatarImages *avatarImages;

    /* Color theme — pens obtained from the current screen's ColorMap */
    FS3EStyle style;

    /* enum fs3eViewMode */
    ULONG     viewMode;

    /* Search channel (VIEWMODE_Search) profile-view state -- see
     * FS3ESearchMode and FS3EApp_OpenProfile(). searchProfileAcct is set
     * the moment a profile is requested (before the account id is even
     * known) so an FS3ENETQ_ACCOUNT_LOOKUP/RELATIONSHIP/FOLLOW reply can
     * be checked against it and discarded if stale (the user opened a
     * different profile before this one's reply arrived).
     * searchProfileAccountId is only set once the lookup reply lands;
     * ViewModeTimeline's VIEWMODE_Search case needs it for the profile's
     * own accounts/{id}/statuses fetch, same as accountId above does for
     * VIEWMODE_User. */
    ULONG  searchMode;            /* FS3ESearchMode */
    char  *searchProfileAcct;
    char  *searchProfileAccountId;

    /* Search channel (VIEWMODE_Search) discussion-view state -- see
     * FS3EApp_OpenDiscussion(). Set the moment a discussion is requested,
     * same "before either reply lands" timing as searchProfileAcct above. */
    char  *searchDiscussionStatusId;
};

extern struct App *app;

/* Print pmessage (if non-NULL) and exit(0); runs exitclose() via atexit(). */
void cleanexit(const char *pmessage);

void fs3e_setViewMode(ULONG viewMode);
#endif /* FRIENDSH3EP_H */
