/*
 * fs3ethumb.c - FriendSh3ep thumbnail process.
 *
 * $VER: fs3ethumb.c 1.0 (07.07.2026)
 * Copyright (C) 2026 FriendSh3ep contributors. All rights reserved.
 *
 * See fs3ethumb.h for the request/reply protocol and design rationale.
 */

#include "fs3ethumb.h"
#include "bmimage.h"

#include <exec/types.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <string.h>
#include <stdio.h>

#include "compilers.h"
#include "bdbprintf.h"

#define FS3ETHUMB_PROC_NAME  "FriendSh3ep_thumbnail"
#define FS3ETHUMB_STACK_SIZE 32768

/*
 * Handshake message used once at startup -- same trick as FS3ENetStartup in
 * network_fs3e/fs3enet.c: the OS3 NDK has no NP_UserData tag for
 * CreateNewProc(), so the request port is handed back via a global that the
 * child reads before replying. FS3EThumb_Start() stays blocked in
 * WaitPort() until then, so the stack-allocated startup struct's lifetime
 * is safe, and FS3EThumb_Start() never runs concurrently with itself
 * (single thumbnail process).
 */
struct FS3EThumbStartup
{
    struct Message  fs3ets_Msg;
    struct MsgPort *fs3ets_RequestPort;
};

static struct FS3EThumbStartup *g_FS3EThumbStartup;

/* Set once by FS3EThumb_ProcEntry right after CreateMsgPort() succeeds, so
 * FS3EThumb_Stop() can Signal() the process directly -- see
 * g_FS3EThumbStopSigBit and the "stopping" fast-path in
 * FS3EThumb_ProcEntry's loop below. Decoding+rescaling a queued image is
 * the slow part of this whole app's background work, so a backlog of
 * several pending FS3ETHUMBQ_MAKE requests is exactly the case that made
 * shutdown wait out the full FIFO queue before this existed. */
static struct Task *g_FS3EThumbTask = NULL;

/* Private signal bit for the shutdown fast-path -- see the matching
 * g_FS3ENetStopSigBit comment in network_fs3e/fs3enet.c for why this is
 * deliberately NOT SIGBREAKF_CTRL_C (libnix's chkabort() polls/consumes
 * that bit for its own Ctrl-C handling, which raced with reusing it here). */
static LONG g_FS3EThumbStopSigBit = -1;
#define FS3ETHUMB_STOP_SIGMASK  ((g_FS3EThumbStopSigBit >= 0) ? (1UL << g_FS3EThumbStopSigBit) : 0UL)

static void FS3EThumb_ProcEntry(void);
static void FS3EThumb_HandleMake(FS3EThumbMessage *msg);

struct MsgPort *FS3EThumb_Start(void)
{
    struct MsgPort          *replyPort;
    struct FS3EThumbStartup  startup;
    struct Process          *proc;

    replyPort = CreateMsgPort();
    if (!replyPort)
        return NULL;

    startup.fs3ets_Msg.mn_ReplyPort = replyPort;
    startup.fs3ets_Msg.mn_Length    = sizeof(startup);
    startup.fs3ets_RequestPort      = NULL;

    g_FS3EThumbStartup = &startup;

    proc = CreateNewProcTags(
        NP_Entry,     (ULONG)FS3EThumb_ProcEntry,
        NP_Name,      (ULONG)FS3ETHUMB_PROC_NAME,
        NP_StackSize, (ULONG)FS3ETHUMB_STACK_SIZE,
        TAG_DONE);

    if (!proc)
    {
        DeleteMsgPort(replyPort);
        return NULL;
    }

    WaitPort(replyPort);
    GetMsg(replyPort);
    DeleteMsgPort(replyPort);

    return startup.fs3ets_RequestPort;
}

void FS3EThumb_Stop(struct MsgPort *requestPort, struct MsgPort *replyPort)
{
    FS3EThumbMessage msg;

    if (!requestPort)
        return;

    /* Signal first -- see the g_FS3EThumbTask comment and the "stopping"
     * fast-path in FS3EThumb_ProcEntry: lets the process abandon (with an
     * immediate error reply, not silently) whatever's still queued behind
     * whatever single image it's currently mid-decode/scale on, instead of
     * grinding through the entire backlog before it even looks at the
     * shutdown message sitting at the back of the queue. */
    if (g_FS3EThumbTask && FS3ETHUMB_STOP_SIGMASK) Signal(g_FS3EThumbTask, FS3ETHUMB_STOP_SIGMASK);

    memset(&msg, 0, sizeof(msg));
    msg.fs3etm_Msg.mn_ReplyPort = replyPort;
    msg.fs3etm_Msg.mn_Length    = sizeof(msg);
    msg.fs3etm_Type             = FS3ETHUMBQ_SHUTDOWN;

    PutMsg(requestPort, (struct Message *)&msg);

    WaitPort(replyPort);
    GetMsg(replyPort);
}

BOOL FS3EThumb_Request(struct MsgPort *requestPort, struct MsgPort *replyPort,
                        const char *srcPath, const char *key, ULONG kind,
                        const char *cacheKeyPath, BOOL deleteSrcAfter,
                        UWORD targetW, UWORD targetH)
{
    FS3EThumbMessage *msg;

    if (!requestPort || !replyPort || !srcPath || !srcPath[0])
        return FALSE;
    if (strlen(srcPath) >= FS3ETHUMB_PATH_SIZE)
        return FALSE;
    if (key && strlen(key) >= FS3ETHUMB_KEY_SIZE)
        return FALSE;
    if (cacheKeyPath && strlen(cacheKeyPath) >= FS3ETHUMB_PATH_SIZE)
        return FALSE;

    msg = (FS3EThumbMessage *)AllocVec(sizeof(FS3EThumbMessage), MEMF_CLEAR | MEMF_PUBLIC);
    if (!msg)
        return FALSE;

    msg->fs3etm_Msg.mn_ReplyPort = replyPort;
    msg->fs3etm_Msg.mn_Length    = sizeof(*msg);
    msg->fs3etm_Type             = FS3ETHUMBQ_MAKE;
    strcpy(msg->fs3etm_SrcPath, srcPath);
    strcpy(msg->fs3etm_Key, key ? key : "");
    msg->fs3etm_Kind    = kind;
    msg->fs3etm_TargetW = targetW;
    msg->fs3etm_TargetH = targetH;
    strcpy(msg->fs3etm_CacheKeyPath, cacheKeyPath ? cacheKeyPath : "");
    msg->fs3etm_DeleteSrcAfter = deleteSrcAfter;

    PutMsg(requestPort, (struct Message *)&msg->fs3etm_Msg);
    return TRUE;
}

/* Debug: human-readable name for a sniffed/detected BmImageFormat, purely
 * for bdbprintf_now() tracing below -- not the format-guessing logic
 * itself (see BmImage_SniffFormat in bmimage.c for that). */
static const char *FS3EThumb_FormatName(BmImageFormat fmt)
{
    switch (fmt) {
        case BMFMT_PNG:  return "png";
        case BMFMT_JPEG: return "jpeg";
        case BMFMT_GIF:  return "gif";
        case BMFMT_WEBP: return "webp";
        case BMFMT_BMP:  return "bmp";
        default:         return "unknown";
    }
}

/* FS3ETHUMBQ_MAKE - decode + box-fit-scale one image to a BMP thumbnail. */
static void FS3EThumb_HandleMake(FS3EThumbMessage *msg)
{
    BmImageError err = BMIMAGE_OK;
    const char *keyPath = msg->fs3etm_CacheKeyPath[0] ? msg->fs3etm_CacheKeyPath : NULL;

    msg->fs3etm_DetectedFormat = (ULONG)BMFMT_UNKNOWN;

    /* Sniffed proactively here (not just on BMIMAGE_ERR_OPEN_FAILED below)
     * purely so the trace line below can show it -- picture.datatype does
     * its own, separate format detection once BmImage_GenerateScaledBmp
     * actually opens the file. */
    bdbprintf_now("FS3EThumb: entering MAKE key=%s src=%s kind=%s fmt=%s target=%lux%lu\n",
                  msg->fs3etm_Key[0] ? msg->fs3etm_Key : "?",
                  msg->fs3etm_SrcPath,
                  (msg->fs3etm_Kind == FS3ETHUMB_KIND_MEDIA) ? "media" : "avatar",
                  FS3EThumb_FormatName(BmImage_SniffFormat(msg->fs3etm_SrcPath)),
                  (unsigned long)msg->fs3etm_TargetW, (unsigned long)msg->fs3etm_TargetH);

    if (BmImage_GenerateScaledBmp(msg->fs3etm_SrcPath, keyPath,
            msg->fs3etm_TargetW, msg->fs3etm_TargetH,
            msg->fs3etm_ThumbPath, sizeof(msg->fs3etm_ThumbPath), &err))
    {
        msg->fs3etm_Result = FS3ETHUMBR_OK;
        bdbprintf_now("FS3EThumb: MAKE done key=%s -> OK thumb=%s\n",
                      msg->fs3etm_Key[0] ? msg->fs3etm_Key : "?", msg->fs3etm_ThumbPath);
    }
    else
    {
        /* Only NewDTObject-couldn't-even-open-it is a "what format is
         * this really" question -- NO_MEMORY/NO_BITMAP/WRITE_FAILED are
         * different failure classes, sniffing wouldn't explain those. */
        if (err == BMIMAGE_ERR_OPEN_FAILED)
            msg->fs3etm_DetectedFormat = (ULONG)BmImage_SniffFormat(msg->fs3etm_SrcPath);
        msg->fs3etm_ThumbPath[0] = '\0';
        msg->fs3etm_Result = FS3ETHUMBR_ERROR;
        bdbprintf_now("FS3EThumb: MAKE done key=%s -> ERROR err=%ld fmt=%s\n",
                      msg->fs3etm_Key[0] ? msg->fs3etm_Key : "?", (long)err,
                      FS3EThumb_FormatName((BmImageFormat)msg->fs3etm_DetectedFormat));
    }

    /* Clean up the RAM:T download regardless of success/failure -- see
     * fs3etm_DeleteSrcAfter's doc comment. */
    if (msg->fs3etm_DeleteSrcAfter && msg->fs3etm_SrcPath[0])
        DeleteFile((STRPTR)msg->fs3etm_SrcPath);
}

/* Debug: peek how many requests are queued on requestPort without removing
 * any of them -- see the matching FS3ENet_CountPending comment in
 * network_fs3e/fs3enet.c for why Disable()/Enable() brackets the walk.
 * Only used for the bdbprintf_now() backlog tracing in
 * FS3EThumb_ProcEntry below. */
static ULONG FS3EThumb_CountPending(struct MsgPort *port)
{
    struct Node *n;
    ULONG        count = 0;

    Disable();
    for (n = port->mp_MsgList.lh_Head; n->ln_Succ; n = n->ln_Succ)
        count++;
    Enable();

    return count;
}

/* Entry point of the thumbnail process, running as its own AmigaDOS task. */
static void FS3EThumb_ProcEntry(void)
{
    struct FS3EThumbStartup *startup = g_FS3EThumbStartup;
    struct MsgPort           *requestPort;
    FS3EThumbMessage         *shutdownMsg = NULL;
    BOOL                      running  = TRUE;
    BOOL                      stopping = FALSE;

    requestPort = CreateMsgPort();
    g_FS3EThumbTask = FindTask(NULL);
    g_FS3EThumbStopSigBit = AllocSignal(-1);

    /* Hand the request port (or NULL on failure) back to FS3EThumb_Start(). */
    startup->fs3ets_RequestPort = requestPort;
    PutMsg(startup->fs3ets_Msg.mn_ReplyPort, &startup->fs3ets_Msg);

    if (!requestPort)
        return;

    while (running)
    {
        FS3EThumbMessage *msg;

        WaitPort(requestPort);

        bdbprintf_now("FS3EThumb: woke up, %lu request(s) waiting\n",
                      (unsigned long)FS3EThumb_CountPending(requestPort));

        if (!stopping && FS3ETHUMB_STOP_SIGMASK &&
            (SetSignal(0, FS3ETHUMB_STOP_SIGMASK) & FS3ETHUMB_STOP_SIGMASK))
            stopping = TRUE;

        while ((msg = (FS3EThumbMessage *)GetMsg(requestPort)) != NULL)
        {
            if (msg->fs3etm_Type == FS3ETHUMBQ_SHUTDOWN)
            {
                /* Hold this one instead of replying immediately -- see the
                 * matching comment in network_fs3e/fs3enet.c's
                 * FS3ENet_ProcEntry: this task shares its loaded code image
                 * with the main process (no separate address space), so
                 * replying now would let the main process race ahead toward
                 * tearing that image down while this task still had cleanup
                 * left to run. */
                running = FALSE;
                msg->fs3etm_Result = FS3ETHUMBR_OK;
                shutdownMsg = msg;
                continue;
            }
            else if (stopping && msg->fs3etm_Type == FS3ETHUMBQ_MAKE)
            {
                /* Abandoned without decoding/scaling -- still clean up the
                 * RAM:T download this request owns (FS3EThumb_HandleMake is
                 * skipped, so its own cleanup never runs), or a burst of
                 * requests right at shutdown would each leak their file --
                 * exactly the disk-space problem "Keep big ..." off is meant
                 * to avoid. */
                if (msg->fs3etm_DeleteSrcAfter && msg->fs3etm_SrcPath[0])
                    DeleteFile((STRPTR)msg->fs3etm_SrcPath);
                msg->fs3etm_ThumbPath[0] = '\0';
                msg->fs3etm_Result = FS3ETHUMBR_ERROR;
            }
            else if (msg->fs3etm_Type == FS3ETHUMBQ_MAKE)
            {
                FS3EThumb_HandleMake(msg);
                if (!stopping && FS3ETHUMB_STOP_SIGMASK &&
                    (SetSignal(0, FS3ETHUMB_STOP_SIGMASK) & FS3ETHUMB_STOP_SIGMASK))
                    stopping = TRUE;
            }

            ReplyMsg((struct Message *)msg);
        }
    }

    DeleteMsgPort(requestPort);
    if (g_FS3EThumbStopSigBit >= 0) { FreeSignal(g_FS3EThumbStopSigBit); g_FS3EThumbStopSigBit = -1; }
    g_FS3EThumbTask = NULL;

    if (shutdownMsg)
        ReplyMsg((struct Message *)shutdownMsg);
}
