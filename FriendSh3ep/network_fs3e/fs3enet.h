#ifndef FS3ENET_H
#define FS3ENET_H

/*
 * FriendSh3ep network process - public API.
 *
 * This static library is linked into the GUI executable, but (besides
 * FS3ENet_Start/FS3ENet_Stop, which run on the caller's task) all of its work
 * happens in a separate AmigaDOS process created with CreateNewProc().
 * The GUI and the network process talk only through FS3ENetMessage exchanged
 * over Exec MsgPorts - no shared GUI state, and this library never includes
 * Intuition/BOOPSI/utf8rastport headers.
 *
 * See ../ARCHITECTURE.md for the full design and roadmap.
 */

#include <exec/ports.h>
#include <exec/types.h>

#include "fs3enet_mastodon.h"

/* Request types, sent by the GUI to the network process' request port. */
enum FS3ENetRequestType
{
    FS3ENETQ_SHUTDOWN = 0,   /* ask the network process to exit */
    FS3ENETQ_LOGIN_START,    /* register app, return authorize URL  (Phase 1) */
    FS3ENETQ_LOGIN_FINISH,   /* exchange oauth code for access token (Phase 1) */
    FS3ENETQ_TIMELINE,       /* fetch a timeline page                (Phase 2) */
    FS3ENETQ_POST_STATUS,    /* publish a new status (toot)          (Phase 2) */
    FS3ENETQ_FETCH_IMAGE,    /* fetch/return cached avatar or media   (Phase 2) */
    FS3ENETQ_FLUSH_CACHE,    /* delete every file in the disk cache               */
    FS3ENETQ_VERIFY_ACCOUNT, /* re-verify an existing access token, backfill account fields */
    FS3ENETQ_FAVORITE,       /* toggle favourite/unfavourite on a status */
    FS3ENETQ_ACCOUNT_LOOKUP, /* resolve an acct string to a full account (profile view) */
    FS3ENETQ_RELATIONSHIP,   /* fetch following/followed-by state for an account id */
    FS3ENETQ_FOLLOW,         /* toggle follow/unfollow on an account */
    FS3ENETQ_INSTANCE_INFO,  /* fetch the server's per-toot character limit */
    FS3ENETQ_EDIT_STATUS,    /* edit an existing status' text (own toots only) */
    FS3ENETQ_DELETE_STATUS,  /* delete an existing status (own toots only) */
    FS3ENETQ_NOTIFICATIONS,  /* fetch a page of notifications */
    FS3ENETQ_ACCOUNTS_LIST,  /* fetch a list of accounts: search results, or a
                               * user's followers/following -- see
                               * FS3ENetAccountsListReq/Reply below */
    FS3ENETQ_RELATIONSHIPS,  /* batch following/followed-by state for N account
                               * ids at once -- see FS3ENetRelationshipsReq/Reply
                               * below; used to badge account-row list items,
                               * unlike singular FS3ENETQ_RELATIONSHIP which only
                               * ever targets the profile header's one account */
    FS3ENETQ_FETCH_PROGRESS /* net-process-originated ONLY -- never sent by the GUI.
                              * A one-way PutMsg() of an FS3ENetFetchProgress block to
                              * app->netReplyPort while a chunked FS3ENETQ_FETCH_IMAGE
                              * download is still in flight; no reply is expected back,
                              * the GUI just frees it like any other FS3ENetMessage off
                              * that port. See FS3ENetFetchProgress below. */
};

/* Result codes returned in FS3ENetMessage.fs3em_Result on reply. */
enum FS3ENetResult
{
    FS3ENETR_OK = 0,
    FS3ENETR_NETWORK_ERROR,
    FS3ENETR_HTTP_ERROR,
    FS3ENETR_AUTH_ERROR,
    FS3ENETR_PARSE_ERROR
};

/*
 * Generic request/reply envelope.
 *
 * fs3em_Msg.mn_ReplyPort is set by the caller before PutMsg(); the network
 * process PutMsg()s the same structure back to that port once done.
 *
 * fs3em_Data/fs3em_DataLen describe a request- or reply-specific payload
 * allocated with AllocVec() by whichever side produces it. Ownership passes
 * to the receiver, which must FreeVec() it.
 */
typedef struct FS3ENetMessage
{
    struct Message fs3em_Msg;
    ULONG           fs3em_Type;    /* enum FS3ENetRequestType */
    ULONG           fs3em_Result;  /* enum FS3ENetResult, set on reply */
    APTR            fs3em_Data;
    ULONG           fs3em_DataLen;
} FS3ENetMessage;

/*
 * All request and reply structs below use char * string fields instead of
 * fixed-size arrays.  Each struct is allocated as a single flat block:
 *
 *   [struct header] + [string data packed contiguously]
 *
 * The char * fields point into the same block, so one FreeVec() on the
 * fs3em_Data pointer frees the struct and all its strings.  fs3em_DataLen
 * is set to the total block size (not sizeof(struct)).
 *
 * Use the _Alloc() helpers below to build request blocks; the network
 * process builds reply blocks internally.
 */

/*
 * FS3ENETQ_LOGIN_START — registers FriendSh3ep as an OAuth app on
 * fs3enl_ApiBaseUrl (see ARCHITECTURE.md section 4.4).
 *
 * On FS3ENETR_OK, fs3em_Data is replaced with an FS3ENetLoginStartReply; the
 * GUI must show fs3enl_AuthorizeUrl to the user and keep
 * fs3enl_ClientId/fs3enl_ClientSecret for FS3ENETQ_LOGIN_FINISH.
 * On error, fs3em_Data still points at the original request block — the GUI
 * must FreeVec it.
 */
typedef struct FS3ENetLoginStartReq
{
    char *fs3enl_ApiBaseUrl;
} FS3ENetLoginStartReq;

/* Allocates a flat request block for LOGIN_START. FreeVec() when done. */
FS3ENetLoginStartReq *FS3ENetLoginStartReq_Alloc(const char *apiBaseUrl);

typedef struct FS3ENetLoginStartReply
{
    char *fs3enl_ClientId;
    char *fs3enl_ClientSecret;
    char *fs3enl_AuthorizeUrl;
} FS3ENetLoginStartReply;

/*
 * FS3ENETQ_LOGIN_FINISH — exchanges the OOB code for an access token and
 * verifies it (brutaldon's mastodon.log_in + verify_credentials).
 *
 * On FS3ENETR_OK, fs3em_Data is replaced with an FS3ENetLoginFinishReply; the
 * GUI must persist fs3enl_AccessToken (and the api base URL) for later
 * FS3ENETQ_TIMELINE/FS3ENETQ_POST_STATUS requests.
 * On error, fs3em_Data still points at the original request block.
 *
 * Note: fs3enl_Account strings point into the same flat reply block.
 * Do NOT call FS3EMastodonAccount_Free() on fs3enl_Account; FreeVec the
 * whole block instead.
 */
typedef struct FS3ENetLoginFinishReq
{
    char *fs3enl_ApiBaseUrl;
    char *fs3enl_ClientId;
    char *fs3enl_ClientSecret;
    char *fs3enl_Code;
} FS3ENetLoginFinishReq;

/* Allocates a flat request block for LOGIN_FINISH. FreeVec() when done. */
FS3ENetLoginFinishReq *FS3ENetLoginFinishReq_Alloc(const char *apiBaseUrl,
    const char *clientId, const char *clientSecret, const char *code);

typedef struct FS3ENetLoginFinishReply
{
    char               *fs3enl_AccessToken;
    FS3EMastodonAccount  fs3enl_Account;
} FS3ENetLoginFinishReply;

/*
 * FS3ENETQ_VERIFY_ACCOUNT — re-runs verify_credentials for an access token
 * the GUI already has (no OAuth exchange, unlike LOGIN_FINISH). Used to
 * backfill account fields added after a user's account.dat was last saved
 * (e.g. fma_Id, needed by VIEWMODE_User's accounts/{id}/statuses fetch) --
 * see FS3EApp_LoadAccount() in friendsh3ep.c.
 *
 * On FS3ENETR_OK, fs3em_Data is replaced with an FS3ENetVerifyAccountReply.
 * On error, fs3em_Data still points at the original request block.
 */
typedef struct FS3ENetVerifyAccountReq
{
    char *fs3eva_ApiBaseUrl;
    char *fs3eva_AccessToken;
} FS3ENetVerifyAccountReq;

/* Allocates a flat request block for VERIFY_ACCOUNT. FreeVec() when done. */
FS3ENetVerifyAccountReq *FS3ENetVerifyAccountReq_Alloc(const char *apiBaseUrl,
    const char *accessToken);

typedef struct FS3ENetVerifyAccountReply
{
    FS3EMastodonAccount fs3eva_Account;
} FS3ENetVerifyAccountReply;

/*
 * FS3ENETQ_INSTANCE_INFO — fetches the server's per-toot character limit
 * (see FS3EMastodon_GetInstanceInfo). No access token needed. Meant to be
 * sent once per account (right after login/load, see
 * FS3EApp_SetAccount() in friendsh3ep.c) rather than per-compose, since
 * an instance's limit essentially never changes mid-session.
 *
 * On FS3ENETR_OK, fs3em_Data is replaced with an FS3ENetInstanceInfoReply.
 * fs3eii_Known says whether fs3eii_MaxChars is a real value the server
 * confirmed, or just FS3EMastodon_GetInstanceInfo's own best-guess
 * fallback because neither instance endpoint answered -- the GUI must NOT
 * present that fallback as if it were a real limit (see
 * FS3ETootView_UpdateCharCount in fs3etootview.c, which shows "Max: -"
 * rather than a possibly-wrong number until fs3eii_Known is TRUE for the
 * currently active account).
 * On error, fs3em_Data still points at the original request block.
 */
typedef struct FS3ENetInstanceInfoReq
{
    char *fs3eii_ApiBaseUrl;
} FS3ENetInstanceInfoReq;

/* Allocates a flat request block for INSTANCE_INFO. FreeVec() when done. */
FS3ENetInstanceInfoReq *FS3ENetInstanceInfoReq_Alloc(const char *apiBaseUrl);

typedef struct FS3ENetInstanceInfoReply
{
    ULONG fs3eii_MaxChars;
    BOOL  fs3eii_Known;
} FS3ENetInstanceInfoReply;

/* FS3ECache subdirectories (see fs3enet_cache.h) -- user avatars and toot
 * media thumbnails are fetched through the identical pipeline but kept in
 * their own cache subdirectory rather than one flat pile of hash-named
 * files, since they're conceptually distinct sets. Shared here (rather
 * than a private #define in friendsh3ep.c) so any caller building an
 * FS3ENetFetchImageReq -- friendsh3ep.c's avatar/thumbnail fetches,
 * fs3emediaview.c's on-demand full-image fetch -- names the same
 * subdirectory. */
#define FS3E_CACHE_SUBDIR_USERICONS   "usericons"
#define FS3E_CACHE_SUBDIR_THUMBNAILS  "thumbnails"
/* Kept distinct from THUMBNAILS (not reused) so the FS3ENETQ_FETCH_IMAGE
 * reply handler (fs3erequests.c) can tell a card-image reply apart from a
 * real attachment-thumbnail reply by subdir alone, and so a card's image
 * URL can never collide with an actual attachment's cache entry. */
#define FS3E_CACHE_SUBDIR_CARDIMAGES  "cardimages"

/*
 * FS3ENETQ_FETCH_IMAGE — fetch a media URL (avatar, attachment thumbnail,
 * custom emoji) and cache it under T:FS3ECache/.  The network process serves
 * from disk cache when the file is already present; it only hits the network
 * on a cache miss.
 *
 * On a cache miss, the network process downloads the file in bounded chunks
 * (see FS3ENetActiveDownload in fs3enet.c) instead of one unbounded blocking
 * fetch, so a large/slow download can't stall other queued requests, and a
 * dead connection times out per-chunk instead of hanging forever. This is
 * purely an internal implementation detail: fs3em_Data/fs3em_Result still
 * arrive exactly once, when the whole file is done or has failed, with the
 * same shape as before chunking existed. If fs3enf_WantProgress is TRUE, the
 * caller ALSO gets zero or more FS3ENETQ_FETCH_PROGRESS pings on
 * app->netReplyPort while the download is in flight -- see
 * FS3ENetFetchProgress below.
 *
 * On FS3ENETR_OK, fs3em_Data is a flat FS3ENetFetchImageReply block whose
 * fs3enf_LocalPath is a NUL-terminated AmigaOS path the GUI can open with
 * NewDTObject() (no file extension; datatype detects JPEG/PNG from magic).
 * On error, fs3em_Data still points at the original request block.
 *
 * The URL need not carry an Authorization header: Mastodon CDN URLs are
 * pre-signed and publicly accessible regardless of auth state.
 */
typedef struct FS3ENetFetchImageReq
{
    char *fs3enf_Url;
    char *fs3enf_Key;     /* caller key echoed in reply; @acct for avatars, the URL itself for media */
    char *fs3enf_Subdir;  /* FS3ECache_Lookup/Store subdir, e.g. "usericons"/"thumbnails"; "" = cache root */
    BOOL  fs3enf_KeepOriginal; /* FALSE = download to FS3ECACHE_RAM_TEMP_DIR instead of the
                                * persistent cache dir (see "Keep big user icons/thumbnails" in
                                * Settings) -- ignored on a cache hit against an already-persisted
                                * original from an earlier TRUE request. */
    BOOL  fs3enf_WantProgress; /* opt in to FS3ENETQ_FETCH_PROGRESS pings for this download.
                                 * FALSE for routine avatar/thumbnail fetches (no progress UI
                                 * for those today) -- TRUE for the media viewer's on-demand
                                 * full-image fetch, the case this was actually added for. */
} FS3ENetFetchImageReq;

/* Allocates a flat request block for FETCH_IMAGE. FreeVec() when done.
 * key is echoed back in the reply so the caller knows which entry to update. */
FS3ENetFetchImageReq *FS3ENetFetchImageReq_Alloc(const char *url, const char *key,
                                                   const char *subdir, BOOL keepOriginal,
                                                   BOOL wantProgress);

typedef struct FS3ENetFetchImageReply
{
    char *fs3enf_LocalPath;  /* e.g. "PROGDIR:.cache/usericons/1a2b3c4d" or "RAM:T/1a2b3c4d" */
    char *fs3enf_Key;        /* echoed from request */
    char *fs3enf_Subdir;     /* echoed from request -- lets the GUI dispatch avatar vs media handling */
    BOOL  fs3enf_IsTemp;     /* TRUE = fs3enf_LocalPath is a RAM:T download the caller must
                               * delete once it's done with it (see FS3EThumb_Request's
                               * deleteSrcAfter) -- FALSE if it's already permanently cached. */
    char *fs3enf_CachePath;  /* deterministic path this URL would live at under fs3enf_Subdir
                               * if kept, computed regardless of fs3enf_IsTemp (see
                               * FS3ECache_ComputePath) -- pass as FS3EThumb_Request's
                               * cacheKeyPath so the resized thumbnail always gets a name
                               * stable across runs, even when fs3enf_LocalPath itself is
                               * a transient RAM:T path. */
} FS3ENetFetchImageReply;

/*
 * FS3ENETQ_FETCH_PROGRESS — see fs3enf_WantProgress above. Sent unsolicited
 * by the network process, zero or more times, while a chunked FETCH_IMAGE
 * download is in flight; correlate with the original request/final reply via
 * fs3efp_Key (the same caller key FS3ENetFetchImageReq/Reply already carry --
 * see e.g. fs3emediaview.c's mv->pendingUrl match against reply->fs3enf_Key).
 * fs3efp_TotalBytes is 0 until the first chunk's response tells us the real
 * size (Content-Range's "/TOTAL" suffix) -- some servers never do (they
 * ignore Range entirely), in which case it stays 0 for the whole download and
 * the caller can only show bytes-so-far, not a percentage.
 */
typedef struct FS3ENetFetchProgress
{
    char  *fs3efp_Key;
    ULONG  fs3efp_BytesSoFar;
    ULONG  fs3efp_TotalBytes; /* 0 = unknown */
} FS3ENetFetchProgress;

/*
 * Start the network process. cacheDir and maxCacheSizeMB are passed straight
 * to FS3ECache_Init() inside the new process; pass NULL for cacheDir to use
 * FS3ECACHE_DEFAULT_DIR, 0 for maxCacheSizeMB for an unbounded cache.
 * Returns the request MsgPort, or NULL on failure.
 */
struct MsgPort *FS3ENet_Start(const char *cacheDir, ULONG maxCacheSizeMB);

/*
 * Ask the network process to shut down and wait for it to exit.
 * requestPort is the port returned by FS3ENet_Start(); replyPort is a
 * temporary port created by the caller to receive the shutdown reply.
 */
void FS3ENet_Stop(struct MsgPort *requestPort, struct MsgPort *replyPort);

/*
 * Ask the network process to flush (delete every file in) its disk cache
 * and wait for the reply. requestPort/replyPort as FS3ENet_Stop().
 * Returns TRUE on FS3ENETR_OK, FALSE otherwise (including requestPort==NULL).
 */
BOOL FS3ENet_FlushCache(struct MsgPort *requestPort, struct MsgPort *replyPort);

/* Which direction a FS3ENETQ_TIMELINE request pages in -- echoed back into
 * FS3ENetTimelineReply so the GUI knows how to splice the results into its
 * post list (prepend at the top vs. append at the bottom) and which
 * in-flight guard to clear, without having to remember what it asked for. */
enum FS3ENetPageDirection
{
    FS3ENETPAGE_INITIAL = 0,  /* first page for this channel; fs3et_MaxId/MinId both "" */
    FS3ENETPAGE_OLDER,        /* fs3et_MaxId set: statuses strictly older than it */
    FS3ENETPAGE_NEWER         /* fs3et_MinId set: statuses strictly newer than it */
};

/* Which JSON shape fs3et_Timeline's endpoint returns -- lets
 * FS3ENETQ_TIMELINE be reused for endpoints that don't hand back a bare
 * status array, without forking the per-status field-extraction parser
 * (content, media_attachments, poll, counts, ...) that ARRAY already
 * shares with every timeline/profile fetch. Echoed back into
 * FS3ENetTimelineReply the same way fs3et_ViewModeBit/PageDirection are,
 * so the GUI reply handler knows which shape it got without depending on
 * mutable app state that could have changed by the time the reply lands. */
enum FS3ENetTimelineShape
{
    FS3ENET_TLSHAPE_ARRAY = 0,       /* bare Status[] -- every timeline/profile-statuses endpoint */
    FS3ENET_TLSHAPE_SINGLE,          /* GET .../statuses/:id -- one Status object, wrapped as a 1-elem array */
    FS3ENET_TLSHAPE_CONTEXT_DESCENDANTS, /* GET .../statuses/:id/context -- {ancestors,descendants}; only
                                          * descendants (the replies) is unwrapped and used, ancestors
                                          * discarded */
    FS3ENET_TLSHAPE_SEARCH_STATUSES, /* GET /api/v2/search?type=statuses&q=... (note: v2, not v1) --
                                          * {accounts,statuses,hashtags}; only statuses is unwrapped and
                                          * used. Word and hashtag search both use this same shape/request
                                          * (see fs3et_SearchQuery below) -- Mastodon's own search treats
                                          * a leading '#' in q as a hashtag match, so there's no need for
                                          * a separate hashtag-timeline request type. */
    FS3ENET_TLSHAPE_SINGLE_REFRESH,  /* GET .../statuses/:id, same one-Status-object wire shape as
                                          * FS3ENET_TLSHAPE_SINGLE, but a different GUI-side meaning: an
                                          * F5-triggered refresh of an already-displayed toot rather than
                                          * a toot being newly inserted. fs3et_MinId is repurposed (see
                                          * FS3ENetTimelineReq's doc comment) to carry the TTLPost.postId
                                          * to patch, echoed back via FS3ENetTimelineReply.fs3et_RefreshPostId
                                          * -- see FS3EApp_RefreshVisibleToots(). */
    FS3ENET_TLSHAPE_SEARCH_ACCOUNTS  /* GET /api/v2/search?type=accounts&q=... -- same
                                          * {accounts,statuses,hashtags} wrapper as SEARCH_STATUSES,
                                          * just unwrapping "accounts" instead of "statuses". Not used
                                          * by FS3ENetTimelineReq/FS3ENETQ_TIMELINE at all -- this value
                                          * is only ever passed to FS3EMastodon_GetTimeline() directly
                                          * by FS3ENET_HandleAccountsList() (FS3ENETQ_ACCOUNTS_LIST),
                                          * which has its own request/reply pair and its own
                                          * FS3ENetAccountsListKind discriminator. */
};

/*
 * FS3ENETQ_TIMELINE — fetch one page of statuses for a timeline.
 *
 * fs3et_ViewModeBit identifies the UI channel (VIEWMODE_* value from
 * friendsh3ep.h); it is echoed unchanged into FS3ENetTimelineReply so the
 * GUI can route replies back to the right TootTimeline channel.
 * fs3et_AccountGeneration is an opaque caller-defined token (FriendSh3ep
 * stamps its App.accountGeneration, bumped on every login/account switch)
 * echoed back the same way -- lets the caller tell a reply for the account
 * that was active when the request was sent apart from one for whatever
 * account is active *now*, since two FS3ENETQ_TIMELINE requests for the
 * same fs3et_ViewModeBit sent under different accounts (e.g. one still in
 * flight when the user switches accounts) are otherwise indistinguishable
 * once the reply comes back -- this process treats it as opaque and never
 * inspects it.
 * fs3et_AccessToken may be "" for public timelines.
 * fs3et_MaxId/fs3et_MinId may be NULL/empty; at most one should be set (see
 * fs3et_PageDirection) -- fs3et_MaxId asks for statuses strictly older than
 * that status id (contiguous with what the GUI already has at the bottom
 * of its list), fs3et_MinId strictly newer (contiguous at the top). Both
 * empty means "the newest page" (FS3ENETPAGE_INITIAL).
 *
 * On FS3ENETR_OK, fs3em_Data is replaced with a flat FS3ENetTimelineReply
 * block; fs3em_Data on error still points at the original request block.
 *
 * fs3et_SearchQuery is only meaningful for FS3ENET_TLSHAPE_SEARCH_STATUSES:
 * the raw (NOT URL-encoded) search text -- FS3ENet_HandleTimeline encodes
 * it itself and folds it onto fs3et_Timeline as "&q=...". "" for every
 * other shape.
 *
 * fs3et_MinId is repurposed for FS3ENET_TLSHAPE_SINGLE_REFRESH: its normal
 * "page strictly newer than this id" pagination meaning doesn't apply to a
 * single-status fetch, so it instead carries the TTLPost.postId the GUI
 * should patch once the reply lands (see FS3ENetTimelineReply.fs3et_RefreshPostId) --
 * this can differ from the refetched status's own id in the notifications
 * view, where TTLPost.postId is the notification's own id, not the embedded
 * status's (see TTLPostSetup.notifStatusId in fs3etoottimeline.h).
 */
typedef struct FS3ENetTimelineReq {
    ULONG  fs3et_ViewModeBit;    /* echoed in reply */
    ULONG  fs3et_PageDirection;  /* FS3ENetPageDirection; echoed in reply */
    ULONG  fs3et_AccountGeneration; /* opaque caller token; echoed in reply */
    ULONG  fs3et_ResponseShape;  /* FS3ENetTimelineShape; echoed in reply */
    char  *fs3et_ApiBaseUrl;
    char  *fs3et_AccessToken;    /* "" = no auth (public timelines) */
    char  *fs3et_Timeline;       /* "home", "public", "public?local=true", … */
    char  *fs3et_MaxId;          /* "" = no lower bound */
    char  *fs3et_MinId;          /* "" = no upper bound */
    char  *fs3et_SearchQuery;    /* raw (unencoded) search text; "" unless
                                   * fs3et_ResponseShape is SEARCH_STATUSES */
} FS3ENetTimelineReq;

FS3ENetTimelineReq *FS3ENetTimelineReq_Alloc(ULONG viewModeBit,
    ULONG pageDirection, ULONG accountGeneration, ULONG responseShape,
    const char *apiBaseUrl, const char *accessToken, const char *timeline,
    const char *maxId, const char *minId, const char *searchQuery);

/* Max media_attachments entries kept per status (Mastodon itself caps
 * normal posts at 4 attachments, so this never truncates in practice). */
#define FS3ENET_MAX_MEDIA 4

/* Max poll options kept per status. Vanilla Mastodon's own default cap is
 * 4, but some instances raise it -- 8 leaves headroom without a real cost
 * (just a handful of extra pointer-sized slots per FS3ENetStatus). */
#define FS3ENET_MAX_POLL_OPTIONS 8

/* Mastodon media_attachments[].type, mapped from the JSON string. Lets
 * the GUI tell an audio attachment apart from an image *before* ever
 * downloading anything for it -- audio has no thumbnail to fetch, and
 * routing its (fallback) full-file URL into the image decoder is exactly
 * what used to make MP3s show up as failed image loads. */
enum FS3ENetMediaKind
{
    FS3ENET_MEDIAKIND_IMAGE = 0,
    FS3ENET_MEDIAKIND_VIDEO,
    FS3ENET_MEDIAKIND_GIFV,
    FS3ENET_MEDIAKIND_AUDIO,
    FS3ENET_MEDIAKIND_UNKNOWN
};

/* Single status entry inside a FS3ENetTimelineReply.
 * All char * fields point into the same flat block — one FreeVec() on
 * the enclosing FS3ENetTimelineReply frees everything. */
typedef struct FS3ENetStatus {
    char *fmas_DisplayName;  /* original author display_name (UTF-8) */
    char *fmas_Acct;         /* original author @user@instance handle */
    char *fmas_Content;      /* HTML-stripped plain text */
    char *fmas_CreatedAt;    /* ISO 8601 timestamp string */
    char *fmas_AvatarURL;    /* original author CDN avatar URL */
    char *fmas_Id;           /* status id string (for pagination) */
    char *fmas_BoostBy;      /* booster display_name, "" if not a reblog */
    char *fmas_BoostByAcct;  /* booster @user@instance handle, "" if not a reblog --
                               * what a click on the "X boosted" line actually needs
                               * to look up their profile; the display name alone
                               * isn't a valid /api/v1/accounts/lookup query. */

    /* media_attachments[].preview_url (falling back to .url if no
     * preview_url) for up to FS3ENET_MAX_MEDIA attachments; entries
     * [fmas_MediaCount..FS3ENET_MAX_MEDIA) are NULL. */
    char  *fmas_MediaUrls[FS3ENET_MAX_MEDIA];
    /* media_attachments[].type for the same slots -- enum FS3ENetMediaKind. */
    ULONG  fmas_MediaKind[FS3ENET_MAX_MEDIA];
    /* media_attachments[].id for the same slots -- needed to resend as
     * media_ids[] on a PUT edit of this status, since Mastodon treats that
     * field as a full replace-list: omit it and existing attachments get
     * stripped even if the edit never touched them. */
    char  *fmas_MediaIds[FS3ENET_MAX_MEDIA];
    ULONG  fmas_MediaCount;

    /* Action-bar counts/state -- for reblogs these belong to the boosted
     * status (Mastodon reports them there, not on the outer reblog
     * wrapper), same as fmas_Content/fmas_MediaUrls above. */
    ULONG  fmas_RepliesCount;
    ULONG  fmas_ReblogsCount;
    ULONG  fmas_FavouritesCount;
    BOOL   fmas_Favourited;   /* connected user already favourited this status */
    BOOL   fmas_Reblogged;    /* connected user already boosted this status */

    /* Poll ("survey"). Mutually exclusive with media_attachments above --
     * Mastodon itself disallows a status having both -- so the GUI treats
     * them as alternatives, not something that can coexist in one post.
     * fmas_PollOptionCount==0 means no poll on this status. An option's
     * votes_count comes back JSON null (not present) from the server
     * until the poll is closed or the connected user has voted -- packed
     * as 0 either way, since there's nothing else to show yet regardless
     * (voting isn't wired up on the GUI side either). */
    char  *fmas_PollOptionTitles[FS3ENET_MAX_POLL_OPTIONS];
    ULONG  fmas_PollOptionVotes[FS3ENET_MAX_POLL_OPTIONS];
    ULONG  fmas_PollOptionCount;
    ULONG  fmas_PollVotesCount;   /* total votes across all options -- percentage denominator */
    BOOL   fmas_PollExpired;      /* TRUE = closed, results are final */
    BOOL   fmas_PollMultiple;     /* TRUE = multiple-choice poll (not used yet, carried for later) */

    /* Link preview "card" -- server-generated (Mastodon itself fetches the
     * linked page's OpenGraph tags when the toot is posted and caches the
     * result), never fetched or parsed by this client. Independent of
     * fmas_MediaCount/poll above -- NOT mutually exclusive with either,
     * since a card comes from a URL in the text, not from what the user
     * attached. fmas_HasCard==FALSE means every other fmas_Card* field
     * below is "" and meaningless. Belongs to src same as content/media/
     * poll (a reblog's card is the boosted status's own, never the outer
     * wrapper's). */
    BOOL  fmas_HasCard;
    char *fmas_CardUrl;          /* the linked article's own URL */
    char *fmas_CardTitle;
    char *fmas_CardDescription;
    char *fmas_CardProviderName; /* site name, e.g. "The Verge" */
    char *fmas_CardImageUrl;     /* "" if the card has no image (some sites provide none) */
} FS3ENetStatus;

/* Header of the flat timeline reply block.
 * Statuses follow immediately: (FS3ENetStatus *)(reply + 1)[i] */
typedef struct FS3ENetTimelineReply {
    ULONG fs3et_ViewModeBit;    /* echoed from request */
    ULONG fs3et_PageDirection; /* echoed from request, see FS3ENetPageDirection */
    ULONG fs3et_AccountGeneration; /* echoed from request, see FS3ENetTimelineReq */
    ULONG fs3et_ResponseShape; /* echoed from request, see FS3ENetTimelineShape */
    ULONG fs3et_Count;
    char *fs3et_RefreshPostId; /* echoes request's (repurposed) fs3et_MinId when
                                 * fs3et_ResponseShape is FS3ENET_TLSHAPE_SINGLE_REFRESH;
                                 * "" for every other shape. See FS3ENetTimelineReq's
                                 * doc comment on fs3et_MinId. */
    /* FS3ENetStatus[fs3et_Count] follows immediately in memory */
} FS3ENetTimelineReply;

/*
 * FS3ENETQ_POST_STATUS — publish a new status (toot).
 *
 * fs3ep_Spoiler is the CW/subject text; pass "" for no content warning.
 * fs3ep_InReplyToId is the status this replies to, "" for a standalone
 * toot -- see FS3EMastodon_PostStatus.
 * On FS3ENETR_OK, fs3em_Data is replaced with an FS3ENetPostStatusReply.
 */
typedef struct FS3ENetPostStatusReq {
    char *fs3ep_ApiBaseUrl;
    char *fs3ep_AccessToken;
    char *fs3ep_Content;     /* UTF-8 post body */
    char *fs3ep_Visibility;  /* "public", "unlisted", "private", "direct" */
    char *fs3ep_Spoiler;     /* CW text; "" = no content warning */
    char *fs3ep_InReplyToId; /* status being replied to; "" = standalone toot */
    char *fs3ep_QuoteApprovalPolicy; /* "public", "followers", "nobody" */
} FS3ENetPostStatusReq;

FS3ENetPostStatusReq *FS3ENetPostStatusReq_Alloc(
    const char *apiBaseUrl, const char *accessToken,
    const char *content, const char *visibility, const char *spoiler,
    const char *inReplyToId, const char *quoteApprovalPolicy);

typedef struct FS3ENetPostStatusReply {
    char *fs3ep_StatusId; /* new status id string */
} FS3ENetPostStatusReply;

/*
 * FS3ENETQ_EDIT_STATUS — PUT /api/v1/statuses/:id, edit an existing status'
 * text (own toots only). fs3ee_MediaIds[0..fs3ee_MediaCount) are the
 * status' existing attachment ids (see FS3ENetStatus.fmas_MediaIds),
 * resent unchanged so the edit doesn't strip them -- see
 * FS3EMastodon_EditStatus. No spoiler/visibility fields: Mastodon's edit
 * endpoint doesn't accept changing either. On FS3ENETR_OK, fs3em_Data is
 * replaced with an FS3ENetEditStatusReply (same free/replace convention as
 * every other request in this file -- the GUI side always expects
 * fs3em_Data to end up a freshly allocated reply block, never the original
 * request left in place).
 */
typedef struct FS3ENetEditStatusReq {
    char *fs3ee_ApiBaseUrl;
    char *fs3ee_AccessToken;
    char *fs3ee_StatusId;
    char *fs3ee_Content;     /* UTF-8 new post body */
    char *fs3ee_MediaIds[FS3ENET_MAX_MEDIA];
    ULONG fs3ee_MediaCount;
} FS3ENetEditStatusReq;

FS3ENetEditStatusReq *FS3ENetEditStatusReq_Alloc(
    const char *apiBaseUrl, const char *accessToken,
    const char *statusId, const char *content,
    const char *const *mediaIds, ULONG mediaCount);

typedef struct FS3ENetEditStatusReply {
    char *fs3ee_StatusId; /* echoes the edited status id back */
} FS3ENetEditStatusReply;

/*
 * FS3ENETQ_DELETE_STATUS — DELETE /api/v1/statuses/:id, delete an existing
 * status (own toots only). On FS3ENETR_OK, fs3em_Data is replaced with an
 * FS3ENetDeleteStatusReply echoing the deleted status id back -- the GUI
 * needs it to remove that post from TootTimeline's in-memory data (see
 * TTIMELINE_RemovePost), same reasoning FS3ENetFavouriteReply already
 * echoes its id for TTIMELINE_UpdatePost.
 */
typedef struct FS3ENetDeleteStatusReq {
    char *fs3ed_ApiBaseUrl;
    char *fs3ed_AccessToken;
    char *fs3ed_StatusId;
} FS3ENetDeleteStatusReq;

FS3ENetDeleteStatusReq *FS3ENetDeleteStatusReq_Alloc(
    const char *apiBaseUrl, const char *accessToken, const char *statusId);

typedef struct FS3ENetDeleteStatusReply {
    char *fs3ed_StatusId;
} FS3ENetDeleteStatusReply;

/* Mastodon Notification.type, mapped from the JSON string. FOLLOW/
 * FOLLOW_REQUEST carry no status (see FS3ENetNotification.fen_HasStatus);
 * every other value does. UNKNOWN covers the admin-only types (sign-up,
 * report, severed relationships, moderation warning) and anything else
 * this app doesn't specifically handle -- rendered with no actor/verb
 * prefix rather than guessed at. */
enum FS3ENetNotifType
{
    FS3ENOTIF_MENTION = 0,
    FS3ENOTIF_REBLOG,
    FS3ENOTIF_FAVOURITE,
    FS3ENOTIF_FOLLOW,
    FS3ENOTIF_FOLLOW_REQUEST,
    FS3ENOTIF_POLL,
    FS3ENOTIF_UPDATE,
    FS3ENOTIF_UNKNOWN
};

/*
 * FS3ENETQ_NOTIFICATIONS — GET /api/v1/notifications.
 *
 * No fs3en_ViewModeBit -- this queue always targets VIEWMODE_Notifs, only
 * one channel is ever meaningful for it, unlike FS3ENETQ_TIMELINE which is
 * shared across every channel including Search's several sub-modes.
 * fs3en_MaxId/MinId/PageDirection mirror FS3ENetTimelineReq exactly --
 * Mastodon paginates notifications by the notification's own id, the same
 * max_id/min_id query-param shape every timeline endpoint already uses.
 */
typedef struct FS3ENetNotificationsReq {
    ULONG  fs3en_PageDirection;     /* FS3ENetPageDirection; echoed in reply */
    ULONG  fs3en_AccountGeneration; /* opaque caller token; echoed in reply */
    char  *fs3en_ApiBaseUrl;
    char  *fs3en_AccessToken;
    char  *fs3en_MaxId;             /* "" = no lower bound */
    char  *fs3en_MinId;             /* "" = no upper bound */
} FS3ENetNotificationsReq;

FS3ENetNotificationsReq *FS3ENetNotificationsReq_Alloc(ULONG pageDirection,
    ULONG accountGeneration, const char *apiBaseUrl, const char *accessToken,
    const char *maxId, const char *minId);

/* One notification entry inside a FS3ENetNotificationsReply.
 * All char * fields point into the same flat block, same convention as
 * FS3ENetStatus -- one FreeVec() on the enclosing reply frees everything,
 * including fen_Status's own pointer fields (fen_Status is embedded by
 * value, not pointed-to, precisely so this holds). */
typedef struct FS3ENetNotification {
    char  *fen_Id;               /* notification's own id -- see TTLPost.postId's
                                   * doc comment on why this, not the status id, drives pagination */
    ULONG  fen_Type;             /* FS3ENetNotifType */
    char  *fen_ActorDisplayName; /* who triggered this notification */
    char  *fen_ActorAcct;
    char  *fen_ActorAvatarURL;
    BOOL   fen_HasStatus;        /* FALSE for FOLLOW/FOLLOW_REQUEST */
    FS3ENetStatus fen_Status;    /* meaningful iff fen_HasStatus */
} FS3ENetNotification;

/* Header of the flat notifications reply block.
 * Notifications follow immediately: (FS3ENetNotification *)(reply + 1)[i] */
typedef struct FS3ENetNotificationsReply {
    ULONG fs3en_PageDirection;
    ULONG fs3en_AccountGeneration;
    ULONG fs3en_Count;
    /* FS3ENetNotification[fs3en_Count] follows immediately in memory */
} FS3ENetNotificationsReply;

/*
 * FS3ENETQ_ACCOUNTS_LIST — fetch a list of accounts: fuzzy account search
 * (GET /api/v2/search?type=accounts), or a user's followers/following
 * (GET /api/v1/accounts/:id/followers or .../following). Unlike
 * FS3ENETQ_TIMELINE, this is deliberately single-page only for now: real
 * pagination for these endpoints is driven by an RFC5988 Link: response
 * header this codebase's HTTP layer doesn't parse at all (FS3EHttpResponse
 * only exposes body/status) -- matches the same "no pagination yet"
 * scope FS3EApp_SearchWord's word/hashtag search already accepted.
 */
enum FS3ENetAccountsListKind
{
    FS3ENET_ACCLIST_FOLLOWERS = 0, /* fs3eal_AccountId is whose followers to list */
    FS3ENET_ACCLIST_FOLLOWING,     /* fs3eal_AccountId is whose following to list */
    FS3ENET_ACCLIST_SEARCH         /* fs3eal_Query is the raw (unencoded) search text */
};

typedef struct FS3ENetAccountsListReq {
    ULONG  fs3eal_Kind;              /* FS3ENetAccountsListKind; echoed in reply */
    ULONG  fs3eal_AccountGeneration; /* opaque caller token; echoed in reply, same
                                       * reasoning as FS3ENetTimelineReq's own field */
    char  *fs3eal_ApiBaseUrl;
    char  *fs3eal_AccessToken;
    char  *fs3eal_AccountId; /* FOLLOWERS/FOLLOWING: whose list; "" for SEARCH */
    char  *fs3eal_Query;     /* SEARCH: raw (unencoded) query text; "" otherwise */
} FS3ENetAccountsListReq;

FS3ENetAccountsListReq *FS3ENetAccountsListReq_Alloc(ULONG kind,
    ULONG accountGeneration, const char *apiBaseUrl, const char *accessToken,
    const char *accountId, const char *query);

/* Header of the flat accounts-list reply block.
 * FS3EMastodonAccount[fs3eal_Count] follows immediately in memory. */
typedef struct FS3ENetAccountsListReply {
    ULONG fs3eal_Kind;              /* echoed from request, see FS3ENetAccountsListKind */
    ULONG fs3eal_AccountGeneration; /* echoed from request, see FS3ENetAccountsListReq */
    ULONG fs3eal_Count;
} FS3ENetAccountsListReply;

/*
 * FS3ENETQ_FAVORITE — POST /api/v1/statuses/:id/favourite or .../unfavourite.
 *
 * fs3efa_Favourite selects which: TRUE = favourite, FALSE = unfavourite.
 * On FS3ENETR_OK, fs3em_Data is replaced with an FS3ENetFavouriteReply
 * carrying just the server-confirmed favourited boolean -- deliberately
 * NOT that response's replies_count/reblogs_count/favourites_count too:
 * those looked like a free, always-fresh echo of the whole toot's counts,
 * but weren't reliably present on every instance's response in practice,
 * and blindly copying them across zeroed this toot's OTHER counts
 * (Reply/Boost) on every single favourite toggle. See
 * FS3EMastodon_Favourite's comment. The resulting favourites_count is a
 * local +1/-1 delta the GUI applies itself -- see
 * TTIMELINE_UpdatePost/TTL_POSTUPD_FAVOURITED in fs3etoottimeline.h.
 */
typedef struct FS3ENetFavouriteReq {
    char *fs3efa_ApiBaseUrl;
    char *fs3efa_AccessToken;
    char *fs3efa_StatusId;
    BOOL  fs3efa_Favourite;   /* TRUE=favourite, FALSE=unfavourite */
} FS3ENetFavouriteReq;

FS3ENetFavouriteReq *FS3ENetFavouriteReq_Alloc(
    const char *apiBaseUrl, const char *accessToken,
    const char *statusId, BOOL favourite);

typedef struct FS3ENetFavouriteReply {
    char  *fs3efa_StatusId;
    BOOL   fs3efa_Favourited;
} FS3ENetFavouriteReply;

/*
 * FS3ENETQ_ACCOUNT_LOOKUP — GET /api/v1/accounts/lookup?acct=<acct>.
 * The entry point for opening a profile view (see TootTimeline's
 * TTIMELINE_ShowProfile): resolves an acct string ("user" or
 * "user@instance", no leading '@') to a full account. fs3eal_AccessToken
 * may be "" (unauthenticated lookup works for public accounts).
 *
 * On FS3ENETR_OK, fs3em_Data is replaced with an FS3ENetAccountLookupReply.
 */
typedef struct FS3ENetAccountLookupReq {
    char *fs3eal_ApiBaseUrl;
    char *fs3eal_AccessToken;
    char *fs3eal_Acct;
} FS3ENetAccountLookupReq;

FS3ENetAccountLookupReq *FS3ENetAccountLookupReq_Alloc(
    const char *apiBaseUrl, const char *accessToken, const char *acct);

typedef struct FS3ENetAccountLookupReply {
    FS3EMastodonAccount fs3eal_Account; /* fma_Note here is HTML-stripped, unlike FS3EMastodon_LookupAccount's raw output */
} FS3ENetAccountLookupReply;

/*
 * FS3ENETQ_RELATIONSHIP — GET /api/v1/accounts/relationships?id[]=<id>.
 * On FS3ENETR_OK, fs3em_Data is replaced with an FS3ENetRelationshipReply.
 */
typedef struct FS3ENetRelationshipReq {
    char *fs3erl_ApiBaseUrl;
    char *fs3erl_AccessToken;
    char *fs3erl_AccountId;
} FS3ENetRelationshipReq;

FS3ENetRelationshipReq *FS3ENetRelationshipReq_Alloc(
    const char *apiBaseUrl, const char *accessToken, const char *accountId);

typedef struct FS3ENetRelationshipReply {
    char *fs3erl_AccountId;
    BOOL  fs3erl_Following;
} FS3ENetRelationshipReply;

/*
 * FS3ENETQ_RELATIONSHIPS — GET /api/v1/accounts/relationships?id[]=<id>&id[]=<id>...,
 * one repeated id[] per account. Batch counterpart of FS3ENETQ_RELATIONSHIP
 * above: fired after an FS3ENETQ_ACCOUNTS_LIST reply lands, covering every
 * account id just added to the list (minus the connected user's own id --
 * Mastodon's relationships endpoint has no self-relationship to report), so
 * TTLAccountRow_Class rows can show a "Follows you" badge (see
 * TTL_POSTUPD_RELATIONSHIP in fs3etoottimeline.h). Unlike
 * FS3ENetRelationshipReply, this one also carries followed_by -- the
 * singular request/reply above only ever needed the connected user's own
 * following state for the profile header's Follow/Unfollow button, this one
 * needs both directions to know if the OTHER account follows back.
 *
 * A char*[fs3erls_Count] pointer array follows the header fields
 * immediately in memory (each entry pointing further into the same
 * AllocVec block, at the id string bytes packed after the array itself) --
 * same "pointer array then string bytes" layout FS3ENet_HandleAccountsList's
 * own reply already uses for its FS3EMastodonAccount[] trailing array.
 *
 * On FS3ENETR_OK, fs3em_Data is replaced with an FS3ENetRelationshipsReply.
 */
typedef struct FS3ENetRelationshipsReq {
    ULONG  fs3erls_AccountGeneration; /* opaque caller token; echoed in reply */
    ULONG  fs3erls_Count;
    char  *fs3erls_ApiBaseUrl;
    char  *fs3erls_AccessToken;
    /* char *fs3erls_AccountIds[fs3erls_Count] follows immediately */
} FS3ENetRelationshipsReq;

FS3ENetRelationshipsReq *FS3ENetRelationshipsReq_Alloc(
    ULONG accountGeneration, const char *apiBaseUrl, const char *accessToken,
    const char *const *accountIds, ULONG count);

typedef struct FS3ENetRelationshipEntry {
    char *fs3erle_AccountId;
    BOOL  fs3erle_Following;
    BOOL  fs3erle_FollowedBy;
} FS3ENetRelationshipEntry;

/* Header of the flat relationships reply block.
 * FS3ENetRelationshipEntry[fs3erls_Count] follows immediately in memory. */
typedef struct FS3ENetRelationshipsReply {
    ULONG fs3erls_AccountGeneration; /* echoed from request */
    ULONG fs3erls_Count;
} FS3ENetRelationshipsReply;

/*
 * FS3ENETQ_FOLLOW — POST /api/v1/accounts/:id/follow or .../unfollow.
 *
 * fs3efo_Follow selects which: TRUE = follow, FALSE = unfollow. On
 * FS3ENETR_OK, fs3em_Data is replaced with an FS3ENetFollowReply carrying
 * just the server-confirmed following boolean -- same "don't trust
 * anything beyond the one confirmed flag" rule as FS3ENETQ_FAVORITE (see
 * FS3ENetFavouriteReply's comment); the Relationship object this endpoint
 * returns doesn't even carry follower/following counts, so there's
 * nothing else to echo anyway. The resulting followers_count is a local
 * +1/-1 delta the GUI applies itself, mirroring TTL_POSTUPD_FAVOURITED.
 */
typedef struct FS3ENetFollowReq {
    char *fs3efo_ApiBaseUrl;
    char *fs3efo_AccessToken;
    char *fs3efo_AccountId;
    BOOL  fs3efo_Follow;   /* TRUE=follow, FALSE=unfollow */
} FS3ENetFollowReq;

FS3ENetFollowReq *FS3ENetFollowReq_Alloc(
    const char *apiBaseUrl, const char *accessToken,
    const char *accountId, BOOL follow);

typedef struct FS3ENetFollowReply {
    char *fs3efo_AccountId;
    BOOL  fs3efo_Following;
} FS3ENetFollowReply;

#endif /* FS3ENET_H */
