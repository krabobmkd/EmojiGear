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
 * Two independent channels, image and audio (see FS3EMediaView's own
 * comment below for the full picture): loading one never clears the
 * other, so an image and an audio attachment from two different toots can
 * both be open in this same window at once, the image showing while the
 * audio plays underneath.
 *
 * A "Close" / "Save Image..." / "Save Audio..." menu is attached the
 * first time the window opens (classic GadTools menu strip, same pattern
 * as fs3etootview.c's window-local "Toot" menu). Each Save entry copies
 * its own channel's already-downloaded cache file straight to disk (no
 * re-fetch) under an ASL file requester defaulting to RAM: and a
 * meaningful name: that channel's poster @user@instance if
 * FS3EMediaView_ShowUrl()/ShowAudioUrl() was given one, else the cache's
 * hash-id filename, with the correct extension always appended -- from
 * the file's own magic bytes (BmImage_SniffFormat) for the image, from
 * the source URL for audio (cache files are hash-named either way, never
 * trusted from the URL for the image case since it may have no extension
 * at all). Each Save entry silently does nothing if its own channel isn't
 * currently loaded.
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

    /* Two independent channels, image and audio -- either, both, or
     * neither can be loaded/active at once. Setting one (ShowUrl/
     * ShowAudioUrl) never clears the other; picGadget shows the image
     * whenever one is loaded (mediaview_push_picture() already prefers the
     * bitmap over any status message), with tapeDeckGadget/sliderGadget
     * driving audio underneath regardless of whether an image is also
     * showing. Each channel gets its own pending-fetch tracking
     * (pendingImageUrl/pendingAudioUrl) since both can have a download in
     * flight at the same time -- a single shared pendingUrl (this struct's
     * earlier, single-channel design) couldn't tell two simultaneous
     * replies apart. */

    /* Currently decoded/remapped picture, if any (see bmimage.h).
     * picGadget's MEDIAPIC_* attributes are pushed from this struct's
     * bitmap/mask/width/height fields every time it (re)loads or is
     * unloaded -- see fs3emediaview.c's mediaview_push_picture(). */
    BmImage image;

    char *pendingImageUrl; /* AllocVec'd; NULL when no image fetch in flight */
    BOOL  imageLoading;

    /* Updated by FS3EMediaView_OnFetchProgress() while pendingImageUrl's
     * chunked download is in flight (see FS3ENETQ_FETCH_PROGRESS in
     * fs3enet.h) -- image channel only, ShowAudioUrl's own fetch never
     * requests progress pings. progressTotalBytes is 0 until/unless the
     * server tells us the real size. Not yet drawn anywhere -- plumbing
     * only, a progress indicator in the window is a follow-up. */
    ULONG progressBytesSoFar;
    ULONG progressTotalBytes;

    /* "Close"/"Save Image..."/"Save Audio..." menu -- built once, the
     * first time the window opens (mediaview_create_menu), torn down in
     * FS3EMediaView_Close (menu strips don't survive WM_CLOSE). */
    struct Menu *menu;
    APTR         menuVisualInfo;

    /* Poster's @user@instance for the currently shown attachment, as
     * passed to FS3EMediaView_ShowUrl()/ShowAudioUrl(); "" if the caller
     * didn't have one. One per channel -- imagePoster/audioPoster can
     * legitimately differ (an image from one post, audio from another,
     * both open in this same window at once) -- each one used only by its
     * own channel's "Save Image.../Save Audio..." to build a meaningful
     * default filename -- see fs3emediaview.c's
     * mediaview_build_image_default_name()/
     * mediaview_build_audio_default_name(). */
    char imagePoster[128];
    char audioPoster[128];

    /* Audio playback (mp3/wav/ogg attachments) -- counterpart to the
     * picture/BmImage fields above, driven by FS3EMediaView_ShowAudioUrl()
     * instead of ShowUrl(). hasAudio TRUE means tapeDeckGadget/sliderGadget
     * are live and there's a loaded/loading audio attachment (independent
     * of whether mv->image is ALSO loaded -- see this struct's own "two
     * channels" note above); FALSE means they're inert leftovers from a
     * previous audio attachment (GID_MEDIAVIEW_TAPEDECK clicks are ignored
     * while hasAudio is FALSE -- see FS3EMediaView_TapeDeckPressed).
     * audioRequestPort/audioReplyPort are cached from ShowAudioUrl's
     * caller (app->audioRequestPort/audioReplyPort, see friendsh3ep.h) so
     * later calls -- the fetch reply starting playback, TapeDeckPressed
     * sending Play/Pause/Stop -- don't need them threaded through every
     * function signature; NULL/harmless once hasAudio is FALSE again.
     * audioKey doubles as the FS3EAudioMessage key (see fs3eaudio.h) so
     * FS3EMediaView_OnAudioReply can tell a reply/notify is about the
     * attachment currently shown. */
    BOOL   hasAudio;
    ULONG  audioBackend;                        /* FS3EMVAudioBackend */
    char   audioLocalPath[FS3EAUDIO_PATH_SIZE]; /* filled once the fetch completes */
    char   audioKey[FS3EAUDIO_KEY_SIZE];
    char  *pendingAudioUrl; /* AllocVec'd; NULL when no audio fetch in flight */
    BOOL   audioLoading;
    BOOL   audioPlaying;
    BOOL   audioPaused;
    struct MsgPort *audioRequestPort;
    struct MsgPort *audioReplyPort;

    /* Seek support (GID_MEDIAVIEW_SLIDER) -- sliderGadget's ICA_TARGET
     * points at the main window's BOOPSI target (see fs3emediaview.c), so
     * every SLIDER_Level change reaches friendsh3ep.c's OM_NOTIFY dispatch
     * as one and the same event REGARDLESS of whether it was a user drag
     * or our own FS3EMediaView_OnAudioReply() pushing a PROGRESS update --
     * slider.gadget doesn't distinguish the two. audioSliderProgLevel is
     * the level WE last set programmatically (from PROGRESS/FINISHED);
     * FS3EMediaView_SliderMoved() compares the notify's level against it --
     * a match means it's just an echo of our own update, a mismatch means
     * the user actually moved the handle. audioTotalMs is the last known
     * track duration (from PROGRESS's fs3eam_TotalMs), needed to convert
     * the slider's 0..100 level back into an absolute ms position to seek
     * to. Both meaningless (left at 0) while !hasAudio. */
    ULONG  audioSliderProgLevel;
    ULONG  audioTotalMs;

    /* trick to avoid double WM_RETHINK calls */
    WORD last_w,last_h;

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
 * one (see TTIMELINE_LastHotSpotAcct), copied into mv->imagePoster for
 * "Save Image..." to use as the default filename -- NULL/"" is fine, Save
 * falls back to the cache's hash-id filename in that case. Never touches
 * the audio channel (mv->audioXXX) -- see FS3EMediaView's own "two
 * channels" comment.
 */
void FS3EMediaView_ShowUrl(FS3EMediaView *mv, const char *url, const char *posterAcct);

/*
 * Feed every FS3ENETQ_FETCH_IMAGE reply through here from friendsh3ep.c's
 * central reply switch (alongside the existing avatar/thumbnail-pipeline
 * handling, not instead of it -- the same download is useful to both).
 * Checked against both channels' own pending fetch (mv->pendingImageUrl,
 * mv->pendingAudioUrl) since either or both can have one in flight at
 * once; ignored if it matches neither.
 */
void FS3EMediaView_OnFetchReply(FS3EMediaView *mv, ULONG result,
                                 const FS3ENetFetchImageReply *reply);

/*
 * Feed every FS3ENETQ_FETCH_PROGRESS ping through here (see
 * FS3ENetFetchProgress in fs3enet.h) -- only ever sent for a request that
 * set fs3enf_WantProgress, which today is just FS3EMediaView_ShowUrl()'s own
 * fetch (the image channel only -- ShowAudioUrl()'s own fetch never
 * requests progress pings). Ignored unless key matches
 * mv->pendingImageUrl, same rule OnFetchReply() already applies. Just
 * records the numbers on mv for now; no progress indicator is drawn yet.
 */
void FS3EMediaView_OnFetchProgress(FS3EMediaView *mv, const char *key,
                                    ULONG bytesSoFar, ULONG totalBytes);

/*
 * Opens (or brings to front) the "FriendSh3ep Media" window for an audio
 * attachment (mp3/wav/ogg) -- counterpart to FS3EMediaView_ShowUrl() for
 * pictures, and independent of it: the image channel (mv->image), if any,
 * is left exactly as it was -- see FS3EMediaView's own "two channels"
 * comment. Fetches url the same way (FS3ENETQ_FETCH_IMAGE, keepOriginal=
 * TRUE, its own cache subdir -- see FS3E_CACHE_SUBDIR_AUDIO in fs3enet.h),
 * then once downloaded starts playback via audioRequestPort/audioReplyPort
 * (app->audioRequestPort/audioReplyPort, see friendsh3ep.h) -- but only for
 * a .mp3 URL (FS3EMV_AUDIO_MPEGA); .wav/.ogg are detected and shown as
 * "not supported yet" in the window instead of silently doing nothing (see
 * FS3EMVAudioBackend's doc comment). Stops whatever audio attachment was
 * previously playing in this same window first (the audio channel only
 * ever holds one track at a time, unlike image-vs-audio which coexist).
 * Returns immediately, same as ShowUrl(). No-op if url is NULL/empty.
 *
 * posterAcct: see FS3EMediaView_ShowUrl()'s doc comment, but copied into
 * mv->audioPoster (this channel's own poster field) instead.
 */
void FS3EMediaView_ShowAudioUrl(FS3EMediaView *mv, const char *url,
                                 const char *posterAcct,
                                 struct MsgPort *audioRequestPort,
                                 struct MsgPort *audioReplyPort);

/*
 * Feed every FS3EAudioMessage reply/notify (PLAY/PAUSE/STOP acks, the
 * eventual FINISHED, and periodic PROGRESS pings -- see fs3eaudio.h)
 * through here from friendsh3ep.c's audio reply drain. Updates mv's
 * playing/paused state, picGadget's status text (shown only while no
 * image is loaded -- see mediaview_push_picture()'s own comment), and
 * sliderGadget's position from PROGRESS. Ignored unless mv->hasAudio and
 * msg's key matches the attachment FS3EMediaView_ShowAudioUrl() is
 * currently showing.
 */
void FS3EMediaView_OnAudioReply(FS3EMediaView *mv, const FS3EAudioMessage *msg);

/*
 * GID_MEDIAVIEW_TAPEDECK's WMHI_GADGETUP handler -- reads TDECK_Mode off
 * tapeDeckGadget (whichever button was just pressed) and sends the
 * matching FS3EAUDIOQ_PLAY(restart)/PAUSE(toggle)/STOP command via the
 * cached audioRequestPort/audioReplyPort. No-op if mv->hasAudio is FALSE or
 * mv->audioBackend isn't FS3EMV_AUDIO_MPEGA (nothing playable loaded).
 * Call from FS3EMediaView_HandleInput() when result's WMHI_GADGETMASK is
 * GID_MEDIAVIEW_TAPEDECK.
 */
void FS3EMediaView_TapeDeckPressed(FS3EMediaView *mv);

/*
 * GID_MEDIAVIEW_SLIDER's OM_NOTIFY handler -- newLevel is sliderGadget's
 * current SLIDER_Level (0..100) at the time of the notify. No-op if it
 * matches audioSliderProgLevel (our own last PROGRESS/FINISHED update,
 * echoed back rather than a real user move -- see mv's own field comment),
 * or if mv->hasAudio is FALSE, mv->audioBackend isn't FS3EMV_AUDIO_MPEGA, or
 * audioTotalMs is still 0 (nothing seekable known yet). Otherwise treats it
 * as the user dragging the handle: converts newLevel back to an absolute
 * ms position against audioTotalMs and sends FS3EAUDIOQ_SEEK via the
 * cached audioRequestPort/audioReplyPort. Call from friendsh3ep.c's
 * GID_MEDIAVIEW_SLIDER case with the notify's SLIDER_Level tag data.
 */
void FS3EMediaView_SliderMoved(FS3EMediaView *mv, ULONG newLevel);

void  FS3EMediaView_Close(FS3EMediaView *mv);
BOOL  FS3EMediaView_HandleInput(FS3EMediaView *mv);
ULONG FS3EMediaView_GetSignalMask(FS3EMediaView *mv);

#endif /* FS3EMEDIAVIEW_H */
