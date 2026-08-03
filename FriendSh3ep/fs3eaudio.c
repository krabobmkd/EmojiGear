/*
 * fs3eaudio.c - FriendSh3ep MP3 audio playback process.
 *
 * $VER: fs3eaudio.c 1.0 (18.07.2026)
 * Copyright (C) 2026 FriendSh3ep contributors. All rights reserved.
 *
 * See fs3eaudio.h for the request/reply protocol and design rationale.
 * Core decode/AHI mechanics adapted from ukonxMyWorldOS3SRC/Ukonx_MyWorld's
 * AHISoundServer.c (double-buffer SendIO/WaitIO loop) and AHIMPEGA.c
 * (MPEGA_decode_frame -> interleaved stereo PCM), verified line-for-line
 * against that proven-working reference -- see the comments on
 * FS3EAudio_StreamOneBuffer() and FS3EAudio_DecodeInto() below for the one
 * intentional behavior change (MPEGA_decode_frame returning 0 samples for
 * a skipped frame is not end-of-stream, per its own doc comment; the
 * reference treated it as EOF).
 */

#include "fs3eaudio.h"

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/io.h>
#include <proto/exec.h>
#include <proto/alib.h>
#include <proto/dos.h>
#include <devices/ahi.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <string.h>

#include <libraries/mpega.h>
#include <clib/mpega_protos.h>
#include <inline/mpega.h>  /* see fs3eaudio.h: <proto/mpega.h> doesn't compile here */


#include "compilers.h"
#include "bdbprintf.h"

#define FS3EAUDIO_PROC_NAME  "FriendSh3ep_audio"
/* mpega layer III decode is float/DSP-heavy, same "needs more than the SDK
 * default" lesson as AmiSSL's own task stack requirement -- 8k-16k has been
 * seen to silently hang other CPU-heavy libraries on real hardware, so this
 * errs generous rather than risk the same class of bug here. */
#define FS3EAUDIO_STACK_SIZE 65536

#define FS3EAUDIO_BUFFERSIZE      (8 << 10)                  /* bytes */
#define FS3EAUDIO_BUFFER_SAMPLES  (FS3EAUDIO_BUFFERSIZE >> 2) /* stereo 16-bit sample-pairs */

struct Library *MPEGABase = NULL; /* MPEGA_BASE_NAME default, see inline/mpega.h */

/* ahi.device is a device, not ahi.library -- AHINAME ("ahi.device") is only
 * ever valid as an OpenDevice() argument, never OpenLibrary(). AHIBase is
 * filled in from the just-opened request's own io_Device once OpenDevice()
 * succeeds (see FS3EAudio_OpenAHI/FS3EAudio_CloseAHI below), not opened
 * separately -- same technique as exampleAHIBase.txt. Nothing in this file
 * currently calls an AHI_* library-style function through it (only plain
 * exec.library device I/O: OpenDevice/CreateExtIO/SendIO/WaitIO/
 * CloseDevice), but keeping it valid while the device is open costs
 * nothing and matches the documented pattern. */
struct Library *AHIBase = NULL;

/*
 * Handshake message used once at startup -- same trick as FS3ENetStartup/
 * FS3EThumbStartup: the OS3 NDK has no NP_UserData tag for CreateNewProc(),
 * so the request port is handed back via a global the child reads before
 * replying. FS3EAudio_Start() stays blocked in WaitPort() until then.
 */
struct FS3EAudioStartup
{
    struct Message  fs3eas_Msg;
    struct MsgPort *fs3eas_RequestPort;
};

static struct FS3EAudioStartup *g_FS3EAudioStartup;

/* -------------------------------------------------------------------------
 * Playback state -- one active stream at a time (this process only ever
 * plays one track). All private to fs3eaudio.c; the process entry function
 * owns the single instance on its own stack.
 * ---------------------------------------------------------------------- */
typedef struct
{
    /* mpega decode state -- NULL/0 when nothing is loaded. */
    MPEGA_STREAM *stream;
    WORD          renderBuffer[MPEGA_PCM_SIZE * MPEGA_MAX_CHANNELS];
    WORD         *pcm[MPEGA_MAX_CHANNELS];
    SHORT        *pleft, *pright; /* cursor into the last-decoded frame */
    LONG          sampleLeft;     /* samples left at pleft/pright before decoding another frame */

    /* AHI device state -- opened per FS3EAUDIOQ_PLAY, closed on stop/EOF/
     * shutdown (mirrors AHIS_Create/AHIS_Delete's per-track lifecycle in
     * the reference implementation). */
    struct MsgPort    *ahiPort;
    struct AHIRequest  *io1, *io2;   /* front/back IO requests, swapped every buffer */
    struct AHIRequest  *join;        /* previously sent request to WaitIO before reusing its buffer */
    SHORT              *buf1, *buf2; /* front/back sample buffers, swapped alongside io1/io2 */
    BOOL                ahiOpen;     /* TRUE once OpenDevice() succeeded (CloseDevice needed) */

    /* Active track bookkeeping -- who to notify, and with what key, once
     * this track reaches EOF on its own (see FS3EAUDIOR_FINISHED), or
     * periodically while playing (see FS3EAUDIOR_PROGRESS). */
    struct MsgPort *replyPort;
    char            key[FS3EAUDIO_KEY_SIZE];
    BOOL            playing;
    BOOL            paused;        /* see FS3EAUDIOQ_PAUSE's doc comment in fs3eaudio.h */
    ULONG           samplesDecoded; /* running total this track, for FS3EAUDIOR_PROGRESS's
                                     * elapsed-ms (samplesDecoded / dec_frequency) */
    UWORD           buffersSinceProgress; /* throttles FS3EAUDIOR_PROGRESS -- see
                                           * FS3EAUDIO_PROGRESS_EVERY_N_BUFFERS */
} FS3EAudioState;

/* -------------------------------------------------------------------------
 * AHI device -- open/close (adapted from AHISStaticThread's init paragraph
 * and AHISStaticThread_Close in the reference AHISoundServer.c; no
 * separate sub-thread spawn needed here, this whole file's process already
 * is the dedicated task).
 * ---------------------------------------------------------------------- */

static void FS3EAudio_CloseAHI(FS3EAudioState *st)
{
    if (st->join) {
        if (!CheckIO((struct IORequest *)st->join))
            AbortIO((struct IORequest *)st->join);
        WaitIO((struct IORequest *)st->join);
        st->join = NULL;
    }

    if (st->buf1) { FreeVec(st->buf1); st->buf1 = NULL; }
    if (st->buf2) { FreeVec(st->buf2); st->buf2 = NULL; }

    if (st->ahiOpen) {
        CloseDevice((struct IORequest *)st->io1);
        st->ahiOpen = FALSE;
        AHIBase = NULL; /* only valid while the device is open -- see FS3EAudio_OpenAHI */
    }
    if (st->io1) { DeleteExtIO((struct IORequest *)st->io1); st->io1 = NULL; }
    if (st->io2) { DeleteExtIO((struct IORequest *)st->io2); st->io2 = NULL; }
    if (st->ahiPort) { DeletePort(st->ahiPort); st->ahiPort = NULL; }
}

static BOOL FS3EAudio_OpenAHI(FS3EAudioState *st)
{
    BYTE err;

    st->ahiPort = CreatePort(NULL, 0);
    if (!st->ahiPort) return FALSE;

    st->io1 = (struct AHIRequest *)CreateExtIO(st->ahiPort, sizeof(struct AHIRequest));
    if (!st->io1) { FS3EAudio_CloseAHI(st); return FALSE; }
    st->io1->ahir_Version = 4;

    err = OpenDevice(AHINAME, 0, (struct IORequest *)st->io1, 0);
    if (err) { FS3EAudio_CloseAHI(st); return FALSE; }
    st->ahiOpen = TRUE;

    /* Fill AHIBase from the just-opened request's own io_Device -- see
     * exampleAHIBase.txt; ahi.device is opened, never OpenLibrary()'d. */
    AHIBase = (struct Library *)st->io1->ahir_Std.io_Device;

    st->io2 = (struct AHIRequest *)CreateExtIO(st->ahiPort, sizeof(struct AHIRequest));
    if (!st->io2) { FS3EAudio_CloseAHI(st); return FALSE; }
    CopyMem(st->io1, st->io2, sizeof(struct AHIRequest));

    st->buf1 = (SHORT *)AllocVec(FS3EAUDIO_BUFFERSIZE, MEMF_PUBLIC | MEMF_CLEAR);
    st->buf2 = (SHORT *)AllocVec(FS3EAUDIO_BUFFERSIZE, MEMF_PUBLIC | MEMF_CLEAR);
    if (!st->buf1 || !st->buf2) { FS3EAudio_CloseAHI(st); return FALSE; }

    st->join = NULL;
    return TRUE;
}

/* -------------------------------------------------------------------------
 * mpega decode
 * ---------------------------------------------------------------------- */

static BOOL FS3EAudio_StartTrack(FS3EAudioState *st, const char *path)
{
    /* Typed MPEGA_CTRL, not the flat USHORT-array cast the reference
     * AHIMPEGA.c used -- that trick assumed a specific (ppc-amigaos-gcc)
     * struct packing/pointer size; unsafe to carry over onto a different
     * (m68k) ABI. force_mono=0 (real stereo decode -- FS3EAudio_DecodeInto
     * always interleaves pcm[0]/pcm[1]), freq_div=1 (full quality, no
     * output-rate division), quality=2 (high). Matches the reference's own
     * layer_3 (MP3) settings, the only layer Mastodon media ever is. */
    static const MPEGA_CTRL mpaCtrl = {
        NULL,                          /* bs_access: default file I/O */
        { 0, { 1, 2, 44100 }, { 1, 2, 44100 } }, /* layer_1_2 (unused by MP3, filled anyway) */
        { 0, { 1, 2, 44100 }, { 1, 2, 44100 } }, /* layer_3 */
        0,                             /* check_mpeg: don't validate at open */
        0                              /* stream_buffer_size: 0 = library default */
    };

    st->stream = MPEGA_open((char *)path, (MPEGA_CTRL *)&mpaCtrl);
    if (!st->stream) return FALSE;

    st->sampleLeft = 0;
    st->pleft  = NULL;
    st->pright = NULL;
    return TRUE;
}

static void FS3EAudio_StopTrack(FS3EAudioState *st)
{
    FS3EAudio_CloseAHI(st);
    if (st->stream) { MPEGA_close(st->stream); st->stream = NULL; }
    st->sampleLeft = 0;
    st->playing = FALSE;
    st->paused  = FALSE;
    st->samplesDecoded       = 0;
    st->buffersSinceProgress = 0;
}

/*
 * Decodes frames into dst (interleaved stereo SHORTs) until nbSamplesToFill
 * sample-pairs have been written or the stream ends. Returns the number of
 * sample-pairs actually written; 0 means the stream is genuinely over
 * (MPEGA_ERR_EOF) or unrecoverably broken (MPEGA_ERR_BADFRAME).
 *
 * Deviates from the reference soundWriteMP3() on one point: MPEGA_decode_
 * frame() returning 0 is documented (mpega_protos.h) as "current frame is
 * skipped... does not indicate end of stream", but the reference treated
 * it as EOF anyway (its own comment: "don't know if same..."). Treating a
 * legitimately skipped frame as a hard stop would cut playback short on
 * any stream that hits one. FS3EAUDIO_MAX_EMPTY_FRAMES bounds the retry so
 * a stream that pathologically never advances still can't hang this
 * process forever.
 */
#define FS3EAUDIO_MAX_EMPTY_FRAMES 64

static ULONG FS3EAudio_DecodeInto(FS3EAudioState *st, SHORT *dst, ULONG nbSamplesToFill)
{
    LONG  toFill = (LONG)nbSamplesToFill;
    ULONG total  = 0;
    int   emptyFrames = 0;

    while (toFill > 0)
    {
        if (st->sampleLeft <= 0)
        {
            LONG result = MPEGA_decode_frame(st->stream, st->pcm);

            if (result == MPEGA_ERR_EOF || result == MPEGA_ERR_BADFRAME)
                return total;

            if (result == 0) {
                if (++emptyFrames >= FS3EAUDIO_MAX_EMPTY_FRAMES)
                    return total;
                continue; /* skipped frame -- try the next one */
            }
            emptyFrames = 0;

            st->sampleLeft = result;
            st->pleft  = (SHORT *)st->pcm[0];
            st->pright = (SHORT *)st->pcm[1];
        }
        {
            LONG nbs = toFill;
            if (st->sampleLeft < nbs) nbs = st->sampleLeft;
            total          += (ULONG)nbs;
            toFill         -= nbs;
            st->sampleLeft -= nbs;
            while (nbs > 0) {
                *dst++ = *(st->pleft++);
                *dst++ = *(st->pright++);
                nbs--;
            }
        }
    }
    return total;
}

/*
 * One iteration of the double-buffer streaming loop: decode a buffer's
 * worth into the current front buffer, send it, wait for the previously
 * sent buffer to finish, then swap front/back -- see AHISStaticThread's
 * loop body in the reference AHISoundServer.c, ported field-for-field
 * (verified against it: same ahir_Link double-buffer chaining, same
 * join-before-reuse ordering). Blocks on WaitIO for at most one buffer's
 * playback duration (well under a second), which is what bounds how
 * quickly FS3EAUDIOQ_STOP/PLAY/SHUTDOWN react while streaming -- see
 * FS3EAudio_ProcEntry(). Returns FALSE once FS3EAudio_DecodeInto() reports
 * genuine end-of-stream.
 */
static BOOL FS3EAudio_StreamOneBuffer(FS3EAudioState *st)
{
    ULONG numSampleWritten;
    SHORT *dst = st->buf1;
    struct AHIRequest *req;

    numSampleWritten = FS3EAudio_DecodeInto(st, dst, FS3EAUDIO_BUFFER_SAMPLES);
    if (numSampleWritten == 0)
        return FALSE;

    st->samplesDecoded += numSampleWritten;

    req = st->io1;
    req->ahir_Std.io_Message.mn_Node.ln_Pri = 127; /* highest normal priority -- prompt handling */
    req->ahir_Std.io_Command = CMD_WRITE;
    req->ahir_Std.io_Data    = (APTR)dst;
    req->ahir_Std.io_Length  = (LONG)(numSampleWritten << 2); /* stereo 16-bit = 4 bytes/pair */
    req->ahir_Std.io_Offset  = 0;
    req->ahir_Version   = 4;
    req->ahir_Frequency = (ULONG)(st->stream ? st->stream->dec_frequency : 44100);
    req->ahir_Type      = AHIST_S16S;
    req->ahir_Volume    = 0x00010000; /* Fixed 1.0 -- full volume */
    req->ahir_Position  = 0x00008000; /* Fixed 0.5 -- centered */
    req->ahir_Link      = st->join;

    SendIO((struct IORequest *)req);

    if (st->join)
        WaitIO((struct IORequest *)st->join);
    st->join = req;

    /* swap double buffer -- see this function's header comment */
    st->io1  = st->io2;  st->io2  = req;
    st->buf1 = st->buf2; st->buf2 = dst;

    return TRUE;
}

/* -------------------------------------------------------------------------
 * FS3EAudio_* public API
 * ---------------------------------------------------------------------- */

static void FS3EAudio_ProcEntry(void);

struct MsgPort *FS3EAudio_Start(void)
{
    struct MsgPort           *replyPort;
    struct FS3EAudioStartup   startup;
    struct Process           *proc;

    replyPort = CreateMsgPort();
    if (!replyPort)
        return NULL;

    startup.fs3eas_Msg.mn_ReplyPort = replyPort;
    startup.fs3eas_Msg.mn_Length    = sizeof(startup);
    startup.fs3eas_RequestPort      = NULL;

    g_FS3EAudioStartup = &startup;

    proc = CreateNewProcTags(
        NP_Entry,     (ULONG)FS3EAudio_ProcEntry,
        NP_Name,      (ULONG)FS3EAUDIO_PROC_NAME,
        NP_StackSize, (ULONG)FS3EAUDIO_STACK_SIZE,
        TAG_DONE);

    if (!proc)
    {
        DeleteMsgPort(replyPort);
        return NULL;
    }

    WaitPort(replyPort);
    GetMsg(replyPort);
    DeleteMsgPort(replyPort);

    return startup.fs3eas_RequestPort;
}

void FS3EAudio_Stop(struct MsgPort *requestPort, struct MsgPort *replyPort)
{
    FS3EAudioMessage msg;

    if (!requestPort)
        return;

    memset(&msg, 0, sizeof(msg));
    msg.fs3eam_Msg.mn_ReplyPort = replyPort;
    msg.fs3eam_Msg.mn_Length    = sizeof(msg);
    msg.fs3eam_Type             = FS3EAUDIOQ_SHUTDOWN;

    PutMsg(requestPort, (struct Message *)&msg);

    WaitPort(replyPort);
    GetMsg(replyPort);
}

BOOL FS3EAudio_Play(struct MsgPort *requestPort, struct MsgPort *replyPort,
                     const char *path, const char *key)
{
    FS3EAudioMessage *msg;

    if (!requestPort || !replyPort || !path || !path[0])
        return FALSE;
    if (strlen(path) >= FS3EAUDIO_PATH_SIZE)
        return FALSE;
    if (key && strlen(key) >= FS3EAUDIO_KEY_SIZE)
        return FALSE;

    msg = (FS3EAudioMessage *)AllocVec(sizeof(FS3EAudioMessage), MEMF_CLEAR | MEMF_PUBLIC);
    if (!msg)
        return FALSE;

    msg->fs3eam_Msg.mn_ReplyPort = replyPort;
    msg->fs3eam_Msg.mn_Length    = sizeof(*msg);
    msg->fs3eam_Type             = FS3EAUDIOQ_PLAY;
    strcpy(msg->fs3eam_Path, path);
    strcpy(msg->fs3eam_Key, key ? key : "");

    PutMsg(requestPort, (struct Message *)&msg->fs3eam_Msg);
    return TRUE;
}

BOOL FS3EAudio_PlayStop(struct MsgPort *requestPort, struct MsgPort *replyPort)
{
    FS3EAudioMessage *msg;

    if (!requestPort)
        return FALSE;

    msg = (FS3EAudioMessage *)AllocVec(sizeof(FS3EAudioMessage), MEMF_CLEAR | MEMF_PUBLIC);
    if (!msg)
        return FALSE;

    msg->fs3eam_Msg.mn_ReplyPort = replyPort;
    msg->fs3eam_Msg.mn_Length    = sizeof(*msg);
    msg->fs3eam_Type             = FS3EAUDIOQ_STOP;

    PutMsg(requestPort, (struct Message *)&msg->fs3eam_Msg);
    return TRUE;
}

BOOL FS3EAudio_Pause(struct MsgPort *requestPort, struct MsgPort *replyPort, BOOL pause)
{
    FS3EAudioMessage *msg;

    if (!requestPort)
        return FALSE;

    msg = (FS3EAudioMessage *)AllocVec(sizeof(FS3EAudioMessage), MEMF_CLEAR | MEMF_PUBLIC);
    if (!msg)
        return FALSE;

    msg->fs3eam_Msg.mn_ReplyPort = replyPort;
    msg->fs3eam_Msg.mn_Length    = sizeof(*msg);
    msg->fs3eam_Type             = FS3EAUDIOQ_PAUSE;
    msg->fs3eam_Pause            = pause;

    PutMsg(requestPort, (struct Message *)&msg->fs3eam_Msg);
    return TRUE;
}

/* -------------------------------------------------------------------------
 * Process entry
 * ---------------------------------------------------------------------- */

/* Sends the spontaneous "track finished" notify for the track st was just
 * playing, to whichever replyPort/key its starting FS3EAUDIOQ_PLAY
 * specified -- called right after FS3EAudio_StreamOneBuffer() reports
 * genuine EOF, before st is reset. mn_ReplyPort is left NULL: this is a
 * one-way notify, nothing replies to it -- the caller just GetMsg()s it
 * off its own reply port and FreeVec()s it, same as any other message
 * here (see fs3eaudio.h's struct comment). */
static void FS3EAudio_NotifyFinished(FS3EAudioState *st)
{
    FS3EAudioMessage *note;

    if (!st->replyPort) return;

    note = (FS3EAudioMessage *)AllocVec(sizeof(FS3EAudioMessage), MEMF_CLEAR | MEMF_PUBLIC);
    if (!note) return;

    note->fs3eam_Msg.mn_ReplyPort = NULL;
    note->fs3eam_Msg.mn_Length    = sizeof(*note);
    note->fs3eam_Type             = FS3EAUDIOQ_PLAY;
    note->fs3eam_Result           = FS3EAUDIOR_FINISHED;
    strcpy(note->fs3eam_Key, st->key);

    PutMsg(st->replyPort, (struct Message *)&note->fs3eam_Msg);
}

/* Every Nth streamed buffer, not every single one -- each buffer is only
 * ~46ms at 44100Hz/FS3EAUDIO_BUFFERSIZE, so sending a notify per buffer
 * would needlessly flood replyPort; ~740ms between updates is plenty for a
 * position slider. */
#define FS3EAUDIO_PROGRESS_EVERY_N_BUFFERS 16

/* Sends an FS3EAUDIOR_PROGRESS notify for the track currently playing, same
 * one-way/no-reply convention as FS3EAudio_NotifyFinished() above -- called
 * from the main loop every FS3EAUDIO_PROGRESS_EVERY_N_BUFFERS buffers while
 * playing and not paused (see FS3EAudio_ProcEntry()). */
static void FS3EAudio_NotifyProgress(FS3EAudioState *st)
{
    FS3EAudioMessage *note;
    ULONG freq;

    if (!st->replyPort) return;

    note = (FS3EAudioMessage *)AllocVec(sizeof(FS3EAudioMessage), MEMF_CLEAR | MEMF_PUBLIC);
    if (!note) return;

    freq = (ULONG)(st->stream ? st->stream->dec_frequency : 44100);

    note->fs3eam_Msg.mn_ReplyPort = NULL;
    note->fs3eam_Msg.mn_Length    = sizeof(*note);
    note->fs3eam_Type             = FS3EAUDIOQ_PLAY;
    note->fs3eam_Result           = FS3EAUDIOR_PROGRESS;
    strcpy(note->fs3eam_Key, st->key);
    /* (samples/freq)*1000 done as whole-seconds + remainder separately --
     * avoids a 32-bit overflow on samplesDecoded*1000, which a plain ULONG
     * multiply would hit after well under two minutes of playback at
     * 44100Hz (no UQUAD/64-bit type in this NDK's exec/types.h). */
    note->fs3eam_ElapsedMs = freq
        ? (st->samplesDecoded / freq) * 1000 + ((st->samplesDecoded % freq) * 1000) / freq
        : 0;
    note->fs3eam_TotalMs   = st->stream ? st->stream->ms_duration : 0;

    PutMsg(st->replyPort, (struct Message *)&note->fs3eam_Msg);
}

/* Entry point of the audio process, running as its own AmigaDOS task. */
static void FS3EAudio_ProcEntry(void)
{
    struct FS3EAudioStartup *startup = g_FS3EAudioStartup;
    struct MsgPort           *requestPort;
    FS3EAudioMessage         *shutdownMsg = NULL;
    FS3EAudioState            st;
    BOOL                      running = TRUE;

    memset(&st, 0, sizeof(st));
    st.pcm[0] = &st.renderBuffer[0];
    st.pcm[1] = &st.renderBuffer[MPEGA_PCM_SIZE];

    requestPort = CreateMsgPort();

    /* Hand the request port (or NULL on failure) back to FS3EAudio_Start(). */
    startup->fs3eas_RequestPort = requestPort;
    PutMsg(startup->fs3eas_Msg.mn_ReplyPort, &startup->fs3eas_Msg);

    if (!requestPort)
        return;

    MPEGABase = OpenLibrary("mpega.library", 0);

    while (running)
    {
        FS3EAudioMessage *msg;

        /* Idle: block for the next request. Streaming: only poll --
         * FS3EAudio_StreamOneBuffer() below is what actually blocks
         * (bounded to one buffer's playback duration), so a request
         * arriving mid-track is seen well within that same bound. Also
         * blocks while paused -- st.playing stays TRUE across a pause (see
         * FS3EAUDIOQ_PAUSE), but nothing is being streamed/decoded then, so
         * without this the loop would spin GetMsg() on an empty port
         * instead of sleeping until resume/stop/shutdown arrives. */
        if (!st.playing || st.paused)
            WaitPort(requestPort);

        while ((msg = (FS3EAudioMessage *)GetMsg(requestPort)) != NULL)
        {
            switch (msg->fs3eam_Type)
            {
                case FS3EAUDIOQ_SHUTDOWN:
                    /* Held instead of replied immediately -- same reasoning
                     * as FS3EThumb_ProcEntry/FS3ENet_ProcEntry: this task
                     * shares its loaded code image with the main process,
                     * so replying now would let the main process race
                     * ahead toward tearing that image down while this task
                     * still had AHI/mpega cleanup left to run. */
                    running = FALSE;
                    if (st.playing) FS3EAudio_StopTrack(&st);
                    msg->fs3eam_Result = FS3EAUDIOR_OK;
                    shutdownMsg = msg;
                    continue;

                case FS3EAUDIOQ_STOP:
                    if (st.playing) FS3EAudio_StopTrack(&st);
                    msg->fs3eam_Result = FS3EAUDIOR_OK;
                    break;

                case FS3EAUDIOQ_PAUSE:
                    /* No-op (still OK) if nothing is loaded -- see the
                     * request's own doc comment in fs3eaudio.h. */
                    if (st.playing) st.paused = msg->fs3eam_Pause;
                    msg->fs3eam_Result = FS3EAUDIOR_OK;
                    break;

                case FS3EAUDIOQ_PLAY:
                    /* Replacing whatever was playing -- no FINISHED notify
                     * for it, that result is reserved for genuine EOF. */
                    if (st.playing) FS3EAudio_StopTrack(&st);

                    strncpy(st.key, msg->fs3eam_Key, sizeof(st.key) - 1);
                    st.key[sizeof(st.key) - 1] = '\0';
                    st.replyPort = msg->fs3eam_Msg.mn_ReplyPort;

                    if (MPEGABase &&
                        FS3EAudio_StartTrack(&st, msg->fs3eam_Path) &&
                        FS3EAudio_OpenAHI(&st))
                    {
                        st.playing = TRUE;
                        msg->fs3eam_Result = FS3EAUDIOR_OK;
                    }
                    else
                    {
                        FS3EAudio_StopTrack(&st);
                        msg->fs3eam_Result = FS3EAUDIOR_ERROR;
                    }
                    break;

                default:
                    msg->fs3eam_Result = FS3EAUDIOR_ERROR;
                    break;
            }

            ReplyMsg((struct Message *)msg);
        }

        if (!running) break;

        if (st.playing && !st.paused)
        {
            if (!FS3EAudio_StreamOneBuffer(&st))
            {
                FS3EAudio_NotifyFinished(&st);
                FS3EAudio_StopTrack(&st);
            }
            else if (++st.buffersSinceProgress >= FS3EAUDIO_PROGRESS_EVERY_N_BUFFERS)
            {
                st.buffersSinceProgress = 0;
                FS3EAudio_NotifyProgress(&st);
            }
        }
    }

    if (MPEGABase) { CloseLibrary(MPEGABase); MPEGABase = NULL; }
    DeleteMsgPort(requestPort);

    if (shutdownMsg)
        ReplyMsg((struct Message *)shutdownMsg);
}
