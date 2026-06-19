/*
 * FriendSh3ep network process - startup, shutdown and request dispatch.
 *
 * See fs3enet.h for the public API and ../ARCHITECTURE.md for the design.
 */

#include "fs3enet.h"
#include "fs3enet_http.h"
#include "fs3enet_mastodon.h"

#include <dos/dos.h>
#include <dos/dostags.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/dos.h>

#define FS3ENET_STACK_SIZE 16384
#define FS3ENET_PROC_NAME  "FriendSh3ep-net"

/* App name FriendSh3ep registers itself under via FS3EMastodon_CreateApp(). */
#define FS3ENET_CLIENT_NAME "AmigaOS3 FriendSh3ep Beta"

/* Handshake message used once at startup so FS3ENet_Start() can hand the
 * new process' request port back to the caller. Lives on FS3ENet_Start()'s
 * stack; FS3ENet_Start() stays blocked in WaitPort() until the child has
 * filled it in and replied, so its lifetime is safe. */
struct FS3ENetStartup
{
    struct Message  fs3ess_Msg;
    struct MsgPort  *fs3ess_RequestPort;
};

/* The OS3 NDK has no NP_UserData tag for CreateNewProc(), so the startup
 * struct is handed to the child via this global instead. FS3ENet_Start()
 * never runs concurrently with itself (single network process), and the
 * child reads g_FS3ENetStartup before FS3ENet_Start() could be called again. */
static struct FS3ENetStartup *g_FS3ENetStartup;

static void FS3ENet_ProcEntry(void);
static void FS3ENet_Dispatch(FS3ENetMessage *fs3em);

struct MsgPort *FS3ENet_Start(void)
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

    FS3EHttp_Cleanup();
    DeleteMsgPort(requestPort);
}

/* FS3ENETQ_LOGIN_START - register the app and build the authorize URL. */
static void FS3ENet_HandleLoginStart(FS3ENetMessage *fs3em)
{
    FS3ENetLoginStartReq   *req = (FS3ENetLoginStartReq *)fs3em->fs3em_Data;
    FS3ENetLoginStartReply *reply;

    if (!req || fs3em->fs3em_DataLen < sizeof(*req))
    {
        fs3em->fs3em_Result = FS3ENETR_PARSE_ERROR;
        return;
    }

    reply = AllocVec(sizeof(*reply), MEMF_ANY | MEMF_CLEAR);
    if (!reply)
    {
        fs3em->fs3em_Result = FS3ENETR_NETWORK_ERROR;
        return;
    }

    if (!FS3EMastodon_CreateApp(req->fs3enl_ApiBaseUrl, FS3ENET_CLIENT_NAME,
            reply->fs3enl_ClientId, sizeof(reply->fs3enl_ClientId),
            reply->fs3enl_ClientSecret, sizeof(reply->fs3enl_ClientSecret)))
    {
        FreeVec(reply);
        fs3em->fs3em_Result = FS3ENETR_HTTP_ERROR;
        return;
    }

    FS3EMastodon_BuildAuthorizeURL(req->fs3enl_ApiBaseUrl, reply->fs3enl_ClientId,
        reply->fs3enl_AuthorizeUrl, sizeof(reply->fs3enl_AuthorizeUrl));

    FreeVec(fs3em->fs3em_Data);
    fs3em->fs3em_Data    = reply;
    fs3em->fs3em_DataLen = sizeof(*reply);
    fs3em->fs3em_Result  = FS3ENETR_OK;
}

/* FS3ENETQ_LOGIN_FINISH - exchange the OOB code for an access token and
 * verify it. */
static void FS3ENet_HandleLoginFinish(FS3ENetMessage *fs3em)
{
    FS3ENetLoginFinishReq   *req = (FS3ENetLoginFinishReq *)fs3em->fs3em_Data;
    FS3ENetLoginFinishReply *reply;

    if (!req || fs3em->fs3em_DataLen < sizeof(*req))
    {
        fs3em->fs3em_Result = FS3ENETR_PARSE_ERROR;
        return;
    }

    reply = AllocVec(sizeof(*reply), MEMF_ANY | MEMF_CLEAR);
    if (!reply)
    {
        fs3em->fs3em_Result = FS3ENETR_NETWORK_ERROR;
        return;
    }

    if (!FS3EMastodon_ExchangeCode(req->fs3enl_ApiBaseUrl, req->fs3enl_ClientId,
            req->fs3enl_ClientSecret, req->fs3enl_Code,
            reply->fs3enl_AccessToken, sizeof(reply->fs3enl_AccessToken)))
    {
        FreeVec(reply);
        fs3em->fs3em_Result = FS3ENETR_AUTH_ERROR;
        return;
    }

    if (!FS3EMastodon_VerifyCredentials(req->fs3enl_ApiBaseUrl, reply->fs3enl_AccessToken,
            &reply->fs3enl_Account))
    {
        FreeVec(reply);
        fs3em->fs3em_Result = FS3ENETR_AUTH_ERROR;
        return;
    }

    FreeVec(fs3em->fs3em_Data);
    fs3em->fs3em_Data    = reply;
    fs3em->fs3em_DataLen = sizeof(*reply);
    fs3em->fs3em_Result  = FS3ENETR_OK;
}

/* Handles one request. Phase 2 will fill in FS3ENETQ_TIMELINE,
 * FS3ENETQ_POST_STATUS and FS3ENETQ_FETCH_IMAGE; for now those are a no-op
 * success. */
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

        case FS3ENETQ_TIMELINE:
        case FS3ENETQ_POST_STATUS:
        case FS3ENETQ_FETCH_IMAGE:
        default:
            /* TODO: dispatch to fs3enet_mastodon.c / fs3enet_http.c (Phase 2) */
            fs3em->fs3em_Result = FS3ENETR_OK;
            break;
    }
}
