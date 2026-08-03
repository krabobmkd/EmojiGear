#ifndef FS3EMEDIAVIEW_H
#define FS3EMEDIAVIEW_H

/*
 * fs3emediaview.h - "FriendSh3ep Media" full-size attachment viewer.
 *
 * $VER: fs3emediaview.h 3.0 (17.07.2026)
 * Copyright (C) 2026 FriendSh3ep contributors. All rights reserved.
 *
 * Opened by clicking a toot's media preview rectangle (TTL_HOT_IMAGE --
 * see friendsh3ep.c's TTIMELINE_HotSpotNotify switch). A classic BOOPSI
 * window.class + layout.gadget sub-window, like every other sub-window in
 * this app (fs3eloginview.c, fs3etootview.c, fs3eemojibox.c, ...).
 *
 * Rendering: a private "FS3EMediaPic" gadgetclass (MakeClass'd once,
 * pattern copied from fs3eemojibox.c's FSEBGrid class), one persistent
 * instance added ONCE to the mother layout via LAYOUT_AddChild and never
 * removed for the life of the window. Showing a new attachment is just an
 * attribute update (MEDIAPIC_BitMap/MaskPlane/Width/Height/Message,
 * see fs3emediaview.c) that repaints the same gadget in place -- earlier
 * attempts to represent the picture as a swappable layout child (a
 * picture.datatype object detached/reattached via LAYOUT_ModifyChild +
 * CHILD_ReplaceObject on every new URL) did not work reliably in practice,
 * which is why this went back to a plain decoded BitMap (bmimage.h,
 * picture.datatype used only as the decoder, same as every other themed
 * image in this app) blitted by hand in GM_RENDER.
 *
 * The image shown is the same URL TootTimeline already displays a small
 * on-screen thumbnail of (TTLPost.mediaUrls[], Mastodon's "preview_url") --
 * not the server's true full-resolution original (a separate future
 * feature). "Big" here means: the undownscaled download the thumbnail
 * process shrank to build the small on-screen preview. That download is
 * cached under FS3E_CACHE_SUBDIR_THUMBNAILS keyed by the URL when "Keep
 * big thumbnails" was on when the post was first shown; if it was off, or
 * this URL has never been opened this way before, FS3EMediaView_ShowUrl()
 * re-requests it with keepOriginal=TRUE (persisting it from now on) -- see
 * fs3emediaview.c for the fetch/cache-hit details.
 *
 * Single reusable window instance: a second click while one is already
 * open reuses it (brought to front, picture replaced) instead of opening a
 * new one each time.
 *
 * A "Close" / "Save Media..." menu is attached the first time the window
 * opens (classic GadTools menu strip, same pattern as fs3etootview.c's
 * window-local "Toot" menu). "Save Media..." copies the already-downloaded
 * cache file straight to disk (no re-fetch) under an ASL file requester
 * defaulting to RAM: and a meaningful name: the poster's @user@instance if
 * FS3EMediaView_ShowUrl() was given one, else the cache's hash-id
 * filename, with the correct extension always appended from the file's
 * own magic bytes (BmImage_SniffFormat) rather than trusted from the URL.
 */

#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/classusr.h>
#include <intuition/classes.h>

#include "bmimage.h"
#include "network_fs3e/fs3enet.h"
#include "fs3eaudio.h"

/* Which decode routine an audio attachment's URL extension mapped to --
 * see mediaview_audio_backend_for_url() in fs3emediaview.c. Only MPEGA is
 * actually playable today; WAV/OGG are recognized and shown as such
 * (rather than silently doing nothing) but need a decode routine other
 * than mpega.library (mpega.library is MP3/MPEG-audio only) -- a
 * follow-up, same "prepared, not finished" state fs3eaudio.c was in
 * before this file wired it up. */
typedef enum FS3EMVAudioBackend {
    FS3EMV_AUDIO_NONE = 0,
    FS3EMV_AUDIO_MPEGA,
    FS3EMV_AUDIO_UNSUPPORTED
} FS3EMVAudioBackend;

typedef struct FS3EMediaView {
    Object        *windowObj;  /* BOOPSI window object (persistent) */
    struct Window *window;     /* Intuition window, valid while open */

    LONG left, top; /* remembered across closes (not persisted to disk) */

    /* Rendering: layout -> transportRow (tapeDeckGadget + sliderGadget),
     * picGadget -- all permanent children, for the whole life of the window
     * (built once by mediaview_ensure_window(), never removed/replaced --
     * see the header comment above for why picGadget itself works this
     * way). picClass is MakeClass'd once and FreeClass'd in Dispose(), same
     * lifecycle as fs3eemojibox.c's gridClass. transportRow is the layout's
     * first line, above picGadget: tapeDeckGadget (stock tapedeck.gadget,
     * see friendsh3ep.c's TapeDeckBase) on the left, sliderGadget (stock
     * slider.gadget, SORIENT_HORIZ, see friendsh3ep.c's SliderBase) filling
     * the rest of the row -- transport controls only for now, neither is
     * yet wired to actually drive audio/video playback (see
     * GID_MEDIAVIEW_TAPEDECK/GID_MEDIAVIEW_SLIDER). */
    Object *layout;
    Class  *picClass;
    Object *picGadget;
    Object *tapeDeckGadget;
    Object *sliderGadget;

    /* Currently decoded/remapped picture, if any (see bmimage.h).
     * picGadget's MEDIAPIC_* attributes are pushed from this struct's
     * bitmap/mask/width/height fields every time it (re)loads or is
     * unloaded -- see fs3emediaview.c's mediaview_push_picture(). */
    BmImage image;

    char *pendingUrl; /* AllocVec'd; NULL when nothing in flight */
    BOOL  loading;

    /* Updated by FS3EMediaView_OnFetchProgress() while pendingUrl's chunked
     * download is in flight (see FS3ENETQ_FETCH_PROGRESS in fs3enet.h).
     * progressTotalBytes is 0 until/unless the server tells us the real
     * size. Not yet drawn anywhere -- plumbing only, a progress indicator
     * in the window is a follow-up. */
    ULONG progressBytesSoFar;
    ULONG progressTotalBytes;

    /* "Close"/"Save Media..." menu -- built once, the first time the
     * window opens (mediaview_create_menu), torn down in
     * FS3EMediaView_Close (menu strips don't survive WM_CLOSE). */
    struct Menu *menu;
    APTR         menuVisualInfo;

    /* Poster's @user@instance for the currently shown attachment, as
     * passed to FS3EMediaView_ShowUrl(); "" if the caller didn't have one.
     * Used by "Save Media..." to build a meaningful default filename --
     * see fs3emediaview.c's mediaview_build_default_name(). */
    char poster[128];

    /* Audio playback (mp3/wav/ogg attachments) -- counterpart to the
     * picture/BmImage fields above, driven by FS3EMediaView_ShowAudioUrl()
     * instead of ShowUrl(). isAudio TRUE means picGadget shows
     * MEDIAPIC_Message text only (mv->image stays unloaded/irrelevant) and
     * tapeDeckGadget/sliderGadget are live; FALSE means they're inert
     * leftovers from a previous audio attachment (GID_MEDIAVIEW_TAPEDECK
     * clicks are ignored while isAudio is FALSE -- see
     * FS3EMediaView_TapeDeckPressed). audioRequestPort/audioReplyPort are
     * cached from ShowAudioUrl's caller (app->audioRequestPort/
     * audioReplyPort, see friendsh3ep.h) so later calls -- the fetch reply
     * starting playback, TapeDeckPressed sending Play/Pause/Stop -- don't
     * need them threaded through every function signature; NULL/harmless
     * once isAudio is FALSE again. audioKey doubles as the FS3EAudioMessage
     * key (see fs3eaudio.h) so FS3EMediaView_OnAudioReply can tell a reply/
     * notify is about the attachment currently shown. */
    BOOL   isAudio;
    ULONG  audioBackend;                        /* FS3EMVAudioBackend */
    char   audioLocalPath[FS3EAUDIO_PATH_SIZE]; /* filled once the fetch completes */
    char   audioKey[FS3EAUDIO_KEY_SIZE];
    BOOL   audioPlaying;
    BOOL   audioPaused;
    struct MsgPort *audioRequestPort;
    struct MsgPort *audioReplyPort;
} FS3EMediaView;

/* Zeroes mv. Nothing to allocate up front -- the window/layout/picClass
 * are created lazily by the first FS3EMediaView_ShowUrl() call. */
void FS3EMediaView_Init(FS3EMediaView *mv);

/* Frees everything and closes the window if still open.
 * Call once at app teardown. */
void FS3EMediaView_Dispose(FS3EMediaView *mv);

/*
 * Opens (or brings to front) the "FriendSh3ep Media" window and starts
 * loading url's image -- see the header comment above for what "url"
 * means and the cache-hit/re-fetch behaviour. Returns immediately; the
 * picture itself appears once FS3EMediaView_OnFetchReply() delivers it.
 * No-op if url is NULL/empty.
 *
 * posterAcct is the attachment's poster @user@instance if the caller has
 * one (see TTIMELINE_LastHotSpotAcct), copied into mv->poster for "Save
 * Media..." to use as the default filename -- NULL/"" is fine, Save falls
 * back to the cache's hash-id filename in that case.
 */
void FS3EMediaView_ShowUrl(FS3EMediaView *mv, const char *url, const char *posterAcct);

/*
 * Feed every FS3ENETQ_FETCH_IMAGE reply through here from friendsh3ep.c's
 * central reply switch (alongside the existing avatar/thumbnail-pipeline
 * handling, not instead of it -- the same download is useful to both).
 * Ignored unless reply->fs3enf_Key matches the URL FS3EMediaView_ShowUrl()
 * is currently waiting for.
 */
void FS3EMediaView_OnFetchReply(FS3EMediaView *mv, ULONG result,
                                 const FS3ENetFetchImageReply *reply);

/*
 * Feed every FS3ENETQ_FETCH_PROGRESS ping through here (see
 * FS3ENetFetchProgress in fs3enet.h) -- only ever sent for a request that
 * set fs3enf_WantProgress, which today is just FS3EMediaView_ShowUrl()'s own
 * fetch. Ignored unless key matches the URL currently pending, same rule
 * OnFetchReply() already applies. Just records the numbers on mv for now;
 * no progress indicator is drawn yet.
 */
void FS3EMediaView_OnFetchProgress(FS3EMediaView *mv, const char *key,
                                    ULONG bytesSoFar, ULONG totalBytes);

/*
 * Opens (or brings to front) the "FriendSh3ep Media" window for an audio
 * attachment (mp3/wav/ogg) -- counterpart to FS3EMediaView_ShowUrl() for
 * pictures. Fetches url the same way (FS3ENETQ_FETCH_IMAGE, keepOriginal=
 * TRUE, its own cache subdir -- see FS3E_CACHE_SUBDIR_AUDIO in fs3enet.h),
 * then once downloaded starts playback via audioRequestPort/audioReplyPort
 * (app->audioRequestPort/audioReplyPort, see friendsh3ep.h) -- but only for
 * a .mp3 URL (FS3EMV_AUDIO_MPEGA); .wav/.ogg are detected and shown as
 * "not supported yet" in the window instead of silently doing nothing (see
 * FS3EMVAudioBackend's doc comment). Stops whatever audio attachment was
 * previously playing in this same window first. Returns immediately, same
 * as ShowUrl(). No-op if url is NULL/empty.
 *
 * posterAcct: see FS3EMediaView_ShowUrl()'s doc comment.
 */
void FS3EMediaView_ShowAudioUrl(FS3EMediaView *mv, const char *url,
                                 const char *posterAcct,
                                 struct MsgPort *audioRequestPort,
                                 struct MsgPort *audioReplyPort);

/*
 * Feed every FS3EAudioMessage reply/notify (PLAY/PAUSE/STOP acks, the
 * eventual FINISHED, and periodic PROGRESS pings -- see fs3eaudio.h)
 * through here from friendsh3ep.c's audio reply drain. Updates mv's
 * playing/paused state, picGadget's status text, and sliderGadget's
 * position from PROGRESS. Ignored unless mv->isAudio and msg's key matches
 * the attachment FS3EMediaView_ShowAudioUrl() is currently showing.
 */
void FS3EMediaView_OnAudioReply(FS3EMediaView *mv, const FS3EAudioMessage *msg);

/*
 * GID_MEDIAVIEW_TAPEDECK's WMHI_GADGETUP handler -- reads TDECK_Mode off
 * tapeDeckGadget (whichever button was just pressed) and sends the
 * matching FS3EAUDIOQ_PLAY(restart)/PAUSE(toggle)/STOP command via the
 * cached audioRequestPort/audioReplyPort. No-op if mv->isAudio is FALSE or
 * mv->audioBackend isn't FS3EMV_AUDIO_MPEGA (nothing playable loaded).
 * Call from FS3EMediaView_HandleInput() when result's WMHI_GADGETMASK is
 * GID_MEDIAVIEW_TAPEDECK.
 */
void FS3EMediaView_TapeDeckPressed(FS3EMediaView *mv);

void  FS3EMediaView_Close(FS3EMediaView *mv);
BOOL  FS3EMediaView_HandleInput(FS3EMediaView *mv);
ULONG FS3EMediaView_GetSignalMask(FS3EMediaView *mv);

#endif /* FS3EMEDIAVIEW_H */
