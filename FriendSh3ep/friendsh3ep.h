#ifndef FRIENDSH3EP_H
#define FRIENDSH3EP_H

/*
 * FriendSh3ep - main application header: struct App and global state.
 *
 * Scope (see friendsh3ep.c's own file header for the fuller rationale):
 * this header exists to expose struct App/FS3EAccount/the enums and to let
 * other files reach app-wide state -- it is not the place to declare a new
 * subsystem's functions. A subsystem split out of friendsh3ep.c (requests,
 * accounts, ...) gets its own <name>.h instead.
 *
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
#include "fs3enetworkview.h"
#include "fs3eemojibox.h"
#include "fs3emediaview.h"
#include "fs3estyle.h"
#include "fs3emenu.h"
#include "fs3esettings.h"
#include "avatarimages.h"

#define FRIENDSH3EP_VERSION "0.8b"

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
 * *why*, driving which requests to fire when Search is (re-)entered. */
typedef enum {
    FS3ESEARCH_NONE = 0,
    FS3ESEARCH_USER_PROFILE,
    FS3ESEARCH_DISCUSSION, /* see FS3EApp_OpenDiscussion(), searchDiscussionStatusId */
    FS3ESEARCH_WORD,       /* see FS3EApp_SearchWord(); word/hashtag search, both
                             * go through the same FS3ENET_TLSHAPE_SEARCH_STATUSES
                             * request -- flat status list, no profile header,
                             * same as FS3ESEARCH_DISCUSSION */
    FS3ESEARCH_ACCOUNT,    /* see FS3EApp_SearchAccount(); fuzzy account search --
                             * flat list of TTLAccountRow_Class rows, no profile header */
    FS3ESEARCH_FOLLOWERS,  /* see FS3EApp_ShowFollowers(); searchProfileAccountId's
                             * followers, same flat account-row list */
    FS3ESEARCH_FOLLOWING   /* see FS3EApp_ShowFollowing(); same, but following */
} FS3ESearchMode;

/* searchWordTypeChooser's two entries (CHOOSER_Active), read by
 * StartSearchFromLine() to decide whether Return dispatches a word search
 * or an account search. */
typedef enum {
    FS3ESEARCHTYPE_WORD = 0,
    FS3ESEARCHTYPE_PEOPLE
} FS3ESearchTypeChoice;

/* One saved "search view configuration" for the Back-navigation stack
 * (see App.searchStack/searchStackDepth and FS3EApp_SearchGoBack() in
 * fs3erequests.c) -- a snapshot of whichever of the identity/query fields
 * below `mode` actually needs, taken right before that mode's state gets
 * overwritten by navigating to something else. Every field not relevant
 * to `mode` is left NULL/0, same "not every field matters for every kind
 * of row" convention TTLPostSetup already uses. */
typedef struct {
    ULONG  mode;               /* FS3ESearchMode being left */
    char  *profileAcct;        /* NetStrDup copy, or NULL */
    char  *profileAccountId;
    char  *discussionStatusId;
    char  *queryText;          /* WORD/ACCOUNT modes only */
    ULONG  queryChooser;       /* FS3ESearchTypeChoice, WORD/ACCOUNT only */
    LONG   scrollY;            /* TTIMELINE_ScrollY at the moment of leaving */
} FS3ESearchStackEntry;

#define FS3E_SEARCH_STACK_MAX 4

/* Max number of accounts kept in App.accounts[] / persisted to
 * accounts.dat (see FS3EApp_SwitchAccount and fs3eloginview.c's
 * acclistGroup). Was 8 (a personal multi-instance/multi-account budget);
 * raised once anonymous connections (see FS3EACCOUNT_ANON_ACCT in
 * fs3eaccounts.h) turned this same list into a "servers I like to browse"
 * list too, not just real logins -- each FS3EAccount is just 6 small
 * AllocVec'd pointers, and the one fixed-size array sized off this
 * (FS3ELoginAccountRow rows[FS3E_MAX_ACCOUNTS], a stack-local in
 * FS3EApp_RefreshLoginAccountsList) is trivially small even at this size. */
#define FS3E_MAX_ACCOUNTS 128

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
//olde    Object *titlebar_settingsBtn;       /*  */
    Object *titlebar_accountBtn;       /*  */
    Object *titlebar_newtootBtn;       /*  */

    /* Part B: navigation bar (NavBarLayoutClass).
     * nav_btns[0..7] correspond to GID_NAV_HOME..GID_NAV_ACCOUNTS. */
    Object *navBarLayout;
    Object *nav_btns[8];

    /* Part C: search editor + toot timeline (SearchBarLayoutClass).
     * searchBarLayout wraps searchWordEditor (one-line UniTextEditor,
     * shown only in VIEWMODE_Search -- see fs3e_setViewMode),
     * searchWordTypeChooser ("Word"/"People" popup) and searchBackButton
     * (far right of the same row, see FS3EApp_SearchGoBack) above
     * tootTimeline (TootTimelineClass); searchBarLayout is what actually
     * gets added to mainlayout, not tootTimeline directly. */
    Object *searchBarLayout;
    Object *searchWordEditor;
    int     searchWordEditor_activateNextRound;
    Object *searchWordTypeChooser;
    struct List   searchWordTypeList;
    struct Node  *searchWordTypeNodes[2]; /* 0="Word", 1="People" -- see FS3ESearchTypeChoice */
    Object *searchBackButton;
    Object *tootTimeline;

    /* Shared draw context for all UniButtonP9 buttons */
    struct URPDrawContext *buttonDC;

    /* Classic BOOPSI sub-windows, opened on demand */
    FS3ELoginView  loginView;
    FS3ETootView   tootView;
    FS3EThemeView  themeView;
    FS3ESettingsView settingsView;
    FS3ENetworkView networkView;
    FS3EEmojiBoxWindow emojiBoxWindow;

    /* window.class/layout.gadget full-size media viewer (see
     * fs3emediaview.h), opened by clicking a toot's media preview. */
    FS3EMediaView  mediaView;

    /* fs3enet ports: requestPort send-only; replyPort receives async replies */
    struct MsgPort *netRequestPort;
    struct MsgPort *netReplyPort;

    /* fs3ethumb ports: same shape as the fs3enet ports above, but talking
     * to the thumbnail process (see fs3ethumb.h) instead of the network
     * process. */
    struct MsgPort *thumbRequestPort;
    struct MsgPort *thumbReplyPort;

    /* fs3eaudio ports: same shape again, talking to the MP3/AHI playback
     * process (see fs3eaudio.h). Generic player only for now -- nothing
     * yet requests FS3EAUDIOQ_PLAY (no toot-audio UI wiring), so
     * audioReplyPort is only drained to avoid leaking messages. */
    struct MsgPort *audioRequestPort;
    struct MsgPort *audioReplyPort;

    /* Login two-phase state machine (FS3ELoginPhase) */
    ULONG  loginPhase;
    char  *loginApiBaseUrl;    /* saved between LOGIN_START reply and LOGIN_FINISH send */
    char  *loginClientId;
    char  *loginClientSecret;

    /* GID_TOOT_SEND_BUTTON's own two-phase state, same shape as login above:
     * TRUE while an FS3ENETQ_UPLOAD_MEDIA is in flight for the compose
     * window's current attachment, between the click and the follow-up
     * FS3EApp_SubmitToot() the reply triggers (see fs3erequests.c's
     * FS3ENETQ_UPLOAD_MEDIA case). pendingToot* save what GID_TOOT_SEND_
     * BUTTON already read off app->tootView's gadgets at click time, so
     * FS3EApp_SubmitToot() doesn't have to re-read a window that might have
     * been edited (or even closed) while the upload was in flight.
     * pendingTootBody is AllocVec'd (FS3ETootView_GetUTF8Body()'s result,
     * ownership moved here), freed by whichever of FS3EApp_SubmitToot() or
     * the UPLOAD_MEDIA failure path ends up handling it. tootUploadPending
     * also gates FS3ETootView_UpdateSendEnabled() so the Toot button can't
     * be double-clicked mid-upload. */
    BOOL   tootUploadPending;
    char  *pendingTootBody;
    LONG   pendingTootVisibility;
    LONG   pendingTootQuotePolicy;

    /* Logged-in account — all NULL when not logged in */
    char  *accountApiBaseUrl;
    char  *accountAccessToken;
    char  *accountDisplayName;
    char  *accountAcct;
    char  *accountAvatarURL;
    char  *accountId;         /* Mastodon numeric account id (fma_Id); used
                                * for VIEWMODE_User's accounts/{id}/statuses
                                * fetch (see ViewModeTimeline). */

    /* TRUE once a FS3ENETQ_VERIFY_ACCOUNT reply comes back FS3ENETR_AUTH_
     * ERROR -- the server positively rejected accountAccessToken (a real
     * HTTP response came back, just not a valid account -- see
     * FS3EMastodon_VerifyCredentials' outRejected param), as opposed to
     * merely being unable to reach it at all (FS3ENETR_NETWORK_ERROR,
     * which leaves this alone -- inconclusive, could just be offline).
     * Set by FS3EApp_VerifyStoredAccount()'s reply in fs3erequests.c
     * (fired once at startup, right after FS3EApp_LoadAccount()); cleared
     * by FS3EApp_SetAccount() on any confirmed-good login/re-verify.
     * Every other channel (Local/Federated/a profile's own statuses) still
     * works while this is TRUE -- Mastodon just falls back to anonymous,
     * public-only access for a rejected token instead of erroring those
     * endpoints outright -- but FS3EApp_CheckConnectionState() still shows
     * this as its own "your token has expired" message, distinct from the
     * "Anonymous connexion" one shown for an account that was deliberately
     * never logged in to begin with (accountAccessToken NULL from the
     * start, see FS3EACCOUNT_ANON_ACCT in fs3eaccounts.h) -- this one used
     * to work and stopped, that one never tried. See
     * FS3EApp_CheckConnectionState's own comments for the exact wording of
     * both. */
    BOOL   accountTokenRejected;

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

    /* VIEWMODE_User's own profile header (bio, follower/following counts --
     * see TTIMELINE_ShowProfile) -- fetched once per real account via
     * FS3EApp_ShowOwnProfileHeader(), not cached locally beyond these two
     * flags (the fetched data is handed straight to TootTimeline, which
     * owns displaying it; fs3erequests.c doesn't keep its own copy).
     * accountProfileFetched TRUE means the header is already showing (or
     * failed after landing -- either way, don't re-fetch just from
     * switching back to the User tab); accountProfileLookupPending TRUE
     * means the FS3ENETQ_ACCOUNT_LOOKUP for it is still in flight (guards
     * against firing a second one on a fast repeat tab switch, and lets the
     * reply handler tell this lookup apart from an unrelated Search-flow
     * one for the same account -- see the FS3ENETQ_ACCOUNT_LOOKUP case in
     * fs3erequests.c). Both reset FALSE on every real account change, same
     * as accountMaxChars above. */
    BOOL   accountProfileFetched;
    BOOL   accountProfileLookupPending;

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
    ULONG       accountCurrentlyConnecting;
    ULONG       accountListJustRefreshing; /*bool, to avoid recursion */

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

    /* Bitmask of channels whose last INITIAL fetch completed successfully
     * with zero items -- lets FS3EApp_CheckConnectionState show "Nothing
     * found." instead of the generic "Account connected." idle text,
     * which was actively misleading for an empty word/account search
     * (TootTimeline's own "waiting" mode -- see TTIMELINE_ViewMode's doc
     * comment in fs3etoottimeline.h -- never clears on its own when
     * nothing was ever added, so without this bit there was no way to
     * tell "genuinely nothing to show" apart from "connected, idle").
     * Set/cleared alongside channelPopulatedMask at every INITIAL-page
     * reply (FS3ENETQ_TIMELINE/NOTIFICATIONS/ACCOUNTS_LIST); untouched by
     * older/newer pagination pages, which don't speak to whether the
     * channel as a whole is empty. */
    ULONG  channelEmptyMask;

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

    /* The query text most recently passed to FS3EApp_SearchWord/
     * SearchAccount, valid whenever searchMode is FS3ESEARCH_WORD/
     * ACCOUNT -- kept as its own copy (NOT re-read from searchWordEditor)
     * specifically so searchStackPush() can snapshot the OLD query when
     * re-searching within the same mode: by the time SearchWord/
     * SearchAccount runs for a NEW query, the editor gadget already
     * displays that new text (StartSearchFromLine reads-then-calls), so
     * re-reading the live gadget at push time would wrongly capture the
     * new query as if it were history. Which chooser selection (Word vs
     * People) this query was searched under is NOT stored separately --
     * it's always implied by searchMode itself (WORD vs ACCOUNT). */
    char  *searchLastQueryText;

    /* Search channel (VIEWMODE_Search) discussion-view state -- see
     * FS3EApp_OpenDiscussion(). Set the moment a discussion is requested,
     * same "before either reply lands" timing as searchProfileAcct above. */
    char  *searchDiscussionStatusId;

    /* Back-navigation history for the Search channel -- see
     * FS3EApp_SearchGoBack()/searchStackPush() in fs3erequests.c. A plain
     * fixed-depth stack (top = searchStack[searchStackDepth-1]), pushed
     * by every FS3EApp_OpenProfile/OpenDiscussion/SearchWord/
     * SearchAccount/ShowFollowers/ShowFollowing call right before it
     * overwrites the fields above. searchPendingScrollY is a one-shot
     * "restore this scroll position once the channel we just asked for
     * actually loads" value, consumed by whichever FS3ENETQ_* reply
     * handler next populates the Search channel; -1 means none pending
     * (0 is a valid scroll position, so it can't double as the sentinel). */
    FS3ESearchStackEntry searchStack[FS3E_SEARCH_STACK_MAX];
    UBYTE  searchStackDepth;
    LONG   searchPendingScrollY;

    /* TRUE while the Timeline menu's autoscroll is (nominally) running --
     * see Action_TimelineAutoscrollPlay/Stop in fs3eaction.c. Toggled by
     * those two even though the actual autoscroll timer isn't implemented
     * yet, purely so the Space key's context-dependent dispatch (Next
     * toot when idle, Stop autoscroll when running -- see WMHI_RAWKEY in
     * friendsh3ep.c) is already correct now; the follow-up work is
     * filling in the real timer, not touching this flag or the key
     * dispatch again. */
    BOOL   timelineAutoscrollActive;
};

extern struct App *app;

/* Print pmessage (if non-NULL) and exit(0); runs exitclose() via atexit(). */
void cleanexit(const char *pmessage);

/* notifyMessage() severity levels -- see its own doc comment below. */
enum {
    FS3ENOTIFY_OK = 0,
    FS3ENOTIFY_WARNING,
    FS3ENOTIFY_ERROR
};

/* User-visible status/notification surface: drives the Network window's
 * (fs3enetworkview.c) status-bar button label. Callers (e.g. the
 * FS3ENETQ_FETCH_IMAGE reply handler in fs3erequests.c, for
 * fs3emanageurl.c's archive downloads) don't need to know or care whether
 * that window is currently open -- see FS3ENetworkView_SetStatus()'s own
 * no-op-while-closed rule. level is one of the FS3ENOTIFY_* values above;
 * unused today (no color/icon per level in the status bar yet). */
void notifyMessage(const char *message, int level);

void fs3e_setViewMode(ULONG viewMode);

/* Re-applies app->settings' font/rendering options to every draw context
 * (button bar, timeline style). Not static so fs3eboopsimainwindow.c's
 * GenericOpenWindow() can call it again right after a real screen is bound
 * -- see its comment in friendsh3ep.c for why that second call is needed. */
void FS3EApp_ApplyFontSettings_Delayed(void);

/* Request loading (or unloading) a UI theme onto the main window from
 * app->settings.themeName: unloads any currently-loaded theme bitmaps and
 * releases their pens, loads style.txt (from PROGDIR:themes/<themeName>)
 * and images if themeName is non-NULL/non-empty, or just resets colors to
 * the built-in defaults if it's NULL/empty ("no theme"), re-applies colors
 * to pens, re-syncs the title bar button images, and WM_RETHINKs the main
 * window so the new layout/colors actually show.
 *
 * Debounced like FS3EApp_ApplyFontSettings(): the real work (see
 * FS3EApp_LoadTheme_Delayed in friendsh3ep.c) runs once on the next
 * Wait() wakeup, not synchronously on this call. Rapid repeated calls
 * (e.g. two fast clicks on the theme chooser) coalesce to a single
 * WM_RETHINK against whatever app->settings.themeName holds by then,
 * instead of each doing a synchronous unload/load/WM_RETHINK re-entrantly
 * from inside WM_HANDLEINPUT dispatch, which used to crash. Callers
 * (fs3ethemeview.c's chooser handler, main()'s startup restore) are
 * expected to have already written the requested name into
 * app->settings.themeName before calling this -- the themeName parameter
 * itself is unused, kept only so call sites read as documentation of that
 * contract. Not static: fs3ethemeview.c calls this when the theme chooser
 * selection changes. */
void FS3EApp_LoadTheme(const char *themeName);

void flushThemeImagesFromButtons();
#endif /* FRIENDSH3EP_H */
