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

FS3ENetFetchImageReq *FS3ENetFetchImageReq_Alloc(const char *url)
{
    ULONG total = sizeof(FS3ENetFetchImageReq) + FS3ENet_PackLen(url);
    FS3ENetFetchImageReq *req =
        (FS3ENetFetchImageReq *)AllocVec(total, MEMF_ANY);
    char *p;

    if (!req) return NULL;
    p = (char *)req + sizeof(*req);
    FS3ENet_PackStr(&req->fs3enf_Url, &p, url);
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

    msg.fs3em_Msg.mn_ReplyPort = replyPort;
    msg.fs3em_Msg.mn_Length    = sizeof(msg);
    msg.fs3em_Type             = FS3ENETQ_SHUTDOWN;
    msg.fs3em_Data             = NULL;
    msg.fs3em_DataLen          = 0;

    PutMsg(requestPort, (struct Message *)&msg);

    WaitPort(replyPort);
    GetMsg(replyPort);
}

BOOL FS3ENet_FlushCache(struct MsgPort *requestPort, struct MsgPort *replyPort)
{
    FS3ENetMessage msg;

    if (!requestPort)
        return FALSE;

    msg.fs3em_Msg.mn_ReplyPort = replyPort;
    msg.fs3em_Msg.mn_Length    = sizeof(msg);
    msg.fs3em_Type             = FS3ENETQ_FLUSH_CACHE;
    msg.fs3em_Data             = NULL;
    msg.fs3em_DataLen          = 0;

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
    BOOL                  running = TRUE;

    requestPort = CreateMsgPort();

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
        while ((fs3em = (FS3ENetMessage *)GetMsg(requestPort)) != NULL)
        {
            if (fs3em->fs3em_Type == FS3ENETQ_SHUTDOWN)
            {
                running = FALSE;
                fs3em->fs3em_Result = FS3ENETR_OK;
            }
            else
            {
                FS3ENet_Dispatch(fs3em);
            }

            ReplyMsg((struct Message *)fs3em);
        }
    }

    FS3ECache_Cleanup();
    FS3EHttp_Cleanup();
    DeleteMsgPort(requestPort);
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
    ULONG total;
    char *p;

    if (!req || fs3em->fs3em_DataLen < sizeof(*req))
    {
        fs3em->fs3em_Result = FS3ENETR_PARSE_ERROR;
        return;
    }

    if (!FS3ECache_Lookup(req->fs3enf_Url, localPath, sizeof(localPath)))
    {
        FS3EHttpResponse resp;

        if (!FS3EHttp_Get(req->fs3enf_Url, NULL, &resp))
        {
            fs3em->fs3em_Result = FS3ENETR_HTTP_ERROR;
            return;
        }

        if (!FS3ECache_Store(req->fs3enf_Url, resp.fhr_Body, resp.fhr_BodyLen,
                             localPath, sizeof(localPath)))
        {
            FS3EHttp_FreeResponse(&resp);
            fs3em->fs3em_Result = FS3ENETR_HTTP_ERROR;
            return;
        }

        FS3EHttp_FreeResponse(&resp);
    }

    total = sizeof(FS3ENetFetchImageReply) + FS3ENet_PackLen(localPath);
    reply = (FS3ENetFetchImageReply *)AllocVec(total, MEMF_ANY);
    if (!reply)
    {
        fs3em->fs3em_Result = FS3ENETR_NETWORK_ERROR;
        return;
    }

    p = (char *)reply + sizeof(*reply);
    FS3ENet_PackStr(&reply->fs3enf_LocalPath, &p, localPath);

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
        case FS3ENETQ_POST_STATUS:
        default:
            /* TODO: Phase 2 */
            fs3em->fs3em_Result = FS3ENETR_OK;
            break;
    }
}
