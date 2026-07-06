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
    FS3ENETQ_FLUSH_CACHE     /* delete every file in the disk cache               */
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
    char *fs3enf_Key;  /* caller key echoed in reply; for avatars: @acct string */
} FS3ENetFetchImageReq;

/* Allocates a flat request block for FETCH_IMAGE. FreeVec() when done.
 * key is echoed back in the reply so the caller knows which entry to update. */
FS3ENetFetchImageReq *FS3ENetFetchImageReq_Alloc(const char *url, const char *key);

typedef struct FS3ENetFetchImageReply
{
    char *fs3enf_LocalPath;  /* e.g. "PROGDIR:.cache/1a2b3c4d" */
    char *fs3enf_Key;        /* echoed from request */
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

/*
 * FS3ENETQ_TIMELINE — fetch the newest page of statuses for a timeline.
 *
 * fs3et_ViewModeBit identifies the UI channel (VIEWMODE_* value from
 * friendsh3ep.h); it is echoed unchanged into FS3ENetTimelineReply so the
 * GUI can route replies back to the right TootTimeline channel.
 * fs3et_AccessToken may be "" for public timelines.
 * fs3et_MaxId may be NULL/empty to request the newest page.
 *
 * On FS3ENETR_OK, fs3em_Data is replaced with a flat FS3ENetTimelineReply
 * block; fs3em_Data on error still points at the original request block.
 */
typedef struct FS3ENetTimelineReq {
    ULONG  fs3et_ViewModeBit;   /* echoed in reply */
    char  *fs3et_ApiBaseUrl;
    char  *fs3et_AccessToken;   /* "" = no auth (public timelines) */
    char  *fs3et_Timeline;      /* "home", "public", "public?local=true", … */
    char  *fs3et_MaxId;         /* "" = newest page */
} FS3ENetTimelineReq;

FS3ENetTimelineReq *FS3ENetTimelineReq_Alloc(ULONG viewModeBit,
    const char *apiBaseUrl, const char *accessToken,
    const char *timeline, const char *maxId);

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
} FS3ENetStatus;

/* Header of the flat timeline reply block.
 * Statuses follow immediately: (FS3ENetStatus *)(reply + 1)[i] */
typedef struct FS3ENetTimelineReply {
    ULONG fs3et_ViewModeBit; /* echoed from request */
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

#endif /* FS3ENET_H */
