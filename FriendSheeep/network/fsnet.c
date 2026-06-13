/*
 * FriendSheeep network process - startup, shutdown and request dispatch.
 *
 * See fsnet.h for the public API and ../ARCHITECTURE.md for the design.
 */

#include "fsnet.h"

#include <dos/dos.h>
#include <dos/dostags.h>
#include <proto/exec.h>
#include <proto/dos.h>

#define FSNET_STACK_SIZE 16384
#define FSNET_PROC_NAME  "FriendSheeep-net"

/* Handshake message used once at startup so FSNet_Start() can hand the
 * new process' request port back to the caller. Lives on FSNet_Start()'s
 * stack; FSNet_Start() stays blocked in WaitPort() until the child has
 * filled it in and replied, so its lifetime is safe. */
struct FSNetStartup
{
    struct Message  fss_Msg;
    struct MsgPort  *fss_RequestPort;
};

/* The OS3 NDK has no NP_UserData tag for CreateNewProc(), so the startup
 * struct is handed to the child via this global instead. FSNet_Start()
 * never runs concurrently with itself (single network process), and the
 * child reads g_FSNetStartup before FSNet_Start() could be called again. */
static struct FSNetStartup *g_FSNetStartup;

static void FSNet_ProcEntry(void);
static void FSNet_Dispatch(FSNetMessage *fsm);

struct MsgPort *FSNet_Start(void)
{
    struct MsgPort     *replyPort;
    struct FSNetStartup startup;
    struct Process     *proc;

    replyPort = CreateMsgPort();
    if (!replyPort)
        return NULL;

    startup.fss_Msg.mn_ReplyPort = replyPort;
    startup.fss_Msg.mn_Length    = sizeof(startup);
    startup.fss_RequestPort      = NULL;

    g_FSNetStartup = &startup;

    proc = CreateNewProcTags(
        NP_Entry,     (ULONG)FSNet_ProcEntry,
        NP_Name,      (ULONG)FSNET_PROC_NAME,
        NP_StackSize, (ULONG)FSNET_STACK_SIZE,
        TAG_DONE);

    if (!proc)
    {
        DeleteMsgPort(replyPort);
        return NULL;
    }

    WaitPort(replyPort);
    GetMsg(replyPort);
    DeleteMsgPort(replyPort);

    return startup.fss_RequestPort;
}

void FSNet_Stop(struct MsgPort *requestPort, struct MsgPort *replyPort)
{
    FSNetMessage msg;

    if (!requestPort)
        return;

    msg.fsm_Msg.mn_ReplyPort = replyPort;
    msg.fsm_Msg.mn_Length    = sizeof(msg);
    msg.fsm_Type             = FSNETQ_SHUTDOWN;
    msg.fsm_Data             = NULL;
    msg.fsm_DataLen          = 0;

    PutMsg(requestPort, (struct Message *)&msg);

    WaitPort(replyPort);
    GetMsg(replyPort);
}

/* Entry point of the network process, running as its own AmigaDOS task. */
static void FSNet_ProcEntry(void)
{
    struct FSNetStartup *startup = g_FSNetStartup;
    struct MsgPort       *requestPort;
    BOOL                  running = TRUE;

    requestPort = CreateMsgPort();

    /* Hand the request port (or NULL on failure) back to FSNet_Start(). */
    startup->fss_RequestPort = requestPort;
    PutMsg(startup->fss_Msg.mn_ReplyPort, &startup->fss_Msg);

    if (!requestPort)
        return;

    while (running)
    {
        FSNetMessage *fsm;

        WaitPort(requestPort);
        while ((fsm = (FSNetMessage *)GetMsg(requestPort)) != NULL)
        {
            if (fsm->fsm_Type == FSNETQ_SHUTDOWN)
                running = FALSE;
            else
                FSNet_Dispatch(fsm);

            fsm->fsm_Result = FSNETR_OK;
            ReplyMsg((struct Message *)fsm);
        }
    }

    DeleteMsgPort(requestPort);
}

/* Handles one request. Phase 1/2 will fill in fsnet_mastodon.c calls per
 * FSNetRequestType; for now every request is a no-op success. */
static void FSNet_Dispatch(FSNetMessage *fsm)
{
    switch (fsm->fsm_Type)
    {
        case FSNETQ_LOGIN_START:
        case FSNETQ_LOGIN_FINISH:
        case FSNETQ_TIMELINE:
        case FSNETQ_POST_STATUS:
        case FSNETQ_FETCH_IMAGE:
        default:
            /* TODO: dispatch to fsnet_mastodon.c / fsnet_http.c (Phase 1/2) */
            break;
    }
}
