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
    FS3ENETQ_FOLLOW          /* toggle follow/unfollow on an account */
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
 * FS3ENETQ_FETCH_IMAGE — fetch a media URL (avatar, attachment thumbnail,
 * custom emoji) and cache it under T:FS3ECache/.  The network process serves
 * from disk cache when the file is already present; it only hits the network
 * on a cache miss.
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
} FS3ENetFetchImageReq;

/* Allocates a flat request block for FETCH_IMAGE. FreeVec() when done.
 * key is echoed back in the reply so the caller knows which entry to update. */
FS3ENetFetchImageReq *FS3ENetFetchImageReq_Alloc(const char *url, const char *key,
                                                   const char *subdir, BOOL keepOriginal);

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
 * Start the network process. cacheDir is the path passed to FS3ECache_Init()
 * inside the new process; pass NULL to use FS3ECACHE_DEFAULT_DIR.
 * Returns the request MsgPort, or NULL on failure.
 */
struct MsgPort *FS3ENet_Start(const char *cacheDir);

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

/*
 * FS3ENETQ_TIMELINE — fetch one page of statuses for a timeline.
 *
 * fs3et_ViewModeBit identifies the UI channel (VIEWMODE_* value from
 * friendsh3ep.h); it is echoed unchanged into FS3ENetTimelineReply so the
 * GUI can route replies back to the right TootTimeline channel.
 * fs3et_AccessToken may be "" for public timelines.
 * fs3et_MaxId/fs3et_MinId may be NULL/empty; at most one should be set (see
 * fs3et_PageDirection) -- fs3et_MaxId asks for statuses strictly older than
 * that status id (contiguous with what the GUI already has at the bottom
 * of its list), fs3et_MinId strictly newer (contiguous at the top). Both
 * empty means "the newest page" (FS3ENETPAGE_INITIAL).
 *
 * On FS3ENETR_OK, fs3em_Data is replaced with a flat FS3ENetTimelineReply
 * block; fs3em_Data on error still points at the original request block.
 */
typedef struct FS3ENetTimelineReq {
    ULONG  fs3et_ViewModeBit;    /* echoed in reply */
    ULONG  fs3et_PageDirection;  /* FS3ENetPageDirection; echoed in reply */
    char  *fs3et_ApiBaseUrl;
    char  *fs3et_AccessToken;    /* "" = no auth (public timelines) */
    char  *fs3et_Timeline;       /* "home", "public", "public?local=true", … */
    char  *fs3et_MaxId;          /* "" = no lower bound */
    char  *fs3et_MinId;          /* "" = no upper bound */
} FS3ENetTimelineReq;

FS3ENetTimelineReq *FS3ENetTimelineReq_Alloc(ULONG viewModeBit,
    ULONG pageDirection, const char *apiBaseUrl, const char *accessToken,
    const char *timeline, const char *maxId, const char *minId);

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
} FS3ENetStatus;

/* Header of the flat timeline reply block.
 * Statuses follow immediately: (FS3ENetStatus *)(reply + 1)[i] */
typedef struct FS3ENetTimelineReply {
    ULONG fs3et_ViewModeBit;    /* echoed from request */
    ULONG fs3et_PageDirection; /* echoed from request, see FS3ENetPageDirection */
    ULONG fs3et_Count;
    /* FS3ENetStatus[fs3et_Count] follows immediately in memory */
} FS3ENetTimelineReply;

/*
 * FS3ENETQ_POST_STATUS — publish a new status (toot).
 *
 * fs3ep_Spoiler is the CW/subject text; pass "" for no content warning.
 * On FS3ENETR_OK, fs3em_Data is replaced with an FS3ENetPostStatusReply.
 */
typedef struct FS3ENetPostStatusReq {
    char *fs3ep_ApiBaseUrl;
    char *fs3ep_AccessToken;
    char *fs3ep_Content;     /* UTF-8 post body */
    char *fs3ep_Visibility;  /* "public", "unlisted", "private", "direct" */
    char *fs3ep_Spoiler;     /* CW text; "" = no content warning */
} FS3ENetPostStatusReq;

FS3ENetPostStatusReq *FS3ENetPostStatusReq_Alloc(
    const char *apiBaseUrl, const char *accessToken,
    const char *content, const char *visibility, const char *spoiler);

typedef struct FS3ENetPostStatusReply {
    char *fs3ep_StatusId; /* new status id string */
} FS3ENetPostStatusReply;

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
