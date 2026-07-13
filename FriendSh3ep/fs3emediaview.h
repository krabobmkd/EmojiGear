#ifndef FS3EMEDIAVIEW_H
#define FS3EMEDIAVIEW_H

/*
 * fs3emediaview.h - "FriendSh3ep Media" full-size attachment viewer.
 *
 * $VER: fs3emediaview.h 1.0 (13.07.2026)
 * Copyright (C) 2026 FriendSh3ep contributors. All rights reserved.
 *
 * Opened by clicking a toot's media preview rectangle (TTL_HOT_IMAGE --
 * see friendsh3ep.c's TTIMELINE_HotSpotNotify switch). Unlike every other
 * sub-window in this app (window.class + layout.gadget), this is a bare
 * Intuition window: there's nothing to lay out, just one picture.datatype
 * image blitted at its natural size -- no scale treatment, see bmimage.h.
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
 * open reuses it (brought to front, image replaced) instead of opening a
 * new one each time.
 */

#include <exec/types.h>
#include <intuition/intuition.h>

#include "bmimage.h"
#include "network_fs3e/fs3enet.h"

typedef struct FS3EMediaView {
    struct Window *window;
    BmImage        image;
    char          *pendingUrl; /* AllocVec'd; NULL when nothing in flight */
    BOOL           loading;
    LONG           left, top;  /* remembered across closes (not persisted to disk) */
} FS3EMediaView;

/* Zeroes mv. Nothing to allocate up front -- the window and image are
 * created lazily by the first FS3EMediaView_ShowUrl() call. */
void FS3EMediaView_Init(FS3EMediaView *mv);

/* Frees the loaded image (if any) and closes the window if still open.
 * Call once at app teardown. */
void FS3EMediaView_Dispose(FS3EMediaView *mv);

/*
 * Opens (or brings to front) the "FriendSh3ep Media" window and starts
 * loading url's image -- see the header comment above for what "url"
 * means and the cache-hit/re-fetch behaviour. Returns immediately; the
 * image itself appears once FS3EMediaView_OnFetchReply() delivers it.
 * No-op if url is NULL/empty.
 */
void FS3EMediaView_ShowUrl(FS3EMediaView *mv, const char *url);

/*
 * Feed every FS3ENETQ_FETCH_IMAGE reply through here from friendsh3ep.c's
 * central reply switch (alongside the existing avatar/thumbnail-pipeline
 * handling, not instead of it -- the same download is useful to both).
 * Ignored unless reply->fs3enf_Key matches the URL FS3EMediaView_ShowUrl()
 * is currently waiting for.
 */
void FS3EMediaView_OnFetchReply(FS3EMediaView *mv, ULONG result,
                                 const FS3ENetFetchImageReply *reply);

void  FS3EMediaView_Close(FS3EMediaView *mv);
BOOL  FS3EMediaView_HandleInput(FS3EMediaView *mv);
ULONG FS3EMediaView_GetSignalMask(FS3EMediaView *mv);

#endif /* FS3EMEDIAVIEW_H */
