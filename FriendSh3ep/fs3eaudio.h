#ifndef FS3EAUDIO_H
#define FS3EAUDIO_H

/*
 * fs3eaudio.h - FriendSh3ep MP3 audio playback process.
 *
 * $VER: fs3eaudio.h 1.0 (18.07.2026)
 * Copyright (C) 2026 FriendSh3ep contributors. All rights reserved.
 *
 * Decodes MP3 (mpega.library, MPEGA_decode_frame) and streams it to
 * ahi.device with a double-buffered SendIO/WaitIO chain (ahir_Link) --
 * ported/adapted from ukonxMyWorldOS3SRC/Ukonx_MyWorld's AHISoundServer.c
 * (a generic AHI double-buffer "sound server" driven by a SoundWriter
 * callback) + AHIMPEGA.c (the MP3-specific callback). Folded into one
 * process/file here since this app only ever plays MP3 through it -- no
 * need for that project's reusable SoundWriter indirection.
 *
 * Runs entirely on its own AmigaDOS process (CreateNewProcTags, same
 * request/reply MsgPort shape as fs3ethumb.c/network_fs3e/fs3enet.c),
 * since a continuously streaming AHI double-buffer loop would otherwise
 * block the GUI task for the whole track. Control messages (PLAY/STOP/
 * SHUTDOWN) are polled non-blockingly between buffer fills -- so reacting
 * to a Stop request is bounded by one buffer's playback duration (well
 * under a second) rather than needing the tight decode/AHI loop itself
 * restructured around a multi-source Wait().
 *
 * Generic/standalone for now -- playable via FS3EAudio_Play() with any
 * local MP3 file path already on disk. Wiring this to Mastodon toot audio
 * attachments (TTL_HOT_PLAY_AUDIO) is a separate, later step.
 *
 * Toolchain note: <proto/mpega.h> does NOT compile with this project's
 * m68k-amigaos-gcc (it unconditionally #includes <pragmas/mpega_pragmas.h>,
 * which this toolchain doesn't ship -- unlike e.g. <proto/datatypes.h> or
 * <proto/ahi.h>, which both have a __GNUC__ branch to the matching header
 * under inline/ instead, mpega's proto header has no such branch).
 * fs3eaudio.c includes <clib/mpega_protos.h> + <inline/mpega.h> directly
 * instead -- the same LP-macro inline-stub mechanism proto/ahi.h itself
 * resolves to, just
 * without the wrapper that's missing the __GNUC__ check.
 */

#include <exec/ports.h>
#include <exec/types.h>

enum FS3EAudioRequestType
{
    FS3EAUDIOQ_SHUTDOWN = 0,  /* ask the audio process to exit (stops playback first) */
    FS3EAUDIOQ_PLAY,          /* decode+stream a new MP3 file, replacing whatever is playing */
    FS3EAUDIOQ_STOP           /* stop whatever is playing; no file loaded after */
};

enum FS3EAudioResult
{
    FS3EAUDIOR_OK = 0,        /* PLAY: now streaming. STOP/SHUTDOWN: done. */
    FS3EAUDIOR_ERROR,         /* PLAY: couldn't open mpega.library / the file / ahi.device */
    FS3EAUDIOR_FINISHED       /* spontaneous notify, fs3eam_Type left at FS3EAUDIOQ_PLAY:
                                * the track decoded to EOF on its own. Never sent for a
                                * track a STOP or a later PLAY replaced first. */
};

#define FS3EAUDIO_PATH_SIZE 256
/* Long enough for a full media CDN URL, same reasoning as fs3ethumb.h's
 * FS3ETHUMB_KEY_SIZE -- unused for now (fs3eam_Key is caller-opaque, just
 * echoed back unchanged), but sized ahead for when this is wired to toot
 * audio attachments (see this file's header comment). */
#define FS3EAUDIO_KEY_SIZE   384

/*
 * Fixed-layout request/reply message (no separate AllocVec'd data block,
 * same reasoning as FS3EThumbMessage). Two different lifecycles share this
 * one struct:
 *   - PLAY/STOP/SHUTDOWN requests: caller AllocVec's it, PutMsg()s it to
 *     requestPort; the process ReplyMsg()s it back with fs3eam_Result set.
 *   - The spontaneous "track finished" notify: the process itself
 *     AllocVec's a fresh one and PutMsg()s it (mn_ReplyPort left NULL --
 *     nothing replies to a notify) directly to whichever replyPort the
 *     FS3EAUDIOQ_PLAY that started that track specified. Either way, once
 *     the caller has GetMsg()'d it off replyPort, it owns the block and
 *     must FreeVec() it -- same convention as every other message type in
 *     this app (FS3ENetMessage, FS3EThumbMessage).
 */
typedef struct FS3EAudioMessage
{
    struct Message fs3eam_Msg;
    ULONG          fs3eam_Type;    /* enum FS3EAudioRequestType */
    ULONG          fs3eam_Result;  /* enum FS3EAudioResult, set on reply/notify */

    /* FS3EAUDIOQ_PLAY request field, filled by the caller. */
    char  fs3eam_Path[FS3EAUDIO_PATH_SIZE]; /* local file path to play, already on disk */

    /* Caller key, echoed back unchanged on the PLAY ack AND on the eventual
     * spontaneous FS3EAUDIOR_FINISHED notify for that same track -- lets
     * the GUI tell which track a FINISHED notify is about without tracking
     * it itself. Empty/unused for STOP/SHUTDOWN. */
    char  fs3eam_Key[FS3EAUDIO_KEY_SIZE];
} FS3EAudioMessage;

/* Start the audio process. Returns the request MsgPort, or NULL on failure. */
struct MsgPort *FS3EAudio_Start(void);

/*
 * Ask the audio process to shut down and wait for it to exit -- stops
 * whatever is playing first. requestPort is the port returned by
 * FS3EAudio_Start(); replyPort is a temporary port created by the caller
 * to receive the shutdown reply.
 */
void FS3EAudio_Stop(struct MsgPort *requestPort, struct MsgPort *replyPort);

/*
 * Ask the audio process to decode+stream path (a local MP3 file already on
 * disk), replacing whatever is currently playing -- returns immediately;
 * the ack (FS3EAUDIOR_OK once streaming has actually started, or
 * FS3EAUDIOR_ERROR if mpega.library/the file/ahi.device couldn't be
 * opened) arrives later on replyPort, the caller's own persistent port
 * (checked from its Wait() loop the same way as network_fs3e's
 * netReplyPort/fs3ethumb's thumbReplyPort). The SAME replyPort also
 * receives the eventual spontaneous FS3EAUDIOR_FINISHED notify once the
 * track decodes to EOF on its own (never sent if a later FS3EAudio_Play()
 * or FS3EAudio_PlayStop() replaced it first). key is echoed back on the
 * ack and the eventual FINISHED notify so the caller can tell tracks
 * apart -- pass NULL/"" if unused. Returns FALSE (nothing queued) if
 * requestPort/path are missing or path/key don't fit the fixed-size
 * fields.
 */
BOOL FS3EAudio_Play(struct MsgPort *requestPort, struct MsgPort *replyPort,
                     const char *path, const char *key);

/*
 * Ask the audio process to stop whatever is currently playing (a no-op,
 * still acked, if nothing is) -- returns immediately; FS3EAUDIOR_OK
 * arrives later on replyPort, same as FS3EAudio_Play(). No FS3EAUDIOR_
 * FINISHED notify follows a stop this causes -- that result is reserved
 * for a track reaching EOF on its own. Returns FALSE (nothing queued) if
 * requestPort is missing.
 */
BOOL FS3EAudio_PlayStop(struct MsgPort *requestPort, struct MsgPort *replyPort);

#endif /* FS3EAUDIO_H */
