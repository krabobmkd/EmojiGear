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

/* AllocVec'd-payload packing helpers, same trick as network_fs3e/fs3enet.c's
 * FS3ENet_PackLen/PackStr (local here rather than shared -- this is a
 * separate module with its own small payload structs, not worth reaching
 * across that boundary for). PackLen reserves strlen()+1 bytes (empty
 * string for NULL); PackStr copies into *p, points *dst at the copy, and
 * advances *p by the same reserved length. */
static ULONG FS3EThumb_PackLen(const char *s)
{
    return (ULONG)strlen(s ? s : "") + 1;
}

static void FS3EThumb_PackStr(char **dst, char **p, const char *s)
{
    ULONG len = FS3EThumb_PackLen(s);
    memcpy(*p, s ? s : "", len);
    *dst = *p;
    *p += len;
}

BOOL FS3EThumb_Request(struct MsgPort *requestPort, struct MsgPort *replyPort,
                        const char *srcPath, const char *key, ULONG kind,
                        const char *cacheKeyPath, BOOL deleteSrcAfter,
                        UWORD targetW, UWORD targetH)
{
    FS3EThumbMessage *msg;
    FS3EThumbMakeReq *req;
    ULONG             total;
    char             *p;

    if (!requestPort || !replyPort || !srcPath || !srcPath[0])
        return FALSE;

    msg = (FS3EThumbMessage *)AllocVec(sizeof(FS3EThumbMessage), MEMF_CLEAR | MEMF_PUBLIC);
    if (!msg)
        return FALSE;

    total = sizeof(FS3EThumbMakeReq)
          + FS3EThumb_PackLen(srcPath)
          + FS3EThumb_PackLen(key)
          + FS3EThumb_PackLen(cacheKeyPath);
    req = (FS3EThumbMakeReq *)AllocVec(total, MEMF_ANY | MEMF_PUBLIC);
    if (!req)
    {
        FreeVec(msg);
        return FALSE;
    }

    req->fs3etmr_Kind           = kind;
    req->fs3etmr_TargetW        = targetW;
    req->fs3etmr_TargetH        = targetH;
    req->fs3etmr_DeleteSrcAfter = deleteSrcAfter;

    p = (char *)req + sizeof(*req);
    FS3EThumb_PackStr(&req->fs3etmr_SrcPath,      &p, srcPath);
    FS3EThumb_PackStr(&req->fs3etmr_Key,          &p, key);
    FS3EThumb_PackStr(&req->fs3etmr_CacheKeyPath, &p, cacheKeyPath);

    msg->fs3etm_Msg.mn_ReplyPort = replyPort;
    msg->fs3etm_Msg.mn_Length    = sizeof(*msg);
    msg->fs3etm_Type             = FS3ETHUMBQ_MAKE;
    msg->fs3etm_Data             = req;
    msg->fs3etm_DataLen          = total;

    PutMsg(requestPort, (struct Message *)&msg->fs3etm_Msg);
    return TRUE;
}

void FS3EThumb_FreeMessage(FS3EThumbMessage *msg)
{
    if (!msg) return;
    FreeVec(msg->fs3etm_Data);
    FreeVec(msg);
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

/* Builds an FS3EThumbMakeReply for msg from kind/key/srcPath/thumbPath/
 * detectedFormat, freeing the original FS3EThumbMakeReq block first and
 * replacing fs3etm_Data -- same convention network_fs3e/fs3enet.c's
 * FS3ENet_BuildFetchImageReply() already follows for its own request/reply
 * swap. Shared by FS3EThumb_HandleMake()'s success/error paths and
 * FS3EThumb_ProcEntry's shutdown-time abandon path. If the reply
 * allocation itself fails, msg ends up with fs3etm_Data == NULL and
 * fs3etm_Result forced to FS3ETHUMBR_ERROR -- nothing left to hand back
 * but the error. */
static void FS3EThumb_BuildReply(FS3EThumbMessage *msg, ULONG kind, const char *key,
                                  const char *srcPath, const char *thumbPath,
                                  ULONG detectedFormat, ULONG result)
{
    FS3EThumbMakeReply *reply;
    ULONG                total;
    char                *p;

    total = sizeof(FS3EThumbMakeReply)
          + FS3EThumb_PackLen(key)
          + FS3EThumb_PackLen(srcPath)
          + FS3EThumb_PackLen(thumbPath);
    reply = (FS3EThumbMakeReply *)AllocVec(total, MEMF_ANY | MEMF_PUBLIC);

    if (!reply)
    {
        /* key/srcPath/thumbPath may point into msg->fs3etm_Data (both call
         * sites pass fields straight out of the FS3EThumbMakeReq this reply
         * replaces) -- free only after every read of them is done, never
         * before, or PackStr below would copy from already-freed memory. */
        FreeVec(msg->fs3etm_Data);
        msg->fs3etm_Data    = NULL;
        msg->fs3etm_DataLen = 0;
        msg->fs3etm_Result  = FS3ETHUMBR_ERROR;
        return;
    }

    reply->fs3etmy_Kind           = kind;
    reply->fs3etmy_DetectedFormat = detectedFormat;

    p = (char *)reply + sizeof(*reply);
    FS3EThumb_PackStr(&reply->fs3etmy_Key,       &p, key);
    FS3EThumb_PackStr(&reply->fs3etmy_SrcPath,   &p, srcPath);
    FS3EThumb_PackStr(&reply->fs3etmy_ThumbPath, &p, thumbPath);

    /* Safe to free the old data now -- everything needed from it (key/
     * srcPath, if they pointed into it) has already been copied above. */
    FreeVec(msg->fs3etm_Data);

    msg->fs3etm_Data    = reply;
    msg->fs3etm_DataLen = total;
    msg->fs3etm_Result  = result;
}

/* FS3ETHUMBQ_MAKE - decode + box-fit-scale one image to a BMP thumbnail. */
static void FS3EThumb_HandleMake(FS3EThumbMessage *msg)
{
    FS3EThumbMakeReq    *req      = (FS3EThumbMakeReq *)msg->fs3etm_Data;
    const char           *keyPath  = (req->fs3etmr_CacheKeyPath && req->fs3etmr_CacheKeyPath[0])
                                      ? req->fs3etmr_CacheKeyPath : NULL;
    const char           *namePath = keyPath ? keyPath : req->fs3etmr_SrcPath;
    BOOL                  deleteSrcAfter = req->fs3etmr_DeleteSrcAfter;
    BmImageError          err = BMIMAGE_OK;
    ULONG                 detectedFormat = (ULONG)BMFMT_UNKNOWN;
    char                 *thumbPathBuf;
    ULONG                 thumbBufSize;
    BOOL                  ok = FALSE;
    FS3EThumbMakeReply   *reply;

    /* Sized off namePath rather than a fixed cap: BmImage_GenerateScaledBmp
     * still fills a caller buffer (see bmimage.h) -- this is the boundary
     * where "no fixed path cap" meets that unchanged contract. +32 covers
     * the ".<W>x<H>.bmp" suffix it appends (max "65535x65535.bmp" is 16
     * chars) plus slack. */
    thumbBufSize = (ULONG)strlen(namePath) + 32;
    thumbPathBuf = (char *)AllocVec(thumbBufSize, MEMF_ANY);
    if (thumbPathBuf)
        ok = BmImage_GenerateScaledBmp(req->fs3etmr_SrcPath, keyPath,
                req->fs3etmr_TargetW, req->fs3etmr_TargetH,
                thumbPathBuf, thumbBufSize, &err);

    /* Only NewDTObject-couldn't-even-open-it is a "what format is this
     * really" question -- NO_MEMORY/NO_BITMAP/WRITE_FAILED are different
     * failure classes, sniffing wouldn't explain those. */
    if (!ok && err == BMIMAGE_ERR_OPEN_FAILED)
        detectedFormat = (ULONG)BmImage_SniffFormat(req->fs3etmr_SrcPath);

    FS3EThumb_BuildReply(msg, req->fs3etmr_Kind, req->fs3etmr_Key, req->fs3etmr_SrcPath,
                          ok ? thumbPathBuf : "", detectedFormat,
                          ok ? FS3ETHUMBR_OK : FS3ETHUMBR_ERROR);
    /* req/keyPath/namePath are dangling from here on -- FS3EThumb_BuildReply
     * just freed the request block. */

    FreeVec(thumbPathBuf);

    /* Clean up the RAM:T download regardless of success/failure -- see
     * fs3etmr_DeleteSrcAfter's doc comment. Read back from the reply's own
     * copy of SrcPath, not the now-freed request. */
    reply = (FS3EThumbMakeReply *)msg->fs3etm_Data;
    if (deleteSrcAfter && reply && reply->fs3etmy_SrcPath[0])
        DeleteFile((STRPTR)reply->fs3etmy_SrcPath);
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

        // bdbprintf_now("FS3EThumb: woke up, %lu request(s) waiting\n",
        //               (unsigned long)FS3EThumb_CountPending(requestPort));

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
                FS3EThumbMakeReq *req = (FS3EThumbMakeReq *)msg->fs3etm_Data;

                if (req->fs3etmr_DeleteSrcAfter && req->fs3etmr_SrcPath[0])
                    DeleteFile((STRPTR)req->fs3etmr_SrcPath);

                FS3EThumb_BuildReply(msg, req->fs3etmr_Kind, req->fs3etmr_Key,
                                      req->fs3etmr_SrcPath, "", (ULONG)BMFMT_UNKNOWN,
                                      FS3ETHUMBR_ERROR);
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
