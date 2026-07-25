/*
 * FriendSh3ep network process - startup, shutdown and request dispatch.
 *
 * See fs3enet.h for the public API and ../ARCHITECTURE.md for the design.
 */

#include "fs3enet.h"
#include "fs3enet_http.h"
#include "fs3enet_mastodon.h"
#include "fs3enet_cache.h"

#include <dos/dos.h>
#include <dos/dostags.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <stdio.h>
#include <string.h>

#include "bdbprintf.h"

#define FS3ENET_STACK_SIZE 65536
#define FS3ENET_PROC_NAME  "FriendSh3ep-net"

/* App name FriendSh3ep registers itself under via FS3EMastodon_CreateApp(). */
#define FS3ENET_CLIENT_NAME "AmigaOS3 FriendSh3ep Beta"

/* ---- Flat-block helpers --------------------------------------------------
 * All IPC structs are a single AllocVec block: [struct header][string data].
 * char * fields in the struct point into the same block, so one FreeVec()
 * frees everything.
 */

/* Returns the number of bytes needed to store s (including NUL), or 1 for
 * NULL (we always write at least a NUL terminator). */
static ULONG FS3ENet_PackLen(const char *s)
{
    return s ? (ULONG)(strlen(s) + 1) : 1;
}

/* Writes s (or an empty string) starting at *p, sets *dst = *p, advances *p
 * by the number of bytes written. */
static void FS3ENet_PackStr(char **dst, char **p, const char *s)
{
    ULONG n = s ? (ULONG)(strlen(s) + 1) : 1;
    *dst = *p;
    if (s)
        CopyMem(s, *p, n);
    else
        **p = '\0';
    *p += n;
}

/* Same as FS3ENet_PackStr, but collapses any run of \r/\n into a single
 * space (never a leading/trailing one) instead of copying it verbatim --
 * for display-name-like fields ONLY (never body content, which
 * legitimately wraps across real lines: see ttl_post_layout's word-wrap).
 * Some Mastodon accounts embed literal newlines in their display_name,
 * which breaks TootTimeline's single-line username/boostBy rendering.
 * Always advances *p by FS3ENet_PackLen(s)'s reserved byte count, same as
 * FS3ENet_PackStr -- collapsing only ever shortens the string, so the
 * pass-1-sized region is never overrun, just partly unused at the end. */
static void FS3ENet_PackStrClean(char **dst, char **p, const char *s)
{
    ULONG n = s ? (ULONG)(strlen(s) + 1) : 1;
    char *out = *p;
    *dst = *p;
    if (s) {
        const char *src = s;
        char *w = out;
        BOOL sawBreak = FALSE;
        while (*src) {
            if (*src == '\n' || *src == '\r') {
                sawBreak = TRUE;
            } else {
                if (sawBreak && w > out) *w++ = ' ';
                sawBreak = FALSE;
                *w++ = *src;
            }
            src++;
        }
        *w = '\0';
    } else {
        *out = '\0';
    }
    *p += n;
}

/* ---- Public _Alloc helpers (called by the GUI before PutMsg) ------------ */

FS3ENetLoginStartReq *FS3ENetLoginStartReq_Alloc(const char *apiBaseUrl)
{
    ULONG total = sizeof(FS3ENetLoginStartReq) + FS3ENet_PackLen(apiBaseUrl);
    FS3ENetLoginStartReq *req =
        (FS3ENetLoginStartReq *)AllocVec(total, MEMF_ANY| MEMF_PUBLIC);
    char *p;

    if (!req) return NULL;
    p = (char *)req + sizeof(*req);
    FS3ENet_PackStr(&req->fs3enl_ApiBaseUrl, &p, apiBaseUrl);
    return req;
}

FS3ENetLoginFinishReq *FS3ENetLoginFinishReq_Alloc(const char *apiBaseUrl,
    const char *clientId, const char *clientSecret, const char *code)
{
    ULONG total = sizeof(FS3ENetLoginFinishReq)
                + FS3ENet_PackLen(apiBaseUrl)
                + FS3ENet_PackLen(clientId)
                + FS3ENet_PackLen(clientSecret)
                + FS3ENet_PackLen(code);
    FS3ENetLoginFinishReq *req =
        (FS3ENetLoginFinishReq *)AllocVec(total, MEMF_ANY | MEMF_PUBLIC);
    char *p;

    if (!req) return NULL;
    p = (char *)req + sizeof(*req);
    FS3ENet_PackStr(&req->fs3enl_ApiBaseUrl,   &p, apiBaseUrl);
    FS3ENet_PackStr(&req->fs3enl_ClientId,     &p, clientId);
    FS3ENet_PackStr(&req->fs3enl_ClientSecret, &p, clientSecret);
    FS3ENet_PackStr(&req->fs3enl_Code,         &p, code);
    return req;
}

FS3ENetVerifyAccountReq *FS3ENetVerifyAccountReq_Alloc(const char *apiBaseUrl,
    const char *accessToken)
{
    ULONG total = sizeof(FS3ENetVerifyAccountReq)
                + FS3ENet_PackLen(apiBaseUrl)
                + FS3ENet_PackLen(accessToken);
    FS3ENetVerifyAccountReq *req =
        (FS3ENetVerifyAccountReq *)AllocVec(total, MEMF_ANY| MEMF_PUBLIC);
    char *p;

    if (!req) return NULL;
    p = (char *)req + sizeof(*req);
    FS3ENet_PackStr(&req->fs3eva_ApiBaseUrl,  &p, apiBaseUrl);
    FS3ENet_PackStr(&req->fs3eva_AccessToken, &p, accessToken);
    return req;
}

FS3ENetInstanceInfoReq *FS3ENetInstanceInfoReq_Alloc(const char *apiBaseUrl)
{
    ULONG total = sizeof(FS3ENetInstanceInfoReq) + FS3ENet_PackLen(apiBaseUrl);
    FS3ENetInstanceInfoReq *req =
        (FS3ENetInstanceInfoReq *)AllocVec(total, MEMF_ANY | MEMF_PUBLIC);
    char *p;

    if (!req) return NULL;
    p = (char *)req + sizeof(*req);
    FS3ENet_PackStr(&req->fs3eii_ApiBaseUrl, &p, apiBaseUrl);
    return req;
}

FS3ENetTimelineReq *FS3ENetTimelineReq_Alloc(ULONG viewModeBit,
    ULONG pageDirection, ULONG accountGeneration, ULONG responseShape,
    const char *apiBaseUrl, const char *accessToken, const char *timeline,
    const char *maxId, const char *minId, const char *searchQuery)
{
    ULONG total = sizeof(FS3ENetTimelineReq)
                + FS3ENet_PackLen(apiBaseUrl)
                + FS3ENet_PackLen(accessToken)
                + FS3ENet_PackLen(timeline)
                + FS3ENet_PackLen(maxId)
                + FS3ENet_PackLen(minId)
                + FS3ENet_PackLen(searchQuery);
    FS3ENetTimelineReq *req =
        (FS3ENetTimelineReq *)AllocVec(total, MEMF_ANY| MEMF_PUBLIC);
    char *p;

    if (!req) return NULL;
    req->fs3et_ViewModeBit      = viewModeBit;
    req->fs3et_PageDirection    = pageDirection;
    req->fs3et_AccountGeneration = accountGeneration;
    req->fs3et_ResponseShape    = responseShape;
    p = (char *)req + sizeof(*req);
    FS3ENet_PackStr(&req->fs3et_ApiBaseUrl,   &p, apiBaseUrl);
    FS3ENet_PackStr(&req->fs3et_AccessToken,  &p, accessToken);
    FS3ENet_PackStr(&req->fs3et_Timeline,     &p, timeline);
    FS3ENet_PackStr(&req->fs3et_MaxId,        &p, maxId);
    FS3ENet_PackStr(&req->fs3et_MinId,        &p, minId);
    FS3ENet_PackStr(&req->fs3et_SearchQuery,  &p, searchQuery);
    return req;
}

FS3ENetPostStatusReq *FS3ENetPostStatusReq_Alloc(
    const char *apiBaseUrl, const char *accessToken,
    const char *content, const char *visibility, const char *spoiler,
    const char *inReplyToId, const char *quoteApprovalPolicy)
{
    ULONG total = sizeof(FS3ENetPostStatusReq)
                + FS3ENet_PackLen(apiBaseUrl)
                + FS3ENet_PackLen(accessToken)
                + FS3ENet_PackLen(content)
                + FS3ENet_PackLen(visibility)
                + FS3ENet_PackLen(spoiler)
                + FS3ENet_PackLen(inReplyToId)
                + FS3ENet_PackLen(quoteApprovalPolicy);
    FS3ENetPostStatusReq *req =
        (FS3ENetPostStatusReq *)AllocVec(total, MEMF_ANY | MEMF_PUBLIC);
    char *p;

    if (!req) return NULL;
    p = (char *)req + sizeof(*req);
    FS3ENet_PackStr(&req->fs3ep_ApiBaseUrl,   &p, apiBaseUrl);
    FS3ENet_PackStr(&req->fs3ep_AccessToken,  &p, accessToken);
    FS3ENet_PackStr(&req->fs3ep_Content,      &p, content);
    FS3ENet_PackStr(&req->fs3ep_Visibility,   &p, visibility);
    FS3ENet_PackStr(&req->fs3ep_Spoiler,      &p, spoiler);
    FS3ENet_PackStr(&req->fs3ep_InReplyToId,  &p, inReplyToId);
    FS3ENet_PackStr(&req->fs3ep_QuoteApprovalPolicy, &p, quoteApprovalPolicy);
    return req;
}

FS3ENetEditStatusReq *FS3ENetEditStatusReq_Alloc(
    const char *apiBaseUrl, const char *accessToken,
    const char *statusId, const char *content,
    const char *const *mediaIds, ULONG mediaCount)
{
    ULONG total = sizeof(FS3ENetEditStatusReq)
                + FS3ENet_PackLen(apiBaseUrl)
                + FS3ENet_PackLen(accessToken)
                + FS3ENet_PackLen(statusId)
                + FS3ENet_PackLen(content);
    FS3ENetEditStatusReq *req;
    char *p;
    ULONG i;

    if (mediaCount > FS3ENET_MAX_MEDIA) mediaCount = FS3ENET_MAX_MEDIA;
    for (i = 0; i < mediaCount; i++)
        total += FS3ENet_PackLen(mediaIds ? mediaIds[i] : NULL);

    req = (FS3ENetEditStatusReq *)AllocVec(total, MEMF_ANY | MEMF_PUBLIC);
    if (!req) return NULL;

    p = (char *)req + sizeof(*req);
    FS3ENet_PackStr(&req->fs3ee_ApiBaseUrl,  &p, apiBaseUrl);
    FS3ENet_PackStr(&req->fs3ee_AccessToken, &p, accessToken);
    FS3ENet_PackStr(&req->fs3ee_StatusId,    &p, statusId);
    FS3ENet_PackStr(&req->fs3ee_Content,     &p, content);
    for (i = 0; i < mediaCount; i++)
        FS3ENet_PackStr(&req->fs3ee_MediaIds[i], &p, mediaIds ? mediaIds[i] : NULL);
    for (; i < FS3ENET_MAX_MEDIA; i++)
        req->fs3ee_MediaIds[i] = NULL;
    req->fs3ee_MediaCount = mediaCount;

    return req;
}

FS3ENetDeleteStatusReq *FS3ENetDeleteStatusReq_Alloc(
    const char *apiBaseUrl, const char *accessToken, const char *statusId)
{
    ULONG total = sizeof(FS3ENetDeleteStatusReq)
                + FS3ENet_PackLen(apiBaseUrl)
                + FS3ENet_PackLen(accessToken)
                + FS3ENet_PackLen(statusId);
    FS3ENetDeleteStatusReq *req =
        (FS3ENetDeleteStatusReq *)AllocVec(total, MEMF_ANY | MEMF_PUBLIC);
    char *p;

    if (!req) return NULL;
    p = (char *)req + sizeof(*req);
    FS3ENet_PackStr(&req->fs3ed_ApiBaseUrl,  &p, apiBaseUrl);
    FS3ENet_PackStr(&req->fs3ed_AccessToken, &p, accessToken);
    FS3ENet_PackStr(&req->fs3ed_StatusId,    &p, statusId);
    return req;
}

FS3ENetNotificationsReq *FS3ENetNotificationsReq_Alloc(ULONG pageDirection,
    ULONG accountGeneration, const char *apiBaseUrl, const char *accessToken,
    const char *maxId, const char *minId)
{
    ULONG total = sizeof(FS3ENetNotificationsReq)
                + FS3ENet_PackLen(apiBaseUrl)
                + FS3ENet_PackLen(accessToken)
                + FS3ENet_PackLen(maxId)
                + FS3ENet_PackLen(minId);
    FS3ENetNotificationsReq *req =
        (FS3ENetNotificationsReq *)AllocVec(total, MEMF_ANY | MEMF_PUBLIC);
    char *p;

    if (!req) return NULL;
    req->fs3en_PageDirection     = pageDirection;
    req->fs3en_AccountGeneration = accountGeneration;
    p = (char *)req + sizeof(*req);
    FS3ENet_PackStr(&req->fs3en_ApiBaseUrl,  &p, apiBaseUrl);
    FS3ENet_PackStr(&req->fs3en_AccessToken, &p, accessToken);
    FS3ENet_PackStr(&req->fs3en_MaxId,       &p, maxId);
    FS3ENet_PackStr(&req->fs3en_MinId,       &p, minId);
    return req;
}

FS3ENetAccountsListReq *FS3ENetAccountsListReq_Alloc(ULONG kind,
    ULONG accountGeneration, const char *apiBaseUrl, const char *accessToken,
    const char *accountId, const char *query)
{
    ULONG total = sizeof(FS3ENetAccountsListReq)
                + FS3ENet_PackLen(apiBaseUrl)
                + FS3ENet_PackLen(accessToken)
                + FS3ENet_PackLen(accountId)
                + FS3ENet_PackLen(query);
    FS3ENetAccountsListReq *req =
        (FS3ENetAccountsListReq *)AllocVec(total, MEMF_ANY | MEMF_PUBLIC);
    char *p;

    if (!req) return NULL;
    req->fs3eal_Kind              = kind;
    req->fs3eal_AccountGeneration = accountGeneration;
    p = (char *)req + sizeof(*req);
    FS3ENet_PackStr(&req->fs3eal_ApiBaseUrl,  &p, apiBaseUrl);
    FS3ENet_PackStr(&req->fs3eal_AccessToken, &p, accessToken);
    FS3ENet_PackStr(&req->fs3eal_AccountId,   &p, accountId);
    FS3ENet_PackStr(&req->fs3eal_Query,       &p, query);
    return req;
}

FS3ENetFavouriteReq *FS3ENetFavouriteReq_Alloc(
    const char *apiBaseUrl, const char *accessToken,
    const char *statusId, BOOL favourite)
{
    ULONG total = sizeof(FS3ENetFavouriteReq)
                + FS3ENet_PackLen(apiBaseUrl)
                + FS3ENet_PackLen(accessToken)
                + FS3ENet_PackLen(statusId);
    FS3ENetFavouriteReq *req =
        (FS3ENetFavouriteReq *)AllocVec(total, MEMF_ANY | MEMF_PUBLIC);
    char *p;

    if (!req) return NULL;
    p = (char *)req + sizeof(*req);
    FS3ENet_PackStr(&req->fs3efa_ApiBaseUrl,  &p, apiBaseUrl);
    FS3ENet_PackStr(&req->fs3efa_AccessToken, &p, accessToken);
    FS3ENet_PackStr(&req->fs3efa_StatusId,    &p, statusId);
    req->fs3efa_Favourite = favourite;
    return req;
}

FS3ENetAccountLookupReq *FS3ENetAccountLookupReq_Alloc(
    const char *apiBaseUrl, const char *accessToken, const char *acct)
{
    ULONG total = sizeof(FS3ENetAccountLookupReq)
                + FS3ENet_PackLen(apiBaseUrl)
                + FS3ENet_PackLen(accessToken)
                + FS3ENet_PackLen(acct);
    FS3ENetAccountLookupReq *req =
        (FS3ENetAccountLookupReq *)AllocVec(total, MEMF_ANY | MEMF_PUBLIC);
    char *p;

    if (!req) return NULL;
    p = (char *)req + sizeof(*req);
    FS3ENet_PackStr(&req->fs3eal_ApiBaseUrl,  &p, apiBaseUrl);
    FS3ENet_PackStr(&req->fs3eal_AccessToken, &p, accessToken);
    FS3ENet_PackStr(&req->fs3eal_Acct,        &p, acct);
    return req;
}

FS3ENetRelationshipReq *FS3ENetRelationshipReq_Alloc(
    const char *apiBaseUrl, const char *accessToken, const char *accountId)
{
    ULONG total = sizeof(FS3ENetRelationshipReq)
                + FS3ENet_PackLen(apiBaseUrl)
                + FS3ENet_PackLen(accessToken)
                + FS3ENet_PackLen(accountId);
    FS3ENetRelationshipReq *req =
        (FS3ENetRelationshipReq *)AllocVec(total, MEMF_ANY | MEMF_PUBLIC);
    char *p;

    if (!req) return NULL;
    p = (char *)req + sizeof(*req);
    FS3ENet_PackStr(&req->fs3erl_ApiBaseUrl,  &p, apiBaseUrl);
    FS3ENet_PackStr(&req->fs3erl_AccessToken, &p, accessToken);
    FS3ENet_PackStr(&req->fs3erl_AccountId,   &p, accountId);
    return req;
}

/* accountIds/count -- see FS3ENetRelationshipsReq's header comment for the
 * resulting block layout: header, then a char*[count] pointer array, then
 * the pointed-to string bytes (ApiBaseUrl/AccessToken/each id), same
 * "pointer array right after the header" shape FS3ENet_HandleAccountsList's
 * own reply uses for its FS3EMastodonAccount[] trailing array. */
FS3ENetRelationshipsReq *FS3ENetRelationshipsReq_Alloc(
    ULONG accountGeneration, const char *apiBaseUrl, const char *accessToken,
    const char *const *accountIds, ULONG count)
{
    ULONG total = sizeof(FS3ENetRelationshipsReq)
                + count * sizeof(char *)
                + FS3ENet_PackLen(apiBaseUrl)
                + FS3ENet_PackLen(accessToken);
    FS3ENetRelationshipsReq *req;
    char **ids;
    char *p;
    ULONG i;

    for (i = 0; i < count; i++)
        total += FS3ENet_PackLen(accountIds[i]);

    req = (FS3ENetRelationshipsReq *)AllocVec(total, MEMF_ANY | MEMF_PUBLIC);
    if (!req) return NULL;

    req->fs3erls_AccountGeneration = accountGeneration;
    req->fs3erls_Count             = count;

    ids = (char **)(req + 1);
    p   = (char *)(ids + count);

    FS3ENet_PackStr(&req->fs3erls_ApiBaseUrl,  &p, apiBaseUrl);
    FS3ENet_PackStr(&req->fs3erls_AccessToken, &p, accessToken);
    for (i = 0; i < count; i++)
        FS3ENet_PackStr(&ids[i], &p, accountIds[i]);

    return req;
}

FS3ENetFollowReq *FS3ENetFollowReq_Alloc(
    const char *apiBaseUrl, const char *accessToken,
    const char *accountId, BOOL follow)
{
    ULONG total = sizeof(FS3ENetFollowReq)
                + FS3ENet_PackLen(apiBaseUrl)
                + FS3ENet_PackLen(accessToken)
                + FS3ENet_PackLen(accountId);
    FS3ENetFollowReq *req =
        (FS3ENetFollowReq *)AllocVec(total, MEMF_ANY | MEMF_PUBLIC);
    char *p;

    if (!req) return NULL;
    p = (char *)req + sizeof(*req);
    FS3ENet_PackStr(&req->fs3efo_ApiBaseUrl,  &p, apiBaseUrl);
    FS3ENet_PackStr(&req->fs3efo_AccessToken, &p, accessToken);
    FS3ENet_PackStr(&req->fs3efo_AccountId,   &p, accountId);
    req->fs3efo_Follow = follow;
    return req;
}

FS3ENetFetchImageReq *FS3ENetFetchImageReq_Alloc(const char *url, const char *key,
                                                   const char *subdir, BOOL keepOriginal,
                                                   BOOL wantProgress)
{
    ULONG total = sizeof(FS3ENetFetchImageReq)
                + FS3ENet_PackLen(url)
                + FS3ENet_PackLen(key)
                + FS3ENet_PackLen(subdir);
    FS3ENetFetchImageReq *req =
        (FS3ENetFetchImageReq *)AllocVec(total, MEMF_ANY | MEMF_PUBLIC);
    char *p;

    if (!req) return NULL;
    p = (char *)req + sizeof(*req);
    FS3ENet_PackStr(&req->fs3enf_Url,    &p, url);
    FS3ENet_PackStr(&req->fs3enf_Key,    &p, key ? key : "");
    FS3ENet_PackStr(&req->fs3enf_Subdir, &p, subdir ? subdir : "");
    req->fs3enf_KeepOriginal  = keepOriginal;
    req->fs3enf_WantProgress = wantProgress;
    return req;
}

/* Handshake message used once at startup so FS3ENet_Start() can hand the
 * new process' request port back to the caller. Lives on FS3ENet_Start()'s
 * stack; FS3ENet_Start() stays blocked in WaitPort() until the child has
 * filled it in and replied, so its lifetime is safe.
 *
 * fs3ess_CacheDir points at the caller's string (app->settings.cachePath).
 * It is read by the child before it replies, while the parent is still
 * blocked, so the pointer is always valid. */
struct FS3ENetStartup
{
    struct Message  fs3ess_Msg;
    struct MsgPort  *fs3ess_RequestPort;
    const char      *fs3ess_CacheDir;
    ULONG            fs3ess_MaxCacheSizeMB;  /* see FS3ECache_Init's maxSizeMB */
};

/* The OS3 NDK has no NP_UserData tag for CreateNewProc(), so the startup
 * struct is handed to the child via this global instead. FS3ENet_Start()
 * never runs concurrently with itself (single network process), and the
 * child reads g_FS3ENetStartup before FS3ENet_Start() could be called again. */
static struct FS3ENetStartup *g_FS3ENetStartup;

/* Set once by FS3ENet_ProcEntry right after CreateMsgPort() succeeds, so
 * FS3ENet_Stop() can Signal() the process directly -- see g_FS3ENetStopSigBit
 * and the "stopping" fast-path in FS3ENet_ProcEntry's loop below. */
static struct Task *g_FS3ENetTask = NULL;

/* Private signal bit for the shutdown fast-path, AllocSignal()'d by
 * FS3ENet_ProcEntry at startup -- deliberately NOT SIGBREAKF_CTRL_C.
 * bsdsocket.library aborts any blocking socket call with EINTR whenever
 * CTRL_C is pending on the calling task (its default break mask), and
 * libnix's own chkabort() polls/consumes CTRL_C to implement user-visible
 * Ctrl-C abort -- reusing that bit for our own signaling raced with both of
 * those and caused intermittent bogus HTTP failures and abandoned requests
 * during ordinary (non-shutdown) operation. -1 means AllocSignal() failed;
 * the mask is then 0 and the fast-path silently never triggers, degrading
 * to a plain full-backlog-drain shutdown rather than risking a crash. */
static LONG g_FS3ENetStopSigBit = -1;
#define FS3ENET_STOP_SIGMASK  ((g_FS3ENetStopSigBit >= 0) ? (1UL << g_FS3ENetStopSigBit) : 0UL)

static void FS3ENet_ProcEntry(void);
static BOOL FS3ENet_Dispatch(FS3ENetMessage *fs3em);
static void FS3ENet_AbortAllActiveDownloads(void);
static void FS3ENet_StepActiveDownloads(void);

/* Head of the chunked-download list -- see "Chunked media downloads" above
 * FS3ENet_HandleFetchImage() further down for the full struct definition
 * and design. Declared here (against the incomplete struct tag) so
 * FS3ENet_ProcEntry()'s loop, which appears before that section, can check
 * whether any download is active without needing the full type. */
static struct FS3ENetActiveDownload *g_FS3EActiveDownloads = NULL;
static ULONG                         g_FS3EActiveDownloadCount = 0;

struct MsgPort *FS3ENet_Start(const char *cacheDir, ULONG maxCacheSizeMB)
{
    struct MsgPort     *replyPort;
    struct FS3ENetStartup startup;
    struct Process     *proc;

    replyPort = CreateMsgPort();
    if (!replyPort)
        return NULL;

    startup.fs3ess_Msg.mn_ReplyPort = replyPort;
    startup.fs3ess_Msg.mn_Length    = sizeof(startup);
    startup.fs3ess_RequestPort      = NULL;
    startup.fs3ess_CacheDir         = cacheDir;  /* may be NULL → default */
    startup.fs3ess_MaxCacheSizeMB   = maxCacheSizeMB;

    g_FS3ENetStartup = &startup;

    proc = CreateNewProcTags(
        NP_Entry,     (ULONG)FS3ENet_ProcEntry,
        NP_Name,      (ULONG)FS3ENET_PROC_NAME,
        NP_StackSize, (ULONG)FS3ENET_STACK_SIZE,
        TAG_DONE);

    if (!proc)
    {
        DeleteMsgPort(replyPort);
        return NULL;
    }

    WaitPort(replyPort);
    GetMsg(replyPort);
    DeleteMsgPort(replyPort);

    return startup.fs3ess_RequestPort;
}

void FS3ENet_Stop(struct MsgPort *requestPort, struct MsgPort *replyPort)
{
    FS3ENetMessage msg;

    if (!requestPort)
        return;

    /* Signal first, before the shutdown message even goes in the queue:
     * lets the process abandon (each with an immediate error reply, not
     * silently) whatever's still queued behind whatever single request
     * it's currently mid-dispatch on, instead of working through the
     * entire backlog in strict FIFO order before it even looks at the
     * shutdown message sitting at the back -- see the "stopping"
     * fast-path in FS3ENet_ProcEntry. */
    if (g_FS3ENetTask && FS3ENET_STOP_SIGMASK) Signal(g_FS3ENetTask, FS3ENET_STOP_SIGMASK);

    /* Zero first -- mn_Node.ln_Pri (and any other Message/Node fields we
     * don't set explicitly) would otherwise be whatever garbage was on the
     * stack, and PutMsg()/Enqueue() sorts by ln_Pri: a stray negative value
     * could in principle land this behind FS3ENETQ_FETCH_IMAGE's priority
     * -5 (see FS3EApp_NetSend), though the stopping fast-path still drains
     * down to it either way. */
    memset(&msg, 0, sizeof(msg));
    msg.fs3em_Msg.mn_ReplyPort = replyPort;
    msg.fs3em_Msg.mn_Length    = sizeof(msg);
    msg.fs3em_Type             = FS3ENETQ_SHUTDOWN;

    PutMsg(requestPort, (struct Message *)&msg);

    WaitPort(replyPort);
    GetMsg(replyPort);
}

BOOL FS3ENet_FlushCache(struct MsgPort *requestPort, struct MsgPort *replyPort)
{
    FS3ENetMessage msg;

    if (!requestPort)
        return FALSE;

    memset(&msg, 0, sizeof(msg));
    msg.fs3em_Msg.mn_ReplyPort = replyPort;
    msg.fs3em_Msg.mn_Length    = sizeof(msg);
    msg.fs3em_Type             = FS3ENETQ_FLUSH_CACHE;

    PutMsg(requestPort, (struct Message *)&msg);

    WaitPort(replyPort);
    GetMsg(replyPort);

    return (msg.fs3em_Result == FS3ENETR_OK);
}

/* Debug: peek how many requests are queued on requestPort without removing
 * any of them. Disable()/Enable() brackets the walk against a concurrent
 * PutMsg() from the main process (Exec's documented safe way to inspect a
 * MsgPort's message list without taking it off). Only used for the
 * bdbprintf_now() backlog tracing in FS3ENet_ProcEntry below. */
static ULONG FS3ENet_CountPending(struct MsgPort *port)
{
    struct Node *n;
    ULONG        count = 0;

    Disable();
    for (n = port->mp_MsgList.lh_Head; n->ln_Succ; n = n->ln_Succ)
        count++;
    Enable();

    return count;
}

/* Entry point of the network process, running as its own AmigaDOS task. */
static void FS3ENet_ProcEntry(void)
{
    struct FS3ENetStartup *startup = g_FS3ENetStartup;
    struct MsgPort       *requestPort;
    FS3ENetMessage        *shutdownMsg = NULL;
    BOOL                  running  = TRUE;
    BOOL                  stopping = FALSE;

    requestPort = CreateMsgPort();
    g_FS3ENetTask = FindTask(NULL);
    g_FS3ENetStopSigBit = AllocSignal(-1);

    /* AmiSSL/bsdsocket must be opened from the task that uses them; if this
     * fails, give up and report failure (NULL request port) to
     * FS3ENet_Start(), same as if CreateMsgPort() itself had failed. */
    if (requestPort && !FS3EHttp_Init())
    {
        DeleteMsgPort(requestPort);
        requestPort = NULL;
    }

    /* Disk cache — non-fatal: image fetches will return HTTP_ERROR if the
     * cache dir cannot be created, but login and timeline fetches still work. */
    if (requestPort)
        FS3ECache_Init(startup->fs3ess_CacheDir, startup->fs3ess_MaxCacheSizeMB);

    /* Hand the request port (or NULL on failure) back to FS3ENet_Start(). */
    startup->fs3ess_RequestPort = requestPort;
    PutMsg(startup->fs3ess_Msg.mn_ReplyPort, &startup->fs3ess_Msg);

    if (!requestPort)
        return;

    while (running)
    {
        FS3ENetMessage *fs3em;

        /* Only block when there's no chunked download to advance --
         * GetMsg() below is always non-blocking regardless, so WaitPort()
         * is the only thing that would sleep past this chunk's turn. See
         * "Chunked media downloads" above FS3ENet_HandleFetchImage() for
         * why a download can be sitting in g_FS3EActiveDownloads here. */
        if (!g_FS3EActiveDownloads)
            WaitPort(requestPort);
#ifdef BDBTRACEMULTIPART
         bdbprintf_now("FS3ENet: woke up, %lu request(s) waiting\n",
                       (unsigned long)FS3ENet_CountPending(requestPort));
#endif
        /* FS3ENet_Stop() Signal()s this before the shutdown message even
         * reaches the queue -- once noticed, every message still queued
         * behind whatever's currently dispatching gets an immediate error
         * reply instead of actually being worked (real HTTP fetches), so
         * shutdown doesn't have to wait out the entire backlog in FIFO
         * order. The one thing this can't shorten is a request already
         * mid-dispatch when the signal arrives (e.g. a slow HTTP GET already
         * under way) -- that one still runs to completion. Any chunked
         * download already registered gets the same immediate-abandon
         * treatment via FS3ENet_AbortAllActiveDownloads() -- its held
         * original request is replied with an error right here rather than
         * being left to finish chunk by chunk. */
        if (!stopping && FS3ENET_STOP_SIGMASK &&
            (SetSignal(0, FS3ENET_STOP_SIGMASK) & FS3ENET_STOP_SIGMASK))
        {
            stopping = TRUE;
            FS3ENet_AbortAllActiveDownloads();
        }

        while ((fs3em = (FS3ENetMessage *)GetMsg(requestPort)) != NULL)
        {
            BOOL deferred = FALSE;

            if (fs3em->fs3em_Type == FS3ENETQ_SHUTDOWN)
            {
                /* Hold this one instead of replying immediately: ReplyMsg()
                 * wakes FS3ENet_Stop() in the main process right away, and
                 * from that instant on the main process is free to race
                 * ahead toward tearing down the whole executable image --
                 * but this task and the main task share that one loaded
                 * image (no separate address space), so any cleanup code
                 * still left to run here (closing AmiSSL/bsdsocket below,
                 * which does real work) would then be racing its own
                 * unmapping. Replying only after that cleanup closes the
                 * window down to (at most) this task's own tiny process-exit
                 * glue, instead of however long FS3EHttp_Cleanup() takes. */
                running = FALSE;
                fs3em->fs3em_Result = FS3ENETR_OK;
                shutdownMsg = fs3em;
                continue;
            }
            else if (stopping)
            {
                fs3em->fs3em_Result = FS3ENETR_NETWORK_ERROR;
            }
            else
            {
                /* FALSE means fs3em is a FETCH_IMAGE cache miss now owned by
                 * the chunked download engine (see FS3ENet_HandleFetchImage)
                 * -- it gets ReplyMsg()'d later, from FS3ENet_FinishDownload,
                 * not here. */
                deferred = !FS3ENet_Dispatch(fs3em);
                if (!deferred && !stopping && FS3ENET_STOP_SIGMASK &&
                    (SetSignal(0, FS3ENET_STOP_SIGMASK) & FS3ENET_STOP_SIGMASK))
                {
                    stopping = TRUE;
                    FS3ENet_AbortAllActiveDownloads();
                }
            }

            if (!deferred)
                ReplyMsg((struct Message *)fs3em);
        }

        /* Advance exactly one chunk of the next active download (round
         * robin), interleaved with the request-port drain above so a large
         * in-flight transfer never starves other queued work. On the way
         * out (shutdown just noticed, or the signal-based fast-path above
         * somehow missed it -- e.g. AllocSignal() failed at startup) make
         * sure nothing is left dangling: every held original request must
         * be replied before this task exits and app->netReplyPort is torn
         * down on the GUI side (see the shutdown-drain comment in
         * friendsh3ep.c), or a late reply would land on a freed port. */
        if (!running || stopping)
            FS3ENet_AbortAllActiveDownloads();
        else if (g_FS3EActiveDownloads)
            FS3ENet_StepActiveDownloads();
    }

    FS3ECache_Cleanup();
    FS3EHttp_Cleanup();
    DeleteMsgPort(requestPort);
    if (g_FS3ENetStopSigBit >= 0) { FreeSignal(g_FS3ENetStopSigBit); g_FS3ENetStopSigBit = -1; }
    g_FS3ENetTask = NULL;

    if (shutdownMsg)
        ReplyMsg((struct Message *)shutdownMsg);
}

/* FS3ENETQ_LOGIN_START - register the app and build the authorize URL. */
static void FS3ENet_HandleLoginStart(FS3ENetMessage *fs3em)
{
    FS3ENetLoginStartReq   *req = (FS3ENetLoginStartReq *)fs3em->fs3em_Data;
    FS3ENetLoginStartReply *reply;
    char clientId[512];
    char clientSecret[512];
    char authorizeUrl[768];
    ULONG total;
    char *p;

    if (!req || fs3em->fs3em_DataLen < sizeof(*req))
    {
        fs3em->fs3em_Result = FS3ENETR_PARSE_ERROR;
        return;
    }


    if (!FS3EMastodon_CreateApp(req->fs3enl_ApiBaseUrl, FS3ENET_CLIENT_NAME,
            clientId, sizeof(clientId),
            clientSecret, sizeof(clientSecret)))
    {
        fs3em->fs3em_Result = FS3ENETR_HTTP_ERROR;
        return;
    }


    FS3EMastodon_BuildAuthorizeURL(req->fs3enl_ApiBaseUrl, clientId,
        authorizeUrl, sizeof(authorizeUrl));

    total = sizeof(FS3ENetLoginStartReply)
          + FS3ENet_PackLen(clientId)
          + FS3ENet_PackLen(clientSecret)
          + FS3ENet_PackLen(authorizeUrl);

    reply = (FS3ENetLoginStartReply *)AllocVec(total, MEMF_ANY | MEMF_PUBLIC);
    if (!reply)
    {
        fs3em->fs3em_Result = FS3ENETR_NETWORK_ERROR;
        return;
    }

    p = (char *)reply + sizeof(*reply);
    FS3ENet_PackStr(&reply->fs3enl_ClientId,     &p, clientId);
    FS3ENet_PackStr(&reply->fs3enl_ClientSecret, &p, clientSecret);
    FS3ENet_PackStr(&reply->fs3enl_AuthorizeUrl, &p, authorizeUrl);

    FreeVec(fs3em->fs3em_Data);
    fs3em->fs3em_Data    = reply;
    fs3em->fs3em_DataLen = total;
    fs3em->fs3em_Result  = FS3ENETR_OK;
}

/* FS3ENETQ_LOGIN_FINISH - exchange the OOB code for an access token and
 * verify it. */
static void FS3ENet_HandleLoginFinish(FS3ENetMessage *fs3em)
{
    FS3ENetLoginFinishReq   *req = (FS3ENetLoginFinishReq *)fs3em->fs3em_Data;
    FS3ENetLoginFinishReply *reply;
    FS3EMastodonAccount      tmpAcc = {0};
    char accessToken[512];
    ULONG total;
    char *p;

    if (!req || fs3em->fs3em_DataLen < sizeof(*req))
    {
        fs3em->fs3em_Result = FS3ENETR_PARSE_ERROR;
        return;
    }


    if (!FS3EMastodon_ExchangeCode(req->fs3enl_ApiBaseUrl, req->fs3enl_ClientId,
            req->fs3enl_ClientSecret, req->fs3enl_Code,
            accessToken, sizeof(accessToken)))
    {
        fs3em->fs3em_Result = FS3ENETR_AUTH_ERROR;
        return;
    }


    if (!FS3EMastodon_VerifyCredentials(req->fs3enl_ApiBaseUrl, accessToken, &tmpAcc))
    {
        FS3EMastodonAccount_Free(&tmpAcc);
        fs3em->fs3em_Result = FS3ENETR_AUTH_ERROR;
        return;
    }

    total = sizeof(FS3ENetLoginFinishReply)
          + FS3ENet_PackLen(accessToken)
          + FS3ENet_PackLen(tmpAcc.fma_Id)
          + FS3ENet_PackLen(tmpAcc.fma_Username)
          + FS3ENet_PackLen(tmpAcc.fma_Acct)
          + FS3ENet_PackLen(tmpAcc.fma_DisplayName)
          + FS3ENet_PackLen(tmpAcc.fma_AvatarURL);

    reply = (FS3ENetLoginFinishReply *)AllocVec(total, MEMF_ANY | MEMF_PUBLIC);
    if (!reply)
    {
        FS3EMastodonAccount_Free(&tmpAcc);
        fs3em->fs3em_Result = FS3ENETR_NETWORK_ERROR;
        return;
    }

    p = (char *)reply + sizeof(*reply);
    FS3ENet_PackStr(&reply->fs3enl_AccessToken,             &p, accessToken);
    FS3ENet_PackStr(&reply->fs3enl_Account.fma_Id,          &p, tmpAcc.fma_Id);
    FS3ENet_PackStr(&reply->fs3enl_Account.fma_Username,    &p, tmpAcc.fma_Username);
    FS3ENet_PackStr(&reply->fs3enl_Account.fma_Acct,        &p, tmpAcc.fma_Acct);
    FS3ENet_PackStrClean(&reply->fs3enl_Account.fma_DisplayName, &p, tmpAcc.fma_DisplayName);
    FS3ENet_PackStr(&reply->fs3enl_Account.fma_AvatarURL,   &p, tmpAcc.fma_AvatarURL);

    FS3EMastodonAccount_Free(&tmpAcc);

    FreeVec(fs3em->fs3em_Data);
    fs3em->fs3em_Data    = reply;
    fs3em->fs3em_DataLen = total;
    fs3em->fs3em_Result  = FS3ENETR_OK;
}

/* FS3ENETQ_VERIFY_ACCOUNT — same verify_credentials call LOGIN_FINISH ends
 * with, but starting from an access token the GUI already has instead of
 * an OAuth code exchange. */
static void FS3ENet_HandleVerifyAccount(FS3ENetMessage *fs3em)
{
    FS3ENetVerifyAccountReq   *req = (FS3ENetVerifyAccountReq *)fs3em->fs3em_Data;
    FS3ENetVerifyAccountReply *reply;
    FS3EMastodonAccount        tmpAcc = {0};
    ULONG total;
    char *p;

    if (!req || fs3em->fs3em_DataLen < sizeof(*req))
    {
        fs3em->fs3em_Result = FS3ENETR_PARSE_ERROR;
        return;
    }


    if (!FS3EMastodon_VerifyCredentials(req->fs3eva_ApiBaseUrl, req->fs3eva_AccessToken, &tmpAcc))
    {
        FS3EMastodonAccount_Free(&tmpAcc);
        fs3em->fs3em_Result = FS3ENETR_AUTH_ERROR;
        return;
    }

    total = sizeof(FS3ENetVerifyAccountReply)
          + FS3ENet_PackLen(tmpAcc.fma_Id)
          + FS3ENet_PackLen(tmpAcc.fma_Username)
          + FS3ENet_PackLen(tmpAcc.fma_Acct)
          + FS3ENet_PackLen(tmpAcc.fma_DisplayName)
          + FS3ENet_PackLen(tmpAcc.fma_AvatarURL);

    reply = (FS3ENetVerifyAccountReply *)AllocVec(total, MEMF_ANY | MEMF_PUBLIC);
    if (!reply)
    {
        FS3EMastodonAccount_Free(&tmpAcc);
        fs3em->fs3em_Result = FS3ENETR_NETWORK_ERROR;
        return;
    }

    p = (char *)reply + sizeof(*reply);
    FS3ENet_PackStr(&reply->fs3eva_Account.fma_Id,          &p, tmpAcc.fma_Id);
    FS3ENet_PackStr(&reply->fs3eva_Account.fma_Username,    &p, tmpAcc.fma_Username);
    FS3ENet_PackStr(&reply->fs3eva_Account.fma_Acct,        &p, tmpAcc.fma_Acct);
    FS3ENet_PackStrClean(&reply->fs3eva_Account.fma_DisplayName, &p, tmpAcc.fma_DisplayName);
    FS3ENet_PackStr(&reply->fs3eva_Account.fma_AvatarURL,   &p, tmpAcc.fma_AvatarURL);

    FS3EMastodonAccount_Free(&tmpAcc);

    FreeVec(fs3em->fs3em_Data);
    fs3em->fs3em_Data    = reply;
    fs3em->fs3em_DataLen = total;
    fs3em->fs3em_Result  = FS3ENETR_OK;
}

/* FS3ENETQ_INSTANCE_INFO — the server's per-toot character limit. */
static void FS3ENet_HandleInstanceInfo(FS3ENetMessage *fs3em)
{
    FS3ENetInstanceInfoReq   *req = (FS3ENetInstanceInfoReq *)fs3em->fs3em_Data;
    FS3ENetInstanceInfoReply *reply;
    ULONG maxChars;
    BOOL  known;

    if (!req || fs3em->fs3em_DataLen < sizeof(*req))
    {
        fs3em->fs3em_Result = FS3ENETR_PARSE_ERROR;
        return;
    }


    /* Always fills maxChars with *some* usable value (falls back to
     * FS3EMASTODON_DEFAULT_MAX_CHARS) so a network hiccup here never
     * surfaces as an error the GUI has to handle specially -- but the
     * return value says whether that's a real, server-confirmed limit or
     * just the fallback guess, and the reply carries that distinction
     * through as fs3eii_Known so the GUI doesn't present a guess as fact. */
    known = FS3EMastodon_GetInstanceInfo(req->fs3eii_ApiBaseUrl, &maxChars);

    reply = (FS3ENetInstanceInfoReply *)AllocVec(sizeof(FS3ENetInstanceInfoReply),
                                                  MEMF_ANY | MEMF_PUBLIC);
    if (!reply)
    {
        fs3em->fs3em_Result = FS3ENETR_NETWORK_ERROR;
        return;
    }
    reply->fs3eii_MaxChars = maxChars;
    reply->fs3eii_Known    = known;

    FreeVec(fs3em->fs3em_Data);
    fs3em->fs3em_Data    = reply;
    fs3em->fs3em_DataLen = sizeof(*reply);
    fs3em->fs3em_Result  = FS3ENETR_OK;
}

/* ---- Chunked media downloads --------------------------------------------
 *
 * FS3ENETQ_FETCH_IMAGE on a cache miss used to block this whole process on
 * one unbounded FS3EHttp_Get() until the entire file arrived -- fine for a
 * few-KB avatar, but a multi-hundred-KB/multi-MB attachment then stalled
 * every other queued request (timeline reloads, other image fetches, login,
 * posting) for as long as it took, with no safe way to time out (see
 * FS3EHTTP_TIMEOUT_SECS's comment in fs3enet_http.c).
 *
 * Instead, a real cache miss is registered here as an FS3ENetActiveDownload
 * and fetched a small Range chunk at a time (FS3EHttp_GetRange(),
 * FS3ENET_CHUNK_SIZE bytes per call) -- each chunk is its own short, safely
 * bounded HTTP exchange, and FS3ENet_ProcEntry's main loop advances exactly
 * one chunk of one download between drains of the request port, round-robin
 * across up to FS3ENET_MAX_ACTIVE_DOWNLOADS concurrent downloads (see
 * FS3ENet_StepActiveDownloads()). The original FS3ENetMessage is held --
 * NOT replied -- until the file is fully written or the download gives up;
 * from the GUI's point of view fs3em_Data/fs3em_Result still arrive exactly
 * once, same shape as before chunking existed.
 *
 * Bytes land directly on disk as each chunk arrives (Write() to a file
 * handle opened once at registration) rather than being buffered whole in
 * RAM first -- FS3ECache_Store()/StoreRAM() (whole-blob writes) are bypassed
 * for this path; FS3ECache_EnforceLimit() is called by hand on a successful
 * persistent-cache finish, mirroring what Store() would have done.
 *
 * Not every server honors Range: if the very first chunk request for a URL
 * comes back 200 instead of 206, that response IS the whole file (server
 * ignored Range and sent everything) -- the download just finishes
 * immediately with whatever was received, same as the old single-shot
 * behavior for that one resource.
 *
 * Verified against static.piaille.fr (bdbprintf_now trace, superlog.txt) --
 * server does honor Range (206 throughout) and does split across multiple
 * FS3ENET_CHUNK_SIZE requests once a file exceeds it, e.g.:
 *   StepDL: .../2657ea0aa2067d98.png offset=0      status=206 totalLen=247089 gotLen=196608
 *   StepDL: .../eb781c6399221cbc.png offset=0      status=206 totalLen=345774 gotLen=196608
 *   StepDL: .../2657ea0aa2067d98.png offset=196608 status=206 totalLen=247089 gotLen=50481   (-> Finish, 247089 total)
 *   StepDL: .../eb781c6399221cbc.png offset=196608 status=206 totalLen=345774 gotLen=149166  (-> Finish, 345774 total)
 * Note the interleaving in that trace: the second file's chunk 1 lands
 * between the first file's chunk 1 and chunk 2 -- direct confirmation of
 * the round-robin, one-chunk-per-download-per-turn behavior described
 * above, not just that chunking itself works. The very first fix attempt
 * (BIO_do_connect_retry() for a bounded connect, see FS3EHttp_DoRawRequest's
 * comment in fs3enet_http.c) made every single chunk fail outright (ok=0,
 * status=0 on every attempt, for every URL) -- caught by this same tracing
 * before it shipped.
 */
#define FS3ENET_CHUNK_SIZE            (192UL * 1024UL)  /* per Range request */
#define FS3ENET_MAX_ACTIVE_DOWNLOADS  4
#define FS3ENET_DOWNLOAD_MAX_RETRIES  3
#define FS3ENET_PROGRESS_MIN_DELTA    (64UL * 1024UL)    /* throttle FETCH_PROGRESS pings */

typedef struct FS3ENetActiveDownload
{
    struct FS3ENetActiveDownload *fs3ead_Next;
    FS3ENetMessage       *fs3ead_OrigMsg;  /* held -- ReplyMsg()'d only by FS3ENet_FinishDownload */
    FS3ENetFetchImageReq *fs3ead_Req;      /* == fs3ead_OrigMsg->fs3em_Data throughout */
    BPTR                  fs3ead_FH;       /* opened once at registration, Write() per chunk */
    char                  fs3ead_LocalPath[FS3ECACHE_PATH_SIZE];
    char                  fs3ead_CachePath[FS3ECACHE_PATH_SIZE];
    BOOL                  fs3ead_IsTemp;
    ULONG                 fs3ead_BytesSoFar;
    ULONG                 fs3ead_TotalBytes;      /* 0 = unknown until first chunk (or never) */
    ULONG                 fs3ead_LastProgressSent; /* fs3ead_BytesSoFar as of the last ping */
    UWORD                 fs3ead_RetryCount;
} FS3ENetActiveDownload;

/* Builds an FS3ENetFetchImageReply for fs3em from localPath/cachePath/isTemp,
 * freeing the original request block first and replacing fs3em_Data --
 * same convention every other Handle* function in this file already
 * follows. Shared by FS3ENet_HandleFetchImage()'s synchronous cache-hit
 * path below and by FS3ENet_FinishDownload() once a chunked download
 * completes. */
static void FS3ENet_BuildFetchImageReply(FS3ENetMessage *fs3em, FS3ENetFetchImageReq *req,
                                          const char *localPath, const char *cachePath,
                                          BOOL isTemp)
{
    FS3ENetFetchImageReply *reply;
    ULONG total;
    char *p;

    total = sizeof(FS3ENetFetchImageReply)
          + FS3ENet_PackLen(localPath)
          + FS3ENet_PackLen(req->fs3enf_Key)
          + FS3ENet_PackLen(req->fs3enf_Subdir)
          + FS3ENet_PackLen(cachePath);
    reply = (FS3ENetFetchImageReply *)AllocVec(total, MEMF_ANY | MEMF_PUBLIC);
    if (!reply)
    {
        fs3em->fs3em_Result = FS3ENETR_NETWORK_ERROR;
        return;
    }

    reply->fs3enf_IsTemp = isTemp;

    p = (char *)reply + sizeof(*reply);
    FS3ENet_PackStr(&reply->fs3enf_LocalPath, &p, localPath);
    FS3ENet_PackStr(&reply->fs3enf_Key,       &p, req->fs3enf_Key    ? req->fs3enf_Key    : "");
    FS3ENet_PackStr(&reply->fs3enf_Subdir,    &p, req->fs3enf_Subdir ? req->fs3enf_Subdir : "");
    FS3ENet_PackStr(&reply->fs3enf_CachePath, &p, cachePath);

    FreeVec(fs3em->fs3em_Data);
    fs3em->fs3em_Data    = reply;
    fs3em->fs3em_DataLen = total;
    fs3em->fs3em_Result  = FS3ENETR_OK;
}

/* Removes dl from g_FS3EActiveDownloads (wherever it is in the list) and
 * decrements the count. Doesn't free dl or touch its file handle -- callers
 * (FS3ENet_FinishDownload, FS3ENet_StepActiveDownloads) do that themselves. */
static void FS3ENet_UnlinkActiveDownload(FS3ENetActiveDownload *dl)
{
    FS3ENetActiveDownload **link = &g_FS3EActiveDownloads;

    while (*link)
    {
        if (*link == dl)
        {
            *link = dl->fs3ead_Next;
            if (g_FS3EActiveDownloadCount > 0) g_FS3EActiveDownloadCount--;
            return;
        }
        link = &(*link)->fs3ead_Next;
    }
}

/* Sends one FS3ENETQ_FETCH_PROGRESS ping for dl to the port the original
 * request came in on -- one-way, no reply expected; the GUI frees it like
 * any other FS3ENetMessage off that port (see FS3ENetFetchProgress's doc
 * comment in fs3enet.h). Best-effort: silently does nothing on an
 * allocation failure, since a missed progress update isn't worth failing
 * the download over. */
static void FS3ENet_SendProgress(FS3ENetActiveDownload *dl)
{
    FS3ENetMessage       *msg;
    FS3ENetFetchProgress *prog;
    ULONG                 total;
    char                 *p;
    const char           *key = dl->fs3ead_Req->fs3enf_Key;

    msg = (FS3ENetMessage *)AllocVec(sizeof(*msg), MEMF_ANY | MEMF_PUBLIC | MEMF_CLEAR);
    if (!msg) return;

    total = sizeof(FS3ENetFetchProgress) + FS3ENet_PackLen(key);
    prog  = (FS3ENetFetchProgress *)AllocVec(total, MEMF_ANY | MEMF_PUBLIC);
    if (!prog) { FreeVec(msg); return; }

    p = (char *)prog + sizeof(*prog);
    FS3ENet_PackStr(&prog->fs3efp_Key, &p, key ? key : "");
    prog->fs3efp_BytesSoFar = dl->fs3ead_BytesSoFar;
    prog->fs3efp_TotalBytes = dl->fs3ead_TotalBytes;

    msg->fs3em_Msg.mn_Length = sizeof(*msg);
    msg->fs3em_Type          = FS3ENETQ_FETCH_PROGRESS;
    msg->fs3em_Result        = FS3ENETR_OK;
    msg->fs3em_Data          = prog;
    msg->fs3em_DataLen       = total;

    PutMsg(dl->fs3ead_OrigMsg->fs3em_Msg.mn_ReplyPort, (struct Message *)msg);

    dl->fs3ead_LastProgressSent = dl->fs3ead_BytesSoFar;
}

/* Completes dl -- successfully (result == FS3ENETR_OK) or not -- closing its
 * file handle, replying its held original request, and freeing dl. A failed
 * download's partial file is deleted, the same "never leave a truncated
 * file behind" contract FS3ECache_Store()/StoreRAM() already have. */
static void FS3ENet_FinishDownload(FS3ENetActiveDownload *dl, ULONG result)
{
    FS3ENetMessage *fs3em = dl->fs3ead_OrigMsg;
#ifdef BDBTRACEMULTIPART
    bdbprintf_now("FinishDL: url=%s result=%lu bytesSoFar=%lu path=%s\n",
                   dl->fs3ead_Req->fs3enf_Url, (unsigned long)result,
                   (unsigned long)dl->fs3ead_BytesSoFar, dl->fs3ead_LocalPath);
#endif
    if (dl->fs3ead_FH) Close(dl->fs3ead_FH);

    if (result == FS3ENETR_OK)
    {
        FS3ENet_BuildFetchImageReply(fs3em, dl->fs3ead_Req,
                                      dl->fs3ead_LocalPath, dl->fs3ead_CachePath,
                                      dl->fs3ead_IsTemp);

        /* Keep the persistent cache under budget -- Store() would have done
         * this itself; writing incrementally here bypasses Store() entirely,
         * so it has to be triggered by hand. Not needed for the RAM:T path,
         * which isn't size-limited. */
        if (!dl->fs3ead_IsTemp)
            FS3ECache_EnforceLimit();
    }
    else
    {
        DeleteFile(dl->fs3ead_LocalPath);
        fs3em->fs3em_Result = result;
        /* fs3em_Data is left pointing at the original request block, same
         * "on error, the GUI must FreeVec it" contract every other request
         * type already documents (see FS3ENetFetchImageReq's doc comment in
         * fs3enet.h). */
    }

    FS3ENet_UnlinkActiveDownload(dl);
    ReplyMsg((struct Message *)fs3em);
    FreeVec(dl);
}

/* Abandons every active download immediately, replying each held original
 * request with FS3ENETR_NETWORK_ERROR. Used when shutdown is detected (see
 * FS3ENet_ProcEntry's loop) so nothing is left dangling: every message this
 * process is holding gets replied before the task exits and the GUI tears
 * down its reply port. */
static void FS3ENet_AbortAllActiveDownloads(void)
{
    while (g_FS3EActiveDownloads)
        FS3ENet_FinishDownload(g_FS3EActiveDownloads, FS3ENETR_NETWORK_ERROR);
}

/* Advances the download at the front of g_FS3EActiveDownloads by exactly one
 * chunk, then either finishes it (file complete, or gave up after too many
 * failed chunks -- see FS3ENet_FinishDownload) or rotates it to the back of
 * the list (round robin) so the next call services a different download
 * first. Called once per FS3ENet_ProcEntry loop iteration whenever the list
 * is non-empty -- see that loop's own comment for why this keeps a large
 * download from starving other queued requests. */
static void FS3ENet_StepActiveDownloads(void)
{
    FS3ENetActiveDownload *dl = g_FS3EActiveDownloads;
    FS3EHttpResponse       resp;
    ULONG                  status = 0, totalLen = 0, gotLen = 0;
    BOOL                   ok, finished = FALSE;
    ULONG                  finishResult = FS3ENETR_OK;

    if (!dl) return;

    ok = FS3EHttp_GetRange(dl->fs3ead_Req->fs3enf_Url, NULL,
                           dl->fs3ead_BytesSoFar, FS3ENET_CHUNK_SIZE,
                           &status, &totalLen, &resp);
#ifdef BDBTRACEMULTIPART
    bdbprintf_now("StepDL: url=%s offset=%lu ok=%ld status=%lu totalLen=%lu gotLen=%lu retry=%u\n",
                   dl->fs3ead_Req->fs3enf_Url, (unsigned long)dl->fs3ead_BytesSoFar,
                   (long)ok, (unsigned long)status, (unsigned long)totalLen,
                   (unsigned long)(ok ? resp.fhr_BodyLen : 0), (unsigned)dl->fs3ead_RetryCount);
#endif
    if (!ok)
    {
        dl->fs3ead_RetryCount++;
        if (dl->fs3ead_RetryCount > FS3ENET_DOWNLOAD_MAX_RETRIES)
        {
#ifdef BDBTRACEMULTIPART
            bdbprintf_now("StepDL: giving up on %s after %u failed chunks\n",
                           dl->fs3ead_Req->fs3enf_Url, (unsigned)dl->fs3ead_RetryCount);
#endif
            finished      = TRUE;
            finishResult  = FS3ENETR_HTTP_ERROR;
        }
    }
    else
    {
        dl->fs3ead_RetryCount = 0;
        gotLen = resp.fhr_BodyLen;

        if (gotLen > 0 &&
            Write(dl->fs3ead_FH, resp.fhr_Body, (LONG)gotLen) != (LONG)gotLen)
        {
        #ifdef BDBTRACEMULTIPART
            bdbprintf_now("StepDL: Write() failed for %s, IoErr=%ld\n",
                           dl->fs3ead_Req->fs3enf_Url, (long)IoErr());
        #endif
            finished     = TRUE;
            finishResult = FS3ENETR_HTTP_ERROR;
        }

        FS3EHttp_FreeResponse(&resp);

        if (!finished)
        {
            dl->fs3ead_BytesSoFar += gotLen;
            if (status == 206 && totalLen > 0)
                dl->fs3ead_TotalBytes = totalLen;

            /* Done when: the server ignored Range entirely (200 -- that
             * response WAS the whole file, regardless of what we asked
             * for), a 206 chunk that reached/exceeded the now-known total,
             * or a chunk shorter than what was requested (server-side EOF,
             * whether or not Content-Range gave us a total to compare
             * against). */
            if (status == 200 ||
                (dl->fs3ead_TotalBytes > 0 && dl->fs3ead_BytesSoFar >= dl->fs3ead_TotalBytes) ||
                gotLen < FS3ENET_CHUNK_SIZE)
            {
                finished     = TRUE;
                finishResult = FS3ENETR_OK;
            }
            else if (dl->fs3ead_Req->fs3enf_WantProgress &&
                     dl->fs3ead_BytesSoFar - dl->fs3ead_LastProgressSent >= FS3ENET_PROGRESS_MIN_DELTA)
            {
                FS3ENet_SendProgress(dl);
            }
        }
    }

    if (finished)
    {
        FS3ENet_FinishDownload(dl, finishResult);
        return;
    }

    /* Still in progress (or retrying after a failed chunk) -- rotate to the
     * back so other active downloads, and the request port, get a turn
     * before this one is serviced again. */
    FS3ENet_UnlinkActiveDownload(dl);
    dl->fs3ead_Next = NULL;
    if (!g_FS3EActiveDownloads)
    {
        g_FS3EActiveDownloads = dl;
    }
    else
    {
        FS3ENetActiveDownload *tail = g_FS3EActiveDownloads;
        while (tail->fs3ead_Next) tail = tail->fs3ead_Next;
        tail->fs3ead_Next = dl;
    }
    g_FS3EActiveDownloadCount++;
}

/* FS3ENETQ_FETCH_IMAGE — check disk cache, fetch on miss, reply with path.
 *
 * Repeat requests for the same URL (e.g. a user double/triple-clicking the
 * same thumbnail, or a click landing on media the passive timeline-render
 * fetch already resolved earlier) are handled by the cache-lookup logic
 * below (FS3ECache_Lookup for the persistent case, FS3ECache_LookupRAM for
 * the !KeepOriginal/RAM:T case) finding the file and skipping the
 * download/write -- every repeat request still gets its own normal
 * FS3ENETR_OK reply with a real, usable path. (An earlier version of this
 * function short-circuited same-URL-as-the-previous-request here with a
 * bounce result and no data, on the theory that "the previous request will
 * deliver it" -- wrong whenever that previous request was a *different*,
 * already-finished fetch, e.g. the passive per-post thumbnail fetch: there
 * was no second in-flight reply coming, and callers like fs3emediaview.c
 * that were told "someone else has this" waited forever. Removed --
 * this is what the cache lookups below are already for.)
 *
 * Returns TRUE if fs3em is resolved and ready for the caller to ReplyMsg()
 * (cache hit, RAM:T reuse, or an error), FALSE if a real cache miss was just
 * handed off to the chunked download engine -- see "Chunked media
 * downloads" above. */
static BOOL FS3ENet_HandleFetchImage(FS3ENetMessage *fs3em)
{
    FS3ENetFetchImageReq *req = (FS3ENetFetchImageReq *)fs3em->fs3em_Data;
    char localPath[FS3ECACHE_PATH_SIZE];
    char cachePath[FS3ECACHE_PATH_SIZE];
    BOOL isTemp = FALSE;

    if (!req || fs3em->fs3em_DataLen < sizeof(*req))
    {
        fs3em->fs3em_Result = FS3ENETR_PARSE_ERROR;
        return TRUE;
    }
#ifdef BDBTRACEMULTIPART
    bdbprintf_now("FetchImage: url=%s subdir=%s keep=%ld\n",
                   req->fs3enf_Url, req->fs3enf_Subdir, (long)req->fs3enf_KeepOriginal);
#endif
    /* Deterministic path this URL would live at if kept -- computed
     * regardless of fs3enf_KeepOriginal so the caller always has a stable
     * name to derive the resized thumbnail's sibling filename from, even
     * on a run where the original itself only ever touches RAM:T. Ensure
     * the subdir exists too: when KeepOriginal is FALSE, the chunked
     * download engine's own file Open() (the thing that normally creates
     * it) never runs against the persistent cache, but the thumbnail
     * process still needs to write the resized sibling under this same
     * subdir a moment from now. */
    FS3ECache_ComputePath(req->fs3enf_Url, req->fs3enf_Subdir, cachePath, sizeof(cachePath));
    FS3ECache_EnsureSubdir(req->fs3enf_Subdir);

    if (!FS3ECache_Lookup(req->fs3enf_Url, req->fs3enf_Subdir, localPath, sizeof(localPath)))
    {
        /* Not in the persistent cache -- but for a !KeepOriginal request,
         * the deterministic RAM:T path a fresh download would write to may
         * already hold an earlier download of this exact URL (e.g. still
         * being read by the thumbnail process, or just never cleaned up).
         * Reuse it instead of re-downloading and re-opening with
         * MODE_NEWFILE, which would silently truncate/replace a file
         * another task might still have open for reading -- see
         * FS3ECache_LookupRAM()'s doc comment for why that specific race
         * is suspected to crash real UAE (not real hardware) setups. */
        BOOL haveExisting = (!req->fs3enf_KeepOriginal) &&
                             FS3ECache_LookupRAM(req->fs3enf_Url, localPath, sizeof(localPath));

        if (haveExisting)
        {
            isTemp = TRUE;
        }
        else
        {
            /* Real cache miss -- hand off to the chunked download engine
             * instead of blocking this whole process on one unbounded
             * fetch. localPath is already the right target either way
             * (persistent cache path from FS3ECache_Lookup above, or the
             * deterministic RAM:T path FS3ECache_LookupRAM just filled in). */
            FS3ENetActiveDownload *dl;
            BPTR fh;

            if (g_FS3EActiveDownloadCount >= FS3ENET_MAX_ACTIVE_DOWNLOADS)
            {
                /* At the concurrency cap -- bounce rather than queue. The
                 * caller re-fetches naturally: avatars/thumbnails are
                 * re-requested on every timeline render, and the media
                 * viewer's on-demand fetch is a single explicit user
                 * action -- hitting this cap in practice with 4 concurrent
                 * slots is expected to be rare. */
#ifdef BDBTRACEMULTIPART
                bdbprintf_now("FetchImage: at concurrency cap (%lu), bouncing %s\n",
                               (unsigned long)g_FS3EActiveDownloadCount, req->fs3enf_Url);
#endif
                fs3em->fs3em_Result = FS3ENETR_NETWORK_ERROR;
                return TRUE;
            }

            if (!req->fs3enf_KeepOriginal && !FS3ECache_EnsureRAMTempDir())
            {
#ifdef BDBTRACEMULTIPART
                bdbprintf_now("FetchImage: FS3ECache_EnsureRAMTempDir failed for %s\n", req->fs3enf_Url);
#endif
                fs3em->fs3em_Result = FS3ENETR_HTTP_ERROR;
                return TRUE;
            }

            fh = Open(localPath, MODE_NEWFILE);
            if (!fh)
            {
#ifdef BDBTRACEMULTIPART
                bdbprintf_now("FetchImage: Open(%s, MODE_NEWFILE) failed, IoErr=%ld\n",
                               localPath, (long)IoErr());
#endif
                fs3em->fs3em_Result = FS3ENETR_HTTP_ERROR;
                return TRUE;
            }

            dl = (FS3ENetActiveDownload *)AllocVec(sizeof(*dl), MEMF_ANY | MEMF_CLEAR);
            if (!dl)
            {
                Close(fh);
                DeleteFile(localPath);
                fs3em->fs3em_Result = FS3ENETR_NETWORK_ERROR;
                return TRUE;
            }

            dl->fs3ead_OrigMsg = fs3em;
            dl->fs3ead_Req     = req;
            dl->fs3ead_FH      = fh;
            strncpy(dl->fs3ead_LocalPath, localPath, sizeof(dl->fs3ead_LocalPath) - 1);
            strncpy(dl->fs3ead_CachePath, cachePath, sizeof(dl->fs3ead_CachePath) - 1);
            dl->fs3ead_IsTemp  = !req->fs3enf_KeepOriginal;

            dl->fs3ead_Next        = g_FS3EActiveDownloads;
            g_FS3EActiveDownloads  = dl;
            g_FS3EActiveDownloadCount++;
#ifdef BDBTRACEMULTIPART
            bdbprintf_now("FetchImage: registered chunked dl for %s -> %s (count=%lu)\n",
                           req->fs3enf_Url, localPath, (unsigned long)g_FS3EActiveDownloadCount);
#endif
            return FALSE; /* deferred -- FS3ENet_FinishDownload() replies later */
        }
    }
    /* else: found on disk already -- either the persistent cache, or (see
     * haveExisting above) an already-downloaded RAM:T temp file this
     * request is now sharing rather than re-fetching. */

    FS3ENet_BuildFetchImageReply(fs3em, req, localPath, cachePath, isTemp);
    return TRUE;
}

/* Encodes Unicode code point cp as UTF-8 into out (up to 4 bytes), bounded
 * by outSize. Returns the number of bytes written (0 if it doesn't fit).
 * FriendSh3ep renders text through utf8rastport.library throughout, so
 * decoded numeric HTML entities (e.g. &#8217; for a curly quote) must come
 * out as proper UTF-8, not a truncated single byte. */
static ULONG EncodeUTF8(ULONG cp, char *out, ULONG outSize)
{
    if (cp < 0x80) {
        if (outSize < 1) return 0;
        out[0] = (char)cp;
        return 1;
    } else if (cp < 0x800) {
        if (outSize < 2) return 0;
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    } else if (cp < 0x10000) {
        if (outSize < 3) return 0;
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    } else {
        if (outSize < 4) return 0;
        out[0] = (char)(0xF0 | (cp >> 18));
        out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }
}

/* Strip HTML tags from Mastodon status content.
 * <p> and <br> become newlines; all other tags are removed.
 * HTML entities &amp; &lt; &gt; &nbsp; &apos; &quot; are decoded, as are
 * numeric references &#39; (decimal) and &#x27; (hex), UTF-8 encoded via
 * EncodeUTF8 above.
 * Leading newlines are suppressed. */
static void StripHTML(const char *html, char *out, ULONG outSize)
{
    ULONG oi = 0;
    const char *s = html;
    int in_tag = 0;
    int first = 1;

    if (!html || !out || outSize == 0) { if (out && outSize) out[0] = '\0'; return; }

    while (*s && oi + 1 < outSize) {
        if (in_tag) {
            if (*s == '>') in_tag = 0;
            s++;
            continue;
        }
        if (*s == '<') {
            /* block elements become newlines */
            if ((s[1] == 'b' || s[1] == 'B') && (s[2] == 'r' || s[2] == 'R')) {
                if (!first) out[oi++] = '\n';
            } else if ((s[1] == 'p' || s[1] == 'P') &&
                       (s[2] == '>' || s[2] == ' ' || s[2] == '/')) {
                if (!first) out[oi++] = '\n';
            }
            in_tag = 1;
            s++;
            continue;
        }
        if (*s == '&') {
            if (strncmp(s, "&amp;",  5) == 0) { out[oi++] = '&';  s += 5; first = 0; continue; }
            if (strncmp(s, "&lt;",   4) == 0) { out[oi++] = '<';  s += 4; first = 0; continue; }
            if (strncmp(s, "&gt;",   4) == 0) { out[oi++] = '>';  s += 4; first = 0; continue; }
            if (strncmp(s, "&nbsp;", 6) == 0) { out[oi++] = ' ';  s += 6; first = 0; continue; }
            if (strncmp(s, "&apos;", 6) == 0) { out[oi++] = '\''; s += 6; first = 0; continue; }
            if (strncmp(s, "&quot;", 6) == 0) { out[oi++] = '"';  s += 6; first = 0; continue; }
            if (s[1] == '#') {
                const char *p2 = s + 2;
                int   hex = 0;
                int   any = 0;
                ULONG cp  = 0;

                if (*p2 == 'x' || *p2 == 'X') { hex = 1; p2++; }

                for (;;) {
                    char c = *p2;
                    int  d;
                    if (c >= '0' && c <= '9')                    d = c - '0';
                    else if (hex && c >= 'a' && c <= 'f')        d = c - 'a' + 10;
                    else if (hex && c >= 'A' && c <= 'F')        d = c - 'A' + 10;
                    else break;
                    cp = cp * (ULONG)(hex ? 16 : 10) + (ULONG)d;
                    p2++;
                    any = 1;
                }

                if (any && *p2 == ';') {
                    oi += EncodeUTF8(cp, out + oi, outSize - oi);
                    s = p2 + 1;
                    first = 0;
                    continue;
                }
                /* not a well-formed numeric entity -- fall through, copy '&' verbatim */
            }
        }
        out[oi++] = *s++;
        first = 0;
    }
    /* trim trailing newlines */
    while (oi > 0 && out[oi - 1] == '\n') oi--;
    out[oi] = '\0';
}

/* FS3ENETQ_TIMELINE — fetch statuses and pack them into a flat reply block. */
#define MAX_STATUSES_TIMELINE 40

/* Extracts every FS3ENetStatus field EXCEPT fmas_BoostBy/fmas_BoostByAcct
 * (reblog-booster identity -- meaningless off a notification's embedded
 * status, which is never itself a reblog wrapper for this app's purposes;
 * callers needing that pair handle it themselves, see the "src != item"
 * blocks in FS3ENet_HandleTimeline). item/src are pre-resolved by the
 * caller: for a genuine reblog-unwrapped timeline entry they differ
 * (content/media/poll/counts live on src, id/created_at on item); pass the
 * same pointer for both when there's no such wrapper (a notification's
 * embedded status, or any plain non-reblog status). Sizing pass -- see
 * FS3ENet_FillStatusFields for the matching fill pass, kept as a
 * deliberately separate function (not a single size-or-fill-by-flag one)
 * so each stays a plain top-to-bottom read of the fields it's summing/
 * writing, same two-pass shape the rest of this file already uses. */
static ULONG FS3ENet_SizeStatusFields(const cJSON *item, const cJSON *src)
{
    ULONG total = sizeof(FS3ENetStatus);
    const cJSON *acct = cJSON_GetObjectItemCaseSensitive(src, "account");
    const cJSON *v;

    v = acct ? cJSON_GetObjectItemCaseSensitive(acct, "display_name") : NULL;
    total += (v && cJSON_IsString(v) && v->valuestring) ? strlen(v->valuestring) + 1 : 1;

    v = acct ? cJSON_GetObjectItemCaseSensitive(acct, "acct") : NULL;
    total += (v && cJSON_IsString(v) && v->valuestring) ? strlen(v->valuestring) + 1 : 1;

    /* reserve original HTML length for stripped content (stripped ≤ original) */
    v = cJSON_GetObjectItemCaseSensitive(src, "content");
    total += (v && cJSON_IsString(v) && v->valuestring) ? strlen(v->valuestring) + 1 : 1;

    v = cJSON_GetObjectItemCaseSensitive(item, "created_at");
    total += (v && cJSON_IsString(v) && v->valuestring) ? strlen(v->valuestring) + 1 : 1;

    v = acct ? cJSON_GetObjectItemCaseSensitive(acct, "avatar") : NULL;
    total += (v && cJSON_IsString(v) && v->valuestring) ? strlen(v->valuestring) + 1 : 1;

    v = cJSON_GetObjectItemCaseSensitive(item, "id");
    total += (v && cJSON_IsString(v) && v->valuestring) ? strlen(v->valuestring) + 1 : 1;

    /* media_attachments belongs to src (the reblogged status for boosts),
     * same as "content" above. */
    v = cJSON_GetObjectItemCaseSensitive(src, "media_attachments");
    {
        int mCount = (v && cJSON_IsArray(v)) ? cJSON_GetArraySize(v) : 0;
        int mi;
        if (mCount > FS3ENET_MAX_MEDIA) mCount = FS3ENET_MAX_MEDIA;
        for (mi = 0; mi < mCount; mi++) {
            const cJSON *att  = cJSON_GetArrayItem(v, mi);
            const cJSON *purl = att ? cJSON_GetObjectItemCaseSensitive(att, "preview_url") : NULL;
            const cJSON *aid  = att ? cJSON_GetObjectItemCaseSensitive(att, "id") : NULL;
            if (!purl || !cJSON_IsString(purl) || !purl->valuestring)
                purl = att ? cJSON_GetObjectItemCaseSensitive(att, "url") : NULL;
            total += (purl && cJSON_IsString(purl) && purl->valuestring)
                   ? strlen(purl->valuestring) + 1 : 1;
            total += (aid && cJSON_IsString(aid) && aid->valuestring)
                   ? strlen(aid->valuestring) + 1 : 1;
        }
    }

    /* Poll -- belongs to src same as media_attachments/content above;
     * mutually exclusive with media_attachments in practice (Mastodon
     * disallows both on one status), but sized independently either way. */
    v = cJSON_GetObjectItemCaseSensitive(src, "poll");
    if (v && !cJSON_IsNull(v)) {
        const cJSON *options = cJSON_GetObjectItemCaseSensitive(v, "options");
        int oCount = (options && cJSON_IsArray(options)) ? cJSON_GetArraySize(options) : 0;
        int oi;
        if (oCount > FS3ENET_MAX_POLL_OPTIONS) oCount = FS3ENET_MAX_POLL_OPTIONS;
        for (oi = 0; oi < oCount; oi++) {
            const cJSON *opt   = cJSON_GetArrayItem(options, oi);
            const cJSON *title = opt ? cJSON_GetObjectItemCaseSensitive(opt, "title") : NULL;
            total += (title && cJSON_IsString(title) && title->valuestring)
                   ? strlen(title->valuestring) + 1 : 1;
        }
    }

    /* Link preview card -- belongs to src same as poll/media_attachments
     * above (a reblog's card is the boosted status's own). NOT mutually
     * exclusive with either: independent of what the user attached, since
     * Mastodon derives it from a URL found in the text. */
    v = cJSON_GetObjectItemCaseSensitive(src, "card");
    if (v && !cJSON_IsNull(v)) {
        const cJSON *f;
        f = cJSON_GetObjectItemCaseSensitive(v, "url");
        total += (f && cJSON_IsString(f) && f->valuestring) ? strlen(f->valuestring) + 1 : 1;
        f = cJSON_GetObjectItemCaseSensitive(v, "title");
        total += (f && cJSON_IsString(f) && f->valuestring) ? strlen(f->valuestring) + 1 : 1;
        f = cJSON_GetObjectItemCaseSensitive(v, "description");
        total += (f && cJSON_IsString(f) && f->valuestring) ? strlen(f->valuestring) + 1 : 1;
        f = cJSON_GetObjectItemCaseSensitive(v, "provider_name");
        total += (f && cJSON_IsString(f) && f->valuestring) ? strlen(f->valuestring) + 1 : 1;
        f = cJSON_GetObjectItemCaseSensitive(v, "image");
        total += (f && cJSON_IsString(f) && f->valuestring) ? strlen(f->valuestring) + 1 : 1;
    } else {
        total += 5; /* five empty strings -- url/title/description/provider_name/image */
    }

    return total;
}

/* Fill pass matching FS3ENet_SizeStatusFields -- see its comment for the
 * item/src contract and what's deliberately excluded (the boostBy pair).
 * stripped/strippedSize is caller-owned scratch space for StripHTML (not
 * declared locally here so a caller processing many items in a loop, like
 * FS3ENet_HandleTimeline's pass 2, can reuse one buffer instead of paying
 * for it on every call). */
static void FS3ENet_FillStatusFields(const cJSON *item, const cJSON *src,
                                      FS3ENetStatus *dst, char **p,
                                      char *stripped, ULONG strippedSize)
{
    const cJSON *acct = cJSON_GetObjectItemCaseSensitive(src, "account");
    const cJSON *v;
    const char *str;

    v = acct ? cJSON_GetObjectItemCaseSensitive(acct, "display_name") : NULL;
    str = (v && cJSON_IsString(v)) ? v->valuestring : "";
    FS3ENet_PackStrClean(&dst->fmas_DisplayName, p, str);

    v = acct ? cJSON_GetObjectItemCaseSensitive(acct, "acct") : NULL;
    str = (v && cJSON_IsString(v)) ? v->valuestring : "";
    FS3ENet_PackStr(&dst->fmas_Acct, p, str);

    v = cJSON_GetObjectItemCaseSensitive(src, "content");
    str = (v && cJSON_IsString(v)) ? v->valuestring : "";
    StripHTML(str, stripped, strippedSize);
    FS3ENet_PackStr(&dst->fmas_Content, p, stripped);

    v = cJSON_GetObjectItemCaseSensitive(item, "created_at");
    str = (v && cJSON_IsString(v)) ? v->valuestring : "";
    FS3ENet_PackStr(&dst->fmas_CreatedAt, p, str);

    v = acct ? cJSON_GetObjectItemCaseSensitive(acct, "avatar") : NULL;
    str = (v && cJSON_IsString(v)) ? v->valuestring : "";
    FS3ENet_PackStr(&dst->fmas_AvatarURL, p, str);

    v = cJSON_GetObjectItemCaseSensitive(item, "id");
    str = (v && cJSON_IsString(v)) ? v->valuestring : "";
    FS3ENet_PackStr(&dst->fmas_Id, p, str);

    /* media_attachments -- see the matching block in FS3ENet_SizeStatusFields. */
    v = cJSON_GetObjectItemCaseSensitive(src, "media_attachments");
    {
        int mCount = (v && cJSON_IsArray(v)) ? cJSON_GetArraySize(v) : 0;
        int mi;
        if (mCount > FS3ENET_MAX_MEDIA) mCount = FS3ENET_MAX_MEDIA;
        for (mi = 0; mi < mCount; mi++) {
            const cJSON *att  = cJSON_GetArrayItem(v, mi);
            const cJSON *purl = att ? cJSON_GetObjectItemCaseSensitive(att, "preview_url") : NULL;
            const cJSON *aid  = att ? cJSON_GetObjectItemCaseSensitive(att, "id") : NULL;
            const cJSON *typeV;
            const char  *typeStr;
            if (!purl || !cJSON_IsString(purl) || !purl->valuestring)
                purl = att ? cJSON_GetObjectItemCaseSensitive(att, "url") : NULL;
            str = (purl && cJSON_IsString(purl)) ? purl->valuestring : "";
            FS3ENet_PackStr(&dst->fmas_MediaUrls[mi], p, str);

            /* Needed to resend as media_ids[] on a PUT edit so existing
             * attachments survive a text-only edit -- see
             * FS3ENetStatus.fmas_MediaIds. */
            str = (aid && cJSON_IsString(aid)) ? aid->valuestring : "";
            FS3ENet_PackStr(&dst->fmas_MediaIds[mi], p, str);

            /* "image"/"video"/"gifv"/"audio"/"unknown" -- lets the GUI
             * skip fetching a thumbnail for audio entirely instead of
             * routing its (fallback, no-preview) full file URL into the
             * image decoder. */
            typeV   = att ? cJSON_GetObjectItemCaseSensitive(att, "type") : NULL;
            typeStr = (typeV && cJSON_IsString(typeV)) ? typeV->valuestring : "";
            if      (strcmp(typeStr, "image") == 0) dst->fmas_MediaKind[mi] = FS3ENET_MEDIAKIND_IMAGE;
            else if (strcmp(typeStr, "video") == 0) dst->fmas_MediaKind[mi] = FS3ENET_MEDIAKIND_VIDEO;
            else if (strcmp(typeStr, "gifv")  == 0) dst->fmas_MediaKind[mi] = FS3ENET_MEDIAKIND_GIFV;
            else if (strcmp(typeStr, "audio") == 0) dst->fmas_MediaKind[mi] = FS3ENET_MEDIAKIND_AUDIO;
            else                                     dst->fmas_MediaKind[mi] = FS3ENET_MEDIAKIND_UNKNOWN;
        }
        for (; mi < FS3ENET_MAX_MEDIA; mi++) {
            dst->fmas_MediaUrls[mi] = NULL;
            dst->fmas_MediaIds[mi]  = NULL;
            dst->fmas_MediaKind[mi] = FS3ENET_MEDIAKIND_UNKNOWN;
        }
        dst->fmas_MediaCount = (ULONG)mCount;
    }

    /* Poll -- see the matching block in FS3ENet_SizeStatusFields. */
    v = cJSON_GetObjectItemCaseSensitive(src, "poll");
    if (v && !cJSON_IsNull(v)) {
        const cJSON *options = cJSON_GetObjectItemCaseSensitive(v, "options");
        int oCount = (options && cJSON_IsArray(options)) ? cJSON_GetArraySize(options) : 0;
        int oi;
        const cJSON *ev;
        if (oCount > FS3ENET_MAX_POLL_OPTIONS) oCount = FS3ENET_MAX_POLL_OPTIONS;
        for (oi = 0; oi < oCount; oi++) {
            const cJSON *opt   = cJSON_GetArrayItem(options, oi);
            const cJSON *title = opt ? cJSON_GetObjectItemCaseSensitive(opt, "title") : NULL;
            const cJSON *votes = opt ? cJSON_GetObjectItemCaseSensitive(opt, "votes_count") : NULL;
            str = (title && cJSON_IsString(title)) ? title->valuestring : "";
            FS3ENet_PackStr(&dst->fmas_PollOptionTitles[oi], p, str);
            dst->fmas_PollOptionVotes[oi] = (votes && cJSON_IsNumber(votes)) ? (ULONG)votes->valueint : 0;
        }
        for (; oi < FS3ENET_MAX_POLL_OPTIONS; oi++)
            dst->fmas_PollOptionTitles[oi] = NULL;
        dst->fmas_PollOptionCount = (ULONG)oCount;

        ev = cJSON_GetObjectItemCaseSensitive(v, "votes_count");
        dst->fmas_PollVotesCount = (ev && cJSON_IsNumber(ev)) ? (ULONG)ev->valueint : 0;
        ev = cJSON_GetObjectItemCaseSensitive(v, "expired");
        dst->fmas_PollExpired = (ev && cJSON_IsTrue(ev)) ? TRUE : FALSE;
        ev = cJSON_GetObjectItemCaseSensitive(v, "multiple");
        dst->fmas_PollMultiple = (ev && cJSON_IsTrue(ev)) ? TRUE : FALSE;
    } else {
        ULONG oi;
        for (oi = 0; oi < FS3ENET_MAX_POLL_OPTIONS; oi++)
            dst->fmas_PollOptionTitles[oi] = NULL;
        dst->fmas_PollOptionCount = 0;
        dst->fmas_PollVotesCount = 0;
        dst->fmas_PollExpired = FALSE;
        dst->fmas_PollMultiple = FALSE;
    }

    /* Link preview card -- see the matching block in FS3ENet_SizeStatusFields. */
    v = cJSON_GetObjectItemCaseSensitive(src, "card");
    if (v && !cJSON_IsNull(v)) {
        const cJSON *f;

        dst->fmas_HasCard = TRUE;

        f = cJSON_GetObjectItemCaseSensitive(v, "url");
        str = (f && cJSON_IsString(f)) ? f->valuestring : "";
        FS3ENet_PackStr(&dst->fmas_CardUrl, p, str);

        f = cJSON_GetObjectItemCaseSensitive(v, "title");
        str = (f && cJSON_IsString(f)) ? f->valuestring : "";
        FS3ENet_PackStrClean(&dst->fmas_CardTitle, p, str);

        f = cJSON_GetObjectItemCaseSensitive(v, "description");
        str = (f && cJSON_IsString(f)) ? f->valuestring : "";
        FS3ENet_PackStrClean(&dst->fmas_CardDescription, p, str);

        f = cJSON_GetObjectItemCaseSensitive(v, "provider_name");
        str = (f && cJSON_IsString(f)) ? f->valuestring : "";
        FS3ENet_PackStrClean(&dst->fmas_CardProviderName, p, str);

        f = cJSON_GetObjectItemCaseSensitive(v, "image");
        str = (f && cJSON_IsString(f)) ? f->valuestring : "";
        FS3ENet_PackStr(&dst->fmas_CardImageUrl, p, str);
    } else {
        dst->fmas_HasCard = FALSE;
        FS3ENet_PackStr(&dst->fmas_CardUrl,          p, "");
        FS3ENet_PackStr(&dst->fmas_CardTitle,        p, "");
        FS3ENet_PackStr(&dst->fmas_CardDescription,  p, "");
        FS3ENet_PackStr(&dst->fmas_CardProviderName, p, "");
        FS3ENet_PackStr(&dst->fmas_CardImageUrl,     p, "");
    }

    /* Action-bar counts/state -- read from src (see the field comment in
     * fs3enet.h: for reblogs these live on the boosted status, not the
     * outer reblog wrapper). */
    v = cJSON_GetObjectItemCaseSensitive(src, "replies_count");
    dst->fmas_RepliesCount = (v && cJSON_IsNumber(v)) ? (ULONG)v->valueint : 0;

    v = cJSON_GetObjectItemCaseSensitive(src, "reblogs_count");
    dst->fmas_ReblogsCount = (v && cJSON_IsNumber(v)) ? (ULONG)v->valueint : 0;

    v = cJSON_GetObjectItemCaseSensitive(src, "favourites_count");
    dst->fmas_FavouritesCount = (v && cJSON_IsNumber(v)) ? (ULONG)v->valueint : 0;

    v = cJSON_GetObjectItemCaseSensitive(src, "favourited");
    dst->fmas_Favourited = (v && cJSON_IsTrue(v)) ? TRUE : FALSE;

    v = cJSON_GetObjectItemCaseSensitive(src, "reblogged");
    dst->fmas_Reblogged = (v && cJSON_IsTrue(v)) ? TRUE : FALSE;
}

/* Sizing pass for FS3ENET_ACCOUNTS_LIST -- mirrors FS3ENet_SizeStatusFields's
 * shape, but item IS the account object itself (top-level id/username/acct/
 * display_name/avatar/note/followers_count/following_count), unlike a
 * status's nested "account" sub-object. */
static ULONG FS3ENet_SizeAccountFields(const cJSON *item)
{
    ULONG total = sizeof(FS3EMastodonAccount);
    const cJSON *v;

    v = cJSON_GetObjectItemCaseSensitive(item, "id");
    total += (v && cJSON_IsString(v) && v->valuestring) ? strlen(v->valuestring) + 1 : 1;

    v = cJSON_GetObjectItemCaseSensitive(item, "username");
    total += (v && cJSON_IsString(v) && v->valuestring) ? strlen(v->valuestring) + 1 : 1;

    v = cJSON_GetObjectItemCaseSensitive(item, "acct");
    total += (v && cJSON_IsString(v) && v->valuestring) ? strlen(v->valuestring) + 1 : 1;

    v = cJSON_GetObjectItemCaseSensitive(item, "display_name");
    total += (v && cJSON_IsString(v) && v->valuestring) ? strlen(v->valuestring) + 1 : 1;

    v = cJSON_GetObjectItemCaseSensitive(item, "avatar");
    total += (v && cJSON_IsString(v) && v->valuestring) ? strlen(v->valuestring) + 1 : 1;

    /* reserve original HTML length for stripped note (stripped <= original) */
    v = cJSON_GetObjectItemCaseSensitive(item, "note");
    total += (v && cJSON_IsString(v) && v->valuestring) ? strlen(v->valuestring) + 1 : 1;

    return total;
}

/* Fill pass matching FS3ENet_SizeAccountFields. stripped/strippedSize is
 * caller-owned scratch space for StripHTML, same reasoning as
 * FS3ENet_FillStatusFields's own stripped buffer. */
static void FS3ENet_FillAccountFields(const cJSON *item, FS3EMastodonAccount *dst,
                                       char **p, char *stripped, ULONG strippedSize)
{
    const cJSON *v;
    const char  *str;

    v = cJSON_GetObjectItemCaseSensitive(item, "id");
    str = (v && cJSON_IsString(v)) ? v->valuestring : "";
    FS3ENet_PackStr(&dst->fma_Id, p, str);

    v = cJSON_GetObjectItemCaseSensitive(item, "username");
    str = (v && cJSON_IsString(v)) ? v->valuestring : "";
    FS3ENet_PackStr(&dst->fma_Username, p, str);

    v = cJSON_GetObjectItemCaseSensitive(item, "acct");
    str = (v && cJSON_IsString(v)) ? v->valuestring : "";
    FS3ENet_PackStr(&dst->fma_Acct, p, str);

    v = cJSON_GetObjectItemCaseSensitive(item, "display_name");
    str = (v && cJSON_IsString(v)) ? v->valuestring : "";
    FS3ENet_PackStrClean(&dst->fma_DisplayName, p, str);

    v = cJSON_GetObjectItemCaseSensitive(item, "avatar");
    str = (v && cJSON_IsString(v)) ? v->valuestring : "";
    FS3ENet_PackStr(&dst->fma_AvatarURL, p, str);

    v = cJSON_GetObjectItemCaseSensitive(item, "note");
    str = (v && cJSON_IsString(v)) ? v->valuestring : "";
    StripHTML(str, stripped, strippedSize);
    FS3ENet_PackStr(&dst->fma_Note, p, stripped);

    v = cJSON_GetObjectItemCaseSensitive(item, "followers_count");
    dst->fma_FollowersCount = (v && cJSON_IsNumber(v)) ? (ULONG)v->valueint : 0;

    v = cJSON_GetObjectItemCaseSensitive(item, "following_count");
    dst->fma_FollowingCount = (v && cJSON_IsNumber(v)) ? (ULONG)v->valueint : 0;
}

static void FS3ENet_HandleTimeline(FS3ENetMessage *fs3em)
{
    FS3ENetTimelineReq   *req = (FS3ENetTimelineReq *)fs3em->fs3em_Data;
    FS3ENetTimelineReply *reply;
    cJSON *json = NULL;
    cJSON *item;
    ULONG count = 0, total;
    char *p;
    char stripped[2048];
    const char *refreshPostId;

    if (!req || fs3em->fs3em_DataLen < sizeof(*req)) {
        fs3em->fs3em_Result = FS3ENETR_PARSE_ERROR;
        return;
    }

    /* See FS3ENetTimelineReq's doc comment on fs3et_MinId: only meaningful
     * (repurposed) for FS3ENET_TLSHAPE_SINGLE_REFRESH. */
    refreshPostId = (req->fs3et_ResponseShape == FS3ENET_TLSHAPE_SINGLE_REFRESH &&
                      req->fs3et_MinId) ? req->fs3et_MinId : "";

    /* Fold max_id/min_id onto the already-built timeline query string (see
     * ViewModeTimeline() in friendsh3ep.c, which already does the same for
     * limit=/local=) -- at most one of the two is ever set (see
     * FS3ENetPageDirection), so this never produces both. */
    {
        char timelineWithPage[300];
        char timelineWithQuery[700];
        const char *timeline = req->fs3et_Timeline ? req->fs3et_Timeline : "";
        const char *finalTimeline;

        if (req->fs3et_MaxId && req->fs3et_MaxId[0])
            snprintf(timelineWithPage, sizeof(timelineWithPage), "%s&max_id=%s",
                     timeline, req->fs3et_MaxId);
        else if (req->fs3et_MinId && req->fs3et_MinId[0])
            snprintf(timelineWithPage, sizeof(timelineWithPage), "%s&min_id=%s",
                     timeline, req->fs3et_MinId);
        else {
            strncpy(timelineWithPage, timeline, sizeof(timelineWithPage) - 1);
            timelineWithPage[sizeof(timelineWithPage) - 1] = '\0';
        }
        finalTimeline = timelineWithPage;

        /* Search: fold the raw query text on as a URL-encoded q= param --
         * kept separate from timelineWithPage's fixed literal/opaque-id
         * components above (never URL-encoded, since they never carry
         * arbitrary user text) because this is the one field here that
         * can. */
        if (req->fs3et_ResponseShape == FS3ENET_TLSHAPE_SEARCH_STATUSES &&
            req->fs3et_SearchQuery && req->fs3et_SearchQuery[0])
        {
            char encQuery[512];
            FS3EMastodon_UrlEncode(req->fs3et_SearchQuery, encQuery, sizeof(encQuery));
            snprintf(timelineWithQuery, sizeof(timelineWithQuery), "%s&q=%s",
                     timelineWithPage, encQuery);
            finalTimeline = timelineWithQuery;
        }

        if (!FS3EMastodon_GetTimeline(req->fs3et_ApiBaseUrl,
                req->fs3et_AccessToken,
                finalTimeline, req->fs3et_ResponseShape, &json)) {
            fs3em->fs3em_Result = FS3ENETR_HTTP_ERROR;
            return;
        }
    }

    /* Pass 1: count statuses and compute flat-block size. */
    total = sizeof(FS3ENetTimelineReply) + FS3ENet_PackLen(refreshPostId);
    cJSON_ArrayForEach(item, json) {
        /* For reblogs: content lives in item.reblog; author is item.reblog.account.
         * The booster is item.account.  For original posts reblog is null/absent. */
        const cJSON *reblog = cJSON_GetObjectItemCaseSensitive(item, "reblog");
        const cJSON *src    = (reblog && !cJSON_IsNull(reblog)) ? reblog : item;
        const cJSON *bAcct  = cJSON_GetObjectItemCaseSensitive(item, "account");
        const cJSON *v;

        if (count >= MAX_STATUSES_TIMELINE) break;

        total += FS3ENet_SizeStatusFields(item, src);

        /* booster display_name + acct (empty strings for non-reblogs) --
         * acct is what a TTL_HOT_AVATAR click on the "X boosted" line
         * actually needs (see TTLPost.boostByAcct): the display name
         * alone can't be looked up via /api/v1/accounts/lookup. Not part
         * of FS3ENet_SizeStatusFields -- see its comment. */
        if (src != item) {
            v = bAcct ? cJSON_GetObjectItemCaseSensitive(bAcct, "display_name") : NULL;
            total += (v && cJSON_IsString(v) && v->valuestring) ? strlen(v->valuestring) + 1 : 1;

            v = bAcct ? cJSON_GetObjectItemCaseSensitive(bAcct, "acct") : NULL;
            total += (v && cJSON_IsString(v) && v->valuestring) ? strlen(v->valuestring) + 1 : 1;
        } else {
            total += 2; /* two empty strings */
        }

        count++;
    }

    reply = (FS3ENetTimelineReply *)AllocVec(total, MEMF_ANY | MEMF_PUBLIC);
    if (!reply) {
        cJSON_Delete(json);
        fs3em->fs3em_Result = FS3ENETR_NETWORK_ERROR;
        return;
    }
    reply->fs3et_ViewModeBit      = req->fs3et_ViewModeBit;
    reply->fs3et_PageDirection    = req->fs3et_PageDirection;
    reply->fs3et_AccountGeneration = req->fs3et_AccountGeneration;
    reply->fs3et_ResponseShape    = req->fs3et_ResponseShape;
    reply->fs3et_Count            = count;

    /* Pass 2: pack strings into the block. */
    {
        FS3ENetStatus *statuses = (FS3ENetStatus *)(reply + 1);
        ULONG i = 0;
        p = (char *)(statuses + count);

        /* Packed first, ahead of the per-status strings below -- reply's
         * own extra field, not one of the FS3ENetStatus[] entries. */
        FS3ENet_PackStr(&reply->fs3et_RefreshPostId, &p, refreshPostId);

        cJSON_ArrayForEach(item, json) {
            const cJSON *reblog, *src, *bAcct, *v;
            const char *str;
            if (i >= count) break;

            reblog = cJSON_GetObjectItemCaseSensitive(item, "reblog");
            src    = (reblog && !cJSON_IsNull(reblog)) ? reblog : item;
            bAcct  = cJSON_GetObjectItemCaseSensitive(item, "account");

            FS3ENet_FillStatusFields(item, src, &statuses[i], &p, stripped, sizeof(stripped));

            /* booster display_name + acct -- not part of
             * FS3ENet_FillStatusFields, see its comment. */
            if (src != item) {
                v = bAcct ? cJSON_GetObjectItemCaseSensitive(bAcct, "display_name") : NULL;
                str = (v && cJSON_IsString(v)) ? v->valuestring : "";
            } else {
                str = "";
            }
            FS3ENet_PackStrClean(&statuses[i].fmas_BoostBy, &p, str);

            if (src != item) {
                v = bAcct ? cJSON_GetObjectItemCaseSensitive(bAcct, "acct") : NULL;
                str = (v && cJSON_IsString(v)) ? v->valuestring : "";
            } else {
                str = "";
            }
            FS3ENet_PackStr(&statuses[i].fmas_BoostByAcct, &p, str);

            i++;
        }
    }

    cJSON_Delete(json);
    FreeVec(fs3em->fs3em_Data);
    fs3em->fs3em_Data    = reply;
    fs3em->fs3em_DataLen = total;
    fs3em->fs3em_Result  = FS3ENETR_OK;
}

/* FS3ENETQ_NOTIFICATIONS — fetch a page of notifications. Reuses
 * FS3EMastodon_GetTimeline() directly rather than a dedicated Mastodon-
 * layer function: GET /api/v1/notifications returns a bare JSON array,
 * the exact FS3ENET_TLSHAPE_ARRAY shape every timeline endpoint already
 * returns, so passing "notifications[?max_id=...]" as the path is all
 * that's needed. Each notification's embedded "status" (when present --
 * see FS3ENetNotification.fen_HasStatus) is parsed via the same
 * FS3ENet_SizeStatusFields/FillStatusFields helpers FS3ENet_HandleTimeline
 * uses, passing the status object as both "item" and "src" (a
 * notification's status is never itself a further reblog wrapper for
 * this app's purposes -- see those helpers' own comment). */
static void FS3ENet_HandleNotifications(FS3ENetMessage *fs3em)
{
    FS3ENetNotificationsReq   *req = (FS3ENetNotificationsReq *)fs3em->fs3em_Data;
    FS3ENetNotificationsReply *reply;
    cJSON *json = NULL;
    cJSON *item;
    ULONG count = 0, total;
    char *p;
    char stripped[2048];

    if (!req || fs3em->fs3em_DataLen < sizeof(*req)) {
        fs3em->fs3em_Result = FS3ENETR_PARSE_ERROR;
        return;
    }


    {
        char pathWithPage[300];

        if (req->fs3en_MaxId && req->fs3en_MaxId[0])
            snprintf(pathWithPage, sizeof(pathWithPage), "notifications?max_id=%s", req->fs3en_MaxId);
        else if (req->fs3en_MinId && req->fs3en_MinId[0])
            snprintf(pathWithPage, sizeof(pathWithPage), "notifications?min_id=%s", req->fs3en_MinId);
        else
            snprintf(pathWithPage, sizeof(pathWithPage), "notifications");

        if (!FS3EMastodon_GetTimeline(req->fs3en_ApiBaseUrl, req->fs3en_AccessToken,
                pathWithPage, FS3ENET_TLSHAPE_ARRAY, &json)) {
            fs3em->fs3em_Result = FS3ENETR_HTTP_ERROR;
            return;
        }
    }

    /* Pass 1: count notifications and compute flat-block size. */
    total = sizeof(FS3ENetNotificationsReply);
    cJSON_ArrayForEach(item, json) {
        const cJSON *account = cJSON_GetObjectItemCaseSensitive(item, "account");
        const cJSON *status  = cJSON_GetObjectItemCaseSensitive(item, "status");
        BOOL hasStatus = (status && !cJSON_IsNull(status)) ? TRUE : FALSE;
        const cJSON *v;

        if (count >= MAX_STATUSES_TIMELINE) break;
        total += sizeof(FS3ENetNotification);

        v = cJSON_GetObjectItemCaseSensitive(item, "id");
        total += (v && cJSON_IsString(v) && v->valuestring) ? strlen(v->valuestring) + 1 : 1;

        v = account ? cJSON_GetObjectItemCaseSensitive(account, "display_name") : NULL;
        total += (v && cJSON_IsString(v) && v->valuestring) ? strlen(v->valuestring) + 1 : 1;

        v = account ? cJSON_GetObjectItemCaseSensitive(account, "acct") : NULL;
        total += (v && cJSON_IsString(v) && v->valuestring) ? strlen(v->valuestring) + 1 : 1;

        v = account ? cJSON_GetObjectItemCaseSensitive(account, "avatar") : NULL;
        total += (v && cJSON_IsString(v) && v->valuestring) ? strlen(v->valuestring) + 1 : 1;

        if (hasStatus)
            total += FS3ENet_SizeStatusFields(status, status);

        count++;
    }

    reply = (FS3ENetNotificationsReply *)AllocVec(total, MEMF_ANY | MEMF_PUBLIC);
    if (!reply) {
        cJSON_Delete(json);
        fs3em->fs3em_Result = FS3ENETR_NETWORK_ERROR;
        return;
    }
    reply->fs3en_PageDirection     = req->fs3en_PageDirection;
    reply->fs3en_AccountGeneration = req->fs3en_AccountGeneration;
    reply->fs3en_Count             = count;

    /* Pass 2: pack strings into the block. */
    {
        FS3ENetNotification *notifs = (FS3ENetNotification *)(reply + 1);
        ULONG i = 0;
        p = (char *)(notifs + count);

        cJSON_ArrayForEach(item, json) {
            const cJSON *account = cJSON_GetObjectItemCaseSensitive(item, "account");
            const cJSON *status  = cJSON_GetObjectItemCaseSensitive(item, "status");
            const cJSON *typeV   = cJSON_GetObjectItemCaseSensitive(item, "type");
            const cJSON *v;
            const char *str;
            const char *typeStr;
            if (i >= count) break;

            notifs[i].fen_HasStatus = (status && !cJSON_IsNull(status)) ? TRUE : FALSE;

            typeStr = (typeV && cJSON_IsString(typeV)) ? typeV->valuestring : "";
            if      (strcmp(typeStr, "mention")   == 0) notifs[i].fen_Type = FS3ENOTIF_MENTION;
            else if (strcmp(typeStr, "reblog")    == 0) notifs[i].fen_Type = FS3ENOTIF_REBLOG;
            else if (strcmp(typeStr, "favourite") == 0) notifs[i].fen_Type = FS3ENOTIF_FAVOURITE;
            else if (strcmp(typeStr, "follow")    == 0) notifs[i].fen_Type = FS3ENOTIF_FOLLOW;
            else if (strcmp(typeStr, "follow_request") == 0) notifs[i].fen_Type = FS3ENOTIF_FOLLOW_REQUEST;
            else if (strcmp(typeStr, "poll")      == 0) notifs[i].fen_Type = FS3ENOTIF_POLL;
            else if (strcmp(typeStr, "update")    == 0) notifs[i].fen_Type = FS3ENOTIF_UPDATE;
            else                                          notifs[i].fen_Type = FS3ENOTIF_UNKNOWN;

            v = cJSON_GetObjectItemCaseSensitive(item, "id");
            str = (v && cJSON_IsString(v)) ? v->valuestring : "";
            FS3ENet_PackStr(&notifs[i].fen_Id, &p, str);

            v = account ? cJSON_GetObjectItemCaseSensitive(account, "display_name") : NULL;
            str = (v && cJSON_IsString(v)) ? v->valuestring : "";
            FS3ENet_PackStrClean(&notifs[i].fen_ActorDisplayName, &p, str);

            v = account ? cJSON_GetObjectItemCaseSensitive(account, "acct") : NULL;
            str = (v && cJSON_IsString(v)) ? v->valuestring : "";
            FS3ENet_PackStr(&notifs[i].fen_ActorAcct, &p, str);

            v = account ? cJSON_GetObjectItemCaseSensitive(account, "avatar") : NULL;
            str = (v && cJSON_IsString(v)) ? v->valuestring : "";
            FS3ENet_PackStr(&notifs[i].fen_ActorAvatarURL, &p, str);

            if (notifs[i].fen_HasStatus)
                FS3ENet_FillStatusFields(status, status, &notifs[i].fen_Status, &p, stripped, sizeof(stripped));
            else
                memset(&notifs[i].fen_Status, 0, sizeof(notifs[i].fen_Status));

            i++;
        }
    }

    cJSON_Delete(json);
    FreeVec(fs3em->fs3em_Data);
    fs3em->fs3em_Data    = reply;
    fs3em->fs3em_DataLen = total;
    fs3em->fs3em_Result  = FS3ENETR_OK;
}

/* FS3ENETQ_ACCOUNTS_LIST — fetch a list of accounts: fuzzy search
 * (type=accounts), or a user's followers/following. Followers/following
 * already return a bare Account[] (FS3ENET_TLSHAPE_ARRAY, unchanged); search
 * needs the {"accounts":[...],...} wrapper unwrapped, see
 * FS3ENET_TLSHAPE_SEARCH_ACCOUNTS. Single page only -- see this request's
 * doc comment in fs3enet.h for why. */
static void FS3ENet_HandleAccountsList(FS3ENetMessage *fs3em)
{
    FS3ENetAccountsListReq   *req = (FS3ENetAccountsListReq *)fs3em->fs3em_Data;
    FS3ENetAccountsListReply *reply;
    cJSON *json = NULL;
    cJSON *item;
    ULONG count = 0, total;
    char *p;
    char stripped[2048];

    if (!req || fs3em->fs3em_DataLen < sizeof(*req)) {
        fs3em->fs3em_Result = FS3ENETR_PARSE_ERROR;
        return;
    }

    {
        char path[400];
        ULONG shape = FS3ENET_TLSHAPE_ARRAY;

        if (req->fs3eal_Kind == FS3ENET_ACCLIST_FOLLOWERS)
            snprintf(path, sizeof(path), "accounts/%s/followers?limit=40", req->fs3eal_AccountId);
        else if (req->fs3eal_Kind == FS3ENET_ACCLIST_FOLLOWING)
            snprintf(path, sizeof(path), "accounts/%s/following?limit=40", req->fs3eal_AccountId);
        else {
            char encQuery[512];
            FS3EMastodon_UrlEncode(req->fs3eal_Query ? req->fs3eal_Query : "",
                                    encQuery, sizeof(encQuery));
            snprintf(path, sizeof(path), "search?type=accounts&limit=20&q=%s", encQuery);
            shape = FS3ENET_TLSHAPE_SEARCH_ACCOUNTS;
        }

        if (!FS3EMastodon_GetTimeline(req->fs3eal_ApiBaseUrl, req->fs3eal_AccessToken,
                path, shape, &json)) {
            fs3em->fs3em_Result = FS3ENETR_HTTP_ERROR;
            return;
        }
    }

    /* Pass 1: count accounts and compute flat-block size. */
    total = sizeof(FS3ENetAccountsListReply);
    cJSON_ArrayForEach(item, json) {
        if (count >= MAX_STATUSES_TIMELINE) break;
        total += FS3ENet_SizeAccountFields(item);
        count++;
    }

    reply = (FS3ENetAccountsListReply *)AllocVec(total, MEMF_ANY | MEMF_PUBLIC);
    if (!reply) {
        cJSON_Delete(json);
        fs3em->fs3em_Result = FS3ENETR_NETWORK_ERROR;
        return;
    }
    reply->fs3eal_Kind              = req->fs3eal_Kind;
    reply->fs3eal_AccountGeneration = req->fs3eal_AccountGeneration;
    reply->fs3eal_Count             = count;

    /* Pass 2: pack accounts into the block. */
    {
        FS3EMastodonAccount *accounts = (FS3EMastodonAccount *)(reply + 1);
        ULONG i = 0;
        p = (char *)(accounts + count);

        cJSON_ArrayForEach(item, json) {
            if (i >= count) break;
            FS3ENet_FillAccountFields(item, &accounts[i], &p, stripped, sizeof(stripped));
            i++;
        }
    }

    cJSON_Delete(json);
    FreeVec(fs3em->fs3em_Data);
    fs3em->fs3em_Data    = reply;
    fs3em->fs3em_DataLen = total;
    fs3em->fs3em_Result  = FS3ENETR_OK;
}

/* FS3ENETQ_POST_STATUS — publish a toot and return its id. */
static void FS3ENet_HandlePostStatus(FS3ENetMessage *fs3em)
{
    FS3ENetPostStatusReq   *req = (FS3ENetPostStatusReq *)fs3em->fs3em_Data;
    FS3ENetPostStatusReply *reply;
    char statusId[64];
    ULONG total;
    char *p;

    if (!req || fs3em->fs3em_DataLen < sizeof(*req)) {
        fs3em->fs3em_Result = FS3ENETR_PARSE_ERROR;
        return;
    }


    if (!FS3EMastodon_PostStatus(req->fs3ep_ApiBaseUrl, req->fs3ep_AccessToken,
            req->fs3ep_Content, req->fs3ep_Visibility, req->fs3ep_InReplyToId,
            req->fs3ep_QuoteApprovalPolicy,
            statusId, sizeof(statusId)))
    {
        fs3em->fs3em_Result = FS3ENETR_HTTP_ERROR;
        return;
    }

    total = sizeof(FS3ENetPostStatusReply) + FS3ENet_PackLen(statusId);
    reply = (FS3ENetPostStatusReply *)AllocVec(total, MEMF_ANY | MEMF_PUBLIC);
    if (!reply) { fs3em->fs3em_Result = FS3ENETR_NETWORK_ERROR; return; }

    p = (char *)reply + sizeof(*reply);
    FS3ENet_PackStr(&reply->fs3ep_StatusId, &p, statusId);

    FreeVec(fs3em->fs3em_Data);
    fs3em->fs3em_Data    = reply;
    fs3em->fs3em_DataLen = total;
    fs3em->fs3em_Result  = FS3ENETR_OK;
}

/* FS3ENETQ_EDIT_STATUS — edit an existing status' text (own toots only). */
static void FS3ENet_HandleEditStatus(FS3ENetMessage *fs3em)
{
    FS3ENetEditStatusReq   *req = (FS3ENetEditStatusReq *)fs3em->fs3em_Data;
    FS3ENetEditStatusReply *reply;
    ULONG total;
    char *p;

    if (!req || fs3em->fs3em_DataLen < sizeof(*req)) {
        fs3em->fs3em_Result = FS3ENETR_PARSE_ERROR;
        return;
    }


    if (!FS3EMastodon_EditStatus(req->fs3ee_ApiBaseUrl, req->fs3ee_AccessToken,
            req->fs3ee_StatusId, req->fs3ee_Content,
            (const char *const *)req->fs3ee_MediaIds, req->fs3ee_MediaCount))
    {
        fs3em->fs3em_Result = FS3ENETR_HTTP_ERROR;
        return;
    }

    total = sizeof(FS3ENetEditStatusReply) + FS3ENet_PackLen(req->fs3ee_StatusId);
    reply = (FS3ENetEditStatusReply *)AllocVec(total, MEMF_ANY | MEMF_PUBLIC);
    if (!reply) { fs3em->fs3em_Result = FS3ENETR_NETWORK_ERROR; return; }

    p = (char *)reply + sizeof(*reply);
    FS3ENet_PackStr(&reply->fs3ee_StatusId, &p, req->fs3ee_StatusId);

    FreeVec(fs3em->fs3em_Data);
    fs3em->fs3em_Data    = reply;
    fs3em->fs3em_DataLen = total;
    fs3em->fs3em_Result  = FS3ENETR_OK;
}

/* FS3ENETQ_DELETE_STATUS — delete an existing status (own toots only). */
static void FS3ENet_HandleDeleteStatus(FS3ENetMessage *fs3em)
{
    FS3ENetDeleteStatusReq   *req = (FS3ENetDeleteStatusReq *)fs3em->fs3em_Data;
    FS3ENetDeleteStatusReply *reply;
    ULONG total;
    char *p;

    if (!req || fs3em->fs3em_DataLen < sizeof(*req)) {
        fs3em->fs3em_Result = FS3ENETR_PARSE_ERROR;
        return;
    }


    if (!FS3EMastodon_DeleteStatus(req->fs3ed_ApiBaseUrl, req->fs3ed_AccessToken,
            req->fs3ed_StatusId))
    {
        fs3em->fs3em_Result = FS3ENETR_HTTP_ERROR;
        return;
    }

    total = sizeof(FS3ENetDeleteStatusReply) + FS3ENet_PackLen(req->fs3ed_StatusId);
    reply = (FS3ENetDeleteStatusReply *)AllocVec(total, MEMF_ANY | MEMF_PUBLIC);
    if (!reply) { fs3em->fs3em_Result = FS3ENETR_NETWORK_ERROR; return; }

    p = (char *)reply + sizeof(*reply);
    FS3ENet_PackStr(&reply->fs3ed_StatusId, &p, req->fs3ed_StatusId);

    FreeVec(fs3em->fs3em_Data);
    fs3em->fs3em_Data    = reply;
    fs3em->fs3em_DataLen = total;
    fs3em->fs3em_Result  = FS3ENETR_OK;
}

/* FS3ENETQ_FAVORITE — toggle favourite/unfavourite on a status, returning
 * the server-confirmed favourited boolean (see the field comment on
 * FS3ENetFavouriteReply in fs3enet.h -- deliberately not that response's
 * other counts too). */
static void FS3ENet_HandleFavourite(FS3ENetMessage *fs3em)
{
    FS3ENetFavouriteReq    *req = (FS3ENetFavouriteReq *)fs3em->fs3em_Data;
    FS3ENetFavouriteReply  *reply;
    BOOL  favourited;
    ULONG total;
    char *p;

    if (!req || fs3em->fs3em_DataLen < sizeof(*req)) {
        fs3em->fs3em_Result = FS3ENETR_PARSE_ERROR;
        return;
    }


    if (!FS3EMastodon_Favourite(req->fs3efa_ApiBaseUrl, req->fs3efa_AccessToken,
            req->fs3efa_StatusId, req->fs3efa_Favourite, &favourited))
    {
        fs3em->fs3em_Result = FS3ENETR_HTTP_ERROR;
        return;
    }

    total = sizeof(FS3ENetFavouriteReply) + FS3ENet_PackLen(req->fs3efa_StatusId);
    reply = (FS3ENetFavouriteReply *)AllocVec(total, MEMF_ANY | MEMF_PUBLIC);
    if (!reply) { fs3em->fs3em_Result = FS3ENETR_NETWORK_ERROR; return; }

    p = (char *)reply + sizeof(*reply);
    FS3ENet_PackStr(&reply->fs3efa_StatusId, &p, req->fs3efa_StatusId);
    reply->fs3efa_Favourited = favourited;

    FreeVec(fs3em->fs3em_Data);
    fs3em->fs3em_Data    = reply;
    fs3em->fs3em_DataLen = total;
    fs3em->fs3em_Result  = FS3ENETR_OK;
}

/* FS3ENETQ_ACCOUNT_LOOKUP — resolve an acct string to a full account
 * (profile view entry point). */
static void FS3ENet_HandleAccountLookup(FS3ENetMessage *fs3em)
{
    FS3ENetAccountLookupReq   *req = (FS3ENetAccountLookupReq *)fs3em->fs3em_Data;
    FS3ENetAccountLookupReply *reply;
    FS3EMastodonAccount        tmpAcc = {0};
    char  stripped[2048];
    ULONG total;
    char *p;

    if (!req || fs3em->fs3em_DataLen < sizeof(*req)) {
        fs3em->fs3em_Result = FS3ENETR_PARSE_ERROR;
        return;
    }


    if (!FS3EMastodon_LookupAccount(req->fs3eal_ApiBaseUrl, req->fs3eal_AccessToken,
            req->fs3eal_Acct, &tmpAcc))
    {
        FS3EMastodonAccount_Free(&tmpAcc);
        fs3em->fs3em_Result = FS3ENETR_HTTP_ERROR;
        return;
    }

    StripHTML(tmpAcc.fma_Note ? tmpAcc.fma_Note : "", stripped, sizeof(stripped));

    total = sizeof(FS3ENetAccountLookupReply)
          + FS3ENet_PackLen(tmpAcc.fma_Id)
          + FS3ENet_PackLen(tmpAcc.fma_Username)
          + FS3ENet_PackLen(tmpAcc.fma_Acct)
          + FS3ENet_PackLen(tmpAcc.fma_DisplayName)
          + FS3ENet_PackLen(tmpAcc.fma_AvatarURL)
          + FS3ENet_PackLen(stripped);

    reply = (FS3ENetAccountLookupReply *)AllocVec(total, MEMF_ANY | MEMF_PUBLIC);
    if (!reply) {
        FS3EMastodonAccount_Free(&tmpAcc);
        fs3em->fs3em_Result = FS3ENETR_NETWORK_ERROR;
        return;
    }

    p = (char *)reply + sizeof(*reply);
    FS3ENet_PackStr(&reply->fs3eal_Account.fma_Id,          &p, tmpAcc.fma_Id);
    FS3ENet_PackStr(&reply->fs3eal_Account.fma_Username,    &p, tmpAcc.fma_Username);
    FS3ENet_PackStr(&reply->fs3eal_Account.fma_Acct,        &p, tmpAcc.fma_Acct);
    FS3ENet_PackStrClean(&reply->fs3eal_Account.fma_DisplayName, &p, tmpAcc.fma_DisplayName);
    FS3ENet_PackStr(&reply->fs3eal_Account.fma_AvatarURL,   &p, tmpAcc.fma_AvatarURL);
    FS3ENet_PackStr(&reply->fs3eal_Account.fma_Note,        &p, stripped);
    reply->fs3eal_Account.fma_FollowersCount = tmpAcc.fma_FollowersCount;
    reply->fs3eal_Account.fma_FollowingCount = tmpAcc.fma_FollowingCount;

    FS3EMastodonAccount_Free(&tmpAcc);

    FreeVec(fs3em->fs3em_Data);
    fs3em->fs3em_Data    = reply;
    fs3em->fs3em_DataLen = total;
    fs3em->fs3em_Result  = FS3ENETR_OK;
}

/* FS3ENETQ_RELATIONSHIP — fetch following state for an account id. */
static void FS3ENet_HandleRelationship(FS3ENetMessage *fs3em)
{
    FS3ENetRelationshipReq   *req = (FS3ENetRelationshipReq *)fs3em->fs3em_Data;
    FS3ENetRelationshipReply *reply;
    BOOL  following;
    ULONG total;
    char *p;

    if (!req || fs3em->fs3em_DataLen < sizeof(*req)) {
        fs3em->fs3em_Result = FS3ENETR_PARSE_ERROR;
        return;
    }


    if (!FS3EMastodon_GetRelationship(req->fs3erl_ApiBaseUrl, req->fs3erl_AccessToken,
            req->fs3erl_AccountId, &following))
    {
        fs3em->fs3em_Result = FS3ENETR_HTTP_ERROR;
        return;
    }

    total = sizeof(FS3ENetRelationshipReply) + FS3ENet_PackLen(req->fs3erl_AccountId);
    reply = (FS3ENetRelationshipReply *)AllocVec(total, MEMF_ANY | MEMF_PUBLIC);
    if (!reply) { fs3em->fs3em_Result = FS3ENETR_NETWORK_ERROR; return; }

    p = (char *)reply + sizeof(*reply);
    FS3ENet_PackStr(&reply->fs3erl_AccountId, &p, req->fs3erl_AccountId);
    reply->fs3erl_Following = following;

    FreeVec(fs3em->fs3em_Data);
    fs3em->fs3em_Data    = reply;
    fs3em->fs3em_DataLen = total;
    fs3em->fs3em_Result  = FS3ENETR_OK;
}

/* FS3ENETQ_RELATIONSHIPS — batch counterpart of FS3ENETQ_RELATIONSHIP
 * above, fetching following+followed_by for every id in req's trailing
 * char*[] array in one call, for badging TTLAccountRow_Class list rows
 * (see TTL_POSTUPD_RELATIONSHIP). Reply entries are built by walking the
 * SERVER's own response array, not the request's id list -- same "trust
 * what came back" approach FS3ENet_HandleAccountsList takes with its own
 * array, so any ordering difference or omission in the server's reply
 * (e.g. an id the server silently drops) never desyncs what gets packed
 * here; the GUI side matches entries back to rows by account id anyway
 * (TTIMELINE_UpdatePost's postId match), not by position. */
static void FS3ENet_HandleRelationships(FS3ENetMessage *fs3em)
{
    FS3ENetRelationshipsReq   *req = (FS3ENetRelationshipsReq *)fs3em->fs3em_Data;
    FS3ENetRelationshipsReply *reply;
    cJSON *json = NULL;
    cJSON *item;
    char **ids;
    ULONG count = 0, total;
    char *p;

    if (!req || fs3em->fs3em_DataLen < sizeof(*req)) {
        fs3em->fs3em_Result = FS3ENETR_PARSE_ERROR;
        return;
    }

    ids = (char **)(req + 1);

    if (!FS3EMastodon_GetRelationships(req->fs3erls_ApiBaseUrl, req->fs3erls_AccessToken,
            (const char *const *)ids, req->fs3erls_Count, &json))
    {
        fs3em->fs3em_Result = FS3ENETR_HTTP_ERROR;
        return;
    }

    /* Pass 1: count usable entries (an "id" field is the minimum to be
     * usable -- following/followed_by simply default FALSE if somehow
     * absent) and compute flat-block size. */
    total = sizeof(FS3ENetRelationshipsReply);
    cJSON_ArrayForEach(item, json) {
        const cJSON *idv = cJSON_GetObjectItemCaseSensitive(item, "id");
        if (!idv || !cJSON_IsString(idv)) continue;
        total += sizeof(FS3ENetRelationshipEntry) + FS3ENet_PackLen(idv->valuestring);
        count++;
    }

    reply = (FS3ENetRelationshipsReply *)AllocVec(total, MEMF_ANY | MEMF_PUBLIC);
    if (!reply) {
        cJSON_Delete(json);
        fs3em->fs3em_Result = FS3ENETR_NETWORK_ERROR;
        return;
    }
    reply->fs3erls_AccountGeneration = req->fs3erls_AccountGeneration;
    reply->fs3erls_Count             = count;

    /* Pass 2: pack entries into the block. */
    {
        FS3ENetRelationshipEntry *entries = (FS3ENetRelationshipEntry *)(reply + 1);
        ULONG idx = 0;
        p = (char *)(entries + count);

        cJSON_ArrayForEach(item, json) {
            const cJSON *idv, *followingV, *followedByV;
            if (idx >= count) break;

            idv = cJSON_GetObjectItemCaseSensitive(item, "id");
            if (!idv || !cJSON_IsString(idv)) continue;

            followingV  = cJSON_GetObjectItemCaseSensitive(item, "following");
            followedByV = cJSON_GetObjectItemCaseSensitive(item, "followed_by");

            FS3ENet_PackStr(&entries[idx].fs3erle_AccountId, &p, idv->valuestring);
            entries[idx].fs3erle_Following  = (followingV  && cJSON_IsTrue(followingV))  ? TRUE : FALSE;
            entries[idx].fs3erle_FollowedBy = (followedByV && cJSON_IsTrue(followedByV)) ? TRUE : FALSE;
            idx++;
        }
    }

    cJSON_Delete(json);
    FreeVec(fs3em->fs3em_Data);
    fs3em->fs3em_Data    = reply;
    fs3em->fs3em_DataLen = total;
    fs3em->fs3em_Result  = FS3ENETR_OK;
}

/* FS3ENETQ_FOLLOW — toggle follow/unfollow on an account, returning the
 * server-confirmed following boolean (see FS3ENetFollowReply's comment --
 * deliberately not any counts too). */
static void FS3ENet_HandleFollow(FS3ENetMessage *fs3em)
{
    FS3ENetFollowReq    *req = (FS3ENetFollowReq *)fs3em->fs3em_Data;
    FS3ENetFollowReply  *reply;
    BOOL  following;
    ULONG total;
    char *p;

    if (!req || fs3em->fs3em_DataLen < sizeof(*req)) {
        fs3em->fs3em_Result = FS3ENETR_PARSE_ERROR;
        return;
    }


    if (!FS3EMastodon_Follow(req->fs3efo_ApiBaseUrl, req->fs3efo_AccessToken,
            req->fs3efo_AccountId, req->fs3efo_Follow, &following))
    {
        fs3em->fs3em_Result = FS3ENETR_HTTP_ERROR;
        return;
    }

    total = sizeof(FS3ENetFollowReply) + FS3ENet_PackLen(req->fs3efo_AccountId);
    reply = (FS3ENetFollowReply *)AllocVec(total, MEMF_ANY | MEMF_PUBLIC);
    if (!reply) { fs3em->fs3em_Result = FS3ENETR_NETWORK_ERROR; return; }

    p = (char *)reply + sizeof(*reply);
    FS3ENet_PackStr(&reply->fs3efo_AccountId, &p, req->fs3efo_AccountId);
    reply->fs3efo_Following = following;

    FreeVec(fs3em->fs3em_Data);
    fs3em->fs3em_Data    = reply;
    fs3em->fs3em_DataLen = total;
    fs3em->fs3em_Result  = FS3ENETR_OK;
}

/* FS3ENETQ_FLUSH_CACHE — delete every file in the disk cache directory. */
static void FS3ENet_HandleFlushCache(FS3ENetMessage *fs3em)
{
    fs3em->fs3em_Result = FS3ECache_Flush() ? FS3ENETR_OK : FS3ENETR_NETWORK_ERROR;
}

/* Handles one request. FS3ENETQ_TIMELINE and FS3ENETQ_POST_STATUS are
 * stubbed until Phase 2 completes the timeline/post flow.
 *
 * Returns TRUE if fs3em is fully resolved and the caller (FS3ENet_ProcEntry)
 * should ReplyMsg() it now, same as every request always used to work.
 * Returns FALSE only for a FS3ENETQ_FETCH_IMAGE cache miss that's been
 * handed off to the chunked download engine (see FS3ENet_HandleFetchImage) --
 * that message is replied later, from FS3ENet_FinishDownload(), once the
 * whole file has arrived or the download has given up. */
static BOOL FS3ENet_Dispatch(FS3ENetMessage *fs3em)
{
    switch (fs3em->fs3em_Type)
    {
        case FS3ENETQ_LOGIN_START:
            FS3ENet_HandleLoginStart(fs3em);
            break;

        case FS3ENETQ_LOGIN_FINISH:
            FS3ENet_HandleLoginFinish(fs3em);
            break;

        case FS3ENETQ_FETCH_IMAGE:
            return FS3ENet_HandleFetchImage(fs3em);

        case FS3ENETQ_FLUSH_CACHE:
            FS3ENet_HandleFlushCache(fs3em);
            break;

        case FS3ENETQ_TIMELINE:
            FS3ENet_HandleTimeline(fs3em);
            break;

        case FS3ENETQ_POST_STATUS:
            FS3ENet_HandlePostStatus(fs3em);
            break;

        case FS3ENETQ_EDIT_STATUS:
            FS3ENet_HandleEditStatus(fs3em);
            break;

        case FS3ENETQ_DELETE_STATUS:
            FS3ENet_HandleDeleteStatus(fs3em);
            break;

        case FS3ENETQ_NOTIFICATIONS:
            FS3ENet_HandleNotifications(fs3em);
            break;

        case FS3ENETQ_ACCOUNTS_LIST:
            FS3ENet_HandleAccountsList(fs3em);
            break;

        case FS3ENETQ_VERIFY_ACCOUNT:
            FS3ENet_HandleVerifyAccount(fs3em);
            break;

        case FS3ENETQ_FAVORITE:
            FS3ENet_HandleFavourite(fs3em);
            break;

        case FS3ENETQ_ACCOUNT_LOOKUP:
            FS3ENet_HandleAccountLookup(fs3em);
            break;

        case FS3ENETQ_RELATIONSHIP:
            FS3ENet_HandleRelationship(fs3em);
            break;

        case FS3ENETQ_RELATIONSHIPS:
            FS3ENet_HandleRelationships(fs3em);
            break;

        case FS3ENETQ_FOLLOW:
            FS3ENet_HandleFollow(fs3em);
            break;

        case FS3ENETQ_INSTANCE_INFO:
            FS3ENet_HandleInstanceInfo(fs3em);
            break;

        default:
            fs3em->fs3em_Result = FS3ENETR_OK;
            break;
    }

    return TRUE;
}
