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

#include <string.h>

#define FS3ENET_STACK_SIZE 16384
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

/* ---- Public _Alloc helpers (called by the GUI before PutMsg) ------------ */

FS3ENetLoginStartReq *FS3ENetLoginStartReq_Alloc(const char *apiBaseUrl)
{
    ULONG total = sizeof(FS3ENetLoginStartReq) + FS3ENet_PackLen(apiBaseUrl);
    FS3ENetLoginStartReq *req =
        (FS3ENetLoginStartReq *)AllocVec(total, MEMF_ANY);
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
        (FS3ENetLoginFinishReq *)AllocVec(total, MEMF_ANY);
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
        (FS3ENetVerifyAccountReq *)AllocVec(total, MEMF_ANY);
    char *p;

    if (!req) return NULL;
    p = (char *)req + sizeof(*req);
    FS3ENet_PackStr(&req->fs3eva_ApiBaseUrl,  &p, apiBaseUrl);
    FS3ENet_PackStr(&req->fs3eva_AccessToken, &p, accessToken);
    return req;
}

FS3ENetTimelineReq *FS3ENetTimelineReq_Alloc(ULONG viewModeBit,
    const char *apiBaseUrl, const char *accessToken,
    const char *timeline, const char *maxId)
{
    ULONG total = sizeof(FS3ENetTimelineReq)
                + FS3ENet_PackLen(apiBaseUrl)
                + FS3ENet_PackLen(accessToken)
                + FS3ENet_PackLen(timeline)
                + FS3ENet_PackLen(maxId);
    FS3ENetTimelineReq *req =
        (FS3ENetTimelineReq *)AllocVec(total, MEMF_ANY);
    char *p;

    if (!req) return NULL;
    req->fs3et_ViewModeBit = viewModeBit;
    p = (char *)req + sizeof(*req);
    FS3ENet_PackStr(&req->fs3et_ApiBaseUrl,   &p, apiBaseUrl);
    FS3ENet_PackStr(&req->fs3et_AccessToken,  &p, accessToken);
    FS3ENet_PackStr(&req->fs3et_Timeline,     &p, timeline);
    FS3ENet_PackStr(&req->fs3et_MaxId,        &p, maxId);
    return req;
}

FS3ENetPostStatusReq *FS3ENetPostStatusReq_Alloc(
    const char *apiBaseUrl, const char *accessToken,
    const char *content, const char *visibility, const char *spoiler)
{
    ULONG total = sizeof(FS3ENetPostStatusReq)
                + FS3ENet_PackLen(apiBaseUrl)
                + FS3ENet_PackLen(accessToken)
                + FS3ENet_PackLen(content)
                + FS3ENet_PackLen(visibility)
                + FS3ENet_PackLen(spoiler);
    FS3ENetPostStatusReq *req =
        (FS3ENetPostStatusReq *)AllocVec(total, MEMF_ANY);
    char *p;

    if (!req) return NULL;
    p = (char *)req + sizeof(*req);
    FS3ENet_PackStr(&req->fs3ep_ApiBaseUrl,   &p, apiBaseUrl);
    FS3ENet_PackStr(&req->fs3ep_AccessToken,  &p, accessToken);
    FS3ENet_PackStr(&req->fs3ep_Content,      &p, content);
    FS3ENet_PackStr(&req->fs3ep_Visibility,   &p, visibility);
    FS3ENet_PackStr(&req->fs3ep_Spoiler,      &p, spoiler);
    return req;
}

FS3ENetFetchImageReq *FS3ENetFetchImageReq_Alloc(const char *url, const char *key,
                                                   const char *subdir, BOOL keepOriginal)
{
    ULONG total = sizeof(FS3ENetFetchImageReq)
                + FS3ENet_PackLen(url)
                + FS3ENet_PackLen(key)
                + FS3ENet_PackLen(subdir);
    FS3ENetFetchImageReq *req =
        (FS3ENetFetchImageReq *)AllocVec(total, MEMF_ANY);
    char *p;

    if (!req) return NULL;
    p = (char *)req + sizeof(*req);
    FS3ENet_PackStr(&req->fs3enf_Url,    &p, url);
    FS3ENet_PackStr(&req->fs3enf_Key,    &p, key ? key : "");
    FS3ENet_PackStr(&req->fs3enf_Subdir, &p, subdir ? subdir : "");
    req->fs3enf_KeepOriginal = keepOriginal;
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
static void FS3ENet_Dispatch(FS3ENetMessage *fs3em);

struct MsgPort *FS3ENet_Start(const char *cacheDir)
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
        FS3ECache_Init(startup->fs3ess_CacheDir);

    /* Hand the request port (or NULL on failure) back to FS3ENet_Start(). */
    startup->fs3ess_RequestPort = requestPort;
    PutMsg(startup->fs3ess_Msg.mn_ReplyPort, &startup->fs3ess_Msg);

    if (!requestPort)
        return;

    while (running)
    {
        FS3ENetMessage *fs3em;

        WaitPort(requestPort);

        /* FS3ENet_Stop() Signal()s this before the shutdown message even
         * reaches the queue -- once noticed, every message still queued
         * behind whatever's currently dispatching gets an immediate error
         * reply instead of actually being worked (real HTTP fetches), so
         * shutdown doesn't have to wait out the entire backlog in FIFO
         * order. The one thing this can't shorten is a request already
         * mid-dispatch when the signal arrives (e.g. a slow HTTP GET already
         * under way) -- that one still runs to completion. */
        if (!stopping && FS3ENET_STOP_SIGMASK &&
            (SetSignal(0, FS3ENET_STOP_SIGMASK) & FS3ENET_STOP_SIGMASK))
            stopping = TRUE;

        while ((fs3em = (FS3ENetMessage *)GetMsg(requestPort)) != NULL)
        {
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
                FS3ENet_Dispatch(fs3em);
                if (!stopping && FS3ENET_STOP_SIGMASK &&
                    (SetSignal(0, FS3ENET_STOP_SIGMASK) & FS3ENET_STOP_SIGMASK))
                    stopping = TRUE;
            }

            ReplyMsg((struct Message *)fs3em);
        }
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
        printf("net: LOGIN_START parse error\n");
        fs3em->fs3em_Result = FS3ENETR_PARSE_ERROR;
        return;
    }

    printf("net: LOGIN_START server=%s\n", req->fs3enl_ApiBaseUrl ? req->fs3enl_ApiBaseUrl : "NULL");

    if (!FS3EMastodon_CreateApp(req->fs3enl_ApiBaseUrl, FS3ENET_CLIENT_NAME,
            clientId, sizeof(clientId),
            clientSecret, sizeof(clientSecret)))
    {
        printf("net: LOGIN_START CreateApp failed\n");
        fs3em->fs3em_Result = FS3ENETR_HTTP_ERROR;
        return;
    }

    printf("net: LOGIN_START app registered, clientId=%s\n", clientId);

    FS3EMastodon_BuildAuthorizeURL(req->fs3enl_ApiBaseUrl, clientId,
        authorizeUrl, sizeof(authorizeUrl));

    total = sizeof(FS3ENetLoginStartReply)
          + FS3ENet_PackLen(clientId)
          + FS3ENet_PackLen(clientSecret)
          + FS3ENet_PackLen(authorizeUrl);

    reply = (FS3ENetLoginStartReply *)AllocVec(total, MEMF_ANY);
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
    printf("net: LOGIN_START done, url=%s\n", authorizeUrl);
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
        printf("net: LOGIN_FINISH parse error\n");
        fs3em->fs3em_Result = FS3ENETR_PARSE_ERROR;
        return;
    }

    printf("net: LOGIN_FINISH server=%s\n", req->fs3enl_ApiBaseUrl ? req->fs3enl_ApiBaseUrl : "NULL");

    if (!FS3EMastodon_ExchangeCode(req->fs3enl_ApiBaseUrl, req->fs3enl_ClientId,
            req->fs3enl_ClientSecret, req->fs3enl_Code,
            accessToken, sizeof(accessToken)))
    {
        printf("net: LOGIN_FINISH ExchangeCode failed\n");
        fs3em->fs3em_Result = FS3ENETR_AUTH_ERROR;
        return;
    }

    printf("net: LOGIN_FINISH token obtained, verifying credentials\n");

    if (!FS3EMastodon_VerifyCredentials(req->fs3enl_ApiBaseUrl, accessToken, &tmpAcc))
    {
        printf("net: LOGIN_FINISH VerifyCredentials failed\n");
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

    reply = (FS3ENetLoginFinishReply *)AllocVec(total, MEMF_ANY);
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
    FS3ENet_PackStr(&reply->fs3enl_Account.fma_DisplayName, &p, tmpAcc.fma_DisplayName);
    FS3ENet_PackStr(&reply->fs3enl_Account.fma_AvatarURL,   &p, tmpAcc.fma_AvatarURL);

    FS3EMastodonAccount_Free(&tmpAcc);

    printf("net: LOGIN_FINISH done, acct=%s\n", reply->fs3enl_Account.fma_Acct ? reply->fs3enl_Account.fma_Acct : "?");
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
        printf("net: VERIFY_ACCOUNT parse error\n");
        fs3em->fs3em_Result = FS3ENETR_PARSE_ERROR;
        return;
    }

    printf("net: VERIFY_ACCOUNT server=%s\n", req->fs3eva_ApiBaseUrl ? req->fs3eva_ApiBaseUrl : "NULL");

    if (!FS3EMastodon_VerifyCredentials(req->fs3eva_ApiBaseUrl, req->fs3eva_AccessToken, &tmpAcc))
    {
        printf("net: VERIFY_ACCOUNT VerifyCredentials failed\n");
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

    reply = (FS3ENetVerifyAccountReply *)AllocVec(total, MEMF_ANY);
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
    FS3ENet_PackStr(&reply->fs3eva_Account.fma_DisplayName, &p, tmpAcc.fma_DisplayName);
    FS3ENet_PackStr(&reply->fs3eva_Account.fma_AvatarURL,   &p, tmpAcc.fma_AvatarURL);

    FS3EMastodonAccount_Free(&tmpAcc);

    printf("net: VERIFY_ACCOUNT done, acct=%s\n", reply->fs3eva_Account.fma_Acct ? reply->fs3eva_Account.fma_Acct : "?");
    FreeVec(fs3em->fs3em_Data);
    fs3em->fs3em_Data    = reply;
    fs3em->fs3em_DataLen = total;
    fs3em->fs3em_Result  = FS3ENETR_OK;
}

/* FS3ENETQ_FETCH_IMAGE — check disk cache, fetch on miss, reply with path. */
static void FS3ENet_HandleFetchImage(FS3ENetMessage *fs3em)
{
    FS3ENetFetchImageReq   *req = (FS3ENetFetchImageReq *)fs3em->fs3em_Data;
    FS3ENetFetchImageReply *reply;
    char localPath[FS3ECACHE_PATH_SIZE];
    char cachePath[FS3ECACHE_PATH_SIZE];
    BOOL isTemp = FALSE;
    ULONG total;
    char *p;

    if (!req || fs3em->fs3em_DataLen < sizeof(*req))
    {
        fs3em->fs3em_Result = FS3ENETR_PARSE_ERROR;
        return;
    }

    /* Deterministic path this URL would live at if kept -- computed
     * regardless of fs3enf_KeepOriginal so the caller always has a stable
     * name to derive the resized thumbnail's sibling filename from, even
     * on a run where the original itself only ever touches RAM:T. Ensure
     * the subdir exists too: when KeepOriginal is FALSE, FS3ECache_Store
     * (the thing that normally creates it) never runs, but the thumbnail
     * process still needs to write the resized sibling under this same
     * subdir a moment from now. */
    FS3ECache_ComputePath(req->fs3enf_Url, req->fs3enf_Subdir, cachePath, sizeof(cachePath));
    FS3ECache_EnsureSubdir(req->fs3enf_Subdir);

    if (!FS3ECache_Lookup(req->fs3enf_Url, req->fs3enf_Subdir, localPath, sizeof(localPath)))
    {
        FS3EHttpResponse resp;

        if (!FS3EHttp_Get(req->fs3enf_Url, NULL, &resp))
        {
            fs3em->fs3em_Result = FS3ENETR_HTTP_ERROR;
            return;
        }

        if (req->fs3enf_KeepOriginal)
        {
            if (!FS3ECache_Store(req->fs3enf_Url, req->fs3enf_Subdir,
                                 resp.fhr_Body, resp.fhr_BodyLen,
                                 localPath, sizeof(localPath)))
            {
                FS3EHttp_FreeResponse(&resp);
                fs3em->fs3em_Result = FS3ENETR_HTTP_ERROR;
                return;
            }
        }
        else
        {
            if (!FS3ECache_StoreRAM(req->fs3enf_Url, resp.fhr_Body, resp.fhr_BodyLen,
                                    localPath, sizeof(localPath)))
            {
                FS3EHttp_FreeResponse(&resp);
                fs3em->fs3em_Result = FS3ENETR_HTTP_ERROR;
                return;
            }
            isTemp = TRUE;
        }

        FS3EHttp_FreeResponse(&resp);
    }
    /* else: found on disk already -- necessarily under the persistent
     * cache (RAM:T downloads are deleted right after use, never looked
     * up), so isTemp stays FALSE regardless of this request's
     * KeepOriginal value. */

    total = sizeof(FS3ENetFetchImageReply)
          + FS3ENet_PackLen(localPath)
          + FS3ENet_PackLen(req->fs3enf_Key)
          + FS3ENet_PackLen(req->fs3enf_Subdir)
          + FS3ENet_PackLen(cachePath);
    reply = (FS3ENetFetchImageReply *)AllocVec(total, MEMF_ANY);
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

static void FS3ENet_HandleTimeline(FS3ENetMessage *fs3em)
{
    FS3ENetTimelineReq   *req = (FS3ENetTimelineReq *)fs3em->fs3em_Data;
    FS3ENetTimelineReply *reply;
    cJSON *json = NULL;
    cJSON *item;
    ULONG count = 0, total;
    char *p;
    char stripped[2048];

    if (!req || fs3em->fs3em_DataLen < sizeof(*req)) {
        printf("net: TIMELINE parse error\n");
        fs3em->fs3em_Result = FS3ENETR_PARSE_ERROR;
        return;
    }

    printf("net: TIMELINE viewMode=%lu timeline=%s\n",
           req->fs3et_ViewModeBit,
           req->fs3et_Timeline ? req->fs3et_Timeline : "NULL");

    if (!FS3EMastodon_GetTimeline(req->fs3et_ApiBaseUrl,
            req->fs3et_AccessToken,
            req->fs3et_Timeline, &json)) {
        printf("net: TIMELINE GetTimeline failed\n");
        fs3em->fs3em_Result = FS3ENETR_HTTP_ERROR;
        return;
    }

    /* Pass 1: count statuses and compute flat-block size. */
    total = sizeof(FS3ENetTimelineReply);
    cJSON_ArrayForEach(item, json) {
        /* For reblogs: content lives in item.reblog; author is item.reblog.account.
         * The booster is item.account.  For original posts reblog is null/absent. */
        const cJSON *reblog = cJSON_GetObjectItemCaseSensitive(item, "reblog");
        const cJSON *src    = (reblog && !cJSON_IsNull(reblog)) ? reblog : item;
        const cJSON *acct   = cJSON_GetObjectItemCaseSensitive(src,  "account");
        const cJSON *bAcct  = cJSON_GetObjectItemCaseSensitive(item, "account");
        const cJSON *v;

        if (count >= MAX_STATUSES_TIMELINE) break;
        total += sizeof(FS3ENetStatus);

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

        /* booster display_name (empty string for non-reblogs) */
        if (src != item) {
            v = bAcct ? cJSON_GetObjectItemCaseSensitive(bAcct, "display_name") : NULL;
            total += (v && cJSON_IsString(v) && v->valuestring) ? strlen(v->valuestring) + 1 : 1;
        } else {
            total += 1; /* empty string */
        }

        /* media_attachments belongs to the actual content (src, i.e. the
         * reblogged status for boosts), same as "content" above. */
        v = cJSON_GetObjectItemCaseSensitive(src, "media_attachments");
        {
            int mCount = (v && cJSON_IsArray(v)) ? cJSON_GetArraySize(v) : 0;
            int mi;
            if (mCount > FS3ENET_MAX_MEDIA) mCount = FS3ENET_MAX_MEDIA;
            for (mi = 0; mi < mCount; mi++) {
                const cJSON *att  = cJSON_GetArrayItem(v, mi);
                const cJSON *purl = att ? cJSON_GetObjectItemCaseSensitive(att, "preview_url") : NULL;
                if (!purl || !cJSON_IsString(purl) || !purl->valuestring)
                    purl = att ? cJSON_GetObjectItemCaseSensitive(att, "url") : NULL;
                total += (purl && cJSON_IsString(purl) && purl->valuestring)
                       ? strlen(purl->valuestring) + 1 : 1;
            }
        }

        count++;
    }

    reply = (FS3ENetTimelineReply *)AllocVec(total, MEMF_ANY);
    if (!reply) {
        cJSON_Delete(json);
        fs3em->fs3em_Result = FS3ENETR_NETWORK_ERROR;
        return;
    }
    reply->fs3et_ViewModeBit = req->fs3et_ViewModeBit;
    reply->fs3et_Count       = count;

    /* Pass 2: pack strings into the block. */
    {
        FS3ENetStatus *statuses = (FS3ENetStatus *)(reply + 1);
        ULONG i = 0;
        p = (char *)(statuses + count);

        cJSON_ArrayForEach(item, json) {
            const cJSON *reblog, *src, *acct, *bAcct, *v;
            const char *str;
            if (i >= count) break;

            reblog = cJSON_GetObjectItemCaseSensitive(item, "reblog");
            src    = (reblog && !cJSON_IsNull(reblog)) ? reblog : item;
            acct   = cJSON_GetObjectItemCaseSensitive(src,  "account");
            bAcct  = cJSON_GetObjectItemCaseSensitive(item, "account");

            v = acct ? cJSON_GetObjectItemCaseSensitive(acct, "display_name") : NULL;
            str = (v && cJSON_IsString(v)) ? v->valuestring : "";
            FS3ENet_PackStr(&statuses[i].fmas_DisplayName, &p, str);

            v = acct ? cJSON_GetObjectItemCaseSensitive(acct, "acct") : NULL;
            str = (v && cJSON_IsString(v)) ? v->valuestring : "";
            FS3ENet_PackStr(&statuses[i].fmas_Acct, &p, str);

            v = cJSON_GetObjectItemCaseSensitive(src, "content");
            str = (v && cJSON_IsString(v)) ? v->valuestring : "";
            StripHTML(str, stripped, sizeof(stripped));
            FS3ENet_PackStr(&statuses[i].fmas_Content, &p, stripped);

            v = cJSON_GetObjectItemCaseSensitive(item, "created_at");
            str = (v && cJSON_IsString(v)) ? v->valuestring : "";
            FS3ENet_PackStr(&statuses[i].fmas_CreatedAt, &p, str);

            v = acct ? cJSON_GetObjectItemCaseSensitive(acct, "avatar") : NULL;
            str = (v && cJSON_IsString(v)) ? v->valuestring : "";
            FS3ENet_PackStr(&statuses[i].fmas_AvatarURL, &p, str);

            v = cJSON_GetObjectItemCaseSensitive(item, "id");
            str = (v && cJSON_IsString(v)) ? v->valuestring : "";
            FS3ENet_PackStr(&statuses[i].fmas_Id, &p, str);

            if (src != item) {
                v = bAcct ? cJSON_GetObjectItemCaseSensitive(bAcct, "display_name") : NULL;
                str = (v && cJSON_IsString(v)) ? v->valuestring : "";
            } else {
                str = "";
            }
            FS3ENet_PackStr(&statuses[i].fmas_BoostBy, &p, str);

            /* media_attachments -- see the matching block in pass 1. */
            v = cJSON_GetObjectItemCaseSensitive(src, "media_attachments");
            {
                int mCount = (v && cJSON_IsArray(v)) ? cJSON_GetArraySize(v) : 0;
                int mi;
                if (mCount > FS3ENET_MAX_MEDIA) mCount = FS3ENET_MAX_MEDIA;
                for (mi = 0; mi < mCount; mi++) {
                    const cJSON *att  = cJSON_GetArrayItem(v, mi);
                    const cJSON *purl = att ? cJSON_GetObjectItemCaseSensitive(att, "preview_url") : NULL;
                    if (!purl || !cJSON_IsString(purl) || !purl->valuestring)
                        purl = att ? cJSON_GetObjectItemCaseSensitive(att, "url") : NULL;
                    str = (purl && cJSON_IsString(purl)) ? purl->valuestring : "";
                    FS3ENet_PackStr(&statuses[i].fmas_MediaUrls[mi], &p, str);
                }
                for (; mi < FS3ENET_MAX_MEDIA; mi++)
                    statuses[i].fmas_MediaUrls[mi] = NULL;
                statuses[i].fmas_MediaCount = (ULONG)mCount;
            }

            i++;
        }
    }

    cJSON_Delete(json);
    FreeVec(fs3em->fs3em_Data);
    fs3em->fs3em_Data    = reply;
    fs3em->fs3em_DataLen = total;
    fs3em->fs3em_Result  = FS3ENETR_OK;
    printf("net: TIMELINE done, count=%lu viewMode=%lu\n",
           (unsigned long)count, (unsigned long)reply->fs3et_ViewModeBit);
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
        printf("net: POST_STATUS parse error\n");
        fs3em->fs3em_Result = FS3ENETR_PARSE_ERROR;
        return;
    }

    printf("net: POST_STATUS visibility=%s\n",
           req->fs3ep_Visibility ? req->fs3ep_Visibility : "public");

    if (!FS3EMastodon_PostStatus(req->fs3ep_ApiBaseUrl, req->fs3ep_AccessToken,
            req->fs3ep_Content, req->fs3ep_Visibility,
            statusId, sizeof(statusId)))
    {
        printf("net: POST_STATUS PostStatus failed\n");
        fs3em->fs3em_Result = FS3ENETR_HTTP_ERROR;
        return;
    }

    total = sizeof(FS3ENetPostStatusReply) + FS3ENet_PackLen(statusId);
    reply = (FS3ENetPostStatusReply *)AllocVec(total, MEMF_ANY);
    if (!reply) { fs3em->fs3em_Result = FS3ENETR_NETWORK_ERROR; return; }

    p = (char *)reply + sizeof(*reply);
    FS3ENet_PackStr(&reply->fs3ep_StatusId, &p, statusId);

    FreeVec(fs3em->fs3em_Data);
    fs3em->fs3em_Data    = reply;
    fs3em->fs3em_DataLen = total;
    fs3em->fs3em_Result  = FS3ENETR_OK;
    printf("net: POST_STATUS done, statusId=%s\n", statusId);
}

/* FS3ENETQ_FLUSH_CACHE — delete every file in the disk cache directory. */
static void FS3ENet_HandleFlushCache(FS3ENetMessage *fs3em)
{
    fs3em->fs3em_Result = FS3ECache_Flush() ? FS3ENETR_OK : FS3ENETR_NETWORK_ERROR;
}

/* Handles one request. FS3ENETQ_TIMELINE and FS3ENETQ_POST_STATUS are
 * stubbed until Phase 2 completes the timeline/post flow. */
static void FS3ENet_Dispatch(FS3ENetMessage *fs3em)
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
            FS3ENet_HandleFetchImage(fs3em);
            break;

        case FS3ENETQ_FLUSH_CACHE:
            FS3ENet_HandleFlushCache(fs3em);
            break;

        case FS3ENETQ_TIMELINE:
            FS3ENet_HandleTimeline(fs3em);
            break;

        case FS3ENETQ_POST_STATUS:
            FS3ENet_HandlePostStatus(fs3em);
            break;

        case FS3ENETQ_VERIFY_ACCOUNT:
            FS3ENet_HandleVerifyAccount(fs3em);
            break;

        default:
            fs3em->fs3em_Result = FS3ENETR_OK;
            break;
    }
}
